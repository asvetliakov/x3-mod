#pragma once
#include <cstdint>
namespace x3m {
// The setter is native, bypassing the application's state shadow. A failed
// Set may have mutated the device, so it also incurs an explicit restoration.
// Callers preserve CPU/LastError around these foreign calls and latch state
// loss whenever restore fails. The original draw HRESULT is never replaced.
struct FogCardMask {
    bool masked = false;
    std::uint32_t saved = 0;
    std::int32_t operation = 0, restore = 0;
    template <class Set> void begin(std::uint32_t mask, Set&& set) noexcept {
        saved = mask;
        operation = set(0);
        masked = operation >= 0;
        if (!masked) restore = set(saved);
    }
    template <class Set> void end(Set&& set) noexcept {
        if (masked) {
            restore = set(saved);
            masked = false;
        }
    }
};
} // namespace x3m
