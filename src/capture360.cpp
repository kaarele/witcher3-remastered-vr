#include "capture360.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <thread>

namespace w3vr::capture360 {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

struct Vec3 {
    float x{};
    float y{};
    float z{};
};

Vec3 direction(float lon, float lat) {
    const float c = std::cos(lat);
    return {std::sin(lon) * c, std::sin(lat), -std::cos(lon) * c};
}

// View space of a camera looking at (yaw, pitch): v = Rx(-pitch) * Ry(yaw) * d,
// where Ry is OpenXR's left-positive yaw (the camera's OpenXR yaw is -yaw,
// since longitude grows to the right).

struct Projection {
    float fx{};       // full-image pixel coordinate (pixel centres at .0)
    float fy{};
    bool valid{};
};

// Per-view constants, computed once per assembly instead of per pixel.
struct Prepared {
    const View* view{};
    float tl{}, tr{}, tu{}, td{};
    float cy{}, sy{}, cp{}, sp{};
    Vec3 forward{};
    float cos_limit{};  // dot(d, forward) below this is outside the image
};

Prepared prepare(const View& view) {
    Prepared p{};
    p.view = &view;
    p.tl = std::tan(view.fov.left);
    p.tr = std::tan(view.fov.right);
    p.tu = std::tan(view.fov.up);
    p.td = std::tan(view.fov.down);
    const float yaw = view.yaw_deg * kDegToRad;
    const float pitch = view.pitch_deg * kDegToRad;
    p.cy = std::cos(yaw);
    p.sy = std::sin(yaw);
    p.cp = std::cos(pitch);
    p.sp = std::sin(pitch);
    p.forward = direction(yaw, pitch);
    const float tx = std::max(std::fabs(p.tl), std::fabs(p.tr));
    const float ty = std::max(std::fabs(p.tu), std::fabs(p.td));
    p.cos_limit = 1.0f / std::sqrt(1.0f + tx * tx + ty * ty) - 0.01f;
    return p;
}

std::vector<Prepared> prepare_all(const std::vector<const View*>& views) {
    std::vector<Prepared> prepared;
    prepared.reserve(views.size());
    for (const View* view : views) {
        prepared.push_back(prepare(*view));
    }
    return prepared;
}

Projection project(const Prepared& c, const Vec3& d) {
    Projection result{};
    if (d.x * c.forward.x + d.y * c.forward.y + d.z * c.forward.z < c.cos_limit) {
        return result;
    }
    const View& view = *c.view;
    // v = Rx(-pitch) * Ry(yaw) * d (see to_view).
    const Vec3 a{c.cy * d.x + c.sy * d.z, d.y, -c.sy * d.x + c.cy * d.z};
    const Vec3 v{a.x, c.cp * a.y + c.sp * a.z, -c.sp * a.y + c.cp * a.z};
    if (v.z >= -1e-4f || c.tr <= c.tl || c.tu <= c.td) {
        return result;
    }
    const float tx = v.x / -v.z;
    const float ty = v.y / -v.z;
    result.fx = (tx - c.tl) / (c.tr - c.tl) * static_cast<float>(view.full_width) - 0.5f;
    result.fy = (c.tu - ty) / (c.tu - c.td) * static_cast<float>(view.full_height) - 0.5f;
    result.valid = true;
    return result;
}

// Distance (pixels) from the stored region's edge; negative = outside.
float edge_distance(const View& view, const Projection& p) {
    const float lx = p.fx - static_cast<float>(view.x0);
    const float dx = std::min(lx, static_cast<float>(view.width) - 1.0f - lx);
    const float dy = std::min(p.fy, static_cast<float>(view.height) - 1.0f - p.fy);
    return std::min(dx, dy);
}

float vertical_edge_distance(const View& view, const Projection& p) {
    return std::min(p.fy, static_cast<float>(view.height) - 1.0f - p.fy);
}

float smoothstep01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// Bilinear BGRA sample (as floats) at a full-image coordinate, with the
// view's exposure gain applied unless apply_gain is false.
void sample(const View& view, const Projection& p, float out[3],
    bool apply_gain = true) {
    const float lx = std::clamp(p.fx - static_cast<float>(view.x0), 0.0f,
        static_cast<float>(view.width) - 1.0f);
    const float ly = std::clamp(p.fy, 0.0f, static_cast<float>(view.height) - 1.0f);
    const uint32_t x0 = static_cast<uint32_t>(lx);
    const uint32_t y0 = static_cast<uint32_t>(ly);
    const uint32_t x1 = std::min(x0 + 1, view.width - 1);
    const uint32_t y1 = std::min(y0 + 1, view.height - 1);
    const float wx = lx - static_cast<float>(x0);
    const float wy = ly - static_cast<float>(y0);
    const uint8_t* row0 = view.bgra.data() + static_cast<size_t>(y0) * view.width * 4;
    const uint8_t* row1 = view.bgra.data() + static_cast<size_t>(y1) * view.width * 4;
    // Gains are solved on sRGB-encoded luminance; applying gain^(1/2.2) to
    // the encoded values is close enough for exposure differences of a few
    // percent between neighbouring directions.
    const float g = apply_gain
        ? std::pow(std::max(view.gain, 0.01f), 1.0f / 2.2f)
        : 1.0f;
    for (int c = 0; c < 3; ++c) {
        const float top = row0[x0 * 4 + c] +
            (static_cast<float>(row0[x1 * 4 + c]) - row0[x0 * 4 + c]) * wx;
        const float bottom = row1[x0 * 4 + c] +
            (static_cast<float>(row1[x1 * 4 + c]) - row1[x0 * 4 + c]) * wx;
        out[c] = (top + (bottom - top) * wy) * g;
    }
}

float luminance(const View& view, const Projection& p) {
    float c[3];
    sample(view, p, c, false);
    // BGRA: c[0]=B, c[1]=G, c[2]=R.
    return 0.0722f * c[0] + 0.7152f * c[1] + 0.2126f * c[2];
}

template <typename RowFn>
void parallel_rows(uint32_t rows, unsigned threads, RowFn fn) {
    threads = std::max(1u, std::min(threads, 64u));
    std::atomic<uint32_t> next{0};
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
        workers.emplace_back([&]() {
            for (uint32_t y = next.fetch_add(1); y < rows; y = next.fetch_add(1)) {
                fn(y);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
}

void store(uint8_t* pixel, const float c[3]) {
    for (int i = 0; i < 3; ++i) {
        pixel[i] = static_cast<uint8_t>(std::clamp(c[i] + 0.5f, 0.0f, 255.0f));
    }
    pixel[3] = 255;
}

float feather_px(const View& view) {
    return std::max(8.0f, 0.08f * static_cast<float>(
        std::min(view.full_width, view.full_height)));
}

float catmull_rom(float t, int i) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    switch (i) {
    case 0: return -0.5f * t3 + t2 - 0.5f * t;
    case 1: return 1.5f * t3 - 2.5f * t2 + 1.0f;
    case 2: return -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    default: return 0.5f * t3 - 0.5f * t2;
    }
}

// Catmull-Rom (bicubic) BGRA sample with the view's exposure gain.
void sample_bicubic(const View& view, const Projection& p, float out[3]) {
    const float lx = std::clamp(p.fx - static_cast<float>(view.x0), 0.0f,
        static_cast<float>(view.width) - 1.0f);
    const float ly = std::clamp(p.fy, 0.0f, static_cast<float>(view.height) - 1.0f);
    const int ix = static_cast<int>(lx);
    const int iy = static_cast<int>(ly);
    const float tx = lx - static_cast<float>(ix);
    const float ty = ly - static_cast<float>(iy);
    float wx[4];
    float wy[4];
    for (int i = 0; i < 4; ++i) {
        wx[i] = catmull_rom(tx, i);
        wy[i] = catmull_rom(ty, i);
    }
    const int max_x = static_cast<int>(view.width) - 1;
    const int max_y = static_cast<int>(view.height) - 1;
    float sum[3]{};
    for (int j = 0; j < 4; ++j) {
        const int y = std::clamp(iy - 1 + j, 0, max_y);
        const uint8_t* row = view.bgra.data() + static_cast<size_t>(y) * view.width * 4;
        float rs[3]{};
        for (int i = 0; i < 4; ++i) {
            const int x = std::clamp(ix - 1 + i, 0, max_x);
            for (int c = 0; c < 3; ++c) {
                rs[c] += wx[i] * row[x * 4 + c];
            }
        }
        for (int c = 0; c < 3; ++c) {
            sum[c] += wy[j] * rs[c];
        }
    }
    const float g = std::pow(std::max(view.gain, 0.01f), 1.0f / 2.2f);
    for (int c = 0; c < 3; ++c) {
        out[c] = std::clamp(sum[c], 0.0f, 255.0f) * g;
    }
}

void sample_detail(const View& view, const Projection& p, float out[3],
    bool bicubic) {
    if (bicubic) {
        sample_bicubic(view, p, out);
    } else {
        sample(view, p, out);
    }
}

// Feather-weighted blend of full views at direction d. Returns the summed
// weight (0 = not covered).
float blend_views(const std::vector<Prepared>& views, const Vec3& d,
    float color[3]) {
    float sum[3]{};
    float weight_sum = 0.0f;
    for (const Prepared& prepared : views) {
        const Projection p = project(prepared, d);
        if (!p.valid) {
            continue;
        }
        const View& view = *prepared.view;
        const float edge = edge_distance(view, p);
        if (edge < 0.0f) {
            continue;
        }
        const float w = std::max(1e-4f, smoothstep01(edge / feather_px(view)));
        float c[3];
        sample(view, p, c);
        for (int i = 0; i < 3; ++i) {
            sum[i] += c[i] * w;
        }
        weight_sum += w;
    }
    if (weight_sum > 0.0f) {
        for (int i = 0; i < 3; ++i) {
            color[i] = sum[i] / weight_sum;
        }
    }
    return weight_sum;
}

// Per-view low-pass image for two-band blending: the view box-averaged into
// cells of kLowCell pixels and blurred, sampled bilinearly. A view's detail
// is (bicubic sample - low sample).
constexpr uint32_t kLowCell = 16;

struct LowImage {
    uint32_t width{};
    uint32_t height{};
    std::vector<float> bgr;
};

LowImage make_low(const View& view) {
    LowImage low{};
    low.width = std::max(1u, (view.width + kLowCell - 1) / kLowCell);
    low.height = std::max(1u, (view.height + kLowCell - 1) / kLowCell);
    low.bgr.assign(static_cast<size_t>(low.width) * low.height * 3, 0.0f);
    const float g = std::pow(std::max(view.gain, 0.01f), 1.0f / 2.2f);
    for (uint32_t cy = 0; cy < low.height; ++cy) {
        for (uint32_t cx = 0; cx < low.width; ++cx) {
            double acc[3]{};
            uint32_t n = 0;
            for (uint32_t y = cy * kLowCell; y < std::min(view.height, (cy + 1) * kLowCell); ++y) {
                const uint8_t* row = view.bgra.data() + static_cast<size_t>(y) * view.width * 4;
                for (uint32_t x = cx * kLowCell; x < std::min(view.width, (cx + 1) * kLowCell); ++x) {
                    for (int c = 0; c < 3; ++c) {
                        acc[c] += row[x * 4 + c];
                    }
                    ++n;
                }
            }
            for (int c = 0; c < 3; ++c) {
                low.bgr[(static_cast<size_t>(cy) * low.width + cx) * 3 + c] =
                    n > 0 ? static_cast<float>(acc[c] / n) * g : 0.0f;
            }
        }
    }
    // Two passes of a 3x3 binomial blur (edges clamp).
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<float> source = low.bgr;
        for (uint32_t y = 0; y < low.height; ++y) {
            for (uint32_t x = 0; x < low.width; ++x) {
                float acc[3]{};
                float wsum = 0.0f;
                for (int j = -1; j <= 1; ++j) {
                    const uint32_t sy = static_cast<uint32_t>(std::clamp(
                        static_cast<int>(y) + j, 0, static_cast<int>(low.height) - 1));
                    for (int i = -1; i <= 1; ++i) {
                        const uint32_t sx = static_cast<uint32_t>(std::clamp(
                            static_cast<int>(x) + i, 0, static_cast<int>(low.width) - 1));
                        const float k = (j == 0 ? 2.0f : 1.0f) * (i == 0 ? 2.0f : 1.0f);
                        for (int c = 0; c < 3; ++c) {
                            acc[c] += k * source[(static_cast<size_t>(sy) * low.width + sx) * 3 + c];
                        }
                        wsum += k;
                    }
                }
                for (int c = 0; c < 3; ++c) {
                    low.bgr[(static_cast<size_t>(y) * low.width + x) * 3 + c] = acc[c] / wsum;
                }
            }
        }
    }
    return low;
}

void sample_low(const LowImage& low, const View& view, const Projection& p,
    float out[3]) {
    const float lx = std::clamp((p.fx - static_cast<float>(view.x0) + 0.5f) /
        kLowCell - 0.5f, 0.0f, static_cast<float>(low.width) - 1.0f);
    const float ly = std::clamp((p.fy + 0.5f) / kLowCell - 0.5f, 0.0f,
        static_cast<float>(low.height) - 1.0f);
    const uint32_t x0 = static_cast<uint32_t>(lx);
    const uint32_t y0 = static_cast<uint32_t>(ly);
    const uint32_t x1 = std::min(x0 + 1, low.width - 1);
    const uint32_t y1 = std::min(y0 + 1, low.height - 1);
    const float tx = lx - static_cast<float>(x0);
    const float ty = ly - static_cast<float>(y0);
    for (int c = 0; c < 3; ++c) {
        const float a = low.bgr[(static_cast<size_t>(y0) * low.width + x0) * 3 + c];
        const float b = low.bgr[(static_cast<size_t>(y0) * low.width + x1) * 3 + c];
        const float e = low.bgr[(static_cast<size_t>(y1) * low.width + x0) * 3 + c];
        const float f = low.bgr[(static_cast<size_t>(y1) * low.width + x1) * 3 + c];
        out[c] = (a + (b - a) * tx) * (1.0f - ty) + (e + (f - e) * tx) * ty;
    }
}

// Two-band blend of full views at d: detail (sample - low) from the most
// central view (weight (edge / half size)^6, so neighbours only mix in a thin
// zone at the seam) plus the low band feathered over options.wide_feather of
// each view, so brightness, bloom or haze differences between views fade out
// smoothly instead of showing as a step or streak. Returns false if no view
// covers d.
bool blend_two_band(const std::vector<Prepared>& views,
    const std::vector<LowImage>& lows, const Vec3& d,
    const AssembleOptions& options, float color[3]) {
    struct Hit {
        size_t index;
        Projection p;
        float edge;
    };
    Hit hits[16];
    int count = 0;
    for (size_t i = 0; i < views.size(); ++i) {
        const Projection p = project(views[i], d);
        if (!p.valid) {
            continue;
        }
        const float edge = edge_distance(*views[i].view, p);
        if (edge < 0.0f || count == 16) {
            continue;
        }
        hits[count++] = {i, p, edge};
    }
    if (count == 0) {
        return false;
    }
    float wn[16];
    float max_wn = 0.0f;
    for (int i = 0; i < count; ++i) {
        const View& view = *views[hits[i].index].view;
        const float half = 0.5f * static_cast<float>(std::min(view.width, view.height));
        const float r = std::clamp(hits[i].edge / std::max(half, 1.0f), 0.0f, 1.0f);
        const float r2 = r * r;
        wn[i] = r2 * r2 * r2 + 1e-12f;
        max_wn = std::max(max_wn, wn[i]);
    }
    float high[3]{};
    float low_sum[3]{};
    float wn_sum = 0.0f;
    float ww_sum = 0.0f;
    for (int i = 0; i < count; ++i) {
        const View& view = *views[hits[i].index].view;
        const LowImage& lowimage = lows[hits[i].index];
        float lo[3];
        sample_low(lowimage, view, hits[i].p, lo);
        const float ww = std::max(1e-4f, smoothstep01(hits[i].edge / std::max(8.0f,
            options.wide_feather * static_cast<float>(
                std::min(view.full_width, view.full_height)))));
        for (int k = 0; k < 3; ++k) {
            low_sum[k] += lo[k] * ww;
        }
        ww_sum += ww;
        if (wn[i] >= 0.02f * max_wn) {
            float c[3];
            sample_detail(view, hits[i].p, c, options.bicubic);
            for (int k = 0; k < 3; ++k) {
                high[k] += (c[k] - lo[k]) * wn[i];
            }
            wn_sum += wn[i];
        }
    }
    for (int k = 0; k < 3; ++k) {
        color[k] = high[k] / wn_sum + low_sum[k] / ww_sum;
    }
    return true;
}

std::vector<LowImage> make_lows(const std::vector<Prepared>& views,
    unsigned threads) {
    std::vector<LowImage> lows(views.size());
    parallel_rows(static_cast<uint32_t>(views.size()), threads, [&](uint32_t i) {
        lows[i] = make_low(*views[i].view);
    });
    return lows;
}

// Mild unsharp mask (3x3 binomial blur; longitude wraps, rows clamp).
void unsharp(Image& image, float amount, unsigned threads) {
    if (amount <= 0.0f || image.width < 3 || image.height < 3) {
        return;
    }
    const std::vector<uint8_t> source = image.bgra;
    const uint32_t w = image.width;
    const uint32_t h = image.height;
    parallel_rows(h, threads, [&](uint32_t y) {
        const uint32_t ys[3] = {y > 0 ? y - 1 : 0, y, std::min(y + 1, h - 1)};
        const float wy[3] = {1.0f, 2.0f, 1.0f};
        uint8_t* out = image.bgra.data() + static_cast<size_t>(y) * w * 4;
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t xs[3] = {(x + w - 1) % w, x, (x + 1) % w};
            float blur[3]{};
            for (int j = 0; j < 3; ++j) {
                const uint8_t* row = source.data() + static_cast<size_t>(ys[j]) * w * 4;
                for (int i = 0; i < 3; ++i) {
                    const float k = wy[j] * wy[i] / 16.0f;
                    for (int c = 0; c < 3; ++c) {
                        blur[c] += k * row[xs[i] * 4 + c];
                    }
                }
            }
            const uint8_t* orig = source.data() + (static_cast<size_t>(y) * w + x) * 4;
            for (int c = 0; c < 3; ++c) {
                out[x * 4 + c] = static_cast<uint8_t>(std::clamp(
                    orig[c] + amount * (orig[c] - blur[c]) + 0.5f, 0.0f, 255.0f));
            }
        }
    });
}

}  // namespace

