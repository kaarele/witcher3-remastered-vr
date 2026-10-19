#include "call_site_hook.h"

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

using Function = int (*)();
Function g_original{};

int detour() {
    return g_original() + 100;
}
}  // namespace

int main() {
    using namespace w3vr::call_site_hook;

    int32_t rel{};
    require(rel32_to(0x140001000, 0x140001005, rel) && rel == 0,
        "rel32 to the next instruction is 0");
    require(rel32_to(0x140001000, 0x140000000, rel) && rel == -0x1005,
        "backward rel32");
    require(!rel32_to(0x140000000, 0x240000000, rel), "4 GB away is out of reach");
    const uint8_t call_bytes[] = {0xE8, 0xFB, 0xFF, 0xFF, 0xFF};
    require(call_destination(call_bytes) == reinterpret_cast<uintptr_t>(call_bytes),
        "call destination decodes a negative rel32");

    // caller: sub rsp,0x28; call callee; add rsp,0x28; ret
    // callee (+0x40): mov eax,5; ret. Returns 5 unhooked, 105 hooked.
    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    require(code != nullptr, "executable test buffer");
    if (code != nullptr) {
        const uint8_t caller[] = {
            0x48, 0x83, 0xEC, 0x28,
            0xE8, 0x37, 0x00, 0x00, 0x00,  // call +0x37 -> code + 0x40
            0x48, 0x83, 0xC4, 0x28,
            0xC3};
        const uint8_t callee[] = {0xB8, 0x05, 0x00, 0x00, 0x00, 0xC3};
        std::memcpy(code, caller, sizeof(caller));
        std::memcpy(code + 0x40, callee, sizeof(callee));
        FlushInstructionCache(GetCurrentProcess(), code, 4096);
        const auto run = reinterpret_cast<Function>(code);
        require(run() == 5, "unhooked call result");
        void* original{};
        require(install(code, reinterpret_cast<void*>(&detour), &original) == nullptr,
            "a non-call instruction is rejected");
        uint8_t* relay = install(code + 4, reinterpret_cast<void*>(&detour), &original);
        require(relay != nullptr, "call redirects");
        require(original == code + 0x40, "original is the unpatched callee");
        require(relay != nullptr && call_destination(code + 4) ==
                reinterpret_cast<uintptr_t>(relay),
            "the call now reaches the jump block");
        g_original = reinterpret_cast<Function>(original);
        require(run() == 105, "redirected call reaches the detour and the callee");
        require(g_original() == 5, "callee itself is unchanged");
    }

    if (failures == 0) {
        std::printf("call site hook tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
