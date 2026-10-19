#include "engine_addresses.h"
#include "game_build.h"

#include <cstdio>
#include <cstring>

namespace {
int failures{};
void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
}

int main() {
    using namespace w3vr::game_build;

    require(identify(kNextGen404.time_date_stamp, kNextGen404.size_of_image) ==
            Build::NextGen404,
        "4.04 PE identity is recognized");
    require(identify(kRemaster501.time_date_stamp, kRemaster501.size_of_image) ==
            Build::Remaster501,
        "Remaster 5.01 PE identity is recognized");
    require(identify(kNextGen404.time_date_stamp, kRemaster501.size_of_image) ==
            Build::Unknown,
        "mixed identity fields are unknown");
    require(identify(0, 0) == Build::Unknown, "empty identity is unknown");

    require(engine_hooks_supported(Build::NextGen404),
        "engine hooks stay enabled on 4.04");
    require(!engine_hooks_supported(Build::Remaster501),
        "engine hooks are disabled on the Remaster until ported");
    require(!engine_hooks_supported(Build::Unknown),
        "engine hooks are disabled on unknown builds");

    require(std::strcmp(name(Build::NextGen404), "next-gen-4.04") == 0,
        "4.04 build name");
    require(std::strcmp(name(Build::Unknown), "unknown") == 0,
        "unknown build name");

    static_assert(identify(kNextGen404.time_date_stamp,
                      kNextGen404.size_of_image) == Build::NextGen404);

    {
        namespace addresses = w3vr::engine_addresses;
        using addresses::Function;
        require(addresses::rva(Build::NextGen404, Function::IsAnyMenu) == 0x01F3C5C0,
            "4.04 IsAnyMenu keeps its original RVA");
        require(addresses::rva(Build::Remaster501, Function::IsAnyMenu) == 0x01DF41A0,
            "5.01 IsAnyMenu RVA");
        require(addresses::rva(Build::NextGen404, Function::IsLoadingScreenVideoPlaying) ==
                0x0154EED0,
            "4.04 IsLoadingScreenVideoPlaying keeps its original RVA");
        require(addresses::rva(Build::Remaster501, Function::IsLoadingScreenVideoPlaying) ==
                0x0227A0F0,
            "5.01 IsLoadingScreenVideoPlaying RVA");
        require(addresses::rva(Build::NextGen404, Function::ActorIsInGameplayScene) ==
                0x01C4CD70 &&
                addresses::rva(Build::NextGen404, Function::ActorIsInNonGameplayCutscene) ==
                0x01C4CD90 &&
                addresses::rva(Build::NextGen404, Function::GameIsPlayingNonGameplayScene) ==
                0x01549800,
            "4.04 scene queries keep their original RVAs");
        for (size_t i = 0; i < static_cast<size_t>(Function::Count); ++i) {
            const auto function = static_cast<Function>(i);
            require(addresses::rva(Build::Remaster501, function) != 0 &&
                    addresses::signature(Build::Remaster501, function).size != 0,
                "every 5.01 entry has an address and a signature");
        }
        require(addresses::rva(Build::NextGen404, Function::FrameDataFactory) == 0x016214C0 &&
                addresses::rva(Build::NextGen404, Function::SceneEnqueue) == 0x01621FC0,
            "4.04 factory and enqueue keep their original RVAs");
        require(addresses::value(Build::NextGen404,
                    addresses::Value::RenderSchedulerGlobal) == 0x057F5930 &&
                addresses::value(Build::NextGen404,
                    addresses::Value::RenderContextSharedOffset) == 0x620,
            "4.04 data values keep their original values");
        require(addresses::value(Build::Unknown, addresses::Value::RenderSchedulerGlobal) == 0,
            "no data values on unknown builds");
        require(addresses::rva(Build::NextGen404, Function::ViewRebuild) == 0x015FE550 &&
                addresses::rva(Build::NextGen404, Function::GameplayEntry) == 0x01D00260 &&
                addresses::rva(Build::NextGen404, Function::RenderSceneSetup) == 0x01553070 &&
                addresses::rva(Build::NextGen404, Function::TopmostCameraObject) == 0x0161B9C0,
            "4.04 stereo-path RVAs keep their original values");
        require(addresses::value(Build::NextGen404, addresses::Value::ViewCentreIndex) == 0x100 &&
                addresses::value(Build::NextGen404, addresses::Value::CustomCameraVtable) ==
                0x02D645B0 &&
                addresses::value(Build::NextGen404, addresses::Value::ViewFactoryReturn) ==
                0x0162196D &&
                addresses::value(Build::NextGen404, addresses::Value::ViewCopyRebuildReturn) ==
                0x015FF863 &&
                addresses::value(Build::NextGen404, addresses::Value::SceneDescriptorBytes) ==
                0xC000 &&
                addresses::value(Build::NextGen404,
                    addresses::Value::SceneDescriptorMinimumBytes) == 0xB000,
            "4.04 stereo-path values keep their original values");
        require(addresses::rva(Build::NextGen404, Function::FrameBuilder) == 0x01D86400 &&
                addresses::value(Build::NextGen404,
                    addresses::Value::GameplayFrameBuilderReturn) == 0x01D002CF &&
                addresses::value(Build::NextGen404,
                    addresses::Value::FrameSecondaryViewOffset) == 0x530 &&
                addresses::value(Build::NextGen404,
                    addresses::Value::FramePrimaryCentreOffset) == 0x420 &&
                addresses::value(Build::NextGen404,
                    addresses::Value::FrameSecondaryCentreOffset) == 0x930 &&
                addresses::value(Build::NextGen404,
                    addresses::Value::FrameInputExtentOffset) == 0xA4C &&
                addresses::value(Build::NextGen404,
                    addresses::Value::FrameCameraTransformOffset) == 0x560,
            "4.04 frame-builder values keep their original values");
        for (size_t i = 0; i < static_cast<size_t>(addresses::Value::Count); ++i) {
            require(addresses::value(Build::Remaster501, static_cast<addresses::Value>(i)) != 0,
                "every 5.01 value is known");
        }
        require(addresses::rva(Build::NextGen404, Function::FrustumAabbTest) == 0x0031E050 &&
                addresses::value(Build::NextGen404,
                    addresses::Value::AnimatedComponentVisibilityReturn) == 0x015E186C,
            "4.04 animated-component visibility keeps its original RVAs");
        {
            // The 5.01 call bytes end at the return address and encode the test.
            const uint8_t* call = addresses::kAnimatedComponentFrustumCall501Bytes;
            int32_t rel{};
            std::memcpy(&rel, call + 1, sizeof(rel));
            const auto return_rva = static_cast<int64_t>(addresses::value(
                Build::Remaster501, addresses::Value::AnimatedComponentVisibilityReturn));
            require(call[0] == 0xE8 &&
                    sizeof(addresses::kAnimatedComponentFrustumCall501Bytes) == 5 &&
                    static_cast<uintptr_t>(return_rva + rel) ==
                        addresses::rva(Build::Remaster501, Function::FrustumAabbTest),
                "5.01 visibility call reaches the frustum test");
        }
        require(addresses::rva(Build::Unknown, Function::IsAnyMenu) == 0,
            "no engine addresses on unknown builds");
        require(addresses::signature(Build::NextGen404, Function::IsAnyMenu).size == 0,
            "4.04 entries are not signature-checked");
        const auto remaster =
            addresses::signature(Build::Remaster501, Function::IsAnyMenu);
        require(remaster.size != 0, "5.01 entries carry a signature");
        require(addresses::signature_matches(remaster.bytes, remaster),
            "a signature matches its own bytes");
        uint8_t changed[64]{};
        std::memcpy(changed, remaster.bytes, remaster.size);
        changed[remaster.size - 1] ^= 0xFF;
        require(!addresses::signature_matches(changed, remaster),
            "a changed byte fails the signature");
        require(addresses::signature_matches(changed, addresses::Signature{nullptr, 0}),
            "an empty signature always matches");
    }

    if (failures == 0) {
        std::puts("game_build tests passed");
    }
    return failures == 0 ? 0 : 1;
}
