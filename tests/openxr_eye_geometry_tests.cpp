#include "openxr_eye_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace eye_geometry = w3vr::openxr_eye_geometry;

namespace {

constexpr float kPi = 3.14159265358979323846f;
int failures{};

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

float quaternion_dot(
    const XrQuaternionf& left, const XrQuaternionf& right) {
    return left.x * right.x + left.y * right.y +
        left.z * right.z + left.w * right.w;
}

bool quaternion_near(
    const XrQuaternionf& left,
    const XrQuaternionf& right,
    float max_angle_radians = 1.0e-4f) {
    XrQuaternionf normalized_left{};
    XrQuaternionf normalized_right{};
    if (!eye_geometry::normalize(left, normalized_left) ||
        !eye_geometry::normalize(right, normalized_right)) {
        return false;
    }
    auto difference = eye_geometry::multiply(
        eye_geometry::conjugate(normalized_left), normalized_right);
    if (difference.w < 0.0f) {
        difference = {
            -difference.x, -difference.y, -difference.z, -difference.w};
    }
    const float vector_length = std::sqrt(
        difference.x * difference.x + difference.y * difference.y +
        difference.z * difference.z);
    return 2.0f * std::atan2(vector_length, difference.w) <=
        max_angle_radians;
}

bool vector_near(
    const XrVector3f& left,
    const XrVector3f& right,
    float tolerance = 2.0e-6f) {
    return std::fabs(left.x - right.x) <= tolerance &&
        std::fabs(left.y - right.y) <= tolerance &&
        std::fabs(left.z - right.z) <= tolerance;
}

XrQuaternionf yaw(float degrees) {
    return eye_geometry::from_hmd_euler_degrees(0.0f, degrees, 0.0f);
}

std::array<XrView, 2> make_views(
    const XrQuaternionf& left_orientation,
    const XrQuaternionf& right_orientation,
    const XrVector3f& center,
    const XrQuaternionf& head_orientation,
    const XrVector3f& left_local,
    const XrVector3f& right_local) {
    std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    views[0].pose.orientation = left_orientation;
    views[1].pose.orientation = right_orientation;
    const auto left_world = eye_geometry::rotate(head_orientation, left_local);
    const auto right_world = eye_geometry::rotate(head_orientation, right_local);
    views[0].pose.position = {
        center.x + left_world.x,
        center.y + left_world.y,
        center.z + left_world.z};
    views[1].pose.position = {
        center.x + right_world.x,
        center.y + right_world.y,
        center.z + right_world.z};
    return views;
}

void require_reconstruction(
    const std::array<XrView, 2>& views,
    const eye_geometry::EyeGeometry& geometry,
    const char* label) {
    for (size_t eye = 0; eye < 2; ++eye) {
        const auto reconstructed_orientation = eye_geometry::multiply(
            geometry.cyclopean_orientation,
            geometry.relative_orientations[eye]);
        require(quaternion_near(
            reconstructed_orientation, views[eye].pose.orientation), label);
        const auto relative_world = eye_geometry::rotate(
            geometry.cyclopean_orientation,
            geometry.relative_positions[eye]);
        const XrVector3f reconstructed_position{
            geometry.cyclopean_position.x + relative_world.x,
            geometry.cyclopean_position.y + relative_world.y,
            geometry.cyclopean_position.z + relative_world.z};
        require(vector_near(
            reconstructed_position, views[eye].pose.position), label);
    }
}

void test_parallel() {
    const auto head = eye_geometry::from_hmd_euler_degrees(12.0f, -21.0f, 3.0f);
    const XrVector3f center{0.2f, 1.6f, -0.4f};
    const auto views = make_views(
        head, head, center, head,
        {-0.032f, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f});
    eye_geometry::EyeGeometry geometry{};
    require(eye_geometry::compute(views, geometry), "parallel compute");
    require(quaternion_near(
        geometry.cyclopean_orientation, head), "parallel midpoint");
    require(quaternion_near(
        geometry.relative_orientations[0], {0, 0, 0, 1}),
        "parallel left identity");
    require(quaternion_near(
        geometry.relative_orientations[1], {0, 0, 0, 1}),
        "parallel right identity");
    require(vector_near(
        geometry.relative_positions[0], {-0.032f, 0, 0}),
        "parallel left position");
    require(vector_near(
        geometry.relative_positions[1], {0.032f, 0, 0}),
        "parallel right position");
    require(std::fabs(geometry.cant_degrees) < 1.0e-4f,
        "parallel zero cant");
    require_reconstruction(views, geometry, "parallel reconstruction");
}

void test_symmetric_cant_and_antipodal() {
    const auto head = eye_geometry::from_hmd_euler_degrees(23.0f, -37.0f, 14.0f);
    const XrVector3f center{0.31f, 1.42f, -0.77f};
    auto left = eye_geometry::multiply(head, yaw(5.0f));
    auto right = eye_geometry::multiply(head, yaw(-5.0f));
    auto views = make_views(
        left, right, center, head,
        {-0.032f, 0.001f, -0.002f},
        {0.032f, -0.001f, 0.002f});
    eye_geometry::EyeGeometry geometry{};
    require(eye_geometry::compute(views, geometry), "symmetric cant compute");
    require(quaternion_near(
        geometry.cyclopean_orientation, head), "symmetric cant midpoint");
    require(std::fabs(geometry.cant_degrees - 10.0f) < 0.001f,
        "symmetric total cant");
    require(quaternion_near(
        geometry.relative_orientations[0], yaw(5.0f)),
        "symmetric left relative");
    require(quaternion_near(
        geometry.relative_orientations[1], yaw(-5.0f)),
        "symmetric right relative");
    require_reconstruction(views, geometry, "symmetric reconstruction");

    views[1].pose.orientation = {
        -views[1].pose.orientation.x,
        -views[1].pose.orientation.y,
        -views[1].pose.orientation.z,
        -views[1].pose.orientation.w};
    eye_geometry::EyeGeometry antipodal{};
    require(eye_geometry::compute(views, antipodal), "antipodal compute");
    require(quaternion_near(
        antipodal.cyclopean_orientation,
        geometry.cyclopean_orientation), "antipodal midpoint");
    require_reconstruction(views, antipodal, "antipodal reconstruction");
}

void test_asymmetric_cant() {
    const auto head = eye_geometry::from_hmd_euler_degrees(-8.0f, 31.0f, -4.0f);
    const auto left = eye_geometry::multiply(head, yaw(7.0f));
    const auto right = eye_geometry::multiply(head, yaw(-3.0f));
    const XrVector3f center{-0.4f, 1.3f, 0.2f};
    const auto views = make_views(
        left, right, center, head,
        {-0.033f, 0.0015f, -0.001f},
        {0.033f, -0.0015f, 0.001f});
    eye_geometry::EyeGeometry geometry{};
    require(eye_geometry::compute(views, geometry), "asymmetric compute");
    const auto expected_midpoint = eye_geometry::multiply(head, yaw(2.0f));
    require(quaternion_near(
        geometry.cyclopean_orientation, expected_midpoint),
        "asymmetric midpoint");
    require(quaternion_near(
        geometry.relative_orientations[0], yaw(5.0f)),
        "asymmetric left relative");
    require(quaternion_near(
        geometry.relative_orientations[1], yaw(-5.0f)),
        "asymmetric right relative");
    require_reconstruction(views, geometry, "asymmetric reconstruction");
}

void test_normalization_and_invalid_input() {
    std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    views[0].pose.orientation = {0.0f, 0.0f, 0.0f, 2.0f};
    views[1].pose.orientation = {0.0f, 0.0f, 0.0f, -3.0f};
    views[0].pose.position = {-0.032f, 0.0f, 0.0f};
    views[1].pose.position = {0.032f, 0.0f, 0.0f};
    eye_geometry::EyeGeometry geometry{};
    require(eye_geometry::compute(views, geometry), "non-unit compute");
    require(quaternion_near(
        geometry.cyclopean_orientation, {0, 0, 0, 1}),
        "non-unit normalization");

    views[0].pose.orientation = {};
    require(!eye_geometry::compute(views, geometry), "zero quaternion rejected");
    views[0].pose.orientation = {0, 0, 0, 1};
    views[1].pose.position.x = std::numeric_limits<float>::quiet_NaN();
    require(!eye_geometry::compute(views, geometry), "NaN position rejected");
}

void test_euler_roundtrip_and_wrap() {
    const auto source = eye_geometry::from_hmd_euler_degrees(23.0f, -37.0f, 14.0f);
    const auto euler = eye_geometry::to_hmd_euler_degrees(source);
    const auto reconstructed = eye_geometry::from_hmd_euler_degrees(
        euler.pitch, euler.yaw, euler.roll);
    require(quaternion_near(source, reconstructed), "Euler roundtrip");
    require(std::fabs(
        eye_geometry::nearest_equivalent_degrees(-179.0f, 181.0f) - 181.0f) <
        1.0e-5f, "nearest angle positive wrap");
    require(std::fabs(
        eye_geometry::nearest_equivalent_degrees(179.0f, -181.0f) + 181.0f) <
        1.0e-5f, "nearest angle negative wrap");
}

void test_redengine_descriptor_composition() {
    // Captured V1017 REDengine descriptor/basis sample. This anchors the
    // decompiled Rz(view[6]) * Rx(view[5]) * Ry(view[4]) convention to an
    // actual rebuilt camera rather than testing only our own round-trip.
    const auto captured =
        eye_geometry::from_redengine_view_euler_degrees(
            0.0f, -9.5f, -40.3269f);
    require(vector_near(
        eye_geometry::rotate(captured, {1.0f, 0.0f, 0.0f}),
        {0.7624f, -0.6471f, 0.0f}, 2.0e-4f),
        "captured REDengine right basis");
    require(vector_near(
        eye_geometry::rotate(captured, {0.0f, 1.0f, 0.0f}),
        {0.6383f, 0.7519f, -0.1650f}, 2.0e-4f),
        "captured REDengine forward basis");
    require(vector_near(
        eye_geometry::rotate(captured, {0.0f, 0.0f, 1.0f}),
        {0.1068f, 0.1258f, 0.9863f}, 2.0e-4f),
        "captured REDengine up basis");

    const auto base =
        eye_geometry::from_redengine_view_euler_degrees(
            -11.0f, 27.0f, 43.0f);
    const auto base_euler =
        eye_geometry::to_redengine_view_euler_degrees(base);
    require(std::fabs(base_euler.roll + 11.0f) < 1.0e-4f,
        "REDengine roll roundtrip");
    require(std::fabs(base_euler.pitch - 27.0f) < 1.0e-4f,
        "REDengine pitch roundtrip");
    require(std::fabs(base_euler.yaw - 43.0f) < 1.0e-4f,
        "REDengine yaw roundtrip");

    const auto xr_pitch = eye_geometry::openxr_to_redengine_local_orientation(
        eye_geometry::from_hmd_euler_degrees(7.0f, 0.0f, 0.0f));
    const auto xr_yaw = eye_geometry::openxr_to_redengine_local_orientation(
        eye_geometry::from_hmd_euler_degrees(0.0f, 7.0f, 0.0f));
    const auto xr_roll = eye_geometry::openxr_to_redengine_local_orientation(
        eye_geometry::from_hmd_euler_degrees(0.0f, 0.0f, 7.0f));
    const auto pitch_euler =
        eye_geometry::to_redengine_view_euler_degrees(xr_pitch);
    const auto yaw_euler =
        eye_geometry::to_redengine_view_euler_degrees(xr_yaw);
    const auto roll_euler =
        eye_geometry::to_redengine_view_euler_degrees(xr_roll);
    require(std::fabs(pitch_euler.pitch - 7.0f) < 1.0e-4f,
        "OpenXR pitch maps to REDengine view[5]");
    require(std::fabs(yaw_euler.yaw - 7.0f) < 1.0e-4f,
        "OpenXR yaw maps to REDengine view[6]");
    require(std::fabs(roll_euler.roll + 7.0f) < 1.0e-4f,
        "OpenXR roll maps with REDengine opposite sign");

    const auto relative_openxr =
        eye_geometry::from_hmd_euler_degrees(3.0f, 5.0f, 2.0f);
    const auto relative_engine =
        eye_geometry::openxr_to_redengine_local_orientation(
            relative_openxr);
    const auto final_orientation =
        eye_geometry::multiply(base, relative_engine);
    const auto final_euler =
        eye_geometry::to_redengine_view_euler_degrees(final_orientation);
    const auto reconstructed =
        eye_geometry::from_redengine_view_euler_degrees(
            final_euler.roll, final_euler.pitch, final_euler.yaw);
    require(quaternion_near(final_orientation, reconstructed),
        "REDengine local eye-pose composition");

    const auto near_gimbal =
        eye_geometry::from_redengine_view_euler_degrees(
            12.0f, 89.0f, -28.0f);
    const auto near_gimbal_euler =
        eye_geometry::to_redengine_view_euler_degrees(near_gimbal);
    const auto near_gimbal_reconstructed =
        eye_geometry::from_redengine_view_euler_degrees(
            near_gimbal_euler.roll,
            near_gimbal_euler.pitch,
            near_gimbal_euler.yaw);
    require(quaternion_near(
        near_gimbal, near_gimbal_reconstructed, 2.0e-4f),
        "REDengine near-gimbal roundtrip");
}

float projected_tangent_x(
    const std::array<float, 16>& clip_positions,
    uint32_t corner,
    const XrFovf& target_fov) {
    const float ndc_x = clip_positions[corner * 4 + 0] /
        clip_positions[corner * 4 + 3];
    const float left = std::tan(target_fov.angleLeft);
    const float right = std::tan(target_fov.angleRight);
    return left + (ndc_x + 1.0f) * 0.5f * (right - left);
}

void test_quest_reference_hud_plane() {
    constexpr float baseline = 0.065330f;
    constexpr float width = 3072.0f;
    constexpr float horizontal_span = 2.75276502f;
    constexpr float source_half_horizontal = 0.942478f;
    constexpr float source_half_vertical = 0.959931f;
    const XrFovf source_fov{
        -source_half_horizontal, source_half_horizontal,
        source_half_vertical, -source_half_vertical};
    const auto views = make_views(
        {0, 0, 0, 1}, {0, 0, 0, 1},
        {0, 0, 0}, {0, 0, 0, 1},
        {-baseline * 0.5f, 0, 0},
        {baseline * 0.5f, 0, 0});
    eye_geometry::EyeGeometry geometry{};
    require(eye_geometry::compute(views, geometry),
        "Quest HUD reference geometry");

    const float inverse_distance =
        eye_geometry::inverse_hud_distance_from_parallel_reference(
            -36.0f, 1.0f, baseline, width, horizontal_span);
    require(std::fabs(inverse_distance - 0.98756973f) < 2.0e-6f,
        "Quest HUD reference inverse distance");
    require(std::fabs(1.0f / inverse_distance - 1.0125867f) < 2.0e-5f,
        "Quest HUD reference physical distance");

    for (uint32_t eye = 0; eye < 2; ++eye) {
        std::array<float, 16> clip_positions{};
        require(eye_geometry::build_cyclopean_hud_plane_clip_positions(
            geometry, eye, source_fov, source_fov, 1.0f,
            inverse_distance, clip_positions),
            "Quest HUD reference clip build");
        const float source_left = std::tan(source_fov.angleLeft);
        const float projected_left = projected_tangent_x(
            clip_positions, 0, source_fov);
        const float output_shift_px =
            (projected_left - source_left) * width / horizontal_span;
        const float expected_output_shift = eye == 0 ? 36.0f : -36.0f;
        require(std::fabs(output_shift_px - expected_output_shift) < 0.002f,
            "Quest HUD plane preserves legacy per-eye offset");
    }

    // Full VR text defaults directly to gameplay-HUD depth at size 1.0.
    // Changing size must vary shift inversely to retain that physical depth.
    const float full_vr_size = 1.0f;
    const float full_vr_inverse_distance =
        eye_geometry::inverse_hud_distance_from_parallel_reference(
            -36.0f, full_vr_size, baseline, width, horizontal_span);
    require(std::fabs(full_vr_inverse_distance - inverse_distance) < 2.0e-6f,
        "Quest Full VR HUD matches gameplay depth");
    std::array<float, 16> full_vr_clip{};
    require(eye_geometry::build_cyclopean_hud_plane_clip_positions(
        geometry, 0, source_fov, source_fov, full_vr_size,
        full_vr_inverse_distance, full_vr_clip),
        "Quest Full VR HUD clip build");
    const float scaled_source_left =
        -full_vr_size * std::tan(source_half_horizontal);
    const float full_vr_output_shift =
        (projected_tangent_x(full_vr_clip, 0, source_fov) -
            scaled_source_left) * width / horizontal_span;
    require(std::fabs(full_vr_output_shift - 36.0f) < 0.01f,
        "Quest Full VR HUD preserves gameplay-depth offset");
    const float large_full_vr_inverse_distance =
        eye_geometry::inverse_hud_distance_from_parallel_reference(
            -24.0f, 1.50f, baseline, width, horizontal_span);
    require(std::fabs(large_full_vr_inverse_distance - inverse_distance) <
            2.0e-6f,
        "Quest Full VR HUD size is independent from physical depth");
}

void test_canted_hud_plane_and_invalid_fallback() {
    constexpr float pimax_baseline = 0.068047f;
    const auto left = yaw(10.0f);
    const auto right = yaw(-10.0f);
    const auto views = make_views(
        left, right, {0, 0, 0}, {0, 0, 0, 1},
        {-pimax_baseline * 0.5f, 0, 0},
        {pimax_baseline * 0.5f, 0, 0});
    eye_geometry::EyeGeometry geometry{};
    require(eye_geometry::compute(views, geometry),
        "Pimax HUD geometry");
    constexpr float source_half_horizontal = 0.816143f;
    constexpr float source_half_vertical = 0.786983f;
    const XrFovf source_fov{
        -source_half_horizontal, source_half_horizontal,
        source_half_vertical, -source_half_vertical};
    const XrFovf raw_fovs[2]{
        {-0.900349f, 0.931882f, 0.903724f, -0.903724f},
        {-0.931882f, 0.900349f, 0.903724f, -0.903724f}};
    const float inverse_distance =
        eye_geometry::inverse_hud_distance_from_parallel_reference(
            -36.0f, 1.0f, 0.065330f, 3072.0f, 2.75276502f);
    for (uint32_t eye = 0; eye < 2; ++eye) {
        std::array<float, 16> clip_positions{};
        require(eye_geometry::build_cyclopean_hud_plane_clip_positions(
            geometry, eye, source_fov, raw_fovs[eye], 1.0f,
            inverse_distance, clip_positions),
            "Pimax canted HUD clip build");
        for (uint32_t corner = 0; corner < 4; ++corner) {
            require(std::isfinite(clip_positions[corner * 4 + 0]) &&
                std::isfinite(clip_positions[corner * 4 + 1]) &&
                clip_positions[corner * 4 + 3] > 0.0f,
                "Pimax canted HUD finite forward corner");
            const float source_edge = corner == 0 || corner == 2
                ? -std::tan(source_half_horizontal)
                : std::tan(source_half_horizontal);
            const auto& origin = geometry.relative_positions[eye];
            const XrVector3f corner_to_eye{
                source_edge - inverse_distance * origin.x,
                -inverse_distance * origin.y,
                -1.0f - inverse_distance * origin.z};
            const auto corner_eye_space = eye_geometry::rotate(
                eye_geometry::conjugate(
                    geometry.relative_orientations[eye]),
                corner_to_eye);
            const float expected_corner_tangent =
                corner_eye_space.x / -corner_eye_space.z;
            require(std::fabs(
                projected_tangent_x(
                    clip_positions, corner, raw_fovs[eye]) -
                expected_corner_tangent) < 2.0e-5f,
                "Pimax raw-FOV clip preserves physical corner tangent");
        }

        // Project the plane center directly. Expressed in the symmetric source
        // pixel scale, the current Pimax capture should need about 215 pixels
        // per eye, not the old universal 36-pixel shift.
        const auto& origin = geometry.relative_positions[eye];
        const XrVector3f center_to_eye{
            -inverse_distance * origin.x,
            -inverse_distance * origin.y,
            -1.0f - inverse_distance * origin.z};
        const auto eye_space = eye_geometry::rotate(
            eye_geometry::conjugate(
                geometry.relative_orientations[eye]),
            center_to_eye);
        const float center_tangent = eye_space.x / -eye_space.z;
        const float equivalent_output_px = center_tangent * 2160.0f /
            (2.0f * std::tan(source_half_horizontal));
        const float expected = eye == 0 ? 214.7f : -214.7f;
        require(std::fabs(equivalent_output_px - expected) < 0.8f,
            "Pimax canted HUD automatic center compensation");
    }

    std::array<float, 16> invalid_clip{};
    XrFovf invalid_fov = source_fov;
    invalid_fov.angleRight = invalid_fov.angleLeft;
    require(!eye_geometry::build_cyclopean_hud_plane_clip_positions(
        geometry, 0, source_fov, invalid_fov, 1.0f,
        inverse_distance, invalid_clip),
        "HUD plane rejects invalid target FOV");
    require(std::isnan(
        eye_geometry::inverse_hud_distance_from_parallel_reference(
            -36.0f, 1.0f, 0.0f, 3072.0f, 2.75276502f)),
        "HUD plane rejects invalid reference baseline");
}

void test_controller_locked_hud_plane() {
    const auto turned_head = yaw(20.0f);
    const auto views = make_views(
        turned_head, turned_head, {0.0f, 1.6f, 0.0f}, turned_head,
        {-0.032f, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f});
    eye_geometry::EyeGeometry current{};
    eye_geometry::EyeGeometry fixed{};
    require(eye_geometry::compute(views, current),
        "controller-locked HUD current eye geometry");
    require(eye_geometry::with_hud_plane_orientation(
        current, {0.0f, 0.0f, 0.0f, 1.0f}, fixed),
        "controller-locked HUD fixed plane geometry");
    require_reconstruction(views, fixed,
        "controller-locked HUD retains physical eye poses");
    const XrFovf fov{-0.9f, 0.9f, 0.9f, -0.9f};
    std::array<float, 16> headlocked_clip{};
    std::array<float, 16> fixed_clip{};
    require(eye_geometry::build_cyclopean_hud_plane_clip_positions(
        current, 0, fov, fov, 0.5f, 0.5f, headlocked_clip),
        "headlocked HUD reference clip");
    require(eye_geometry::build_cyclopean_hud_plane_clip_positions(
        fixed, 0, fov, fov, 0.5f, 0.5f, fixed_clip),
        "controller-locked HUD fixed clip");
    const float headlocked_center =
        (headlocked_clip[0] / headlocked_clip[3] +
            headlocked_clip[4] / headlocked_clip[7]) * 0.5f;
    const float fixed_center =
        (fixed_clip[0] / fixed_clip[3] +
            fixed_clip[4] / fixed_clip[7]) * 0.5f;
    require(std::fabs(fixed_center - headlocked_center) > 0.1f,
        "fixed HUD moves in the view when the headset turns");
    eye_geometry::EyeGeometry recentered{};
    require(eye_geometry::with_hud_plane_orientation(
        current, turned_head, recentered),
        "controller-locked HUD recenter");
    std::array<float, 16> recentered_clip{};
    require(eye_geometry::build_cyclopean_hud_plane_clip_positions(
        recentered, 0, fov, fov, 0.5f, 0.5f, recentered_clip),
        "controller-locked HUD recentered clip");
    require(std::fabs(recentered_clip[0] / recentered_clip[3] -
        headlocked_clip[0] / headlocked_clip[3]) < 1.0e-5f,
        "F9 recenter restores the HUD to the current head direction");
}

// hud.ws UpdateScaleformStageSize plus Scaleform's showAll letterbox: the
// HUD texture pixel of a script view ratio (x right, y down, -1..1).
void script_ratio_to_texture_fraction(
    float ratio_x,
    float ratio_y,
    uint32_t width,
    uint32_t height,
    float& fraction_x,
    float& fraction_y) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float aspect = w / h;
    float stage_width = 1920.0f;
    float stage_height = 1080.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    if (std::fabs(aspect - 4.0f / 3.0f) < 0.01f) {
        stage_height = 1440.0f;
        offset_y = -180.0f;
    } else if (std::fabs(aspect - 21.0f / 9.0f) < 0.01f) {
        stage_width = 2520.0f;
        offset_x = -300.0f;
    }
    const float stage_x = (ratio_x + 1.0f) * 0.5f * stage_width + offset_x;
    const float stage_y = (ratio_y + 1.0f) * 0.5f * stage_height + offset_y;
    const float movie_scale = std::min(w / 1920.0f, h / 1080.0f);
    fraction_x = ((w - 1920.0f * movie_scale) * 0.5f +
        stage_x * movie_scale) / w;
    fraction_y = ((h - 1080.0f * movie_scale) * 0.5f +
        stage_y * movie_scale) / h;
}