std::vector<Step> plan_steps(Mode mode, const PlanOptions& options) {
    std::vector<Step> steps;
    auto ring = [&](float pitch, int count, float phase) {
        for (int i = 0; i < count; ++i) {
            steps.push_back({phase + 360.0f * static_cast<float>(i) / count,
                pitch, false, false});
        }
    };
    if (mode == Mode::Mono) {
        ring(0.0f, 8, 0.0f);
    } else {
        const float step = std::clamp(options.ods_step_deg, 1.0f, 15.0f);
        const int count = std::max(24, static_cast<int>(std::lround(360.0f / step)));
        for (int i = 0; i < count; ++i) {
            steps.push_back({360.0f * static_cast<float>(i) / count, 0.0f, true, true});
        }
    }
    ring(options.pole_pitch_deg, 6, 0.0f);
    ring(options.zenith_pitch_deg, 3, 0.0f);
    ring(-options.pole_pitch_deg, 6, 0.0f);
    ring(-options.zenith_pitch_deg, 3, 0.0f);
    return steps;
}

void strip_columns(const Fov& fov, uint32_t full_width, float half_angle_deg,
    uint32_t margin_px, uint32_t& x0, uint32_t& x1) {
    const float tl = std::tan(fov.left);
    const float tr = std::tan(fov.right);
    const float h = std::tan(half_angle_deg * kDegToRad);
    const float w = static_cast<float>(full_width);
    const float a = (-h - tl) / (tr - tl) * w;
    const float b = (h - tl) / (tr - tl) * w;
    const int64_t lo = static_cast<int64_t>(std::floor(a)) - margin_px;
    const int64_t hi = static_cast<int64_t>(std::ceil(b)) + margin_px;
    x0 = static_cast<uint32_t>(std::clamp<int64_t>(lo, 0, full_width));
    x1 = static_cast<uint32_t>(std::clamp<int64_t>(hi, x0, full_width));
}

