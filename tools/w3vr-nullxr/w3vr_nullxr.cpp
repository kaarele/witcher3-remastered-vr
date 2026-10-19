// SPDX-License-Identifier: GPL-3.0-or-later
//
// w3vr_nullxr: a minimal headless OpenXR runtime (D3D12 only) for testing the
// witcher3-vr mod without a headset.
//
// Copyright (C) 2026 kaarele
//
// The W3VR_NULLXR_PACER=wivrn pacing model (see "pacing" below:
// wivrn_predict(), pacer_gpu_done() and the frame bookkeeping around them) is
// adapted from WiVRn's application pacer, server/driver/app_pacer.cpp,
// Copyright (C) 2025 Patrick Nicolas, https://github.com/WiVRn/WiVRn,
// licensed under the GNU General Public License v3.0 or later.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. This program is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
// Public License for more details: LICENSE in this directory, or
// <https://www.gnu.org/licenses/>.
//
// This tool is for development and testing only. It is built separately
// (build.sh) and is not part of the mod or its release packages; the mod
// itself stays under the MIT License.
//
// It runs inside the game process as a Windows DLL. The mod loads it in place
// of openxr_loader.dll when witcher3vr.ini sets [test] openxr_loader=<path to
// this DLL> (relative paths are resolved from the game exe's folder), so no
// OpenXR loader, wineopenxr or host runtime is involved; this also works under
// Proton, where XR_RUNTIME_JSON cannot be used.
//
// It reports a Quest 3-like HMD (per-eye FOV and recommended size as reported
// by WiVRn for a Quest 3; static, or with scripted head yaw), paces
// xrWaitFrame, accepts every submitted layer and can dump the images of
// submitted projection and quad layers to BMP files, i.e. exactly what a
// compositor would receive.
//
// Environment (all optional):
//   W3VR_NULLXR_HZ              display rate (default 90)
//   W3VR_NULLXR_LOG             log file (default w3vr_nullxr.log next to the DLL)
//   W3VR_NULLXR_CAPTURE_DIR     capture directory (default nullxr_captures next to the DLL)
//   W3VR_NULLXR_CAPTURE_START_MS  first capture, ms after xrBeginSession (default 30000)
//   W3VR_NULLXR_CAPTURE_EVERY_MS  capture period in ms; 0 disables periodic captures (default 0)
//   W3VR_NULLXR_CAPTURE_MAX     maximum number of captured frames (default 40)
//   W3VR_NULLXR_CAPTURE_AT_MS   comma-separated capture times, ms after xrBeginSession
//   W3VR_NULLXR_IPD             interpupillary distance in metres (default 0.063)
//   W3VR_NULLXR_MOTION          off (default), yaw_sine or yaw_triangle: scripted head yaw, see head_yaw_at()
//   W3VR_NULLXR_MOTION_START_MS / _AMP_DEG / _PERIOD_MS / _DWELL_MS  motion start, amplitude, period, end holds
//   W3VR_NULLXR_PACER           unset (default): fixed-rate xrWaitFrame that never blocks a late frame;
//                               wivrn: WiVRn's app pacer, see "pacing" below
//   W3VR_NULLXR_PACER_GPU_MS    GPU time per frame; unset (default) with wivrn: measured with a fence on the
//                               application's queue, otherwise fixed (default 2)
//   W3VR_NULLXR_PACER_COMPOSITOR_MS  compositor time, predicted display time - compositor wake-up (default 30
//                               with wivrn, where it covers encode, network, decode and the headset; else 0)
//   W3VR_NULLXR_PACER_MARGIN_MS  wake-up margin (default 1.5, WiVRn's U_PACING_APP_MIN_MARGIN_MS)
//   W3VR_NULLXR_PACER_BACKPRESSURE  1 (default): with a measured GPU time, xrEndFrame waits for the previous
//                               frame's GPU work like Monado; 0 never blocks
//   W3VR_NULLXR_FRAME_LOG       one log line per xrEndFrame with its compositor tick (default 1 with wivrn, else 0)
//   W3VR_NULLXR_STATS_MS        pacing summary period in ms of display time; 0 disables (default 5000)
// A file named "capture.now" in the capture directory triggers one capture
// of the next submitted frame and is then deleted.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_PLATFORM_WIN32
#define XR_NO_PROTOTYPES  // the entry points below are the first declarations (and exported)
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define NULLXR_EXPORT extern "C" __declspec(dllexport)