struct MarkerCase {
    const char* label;
    float hud_size;
    bool visible_origin;
    uint32_t width;
    uint32_t height;
    XrQuaternionf head;
    bool fixed_plane;
};

// World point -> WorldVectorToViewRatio with the mapped marker matrix ->
// script stage -> HUD texel -> build_cyclopean_hud_plane_clip_positions:
// the texel must lie in the point's direction from the cyclopean head.
void run_hud_marker_case(const MarkerCase& test) {
    const XrFovf source_fov{-0.942478f, 0.942478f, 0.978828f, -0.978828f};
    const float half_x = std::tan(source_fov.angleRight);
    const float half_y = std::tan(source_fov.angleUp);
    const XrFovf eye_fov{
        -52.0f * kPi / 180.0f, 40.0f * kPi / 180.0f,
        44.0f * kPi / 180.0f, -55.0f * kPi / 180.0f};
    const float origin_y = test.visible_origin && test.hud_size < 0.999f
        ? (eye_geometry::visible_center_tangent_y(eye_fov) +
            eye_geometry::visible_center_tangent_y(eye_fov)) * 0.5f
        : NAN;
    float stage_x{};
    float stage_y{};
    require(eye_geometry::scaleform_stage_ratio_scale(
        test.width, test.height, stage_x, stage_y), test.label);
    eye_geometry::HudMarkerClipMapping mapping{};
    require(eye_geometry::hud_marker_clip_mapping(
        source_fov, half_x, half_y, test.hud_size, origin_y,
        stage_x, stage_y, mapping), test.label);

    const auto views = make_views(
        test.head, test.head, {0.1f, 1.6f, -0.2f}, test.head,
        {-0.032f, 0.0f, 0.0f}, {0.032f, 0.0f, 0.0f});
    eye_geometry::EyeGeometry current{};
    require(eye_geometry::compute(views, current), test.label);
    eye_geometry::EyeGeometry plane = current;
    if (test.fixed_plane) {
        require(eye_geometry::with_hud_plane_orientation(
            current, {0.0f, 0.0f, 0.0f, 1.0f}, plane), test.label);
    }
    // Infinite plane distance: the texel direction is then the same from
    // either eye, which isolates the angular mapping from stereo parallax.
    std::array<float, 16> corners{};
    require(eye_geometry::build_cyclopean_hud_plane_clip_positions(
        plane, 0, source_fov, eye_fov, test.hud_size, 0.0f, corners,
        origin_y), test.label);
    XrQuaternionf relative{};
    require(eye_geometry::normalize(plane.relative_orientations[0], relative),
        test.label);

    // Marker camera in a z-up world (REDengine): yawed basis, any position.
    const float yaw_radians = 0.7f;
    const float camera_right[3]{
        std::cos(yaw_radians), std::sin(yaw_radians), 0.0f};
    const float camera_forward[3]{
        -std::sin(yaw_radians), std::cos(yaw_radians), 0.0f};
    const float camera_up[3]{0.0f, 0.0f, 1.0f};
    const float camera_position[3]{12.0f, -40.0f, 3.5f};
    std::array<float, 16> matrix{};
    for (size_t axis = 0; axis < 3; ++axis) {
        matrix[axis * 4 + 0] = camera_right[axis] / half_x;
        matrix[axis * 4 + 1] = camera_up[axis] / half_y;
        matrix[axis * 4 + 2] = camera_forward[axis] * 0.5f;
        matrix[axis * 4 + 3] = camera_forward[axis];
    }
    float right_offset{};
    float up_offset{};
    float forward_offset{};
    for (size_t axis = 0; axis < 3; ++axis) {
        right_offset -= camera_position[axis] * camera_right[axis];
        up_offset -= camera_position[axis] * camera_up[axis];
        forward_offset -= camera_position[axis] * camera_forward[axis];
    }
    matrix[12] = right_offset / half_x;
    matrix[13] = up_offset / half_y;
    matrix[14] = forward_offset * 0.5f;
    matrix[15] = forward_offset;
    eye_geometry::apply_hud_marker_clip_mapping(mapping, matrix.data());

    uint32_t seed = 12345u;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    };
    float max_error{};
    for (int sample = 0; sample < 200; ++sample) {
        float target_x{};
        float target_y{};
        require(eye_geometry::hud_marker_ratio_to_plane_tangent(
            mapping, next() * 1.8f - 0.9f, next() * 1.8f - 0.9f,
            target_x, target_y), test.label);
        const float depth = 0.5f + next() * 40.0f;
        float world[4]{0.0f, 0.0f, 0.0f, 1.0f};
        for (size_t axis = 0; axis < 3; ++axis) {
            world[axis] = camera_position[axis] +
                camera_right[axis] * target_x * depth +
                camera_up[axis] * target_y * depth +
                camera_forward[axis] * depth;
        }
        float clip[4]{};
        for (size_t column = 0; column < 4; ++column) {
            for (size_t row = 0; row < 4; ++row) {
                clip[column] += world[row] * matrix[row * 4 + column];
            }
        }
        // The native helper: x = clip_x / w, y = -(clip_y / w).
        const float ratio_x = clip[0] / clip[3];
        const float ratio_y = -(clip[1] / clip[3]);
        float fraction_x{};
        float fraction_y{};
        script_ratio_to_texture_fraction(ratio_x, ratio_y,
            test.width, test.height, fraction_x, fraction_y);
        // Corners TL, TR, BL, BR; clip is linear across the plane.
        float texel[4]{};
        for (size_t component = 0; component < 4; ++component) {
            const float top = corners[component] +
                fraction_x * (corners[4 + component] - corners[component]);
            const float bottom = corners[8 + component] + fraction_x *
                (corners[12 + component] - corners[8 + component]);
            texel[component] = top + fraction_y * (bottom - top);
        }
        const float eye_tangent_x = std::tan(eye_fov.angleLeft) +
            (texel[0] / texel[3] + 1.0f) * 0.5f *
                (std::tan(eye_fov.angleRight) - std::tan(eye_fov.angleLeft));
        const float eye_tangent_y = std::tan(eye_fov.angleDown) +
            (texel[1] / texel[3] + 1.0f) * 0.5f *
                (std::tan(eye_fov.angleUp) - std::tan(eye_fov.angleDown));
        const auto plane_direction = eye_geometry::rotate(
            relative, {eye_tangent_x, eye_tangent_y, -1.0f});
        const float plane_x = plane_direction.x / -plane_direction.z;
        const float plane_y = plane_direction.y / -plane_direction.z;
        max_error = std::max(max_error, std::max(
            std::fabs(plane_x - target_x), std::fabs(plane_y - target_y)));
        float recovered_x{};
        float recovered_y{};
        require(eye_geometry::hud_marker_ratio_to_plane_tangent(
            mapping, ratio_x, ratio_y, recovered_x, recovered_y), test.label);
        max_error = std::max(max_error, std::max(
            std::fabs(recovered_x - target_x),
            std::fabs(recovered_y - target_y)));
    }
    if (max_error > 2.0e-5f) {
        std::fprintf(stderr, "%s: max tangent error %.3g\n",
            test.label, max_error);
    }
    require(max_error <= 2.0e-5f, test.label);
}