void solve_gains(std::vector<View>& views, uint32_t sample_width) {
    const size_t n = views.size();
    if (n < 2) {
        return;
    }
    for (auto& view : views) {
        view.gain = 1.0f;
    }
    // Pairwise overlap means of luminance (a: view i, b: view j).
    std::vector<double> sum_a(n * n), sum_b(n * n), count(n * n);
    const uint32_t sw = std::max(64u, sample_width);
    const uint32_t sh = sw / 2;
    std::vector<size_t> covering;
    std::vector<float> lum;
    std::vector<Prepared> prepared;
    for (const auto& view : views) {
        prepared.push_back(prepare(view));
    }
    for (uint32_t y = 0; y < sh; ++y) {
        const float lat = (0.5f - (y + 0.5f) / sh) * kPi;
        for (uint32_t x = 0; x < sw; ++x) {
            const float lon = ((x + 0.5f) / sw) * 2.0f * kPi;
            const Vec3 d = direction(lon, lat);
            covering.clear();
            lum.clear();
            for (size_t i = 0; i < n; ++i) {
                const Projection p = project(prepared[i], d);
                if (!p.valid || edge_distance(views[i], p) < 2.0f) {
                    continue;
                }
                covering.push_back(i);
                lum.push_back(luminance(views[i], p));
            }
            for (size_t a = 0; a < covering.size(); ++a) {
                for (size_t b = a + 1; b < covering.size(); ++b) {
                    const size_t i = covering[a];
                    const size_t j = covering[b];
                    sum_a[i * n + j] += lum[a];
                    sum_b[i * n + j] += lum[b];
                    count[i * n + j] += 1.0;
                    sum_a[j * n + i] += lum[b];
                    sum_b[j * n + i] += lum[a];
                    count[j * n + i] += 1.0;
                }
            }
        }
    }
    std::vector<double> gain(n, 1.0);
    std::vector<double> total(n);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            total[i] += count[i * n + j];
        }
    }
    constexpr double kAnchor = 0.05;
    for (int iteration = 0; iteration < 200; ++iteration) {
        for (size_t i = 0; i < n; ++i) {
            if (total[i] <= 0.0) {
                continue;
            }
            double numerator = kAnchor * total[i];
            double denominator = kAnchor * total[i];
            for (size_t j = 0; j < n; ++j) {
                const double c = count[i * n + j];
                if (c < 16.0) {
                    continue;
                }
                const double a = sum_a[i * n + j] / c;
                const double b = sum_b[i * n + j] / c;
                numerator += c * a * gain[j] * b;
                denominator += c * a * a;
            }
            if (denominator > 0.0) {
                gain[i] = std::clamp(numerator / denominator, 0.5, 2.0);
            }
        }
    }
    for (size_t i = 0; i < n; ++i) {
        views[i].gain = static_cast<float>(gain[i]);
    }
}

