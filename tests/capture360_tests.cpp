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
void world(const V3& d, float out[3]) {
    const float lon = std::atan2(d.x, -d.z);
    const float lat = std::asin(std::fmax(-1.0f, std::fmin(1.0f, d.y)));
    const float grid = (std::fabs(std::sin(lon * 12.0f)) < 0.08f ||
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
    const auto mono_image = c360::assemble_mono(mono_ptrs, 1024, 8);
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
        ods_options.ods_step_deg, 1024, 8);
    const double ods_psnr = psnr(ods_image);
    std::printf("ods psnr %.2f dB\n", ods_psnr);
    check(ods_psnr > 27.0, "ODS assembly matches the world (>27 dB)");

    const auto tb = c360::stack_top_bottom(ods_image, ods_image);
    check(tb.width == 1024 && tb.height == 1024, "top-bottom stack size");

    if (g_failures == 0) {
        std::printf("capture360 tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
