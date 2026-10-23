#include "seta_restore.h"
#include "capture.h"
#include "chase_transition.h"
#include "config.h"
#include "cpu_state.h"
#include "engine_memory.h"
#include "engine_patch.h"
#include "fog_prefill.h"
#include "object_trace.h"
#include <windows.h>
#include <atomic>
#include <cstring>

static_assert(sizeof(void*) == 4, "x86 code patching only");
namespace {
using namespace x3m::seta_restore::core;
namespace engine_patch = x3m::engine_patch;

// patched_: the hook is registered for shutdown() (also after a failed rollback); armed_: a complete install (hook and
// injection consumer), the only state in which the handlers act.
bool patched_ = false, attached_ = false;
std::atomic<bool> armed_{false};
const char* state_ = "disabled";
const char* inject_state_ = "unclaimed"; // the shared injection site's status at the attach
engine_patch::Site hook_site_{};
SRWLOCK lock_ = SRWLOCK_INIT;
State machine_; // under lock_
std::atomic<std::uint64_t> frame_{0};
std::atomic<std::uint32_t> epoch_{1}; // the EH adapter's unwinds (lock-free)
struct Guard {
    Guard() { AcquireSRWLockExclusive(&lock_); }
    ~Guard() { ReleaseSRWLockExclusive(&lock_); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

bool read(std::uintptr_t at, void* out, unsigned n) {
    return at && x3m::engine_memory::read(at, out, n);
}
template <class T> bool load(std::uintptr_t at, T& out) {
    return read(at, &out, unsigned(sizeof out));
}
Clock now() {
    Clock c;
    c.frame = frame_.load(std::memory_order_relaxed);
    c.ms = GetTickCount64();
    c.epoch = epoch_.load(std::memory_order_acquire);
    return c;
}
// Checked engine reads for the state machine (seta_restore_core.h, "What the injection site's probe answers").
struct EngineProbe {
    bool player(std::uint32_t& ship) {
        std::uint32_t m = 0;
        return load(manager_ptr_va, m) && m && !(m & 3) && load(m + manager_player, ship);
    }
    bool parent(std::uint32_t ship, std::uint32_t& sector) {
        return ship && !(ship & 3) && load(ship + object_parent, sector);
    }
    bool sector_id(std::uint32_t sector, std::uint32_t& id) {
        return sector && !(sector & 3) && load(sector + object_id, id);
    }
    bool paused() {
        std::uint32_t game = 0, bits = 0;
        if (!load(game_root_va, game) || !game || !load(game + game_pause, bits)) return true; // unreadable: defer
        return (bits & pause_bit) != 0;
    }
    // sector-transit-order.md section 5 (the fog hand-over's walk): the newest sector object, alive, with its scene,
    // and an id other than the departure sector's. At most once per presented frame while armed.
    bool ready(std::uint32_t departure_id, std::uint32_t& node) {
        x3m::engine_memory::revalidate(); // the stall reallocated: an epoch only (fog_prefill_poll does the same)
        auto r = [](std::uintptr_t p, void* out, std::size_t n) { return x3m::engine_memory::read(p, out, n); };
        const x3m::fog_prefill::Result result = x3m::fog_prefill::walk(r, departure_id);
        if (result.status != x3m::fog_prefill::Walk::Found) return false;
        node = result.node;
        return true;
    }
};
void publish() {
    x3m::chase_transition::inject_want(machine_.active());
}
// One row per transition, formatted outside every lock (x3m::log runs the CRT formatter behind call_preserved).
void emit(const Report& r) {
    switch (r.row) {
    case Row::none: return;
    case Row::armed:
        x3m::log("seta_restore armed frame=%llu old=0x%08lx new=0x%08lx ip=0x%05lx ret=0x%05lx ret_name=%s ship=0x%08lx sector=0x%08lx sector_id=%lu build=%lu",
                 r.frame, static_cast<unsigned long>(r.old_factor), static_cast<unsigned long>(r.new_factor),
                 static_cast<unsigned long>(r.ip), static_cast<unsigned long>(r.ret),
                 return_name(r.ret, r.scan == Scan::bottom), static_cast<unsigned long>(r.ship),
                 static_cast<unsigned long>(r.sector), static_cast<unsigned long>(r.sector_id),
                 static_cast<unsigned long>(r.build));
        return;
    case Row::cancelled:
        x3m::log("seta_restore cancelled reason=%s frame=%llu frames_since_arm=%llu ms_since_arm=%llu write_old=0x%08lx write_new=0x%08lx write_ip=0x%05lx write_ret=0x%05lx write_scan=%s write_ret_name=%s ship=0x%08lx sector_id=%lu ready_sector=0x%08lx skipped=%lu",
                 cancel_name(r.reason), r.frame, r.frames_since_arm, r.ms_since_arm,
                 static_cast<unsigned long>(r.old_factor), static_cast<unsigned long>(r.new_factor),
                 static_cast<unsigned long>(r.ip), static_cast<unsigned long>(r.ret), scan_name(r.scan),
                 r.reason == cancel_factor_write ? return_name(r.ret, r.scan == Scan::bottom) : "none",
                 static_cast<unsigned long>(r.ship), static_cast<unsigned long>(r.sector_id),
                 static_cast<unsigned long>(r.ready_sector), static_cast<unsigned long>(r.skipped));
        return;
    case Row::restored:
    case Row::restore_failed:
        x3m::log("seta_restore %s frame=%llu frames_after_transit=%llu frames_since_arm=%llu ms_since_arm=%llu result=%lu call=0x%08lx key=0x%lx method=%s ship=0x%08lx sector_id=%lu ready_sector=0x%08lx skipped=%lu",
                 r.row == Row::restored ? "restored" : "restore_failed", r.frame, r.frames_after_transit,
                 r.frames_since_arm, r.ms_since_arm, static_cast<unsigned long>(r.result), static_cast<unsigned long>(script_call_va),
                 static_cast<unsigned long>(call_key), call_method, static_cast<unsigned long>(r.ship),
                 static_cast<unsigned long>(r.sector_id), static_cast<unsigned long>(r.ready_sector),
                 static_cast<unsigned long>(r.skipped));
        return;
    case Row::refused:
        x3m::log("seta_restore refused reason=%s frame=%llu build=%lu expected_build=%lu old=0x%08lx ip=0x%05lx ret=0x%05lx sector_id=%lu",
                 refusal_name(r.reason), r.frame, static_cast<unsigned long>(r.build),
                 static_cast<unsigned long>(story_build), static_cast<unsigned long>(r.old_factor),
                 static_cast<unsigned long>(r.ip), static_cast<unsigned long>(r.ret),
                 static_cast<unsigned long>(r.sector_id));
        return;
    }
}

// ---- the injection consumer (chase_transition::InjectConsumer) ----
bool poll(std::uint32_t controlled, x3m::chase_transition::InjectCall& call) noexcept {
    if (!armed_.load(std::memory_order_acquire)) return false;
    Report r;
    bool wanted = false;
    {
        Guard guard;
        EngineProbe probe;
        wanted = machine_.on_inject(now(), controlled, probe, r);
        publish();
    }
    emit(r);
    if (!wanted) return false;
    call.target = script_call_va;
    call.key = call_key;
    call.name = call_method;
    call.return_kind = call_return_kind;
    call.argc = call_argc;
    call.value = 0;
    return true;
}
void result(unsigned al) noexcept {
    Report r;
    {
        Guard guard;
        r = machine_.on_result(al, now());
        publish();
    }
    emit(r);
}
void vm(unsigned kind) noexcept {
    if (!armed_.load(std::memory_order_acquire)) return;
    Report r;
    {
        Guard guard;
        r = machine_.on_vm(kind, now());
        publish();
    }
    emit(r);
}
void unwind() noexcept {
    epoch_.fetch_add(1, std::memory_order_acq_rel);
}
const x3m::chase_transition::InjectConsumer consumer_{&poll, &result, &vm, &unwind};
}

extern "C" {
volatile std::uint32_t x3m_seta_restore_ti_continue = 0; // the claim tail (the two displaced instructions + jmp back)

// At the TI_SetTimeWarpFactor case entry, on the VM's thread inside a native dispatch: checked reads and the state
// machine only, no VM call. The stack scan runs only for a write that can arm or cancel (SETA stops, writes while a
// ticket is active): rare.
__attribute__((force_align_arg_pointer)) void __cdecl x3m_seta_restore_factor_write(std::uint32_t args,
                                                                                    std::uint32_t task) {
    x3m::LightCallBoundary cpu; // MXCSR + LastError; no x87 below (the row formatter runs behind call_preserved)
    if (!armed_.load(std::memory_order_acquire)) return;
    Write w;
    std::uint32_t cfg = 0, vm_root = 0;
    unsigned char tag = 0;
    w.readable = load(cfg_ptr_va, cfg) && cfg && load(cfg + cfg_factor, w.old_factor) && read(args, &tag, 1) &&
                 load(args + 1, w.new_factor) && load(task + task_ip, w.ip);
    w.tag = tag;
    Report r;
    {
        Guard guard;
        const bool stop_shaped = w.readable && w.old_factor > factor_one && w.new_factor == factor_one;
        if (!machine_.disabled && (machine_.active() || stop_shaped)) {
            w.build_readable = load(vm_ptr_va, vm_root) && vm_root && load(vm_root + vm_build, w.build);
            w.ret = first_return(task, [](std::uintptr_t at, void* out, unsigned n) { return read(at, out, n); });
        }
        EngineProbe probe;
        r = machine_.on_write(w, now(), probe);
        publish();
    }
    emit(r);
}
}

// Integer-only stub at 0x004b148c (entered by the claim's dispatcher): saves EFLAGS, the eight general registers and
// XMM0..7 (the handler is SSE2 code), passes EBX (argument cells) and EBP (task), clears DF for the C ABI, restores
// everything and continues at the claim tail, which replays `mov eax,[0x00606f34]; mov edx,[ebx+1]` and jumps to
// 0x004b1494. ESP leaves exactly as it entered.
asm(R"(
    .intel_syntax noprefix
    .text
    .p2align 4
    .globl _x3m_seta_restore_ti_stub
_x3m_seta_restore_ti_stub:
    pushfd
    pushad
    sub esp, 0x80
    movdqu [esp+0x00], xmm0
    movdqu [esp+0x10], xmm1
    movdqu [esp+0x20], xmm2
    movdqu [esp+0x30], xmm3
    movdqu [esp+0x40], xmm4
    movdqu [esp+0x50], xmm5
    movdqu [esp+0x60], xmm6
    movdqu [esp+0x70], xmm7
    cld
    push ebp
    push ebx
    call _x3m_seta_restore_factor_write
    add esp, 8
    movdqu xmm0, [esp+0x00]
    movdqu xmm1, [esp+0x10]
    movdqu xmm2, [esp+0x20]
    movdqu xmm3, [esp+0x30]
    movdqu xmm4, [esp+0x40]
    movdqu xmm5, [esp+0x50]
    movdqu xmm6, [esp+0x60]
    movdqu xmm7, [esp+0x70]
    add esp, 0x80
    popad
    popfd
    jmp dword ptr [_x3m_seta_restore_ti_continue]
    .att_syntax
)");
extern "C" void x3m_seta_restore_ti_stub();

namespace {
std::uint32_t address_of(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
// Pins this DLL for the process lifetime (documented: GET_MODULE_HANDLE_EX_FLAG_PIN): the engine jumps into the stub.
bool pin_self() {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&patched_), &module) != FALSE &&
           module != nullptr;
}
// Every window the hook, its register contract and the call depend on (core::windows; verify_seta_restore_sites.py
// pins the same), each read whole through ReadProcessMemory; mismatch_ names the first one that differs.
Mismatch mismatch_{};
bool windows_match() {
    return compare_windows(
        [](std::uint32_t at, void* out, unsigned n) {
            return engine_patch::read_code(at, static_cast<unsigned char*>(out), n);
        },
        mismatch_);
}
// Lower-case hex of n <= shown_bytes bytes into out (2 * shown_bytes + 1 chars), "-" for none.
void hex(const unsigned char* bytes, unsigned n, char* out) {
    static const char digits[] = "0123456789abcdef";
    if (!n) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    for (unsigned i = 0; i < n; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * n] = 0;
}
const char* write_kind(const engine_patch::Site& s) {
    return s.patched_in ? (s.atomic_write ? "atomic" : "plain") : "none";
}
// Hook, then the shared injection site; a later failure puts the hook back. A hook that cannot be restored keeps the
// module registered (patched_) so shutdown() tries again; armed_ stays false (the stub then only forwards).
bool install(const char** reason) {
    mismatch_ = core::Mismatch{}; // a retried initialize() must not print a stale window on a non-byte refusal
    if (!x3m::object_trace::executable_verified()) {
        *reason = "executable_mismatch";
        return false;
    }
    if (!engine_patch::install_window_open()) {
        *reason = "late_claim";
        return false;
    }
    if (!windows_match()) {
        *reason = "bytes_mismatch";
        return false;
    }
    if (!pin_self()) {
        *reason = "pin_failed";
        return false;
    }
    engine_patch::SiteSpec spec{};
    spec.name = "seta_restore_factor";
    spec.address = hook_va;
    spec.length = hook_length;
    spec.ret_pop = 0;
    spec.rel32_offset = 0;
    std::memcpy(spec.expected, hook_bytes, hook_length);
    hook_site_ = engine_patch::Site{};
    if (!engine_patch::claim(hook_site_, spec)) {
        *reason = hook_site_.status;
        if (hook_site_.patched_in) patched_ = true; // rollback_failed: still registered
        return false;
    }
    x3m_seta_restore_ti_continue = address_of(*hook_site_.entry);
    bool ok = engine_patch::push_front(hook_site_, reinterpret_cast<void*>(&x3m_seta_restore_ti_stub)) != nullptr;
    if (!ok) *reason = "chain_failed";
    if (ok) {
        ok = x3m::chase_transition::inject_attach(&consumer_, &inject_state_);
        if (!ok) *reason = inject_state_;
        attached_ = ok;
    }
    if (!ok) {
        if (!engine_patch::restore(hook_site_)) {
            patched_ = true;
            *reason = "rollback_failed";
        }
        return false;
    }
    patched_ = true;
    *reason = "ok";
    return true;
}
}

namespace x3m::seta_restore {
bool initialize() {
    const DWORD error = GetLastError();
    if (patched_ || attached_) {
        SetLastError(error);
        return armed_.load(std::memory_order_acquire);
    }
    wchar_t setting[4]{};
    const DWORD length = x3m::config::get(L"X3M_SETA_RESTORE", setting, 4);
    if (!(length == 1 && setting[0] == L'1')) {
        state_ = "disabled";
        if (length) log("seta_restore installed=0 requested=0 reason=disabled");
        SetLastError(error);
        return false;
    }
    {
        Guard guard;
        machine_ = State{};
    }
    const bool ok = install(&state_);
    armed_.store(ok, std::memory_order_release);
    if (ok)
        log("seta_restore installed=1 hook=0x%08lx write=%s stub=0x%08lx inject_site=0x%08lx inject=%s call=0x%08lx key=0x%lx method=%s story_build=%lu stop_ip=0x%05lx gate_return=0x%05lx due_frames=%llu expiry_ms=%llu",
            static_cast<unsigned long>(hook_va), write_kind(hook_site_),
            static_cast<unsigned long>(address_of(reinterpret_cast<const void*>(&x3m_seta_restore_ti_stub))),
            static_cast<unsigned long>(inject_site_va), inject_state_, static_cast<unsigned long>(script_call_va),
            static_cast<unsigned long>(call_key), call_method, static_cast<unsigned long>(story_build),
            static_cast<unsigned long>(stop_ip), static_cast<unsigned long>(gate_return),
            static_cast<unsigned long long>(due_frames), static_cast<unsigned long long>(expiry_ms));
    else {
        const Window* w = mismatch_.window;
        char expected[2 * shown_bytes + 1], got[2 * shown_bytes + 1];
        hex(w ? w->bytes : nullptr, w ? mismatch_.shown : 0, expected);
        if (w && !mismatch_.readable)
            std::memcpy(got, "unreadable", sizeof "unreadable");
        else
            hex(mismatch_.got, w ? mismatch_.shown : 0, got);
        log("seta_restore refused reason=%s window=0x%08lx window_name=%s expected=%s got=%s hook=0x%08lx hook_status=%s inject=%s patched=%u",
            state_, static_cast<unsigned long>(w ? w->va : 0), w ? w->name : "-", expected, got,
            static_cast<unsigned long>(hook_va), hook_site_.status, inject_state_, unsigned(patched_));
    }
    SetLastError(error);
    return ok;
}
bool shutdown() {
    const DWORD error = GetLastError();
    armed_.store(false, std::memory_order_release);
    bool ok = true;
    if (attached_) {
        ok = x3m::chase_transition::inject_detach() && ok;
        attached_ = false;
    }
    if (patched_) {
        const bool back = engine_patch::restore(hook_site_);
        if (back) patched_ = false;
        ok = back && ok;
    }
    SetLastError(error);
    return ok;
}
const char* state() {
    return state_;
}
void present(unsigned long long frame) {
    if (armed_.load(std::memory_order_relaxed)) frame_.store(frame, std::memory_order_relaxed);
}
}