Image assemble_mono(const std::vector<const View*>& views,
    uint32_t out_width, unsigned threads, const AssembleOptions& options) {
    Image image{};
    image.width = out_width;
    image.height = out_width / 2;
    image.bgra.assign(static_cast<size_t>(image.width) * image.height * 4, 0);
    const std::vector<Prepared> prepared = prepare_all(views);
    const std::vector<LowImage> lows = options.two_band
        ? make_lows(prepared, threads) : std::vector<LowImage>{};
    parallel_rows(image.height, threads, [&](uint32_t y) {
        const float lat = (0.5f - (y + 0.5f) / image.height) * kPi;
        uint8_t* row = image.bgra.data() + static_cast<size_t>(y) * image.width * 4;
        for (uint32_t x = 0; x < image.width; ++x) {
            const float lon = ((x + 0.5f) / image.width) * 2.0f * kPi - kPi;
            const Vec3 d = direction(lon, lat);
            float c[3]{};
            if (options.two_band) {
                blend_two_band(prepared, lows, d, options, c);
            } else {
                blend_views(prepared, d, c);
            }
            store(row + x * 4, c);
        }
    });
    unsharp(image, options.sharpen, threads);
    return image;
}

Image assemble_ods_eye(const std::vector<const View*>& band,
    const std::vector<const View*>& poles, float ods_step_deg,
    uint32_t out_width, unsigned threads, const AssembleOptions& options) {
    Image image{};
    image.width = out_width;
    image.height = out_width / 2;
    image.bgra.assign(static_cast<size_t>(image.width) * image.height * 4, 0);
    if (band.empty()) {
        return image;
    }
    // Band views sorted by yaw; the slit-scan step between neighbours.
    std::vector<const View*> sorted = band;
    std::sort(sorted.begin(), sorted.end(), [](const View* a, const View* b) {
        return a->yaw_deg < b->yaw_deg;
    });
    const size_t count = sorted.size();
    const float step = 360.0f / static_cast<float>(count);
    const std::vector<Prepared> band_prepared = prepare_all(sorted);
    const std::vector<Prepared> pole_prepared = prepare_all(poles);
    const std::vector<LowImage> pole_lows = options.two_band
        ? make_lows(pole_prepared, threads) : std::vector<LowImage>{};
    (void)ods_step_deg;
    // Band-to-pole transition: about 10 degrees of the strip's height.
    parallel_rows(image.height, threads, [&](uint32_t y) {
        const float lat = (0.5f - (y + 0.5f) / image.height) * kPi;
        uint8_t* row = image.bgra.data() + static_cast<size_t>(y) * image.width * 4;
        for (uint32_t x = 0; x < image.width; ++x) {
            const float lon_deg = ((x + 0.5f) / image.width) * 360.0f - 180.0f;
            const Vec3 d = direction(lon_deg * kDegToRad, lat);
            // Nearest two band steps around this longitude.
            float wrapped = lon_deg - sorted[0]->yaw_deg;
            wrapped = std::fmod(std::fmod(wrapped, 360.0f) + 360.0f, 360.0f);
            const float k = wrapped / step;
            const size_t k0 = static_cast<size_t>(std::floor(k)) % count;
            const size_t k1 = (k0 + 1) % count;
            const float t = k - std::floor(k);
            float band_color[3]{};
            float band_weight = 0.0f;
            float band_vertical = 0.0f;
            for (int side = 0; side < 2; ++side) {
                const Prepared& prepared = band_prepared[side == 0 ? k0 : k1];
                const View& view = *prepared.view;
                const float w = side == 0 ? 1.0f - t : t;
                if (w <= 0.0f) {
                    continue;
                }
                const Projection p = project(prepared, d);
                if (!p.valid || edge_distance(view, p) < 0.0f) {
                    continue;
                }
                float c[3];
                sample_detail(view, p, c, options.bicubic);
                for (int i = 0; i < 3; ++i) {
                    band_color[i] += c[i] * w;
                }
                band_weight += w;
                const float transition =
                    std::max(8.0f, static_cast<float>(view.full_height) * 0.10f);
                band_vertical += w * smoothstep01(
                    vertical_edge_distance(view, p) / transition);
            }
            if (band_weight > 0.0f) {
                for (int i = 0; i < 3; ++i) {
                    band_color[i] /= band_weight;
                }
                band_vertical /= band_weight;
            }
            float pole_color[3]{};
            bool pole_covered = false;
            if (band_vertical < 1.0f) {
                pole_covered = options.two_band
                    ? blend_two_band(pole_prepared, pole_lows, d, options, pole_color)
                    : blend_views(pole_prepared, d, pole_color) > 0.0f;
            }
            float c[3]{};
            if (band_weight > 0.0f && pole_covered) {
                for (int i = 0; i < 3; ++i) {
                    c[i] = band_color[i] * band_vertical +
                        pole_color[i] * (1.0f - band_vertical);
                }
            } else if (band_weight > 0.0f) {
                std::copy(band_color, band_color + 3, c);
            } else if (pole_covered) {
                std::copy(pole_color, pole_color + 3, c);
            }
            store(row + x * 4, c);
        }
    });
    unsharp(image, options.sharpen, threads);
    return image;
}