void test_hud_marker_clip_mapping() {
    // Quest 3, 2476x2594 HUD, hud_size 0.5 about the visible centre.
    float stage_x{};
    float stage_y{};
    require(eye_geometry::scaleform_stage_ratio_scale(
        2476, 2594, stage_x, stage_y), "marker stage scale");
    require(std::fabs(stage_x - 1.0f) < 1.0e-6f &&
        std::fabs(stage_y - 0.536912f) < 1.0e-4f,
        "marker stage scale is the 16:9 width-fit band");
    const XrFovf source_fov{-0.942478f, 0.942478f, 0.978828f, -0.978828f};
    const float visible_origin = (std::tan(44.0f * kPi / 180.0f) +
        std::tan(-55.0f * kPi / 180.0f)) * 0.5f;
    eye_geometry::HudMarkerClipMapping owner{};
    require(eye_geometry::hud_marker_clip_mapping(
        source_fov, std::tan(0.942478f), std::tan(0.978828f), 0.5f,
        visible_origin, stage_x, stage_y, owner), "marker owner mapping");
    require(std::fabs(owner.scale_x - 2.0f) < 1.0e-4f &&
        std::fabs(owner.offset_x) < 1.0e-6f &&
        std::fabs(owner.scale_y - 3.725f) < 2.0e-3f &&
        std::fabs(owner.offset_y - 0.2896f) < 1.0e-3f,
        "marker owner mapping x 2.000, y 3.725, offset +0.2896");
    // Full-size HUD about the render centre: only the stage band remains.
    eye_geometry::HudMarkerClipMapping full{};
    require(eye_geometry::hud_marker_clip_mapping(
        source_fov, std::tan(0.942478f), std::tan(0.978828f), 1.0f, NAN,
        stage_x, stage_y, full), "marker full-size mapping");
    require(std::fabs(full.scale_x - 1.0f) < 1.0e-6f &&
        std::fabs(full.scale_y - 1.0f / stage_y) < 1.0e-4f &&
        std::fabs(full.offset_y) < 1.0e-6f,
        "marker full-size mapping is 1 / stage band");
    eye_geometry::HudMarkerClipMapping rejected{};
    require(!eye_geometry::hud_marker_clip_mapping(
        source_fov, std::tan(0.942478f), std::tan(0.978828f), 0.0f, NAN,
        stage_x, stage_y, rejected), "marker mapping rejects hud_size 0");
    require(!eye_geometry::scaleform_stage_ratio_scale(0, 2594,
        stage_x, stage_y), "marker stage scale rejects empty texture");

    const auto turned = eye_geometry::from_hmd_euler_degrees(-10.0f, 20.0f, 3.0f);
    run_hud_marker_case({"marker fixed HUD, Quest 3, size 0.5, visible origin",
        0.5f, true, 2476, 2594, turned, true});
    run_hud_marker_case({"marker head-locked HUD, size 1, render origin",
        1.0f, false, 2476, 2594, turned, false});
    run_hud_marker_case({"marker fixed HUD, 4:3 stage, size 0.75",
        0.75f, true, 1600, 1200, {0.0f, 0.0f, 0.0f, 1.0f}, true});
    run_hud_marker_case({"marker fixed HUD, 21:9 stage, size 1.25",
        1.25f, true, 2520, 1080, turned, true});
}

