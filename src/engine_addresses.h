#pragma once

#include <cstddef>
#include <cstdint>

#include "game_build.h"

// Per-build addresses of engine-internal functions. The Next-Gen 4.04 values are
// the ones this project was written against. Other builds get an address only
// once it has been located and checked; an address of 0 means "unknown on this
// build", and the hook that needs it stays disabled. Entries for builds other
// than 4.04 also carry the expected leading bytes, verified at runtime before
// the hook is placed, so a wrong address fails closed instead of patching
// unrelated code.
namespace w3vr::engine_addresses {

enum class Function : size_t {
    IsAnyMenu,                    // native theGame.IsAnyMenu (GUI manager)
    IsLoadingScreenVideoPlaying,  // native theGame.IsLoadingScreenVideoPlaying
    ActorIsInGameplayScene,       // native CActor.IsInGameplayScene (+0x2F3)
    ActorIsInNonGameplayCutscene, // native CActor.IsInNonGameplayCutscene (+0x2F2)
    GameIsPlayingNonGameplayScene,  // native theGame.IsCurrentlyPlayingNonGameplayScene
    FrameDataFactory,  // builds a frame (render_context, render_settings, scene_descriptor)
    SceneEnqueue,      // queues a frame as CRenderCommand_RenderScene (render_context, frame)
    TopmostCameraObject,  // native CCameraDirector.GetTopmostCameraObject
    ViewRebuild,       // rebuilds a view's matrices from its inputs (float* view)
    GameplayEntry,     // render-thread gameplay frame task; calls the frame builder
    RenderSceneSetup,  // per-tick scene setup; calls the frame factory
    FrameBuilder,      // render-thread frame build (render_context, frame, extra)
    CameraEnableManualControl,  // native CCustomCamera.EnableManualControl
    CameraDirection,   // native CCameraDirector.GetCameraDirection (first of two copies)
    WorldVectorToViewRatio,  // native CCameraDirector.WorldVectorToViewRatio
    ViewportResolution,  // (fullscreen, resolution setting, int* width, int* height)
    CameraLightPlacement,  // places one camera light from the render camera (pos + yaw)
    DlssConstantsBuilder,  // fills Streamline constants + tags (state, frame_data, reset)
    DlssInputPrepare,      // tags the DLSS inputs (state, frame_data, params)
    DlssSrApply,           // sets DLSS options and evaluates SR (state, frame_data, params, pass, ...)
    TemporalCameraBuild,   // builds the 0xB0 temporal camera record (motion-vector camera)
    FrustumCornerHelper,   // view frustum corners for shadow cascades (view, ndc_z, out, rebuild)
    GuiFillRect,           // 2D colour quad into the GUI target (_, x, y, w, h, const uint8_t rgba[4])
    RenderFenceWait,       // CRenderFence wait, vtable +0x18 (fence): blocks until it is signalled
    ParallelFlushCommands, // ParallelRenderCommands::FlushCommands (commands): staged batches -> ready, apply
    Count,
};

struct Signature {
    const uint8_t* bytes;
    size_t size;
};

struct Entry {
    const char* name;
    uintptr_t next_gen_404;
    uintptr_t remaster_501;
    Signature remaster_501_signature;
};

// Whole function: forwards to a GUI-manager virtual (+0x1E0) and stores the
// bool result. Identical in 4.04 and 5.01.
inline constexpr uint8_t kIsAnyMenuBytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xFF, 0x42, 0x30, 0x49, 0x8B,
    0xD8, 0x48, 0x8B, 0x01, 0xFF, 0x90, 0xE0, 0x01, 0x00, 0x00, 0x48, 0x85,
    0xDB, 0x74, 0x02, 0x88, 0x03, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};

