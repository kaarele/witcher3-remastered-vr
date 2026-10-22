#pragma once

// 360-degree capture: step planning and equirectangular assembly.
//
// The mod turns the game camera through a list of steps while the game is
// paused, reads each eye image back before the HUD composite and assembles
// the views here into an equirectangular picture (mono) or a top-bottom
// omnidirectional-stereo picture (slit-scan ODS). Everything in this file is
// plain C++ so it can be unit-tested without D3D12 or OpenXR.
//
// Conventions: view space is OpenXR's (x right, y up, -z forward). Longitude
// grows to the right, latitude upwards; longitude 0 / latitude 0 is the
// capture's forward direction. A step's camera looks at (yaw_deg, pitch_deg)
// in that frame with no roll.

#include <cstdint>
#include <vector>

namespace w3vr::capture360 {

enum class Mode { Mono, StereoOds };

// OpenXR XrFovf angles in radians (left and down are negative).
struct Fov {
    float left{};
    float right{};
    float up{};
    float down{};
};

struct Step {
    float yaw_deg{};    // longitude of the camera's forward direction
    float pitch_deg{};  // latitude of the camera's forward direction
    bool stereo{};      // true: render with the eye offset (ODS band)
    bool band{};        // part of the ODS band (strips) rather than a full view
};

// A captured image or vertical strip of one. x0/width select the stored
// columns of a full_width x full_height image rendered with `fov`.
struct View {
    float yaw_deg{};
    float pitch_deg{};
    Fov fov{};
    uint32_t full_width{};
    uint32_t full_height{};
    uint32_t x0{};
    uint32_t width{};
    uint32_t height{};
    int eye{-1};            // 0 left, 1 right, -1 shared (mono / poles)
    bool band{};
    float gain{1.0f};       // exposure correction, solved before assembly
    std::vector<uint8_t> bgra;  // width * height * 4
};

struct Image {
    uint32_t width{};
    uint32_t height{};
    std::vector<uint8_t> bgra;
};

struct PlanOptions {
    float ods_step_deg{3.0f};
    float pole_pitch_deg{55.0f};   // pitch of the two mono pole rings
    float zenith_pitch_deg{85.0f}; // near-vertical views (engine views avoid exactly 90)
};

std::vector<Step> plan_steps(Mode mode, const PlanOptions& options);

// Columns [x0, x1) of a full image that cover +-half_angle_deg around the
// image's forward direction (tangent 0), widened by margin_px and clamped.
void strip_columns(const Fov& fov, uint32_t full_width, float half_angle_deg,
    uint32_t margin_px, uint32_t& x0, uint32_t& x1);

// Per-view exposure gains from overlapping regions (auto exposure differs
// between viewing directions). Leaves gain = 1 when there is no overlap.
void solve_gains(std::vector<View>& views, uint32_t sample_width);

// Assembly quality options.
//  - Two-band blend: fine detail comes from the most central view of each
//    direction (narrow seams, no averaging of slightly different views), while
//    brightness and haze differences between views are spread over a wide
//    area, so no exposure step or streak shows at the seams.
//  - Bicubic (Catmull-Rom) sampling keeps more detail than bilinear.
//  - A mild unsharp mask undoes the softening of the equirect resampling.
struct AssembleOptions {
    bool two_band{true};
    bool bicubic{true};
    float wide_feather{0.25f};  // fraction of the view's smaller side
    float sharpen{0.35f};       // unsharp-mask amount, 0 = off
};

// Mono equirect from full views (no parallax).
Image assemble_mono(const std::vector<const View*>& views,
    uint32_t out_width, unsigned threads, const AssembleOptions& options = {});

// One eye of an ODS panorama: band strips of this eye (nearest steps, linear
// blend between neighbours) and the shared pole views above/below the band.
Image assemble_ods_eye(const std::vector<const View*>& band,
    const std::vector<const View*>& poles, float ods_step_deg,
    uint32_t out_width, unsigned threads, const AssembleOptions& options = {});

// Rotates an equirect picture about the vertical axis: content moves
// `degrees` to the LEFT (longitude decreases), e.g. 45 puts the capture's
// forward direction 45 degrees left of the picture centre.
void rotate_left(Image& image, float degrees);

// Top-bottom stack (left eye on top), as the loading-background viewer expects.
Image stack_top_bottom(const Image& top, const Image& bottom);

}  // namespace w3vr::capture360