void test_parallel_headset_adaptation() {
    constexpr float baseline = 0.068047f;
    constexpr float width = 2160.0f;
    constexpr float half_horizontal = 0.816143f;
    const XrFovf fov{
        -half_horizontal, half_horizontal, 0.786983f, -0.786983f};
    const auto views = make_views(
        {0, 0, 0, 1}, {0, 0, 0, 1},
        {0, 0, 0}, {0, 0, 0, 1},
        {-baseline * 0.5f, 0, 0},
        {baseline * 0.5f, 0, 0});
    eye_geometry::EyeGeometry geometry{};
    require(eye_geometry::compute(views, geometry),
        "alternate parallel headset geometry");
    const float inverse_distance =
        eye_geometry::inverse_hud_distance_from_parallel_reference(
            -36.0f, 1.0f, 0.065330f, 3072.0f, 2.75276502f);
    const float span = 2.0f * std::tan(half_horizontal);
    for (uint32_t eye = 0; eye < 2; ++eye) {
        std::array<float, 16> clip_positions{};
        require(eye_geometry::build_cyclopean_hud_plane_clip_positions(
            geometry, eye, fov, fov, 1.0f,
            inverse_distance, clip_positions),
            "alternate parallel headset HUD clip build");
        const float projected_left = projected_tangent_x(
            clip_positions, 0, fov);
        const float output_shift_px =
            (projected_left + std::tan(half_horizontal)) * width / span;
        const float expected = eye == 0 ? 34.123f : -34.123f;
        require(std::fabs(output_shift_px - expected) < 0.01f,
            "parallel headset adapts Quest depth to baseline/FOV/resolution");
    }
}