// Whole function: game +0x198 loading screen, its +0x38 player, player vtable
// +0x18 state, active unless the state is 2. Identical in 4.04 and 5.01.
inline constexpr uint8_t kIsLoadingScreenVideoPlayingBytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xFF,
    0x42, 0x30, 0x49, 0x8B, 0xF8, 0x48, 0x8B, 0x99, 0x98, 0x01, 0x00, 0x00,
    0x48, 0x85, 0xDB, 0x74, 0x30, 0x48, 0x8B, 0x4B, 0x38, 0x48, 0x85, 0xC9,
    0x74, 0x1D, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18, 0x83, 0xF8, 0x02, 0x75};

// Script-native bool getters: bump the stack code pointer, store one byte
// flag of the object if a result is requested, return.
inline constexpr uint8_t kActorIsInGameplaySceneBytes[] = {
    0x48, 0xFF, 0x42, 0x30, 0x4D, 0x85, 0xC0, 0x74, 0x0A, 0x0F, 0xB6, 0x81,
    0xF3, 0x02, 0x00, 0x00, 0x41, 0x88, 0x00, 0xC3};
inline constexpr uint8_t kActorIsInNonGameplayCutsceneBytes[] = {
    0x48, 0xFF, 0x42, 0x30, 0x4D, 0x85, 0xC0, 0x74, 0x0A, 0x0F, 0xB6, 0x81,
    0xF2, 0x02, 0x00, 0x00, 0x41, 0x88, 0x00, 0xC3};
inline constexpr uint8_t kGameIsPlayingNonGameplaySceneBytes[] = {
    0x48, 0xFF, 0x42, 0x30, 0x4D, 0x85, 0xC0, 0x74, 0x0A, 0x0F, 0xB6, 0x81,
    0x1F, 0x01, 0x00, 0x00, 0x41, 0x88, 0x00, 0xC3};

// Large functions differ between the builds (5.01 is an AVX build); their 5.01
// entries carry the 5.01 prologue, which is unique in the 5.01 exe. The match
// itself comes from the call graph: render-scene setup calls the factory, the
// factory calls the view rebuild, and the enqueue is the only other function
// that builds a CRenderCommand_RenderScene with the same logic.
inline constexpr uint8_t kFrameDataFactory501Bytes[] = {
    0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x57, 0x48, 0x8D, 0xAC, 0x24,
    0xA0, 0x6C, 0xFF, 0xFF};
inline constexpr uint8_t kSceneEnqueue501Bytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0x05, 0x4F, 0xCF, 0x79};

inline constexpr uint8_t kTopmostCameraObject501Bytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xFF,
    0x42, 0x30, 0x45, 0x33};

inline constexpr uint8_t kViewRebuild501Bytes[] = {
    0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0xA8, 0x08, 0xFB,
    0xFF, 0xFF, 0x48, 0x81};

inline constexpr uint8_t kGameplayEntry501Bytes[] = {
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x15, 0x7B, 0xBA, 0xB4,
    0x03, 0x48, 0x8B, 0xF9};
inline constexpr uint8_t kRenderSceneSetup501Bytes[] = {
    0x40, 0x55, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x98, 0x08, 0xFF, 0xFF, 0xB8,
    0x68, 0xF8, 0x00, 0x00};

// Called from the gameplay entry (return 0x01D0C5E1 on 5.01); passes
// frame+0x10 and the input extent at +0xBEC to the temporal writer.
inline constexpr uint8_t kFrameBuilder501Bytes[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x55, 0x53, 0x56, 0x41, 0x54,
    0x41, 0x55, 0x41, 0x56};

// Registered script native; 24 bytes because the first 16 occur 109 times.
inline constexpr uint8_t kCameraEnableManualControl501Bytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0x42, 0x30, 0x4C, 0x8D, 0x0D, 0x5B, 0x82, 0xC6, 0x03, 0x48, 0x8B, 0xD9};

