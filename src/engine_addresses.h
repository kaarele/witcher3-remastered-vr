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
    FrustumAabbTest,       // (frustum, world box {min, max}): 1 = box fully outside the frustum
    ActorGetHeadBoneIndex,           // native CActor.GetHeadBoneIndex
    EntityGetBoneWorldMatrixByIndex, // native CEntity.GetBoneWorldMatrixByIndex
    DlssMipBias,           // (frame_data, _, float* bias): texture LOD bias for the frame's samplers
    FsrUpscaleDispatch,    // FSR upscale dispatch (state, frame_data, params, index, ...)
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
// 29 bytes because the first 24 (a common prologue) occur 186 times in the
// 5.01 .text; the 29 are unique.
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

// [FIX:REMASTER-ANIMATED-VISIBILITY 1/8] Frustum AABB test (frustum, world
// box {min at +0, max at +0x10}). Both builds return al = 1 when the box is
// fully outside one of the six planes and 0 when it is inside or
// intersecting. 5.01 rewrote it with AVX over a structure-of-arrays frustum
// (planes at +0x80..+0x140). In each build its only caller is the
// animated-component visibility update (AnimatedComponentVisibilityReturn).
// The 5.01 code has a VEX instruction at byte 4 (MinHook cannot relocate it)
// and a RIP-relative one at byte 13 (too short for prefix_hook), so on 5.01
// the mod redirects that one call (src/call_site_hook.h) and leaves the
// function itself unpatched.
inline constexpr uint8_t kFrustumAabbTest501Bytes[] = {
    0x48, 0x83, 0xEC, 0x18, 0xC5, 0xF8, 0x10, 0x52, 0x10, 0xC5, 0xE8, 0x58,
    0x02, 0xC5, 0xF8, 0x59};
// The visibility update's call of the test (call rel32, ending at the return
// address; the rel32 encodes FrustumAabbTest) and the code right after it:
// lea rcx,[r15+0xF50]; mov [r15+0xF74],al (the "outside" byte the update
// branches on).
inline constexpr uint8_t kAnimatedComponentFrustumCall501Bytes[] = {
    0xE8, 0x20, 0x38, 0x56, 0xFE};
inline constexpr uint8_t kAnimatedComponentFrustumStore501Bytes[] = {
    0x49, 0x8D, 0x8F, 0x50, 0x0F, 0x00, 0x00, 0x41, 0x88, 0x87, 0x74, 0x0F,
    0x00, 0x00};
// [FEATURE:REMASTER-DLSS-IMAGE 1/5] 5.01 texture LOD bias, called by the frame
// builder (0x141C1AC02) and two other passes. Unless frame_data+0xBEC is set,
// AA modes 3-8 get clamp(log2(render_h / output_h) - 1, -3, 0) plus the
// TextureMipBias setting (render_h at frame_data+0xBE0, output_h at +0xBE8),
// other modes TextureMipBias alone. push rbx; sub rsp,0x30; cmp byte ptr
// [rcx+0xBEC],0; mov rbx,r8; mov r8,rcx; jne: unique in the 5.01 exe.
inline constexpr uint8_t kDlssMipBias501Bytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x80, 0xB9, 0xEC, 0x0B, 0x00, 0x00,
    0x00, 0x49, 0x8B, 0xD8, 0x4C, 0x8B, 0xC1, 0x0F, 0x85, 0x84, 0x00, 0x00,
    0x00};

// Registered script natives used by the first-person head provider. The
// opening bytes are identical in 4.04 and 5.01 and unique in the 5.01 exe;
// the head getter forwards to the actor's +0x200 animated component.
inline constexpr uint8_t kActorGetHeadBoneIndexBytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xFF, 0x42, 0x30, 0x48, 0x81,
    0xC1, 0x00, 0x02, 0x00, 0x00, 0x49, 0x8B, 0xD8, 0x48, 0x8B, 0x01, 0xFF,
    0x50, 0x38};