void rotate_left(Image& image, float degrees) {
    if (image.width == 0 || image.height == 0) {
        return;
    }
    const int64_t w = image.width;
    int64_t shift = static_cast<int64_t>(std::llround(
        static_cast<double>(degrees) / 360.0 * static_cast<double>(w)));
    shift = ((shift % w) + w) % w;  // columns to move left
    if (shift == 0) {
        return;
    }
    std::vector<uint8_t> row(static_cast<size_t>(w) * 4);
    for (uint32_t y = 0; y < image.height; ++y) {
        uint8_t* data = image.bgra.data() + static_cast<size_t>(y) * w * 4;
        // new[x] = old[x + shift]
        std::copy(data + shift * 4, data + w * 4, row.begin());
        std::copy(data, data + shift * 4, row.begin() + (w - shift) * 4);
        std::copy(row.begin(), row.end(), data);
    }
}

Image stack_top_bottom(const Image& top, const Image& bottom) {
    Image image{};
    image.width = top.width;
    image.height = top.height + bottom.height;
    image.bgra.reserve(top.bgra.size() + bottom.bgra.size());
    image.bgra.insert(image.bgra.end(), top.bgra.begin(), top.bgra.end());
    image.bgra.insert(image.bgra.end(), bottom.bgra.begin(), bottom.bgra.end());
    return image;
}

}  // namespace w3vr::capture360