// Leaf function: inc [rdx+0x30]; vmovss xmm0/1/2, [rcx+0x790/0x794/0x798].
// The signature is exactly the 20-byte prefix the 5.01 prefix hook relocates
// (whole, position-independent instructions; the only branch targets ret).
inline constexpr uint8_t kCameraDirection501Bytes[] = {
    0x48, 0xFF, 0x42, 0x30, 0xC5, 0xFA, 0x10, 0x81, 0x90, 0x07, 0x00, 0x00,
    0xC5, 0xFA, 0x10, 0x89, 0x94, 0x07, 0x00, 0x00};

// Registered script native: decodes the world vector and the two out floats
// through the script decoder table (the lea r14 in these bytes; null entries
// are skipped), then calls the projection helper 0x02385EB0, which reads the
// world-to-clip at director+0x2E0 like 4.04. 32 bytes because the first 24
// occur twice in the exe.
inline constexpr uint8_t kWorldVectorToViewRatio501Bytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57,
    0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x42, 0x30, 0x4C, 0x8D,
    0x35, 0x03, 0x2E, 0x6F, 0x03, 0xC5, 0xF9, 0xEF};

// Same signature and logic as 4.04 (desktop size from DXGI, then the "WxH"
// setting); 5.01 moved the DXGI part into a helper (0x0228E230).
inline constexpr uint8_t kViewportResolution501Bytes[] = {
    0x48, 0x8B, 0xC4, 0x88, 0x48, 0x08, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x00,
    0x00, 0x00, 0x48, 0x89};

// (light out, render camera, light params, setup, colour, ...): rotates the
// light offset by the camera's yaw (camera[6]) only and adds camera[0..2].
// Only caller: the frame light list (the one reading DisableAllCameraLights).
inline constexpr uint8_t kCameraLightPlacement501Bytes[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x70, 0x10, 0x4C, 0x89, 0x60, 0x18, 0x55,
    0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xA8, 0xF8, 0xFE, 0xFF, 0xFF, 0x48};

// DLSS SR on 5.01 (Streamline 2). Leading bytes, unique in the exe at these lengths.
inline constexpr uint8_t kDlssConstantsBuilder501Bytes[] = {
    0x44, 0x88, 0x44, 0x24, 0x18, 0x55, 0x53, 0x41, 0x56, 0x48, 0x8D, 0xAC,
    0x24, 0xA0, 0xC2, 0xFF, 0xFF, 0xB8, 0x60, 0x3E, 0x00, 0x00, 0xE8, 0x55};
inline constexpr uint8_t kDlssInputPrepare501Bytes[] = {
    0x40, 0x55, 0x56, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xB9, 0x48, 0x81,
    0xEC, 0xF0, 0x00, 0x00, 0x00, 0x48, 0x83, 0x3A, 0x00, 0x48, 0x8B, 0xF2};
inline constexpr uint8_t kDlssSrApply501Bytes[] = {
    0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D,
    0x6C, 0x24, 0xA0, 0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00, 0x80, 0x79};
// Same code as 4.04 0x015FE010 apart from the AVX encoding; its 0x4E-byte
// wrapper (5.01 0x02291BB0, 4.04 0x015FFA40) matches instruction for instruction.
inline constexpr uint8_t kTemporalCameraBuild501Bytes[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48,
    0x89, 0x78, 0x18, 0x55, 0x48, 0x8D, 0xA8, 0x18, 0xFF, 0xFF, 0xFF, 0x48,
    0x81, 0xEC, 0xE0, 0x01, 0x00, 0x00, 0xC5, 0xFC};

// Prologue; called by the shadow cascade builder (0x01CD7500, rebuild=1 with
// its private view copy) and 0x01D706B2 (rebuild=0). With rebuild set it
// rebuilds the projection from FOV/aspect and applies only the centre block,
// not the 5.01 NDC offset at view+0x520.
inline constexpr uint8_t kFrustumCornerHelper501Bytes[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x78, 0x18, 0xC5,
    0xFA, 0x11, 0x48, 0x10, 0x55, 0x48, 0x8D, 0xA8, 0xE8, 0xFD, 0xFF, 0xFF,
    0x48, 0x81, 0xEC, 0x10, 0x03, 0x00, 0x00};