inline constexpr uint8_t kEntityGetBoneWorldMatrixByIndexBytes[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x18, 0x48,
    0x89, 0x78, 0x20, 0x55, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0x00,
    0x01, 0x00, 0x00};

// [FIX:REMASTER-ASYNC-ANIM-TICK-VISIBILITY 1/5] 5.01 only. The async
// animation tick (CAsyncAnimTickManager, tick 0x14265D900) skips every
// registered entity (CAnimatedEntity, CBgCutsceneEntity, CBgNpc) whose cached
// box lies outside its frustum: such an entity is neither ticked nor gets its
// box refreshed. Its sync step (0x14265E870) builds that frustum from the GAME
// camera's world-to-clip matrix with FrustumFromMatrix and copies it to
// manager+0x40. The frustum's structure-of-arrays planes (8 lanes, 6 used):
// nx +0x60, ny +0x80, nz +0xA0, d +0xC0, |nx| +0xE0, |ny| +0x100, |nz| +0x120;
// the array-of-planes copy at +0x00..+0x5F is not read by the tick.
inline constexpr uintptr_t kFrustumFromMatrix501Rva = 0x026EE940;
// mov rax,rsp; sub rsp,0x118 (the next instruction is RIP-relative).
inline constexpr uint8_t kFrustumFromMatrix501Bytes[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x81, 0xEC, 0x18, 0x01, 0x00, 0x00};
// The sync step's call of FrustumFromMatrix (call rel32 ending at the return
// address) and the copy loop setup after it: lea rax,[r13+0x40]; mov edx,2.
inline constexpr uintptr_t kAsyncAnimTickFrustumReturn501Rva = 0x0265E9E9;
inline constexpr uint8_t kAsyncAnimTickFrustumCall501Bytes[] = {
    0xE8, 0x57, 0xFF, 0x08, 0x00};
inline constexpr uint8_t kAsyncAnimTickFrustumCopy501Bytes[] = {
    0x49, 0x8D, 0x45, 0x40, 0xBA, 0x02, 0x00, 0x00, 0x00};
// The tick's inline plane test, from the nx multiply to the skip: the
// manager+0xA0..+0x160 operands pin the plane layout above, and the box is
// outside when any of the 6 lanes of n.c + d + |n|.e is negative
// (vcmpltps; vmovmskps; test al,0x3F; jne skip).
inline constexpr uintptr_t kAsyncAnimTickCull501Rva = 0x0265DA49;
inline constexpr uint8_t kAsyncAnimTickCull501Bytes[] = {
    0xC5, 0xFC, 0x59, 0x8F, 0xA0, 0x00, 0x00, 0x00, 0xC5, 0xF4, 0x58, 0x9F,
    0x00, 0x01, 0x00, 0x00, 0xC4, 0xE3, 0x7D, 0x04, 0xD5, 0x55, 0xC5, 0xEC,
    0x59, 0x87, 0xC0, 0x00, 0x00, 0x00, 0xC5, 0xE4, 0x58, 0xE0, 0xC4, 0xE3,
    0x7D, 0x04, 0xCD, 0xAA, 0xC5, 0xF4, 0x59, 0x97, 0xE0, 0x00, 0x00, 0x00,
    0xC5, 0xDC, 0x58, 0xEA, 0xC4, 0xE3, 0x7D, 0x04, 0xC6, 0x00, 0xC5, 0xFC,
    0x59, 0x9F, 0x20, 0x01, 0x00, 0x00, 0xC4, 0xE3, 0x7D, 0x04, 0xCE, 0x55,
    0xC5, 0xF4, 0x59, 0x97, 0x40, 0x01, 0x00, 0x00, 0xC4, 0xE3, 0x7D, 0x04,
    0xC6, 0xAA, 0xC5, 0xFC, 0x59, 0x8F, 0x60, 0x01, 0x00, 0x00, 0xC5, 0xE4,
    0x58, 0xE2, 0xC5, 0xDC, 0x58, 0xD1, 0xC5, 0xD4, 0x58, 0xDA, 0xC4, 0xC1,
    0x64, 0xC2, 0xC4, 0x11, 0xC5, 0xFC, 0x50, 0xC0, 0xA8, 0x3F, 0x0F, 0x85,
    0x18, 0x01, 0x00, 0x00};

