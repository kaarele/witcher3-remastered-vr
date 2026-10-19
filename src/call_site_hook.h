#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "prefix_hook.h"

// [FIX:REMASTER-ANIMATED-VISIBILITY 3/8] Redirects one `call rel32` in engine
// code to a detour, for a callee that cannot be patched at its entry and that
// matters at only one call site (prefix_hook.h explains why small 5.01
// functions often cannot be). The callee stays unpatched: the original to
// call is the callee itself, and the detour sees the call's own return
// address. The new rel32 points at a 14-byte absolute jump to the detour,
// placed within rel32 reach of the call.
namespace w3vr::call_site_hook {

inline constexpr size_t kCallBytes = 5;  // E8 rel32
// Search window for the jump block on each side of the call.
inline constexpr uintptr_t kNearRange = 0x40000000;

// Destination of the call rel32 at |call|.
inline uintptr_t call_destination(const uint8_t* call) {
    int32_t rel{};
    std::memcpy(&rel, call + 1, sizeof(rel));
    return reinterpret_cast<uintptr_t>(call) + kCallBytes +
        static_cast<uintptr_t>(static_cast<intptr_t>(rel));
}

// rel32 that makes a call at |call_address| reach |destination|; false when
// the destination is out of reach.
inline bool rel32_to(uintptr_t call_address, uintptr_t destination, int32_t& rel) {
    const auto delta =
        static_cast<int64_t>(destination - (call_address + kCallBytes));
    if (delta < INT32_MIN || delta > INT32_MAX) {
        return false;
    }
    rel = static_cast<int32_t>(delta);
    return true;
}

// Commits |size| executable bytes at the free allocation-granularity address
// nearest to |origin|, below it first and then above, within kNearRange;
// nullptr when there is none.
inline uint8_t* allocate_near(uintptr_t origin, size_t size) {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const uintptr_t granularity = system.dwAllocationGranularity;
    if (granularity == 0 || size == 0 || size > granularity) {
        return nullptr;
    }
    // No std::min/max: <windows.h> may define min/max macros here.
    const auto minimum =
        reinterpret_cast<uintptr_t>(system.lpMinimumApplicationAddress);
    const auto maximum =
        reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress);
    const uintptr_t lowest = origin > minimum + kNearRange
        ? origin - kNearRange : minimum;
    const uintptr_t highest = origin < maximum - kNearRange
        ? origin + kNearRange : maximum;
    const auto commit = [size](uintptr_t address) {
        return static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(address),
            size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    };
    const uintptr_t start = origin - origin % granularity;
    for (uintptr_t address = start; address >= lowest + granularity;) {
        address -= granularity;
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<const void*>(address), &memory,
                sizeof(memory)) == 0) {
            break;
        }
        if (memory.State == MEM_FREE) {
            if (uint8_t* block = commit(address)) {
                return block;
            }
            continue;
        }
        // Skip the rest of this allocation.
        const auto base = reinterpret_cast<uintptr_t>(memory.AllocationBase);
        if (base < address) {
            address = base - base % granularity;
        }
    }
    for (uintptr_t address = start + granularity; address + size <= highest;) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<const void*>(address), &memory,
                sizeof(memory)) == 0) {
            break;
        }
        if (memory.State == MEM_FREE) {
            if (uint8_t* block = commit(address)) {
                return block;
            }
            address += granularity;
            continue;
        }
        // Next granularity boundary at or after the end of this region.
        const uintptr_t end = reinterpret_cast<uintptr_t>(memory.BaseAddress) +
            memory.RegionSize + granularity - 1;
        const uintptr_t next = end - end % granularity;
        address = next > address ? next : address + granularity;
    }
    return nullptr;
}

// Points the call rel32 at |call| (an E8 instruction) at a jump to |detour|
// and stores the callee in *original first. Returns the jump block, or
// nullptr with nothing changed. Must run while no thread executes the call
// (engine hooks are installed at startup).
inline uint8_t* install(uint8_t* call, void* detour, void** original) {
    if (call == nullptr || call[0] != 0xE8 || detour == nullptr ||
        original == nullptr) {
        return nullptr;
    }
    const auto call_address = reinterpret_cast<uintptr_t>(call);
    uint8_t* relay = allocate_near(call_address, prefix_hook::kAbsoluteJumpBytes);
    if (relay == nullptr) {
        return nullptr;
    }
    int32_t rel{};
    DWORD protect{};
    if (!rel32_to(call_address, reinterpret_cast<uintptr_t>(relay), rel) ||
        !VirtualProtect(call + 1, sizeof(rel), PAGE_EXECUTE_READWRITE, &protect)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return nullptr;
    }
    prefix_hook::write_absolute_jump(relay, reinterpret_cast<uint64_t>(detour));
    DWORD relay_protect{};
    VirtualProtect(relay, prefix_hook::kAbsoluteJumpBytes, PAGE_EXECUTE_READ,
        &relay_protect);
    FlushInstructionCache(GetCurrentProcess(), relay, prefix_hook::kAbsoluteJumpBytes);
    *original = reinterpret_cast<void*>(call_destination(call));
    std::memcpy(call + 1, &rel, sizeof(rel));
    VirtualProtect(call + 1, sizeof(rel), protect, &protect);
    FlushInstructionCache(GetCurrentProcess(), call, kCallBytes);
    return relay;
}

}  // namespace w3vr::call_site_hook