// Fill-rect helper of the GUI pass (5.01 only): two vertex-coloured triangles
// with the render state at 0x05C78BF0. Its only callers draw the screen fade
// (GuiFadeFillReturn, FadeObjectFillReturn); the fill lands in the same target
// as the Flash HUD.
inline constexpr uint8_t kGuiFillRect501Bytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89,
    0x7C, 0x24, 0x18, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xB9, 0x48, 0x81, 0xEC,
    0xD0, 0x00, 0x00, 0x00, 0x8B, 0xDA, 0x41, 0x8B, 0xF1};
// Code at the two fade sites, checked before the fill-rect detour is placed:
// the GUI pass's "fade amount > 0" test (vcomiss xmm0,[rdi+0x48]; jae) and the
// call instructions that end at the two return addresses.
inline constexpr uint8_t kGuiFadeAmountTest501Bytes[] = {
    0xC5, 0xF8, 0x2F, 0x47, 0x48, 0x0F, 0x83};
inline constexpr uint8_t kGuiFadeFillCall501Bytes[] = {
    0xE8, 0x5D, 0x28, 0x07, 0x00};
inline constexpr uint8_t kFadeObjectFillCall501Bytes[] = {
    0xE8, 0x33, 0xDE, 0x08, 0x00};

// [FIX:PAIR-ENQUEUE-AFTER-FENCE 1/11] REDengine's per-tick throttle and the
// batched skinning/relink queue (5.01 only).
// CRenderFence wait, slot +0x18 of the CRenderFence vtable (0x037AE648):
// WaitForSingleObject on fence+0x10 in 100 ms steps, pumping messages on the
// main thread between them. Its first 18 bytes are unique in the exe.
inline constexpr uint8_t kRenderFenceWait501Bytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0xD9, 0xBA, 0x64, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0x49, 0x10};
// ParallelRenderCommands::FlushCommands (named by the RTTI of its
// FunctionTask lambda); only caller the frame builder (return 0x01C1A6F4).
// 29 bytes because the first 24 occur ten times.
inline constexpr uint8_t kParallelFlushCommands501Bytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89,
    0x7C, 0x24, 0x18, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0xB8, 0xA0, 0xF6, 0x01, 0x00};
// RenderSceneSetup call sites, each ending at its return address: the frame
// factory call (call rel32 to FrameDataFactory) and the per-tick fence wait
// (mov rcx,[rdi+0x1D0]; vmovaps xmm6,xmm0; mov rax,[rcx]; call [rax+0x18]).
inline constexpr uint8_t kRenderSceneFactoryCall501Bytes[] = {
    0xE8, 0x9E, 0x2E, 0x04, 0x00};
inline constexpr uint8_t kRenderSceneFenceWaitCall501Bytes[] = {
    0x48, 0x8B, 0x8F, 0xD0, 0x01, 0x00, 0x00, 0xC5, 0xF8, 0x28, 0xF0, 0x48,
    0x8B, 0x01, 0xFF, 0x50, 0x18};

