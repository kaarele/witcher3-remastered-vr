// Geometry and assembly checks for src/capture360.cpp: synthetic views of a
// procedural sky (directions only, no parallax) must reassemble into the
// same equirectangular picture, in mono and in ODS mode, and solved gains
// must undo a per-view exposure change.

#include "../src/capture360.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace c360 = w3vr::capture360;

namespace {

constexpr float kPi = 3.14159265358979323846f;
int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

struct V3 {
    float x, y, z;
};

// Procedural world colour (BGR) from a direction: smooth gradients plus a
// lat/lon grid so misplacement shows up as error.
bool g_world_grid = true;

void world(const V3& d, float out[3]) {
    const float lon = std::atan2(d.x, -d.z);
    const float lat = std::asin(std::fmax(-1.0f, std::fmin(1.0f, d.y)));
    const float grid = g_world_grid && (std::fabs(std::sin(lon * 12.0f)) < 0.08f ||
        std::fabs(std::sin(lat * 12.0f)) < 0.08f) ? 60.0f : 0.0f;
    out[0] = 100.0f + 80.0f * std::sin(lat) + grid;
    out[1] = 120.0f + 60.0f * std::cos(lon) * std::cos(lat) + grid;
    out[2] = 110.0f + 70.0f * std::sin(lon * 2.0f) * std::cos(lat) + grid;
}

// View space -> world for a camera at (yaw right-positive, pitch up).
V3 to_world(const V3& v, float yaw, float pitch) {
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const V3 a{v.x, cp * v.y - sp * v.z, sp * v.y + cp * v.z};
    const float a_yaw = -yaw;  // OpenXR yaw is left-positive
    const float cy = std::cos(a_yaw), sy = std::sin(a_yaw);
    return {cy * a.x + sy * a.z, a.y, -sy * a.x + cy * a.z};
}

c360::View render_view(const c360::Step& step, const c360::Fov& fov,
    uint32_t w, uint32_t h, int eye, float gain, uint32_t x0 = 0,
    uint32_t x1 = 0) {
    c360::View view{};
    view.yaw_deg = step.yaw_deg;
    view.pitch_deg = step.pitch_deg;
    view.fov = fov;
    view.full_width = w;
    view.full_height = h;
    view.x0 = x0;
    view.width = (x1 > x0 ? x1 : w) - x0;
    view.height = h;
    view.eye = eye;
    view.band = step.band;
    view.bgra.resize(static_cast<size_t>(view.width) * h * 4);
    const float tl = std::tan(fov.left), tr = std::tan(fov.right);
    const float tu = std::tan(fov.up), td = std::tan(fov.down);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t lx = 0; lx < view.width; ++lx) {
            const uint32_t x = lx + x0;
            const float tx = tl + (x + 0.5f) / w * (tr - tl);
            const float ty = tu - (y + 0.5f) / h * (tu - td);
            const float len = std::sqrt(tx * tx + ty * ty + 1.0f);
            const V3 d = to_world({tx / len, ty / len, -1.0f / len},
                step.yaw_deg * kPi / 180.0f, step.pitch_deg * kPi / 180.0f);
            float c[3];
            world(d, c);
            uint8_t* p = view.bgra.data() + (static_cast<size_t>(y) * view.width + lx) * 4;
            for (int i = 0; i < 3; ++i) {
                // Exposure applied in sRGB-encoded space as gain^(1/2.2).
                const float v = c[i] * std::pow(gain, 1.0f / 2.2f);
                p[i] = static_cast<uint8_t>(std::fmin(255.0f, std::fmax(0.0f, v + 0.5f)));
            }
            p[3] = 255;
        }
    }
    return view;
}

double psnr(const c360::Image& image) {
    double se = 0.0;
    size_t n = 0;
    for (uint32_t y = 0; y < image.height; ++y) {
        const float lat = (0.5f - (y + 0.5f) / image.height) * kPi;
        for (uint32_t x = 0; x < image.width; ++x) {
            const float lon = ((x + 0.5f) / image.width) * 2.0f * kPi - kPi;
            const V3 d{std::sin(lon) * std::cos(lat), std::sin(lat),
                -std::cos(lon) * std::cos(lat)};
            float c[3];
            world(d, c);
            const uint8_t* p = image.bgra.data() + (static_cast<size_t>(y) * image.width + x) * 4;
            for (int i = 0; i < 3; ++i) {
                const double e = static_cast<double>(p[i]) - std::fmin(255.0f, c[i]);
                se += e * e;
                ++n;
            }
        }
    }
    const double mse = se / static_cast<double>(n);
    return 10.0 * std::log10(255.0 * 255.0 / std::fmax(mse, 1e-9));
}

}  // namespace