// [FIX:REMASTER-PAIR-JITTER 1/6] 5.01 only. The frame builder (FrameBuilder)
// asks the temporal jitter generator (0x01C0A9B0: (context, float2* out,
// mode, frame_data, index); Halton(3,2) of index + 1, or the FidelityFX
// jitter query for FSR) twice per render: the current jitter with the render
// counter (render context +0x7F0, +1 per engine render) and the previous one
// with counter - 1. In strict Stereo every eye render advances the counter,
// so each eye's own history only ever sees every other Halton sample (half
// the pixel in y) and the two eyes jitter differently. Generator entry:
// mov [rsp+8],rcx; push rbx; push rdi; push r14; sub rsp,0x70; xor ebx,ebx;
// lea r14,[rdx+4].
inline constexpr uintptr_t kTemporalJitterGenerator501Rva = 0x01C0A9B0;
inline constexpr uint8_t kTemporalJitterGenerator501Bytes[] = {
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC,
    0x70, 0x33, 0xDB, 0x4C, 0x8D, 0x72, 0x04};
// The current-jitter call, from its counter load: mov eax,[rsi+0x7F0];
// lea rdx,[rsp+0x50]; mov r9,r14; mov [rsp+0x20],eax; call rel32.
inline constexpr uintptr_t kTemporalJitterCurrentSite501Rva = 0x01C1B754;
inline constexpr uintptr_t kTemporalJitterCurrentCall501Rva = 0x01C1B766;
inline constexpr uint8_t kTemporalJitterCurrentSite501Bytes[] = {
    0x8B, 0x86, 0xF0, 0x07, 0x00, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x50, 0x4D,
    0x8B, 0xCE, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x45, 0xF2, 0xFE, 0xFF};
// The previous-jitter call: mov eax,[rsi+0x7F0]; mov r9,r14; dec eax;
// mov [rsp+0x20],eax; mov r8d,[r8+0x74]; call rel32.
inline constexpr uintptr_t kTemporalJitterPreviousSite501Rva = 0x01C1B773;
inline constexpr uintptr_t kTemporalJitterPreviousCall501Rva = 0x01C1B786;
inline constexpr uint8_t kTemporalJitterPreviousSite501Bytes[] = {
    0x8B, 0x86, 0xF0, 0x07, 0x00, 0x00, 0x4D, 0x8B, 0xCE, 0xFF, 0xC8, 0x89,
    0x44, 0x24, 0x20, 0x45, 0x8B, 0x40, 0x74, 0xE8, 0x25, 0xF2, 0xFE, 0xFF};

// [FIX:REMASTER-PAIR-NOISE 1/4] 5.01 only. The builder copies the render
// counter to frame_data +0xC40. GTAO (0x01D19810) uses it & 63 as its noise
// index and the screen-space shadow pass (0x01C6CC20) passes it to its
// constants, so both noise patterns differ per eye and each eye's GTAO
// history sees only every other index. Each site is one 7-byte load from
// r14 = frame_data, which the mod replaces with a call into a stub.
// GTAO: mov ecx,[r14+0xBE0]; mov edx,[r14+0xC40] (site); mov [rip+..],eax.
inline constexpr uintptr_t kGtaoNoiseReadContext501Rva = 0x01D1A0A6;
inline constexpr uintptr_t kGtaoNoiseRead501Rva = 0x01D1A0AD;
inline constexpr uint8_t kGtaoNoiseReadContext501Bytes[] = {
    0x41, 0x8B, 0x8E, 0xE0, 0x0B, 0x00, 0x00, 0x41, 0x8B, 0x96, 0x40, 0x0C,
    0x00, 0x00, 0x89, 0x05};