inline constexpr Entry kEntries[] = {
    {"IsAnyMenu", 0x01F3C5C0, 0x01DF41A0, {kIsAnyMenuBytes, sizeof(kIsAnyMenuBytes)}},
    {"IsLoadingScreenVideoPlaying", 0x0154EED0, 0x0227A0F0,
        {kIsLoadingScreenVideoPlayingBytes, sizeof(kIsLoadingScreenVideoPlayingBytes)}},
    {"Actor.IsInGameplayScene", 0x01C4CD70, 0x0210B410,
        {kActorIsInGameplaySceneBytes, sizeof(kActorIsInGameplaySceneBytes)}},
    {"Actor.IsInNonGameplayCutscene", 0x01C4CD90, 0x0210B430,
        {kActorIsInNonGameplayCutsceneBytes, sizeof(kActorIsInNonGameplayCutsceneBytes)}},
    {"Game.IsPlayingNonGameplayScene", 0x01549800, 0x022753C0,
        {kGameIsPlayingNonGameplaySceneBytes, sizeof(kGameIsPlayingNonGameplaySceneBytes)}},
    {"FrameDataFactory", 0x016214C0, 0x022C0820,
        {kFrameDataFactory501Bytes, sizeof(kFrameDataFactory501Bytes)}},
    {"SceneEnqueue", 0x01621FC0, 0x022BE990,
        {kSceneEnqueue501Bytes, sizeof(kSceneEnqueue501Bytes)}},
    {"CameraDirector.GetTopmostCameraObject", 0x0161B9C0, 0x023825D0,
        {kTopmostCameraObject501Bytes, sizeof(kTopmostCameraObject501Bytes)}},
    {"ViewRebuild", 0x015FE550, 0x02293240,
        {kViewRebuild501Bytes, sizeof(kViewRebuild501Bytes)}},
    {"GameplayEntry", 0x01D00260, 0x01D0C570,
        {kGameplayEntry501Bytes, sizeof(kGameplayEntry501Bytes)}},
    {"RenderSceneSetup", 0x01553070, 0x0227D1F0,
        {kRenderSceneSetup501Bytes, sizeof(kRenderSceneSetup501Bytes)}},
    {"FrameBuilder", 0x01D86400, 0x01C1A210,
        {kFrameBuilder501Bytes, sizeof(kFrameBuilder501Bytes)}},
    {"CustomCamera.EnableManualControl", 0x01F580E0, 0x01E0D800,
        {kCameraEnableManualControl501Bytes, sizeof(kCameraEnableManualControl501Bytes)}},
    {"CameraDirector.GetCameraDirection", 0x0161BBC0, 0x023826F0,
        {kCameraDirection501Bytes, sizeof(kCameraDirection501Bytes)}},
    {"CameraDirector.WorldVectorToViewRatio", 0x0161BDA0, 0x02382C50,
        {kWorldVectorToViewRatio501Bytes, sizeof(kWorldVectorToViewRatio501Bytes)}},
    {"ViewportResolution", 0x01563DC0, 0x0228F3B0,
        {kViewportResolution501Bytes, sizeof(kViewportResolution501Bytes)}},
    {"CameraLightPlacement", 0x00718310, 0x0035B500,
        {kCameraLightPlacement501Bytes, sizeof(kCameraLightPlacement501Bytes)}},
    {"DlssConstantsBuilder", 0x01CFE280, 0x01B80230,
        {kDlssConstantsBuilder501Bytes, sizeof(kDlssConstantsBuilder501Bytes)}},
    {"DlssInputPrepare", 0x01CFD760, 0x01B7F550,
        {kDlssInputPrepare501Bytes, sizeof(kDlssInputPrepare501Bytes)}},
    {"DlssSrApply", 0x01CFCF20, 0x01B7E720,
        {kDlssSrApply501Bytes, sizeof(kDlssSrApply501Bytes)}},
    {"TemporalCameraBuild", 0x015FE010, 0x02291600,
        {kTemporalCameraBuild501Bytes, sizeof(kTemporalCameraBuild501Bytes)}},
    {"FrustumCornerHelper", 0x015FC370, 0x02291CA0,
        {kFrustumCornerHelper501Bytes, sizeof(kFrustumCornerHelper501Bytes)}},
    {"GuiFillRect", 0, 0x01C99780,
        {kGuiFillRect501Bytes, sizeof(kGuiFillRect501Bytes)}},
    {"RenderFenceWait", 0, 0x01CABDE0,
        {kRenderFenceWait501Bytes, sizeof(kRenderFenceWait501Bytes)}},
    {"ParallelRenderCommands.FlushCommands", 0, 0x01C1CA10,
        {kParallelFlushCommands501Bytes, sizeof(kParallelFlushCommands501Bytes)}},
};
static_assert(sizeof(kEntries) / sizeof(kEntries[0]) ==
    static_cast<size_t>(Function::Count));