void test_asymmetric_projection_descriptor() {
    constexpr uint32_t width = 3072;
    constexpr uint32_t height = 3216;
    const XrFovf quest_left{
        -0.942478f, 0.698132f, 0.767945f, -0.959931f};
    const XrFovf quest_right{
        -0.698132f, 0.942478f, 0.767945f, -0.959931f};
    eye_geometry::AsymmetricProjectionDescriptor left{};
    eye_geometry::AsymmetricProjectionDescriptor right{};
    require(eye_geometry::derive_asymmetric_projection_descriptor(
        quest_left, width, height, left),
        "Quest left asymmetric descriptor");
    require(eye_geometry::derive_asymmetric_projection_descriptor(
        quest_right, width, height, right),
        "Quest right asymmetric descriptor");
    require(std::fabs(left.optical_center_offset_px_x - 372.50f) < 0.2f,
        "Quest left optical center X");
    require(std::fabs(right.optical_center_offset_px_x + 372.50f) < 0.2f,
        "Quest right optical center X");
    require(std::fabs(left.optical_center_offset_px_y - 310.65f) < 0.2f &&
        std::fabs(right.optical_center_offset_px_y - 310.65f) < 0.2f,
        "Quest optical center Y");
    require(std::fabs(left.redengine_center_offset_px_x - 372.50f) < 0.2f &&
        std::fabs(right.redengine_center_offset_px_x + 372.50f) < 0.2f,
        "Quest REDengine native center X convention");
    require(std::fabs(left.redengine_center_offset_px_y - 310.65f) < 0.2f &&
        std::fabs(right.redengine_center_offset_px_y - 310.65f) < 0.2f,
        "Quest REDengine native center Y convention");
    require(std::fabs(left.center_ndc_x + right.center_ndc_x) < 2.0e-6f,
        "Quest mirrored horizontal NDC centers");
    require(std::fabs(left.center_ndc_y - right.center_ndc_y) < 2.0e-6f,
        "Quest shared vertical NDC center");
    require(std::fabs(left.aspect - right.aspect) < 2.0e-6f,
        "Quest mirrored raw aspect");
    require(std::fabs(left.vertical_fov_degrees - 100.2439f) < 0.002f &&
        std::fabs(right.vertical_fov_degrees - 100.2439f) < 0.002f,
        "Quest lossless vertical FOV");
    require(std::fabs(left.aspect - 0.92549446f) < 2.0e-6f &&
        std::fabs(right.aspect - 0.92549446f) < 2.0e-6f,
        "Quest lossless tangent aspect");

    const XrFovf pimax_dream_air_left{
        -0.912782907f, 0.701172709f, 0.811612010f, -0.811612010f};
    const XrFovf pimax_dream_air_right{
        -0.701172650f, 0.912782907f, 0.811612010f, -0.811612010f};
    eye_geometry::AsymmetricProjectionDescriptor pimax_left{};
    eye_geometry::AsymmetricProjectionDescriptor pimax_right{};
    require(eye_geometry::derive_asymmetric_projection_descriptor(
            pimax_dream_air_left, 3116, 3072, pimax_left) &&
        eye_geometry::derive_asymmetric_projection_descriptor(
            pimax_dream_air_right, 3116, 3072, pimax_right),
        "Pimax Dream Air asymmetric descriptors");
    require(std::fabs(pimax_left.center_ndc_x - 0.210230261f) < 2.0e-6f &&
        std::fabs(pimax_right.center_ndc_x + 0.210230276f) < 2.0e-6f,
        "Pimax Dream Air runtime horizontal centers");
    require(std::fabs(pimax_left.center_ndc_y) < 2.0e-6f &&
        std::fabs(pimax_right.center_ndc_y) < 2.0e-6f,
        "Pimax Dream Air runtime vertical centers");
    require(std::fabs(left.horizontal_tangent_span - 2.21548265f) <
            3.0e-6f &&
        std::fabs(right.horizontal_tangent_span - 2.21548265f) <
            3.0e-6f &&
        std::fabs(left.vertical_tangent_span - 2.39383676f) <
            3.0e-6f &&
        std::fabs(right.vertical_tangent_span - 2.39383676f) <
            3.0e-6f,
        "Quest lossless tangent spans");
    const auto require_fov_roundtrip = [=](
            const XrFovf& source,
            const eye_geometry::AsymmetricProjectionDescriptor& descriptor,
            const char* label) {
        constexpr float pi = 3.14159265358979323846f;
        const float reconstructed_vertical_span = 2.0f * std::tan(
            descriptor.vertical_fov_degrees * pi / 360.0f);
        const float reconstructed_horizontal_span =
            descriptor.aspect * reconstructed_vertical_span;
        const float sum_x =
            -descriptor.center_ndc_x * reconstructed_horizontal_span;
        const float reconstructed_left =
            (sum_x - reconstructed_horizontal_span) * 0.5f;
        const float reconstructed_right =
            (sum_x + reconstructed_horizontal_span) * 0.5f;
        const float sum_y =
            -descriptor.center_ndc_y * reconstructed_vertical_span;
        const float reconstructed_down =
            (sum_y - reconstructed_vertical_span) * 0.5f;
        const float reconstructed_up =
            (sum_y + reconstructed_vertical_span) * 0.5f;
        require(std::fabs(
            descriptor.horizontal_tangent_span -
                (std::tan(source.angleRight) -
                    std::tan(source.angleLeft))) < 2.0e-6f,
            label);
        require(std::fabs(
            descriptor.vertical_tangent_span -
                (std::tan(source.angleUp) -
                    std::tan(source.angleDown))) < 2.0e-6f,
            label);
        require(std::fabs(reconstructed_left -
                std::tan(source.angleLeft)) < 2.0e-6f &&
            std::fabs(reconstructed_right -
                std::tan(source.angleRight)) < 2.0e-6f &&
            std::fabs(reconstructed_up -
                std::tan(source.angleUp)) < 2.0e-6f &&
            std::fabs(reconstructed_down -
                std::tan(source.angleDown)) < 2.0e-6f,
            label);
    };
    require_fov_roundtrip(quest_left, left,
        "Quest left tangent roundtrip");
    require_fov_roundtrip(quest_right, right,
        "Quest right tangent roundtrip");

    const XrFovf symmetric{-0.8f, 0.8f, 0.9f, -0.9f};
    eye_geometry::AsymmetricProjectionDescriptor centered{};
    require(eye_geometry::derive_asymmetric_projection_descriptor(
        symmetric, 2160, 2104, centered),
        "symmetric descriptor");
    require(std::fabs(centered.optical_center_offset_px_x) < 1.0e-5f &&
        std::fabs(centered.optical_center_offset_px_y) < 1.0e-5f &&
        std::fabs(centered.redengine_center_offset_px_x) < 1.0e-5f &&
        std::fabs(centered.redengine_center_offset_px_y) < 1.0e-5f,
        "symmetric descriptor has zero optical offset");

    XrFovf invalid = symmetric;
    invalid.angleRight = invalid.angleLeft;
    require(!eye_geometry::derive_asymmetric_projection_descriptor(
        invalid, width, height, centered),
        "asymmetric descriptor rejects collapsed FOV");
    require(!eye_geometry::derive_asymmetric_projection_descriptor(
        symmetric, 0, height, centered),
        "asymmetric descriptor rejects zero extent");
}

