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
// Same contract as CpuCallBoundary for hooks whose own code executes no x87
// instruction: the legacy caller's x87 stack/control/status are then untouched
// by construction, so only MXCSR and the thread's last error are saved and
// restored. FNSAVE/FRSTOR dominate CpuCallBoundary's cost (four of them per
// hook), which matters for setters the game calls thousands of times per frame.
// Precondition for using it: the hook body and every non-native function it
// reaches are built with -mfpmath=sse, return no float/double by value (the
// i386 ABI returns those in st(0)) and contain no x87 opcode; in particular no
// logging, since the printf formatter is x87 code. The shader, constant and
// viewport setter hooks of src/proxy/capture.cpp meet this;
// verification/probe/check_no_x87.py checks their disassembly in the built
// DLL (docs/verification/motion-output.md).
class LightCallBoundary {
public:
    LightCallBoundary() noexcept { capture(incoming_); outgoing_=incoming_; }
    ~LightCallBoundary(){ restore(outgoing_); }
    void before_original() const noexcept { restore(incoming_); }
    void after_original() noexcept { capture(outgoing_); }
    LightCallBoundary(const LightCallBoundary&)=delete;
    LightCallBoundary& operator=(const LightCallBoundary&)=delete;
private:
    struct State { std::uint32_t mxcsr=0; DWORD error=0; };
    static void capture(State& s) noexcept { s.error=GetLastError(); asm volatile("stmxcsr %0" : "=m"(s.mxcsr) :: "memory"); }
    static void restore(const State& s) noexcept { asm volatile("ldmxcsr %0" :: "m"(s.mxcsr) : "memory"); SetLastError(s.error); }
    State incoming_,outgoing_;
};
} // namespace x3m
