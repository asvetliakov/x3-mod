#pragma once
#include <windows.h>
#include <cstdint>

namespace x3m {
// Preserve computational caller state around injected work. x87 instructions
// transport the legacy caller's live stack/control/status; our arithmetic still
// uses SSE2. Preview does not reliably restore live x87 tags with FXRSTOR, so use
// the qualified FNSAVE/FRSTOR path. Volatile XMM registers retain the ordinary ABI.
struct CpuState {
    alignas(16) unsigned char x87[108]{};
    std::uint32_t mxcsr=0;
    DWORD error=0;
    void capture() noexcept {
        error=GetLastError();
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1"
                     : "=m"(x87),"=m"(mxcsr) :: "memory");
    }
    void restore() const noexcept {
        asm volatile("frstor %0\n\tldmxcsr %1" :: "m"(x87),"m"(mxcsr) : "memory");
        SetLastError(error);
    }
};
class PreserveCpuState {
public:
    PreserveCpuState() noexcept { saved_.capture(); }
    ~PreserveCpuState(){saved_.restore();}
    PreserveCpuState(const PreserveCpuState&)=delete;
    PreserveCpuState& operator=(const PreserveCpuState&)=delete;
private:
    CpuState saved_;
};
// Pre-call instrumentation must not alter the native call's input state, and
// post-call instrumentation must not alter its output state. Declare first so
// restoration happens after timing, lock and COM-resource destructors.
class CpuCallBoundary {
public:
    CpuCallBoundary() noexcept { incoming_.capture();outgoing_=incoming_; }
    ~CpuCallBoundary(){outgoing_.restore();}
    void before_original() const noexcept {incoming_.restore();}
    void after_original() noexcept {outgoing_.capture();}
    CpuCallBoundary(const CpuCallBoundary&)=delete;
    CpuCallBoundary& operator=(const CpuCallBoundary&)=delete;
private:
    CpuState incoming_,outgoing_;
};
} // namespace x3m