int main() {
    // Quest-3-like asymmetric left-eye FOV (radians).
    const c360::Fov fov{-0.942f, 0.698f, 0.768f, -0.890f};
    const uint32_t w = 720, h = 760;

    // Step plans.
    const auto mono_plan = c360::plan_steps(c360::Mode::Mono, {});
    check(mono_plan.size() == 26, "mono plan has 8 + 2x6 + 2x3 steps");
    c360::PlanOptions ods_options{};
    ods_options.ods_step_deg = 5.0f;
    const auto ods_plan = c360::plan_steps(c360::Mode::StereoOds, ods_options);
    size_t band_steps = 0;
    for (const auto& s : ods_plan) {
        band_steps += s.band ? 1 : 0;
    }
    check(band_steps == 72, "ODS plan has 360/5 band steps");

    // Strip columns: symmetric FOV, 10 degrees around the centre.
    uint32_t sx0 = 0, sx1 = 0;
    c360::strip_columns({-0.785398f, 0.785398f, 0.785398f, -0.785398f}, 1000,
        5.0f, 0, sx0, sx1);
    check(sx0 <= 457 && sx0 >= 454 && sx1 >= 544 && sx1 <= 547,
        "strip columns of +-5 deg in a 90 deg / 1000 px view");

    // Mono: views with random exposure, gains solved, then assembled.
    std::vector<c360::View> mono;
    srand(7);
    for (size_t i = 0; i < mono_plan.size(); ++i) {
        const float gain = 0.85f + 0.3f * static_cast<float>(rand() % 1000) / 1000.0f;
        mono.push_back(render_view(mono_plan[i], fov, w, h, -1, gain));
    }
    c360::solve_gains(mono, 256);
    std::vector<const c360::View*> mono_ptrs;
    for (const auto& v : mono) {
        mono_ptrs.push_back(&v);
    }
    c360::AssembleOptions exact{};
    exact.sharpen = 0.0f;  // compare against the world without the unsharp mask
    const auto mono_image = c360::assemble_mono(mono_ptrs, 1024, 8, exact);
    const double mono_psnr = psnr(mono_image);
    std::printf("mono psnr %.2f dB\n", mono_psnr);
    check(mono_psnr > 27.0, "mono assembly matches the world (>27 dB)");

    // ODS (scene at infinity, so both eyes equal the world): strips + poles.
    std::vector<c360::View> ods;
    for (const auto& step : ods_plan) {
        if (step.band) {
            uint32_t x0 = 0, x1 = 0;
            c360::strip_columns(fov, w, ods_options.ods_step_deg * 0.5f + 1.5f, 4, x0, x1);
            ods.push_back(render_view(step, fov, w, h, 0, 1.0f, x0, x1));
        } else {
            ods.push_back(render_view(step, fov, w, h, -1, 1.0f));
        }
    }
    c360::solve_gains(ods, 256);
    std::vector<const c360::View*> band_ptrs, pole_ptrs;
    for (const auto& v : ods) {
        (v.band ? band_ptrs : pole_ptrs).push_back(&v);
    }
    const auto ods_image = c360::assemble_ods_eye(band_ptrs, pole_ptrs,
        ods_options.ods_step_deg, 1024, 8, exact);
    const double ods_psnr = psnr(ods_image);
    std::printf("ods psnr %.2f dB\n", ods_psnr);
    check(ods_psnr > 27.0, "ODS assembly matches the world (>27 dB)");

    const auto tb = c360::stack_top_bottom(ods_image, ods_image);
    check(tb.width == 1024 && tb.height == 1024, "top-bottom stack size");

    // Single-band (old) path still matches.
    c360::AssembleOptions single = exact;
    single.two_band = false;
    single.bicubic = false;
    const double single_psnr = psnr(c360::assemble_mono(mono_ptrs, 1024, 8, single));
    std::printf("mono single-band psnr %.2f dB\n", single_psnr);
    check(single_psnr > 27.0, "single-band mono matches the world (>27 dB)");

    // Seams: uncorrected exposure steps (no gain solve) on a smooth world.
    // Two-band blending must spread them: the largest column-to-column jump of
    // the equator's mean brightness gets smaller.
    g_world_grid = false;
    std::vector<c360::View> raw;
    srand(11);
    for (size_t i = 0; i < mono_plan.size(); ++i) {
        const float gain = 0.75f + 0.5f * static_cast<float>(rand() % 1000) / 1000.0f;
        raw.push_back(render_view(mono_plan[i], fov, w, h, -1, gain));
    }
    std::vector<const c360::View*> raw_ptrs;
    for (const auto& v : raw) {
        raw_ptrs.push_back(&v);
    }
    auto max_step = [](const c360::Image& image) {
        std::vector<double> column(image.width, 0.0);
        for (uint32_t y = image.height * 2 / 5; y < image.height * 3 / 5; ++y) {
            for (uint32_t x = 0; x < image.width; ++x) {
                const uint8_t* p = image.bgra.data() + (static_cast<size_t>(y) * image.width + x) * 4;
                column[x] += 0.0722 * p[0] + 0.7152 * p[1] + 0.2126 * p[2];
            }
        }
        double worst = 0.0;
        for (uint32_t x = 0; x + 1 < image.width; ++x) {
            worst = std::fmax(worst, std::fabs(column[x + 1] - column[x]) / (image.height / 5));
        }
        return worst;
    };
    c360::AssembleOptions narrow_only = exact;
    narrow_only.two_band = true;
    const double step_two = max_step(c360::assemble_mono(raw_ptrs, 1024, 8, narrow_only));
    c360::AssembleOptions hard = exact;
    hard.two_band = false;
    const double step_single = max_step(c360::assemble_mono(raw_ptrs, 1024, 8, hard));
    std::printf("exposure seam step: two-band %.2f, single %.2f\n", step_two, step_single);
    check(step_two < 2.0, "two-band exposure seams are soft (<2 levels per column)");
    g_world_grid = true;

    // ODS band-to-pole seam: pole views 15% brighter than the band strips
    // (no gain solve). The largest row-to-row jump of the mean brightness must
    // shrink with two-band blending (the reported sky streak).
    {
        g_world_grid = false;
        std::vector<c360::View> seam_views;
        for (const auto& step : ods_plan) {
            if (step.band) {
                uint32_t x0 = 0, x1 = 0;
                c360::strip_columns(fov, w, ods_options.ods_step_deg * 0.5f + 1.5f, 4, x0, x1);
                seam_views.push_back(render_view(step, fov, w, h, 0, 1.0f, x0, x1));
            } else {
                seam_views.push_back(render_view(step, fov, w, h, -1, 1.15f));
            }
        }
        std::vector<const c360::View*> b, p;
        for (const auto& v : seam_views) {
            (v.band ? b : p).push_back(&v);
        }
        auto row_jump = [](const c360::Image& image) {
            double worst = 0.0;
            double previous = -1.0;
            for (uint32_t y = 0; y < image.height / 2; ++y) {
                double sum = 0.0;
                for (uint32_t x = 0; x < image.width; ++x) {
                    const uint8_t* q = image.bgra.data() + (static_cast<size_t>(y) * image.width + x) * 4;
                    sum += 0.0722 * q[0] + 0.7152 * q[1] + 0.2126 * q[2];
                }
                const double mean = sum / image.width;
                if (previous >= 0.0) {
                    worst = std::fmax(worst, std::fabs(mean - previous));
                }
                previous = mean;
            }
            return worst;
        };
        c360::AssembleOptions one = exact;
        one.two_band = false;
        const double jump_single = row_jump(c360::assemble_ods_eye(b, p,
            ods_options.ods_step_deg, 1024, 8, one));
        const double jump_two = row_jump(c360::assemble_ods_eye(b, p,
            ods_options.ods_step_deg, 1024, 8, exact));
        std::printf("ODS band/pole seam row jump: two-band %.2f, single %.2f\n",
            jump_two, jump_single);
        check(jump_two < 2.0, "band/pole seam stays soft (<2 levels per row)");
        g_world_grid = true;
    }

    // rotate_left: the forward direction (centre column) moves 45 deg left.
    c360::Image ramp{};
    ramp.width = 360;
    ramp.height = 2;
    ramp.bgra.assign(360 * 2 * 4, 0);
    for (uint32_t x = 0; x < 360; ++x) {
        ramp.bgra[x * 4] = static_cast<uint8_t>(x % 256);
    }
    c360::rotate_left(ramp, 45.0f);
    check(ramp.bgra[(180 - 45) * 4] == 180, "rotate_left moves the centre 45 deg left");

    if (g_failures == 0) {
        std::printf("capture360 tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
