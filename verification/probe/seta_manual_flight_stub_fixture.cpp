// Native execution of the five SETA-manual-flight stubs (src/proxy/seta_manual_flight_stubs.h) against a fake engine
// image: i686 MinGW, the DLL's code flags, run on Windows (test_seta_manual_flight.py runs it through WSL interop; no
// game, no Wine, no device). The engine's globals sit at their real addresses (cfg pointer 0x00606f34, mouse types
// 0x00608e40/44, records 0x00608e50/78/a0) inside the fixture's own image: a 2.25 MiB .bss array of an image fixed at
// 0x00400000 (-Wl,--disable-dynamicbase) spans 0x00600000..0x0060a000, so the loader maps them before any heap. A driver loads the
// registers and an x87 stack (sentinels below the live values), jumps into a stub, and a landing records every
// general register, EFLAGS, ESP and the whole x87 stack with its depth. The continuation is a tail of the engine's own
// displaced bytes (as engine_patch emits it), so the original path is executed for real, not modelled.
// Prints one line per failed check and "checks=N failures=M"; exit 0 only when every check holds.
#include "seta_manual_flight_stubs.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace x3m::seta_manual_flight;
// One .bss array from just above the fixture's code to past 0x0060a000: the engine globals the stubs and the helper
// read (cfg pointer, mouse types, records) fall inside it at their real addresses. PE sections must be contiguous, so a
// section of its own at 0x00600000 would leave a gap the loader refuses.
unsigned char x3eng[0x240000];
constexpr std::uintptr_t engine_lo = 0x00600000, engine_hi = 0x0060a000;

extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_seta_manual_flight_axis(unsigned axis,
                                                                                            std::uint32_t record,
                                                                                            std::uint32_t) {
    core::Inputs in;
    core::Out out;
    publish(axis, record, in, out);
}

extern "C" {
struct Regs {
    std::uint32_t eax, ecx, edx, ebx, esi, edi, ebp, esp, eflags;
};
Regs fx_in{}, fx_out{};
double fx_x87_in[3]{};   // pushed in reverse: fx_x87_in[0] ends as st0
std::uint32_t fx_x87_in_depth = 0;
double fx_x87_out[8]{};  // st0.. as stored by the landing
std::uint32_t fx_status_before = 0, fx_status_after = 0, fx_status_empty = 0, fx_set_df = 0;
std::uint32_t fx_saved_esp = 0, fx_entry_esp = 0, fx_target = 0;
void fx_run();
void fx_landing();
void fx_tail_rot_edi();
void fx_tail_rot_esi();
void fx_tail_strafe();
}
// fx_run: saves the callee-saved registers, fills the x87 stack (finit; depth values, last = st0), records the status
// word, loads the eight general registers from fx_in (ESP is the driver's), optionally sets DF, jumps to fx_target.
// fx_landing: records the registers, EFLAGS (DF included), ESP, the status word, pops up to eight x87 values while the
// stack is not empty, restores the driver's frame and returns to fx_run's caller.
asm(R"(
    .intel_syntax noprefix
    .text
    .globl _fx_run
_fx_run:
    push ebx
    push esi
    push edi
    push ebp
    finit
    mov ecx, dword ptr [_fx_x87_in_depth]
    test ecx, ecx
    jz 2f
1:
    fld qword ptr [_fx_x87_in + ecx*8 - 8]
    dec ecx
    jnz 1b
2:
    fnstsw ax
    mov dword ptr [_fx_status_before], eax
    mov dword ptr [_fx_saved_esp], esp
    mov dword ptr [_fx_entry_esp], esp
    mov eax, dword ptr [_fx_in + 0]
    mov ecx, dword ptr [_fx_in + 4]
    mov edx, dword ptr [_fx_in + 8]
    mov ebx, dword ptr [_fx_in + 12]
    mov esi, dword ptr [_fx_in + 16]
    mov edi, dword ptr [_fx_in + 20]
    mov ebp, dword ptr [_fx_in + 24]
    cmp dword ptr [_fx_set_df], 0
    je 3f
    std
3:
    jmp dword ptr [_fx_target]

    .globl _fx_landing
_fx_landing:
    mov dword ptr [_fx_out + 0], eax
    mov dword ptr [_fx_out + 4], ecx
    mov dword ptr [_fx_out + 8], edx
    mov dword ptr [_fx_out + 12], ebx
    mov dword ptr [_fx_out + 16], esi
    mov dword ptr [_fx_out + 20], edi
    mov dword ptr [_fx_out + 24], ebp
    mov dword ptr [_fx_out + 28], esp
    pushfd
    pop eax
    mov dword ptr [_fx_out + 32], eax
    cld
    fnstsw ax
    mov dword ptr [_fx_status_after], eax
    xor ecx, ecx
4:
    fxam
    fnstsw ax
    and ax, 0x4500
    cmp ax, 0x4100
    je 5f
    fstp qword ptr [_fx_x87_out + ecx*8]
    inc ecx
    cmp ecx, 8
    jb 4b
5:
    fnstsw ax
    mov dword ptr [_fx_status_empty], eax
    mov esp, dword ptr [_fx_saved_esp]
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

    .globl _fx_tail_rot_edi
_fx_tail_rot_edi:
    .byte 0xa1, 0x34, 0x6f, 0x60, 0x00, 0xda, 0x88, 0x14, 0x07, 0x00, 0x00, 0x8b, 0x4f, 0x24
    jmp _fx_landing
    .globl _fx_tail_rot_esi
_fx_tail_rot_esi:
    .byte 0xa1, 0x34, 0x6f, 0x60, 0x00, 0xda, 0x88, 0x14, 0x07, 0x00, 0x00, 0x8b, 0x4e, 0x24
    jmp _fx_landing
    .globl _fx_tail_strafe
_fx_tail_strafe:
    .byte 0xdb, 0x86, 0x14, 0x07, 0x00, 0x00
    jmp _fx_landing
    .att_syntax
)");

namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* what, double got = 0, double want = 0) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s got=%.17g want=%.17g\n", what, got, want);
    }
}
bool close_to(double a, double b) {
    return std::fabs(a - b) <= 1e-12 * (std::fabs(b) > 1 ? std::fabs(b) : 1);
}
unsigned char* cfg = nullptr;
void set_engine(std::int32_t factor, std::int32_t governor, std::int32_t game_ms, std::uint32_t mouse_x,
                std::uint32_t mouse_y) {
    std::memcpy(cfg + core::cfg_factor, &factor, 4);
    std::memcpy(cfg + core::cfg_governor, &governor, 4);
    std::memcpy(cfg + core::cfg_game_ms, &game_ms, 4);
    std::memcpy(reinterpret_cast<void*>(core::mouse_type_x_va), &mouse_x, 4);
    std::memcpy(reinterpret_cast<void*>(core::mouse_type_y_va), &mouse_y, 4);
}
void set_target(unsigned axis, std::int32_t target) {
    std::memcpy(reinterpret_cast<void*>(core::sites[axis].record + core::record_target), &target, 4);
}
void* stub_of(unsigned axis) {
    switch (axis) {
    case core::yaw: return reinterpret_cast<void*>(&x3m_smf_yaw_stub);
    case core::pitch: return reinterpret_cast<void*>(&x3m_smf_pitch_stub);
    case core::roll: return reinterpret_cast<void*>(&x3m_smf_roll_stub);
    case core::strafe_y: return reinterpret_cast<void*>(&x3m_smf_strafe_y_stub);
    default: return reinterpret_cast<void*>(&x3m_smf_strafe_x_stub);
    }
}
unsigned top(std::uint32_t status) {
    return (status >> 11) & 7;
}
// One stub run. Rotation: st0 = accel over two sentinels; record/ship in EDI/ESI (roll swapped). Strafe: the two
// strafe constants 0.001 and 20000 as the engine leaves them, ESI = cfg. Every other register carries a marker.
struct Run {
    double st[8];
    unsigned depth;
    Regs out;
};
Run run(unsigned axis, double accel, std::uint32_t record_override = 0, bool df = false) {
    const bool rotation = axis < core::strafe_y;
    fx_in = Regs{0xa5a50001, 0xa5a50002, 0xa5a50003, 0xa5a50004, 0, 0, 0xa5a50007, 0, 0};
    const std::uint32_t ship = 0x00609400;
    const std::uint32_t record = record_override ? record_override : core::sites[axis].record;
    if (!rotation) {
        fx_in.esi = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
        fx_in.edi = 0xa5a50006;
        fx_x87_in[0] = 0.001;
        fx_x87_in[1] = 20000.0;
        fx_x87_in[2] = -7.25; // a caller's value below
        fx_x87_in_depth = 3;
    } else {
        fx_in.edi = axis == core::roll ? ship : record;
        fx_in.esi = axis == core::roll ? record : ship;
        fx_x87_in[0] = accel;
        fx_x87_in[1] = 123.5;
        fx_x87_in[2] = -7.25;
        fx_x87_in_depth = 3;
    }
    fx_set_df = df ? 1 : 0;
    fx_target = std::uint32_t(reinterpret_cast<std::uintptr_t>(stub_of(axis)));
    std::memset(fx_x87_out, 0, sizeof fx_x87_out);
    fx_run();
    Run r{};
    std::memcpy(r.st, fx_x87_out, sizeof r.st);
    r.depth = (8 - top(fx_status_after)) & 7; // 1..7 live values (TOP = 8 - depth)
    r.out = fx_out;
    return r;
}
// The registers the stub must leave as they entered on both paths (EAX/ECX are the span's outputs).
void check_preserved(const Run& r, unsigned axis, bool scaled, const char* what) {
    char label[160];
    const bool rotation = axis < core::strafe_y;
    std::snprintf(label, sizeof label, "%s: EBX/EBP/ESI/EDI/EDX preserved", what);
    check(r.out.ebx == fx_in.ebx && r.out.ebp == fx_in.ebp && r.out.esi == fx_in.esi && r.out.edi == fx_in.edi &&
              r.out.edx == fx_in.edx,
          label);
    std::snprintf(label, sizeof label, "%s: ESP unchanged", what);
    check(r.out.esp == fx_entry_esp, label, r.out.esp, fx_entry_esp);
    std::snprintf(label, sizeof label, "%s: DF as on entry", what);
    check(((r.out.eflags >> 10) & 1) == fx_set_df, label, (r.out.eflags >> 10) & 1, fx_set_df);
    std::snprintf(label, sizeof label, "%s: x87 depth", what);
    check(r.depth == (rotation ? 3u : 4u), label, r.depth, rotation ? 3 : 4);
    std::snprintf(label, sizeof label, "%s: sentinels below untouched", what);
    if (rotation)
        check(r.st[1] == 123.5 && r.st[2] == -7.25, label, r.st[1], 123.5);
    else
        check(r.st[1] == 0.001 && r.st[2] == 20000.0 && r.st[3] == -7.25, label, r.st[1], 0.001);
    std::snprintf(label, sizeof label, "%s: EAX", what);
    if (rotation)
        check(r.out.eax == std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg)), label, r.out.eax, 0); // = cfg, both paths
    else
        check(r.out.eax == fx_in.eax, label, r.out.eax, fx_in.eax);
    std::snprintf(label, sizeof label, "%s: ECX", what);
    if (!rotation) check(r.out.ecx == fx_in.ecx, label, r.out.ecx, fx_in.ecx);
    (void)scaled;
}
}