constexpr const Entry& entry(Function function) {
    return kEntries[static_cast<size_t>(function)];
}

// Build-dependent data addresses and object layout values (0 = unknown).
enum class Value : size_t {
    RenderSchedulerGlobal,      // RVA of the render scheduler pointer (flags at +0x3C)
    RenderContextSharedOffset,  // render context field passed on by the scene enqueue
    ViewCentreIndex,            // view-array float index of centre offset x, y and viewport w, h
    CustomCameraVtable,         // RVA of the CCustomCamera vtable at subobject +0x170
    ViewFactoryReturn,          // return address of the view rebuild call in the factory
    ViewCopyRebuildReturn,      // return address of the view rebuild call in the copy wrapper
    SceneDescriptorBytes,       // bytes of the stack scene descriptor copied for the second eye
    SceneDescriptorMinimumBytes,  // copy fails below this many readable bytes
    GameplayFrameBuilderReturn,   // return address of the frame-builder call in the gameplay entry
    // Frame data built by the factory: two views (core at +0x20 and the
    // secondary offset, stride 0x510 on 4.04 and 0x5E0 on 5.01), each with a
    // centre offset x, y and extent w, h block, then the input extent.
    FrameSecondaryViewOffset,     // secondary view core (primary core is +0x20)
    FramePrimaryCentreOffset,     // primary centre x, y; extent w, h follows at +8
    FrameSecondaryCentreOffset,   // secondary centre x, y; extent w, h follows at +8
    FrameInputExtentOffset,       // render input width, height
    FrameCameraTransformOffset,   // camera transform (row 3 = position); diagnostics only
    CustomCameraManualControlOffset,  // CCustomCamera manual-control flag byte
    // Script VM and the WorldVectorToViewRatio projection helper, for the
    // local marker projection that mirrors the native.
    ScriptArgumentDecoderTable,   // RVA of the per-opcode argument decoder table
    ScriptOutParameter,           // RVA of the global out-parameter pointer
    ProjectionNearThreshold,      // RVA of the helper's minimum clip w (0.001f)
    ProjectionNumerator,          // RVA of the helper's 1.0f numerator of 1/w
    ProjectionYSignMask,          // RVA of the sign mask that negates the y ratio
    TemporalCameraRecordOffset,   // bytes from a view to its previous-frame camera record
    DlssStateViewport,            // DLSS state: Streamline viewport id (u32)
    DlssStateConstantsFrame,      // DLSS state: frame index last given constants
    DlssStateApplyFrame,          // DLSS state: render counter of the last SR apply
    DlssStatePrepareFrame,        // DLSS state: render counter of the last input prepare
    DlssStateResetFlag,           // DLSS state: this frame's constants request a history reset
    DlssFrameRayReconstructionFlag,  // frame_data: Ray Reconstruction path active
    DlssFrameDataFromFrame,       // frame_data = frame + this
    ViewProjectionOffsetIndex,    // view-array float index of the NDC projection offset x, y (5.01 only)
    ReprojectionMatrixIndex,      // view-array float index of the current-to-previous clip reprojection
    GuiFadeAmountTest,            // RVA of the GUI pass's fade-amount test (5.01 only)
    GuiFadeFillReturn,            // return address of the fill-rect call drawing the GUI-pass fade
    FadeObjectFillReturn,         // return address of the fill-rect call in the standalone fade draw
    RenderSceneFactoryReturn,     // return address of the frame-factory call in RenderSceneSetup
    RenderSceneFenceWaitReturn,   // return address of RenderSceneSetup's per-tick fence wait
    RenderSceneFenceOffset,       // RenderSceneSetup's this: its CRenderFence pointer
    ParallelStagedCountOffset,    // FlushCommands' this: entry count of staged bucket 0
    ParallelBucketStride,         // bytes from one bucket of a list to the next
    ParallelBucketCount,          // buckets per list (hashed by proxy address)
    Count,
};