void test_asymmetric_presentation_scale() {
    constexpr uint32_t width = 3072;
    constexpr uint32_t height = 3216;
    const XrFovf source{
        -0.942478f, 0.698132f, 0.767945f, -0.959931f};

    XrFovf identity{};
    require(eye_geometry::scale_asymmetric_projection_fov(
        source, 1.0f, identity),
        "asymmetric presentation identity scale");
    require(identity.angleLeft == source.angleLeft &&
        identity.angleRight == source.angleRight &&
        identity.angleUp == source.angleUp &&
        identity.angleDown == source.angleDown,
        "asymmetric presentation scale 1 preserves exact FOV");

    constexpr float scale = 0.75f;
    XrFovf scaled{};
    require(eye_geometry::scale_asymmetric_projection_fov(
        source, scale, scaled),
        "asymmetric presentation reduced scale");
    eye_geometry::AsymmetricProjectionDescriptor original_descriptor{};
    eye_geometry::AsymmetricProjectionDescriptor scaled_descriptor{};
    require(eye_geometry::derive_asymmetric_projection_descriptor(
        source, width, height, original_descriptor),
        "asymmetric presentation source descriptor");
    require(eye_geometry::derive_asymmetric_projection_descriptor(
        scaled, width, height, scaled_descriptor),
        "asymmetric presentation scaled descriptor");
    require(std::fabs(scaled_descriptor.horizontal_tangent_span -
        original_descriptor.horizontal_tangent_span * scale) < 2.0e-6f &&
        std::fabs(scaled_descriptor.vertical_tangent_span -
        original_descriptor.vertical_tangent_span * scale) < 2.0e-6f,
        "asymmetric presentation scales both tangent spans");
    require(std::fabs(scaled_descriptor.center_ndc_x -
        original_descriptor.center_ndc_x) < 2.0e-6f &&
        std::fabs(scaled_descriptor.center_ndc_y -
        original_descriptor.center_ndc_y) < 2.0e-6f,
        "asymmetric presentation preserves optical center");
    require(std::fabs(scaled_descriptor.redengine_center_offset_px_x -
        original_descriptor.redengine_center_offset_px_x) < 0.001f &&
        std::fabs(scaled_descriptor.redengine_center_offset_px_y -
        original_descriptor.redengine_center_offset_px_y) < 0.001f,
        "asymmetric presentation preserves REDengine center offsets");
    require(!eye_geometry::scale_asymmetric_projection_fov(
        source, 0.0f, scaled),
        "asymmetric presentation rejects zero scale");
    require(!eye_geometry::scale_asymmetric_projection_fov(
        source, std::numeric_limits<float>::quiet_NaN(), scaled),
        "asymmetric presentation rejects non-finite scale");
}

