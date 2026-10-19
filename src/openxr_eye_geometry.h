#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include <openxr/openxr.h>

namespace w3vr::openxr_eye_geometry {

struct EyeGeometry {
    XrQuaternionf cyclopean_orientation{0.0f, 0.0f, 0.0f, 1.0f};
    XrVector3f cyclopean_position{};
    std::array<XrQuaternionf, 2> eye_orientations{{
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}}};
    std::array<XrQuaternionf, 2> relative_orientations{{
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 1.0f}}};
    std::array<XrVector3f, 2> relative_positions{};
    float baseline_m{};
    float cant_degrees{};
};

struct HmdEulerDegrees {
    float pitch{};
    float yaw{};
    float roll{};
};

struct RedEngineViewEulerDegrees {
    float roll{};
    float pitch{};
    float yaw{};
};

// REDengine represents a perspective view with a symmetric vertical FOV and
// aspect ratio plus a pixel-space projection-center offset. This structure
// records both the conventional projection/optical-center displacement and
// REDengine's native field convention. V1043's persistent writer run and the
// decompiled row-vector translation proved that both native fields use the
// same NDC sign as the optical center; V980 had not reached that boundary.
struct AsymmetricProjectionDescriptor {
    float vertical_fov_degrees{};
    float aspect{};
    float center_ndc_x{};
    float center_ndc_y{};
    float optical_center_offset_px_x{};
    float optical_center_offset_px_y{};
    float redengine_center_offset_px_x{};
    float redengine_center_offset_px_y{};
    float horizontal_tangent_span{};
    float vertical_tangent_span{};
};

// A symmetric producer stores a centered angular envelope in one full image.
// Strict Stereo submits one eye-specific part of that image. The represented
// FOV is derived back from the quantized integer rectangle so OpenXR rays stay
// paired with the exact source pixels selected by imageRect.
struct SymmetricEyeSubimage {
    XrRect2Di image_rect{};
    XrFovf represented_fov{};
};


XrQuaternionf multiply(
    const XrQuaternionf& left, const XrQuaternionf& right);
XrQuaternionf conjugate(const XrQuaternionf& quaternion);
bool normalize(
    const XrQuaternionf& quaternion, XrQuaternionf& normalized);
XrVector3f rotate(
    const XrQuaternionf& rotation, const XrVector3f& vector);
XrQuaternionf from_hmd_euler_degrees(
    float pitch, float yaw, float roll);
HmdEulerDegrees to_hmd_euler_degrees(
    const XrQuaternionf& quaternion);
float nearest_equivalent_degrees(float angle, float reference);
XrQuaternionf openxr_to_redengine_local_orientation(
    const XrQuaternionf& orientation);
XrQuaternionf from_redengine_view_euler_degrees(
    float roll, float pitch, float yaw);
RedEngineViewEulerDegrees to_redengine_view_euler_degrees(
    const XrQuaternionf& orientation);
bool compute(const std::array<XrView, 2>& views, EyeGeometry& geometry);
// Re-express the same physical eyes around a fixed HUD-plane orientation.
// The cyclopean position and eye poses remain current, so only headset
// rotation is removed from the HUD plane; stereo parallax stays live.
bool with_hud_plane_orientation(
    const EyeGeometry& current,
    const XrQuaternionf& plane_orientation,
    EyeGeometry& result);
bool derive_asymmetric_projection_descriptor(
    const XrFovf& fov,
    uint32_t render_width,
    uint32_t render_height,
    AsymmetricProjectionDescriptor& descriptor);

// Converts the headset optical centre and the legacy horizontal convergence
// into source-pixel offsets for the scaled HUD composite. Pixel-space Y grows
// down while OpenXR NDC Y grows up, so a positive optical-centre Y becomes a
// positive source offset and moves the displayed HUD upward.
bool derive_asymmetric_hud_source_shift(
    const AsymmetricProjectionDescriptor& descriptor,
    float base_hud_size,
    float asymmetric_hud_size,
    int legacy_horizontal_shift_px,
    int& source_shift_x_px,
    int& source_shift_y_px);

// Scales all four tangent extents around the eye's optical axis. This changes
// angular presentation size while preserving the asymmetric principal point.
// A scale of 1 returns the original OpenXR FOV exactly.
bool scale_asymmetric_projection_fov(
    const XrFovf& fov,
    float scale,
    XrFovf& scaled_fov);


bool derive_pixel_exact_subimage_fov(
    const XrFovf& content_fov,
    const XrRect2Di& content_rect,
    const XrRect2Di& submitted_rect,
    XrFovf& represented_fov);

// Selects the integer sub-image which represents target_fov inside a centered
// producer image. Tangent-space leading edges are floored and trailing edges
// are ceiled so quantization never drops a requested ray at the boundary.
bool derive_symmetric_eye_subimage(
    const XrFovf& content_fov,
    const XrFovf& target_fov,
    const XrRect2Di& content_rect,
    SymmetricEyeSubimage& subimage);