struct ValueEntry {
    const char* name;
    uintptr_t next_gen_404;
    uintptr_t remaster_501;
};

inline constexpr ValueEntry kValues[] = {
    {"RenderSchedulerGlobal", 0x057F5930, 0x05A5B8F0},
    {"RenderContextSharedOffset", 0x620, 0x5440},
    {"ViewCentreIndex", 0x100, 0x130},
    {"CustomCameraVtable", 0x02D645B0, 0x03810198},
    {"ViewFactoryReturn", 0x0162196D, 0x022C101A},
    {"ViewCopyRebuildReturn", 0x015FF863, 0x02292EA7},
    // The factory reads up to about +0xB765 (4.04) and +0xF714 (5.01).
    {"SceneDescriptorBytes", 0xC000, 0x10000},
    {"SceneDescriptorMinimumBytes", 0xB000, 0xF800},
    {"GameplayFrameBuilderReturn", 0x01D002CF, 0x01D0C5E1},
    // 5.01 values follow the view stride (+0xD0 per view) and match the
    // temporal writer (frame+0x10 +0x4D0/+0xAB0) and the builder (+0xBEC).
    {"FrameSecondaryViewOffset", 0x530, 0x600},
    {"FramePrimaryCentreOffset", 0x420, 0x4E0},
    {"FrameSecondaryCentreOffset", 0x930, 0xAC0},
    {"FrameInputExtentOffset", 0xA4C, 0xBEC},
    // Secondary view +0x30 (view floats 0x0C-0x1B, unchanged below 0xAC).
    {"FrameCameraTransformOffset", 0x560, 0x630},
    // EnableManualControl: the custom-camera fields moved by +0x10 in 5.01.
    {"CustomCameraManualControlOffset", 0x259, 0x269},
    // 5.01 values from the WorldVectorToViewRatio native (decoder table in its
    // lea r14, out parameter read at +0x8D) and its helper's constants.
    {"ScriptArgumentDecoderTable", 0x057ED890, 0x05A75A70},
    {"ScriptOutParameter", 0x057F58E0, 0x05A7DA80},
    {"ProjectionNearThreshold", 0x02D84B2C, 0x03AB7838},
    {"ProjectionNumerator", 0x02D84EA0, 0x03AB7D00},
    {"ProjectionYSignMask", 0x02D865B0, 0x03AB9900},
    // Previous-frame camera record inside a view: the frame-data factory tests
    // its valid byte at r8+0x470 (4.04 0x0162152D) / r8+0x540 (5.01 0x022C0888),
    // with the view at descriptor+0x10.
    {"TemporalCameraRecordOffset", 0x460, 0x530},
    // DLSS state object (rcx of the three DLSS functions): the Streamline
    // viewport id and the per-frame guards of the constants builder, the SR
    // apply and the input prepare (compared with frame_data +0xC44/+0xC40).
    {"DlssStateViewport", 0, 0x180},
    {"DlssStateConstantsFrame", 0x6C, 0x120},
    {"DlssStateApplyFrame", 0x70, 0x124},
    {"DlssStatePrepareFrame", 0x74, 0x128},
    // Byte the constants builder sets when this frame's constants request a
    // history reset (stored 0x01B80513, read for Constants.reset 0x01B80552).
    {"DlssStateResetFlag", 0, 0x110},
    // frame_data byte the upscaler dispatch tests to take the Ray
    // Reconstruction path instead of SR apply (0x01C09F7A).
    {"DlssFrameRayReconstructionFlag", 0, 0xF73C},
    // The DLSS functions receive frame + 0x10 (FrameBuilder: lea r14,[r12+0x10]).
    {"DlssFrameDataFromFrame", 0x10, 0x10},
    // 5.01 ViewRebuild (0x02294135) multiplies the projection (+0x180) and
    // the previous projection (+0x1C0) by a translation with row 3 =
    // (view+0x520, view+0x524, 0, 1): a raw NDC offset applied after the
    // centre/jitter translation. Zeroed by the view constructors and copied
    // by the copy routines; nothing else in the game writes it. 4.04 has no
    // such field.
    {"ViewProjectionOffsetIndex", 0, 0x148},
    // Current-to-previous clip-space reprojection (4x4, view+0x400 on 5.01)
    // that ViewRebuild computes from the projections before any centre or
    // offset translation; the DLSS input prepare and other temporal passes
    // read it from frame_data +0x410.
    {"ReprojectionMatrixIndex", 0, 0x100},
    // Screen fade (theGame.FadeOut/FadeIn, blackscreens) in the GUI pass
    // 0x01C26A60: after the HUD movie lists, if the amount at fade block
    // +0x48 is above 0, the colour (rgb*255, amount*255) goes to GuiFillRect
    // (call 0x01C26F1E). 0x01C0B7E0 is a standalone copy of that draw
    // (call 0x01C0B948). 4.04 has no such helper.
    {"GuiFadeAmountTest", 0, 0x01C26DE4},
    {"GuiFadeFillReturn", 0, 0x01C26F23},
    {"FadeObjectFillReturn", 0, 0x01C0B94D},
    // [FIX:PAIR-ENQUEUE-AFTER-FENCE 2/11] RenderSceneSetup 0x0227D1F0 calls
    // the frame factory (0x0227D97D), then waits on the fence at this+0x1D0
    // that the previous tick's CRenderCommand_Fence signals (0x0227D9F4; the
    // first call finds no fence, creates it and does not wait), then commits
    // the primary RenderScene and the next Fence command.
    {"RenderSceneFactoryReturn", 0, 0x0227D982},
    {"RenderSceneFenceWaitReturn", 0, 0x0227D9F7},
    {"RenderSceneFenceOffset", 0, 0x1D0},
    // ParallelRenderCommands (renderer+0x118): 17 spinlocks, then the append,
    // staged and ready lists, 17 buckets each of 12 bytes (pointer, u32
    // count): staged at +0x154 (count +0x15C), ready at +0x220. The publish
    // (renderer vtable +0x2F8, called by the RenderScene and Fence command
    // constructors) appends the append lists to the staged ones;
    // FlushCommands swaps staged into ready and applies the ready entries.
    {"ParallelStagedCountOffset", 0, 0x15C},
    {"ParallelBucketStride", 0, 0xC},
    {"ParallelBucketCount", 0, 17},
};
static_assert(sizeof(kValues) / sizeof(kValues[0]) == static_cast<size_t>(Value::Count));

constexpr uintptr_t value(game_build::Build build, Value which) {
    const ValueEntry& entry = kValues[static_cast<size_t>(which)];
    switch (build) {
    case game_build::Build::NextGen404:
        return entry.next_gen_404;
    case game_build::Build::Remaster501:
        return entry.remaster_501;
    case game_build::Build::Unknown:
        break;
    }
    return 0;
}

// RVA of |function| on |build|, or 0 when it is unknown there.
constexpr uintptr_t rva(game_build::Build build, Function function) {
    switch (build) {
    case game_build::Build::NextGen404:
        return entry(function).next_gen_404;
    case game_build::Build::Remaster501:
        return entry(function).remaster_501;
    case game_build::Build::Unknown:
        break;
    }
    return 0;
}

// Expected leading bytes at the RVA on |build|; empty when none are checked.
constexpr Signature signature(game_build::Build build, Function function) {
    return build == game_build::Build::Remaster501 ?
        entry(function).remaster_501_signature : Signature{nullptr, 0};
}

constexpr bool signature_matches(const uint8_t* code, Signature expected) {
    for (size_t i = 0; i < expected.size; ++i) {
        if (code[i] != expected.bytes[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace w3vr::engine_addresses