namespace {

// Quest 3 per-eye field of view and recommended size as reported by WiVRn
// 26.9: left eye -54/+40 deg horizontally, +44/-55 deg vertically; the right
// eye is mirrored. For another headset, take the values the real runtime
// reports (the mod logs them as "OpenXR runtime FOV geometry").
constexpr XrFovf kFov[2] = {
    {-0.942478f, 0.698132f, 0.767945f, -0.959931f},
    {-0.698132f, 0.942478f, 0.767945f, -0.959931f},
};
constexpr uint32_t kRecommendedWidth = 2476;
constexpr uint32_t kRecommendedHeight = 2594;
constexpr uint32_t kMaxWidth = 4952;
constexpr uint32_t kMaxHeight = 5188;
constexpr XrSystemId kSystemId = 1;

HMODULE g_module{};
std::mutex g_mutex;      // events and session registry
std::mutex g_log_mutex;  // log file only (log() is called with g_mutex held)
FILE* g_log{};
std::wstring g_dir;

std::wstring env(const wchar_t* name, const wchar_t* fallback) {
    wchar_t buffer[1024];
    const DWORD n = GetEnvironmentVariableW(name, buffer, 1024);
    return n > 0 && n < 1024 ? std::wstring(buffer, n) : std::wstring(fallback);
}

double env_number(const wchar_t* name, double fallback) {
    const std::wstring value = env(name, L"");
    return value.empty() ? fallback : _wtof(value.c_str());
}

void log(const char* format, ...) {
    std::scoped_lock lock{g_log_mutex};
    if (g_log == nullptr) {
        const std::wstring path = env(L"W3VR_NULLXR_LOG", (g_dir + L"w3vr_nullxr.log").c_str());
        g_log = _wfopen(path.c_str(), L"w");
        if (g_log == nullptr) {
            return;
        }
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, format);
    vfprintf(g_log, format, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

XrTime now_ns() {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<XrTime>(static_cast<long double>(c.QuadPart) * 1e9L / frequency);
}

// Optional scripted head motion (yaw only), so temporal effects can be tested under head rotation.
// W3VR_NULLXR_MOTION=yaw_sine|yaw_triangle (default off), _MOTION_START_MS (time after xrBeginSession,
// default 150000), _MOTION_AMP_DEG (20), _MOTION_PERIOD_MS (4000), _MOTION_DWELL_MS (0: hold this long at
// each end point of the triangle). Positive yaw turns the head left (counter-clockwise seen from above).
std::atomic<XrTime> g_motion_origin{};

float head_yaw_at(XrTime time) {
    static const std::wstring mode = env(L"W3VR_NULLXR_MOTION", L"off");
    static const double start_ms = env_number(L"W3VR_NULLXR_MOTION_START_MS", 150000);
    static const double amp = env_number(L"W3VR_NULLXR_MOTION_AMP_DEG", 20) * 3.14159265358979 / 180.0;
    static const double period_ms = (std::max)(env_number(L"W3VR_NULLXR_MOTION_PERIOD_MS", 4000), 100.0);
    static const double dwell_ms = (std::max)(env_number(L"W3VR_NULLXR_MOTION_DWELL_MS", 0), 0.0);
    const XrTime origin = g_motion_origin.load();
    if (mode == L"off" || origin == 0) {
        return 0.0f;
    }
    const double t = static_cast<double>(time - origin) / 1e6 - start_ms;
    if (t <= 0) {
        return 0.0f;
    }
    if (mode == L"yaw_sine") {
        return static_cast<float>(amp * std::sin(2 * 3.14159265358979 * t / period_ms));
    }
    // Triangle 0 -> +amp -> (dwell) -> -amp -> (dwell) -> 0, constant angular speed between the holds.
    const double cycle = period_ms + 2 * dwell_ms;
    double u = std::fmod(t, cycle);
    const double q = period_ms / 4;
    if (u < q) return static_cast<float>(amp * u / q);
    u -= q;
    if (u < dwell_ms) return static_cast<float>(amp);
    u -= dwell_ms;
    if (u < 2 * q) return static_cast<float>(amp * (1 - u / q));
    u -= 2 * q;
    if (u < dwell_ms) return static_cast<float>(-amp);
    u -= dwell_ms;
    return static_cast<float>(-amp * (1 - u / q));
}

XrPosef head_pose_at(XrTime time) {
    const float yaw = head_yaw_at(time);
    return XrPosef{{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}, {0, 0, 0}};
}

void sleep_until(XrTime target) {
    for (;;) {
        const XrTime remaining = target - now_ns();
        if (remaining <= 0) {
            return;
        }
        if (remaining > 2'000'000) {
            Sleep(static_cast<DWORD>((remaining - 1'000'000) / 1'000'000));
        } else {
            YieldProcessor();
        }
    }
}

struct Swapchain {
    XrSwapchainCreateInfo info{};
    DXGI_FORMAT format{};
    std::vector<ID3D12Resource*> images;
    uint32_t next_acquire{};
    std::deque<uint32_t> acquired;
    int64_t last_released{-1};
};

struct Space {
    XrReferenceSpaceType type{};
};

// ----------------------------------------------------------------- pacing ----
// W3VR_NULLXR_PACER=wivrn reproduces WiVRn v26.9's application pacer
// (server/driver/app_pacer.cpp, run by Monado's multi compositor), so headless
// runs show the frame-rate steps of the headset. This part is adapted from
// WiVRn (GPL-3.0-or-later, Copyright (C) 2025 Patrick Nicolas; see the notice
// at the top of this file). The model:
// - cpu_time = EMA(0.1) of xrWaitFrame wake-up -> xrEndFrame and gpu_time =
//   EMA(0.1) of xrEndFrame -> GPU done, both updated when the GPU work is done;
// - the predicted display time is the first compositor slot at or after
//   now + cpu_time + gpu_time + compositor time, phase-aligned to the grid;
// - xrWaitFrame sleeps until that slot minus (cpu + gpu + compositor time +
//   margin). It returns at once ("limited by app time, don't wait") when the
//   app time exceeds one period, or when the ideal slot (one period after the
//   previous frame) is out of reach but the slot after it is not;
// - with a measured GPU time, xrEndFrame first waits for the previous frame's
//   GPU work (Monado's layer_begin waits for its wait thread).
// Both modes model which compositor tick latches each frame: tick k displays at
// begin + k * period and wakes compositor time earlier; it takes the newest
// frame whose GPU work is done and whose display time is due (0.5 ms slack, as
// Monado), and a newer frame reaching the same tick replaces the older one.
// The hold of a frame is the number of ticks it stays on screen.
struct PacerFrame {
    uint64_t id{};
    XrTime wake{};       // xrWaitFrame returned
    XrTime slept{};      // time spent in xrWaitFrame
    XrTime delivered{};  // xrEndFrame called
    XrTime display{};    // XrFrameEndInfo::displayTime
    bool no_wait{};      // the pacer took its "don't wait" branch
};

struct PacingWindow {
    XrTime start{};
    uint32_t ended{};
    uint32_t latched{};
    uint32_t shown{};     // frames that stayed on screen for at least one tick
    uint32_t replaced{};  // frames replaced by a newer one before their tick
    std::array<uint32_t, 8> holds{};  // 1..7 and 8+ ticks
    uint32_t waits{};
    uint32_t slept{};     // waits that slept at least 0.2 ms
    uint32_t no_waits{};
    double sleep_ms{};
    double cpu_ms{};      // wake-up -> xrEndFrame
    uint32_t end_to_wait_count{};
    double end_to_wait_ms{};  // xrEndFrame return -> next xrWaitFrame
    double late_ms{};     // latched tick display time - submitted display time
    double judder_ms{};   // |hold * period - content interval| of shown frames
};

enum PacingOrderError { kWaitWhileWaited, kBeginWithoutWait, kBeginWhileBegun, kEndWithoutBegin, kOrderErrorKinds };

struct Pacing {
    bool wivrn{};
    bool measure_gpu{};
    bool back_pressure{};
    bool frame_log{};
    XrTime gpu_fixed{};
    XrTime compositor{};
    XrTime margin{};
    XrTime window{};
    std::mutex mutex;  // everything below
    // WiVRn app_pacer state
    uint64_t frame_id{};
    XrTime last_display{};
    XrTime cpu_time{};
    XrTime gpu_time{};
    std::array<PacerFrame, 16> frames{};
    // Call order: one waited and one begun frame at most.
    uint64_t waited{};
    uint64_t begun{};
    std::array<uint32_t, kOrderErrorKinds> order_errors{};
    XrTime last_end_return{};
    // Latch model
    int64_t last_tick{-1};
    XrTime last_tick_delivered{};
    PacingWindow stats;
    // GPU completion of each frame (measured GPU time only)
    struct GpuWait {
        PacerFrame frame;
        uint64_t value{};
    };
    ID3D12Fence* fence{};
    HANDLE event{};
    uint64_t fence_value{};
    std::deque<GpuWait> gpu_pending;
    std::condition_variable cv;
    bool stop{};
    std::thread waiter;
};

struct Session {
    ID3D12Device* device{};
    ID3D12CommandQueue* queue{};
    XrSessionState state{XR_SESSION_STATE_UNKNOWN};
    bool running{};
    bool exit_requested{};
    XrTime begin_time{};
    XrTime next_display{};
    XrTime period{};
    uint64_t frames_waited{};
    uint64_t frames_ended{};
    // capture
    ID3D12CommandAllocator* capture_allocator{};
    ID3D12GraphicsCommandList* capture_list{};
    ID3D12Fence* capture_fence{};
    UINT64 capture_fence_value{};
    HANDLE capture_event{};
    XrTime next_capture{};
    size_t next_capture_at{};  // index into W3VR_NULLXR_CAPTURE_AT_MS
    uint32_t captures{};
    Pacing pacing;
};

struct Instance {
    bool d3d12_enabled{};
};

Instance* g_instance{};
Session* g_session{};
std::deque<XrEventDataBuffer> g_events;

template <typename T>
T* from_handle(uint64_t handle) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(handle));
}

template <typename H, typename T>
H to_handle(T* object) {
    return reinterpret_cast<H>(object);
}

void push_state(Session* session, XrSessionState state) {
    session->state = state;
    XrEventDataBuffer buffer{XR_TYPE_EVENT_DATA_BUFFER};
    auto* event = reinterpret_cast<XrEventDataSessionStateChanged*>(&buffer);
    event->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    event->next = nullptr;
    event->session = to_handle<XrSession>(session);
    event->state = state;
    event->time = now_ns();
    g_events.push_back(buffer);
    log("session state -> %d", static_cast<int>(state));
}

// ---------------------------------------------------------------- pacing ----

double ns_to_ms(XrTime ns) {
    return static_cast<double>(ns) / 1e6;
}

XrTime ms_to_ns(double ms) {
    return static_cast<XrTime>(ms * 1e6);
}

// app_pacer.cpp lerp0: the first sample initialises the average.
XrTime lerp0(XrTime a, XrTime b, double t) {
    return a == 0 ? b : static_cast<XrTime>(std::lerp(static_cast<double>(a), static_cast<double>(b), t));
}

XrTime tick_display(const Session* s, int64_t tick) {
    return s->begin_time + tick * s->period;
}

int64_t first_tick_at_or_after(const Session* s, XrTime time) {
    return time <= s->begin_time ? 0 : (time - s->begin_time + s->period - 1) / s->period;
}

void count_order_error(Pacing& p, PacingOrderError kind, uint64_t frame) {
    static const char* const kNames[kOrderErrorKinds] = {
        "xrWaitFrame while the previous frame was not begun", "xrBeginFrame without xrWaitFrame",
        "xrBeginFrame while a frame was begun (discarded)", "xrEndFrame without xrBeginFrame"};
    if (++p.order_errors[kind] <= 16) {
        log("call order: %s frame=%llu count=%u", kNames[kind], static_cast<unsigned long long>(frame),
            p.order_errors[kind]);
    }
}

uint32_t order_error_total(const Pacing& p) {
    uint32_t total = 0;
    for (const uint32_t count : p.order_errors) total += count;
    return total;
}

struct Prediction {
    uint64_t id{};
    XrTime wake{};
    XrTime display{};
    bool no_wait{};
};

// app_pacer::predict. The compositor broadcasts its latest frame to the pacer on
// every tick (app_pacer::info); here that is the newest tick whose wake-up
// (display time - compositor time) has passed.
Prediction wivrn_predict(Session* s, XrTime now) {
    Pacing& p = s->pacing;
    const XrTime period = s->period;
    Prediction out{++p.frame_id};
    const XrTime since_begin = now + p.compositor - s->begin_time;
    const XrTime compositor_display = tick_display(s, since_begin > 0 ? since_begin / period : 0);
    if (p.last_display == 0) {
        // WiVRn starts from 0 and steps up to now one period at a time.
        p.last_display = compositor_display;
    }
    const XrTime min_ready = now + p.cpu_time + p.gpu_time + p.compositor;
    // The ideal display time: one frame after the last, in phase with the compositor.
    p.last_display += period;
    p.last_display = compositor_display + period * ((period / 2 + p.last_display - compositor_display) / period);
    if (p.cpu_time > period || p.gpu_time > period ||
        (min_ready > p.last_display && min_ready < p.last_display + period)) {
        // Limited by app time, don't wait.
        out.wake = now;
        out.no_wait = true;
        while (p.last_display < min_ready) p.last_display += period;
    } else {
        while (p.last_display < min_ready) p.last_display += period;
        out.wake = p.last_display - (p.cpu_time + p.gpu_time + p.compositor + p.margin);
    }
    out.display = p.last_display;
    return out;
}

// app_pacer::mark_gpu_done
void pacer_gpu_done(Pacing& p, XrTime period, const PacerFrame& frame, XrTime when) {
    if (frame.wake == 0 || frame.delivered == 0) {
        return;
    }
    p.cpu_time = lerp0(p.cpu_time, (std::min)(3 * period, frame.delivered - frame.wake), 0.1);
    p.gpu_time = lerp0(p.gpu_time, (std::min)(3 * period, when - frame.delivered), 0.1);
}

std::string pacing_summary(Session* s, XrTime now) {
    const Pacing& p = s->pacing;
    const PacingWindow& w = p.stats;
    const double seconds = static_cast<double>(now - w.start) / 1e9;
    size_t dominant = 0;
    for (size_t i = 1; i < w.holds.size(); ++i) {
        if (w.holds[i] > w.holds[dominant]) dominant = i;
    }
    const auto mean = [](double sum, uint32_t count) { return count > 0 ? sum / count : 0.0; };
    char line[768];
    snprintf(line, sizeof(line),
        "pacing %s %.1fHz t=%.1fs window=%.2fs ended=%u shown=%u replaced=%u unique_fps=%.2f "
        "holds 1:%u 2:%u 3:%u 4:%u 5:%u 6:%u 7:%u 8+:%u dominant=%zu share=%.0f%% judder=%.2fms late=%.2fms "
        "waits=%u slept=%u nowait=%u sleep=%.2fms cpu=%.2fms end_to_wait=%.2fms ema_cpu=%.2fms ema_gpu=%.2fms "
        "order_errors=%u",
        p.wivrn ? "wivrn" : "fixed", 1e9 / static_cast<double>(s->period),
        static_cast<double>(now - s->begin_time) / 1e9, seconds, w.ended, w.shown, w.replaced,
        seconds > 0 ? w.shown / seconds : 0.0, w.holds[0], w.holds[1], w.holds[2], w.holds[3], w.holds[4],
        w.holds[5], w.holds[6], w.holds[7], dominant + 1,
        w.shown > 0 ? 100.0 * w.holds[dominant] / w.shown : 0.0, mean(w.judder_ms, w.shown),
        mean(w.late_ms, w.latched), w.waits, w.slept, w.no_waits, mean(w.sleep_ms, w.waits),
        mean(w.cpu_ms, w.ended), mean(w.end_to_wait_ms, w.end_to_wait_count), ns_to_ms(p.cpu_time),
        ns_to_ms(p.gpu_time), order_error_total(p));
    return line;
}

// Latches |frame| (GPU work done at |ready|) at its compositor tick and
// finalises the hold of the previous frame. Called in submission order with
// the pacing mutex held; returns the lines to log after unlocking.
void latch_frame(Session* s, const PacerFrame& frame, XrTime ready, std::string& line, std::string& summary) {
    Pacing& p = s->pacing;
    const XrTime period = s->period;
    const int64_t tick = (std::max)({first_tick_at_or_after(s, ready + p.compositor),
        first_tick_at_or_after(s, frame.display - 500'000), p.last_tick});
    const XrTime shown_at = tick_display(s, tick);
    PacingWindow& w = p.stats;
    int64_t previous_hold = -1;
    if (p.last_tick >= 0) {
        previous_hold = tick - p.last_tick;
        if (previous_hold == 0) {
            ++w.replaced;
        } else {
            ++w.shown;
            ++w.holds[static_cast<size_t>((std::min<int64_t>)(previous_hold, 8) - 1)];
            w.judder_ms += std::fabs(ns_to_ms(previous_hold * period - (frame.delivered - p.last_tick_delivered)));
        }
    }
    p.last_tick = tick;
    p.last_tick_delivered = frame.delivered;
    ++w.latched;
    w.late_ms += ns_to_ms(shown_at - frame.display);
    if (p.frame_log) {
        char buffer[320];
        snprintf(buffer, sizeof(buffer),
            "frame %llu wake=%.2f slept=%.2f%s end=%.2f cpu=%.2f gpu_done=%.2f display=%.2f tick=%lld shown=%.2f "
            "late=%+.2f prev_hold=%lld",
            static_cast<unsigned long long>(frame.id), ns_to_ms(frame.wake - s->begin_time), ns_to_ms(frame.slept),
            frame.no_wait ? " nowait" : "", ns_to_ms(frame.delivered - s->begin_time),
            ns_to_ms(frame.delivered - frame.wake), ns_to_ms(ready - s->begin_time),
            ns_to_ms(frame.display - s->begin_time), static_cast<long long>(tick),
            ns_to_ms(shown_at - s->begin_time), ns_to_ms(shown_at - frame.display),
            static_cast<long long>(previous_hold));
        line = buffer;
    }
    if (p.window > 0 && shown_at - w.start >= p.window) {
        summary = pacing_summary(s, shown_at);
        p.stats = PacingWindow{};
        p.stats.start = shown_at;
    }
}

// Monado's per-client wait thread: waits for each frame's GPU work, then feeds
// the pacer (mark_gpu_done) and the compositor (the frame becomes latchable).
void gpu_waiter(Session* s) {
    Pacing& p = s->pacing;
    std::unique_lock lock{p.mutex};
    while (!p.stop) {
        if (p.gpu_pending.empty()) {
            p.cv.wait(lock);
            continue;
        }
        const Pacing::GpuWait job = p.gpu_pending.front();
        lock.unlock();
        while (p.fence->GetCompletedValue() < job.value) {
            p.fence->SetEventOnCompletion(job.value, p.event);
            WaitForSingleObject(p.event, 100);
            std::scoped_lock check{p.mutex};
            if (p.stop) {
                return;
            }
        }
        const XrTime done = now_ns();
        std::string line, summary;
        lock.lock();
        p.gpu_pending.pop_front();
        pacer_gpu_done(p, s->period, job.frame, done);
        latch_frame(s, job.frame, done, line, summary);
        p.cv.notify_all();
        lock.unlock();
        if (!line.empty()) log("%s", line.c_str());
        if (!summary.empty()) log("%s", summary.c_str());
        lock.lock();
    }
}

void configure_pacing(Session* s) {
    Pacing& p = s->pacing;
    p.wivrn = env(L"W3VR_NULLXR_PACER", L"") == L"wivrn";
    const double gpu_ms = env_number(L"W3VR_NULLXR_PACER_GPU_MS", -1);
    p.measure_gpu = p.wivrn && gpu_ms < 0;
    p.gpu_fixed = ms_to_ns(gpu_ms < 0 ? 2.0 : gpu_ms);
    p.compositor = ms_to_ns((std::max)(env_number(L"W3VR_NULLXR_PACER_COMPOSITOR_MS", p.wivrn ? 30 : 0), 0.0));
    p.margin = ms_to_ns(std::clamp(env_number(L"W3VR_NULLXR_PACER_MARGIN_MS", 1.5), 0.5, 5.0));
    p.back_pressure = env_number(L"W3VR_NULLXR_PACER_BACKPRESSURE", 1) != 0;
    p.frame_log = env_number(L"W3VR_NULLXR_FRAME_LOG", p.wivrn ? 1 : 0) != 0;
    p.window = ms_to_ns((std::max)(env_number(L"W3VR_NULLXR_STATS_MS", 5000), 0.0));
    if (p.measure_gpu) {
        if (SUCCEEDED(s->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p.fence))) &&
            (p.event = CreateEventW(nullptr, FALSE, FALSE, nullptr)) != nullptr) {
            p.waiter = std::thread(gpu_waiter, s);
        } else {
            log("pacer: no GPU fence, using a fixed GPU time");
            p.measure_gpu = false;
        }
    }
    p.back_pressure = p.back_pressure && p.measure_gpu;
    log("pacer=%s gpu=%s%.1fms compositor=%.1fms margin=%.1fms back_pressure=%d frame_log=%d stats=%.0fms",
        p.wivrn ? "wivrn" : "fixed", p.measure_gpu ? "measured, start " : "", ns_to_ms(p.gpu_fixed),
        ns_to_ms(p.compositor), ns_to_ms(p.margin), p.back_pressure ? 1 : 0, p.frame_log ? 1 : 0,
        ns_to_ms(p.window));
}

void reset_pacing(Session* s) {
    Pacing& p = s->pacing;
    std::scoped_lock lock{p.mutex};
    p.last_display = 0;
    p.cpu_time = 0;
    p.gpu_time = 0;
    p.waited = 0;
    p.begun = 0;
    p.last_end_return = 0;
    p.last_tick = -1;
    p.last_tick_delivered = 0;
    p.stats = PacingWindow{};
    p.stats.start = s->begin_time;
}

void stop_pacing(Session* s) {
    Pacing& p = s->pacing;
    if (p.waiter.joinable()) {
        {
            std::scoped_lock lock{p.mutex};
            p.stop = true;
        }
        p.cv.notify_all();
        SetEvent(p.event);
        p.waiter.join();
    }
    if (p.fence != nullptr) p.fence->Release();
    if (p.event != nullptr) CloseHandle(p.event);
    log("pacing order_errors wait_while_waited=%u begin_without_wait=%u begin_while_begun=%u end_without_begin=%u",
        p.order_errors[kWaitWhileWaited], p.order_errors[kBeginWithoutWait], p.order_errors[kBeginWhileBegun],
        p.order_errors[kEndWithoutBegin]);
}

// xrEndFrame: the frame is delivered now. With a measured GPU time it is
// latched once its GPU work completes, otherwise after the fixed GPU time.
void pace_end_frame(Session* s, XrTime delivered, XrTime display_time) {
    Pacing& p = s->pacing;
    std::string line, summary;
    std::unique_lock lock{p.mutex};
    const uint64_t id = p.begun;
    if (id == 0) {
        count_order_error(p, kEndWithoutBegin, p.waited);
        return;
    }
    p.begun = 0;
    PacerFrame& frame = p.frames[id % p.frames.size()];
    if (frame.id != id) {
        return;
    }
    frame.delivered = delivered;
    frame.display = display_time;
    ++p.stats.ended;
    p.stats.cpu_ms += ns_to_ms(delivered - frame.wake);
    if (p.measure_gpu) {
        if (p.back_pressure) {
            p.cv.wait_for(lock, std::chrono::milliseconds(250), [&p] { return p.gpu_pending.empty(); });
        }
        const uint64_t value = ++p.fence_value;
        s->queue->Signal(p.fence, value);
        p.gpu_pending.push_back({frame, value});
        p.cv.notify_all();
        return;
    }
    const XrTime done = delivered + p.gpu_fixed;
    pacer_gpu_done(p, s->period, frame, done);
    latch_frame(s, frame, done, line, summary);
    lock.unlock();
    if (!line.empty()) log("%s", line.c_str());
    if (!summary.empty()) log("%s", summary.c_str());
}

DXGI_FORMAT resource_format(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_R24G8_TYPELESS;
    default:
        return format;
    }
}

bool is_depth(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
        format == DXGI_FORMAT_D16_UNORM || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
}

// ---------------------------------------------------------------- capture ----

bool ensure_capture_objects(Session* s) {
    if (s->capture_list != nullptr) {
        return true;
    }
    if (FAILED(s->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&s->capture_allocator))) ||
        FAILED(s->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            s->capture_allocator, nullptr, IID_PPV_ARGS(&s->capture_list))) ||
        FAILED(s->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s->capture_fence)))) {
        log("capture: failed to create command objects");
        return false;
    }
    s->capture_list->Close();
    s->capture_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return true;
}