// Converts a legacy left-eye source-pixel shift, captured on a parallel-view
// reference headset, into the inverse physical distance of a cyclopean HUD
// plane. A negative left-eye shift produces a positive distance in front of
// the viewer. Including hud_size preserves the legacy shader's exact shift
// semantics while making the resulting plane portable across eye geometry.
float inverse_hud_distance_from_parallel_reference(
    float left_eye_shift_px,
    float hud_size,
    float reference_baseline_m,
    float reference_source_width_px,
    float reference_horizontal_tangent_span);

// Builds four perspective-correct clip-space corners for a cyclopean HUD
// plane. source_fov describes the symmetric projection that generated the HUD
// texture; target_fov is the exact raw FOV submitted for this eye. The output
// order is top-left, top-right, bottom-left, bottom-right.
// hud_size scales the plane about the source FOV centre, or vertically about
// scale_origin_tangent_y when that is finite (e.g. the centre of the visible
// image, so a reduced HUD stays centred in view). At hud_size 1 the origin has
// no effect.
bool build_cyclopean_hud_plane_clip_positions(
    const EyeGeometry& geometry,
    uint32_t eye,
    const XrFovf& source_fov,
    const XrFovf& target_fov,
    float hud_size,
    float inverse_distance_m,
    std::array<float, 16>& clip_positions,
    float scale_origin_tangent_y = NAN);

// Vertical tangent of the centre of the visible image for a raw OpenXR FOV
// (Quest 3: +44/-55 degrees gives tan -0.231, i.e. 13 degrees below forward).
float visible_center_tangent_y(const XrFovf& fov);

// World markers (interaction prompts, NPC icons) are placed by the game script
// from CameraDirector.WorldVectorToViewRatio: clip.xy / clip.w of a row-vector
// world-to-clip matrix (y negated), mapped 0..1 across the Scaleform stage of
// the HUD texture. The HUD texture is then drawn on the cyclopean plane of
// build_cyclopean_hud_plane_clip_positions. Projecting a marker with the
// frustum that the plane's stage window subtends from the cyclopean head puts
// its texel exactly in the marker's direction. For a centred marker camera
// with the plane's orientation (half tangents marker_half_tangent_x/y), that
// frustum is a linear change of two clip columns:
//   col_x' = scale_x * col_x + offset_x * col_w
//   col_y' = scale_y * col_y + offset_y * col_w
// The inputs are kept for the inverse (checks and diagnostics).
struct HudMarkerClipMapping {
    float scale_x{1.0f};
    float offset_x{};
    float scale_y{1.0f};
    float offset_y{};
    // Source (HUD texture) tangent bounds, as in the composite's source_fov.
    float source_left{};
    float source_right{};
    float source_up{};
    float source_down{};
    float hud_size{1.0f};
    // Plane scale origin (x: source centre; y: the composite's origin).
    float origin_x{};
    float origin_y{};
    // Fraction of the HUD texture width/height spanned by ratio -1..1.
    float stage_scale_x{1.0f};
    float stage_scale_y{1.0f};
    float marker_half_tangent_x{1.0f};
    float marker_half_tangent_y{1.0f};
};

// Fraction of a width x height HUD texture covered by the script's view-ratio
// range. hud.ws UpdateScaleformStageSize picks a centred stage rectangle by
// aspect (4:3 1920x1440, 21:9 2520x1080, 43:18 2580x1080, else 1920x1080)
// and Scaleform letterboxes the 1920x1080 movie into the texture (showAll).
bool scaleform_stage_ratio_scale(
    uint32_t width,
    uint32_t height,
    float& scale_x,
    float& scale_y);

// source_fov and hud_size as passed to build_cyclopean_hud_plane_clip_positions;
// scale_origin_tangent_y is the composite's vertical scale origin (NaN = the
// source centre). No tuned parameters: everything follows from the plane.
bool hud_marker_clip_mapping(
    const XrFovf& source_fov,
    float marker_half_tangent_x,
    float marker_half_tangent_y,
    float hud_size,
    float scale_origin_tangent_y,
    float stage_scale_x,
    float stage_scale_y,
    HudMarkerClipMapping& mapping);

// Applies the mapping to a row-vector world-to-clip matrix (m[row * 4 + c]).
void apply_hud_marker_clip_mapping(
    const HudMarkerClipMapping& mapping, float* world_to_clip);

// Inverse through the composite's geometry: the plane tangent (x right, y up)
// of the HUD texel at a script view ratio (x right, y down, -1..1).
bool hud_marker_ratio_to_plane_tangent(
    const HudMarkerClipMapping& mapping,
    float ratio_x,
    float ratio_y,
    float& plane_tangent_x,
    float& plane_tangent_y);

} // namespace w3vr::openxr_eye_geometry