// Screen shadows: jne +0x0C; mov eax,[r14+0xC40] (site); mov [rbp-0x60],eax;
// jmp +3.
inline constexpr uintptr_t kScreenShadowNoiseReadContext501Rva = 0x01C6D1A8;
inline constexpr uintptr_t kScreenShadowNoiseRead501Rva = 0x01C6D1AA;
inline constexpr uint8_t kScreenShadowNoiseReadContext501Bytes[] = {
    0x75, 0x0C, 0x41, 0x8B, 0x86, 0x40, 0x0C, 0x00, 0x00, 0x89, 0x45, 0xA0,
    0xEB, 0x03};
inline constexpr size_t kFrameNoiseReadBytes = 7;

// [FEATURE:REMASTER-FSR-PER-EYE 1/7] 5.01 FSR upscale dispatch, the only
// caller of ffxDispatch with FFX_API_DISPATCH_DESC_TYPE_UPSCALE (0x01C66C3D).
// The upscaler selection (0x01C09F57) passes the FSR state (renderer +0xD0)
// and the same frame_data as the DLSS functions. It dispatches on the
// ffxContext at state +0x40 (created by 0x01C64D20, one per game) and takes
// the history reset from the byte at state +0x26, which it clears afterwards
// unless frame generation owns it (state +0x24). Leading bytes up to the
// RIP-relative compare, unique in the exe.
inline constexpr uint8_t kFsrUpscaleDispatch501Bytes[] = {
    0x40, 0x55, 0x53, 0x57, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x68, 0xFE,
    0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x98, 0x02, 0x00, 0x00, 0x80, 0x3D};
// [FEATURE:REMASTER-FSR2-PER-EYE 1/4] The built-in FSR 2 (GPUs the FidelityFX
// loader does not serve with FSR 4, e.g. NVIDIA, Intel, RX 6000): the upscale
// dispatch above starts with cmp byte [flag], 0 (disp32 right after these
// bytes); with the flag set it calls the exe's own FSR 2 (0x01CD4A20) on the
// object at state +8 with the same frame_data and the render surfaces
// (CRenderSurfaces; CRenderTarget* table at +0x30, indexed by the
// render-target name) and returns. That FSR 2 keeps its per-frame state in the
// object: +0x10 a counter whose low bit picks the ping-pong history pair,
// +0x14 the reset byte (read and cleared on entry), +0x3C the constants'
// frame index (0 after a reset, +1 per dispatch). Its history lives in five
// engine render targets: RTN_FSR2_LockStatus1/2 (0x5A/0x5B),
// RTN_FSR2_InternalUpscaled1/2 (0x5C/0x5D) and RTN_FSR2_LumaHistory (0x5F);
// the other RTN_FSR2_* targets are written and read within one dispatch.
// Bytes at FsrBuiltinBranch: je over it; mov eax,[rbp+0x1E0];
// mov rcx,[rcx+8]; mov [rsp+0x20],eax; call rel32 (-> Fsr2BuiltinDispatch).
inline constexpr uint8_t kFsrBuiltinBranch501Bytes[] = {
    0x74, 0x20, 0x8B, 0x85, 0xE0, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x49, 0x08,
    0x89, 0x44, 0x24, 0x20, 0xE8};
inline constexpr uint8_t kFsr2BuiltinDispatch501Bytes[] = {
    0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
    0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0x88};
// movzx eax,byte [rcx+0x14] ... mov r15,rcx; mov byte [rcx+0x14],0
inline constexpr uint8_t kFsr2DispatchReset501Bytes[] = {
    0x0F, 0xB6, 0x41, 0x14, 0x33, 0xDB, 0x88, 0x85, 0xD8, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0xFA, 0x49, 0x63, 0xC1, 0x4C, 0x8B, 0xF9, 0xC6, 0x41, 0x14,
    0x00};