void test_strict_stereo_final_presentation_scale() {
    constexpr uint32_t width = 3072;
    constexpr uint32_t height = 3216;
    const XrRect2Di full_rect{
        {0, 0}, {static_cast<int32_t>(width),
                 static_cast<int32_t>(height)}};
    const XrFovf runtime_fovs[2]{
        {-0.942478f, 0.698132f, 0.767945f, -0.959931f},
        {-0.698132f, 0.942478f, 0.767945f, -0.959931f}};

    // Reproduce the centered producer and derive the established per-eye
    // subimages once. Presentation Size must not participate in either step.
    float cover = 1.0f;
    for (const auto& fov : runtime_fovs) {
        const float left = std::tan(fov.angleLeft);
        const float right = std::tan(fov.angleRight);
        const float up = std::tan(fov.angleUp);
        const float down = std::tan(fov.angleDown);
        const float center_x = -left / (right - left);
        const float center_y = up / (up - down);
        cover = std::min(cover,
            0.5f / std::max(center_x, 1.0f - center_x));
        cover = std::min(cover,
            0.5f / std::max(center_y, 1.0f - center_y));
    }
    require(std::fabs(cover - 0.804821f) < 2.0e-5f,
        "Quest strict-Stereo cover reference");

    const float runtime_horizontal_span =
        std::tan(runtime_fovs[0].angleRight) -
        std::tan(runtime_fovs[0].angleLeft);
    const float runtime_vertical_span =
        std::tan(runtime_fovs[0].angleUp) -
        std::tan(runtime_fovs[0].angleDown);
    const float content_half_x = runtime_horizontal_span / (2.0f * cover);
    const float content_half_y = runtime_vertical_span / (2.0f * cover);
    const XrFovf base_content{
        std::atan(-content_half_x), std::atan(content_half_x),
        std::atan(content_half_y), std::atan(-content_half_y)};

    eye_geometry::SymmetricEyeSubimage crops[2]{};
    for (uint32_t eye = 0; eye < 2; ++eye) {
        require(eye_geometry::derive_symmetric_eye_subimage(
            base_content, runtime_fovs[eye], full_rect, crops[eye]),
            "strict-Stereo symmetric tangent crop");
        const float content_step_x =
            (std::tan(base_content.angleRight) -
                std::tan(base_content.angleLeft)) / width;
        const float content_step_y =
            (std::tan(base_content.angleUp) -
                std::tan(base_content.angleDown)) / height;
        require(std::fabs(
            std::tan(crops[eye].represented_fov.angleLeft) -
                std::tan(runtime_fovs[eye].angleLeft)) <=
                content_step_x * 1.01f &&
            std::fabs(
            std::tan(crops[eye].represented_fov.angleRight) -
                std::tan(runtime_fovs[eye].angleRight)) <=
                content_step_x * 1.01f &&
            std::fabs(
            std::tan(crops[eye].represented_fov.angleUp) -
                std::tan(runtime_fovs[eye].angleUp)) <=
                content_step_y * 1.01f &&
            std::fabs(
            std::tan(crops[eye].represented_fov.angleDown) -
                std::tan(runtime_fovs[eye].angleDown)) <=
                content_step_y * 1.01f,
            "strict-Stereo crop target differs by under one source pixel");

        const auto base_rect = crops[eye].image_rect;
        XrFovf requested_fov{};
        constexpr float presentation_scale = 0.8f;
        require(eye_geometry::scale_asymmetric_projection_fov(
            crops[eye].represented_fov, presentation_scale, requested_fov),
            "strict-Stereo requested final Presentation Size FOV");
        eye_geometry::SymmetricEyeSubimage presentation{};
        require(eye_geometry::derive_symmetric_eye_subimage(
            crops[eye].represented_fov, requested_fov,
            base_rect, presentation),
            "strict-Stereo final Presentation Size subimage/FOV pair");
        eye_geometry::AsymmetricProjectionDescriptor base_descriptor{};
        eye_geometry::AsymmetricProjectionDescriptor final_descriptor{};
        require(eye_geometry::derive_asymmetric_projection_descriptor(
            crops[eye].represented_fov, width, height, base_descriptor) &&
            eye_geometry::derive_asymmetric_projection_descriptor(
                presentation.represented_fov,
                static_cast<uint32_t>(presentation.image_rect.extent.width),
                static_cast<uint32_t>(presentation.image_rect.extent.height),
                final_descriptor),
            "strict-Stereo final Presentation Size descriptors");
        const float base_step_x =
            base_descriptor.horizontal_tangent_span /
            static_cast<float>(base_rect.extent.width);
        const float base_step_y =
            base_descriptor.vertical_tangent_span /
            static_cast<float>(base_rect.extent.height);
        require(std::fabs(final_descriptor.horizontal_tangent_span -
            base_descriptor.horizontal_tangent_span * presentation_scale) <=
                base_step_x * 2.01f &&
            std::fabs(final_descriptor.vertical_tangent_span -
            base_descriptor.vertical_tangent_span * presentation_scale) <=
                base_step_y * 2.01f,
            "strict-Stereo final subimage represents requested tangent span");
        require(presentation.image_rect.offset.x >= base_rect.offset.x &&
            presentation.image_rect.offset.y >= base_rect.offset.y &&
            presentation.image_rect.offset.x +
                presentation.image_rect.extent.width <=
                base_rect.offset.x + base_rect.extent.width &&
            presentation.image_rect.offset.y +
                presentation.image_rect.extent.height <=
                base_rect.offset.y + base_rect.extent.height &&
            presentation.image_rect.extent.width < base_rect.extent.width &&
            presentation.image_rect.extent.height < base_rect.extent.height,
            "strict-Stereo final Presentation Size selects a contained crop");
        require(std::fabs(
            static_cast<float>(presentation.image_rect.extent.width) /
                base_rect.extent.width - presentation_scale) <=
                2.01f / base_rect.extent.width &&
            std::fabs(
            static_cast<float>(presentation.image_rect.extent.height) /
                base_rect.extent.height - presentation_scale) <=
                2.01f / base_rect.extent.height,
            "strict-Stereo final crop follows Presentation Size per axis");
        require(std::fabs(
            static_cast<float>(base_rect.extent.width) / width - cover) <=
                2.0f / width &&
            std::fabs(
            static_cast<float>(base_rect.extent.height) / height - cover) <=
                2.0f / height,
            "strict-Stereo crop uses Quest cover fraction per axis");
    }
    const auto& left = crops[0].image_rect;
    const auto& right = crops[1].image_rect;
    require(left.offset.x == static_cast<int32_t>(width) -
            (right.offset.x + right.extent.width) &&
        left.offset.y == right.offset.y &&
        left.extent.width == right.extent.width &&
        left.extent.height == right.extent.height,
        "strict-Stereo eye crops are mirrored horizontally");

    XrFovf outside = runtime_fovs[0];
    outside.angleLeft = std::atan(-content_half_x * 1.1f);
    eye_geometry::SymmetricEyeSubimage rejected{};
    require(!eye_geometry::derive_symmetric_eye_subimage(
        base_content, outside, full_rect, rejected),
        "strict-Stereo crop rejects target outside content");
    XrRect2Di outside_rect{{-1, 0}, {static_cast<int32_t>(width), 10}};
    XrFovf represented{};
    require(!eye_geometry::derive_pixel_exact_subimage_fov(
        base_content, full_rect, outside_rect, represented),
        "strict-Stereo exact FOV rejects rectangle outside content");
}