void write_bmp(const std::wstring& path, const uint8_t* data, UINT row_pitch,
    uint32_t width, uint32_t height, bool bgra) {
    FILE* file = _wfopen(path.c_str(), L"wb");
    if (file == nullptr) {
        log("capture: cannot open output file");
        return;
    }
    const uint32_t row_bytes = (width * 3 + 3) & ~3u;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + row_bytes * height;
    ih.biSize = sizeof(ih);
    ih.biWidth = static_cast<LONG>(width);
    ih.biHeight = -static_cast<LONG>(height);
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    fwrite(&fh, sizeof(fh), 1, file);
    fwrite(&ih, sizeof(ih), 1, file);
    std::vector<uint8_t> row(row_bytes);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src = data + static_cast<size_t>(y) * row_pitch;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* p = src + x * 4;
            row[x * 3 + 0] = bgra ? p[0] : p[2];
            row[x * 3 + 1] = p[1];
            row[x * 3 + 2] = bgra ? p[2] : p[0];
        }
        fwrite(row.data(), 1, row_bytes, file);
    }
    fclose(file);
}

// Copies one array slice of the most recently released image of |chain| to a
// readback buffer on the application's queue and writes it to a BMP file.
void capture_image(Session* s, Swapchain* chain, uint32_t array_index, const std::wstring& path) {
    if (chain->last_released < 0 || !ensure_capture_objects(s)) {
        return;
    }
    const DXGI_FORMAT f = chain->format;
    const bool rgba = f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const bool bgra = f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if (!rgba && !bgra) {
        log("capture: format %d not supported", static_cast<int>(f));
        return;
    }
    ID3D12Resource* image = chain->images[static_cast<size_t>(chain->last_released)];
    const D3D12_RESOURCE_DESC desc = image->GetDesc();
    const UINT subresource = array_index;  // single mip
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 row_size = 0, total = 0;
    s->device->GetCopyableFootprints(&desc, subresource, 1, 0, &footprint, &rows, &row_size, &total);

    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* readback = nullptr;
    if (FAILED(s->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
        log("capture: readback allocation failed");
        return;
    }
    s->capture_allocator->Reset();
    s->capture_list->Reset(s->capture_allocator, nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = image;
    barrier.Transition.Subresource = subresource;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    s->capture_list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION dst{readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION src{image, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    src.SubresourceIndex = subresource;
    s->capture_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    s->capture_list->ResourceBarrier(1, &barrier);
    s->capture_list->Close();
    ID3D12CommandList* lists[] = {s->capture_list};
    s->queue->ExecuteCommandLists(1, lists);
    const UINT64 value = ++s->capture_fence_value;
    s->queue->Signal(s->capture_fence, value);
    if (s->capture_fence->GetCompletedValue() < value) {
        s->capture_fence->SetEventOnCompletion(value, s->capture_event);
        WaitForSingleObject(s->capture_event, 5000);
    }
    void* mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    if (SUCCEEDED(readback->Map(0, &range, &mapped))) {
        write_bmp(path, static_cast<const uint8_t*>(mapped) + footprint.Offset,
            footprint.Footprint.RowPitch, static_cast<uint32_t>(desc.Width), desc.Height, bgra);
        D3D12_RANGE none{0, 0};
        readback->Unmap(0, &none);
    }
    readback->Release();
}

std::wstring capture_dir() {
    static const std::wstring dir = [] {
        std::wstring d = env(L"W3VR_NULLXR_CAPTURE_DIR", (g_dir + L"nullxr_captures").c_str());
        CreateDirectoryW(d.c_str(), nullptr);
        if (!d.empty() && d.back() != L'\\') {
            d += L'\\';
        }
        return d;
    }();
    return dir;
}

// W3VR_NULLXR_CAPTURE_AT_MS: comma-separated capture times in ms after xrBeginSession, ascending.
const std::vector<double>& capture_times_ms() {
    static const std::vector<double> times = [] {
        std::vector<double> out;
        const std::wstring list = env(L"W3VR_NULLXR_CAPTURE_AT_MS", L"");
        size_t start = 0;
        while (start < list.size()) {
            size_t end = list.find(L',', start);
            if (end == std::wstring::npos) {
                end = list.size();
            }
            const std::wstring item = list.substr(start, end - start);
            if (!item.empty()) {
                out.push_back(_wtof(item.c_str()));
            }
            start = end + 1;
        }
        return out;
    }();
    return times;
}

void maybe_capture(Session* s, const XrFrameEndInfo* info) {
    const uint32_t max_captures = static_cast<uint32_t>(env_number(L"W3VR_NULLXR_CAPTURE_MAX", 40));
    const double every_ms = env_number(L"W3VR_NULLXR_CAPTURE_EVERY_MS", 0);
    const std::wstring trigger = capture_dir() + L"capture.now";
    const bool triggered = GetFileAttributesW(trigger.c_str()) != INVALID_FILE_ATTRIBUTES;
    const XrTime t = now_ns();
    const bool periodic = every_ms > 0 && t >= s->next_capture;
    const auto& times = capture_times_ms();
    const bool scheduled = s->next_capture_at < times.size() &&
        t >= s->begin_time + static_cast<XrTime>(times[s->next_capture_at] * 1e6);
    if ((!triggered && !periodic && !scheduled) || s->captures >= max_captures) {
        return;
    }
    if (triggered) {
        DeleteFileW(trigger.c_str());
    }
    if (periodic) {
        s->next_capture = t + static_cast<XrTime>(every_ms * 1e6);
    }
    while (s->next_capture_at < times.size() &&
           t >= s->begin_time + static_cast<XrTime>(times[s->next_capture_at] * 1e6)) {
        ++s->next_capture_at;
    }
    const unsigned ms = static_cast<unsigned>((t - s->begin_time) / 1'000'000);
    ++s->captures;
    for (uint32_t l = 0; l < info->layerCount; ++l) {
        const XrCompositionLayerBaseHeader* layer = info->layers[l];
        if (layer == nullptr) {
            continue;
        }
        wchar_t name[160];
        if (layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            for (uint32_t v = 0; v < p->viewCount && v < 2; ++v) {
                const XrCompositionLayerProjectionView& view = p->views[v];
                auto* chain = from_handle<Swapchain>(reinterpret_cast<uint64_t>(view.subImage.swapchain));
                swprintf(name, 160, L"nullxr_%07u_l%u_proj_eye%u.bmp", ms, l, v);
                capture_image(s, chain, view.subImage.imageArrayIndex, capture_dir() + name);
                log("capture t=%ums layer=%u projection eye=%u rect=%d,%d %dx%d slice=%u fov=%.6f,%.6f,%.6f,%.6f "
                    "pos=%.4f,%.4f,%.4f yaw=%.3f image=%lld",
                    ms, l, v, view.subImage.imageRect.offset.x, view.subImage.imageRect.offset.y,
                    view.subImage.imageRect.extent.width, view.subImage.imageRect.extent.height,
                    view.subImage.imageArrayIndex, view.fov.angleLeft, view.fov.angleRight,
                    view.fov.angleUp, view.fov.angleDown, view.pose.position.x, view.pose.position.y,
                    view.pose.position.z,
                    2 * std::atan2(view.pose.orientation.y, view.pose.orientation.w) * 180.0 / 3.14159265358979,
                    static_cast<long long>(chain->last_released));
            }
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const auto* q = reinterpret_cast<const XrCompositionLayerQuad*>(layer);
            auto* chain = from_handle<Swapchain>(reinterpret_cast<uint64_t>(q->subImage.swapchain));
            swprintf(name, 160, L"nullxr_%07u_l%u_quad.bmp", ms, l);
            capture_image(s, chain, q->subImage.imageArrayIndex, capture_dir() + name);
            log("capture t=%ums layer=%u quad eye_visibility=%d rect=%d,%d %dx%d size=%.3fx%.3f "
                "pos=%.4f,%.4f,%.4f rot=%.4f,%.4f,%.4f,%.4f",
                ms, l, static_cast<int>(q->eyeVisibility), q->subImage.imageRect.offset.x,
                q->subImage.imageRect.offset.y, q->subImage.imageRect.extent.width,
                q->subImage.imageRect.extent.height, q->size.width, q->size.height,
                q->pose.position.x, q->pose.position.y, q->pose.position.z, q->pose.orientation.x,
                q->pose.orientation.y, q->pose.orientation.z, q->pose.orientation.w);
        } else {
            log("capture t=%ums layer=%u type=%d not captured", ms, l, static_cast<int>(layer->type));
        }
    }
}

}  // namespace

// --------------------------------------------------------------- the API ----
// Every entry point is exported under its OpenXR name, so the same DLL works
// as a runtime (xrNegotiateLoaderRuntimeInterface) and as a drop-in
// openxr_loader.dll replacement (the mod imports these names directly).

NULLXR_EXPORT XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(const char* layer, uint32_t capacity,
    uint32_t* count, XrExtensionProperties* properties) {
    if (layer != nullptr) {
        return XR_ERROR_API_LAYER_NOT_PRESENT;
    }
    *count = 1;
    if (capacity == 0) {
        return XR_SUCCESS;
    }
    if (capacity < 1) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    strcpy_s(properties[0].extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
    properties[0].extensionVersion = XR_KHR_D3D12_enable_SPEC_VERSION;
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo* info, XrInstance* instance) {
    auto* created = new Instance{};
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        if (strcmp(info->enabledExtensionNames[i], XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0) {
            created->d3d12_enabled = true;
        } else {
            log("xrCreateInstance: unsupported extension %s", info->enabledExtensionNames[i]);
            delete created;
            return XR_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    g_instance = created;
    *instance = to_handle<XrInstance>(created);
    log("xrCreateInstance app=%s api=%u.%u d3d12=%d", info->applicationInfo.applicationName,
        XR_VERSION_MAJOR(info->applicationInfo.apiVersion),
        XR_VERSION_MINOR(info->applicationInfo.apiVersion), created->d3d12_enabled ? 1 : 0);
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance) {
    delete from_handle<Instance>(reinterpret_cast<uint64_t>(instance));
    g_instance = nullptr;
    log("xrDestroyInstance");
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrGetInstanceProperties(XrInstance, XrInstanceProperties* properties) {
    properties->runtimeVersion = XR_MAKE_VERSION(0, 1, 0);
    strcpy_s(properties->runtimeName, "w3vr null HMD");
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrPollEvent(XrInstance, XrEventDataBuffer* buffer) {
    std::scoped_lock lock{g_mutex};
    if (g_events.empty()) {
        return XR_EVENT_UNAVAILABLE;
    }
    *buffer = g_events.front();
    g_events.pop_front();
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrResultToString(XrInstance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE]) {
    snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_RESULT_%d", static_cast<int>(value));
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrStructureTypeToString(XrInstance, XrStructureType value,
    char buffer[XR_MAX_STRUCTURE_NAME_SIZE]) {
    snprintf(buffer, XR_MAX_STRUCTURE_NAME_SIZE, "XR_TYPE_%d", static_cast<int>(value));
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrGetSystem(XrInstance, const XrSystemGetInfo* info, XrSystemId* system) {
    if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
        return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
    }
    *system = kSystemId;
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrGetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* properties) {
    properties->systemId = kSystemId;
    properties->vendorId = 0;
    strcpy_s(properties->systemName, "w3vr null HMD (Quest 3 geometry)");
    properties->graphicsProperties.maxSwapchainImageWidth = 8192;
    properties->graphicsProperties.maxSwapchainImageHeight = 8192;
    properties->graphicsProperties.maxLayerCount = 16;
    properties->trackingProperties.orientationTracking = XR_TRUE;
    properties->trackingProperties.positionTracking = XR_TRUE;
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrEnumerateViewConfigurations(XrInstance, XrSystemId, uint32_t capacity,
    uint32_t* count, XrViewConfigurationType* types) {
    *count = 1;
    if (capacity >= 1) {
        types[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    }
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrGetViewConfigurationProperties(XrInstance, XrSystemId, XrViewConfigurationType type,
    XrViewConfigurationProperties* properties) {
    if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    properties->viewConfigurationType = type;
    properties->fovMutable = XR_TRUE;
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType type,
    uint32_t capacity, uint32_t* count, XrViewConfigurationView* views) {
    if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    *count = 2;
    if (capacity == 0) {
        return XR_SUCCESS;
    }
    if (capacity < 2) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    for (int i = 0; i < 2; ++i) {
        views[i].recommendedImageRectWidth = kRecommendedWidth;
        views[i].recommendedImageRectHeight = kRecommendedHeight;
        views[i].maxImageRectWidth = kMaxWidth;
        views[i].maxImageRectHeight = kMaxHeight;
        views[i].recommendedSwapchainSampleCount = 1;
        views[i].maxSwapchainSampleCount = 1;
    }
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes(XrInstance, XrSystemId, XrViewConfigurationType,
    uint32_t capacity, uint32_t* count, XrEnvironmentBlendMode* modes) {
    *count = 1;
    if (capacity >= 1) {
        modes[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    }
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrGetD3D12GraphicsRequirementsKHR(XrInstance, XrSystemId,
    XrGraphicsRequirementsD3D12KHR* requirements) {
    requirements->adapterLuid = LUID{};
    requirements->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrCreateSession(XrInstance, const XrSessionCreateInfo* info, XrSession* session) {
    const XrGraphicsBindingD3D12KHR* binding = nullptr;
    for (auto* next = static_cast<const XrBaseInStructure*>(info->next); next != nullptr; next = next->next) {
        if (next->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
            binding = reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(next);
        }
    }
    if (binding == nullptr || binding->device == nullptr || binding->queue == nullptr) {
        log("xrCreateSession: no D3D12 binding");
        return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    }
    auto* created = new Session{};
    created->device = binding->device;
    created->queue = binding->queue;
    created->device->AddRef();
    created->queue->AddRef();
    const double hz = std::clamp(env_number(L"W3VR_NULLXR_HZ", 90), 10.0, 1000.0);
    created->period = static_cast<XrTime>(1e9 / hz);
    configure_pacing(created);
    {
        std::scoped_lock lock{g_mutex};
        g_session = created;
        push_state(created, XR_SESSION_STATE_IDLE);
        push_state(created, XR_SESSION_STATE_READY);
    }
    const LUID luid = created->device->GetAdapterLuid();
    log("xrCreateSession device=%p queue=%p luid=%08lX:%08lX rate=%.1fHz", created->device, created->queue,
        luid.HighPart, luid.LowPart, hz);
    *session = to_handle<XrSession>(created);
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrDestroySession(XrSession session) {
    auto* s = from_handle<Session>(reinterpret_cast<uint64_t>(session));
    {
        std::scoped_lock lock{g_mutex};
        if (g_session == s) {
            g_session = nullptr;
        }
    }
    if (s->capture_list) s->capture_list->Release();
    if (s->capture_allocator) s->capture_allocator->Release();
    if (s->capture_fence) s->capture_fence->Release();
    if (s->capture_event) CloseHandle(s->capture_event);
    stop_pacing(s);
    s->queue->Release();
    s->device->Release();
    log("xrDestroySession frames_waited=%llu frames_ended=%llu", static_cast<unsigned long long>(s->frames_waited),
        static_cast<unsigned long long>(s->frames_ended));
    delete s;
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrBeginSession(XrSession session, const XrSessionBeginInfo* info) {
    auto* s = from_handle<Session>(reinterpret_cast<uint64_t>(session));
    if (info->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    s->running = true;
    s->begin_time = now_ns();
    g_motion_origin = s->begin_time;
    s->next_display = s->begin_time + s->period;
    reset_pacing(s);
    s->next_capture = s->begin_time +
        static_cast<XrTime>(env_number(L"W3VR_NULLXR_CAPTURE_START_MS", 30000) * 1e6);
    std::scoped_lock lock{g_mutex};
    push_state(s, XR_SESSION_STATE_SYNCHRONIZED);
    push_state(s, XR_SESSION_STATE_VISIBLE);
    push_state(s, XR_SESSION_STATE_FOCUSED);
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrEndSession(XrSession session) {
    auto* s = from_handle<Session>(reinterpret_cast<uint64_t>(session));
    s->running = false;
    {
        // Like Monado, ending the session discards a waited or begun frame.
        std::scoped_lock pacing_lock{s->pacing.mutex};
        s->pacing.waited = 0;
        s->pacing.begun = 0;
    }
    std::scoped_lock lock{g_mutex};
    push_state(s, XR_SESSION_STATE_IDLE);
    if (s->exit_requested) {
        push_state(s, XR_SESSION_STATE_EXITING);
    }
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrRequestExitSession(XrSession session) {
    auto* s = from_handle<Session>(reinterpret_cast<uint64_t>(session));
    s->exit_requested = true;
    std::scoped_lock lock{g_mutex};
    push_state(s, XR_SESSION_STATE_STOPPING);
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrEnumerateReferenceSpaces(XrSession, uint32_t capacity, uint32_t* count,
    XrReferenceSpaceType* spaces) {
    const XrReferenceSpaceType all[] = {XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL,
        XR_REFERENCE_SPACE_TYPE_STAGE};
    *count = 3;
    for (uint32_t i = 0; i < capacity && i < 3; ++i) {
        spaces[i] = all[i];
    }
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo* info, XrSpace* space) {
    auto* created = new Space{info->referenceSpaceType};
    *space = to_handle<XrSpace>(created);
    log("xrCreateReferenceSpace type=%d", static_cast<int>(info->referenceSpaceType));
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrDestroySpace(XrSpace space) {
    delete from_handle<Space>(reinterpret_cast<uint64_t>(space));
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrLocateSpace(XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* location) {
    location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    // Only yaw about the origin is simulated, so the relative pose is the head yaw difference.
    const auto yaw_of = [time](XrSpace handle) {
        const auto* sp = from_handle<Space>(reinterpret_cast<uint64_t>(handle));
        return sp != nullptr && sp->type == XR_REFERENCE_SPACE_TYPE_VIEW ? head_yaw_at(time) : 0.0f;
    };
    const float yaw = yaw_of(space) - yaw_of(base);
    location->pose = XrPosef{{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}, {0, 0, 0}};
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrLocateViews(XrSession, const XrViewLocateInfo* info, XrViewState* state,
    uint32_t capacity, uint32_t* count, XrView* views) {
    if (info->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    *count = 2;
    if (capacity == 0) {
        return XR_SUCCESS;
    }
    if (capacity < 2) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    static const float half_ipd = static_cast<float>(env_number(L"W3VR_NULLXR_IPD", 0.063) * 0.5);
    state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT |
        XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
    const XrPosef head = head_pose_at(info->displayTime);
    const float yaw = 2 * std::atan2(head.orientation.y, head.orientation.w);
    for (int i = 0; i < 2; ++i) {
        const float x = i == 0 ? -half_ipd : half_ipd;
        // Eye offset (x, 0, 0) rotated by the head yaw about +Y.
        views[i].pose = XrPosef{head.orientation, {x * std::cos(yaw), 0, -x * std::sin(yaw)}};
        views[i].fov = kFov[i];
    }
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrEnumerateSwapchainFormats(XrSession, uint32_t capacity, uint32_t* count, int64_t* formats) {
    const int64_t all[] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT};
    const uint32_t n = static_cast<uint32_t>(sizeof(all) / sizeof(all[0]));
    *count = n;
    if (capacity == 0) {
        return XR_SUCCESS;
    }
    if (capacity < n) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    std::copy(all, all + n, formats);
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* swapchain) {
    auto* s = from_handle<Session>(reinterpret_cast<uint64_t>(session));
    auto* chain = new Swapchain{};
    chain->info = *info;
    chain->format = static_cast<DXGI_FORMAT>(info->format);
    const bool depth = is_depth(chain->format);
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = info->width;
    desc.Height = info->height;
    desc.DepthOrArraySize = static_cast<UINT16>(std::max<uint32_t>(info->arraySize, 1));
    desc.MipLevels = static_cast<UINT16>(std::max<uint32_t>(info->mipCount, 1));
    desc.Format = resource_format(chain->format);
    desc.SampleDesc.Count = std::max<uint32_t>(info->sampleCount, 1);
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (info->usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (info->usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (info->usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!depth) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;  // the capture path needs nothing more
    const D3D12_RESOURCE_STATES initial = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
    for (int i = 0; i < 3; ++i) {
        ID3D12Resource* image = nullptr;
        const HRESULT hr = s->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initial, nullptr,
            IID_PPV_ARGS(&image));
        if (FAILED(hr)) {
            log("xrCreateSwapchain: CreateCommittedResource failed hr=0x%08lX", static_cast<unsigned long>(hr));
            for (auto* created : chain->images) created->Release();
            delete chain;
            return XR_ERROR_RUNTIME_FAILURE;
        }
        chain->images.push_back(image);
    }
    log("xrCreateSwapchain %ux%u array=%u mips=%u format=%lld usage=0x%llx flags=0x%llx", info->width, info->height,
        info->arraySize, info->mipCount, static_cast<long long>(info->format),
        static_cast<unsigned long long>(info->usageFlags), static_cast<unsigned long long>(info->createFlags));
    *swapchain = to_handle<XrSwapchain>(chain);
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain swapchain) {
    auto* chain = from_handle<Swapchain>(reinterpret_cast<uint64_t>(swapchain));
    for (auto* image : chain->images) image->Release();
    delete chain;
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t capacity, uint32_t* count,
    XrSwapchainImageBaseHeader* images) {
    auto* chain = from_handle<Swapchain>(reinterpret_cast<uint64_t>(swapchain));
    const uint32_t n = static_cast<uint32_t>(chain->images.size());
    *count = n;
    if (capacity == 0) {
        return XR_SUCCESS;
    }
    if (capacity < n) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    auto* d3d = reinterpret_cast<XrSwapchainImageD3D12KHR*>(images);
    for (uint32_t i = 0; i < n; ++i) {
        d3d[i].texture = chain->images[i];
    }
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, uint32_t* index) {
    auto* chain = from_handle<Swapchain>(reinterpret_cast<uint64_t>(swapchain));
    if (chain->acquired.size() >= chain->images.size()) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }
    *index = chain->next_acquire;
    chain->acquired.push_back(chain->next_acquire);
    chain->next_acquire = (chain->next_acquire + 1) % static_cast<uint32_t>(chain->images.size());
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*) {
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*) {
    auto* chain = from_handle<Swapchain>(reinterpret_cast<uint64_t>(swapchain));
    if (chain->acquired.empty()) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }
    chain->last_released = chain->acquired.front();
    chain->acquired.pop_front();
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrWaitFrame(XrSession session, const XrFrameWaitInfo*, XrFrameState* state) {
    auto* s = from_handle<Session>(reinterpret_cast<uint64_t>(session));
    if (!s->running) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    Pacing& p = s->pacing;
    const XrTime now = now_ns();
    Prediction prediction;
    {
        std::scoped_lock lock{p.mutex};
        if (p.waited != 0) {
            count_order_error(p, kWaitWhileWaited, p.waited);
        }
        if (p.last_end_return != 0) {
            ++p.stats.end_to_wait_count;
            p.stats.end_to_wait_ms += ns_to_ms(now - p.last_end_return);
            p.last_end_return = 0;
        }
        prediction = p.wivrn ? wivrn_predict(s, now) : Prediction{++p.frame_id};
        p.frames[prediction.id % p.frames.size()] = PacerFrame{prediction.id};
    }
    if (p.wivrn) {
        sleep_until(prediction.wake);
    } else {
        if (s->next_display < now) {
            // Skip missed display periods, like a compositor would.
            const XrTime missed = (now - s->next_display) / s->period + 1;
            s->next_display += missed * s->period;
        }
        sleep_until(s->next_display - s->period);
        prediction.display = s->next_display;
        s->next_display += s->period;
    }
    const XrTime woke = now_ns();
    {
        std::scoped_lock lock{p.mutex};
        PacerFrame& frame = p.frames[prediction.id % p.frames.size()];
        if (frame.id == prediction.id) {
            frame.wake = woke;
            frame.slept = woke - now;
            frame.no_wait = prediction.no_wait;
        }
        p.waited = prediction.id;
        ++p.stats.waits;
        p.stats.slept += woke - now >= 200'000 ? 1 : 0;
        p.stats.no_waits += prediction.no_wait ? 1 : 0;
        p.stats.sleep_ms += ns_to_ms(woke - now);
    }
    state->predictedDisplayTime = prediction.display;
    state->predictedDisplayPeriod = s->period;
    state->shouldRender = XR_TRUE;
    ++s->frames_waited;
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrBeginFrame(XrSession session, const XrFrameBeginInfo*) {
    auto* s = from_handle<Session>(reinterpret_cast<uint64_t>(session));
    if (!s->running) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    Pacing& p = s->pacing;
    std::scoped_lock lock{p.mutex};
    if (p.waited == 0) {
        count_order_error(p, kBeginWithoutWait, p.begun);
    } else {
        if (p.begun != 0) {
            count_order_error(p, kBeginWhileBegun, p.begun);
        }
        p.begun = p.waited;
        p.waited = 0;
    }
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo* info) {
    auto* s = from_handle<Session>(reinterpret_cast<uint64_t>(session));
    if (!s->running) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    const XrTime delivered = now_ns();
    ++s->frames_ended;
    if (s->frames_ended <= 3 || s->frames_ended % 3000 == 0) {
        log("xrEndFrame #%llu layers=%u", static_cast<unsigned long long>(s->frames_ended), info->layerCount);
    }
    pace_end_frame(s, delivered, info->displayTime);
    maybe_capture(s, info);
    {
        std::scoped_lock lock{s->pacing.mutex};
        s->pacing.last_end_return = now_ns();
    }
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function);

NULLXR_EXPORT XrResult XRAPI_CALL xrEnumerateApiLayerProperties(uint32_t, uint32_t*, XrApiLayerProperties*);

namespace {

struct Entry {
    const char* name;
    PFN_xrVoidFunction function;
};

#define NULLXR_ENTRY(api, impl) {#api, reinterpret_cast<PFN_xrVoidFunction>(&api)}
const Entry kEntries[] = {
    {"xrEnumerateApiLayerProperties", reinterpret_cast<PFN_xrVoidFunction>(&xrEnumerateApiLayerProperties)},
    NULLXR_ENTRY(xrGetInstanceProcAddr, GetInstanceProcAddr),
    NULLXR_ENTRY(xrEnumerateInstanceExtensionProperties, EnumerateInstanceExtensionProperties),
    NULLXR_ENTRY(xrCreateInstance, CreateInstance),
    NULLXR_ENTRY(xrDestroyInstance, DestroyInstance),
    NULLXR_ENTRY(xrGetInstanceProperties, GetInstanceProperties),
    NULLXR_ENTRY(xrPollEvent, PollEvent),
    NULLXR_ENTRY(xrResultToString, ResultToString),
    NULLXR_ENTRY(xrStructureTypeToString, StructureTypeToString),
    NULLXR_ENTRY(xrGetSystem, GetSystem),
    NULLXR_ENTRY(xrGetSystemProperties, GetSystemProperties),
    NULLXR_ENTRY(xrEnumerateViewConfigurations, EnumerateViewConfigurations),
    NULLXR_ENTRY(xrGetViewConfigurationProperties, GetViewConfigurationProperties),
    NULLXR_ENTRY(xrEnumerateViewConfigurationViews, EnumerateViewConfigurationViews),
    NULLXR_ENTRY(xrEnumerateEnvironmentBlendModes, EnumerateEnvironmentBlendModes),
    NULLXR_ENTRY(xrGetD3D12GraphicsRequirementsKHR, GetD3D12GraphicsRequirementsKHR),
    NULLXR_ENTRY(xrCreateSession, CreateSession),
    NULLXR_ENTRY(xrDestroySession, DestroySession),
    NULLXR_ENTRY(xrBeginSession, BeginSession),
    NULLXR_ENTRY(xrEndSession, EndSession),
    NULLXR_ENTRY(xrRequestExitSession, RequestExitSession),
    NULLXR_ENTRY(xrEnumerateReferenceSpaces, EnumerateReferenceSpaces),
    NULLXR_ENTRY(xrCreateReferenceSpace, CreateReferenceSpace),
    NULLXR_ENTRY(xrDestroySpace, DestroySpace),
    NULLXR_ENTRY(xrLocateSpace, LocateSpace),
    NULLXR_ENTRY(xrLocateViews, LocateViews),
    NULLXR_ENTRY(xrEnumerateSwapchainFormats, EnumerateSwapchainFormats),
    NULLXR_ENTRY(xrCreateSwapchain, CreateSwapchain),
    NULLXR_ENTRY(xrDestroySwapchain, DestroySwapchain),
    NULLXR_ENTRY(xrEnumerateSwapchainImages, EnumerateSwapchainImages),
    NULLXR_ENTRY(xrAcquireSwapchainImage, AcquireSwapchainImage),
    NULLXR_ENTRY(xrWaitSwapchainImage, WaitSwapchainImage),
    NULLXR_ENTRY(xrReleaseSwapchainImage, ReleaseSwapchainImage),
    NULLXR_ENTRY(xrWaitFrame, WaitFrame),
    NULLXR_ENTRY(xrBeginFrame, BeginFrame),
    NULLXR_ENTRY(xrEndFrame, EndFrame),
};
#undef NULLXR_ENTRY

NULLXR_EXPORT XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function) {
    for (const Entry& entry : kEntries) {
        if (strcmp(entry.name, name) == 0) {
            *function = entry.function;
            return XR_SUCCESS;
        }
    }
    *function = nullptr;
    static std::atomic<int> unsupported_logs{};
    if (unsupported_logs.fetch_add(1) < 64) {
        log("xrGetInstanceProcAddr: unsupported %s", name);
    }
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

}  // namespace

NULLXR_EXPORT XrResult XRAPI_CALL xrEnumerateApiLayerProperties(uint32_t, uint32_t* count, XrApiLayerProperties*) {
    *count = 0;
    return XR_SUCCESS;
}

NULLXR_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* loader,
    XrNegotiateRuntimeRequest* request) {
    if (loader == nullptr || request == nullptr ||
        loader->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        request->structType != XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST ||
        loader->minInterfaceVersion > XR_CURRENT_LOADER_RUNTIME_VERSION ||
        loader->maxInterfaceVersion < XR_CURRENT_LOADER_RUNTIME_VERSION) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    request->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    request->runtimeApiVersion = std::min<XrVersion>(loader->maxApiVersion, XR_CURRENT_API_VERSION);
    request->getInstanceProcAddr = &xrGetInstanceProcAddr;
    log("negotiated with loader: interface %u, api %u.%u.%u", XR_CURRENT_LOADER_RUNTIME_VERSION,
        XR_VERSION_MAJOR(request->runtimeApiVersion), XR_VERSION_MINOR(request->runtimeApiVersion),
        XR_VERSION_PATCH(request->runtimeApiVersion));
    return XR_SUCCESS;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        wchar_t path[MAX_PATH];
        const DWORD n = GetModuleFileNameW(module, path, MAX_PATH);
        std::wstring dir(path, n);
        const size_t slash = dir.find_last_of(L"\\/");
        g_dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
    }
    return TRUE;
}