// mov eax,[r15+0x10]; and eax,1; ...; mov r9,[r8+rcx*8+0x300] (LockStatus)
inline constexpr uint8_t kFsr2DispatchParity501Bytes[] = {
    0x41, 0x8B, 0x47, 0x10, 0x83, 0xE0, 0x01, 0x89, 0x55, 0x80, 0x8B, 0xC8,
    0x83, 0xF0, 0x01, 0x4D, 0x8B, 0x8C, 0xC8, 0x00, 0x03, 0x00, 0x00};
// mov rax,[r8+rcx*8+0x310] (InternalUpscaled)
inline constexpr uint8_t kFsr2DispatchUpscaled501Bytes[] = {
    0x49, 0x8B, 0x84, 0xC8, 0x10, 0x03, 0x00, 0x00};
// mov rax,[r8+0x328] (LumaHistory)
inline constexpr uint8_t kFsr2DispatchLuma501Bytes[] = {
    0x49, 0x8B, 0x80, 0x28, 0x03, 0x00, 0x00};
// test r8b,r8b; je; mov [r15+0x3C],ebx; jmp; inc dword [r15+0x3C]
inline constexpr uint8_t kFsr2DispatchFrameIndex501Bytes[] = {
    0x45, 0x84, 0xC0, 0x74, 0x06, 0x41, 0x89, 0x5F, 0x3C, 0xEB, 0x04, 0x41,
    0xFF, 0x47, 0x3C};
// inc eax; and eax,0xF; mov [r15+0x10],eax
inline constexpr uint8_t kFsr2DispatchCounter501Bytes[] = {
    0xFF, 0xC0, 0x83, 0xE0, 0x0F, 0x41, 0x89, 0x47, 0x10};
// CRenderTarget (0x50 bytes, vtable 0x036EBA80): +8 the GpuApi texture id,
// +0xC/+0x10 size, +0x14 type, +0x18 format. Vtable slot 1 releases the
// texture and clears +8; slot 2 creates it from the object's description and
// stores the id at +8 (the store below), without touching anything else.
inline constexpr uint8_t kRenderTargetRelease501Bytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x41, 0x08, 0x48, 0x8D, 0x59,
    0x08, 0x85, 0xC0, 0x74};
inline constexpr uint8_t kRenderTargetCreate501Bytes[] = {
    0x40, 0x55, 0x53, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x78,
    0xFF, 0xFF, 0xFF, 0x48};