void test_asymmetric_hud_source_shift() {
    constexpr uint32_t width = 3072;
    constexpr uint32_t height = 3216;
    const XrFovf quest_left{
        -0.942478f, 0.698132f, 0.767945f, -0.959931f};
    eye_geometry::AsymmetricProjectionDescriptor descriptor{};
    require(eye_geometry::derive_asymmetric_projection_descriptor(
        quest_left, width, height, descriptor),
        "asymmetric HUD Quest descriptor");

    constexpr float base_hud_size = 1.0f;
    constexpr float asymmetric_hud_size = 1.2425f;
    int source_x{};
    int source_y{};
    require(eye_geometry::derive_asymmetric_hud_source_shift(
        descriptor, base_hud_size, asymmetric_hud_size, -36,
        source_x, source_y),
        "asymmetric HUD source shift");
    const float displayed_x = -static_cast<float>(source_x) *
        asymmetric_hud_size;
    const float displayed_y = -static_cast<float>(source_y) *
        asymmetric_hud_size;
    require(std::fabs(displayed_x -
        (descriptor.optical_center_offset_px_x + 36.0f)) <=
            asymmetric_hud_size,
        "asymmetric HUD keeps horizontal optical centre and convergence");
    require(std::fabs(displayed_y +
        descriptor.optical_center_offset_px_y) <= asymmetric_hud_size,
        "asymmetric HUD moves upward to the OpenXR optical centre");
    require(source_y > 0,
        "asymmetric HUD positive OpenXR Y uses positive source shift");

    require(!eye_geometry::derive_asymmetric_hud_source_shift(
        descriptor, base_hud_size, 0.0f, -36, source_x, source_y),
        "asymmetric HUD rejects zero scale");
}

} // namespace

int main() {
    test_parallel();
    test_symmetric_cant_and_antipodal();
    test_asymmetric_cant();
    test_normalization_and_invalid_input();
    test_euler_roundtrip_and_wrap();
    test_redengine_descriptor_composition();
    test_quest_reference_hud_plane();
    test_canted_hud_plane_and_invalid_fallback();
    test_controller_locked_hud_plane();
    test_parallel_headset_adaptation();
    test_asymmetric_projection_descriptor();
    test_asymmetric_presentation_scale();
    test_strict_stereo_final_presentation_scale();
    test_asymmetric_hud_source_shift();
    test_hud_marker_clip_mapping();
    if (failures != 0) {
        std::fprintf(stderr, "%d eye-geometry test(s) failed\n", failures);
        return 1;
    }
    std::puts("OpenXR eye-geometry tests passed");
    return 0;
}
