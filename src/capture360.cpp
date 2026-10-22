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
    uint32_t out_width, unsigned threads) {
    Image image{};
    image.width = out_width;
    image.height = out_width / 2;
    image.bgra.assign(static_cast<size_t>(image.width) * image.height * 4, 0);
    const std::vector<Prepared> prepared = prepare_all(views);
    parallel_rows(image.height, threads, [&](uint32_t y) {
        const float lat = (0.5f - (y + 0.5f) / image.height) * kPi;
        uint8_t* row = image.bgra.data() + static_cast<size_t>(y) * image.width * 4;
        for (uint32_t x = 0; x < image.width; ++x) {
            const float lon = ((x + 0.5f) / image.width) * 2.0f * kPi - kPi;
            float c[3]{};
            blend_views(prepared, direction(lon, lat), c);
            store(row + x * 4, c);
        }
    });
    return image;
}

Image assemble_ods_eye(const std::vector<const View*>& band,
    const std::vector<const View*>& poles, float ods_step_deg,
    uint32_t out_width, unsigned threads) {
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
                sample(view, p, c);
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
            const float pole_weight = band_vertical >= 1.0f
                ? 0.0f
                : blend_views(pole_prepared, d, pole_color);
            float c[3]{};
            if (band_weight > 0.0f && pole_weight > 0.0f) {
                for (int i = 0; i < 3; ++i) {
                    c[i] = band_color[i] * band_vertical +
                        pole_color[i] * (1.0f - band_vertical);
                }
            } else if (band_weight > 0.0f) {
                std::copy(band_color, band_color + 3, c);
            } else if (pole_weight > 0.0f) {
                std::copy(pole_color, pole_color + 3, c);
            }
            store(row + x * 4, c);
        }
    });
    return image;
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