// cmp dword [r13+0x14],9; lea rcx,[r13+8]; mov [rcx],r14d
inline constexpr uint8_t kRenderTargetIdStore501Bytes[] = {
    0x41, 0x83, 0x7D, 0x14, 0x09, 0x49, 0x8D, 0x4D, 0x08, 0x44, 0x89, 0x31};

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
    {"FrustumAabbTest", 0x0031E050, 0x0099B2B0,
        {kFrustumAabbTest501Bytes, sizeof(kFrustumAabbTest501Bytes)}},
    {"Actor.GetHeadBoneIndex", 0x01C4B550, 0x0210A1F0,
        {kActorGetHeadBoneIndexBytes, sizeof(kActorGetHeadBoneIndexBytes)}},
    {"Entity.GetBoneWorldMatrixByIndex", 0x0152E600, 0x02259FC0,
        {kEntityGetBoneWorldMatrixByIndexBytes,
            sizeof(kEntityGetBoneWorldMatrixByIndexBytes)}},
    {"DlssMipBias", 0, 0x01C1D180,
        {kDlssMipBias501Bytes, sizeof(kDlssMipBias501Bytes)}},
    {"FsrUpscaleDispatch", 0, 0x01C665B0,
        {kFsrUpscaleDispatch501Bytes, sizeof(kFsrUpscaleDispatch501Bytes)}},
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
    AnimatedComponentVisibilityReturn,  // return address of the frustum test call in the
                                        // animated-component visibility update
    FsrStateContext,              // FSR state: the game's upscale ffxContext
    FsrStateResetFlag,            // FSR state: byte requesting a history reset for the next dispatch
    FsrBuiltinBranch,             // RVA of the upscale dispatch's call into the built-in FSR 2
    Fsr2BuiltinDispatch,          // RVA of the built-in FSR 2 dispatch (object, frame_data, surfaces, ...)
    Fsr2DispatchResetSite,        // RVA: its reset-byte read and clear
    Fsr2DispatchParitySite,       // RVA: its ping-pong parity and LockStatus table read
    Fsr2DispatchUpscaledSite,     // RVA: its InternalUpscaled table read
    Fsr2DispatchLumaSite,         // RVA: its LumaHistory table read
    Fsr2DispatchFrameIndexSite,   // RVA: its frame-index reset / increment
    Fsr2DispatchCounterSite,      // RVA: its counter increment at the end
    FsrStateBuiltinFsr2,          // FSR state: the built-in FSR 2 object
    Fsr2FrameCounter,             // FSR 2 object: dispatch counter (low bit = history pair)
    Fsr2ResetFlag,                // FSR 2 object: reset byte
    Fsr2FrameIndex,               // FSR 2 object: constants' frame index
    RenderSurfacesTargets,        // CRenderSurfaces: CRenderTarget* table, indexed by name
    RenderTargetVtable,           // RVA of the CRenderTarget vtable
    RenderTargetReleaseFunction,  // RVA of CRenderTarget vtable slot 1 (release texture)
    RenderTargetCreateFunction,   // RVA of CRenderTarget vtable slot 2 (create texture)
    RenderTargetIdStoreSite,      // RVA: slot 2's store of the new texture id
    RenderTargetTexture,          // CRenderTarget: GpuApi texture id
    RenderTargetObjectSize,       // CRenderTarget: object size
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
    // [FIX:REMASTER-ANIMATED-VISIBILITY 2/8] CAnimatedComponent vtable slot 67
    // (4.04 0x015E1750, 5.01 0x02437970; shared by the mimic, moving-agent
    // and background-root components) tests the component's world bounds
    // against the game camera's frustum and stores the result at +0xF74.
    // While it is set, the update skips the skinned attachments (the body
    // keeps its last pose, or the bind pose if it was never updated) and only
    // moves plain child transforms such as a held weapon.
    {"AnimatedComponentVisibilityReturn", 0x015E186C, 0x02437A90},
    // [FEATURE:REMASTER-FSR-PER-EYE 2/7] See kFsrUpscaleDispatch501Bytes.
    {"FsrStateContext", 0, 0x40},
    {"FsrStateResetFlag", 0, 0x26},
    // [FEATURE:REMASTER-FSR2-PER-EYE 2/4] See kFsrBuiltinBranch501Bytes.
    {"FsrBuiltinBranch", 0, 0x01C665D5},
    {"Fsr2BuiltinDispatch", 0, 0x01CD4A20},
    {"Fsr2DispatchResetSite", 0, 0x01CD4A72},
    {"Fsr2DispatchParitySite", 0, 0x01CD4B88},
    {"Fsr2DispatchUpscaledSite", 0, 0x01CD4BCC},
    {"Fsr2DispatchLumaSite", 0, 0x01CD4C11},
    {"Fsr2DispatchFrameIndexSite", 0, 0x01CD5197},
    {"Fsr2DispatchCounterSite", 0, 0x01CD6612},
    {"FsrStateBuiltinFsr2", 0, 0x8},
    {"Fsr2FrameCounter", 0, 0x10},
    {"Fsr2ResetFlag", 0, 0x14},
    {"Fsr2FrameIndex", 0, 0x3C},
    {"RenderSurfacesTargets", 0, 0x30},
    {"RenderTargetVtable", 0, 0x036EBA80},
    {"RenderTargetReleaseFunction", 0, 0x01BEF260},
    {"RenderTargetCreateFunction", 0, 0x01BEF2C0},
    {"RenderTargetIdStoreSite", 0, 0x01BEF8B2},
    {"RenderTargetTexture", 0, 0x8},
    {"RenderTargetObjectSize", 0, 0x50},
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