int main() {
    if (reinterpret_cast<std::uintptr_t>(x3eng) > engine_lo || reinterpret_cast<std::uintptr_t>(x3eng) + sizeof x3eng < engine_hi) {
        std::printf("fixture layout_failed x3eng=%p\n", static_cast<void*>(x3eng));
        return 2;
    }
    static unsigned char cfg_block[0x800];
    cfg = cfg_block;
    const std::uint32_t cfg_va = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
    std::memcpy(reinterpret_cast<void*>(core::cfg_ptr_va), &cfg_va, 4);
    // The tails: the engine's displaced bytes, then the landing; the span ends land there too.
    x3m_smf_continue[core::yaw] = std::uint32_t(reinterpret_cast<std::uintptr_t>(&fx_tail_rot_edi));
    x3m_smf_continue[core::pitch] = std::uint32_t(reinterpret_cast<std::uintptr_t>(&fx_tail_rot_edi));
    x3m_smf_continue[core::roll] = std::uint32_t(reinterpret_cast<std::uintptr_t>(&fx_tail_rot_esi));
    x3m_smf_continue[core::strafe_y] = std::uint32_t(reinterpret_cast<std::uintptr_t>(&fx_tail_strafe));
    x3m_smf_continue[core::strafe_x] = std::uint32_t(reinterpret_cast<std::uintptr_t>(&fx_tail_strafe));
    for (unsigned a = 0; a < core::axis_count; ++a)
        x3m_smf_resume[a] = std::uint32_t(reinterpret_cast<std::uintptr_t>(&fx_landing));

    const double accel = 1.4249267578125; // (yaw + pitch + roll max) * 0x7999 / 65536 for one turn/s each
    const std::int32_t game_ms = 167, target = 0x10000; // one turn per game second
    struct Case {
        const char* name;
        std::int32_t factor, governor;
        double k; // expected R / 65536, 0 = original path
    };
    const Case cases[] = {{"k=1 (factor 1.0)", 0x10000, 0x10000, 0},
                          {"k=1 (factor 1.0, governor 0.5)", 0x10000, 0x8000, 0},
                          {"k=1 (factor 2.0, governor 0.5): original", 0x20000, 0x8000, 0},
                          {"k=0.6 (factor 1.5, governor 0.4): original", 0x18000, 0x6666, 0},
                          {"k=1.5 (factor 1.5, governor 1.0)", 0x18000, 0x10000, 1.5},
                          {"k=2.5", 0x28000, 0x10000, 2.5},
                          {"k=10", 0xa0000, 0x10000, 10.0},
                          {"k=5 (factor 10, governor 0.5)", 0xa0000, 0x8000, 5.0}};
    char what[192];
    for (const Case& c : cases) {
        for (unsigned axis = 0; axis < core::axis_count; ++axis) {
            set_engine(c.factor, c.governor, game_ms, 0xb, 0xc); // the default mouse mapping (InputMouseDelta)
            if (axis < core::strafe_y) set_target(axis, axis == core::roll ? -target / 3 : target);
            for (int df = 0; df < 2; ++df) {
                const Run r = run(axis, accel, 0, df != 0);
                std::snprintf(what, sizeof what, "%s %s df=%d", c.name, core::axis_names[axis], df);
                check_preserved(r, axis, c.k != 0, what);
                char label[224];
                if (axis < core::strafe_y) {
                    const std::int32_t t = axis == core::roll ? -target / 3 : target;
                    const double want_st0 = c.k ? accel * game_ms / (c.k * c.k) : accel * game_ms;
                    const std::int32_t want_ecx = c.k ? core::round_scaled(t, 1.0 / c.k) : t;
                    std::snprintf(label, sizeof label, "%s: st0 = accel * game_ms / k^2", what);
                    check(c.k ? close_to(r.st[0], want_st0) : r.st[0] == want_st0, label, r.st[0], want_st0);
                    std::snprintf(label, sizeof label, "%s: ECX = round(target / k)", what);
                    check(std::int32_t(r.out.ecx) == want_ecx, label, std::int32_t(r.out.ecx), want_ecx);
                } else {
                    const double want = c.k ? game_ms / c.k : game_ms;
                    std::snprintf(label, sizeof label, "%s: st0 = game_ms / k", what);
                    check(c.k ? close_to(r.st[0], want) : r.st[0] == want, label, r.st[0], want);
                }
            }
        }
    }
    // Mouse-delta modes (types 1/2/3 on either ship-view axis) keep their axis original at k = 10; the others scale.
    for (std::uint32_t type = 1; type <= 5; ++type) {
        for (int on_y = 0; on_y < 2; ++on_y) {
            for (unsigned axis = 0; axis < core::strafe_y; ++axis) {
                set_engine(0xa0000, 0x10000, game_ms, on_y ? 0xb : type, on_y ? type : 0xc);
                set_target(axis, target);
                const Run r = run(axis, accel);
                const bool original = type == core::mouse_delta_type[axis];
                std::snprintf(what, sizeof what, "mouse %s type %u: %s %s", on_y ? "y" : "x", type,
                              core::axis_names[axis], original ? "original" : "scaled");
                check_preserved(r, axis, !original, what);
                const double want = original ? accel * game_ms : accel * game_ms / 100.0;
                check(original ? r.st[0] == want : close_to(r.st[0], want), what, r.st[0], want);
                check(std::int32_t(r.out.ecx) == (original ? target : core::round_scaled(target, 0.1)), what,
                      std::int32_t(r.out.ecx), original ? target : target / 10.0);
            }
        }
    }
    // The view-flag pair 0x00608e48/4c: a delta type there keeps its axis original too (conservative skip).
    for (unsigned word = 0; word < 2; ++word) {
        set_engine(0xa0000, 0x10000, game_ms, 0xb, 0xc);
        const std::uint32_t yaw_type = 1, other = 0xf;
        std::memcpy(reinterpret_cast<void*>(word ? core::mouse_turret_y_va : core::mouse_turret_x_va), &yaw_type, 4);
        std::memcpy(reinterpret_cast<void*>(word ? core::mouse_turret_x_va : core::mouse_turret_y_va), &other, 4);
        set_target(core::yaw, target);
        set_target(core::pitch, target);
        const Run y = run(core::yaw, accel);
        check_preserved(y, core::yaw, false, "view-flag pair type 1: yaw"); // before the next run replaces fx_in
        const Run pt = run(core::pitch, accel);
        check_preserved(pt, core::pitch, true, "view-flag pair type 1: pitch");
        check(y.st[0] == accel * game_ms && std::int32_t(y.out.ecx) == target, "view-flag pair type 1: yaw original", y.st[0],
              accel * game_ms);
        check(close_to(pt.st[0], accel * game_ms / 100.0) && std::int32_t(pt.out.ecx) == core::round_scaled(target, 0.1),
              "view-flag pair type 1: pitch scaled", pt.st[0], accel * game_ms / 100.0);
        const std::uint32_t none = 0;
        std::memcpy(reinterpret_cast<void*>(core::mouse_turret_x_va), &none, 4);
        std::memcpy(reinterpret_cast<void*>(core::mouse_turret_y_va), &none, 4);
    }
    // A record register that is not the axis's record: the original path (contract), registers preserved.
    set_engine(0xa0000, 0x10000, game_ms, 0xb, 0xc);
    {
        std::int32_t t = 4321;
        std::memcpy(reinterpret_cast<void*>(0x00608000 + core::record_target), &t, 4);
        const Run r = run(core::yaw, accel, 0x00608000);
        check_preserved(r, core::yaw, false, "contract");
        check(r.st[0] == accel * game_ms && r.out.ecx == 4321, "contract: original path", r.st[0], accel * game_ms);
    }
    // Rounding half away from zero (k = 2: 5 -> 3, -5 -> -3, 4 -> 2).
    set_engine(0x20000, 0x10000, game_ms, 0xb, 0xc);
    for (std::int32_t t : {5, -5, 4, -4, 0x7fffffff, -0x7fffffff}) {
        set_target(core::yaw, t);
        const Run r = run(core::yaw, accel);
        const std::int32_t want = core::round_scaled(t, 0.5);
        std::snprintf(what, sizeof what, "rounding target=%ld", static_cast<long>(t));
        check(std::int32_t(r.out.ecx) == want, what, std::int32_t(r.out.ecx), want);
    }
    // Cost: the scaled yaw path including the driver and landing, k = 10.
    set_engine(0xa0000, 0x10000, game_ms, 0xb, 0xc);
    set_target(core::yaw, target);
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    const unsigned n = 200000;
    for (unsigned i = 0; i < n; ++i) run(core::yaw, accel);
    QueryPerformanceCounter(&t1);
    std::printf("cost ns_per_stub_run=%.1f runs=%u (driver, finit, landing included)\n",
                double(t1.QuadPart - t0.QuadPart) * 1e9 / double(f.QuadPart) / n, n);
    std::printf("checks=%u failures=%u %s\n", checks, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
