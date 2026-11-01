// FL edition of x3m::object_trace (src/proxy/object_trace.h): the per-draw engine scope of X3FL.exe's
// material submission (docs/architecture/farnhams-legacy-object-identity.md, "Step 3a: scope hook";
// docs/reverse-engineering/fl/material-submit.md). One five-byte `call rel32` at module_base() + site RVA,
// the target's only direct caller, is redirected through engine_patch::claim_call (install window, one
// lock cmpxchg8b, rollback) to the naked dispatcher below, which publishes the scope in TLS for the
// duration of the original call; current() reads the node/camera fields lazily inside the scope, as the
// AP file does (the engine dereferences the same node for the whole call). Every address is
// fl::module_base() + RVA from the generated object_trace_fl_inc.h; executable_verified() stays the
// constant false object_trace.h declares for FL, so no AP engine path is enabled by this file.
//
// ABI at the site (material-submit.md section 1): ECX = material descriptor, EDX = render node, then
// on the stack camera, flags, light1, light2; the target ends in a plain RET and the caller removes the
// four words (`add esp,0x10` right after the call, part of the gated bytes). The dispatcher therefore
// forwards ECX/EDX and the four words, removes its own copy, keeps EAX/ECX/EDX of the return and returns
// with a plain RET. The incoming arithmetic flags are dead (the target's third instruction is the gated
// `sub esp,8`), and so are the returned ones (the gated `add esp,0x10`). This file is built without
// SSE/MMX and executes no x87 code on the dispatch path, so the caller's XMM and x87 state reach the
// target and return from it untouched. The rare diagnostic paths that call into Windows (which may change
// volatile XMM state) hold an XmmSave; the periodic fl.trace_sample row also runs log()
// under x3m::call_preserved (x87, MXCSR, LastError).
#include "../../proxy/object_trace.h"
#include "../../proxy/config.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/executable_identity.h"
#include "executable_identity_fl.h"
#include <excpt.h>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace x3m {
void log(const char* format, ...);
}
static_assert(sizeof(void*) == 4, "x86 callsite only");
namespace {
namespace layout {
#include "object_trace_fl_inc.h"
}
static_assert(sizeof layout::site_bytes == 8 && layout::site_bytes[0] == 0xe8 && layout::site_bytes[5] == 0x83 &&
                  layout::site_bytes[6] == 0xc4 && layout::site_bytes[7] == 0x10,
              "call rel32; add esp,0x10");
static_assert(layout::node_model == 0x144 && layout::node_lod == 0x150, "FL node layout, not AP's +0x140/+0x14c");
static_assert(layout::node_lod > layout::node_model && layout::node_lod - layout::node_model == 12,
              "model and LOD in one 16-byte read");
constexpr std::uint32_t node_span = layout::node_lod + 4; // 0x154: the highest field the snapshot copies
// One scope per dispatcher frame; the first two words are a real x86 SEH registration record.
struct Scope {
    void* previous_seh;
    void* handler;
    Scope* parent;
    uintptr_t args[6]; // descriptor (ECX), node (EDX), camera, flags, light1, light2
    uint32_t depth;
};
static_assert(sizeof(Scope) == 40 && offsetof(Scope, args) == 12 && offsetof(Scope, depth) == 36,
              "the dispatcher's frame offsets");
DWORD tls_slot = TLS_OUT_OF_INDEXES;
uintptr_t engine_slot = 0; // module_base() + engine_slot_rva: the engine pointer, registry at +0xc
uintptr_t install_base = 0, install_site = 0, install_target = 0;
x3m::engine_patch::CallSite site_;
std::atomic<bool> observation{false};
std::atomic<const char*> state{"disabled"};
uint64_t session = 0;
#ifdef X3M_OBJECT_TRACE_FIXTURE
bool fail_next_tls = false;
#endif

bool read_memory(uintptr_t address, void* out, size_t size) {
    return x3m::engine_memory::read(address, out, size);
}
bool set_top(void* value) {
#ifdef X3M_OBJECT_TRACE_FIXTURE
    if (fail_next_tls) {
        fail_next_tls = false;
        return false;
    }
#endif
    return TlsSetValue(tls_slot, value) != FALSE;
}
Scope* top() {
    return tls_slot == TLS_OUT_OF_INDEXES ? nullptr : static_cast<Scope*>(TlsGetValue(tls_slot));
}
// XMM0-7 saved for the lifetime of the object. The common dispatch path makes no system call and touches no
// XMM register; the rare paths that may enter the kernel (an engine_memory VirtualQuery on a first sighting
// or a row, QueryPerformanceCounter, log()) run inside one of these: a Win32 call may change volatile XMM state
// (the fixture's XMM_AFTER_VIRTUALQUERY row: natively XMM0-5 read zero after one VirtualQuery).
struct XmmSave {
    alignas(16) unsigned char xmm[128];
    XmmSave() {
        asm volatile("movups %%xmm0,(%0)\n\tmovups %%xmm1,16(%0)\n\tmovups %%xmm2,32(%0)\n\tmovups %%xmm3,48(%0)\n\t"
                     "movups %%xmm4,64(%0)\n\tmovups %%xmm5,80(%0)\n\tmovups %%xmm6,96(%0)\n\tmovups %%xmm7,112(%0)" ::"r"(
                         xmm)
                     : "memory");
    }
    ~XmmSave() {
        asm volatile("movups (%0),%%xmm0\n\tmovups 16(%0),%%xmm1\n\tmovups 32(%0),%%xmm2\n\tmovups 48(%0),%%xmm3\n\t"
                     "movups 64(%0),%%xmm4\n\tmovups 80(%0),%%xmm5\n\tmovups 96(%0),%%xmm6\n\tmovups 112(%0),%%xmm7" ::"r"(
                         xmm)
                     : "memory");
    }
    XmmSave(const XmmSave&) = delete;
    XmmSave& operator=(const XmmSave&) = delete;
};
// log() from inside the engine's call: XMM0-7 (XmmSave), x87, MXCSR and LastError (call_preserved). Paid once
// per row, never per draw.
template <class F> void preserved(F&& f) {
    XmmSave xmm;
    x3m::call_preserved(f);
}
uintptr_t registry_now() {
    uintptr_t engine = 0, registry = 0;
    if (engine_slot && read_memory(engine_slot, &engine, 4) && engine &&
        read_memory(engine + layout::registry_offset, &registry, 4))
        return registry;
    return 0;
}
void log_status() {
    x3m::log("fl.trace status=%s base=0x%08lx site=0x%08lx target=0x%08lx engine_slot=0x%08lx registry=0x%08lx "
             "write=%s",
             state.load(), static_cast<unsigned long>(install_base), static_cast<unsigned long>(install_site),
             static_cast<unsigned long>(install_target), static_cast<unsigned long>(engine_slot),
             static_cast<unsigned long>(registry_now()),
             site_.patched_in ? (site_.atomic_write ? "atomic" : "plain") : "none");
}

// ---- diagnostics: one fl.trace_sample row per period, written by the first entering thread ----------------
// Distinct pointers of one period: open addressing over 512 slots, at most 256 entries, then saturated (the
// row prints 256+ and no further first sightings are read). No allocation; cleared per row.
struct PointerSet {
    static constexpr unsigned slots = 512, cap = 256;
    uintptr_t slot[slots]{};
    unsigned count = 0;
    bool saturated = false;
    void clear() {
        std::memset(slot, 0, sizeof slot);
        count = 0;
        saturated = false;
    }
    // true on the period's first sighting of p (nonzero)
    bool insert(uintptr_t p) {
        if (saturated) return false;
        unsigned i = (static_cast<uint32_t>(p) * 2654435761u) >> 23;
        while (slot[i]) {
            if (slot[i] == p) return false;
            i = (i + 1) & (slots - 1);
        }
        if (count >= cap) {
            saturated = true;
            return false;
        }
        slot[i] = p;
        ++count;
        return true;
    }
};
struct Diagnostic {
    long long period = 0, due = 0; // QPC ticks; period 0: off
    std::atomic<DWORD> thread{0};  // the first entering thread; entries on any other are only counted
    bool force = false;
    unsigned long rows = 0, entries = 0, depth_max = 0, null_node = 0, null_camera = 0, fields = 0, field_failed = 0,
                  camera_unreadable = 0;
    uint32_t handle_min = 0, handle_max = 0;
    int32_t lod_min = 0, lod_max = 0, model_min = 0, model_max = 0;
    uintptr_t previous_registry = 0;
    bool have_registry = false;
    PointerSet nodes, cameras;
} diagnostic;
std::atomic<unsigned long> foreign_entries{0}, current_calls{0}, current_scoped{0};
constexpr unsigned long qpc_stride = 256; // entries between QPC checks
// X3M_FL_TRACE_LOG (fl_trace_log, as fl_camera_log): seconds between fl.trace_sample rows, 0 off, unset 10, at
// most 3600. Digits only, else the default. Read once at install (load_backend), never in the hook.
unsigned sample_seconds() {
    wchar_t text[8]{};
    const DWORD length = x3m::config::get(L"X3M_FL_TRACE_LOG", text, 8);
    if (length == 0 || length >= 8) return 10;
    unsigned value = 0;
    for (DWORD i = 0; i < length; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return 10;
        value = value * 10 + unsigned(text[i] - L'0');
    }
    return value > 3600 ? 3600 : value;
}

void arm_diagnostic() {
    diagnostic.period = diagnostic.due = 0;
    diagnostic.force = false;
    diagnostic.thread.store(0);
    diagnostic.rows = 0;
    diagnostic.have_registry = false;
    diagnostic.previous_registry = 0;
    const unsigned seconds = sample_seconds();
    LARGE_INTEGER frequency{}, now{};
    if (seconds && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && QueryPerformanceCounter(&now)) {
        diagnostic.period = frequency.QuadPart * static_cast<long long>(seconds);
        diagnostic.due = now.QuadPart + diagnostic.period;
    }
}
void reset_period() {
    Diagnostic& d = diagnostic;
    d.entries = d.depth_max = d.null_node = d.null_camera = d.fields = d.field_failed = d.camera_unreadable = 0;
    d.handle_min = d.handle_max = 0;
    d.lod_min = d.lod_max = d.model_min = d.model_max = 0;
    d.nodes.clear();
    d.cameras.clear();
}
// First sighting of a node in this period: handle, model and LOD (two bounded reads).
void note_node_fields(uintptr_t node) {
    Diagnostic& d = diagnostic;
    uint32_t handle = 0, tail[4]{};
    if (!read_memory(node + layout::node_handle, &handle, 4) || !read_memory(node + layout::node_model, tail, sizeof tail)) {
        ++d.field_failed;
        return;
    }
    const int32_t model = static_cast<int32_t>(tail[0]), lod = static_cast<int32_t>(tail[3]);
    if (!d.fields++) {
        d.handle_min = d.handle_max = handle;
        d.model_min = d.model_max = model;
        d.lod_min = d.lod_max = lod;
        return;
    }
    if (handle < d.handle_min) d.handle_min = handle;
    if (handle > d.handle_max) d.handle_max = handle;
    if (model < d.model_min) d.model_min = model;
    if (model > d.model_max) d.model_max = model;
    if (lod < d.lod_min) d.lod_min = lod;
    if (lod > d.lod_max) d.lod_max = lod;
}
void write_sample(const Scope* scope, long long now) {
    Diagnostic& d = diagnostic;
    d.due = now + d.period;
    const uintptr_t node = scope->args[1], camera = scope->args[2];
    uint32_t last_handle = 0, last_camera_handle = 0;
    if (node) read_memory(node + layout::node_handle, &last_handle, 4);
    if (camera) read_memory(camera + layout::camera_handle, &last_camera_handle, 4);
    const uintptr_t registry = registry_now();
    const bool changed = d.have_registry && registry != d.previous_registry;
    d.have_registry = true;
    d.previous_registry = registry;
    const unsigned long calls = current_calls.exchange(0), scoped = current_scoped.exchange(0),
                        foreign = foreign_entries.exchange(0);
    ++d.rows;
    preserved([&]() {
        x3m::log("fl.trace_sample row=%lu status=%s entries=%lu depth_max=%lu null_node=%lu null_camera=%lu nodes=%u%s "
                 "cameras=%u%s camera_unreadable=%lu fields=%lu field_failed=%lu handle_min=%lu handle_max=%lu lod_min=%ld lod_max=%ld "
                 "model_min=%ld model_max=%ld last_node=0x%08lx last_handle=%lu last_camera=0x%08lx "
                 "last_camera_handle=%lu registry=0x%08lx registry_changed=%u current_calls=%lu current_scoped=%lu "
                 "foreign_entries=%lu",
                 d.rows, state.load(), d.entries, d.depth_max, d.null_node, d.null_camera, d.nodes.count,
                 d.nodes.saturated ? "+" : "", d.cameras.count, d.cameras.saturated ? "+" : "", d.camera_unreadable, d.fields,
                 d.field_failed, static_cast<unsigned long>(d.handle_min), static_cast<unsigned long>(d.handle_max),
                 static_cast<long>(d.lod_min), static_cast<long>(d.lod_max), static_cast<long>(d.model_min),
                 static_cast<long>(d.model_max), static_cast<unsigned long>(node),
                 static_cast<unsigned long>(last_handle), static_cast<unsigned long>(camera),
                 static_cast<unsigned long>(last_camera_handle), static_cast<unsigned long>(registry), unsigned(changed),
                 calls, scoped, foreign);
    });
    reset_period();
}
// Per scope entry while observing: counters, two set probes, field reads on a first sighting only, one QPC
// per qpc_stride entries. No allocation, no formatting unless a row is due; every path that may make a system
// call holds an XmmSave.
void note_entry(const Scope* scope) {
    Diagnostic& d = diagnostic;
    if (!d.period) return;
    const DWORD self = GetCurrentThreadId();
    DWORD owner = d.thread.load(std::memory_order_relaxed);
    if (!owner) {
        d.thread.compare_exchange_strong(owner, self);
        owner = d.thread.load(std::memory_order_relaxed);
    }
    if (owner != self) {
        foreign_entries.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    ++d.entries;
    if (scope->depth > d.depth_max) d.depth_max = scope->depth;
    const uintptr_t node = scope->args[1], camera = scope->args[2];
    if (!node)
        ++d.null_node;
    else if (d.nodes.insert(node)) {
        XmmSave xmm; // engine_memory may VirtualQuery the node's region
        note_node_fields(node);
    }
    if (!camera)
        ++d.null_camera;
    else if (d.cameras.insert(camera)) {
        XmmSave xmm; // first sighting in the period: is the camera handle readable?
        uint32_t handle = 0;
        if (!read_memory(camera + layout::camera_handle, &handle, 4)) ++d.camera_unreadable;
    }
    if (!d.force && d.entries % qpc_stride) return;
    XmmSave xmm; // QueryPerformanceCounter may enter the kernel; the row's reads may VirtualQuery
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (!d.force && now.QuadPart < d.due) return;
    d.force = false;
    write_sample(scope, now.QuadPart);
}
void pop(Scope* scope) {
    if (top() == scope && !set_top(scope->parent)) {
        observation.store(false);
        state.store("tls_restore_failed");
        preserved([]() { log_status(); });
    }
}
}
extern "C" {
// The original target, written before the claim (a redirect may be live even when the claim fails late).
uintptr_t x3m_fl_object_original = 0;
__attribute__((force_align_arg_pointer)) void __cdecl x3m_fl_object_enter(Scope* scope) {
    const DWORD error = GetLastError();
    scope->parent = nullptr;
    scope->depth = 0;
    if (!observation.load()) {
        SetLastError(error);
        return;
    }
    Scope* parent = top();
    scope->parent = parent;
    scope->depth = parent ? parent->depth + 1 : 1;
    if (!set_top(scope)) {
        // Observation stops; the forwarded registers and stack words are the dispatcher's own copies.
        observation.store(false);
        state.store("tls_enter_failed");
        preserved([]() { log_status(); });
    } else
        note_entry(scope);
    SetLastError(error);
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_fl_object_leave(Scope* scope) {
    const DWORD error = GetLastError();
    pop(scope);
    SetLastError(error);
}
__attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl x3m_fl_object_unwind(EXCEPTION_RECORD* record,
                                                                                            void* frame, CONTEXT*,
                                                                                            void*) {
    if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        x3m_fl_object_leave(static_cast<Scope*>(frame));
    return ExceptionContinueSearch;
}
// Entry: ECX descriptor, EDX node, [esp] return into the caller (site + 5), [esp+4..+16] camera, flags,
// light1, light2. Frame: Scope at EBP-52..-13 (its first two words the SEH record), saved ECX/EDX/EAX of the
// return at EBP-12/-8/-4. The helpers are cdecl (EBX/ESI/EDI/EBP preserved); ECX/EDX for the target are
// reloaded from the scope's args, the four words from the caller's frame, so a helper failure cannot change
// what is forwarded. `addl $16` removes this frame's copy (the target expects caller cleanup); the caller's
// own `add esp,0x10` then removes the original four words after this RET.
__attribute__((naked)) void x3m_fl_object_dispatch() {
    __asm__ __volatile__(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $52,%esp\n\t"
        "movl %ecx,-40(%ebp)\n\tmovl %edx,-36(%ebp)\n\t"
        "movl 8(%ebp),%eax\n\tmovl %eax,-32(%ebp)\n\tmovl 12(%ebp),%eax\n\tmovl %eax,-28(%ebp)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,-24(%ebp)\n\tmovl 20(%ebp),%eax\n\tmovl %eax,-20(%ebp)\n\t"
        "leal -52(%ebp),%eax\n\tpushl %eax\n\tcall _x3m_fl_object_enter\n\taddl $4,%esp\n\t"
        "movl %fs:0,%eax\n\tmovl %eax,-52(%ebp)\n\tmovl $_x3m_fl_object_unwind,-48(%ebp)\n\t"
        "leal -52(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
        "pushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\t"
        "movl -40(%ebp),%ecx\n\tmovl -36(%ebp),%edx\n\t"
        "call *_x3m_fl_object_original\n\taddl $16,%esp\n\t"
        "movl %eax,-4(%ebp)\n\tmovl %edx,-8(%ebp)\n\tmovl %ecx,-12(%ebp)\n\t"
        "leal -52(%ebp),%eax\n\tpushl %eax\n\tcall _x3m_fl_object_leave\n\taddl $4,%esp\n\t"
        "movl -52(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
        "movl -4(%ebp),%eax\n\tmovl -8(%ebp),%edx\n\tmovl -12(%ebp),%ecx\n\tleave\n\tret\n\t");
}
}
namespace {
// The site holds `call rel32 <target>; add esp,0x10` and the target begins with the six gated bytes. exact
// (production): the eight site bytes also equal the generated ones (implied by rel32 == target - (site + 5)
// for site/target at the same base; compared explicitly). nullptr when the gate passes, else the reason.
static_assert(sizeof layout::target_entry >= 5, "engine_patch::verify_bytes compares at least five bytes");
const char* gate(uintptr_t site, uintptr_t target, bool exact) {
    unsigned char code[sizeof layout::site_bytes]{};
    if (!x3m::engine_patch::read_code(site, code, sizeof code) || code[0] != 0xe8) return "site_mismatch";
    uint32_t displacement = 0;
    std::memcpy(&displacement, code + 1, 4);
    if (site + 5 + displacement != target) return "target_mismatch";
    if (std::memcmp(code + 5, layout::site_bytes + 5, 3)) return "cleanup_mismatch";
    if (exact && std::memcmp(code, layout::site_bytes, sizeof code)) return "site_mismatch";
    if (!x3m::engine_patch::verify_bytes(target, layout::target_entry, sizeof layout::target_entry))
        return "entry_mismatch";
    return nullptr;
}
// The engine anchor at base: the bytes with the relocated disp32 equal to base + engine_slot_rva.
bool engine_anchor_matches(uintptr_t base) {
    unsigned char actual[sizeof layout::engine_anchor]{};
    if (!x3m::engine_patch::read_code(base + layout::engine_anchor_rva, actual, sizeof actual)) return false;
    uint32_t operand = 0;
    std::memcpy(&operand, actual + layout::engine_operand_offset, 4);
    if (operand != base + layout::engine_slot_rva) return false;
    std::memset(actual + layout::engine_operand_offset, 0, 4);
    return !std::memcmp(actual, layout::engine_anchor, sizeof actual);
}
// Pins this DLL for the process lifetime (documented GET_MODULE_HANDLE_EX_FLAG_PIN): the engine calls into it.
bool pin_self() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&site_), &self) != FALSE &&
           self != nullptr;
}
// The gate passed: TLS, pin, claim. On any refusal observation stays off; a claim whose rollback failed leaves
// the redirect registered (recovery_required) and the dispatcher forwarding unobserved.
bool install(uintptr_t site, uintptr_t target) {
    if (!x3m::engine_patch::install_window_open()) {
        state = "late_claim";
        return false;
    }
    if (tls_slot == TLS_OUT_OF_INDEXES) tls_slot = TlsAlloc();
    if (tls_slot == TLS_OUT_OF_INDEXES) {
        state = "tls_unavailable";
        return false;
    }
    if (!pin_self()) {
        state = "pin_failed";
        return false;
    }
    observation.store(false);
    x3m_fl_object_original = target;
    if (!x3m::engine_patch::claim_call(site_, site, target, reinterpret_cast<void*>(&x3m_fl_object_dispatch))) {
        state = site_.status; // late_claim, callsite_mismatch, target_mismatch, protect_failed, patch_rolled_back,
                              // rollback_failed (static strings)
        return false;
    }
    arm_diagnostic();
    reset_period();
    ++session;
    observation.store(true);
    state = "active";
    return true;
}
}
namespace x3m::object_trace {
bool large_address_aware() {
    const DWORD error = GetLastError();
    const bool laa = executable_identity::large_address_aware(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
                                                              read_memory);
    SetLastError(error);
    return laa;
}
// X3M_OBJECT_TRACE=1 (schema default on), X3FL.exe identity, the gated site/target bytes and the engine anchor.
// Inside the engine_patch install window only (load_backend); one fl.trace row either way.
bool initialize() {
    const DWORD error = GetLastError();
    if (site_.patched_in) {
        const bool enabled = observation.load();
        SetLastError(error);
        return enabled;
    }
    wchar_t setting[4]{};
    bool result = false;
    if (x3m::config::get(L"X3M_OBJECT_TRACE", setting, 4) != 1 || setting[0] != L'1')
        state = "disabled";
    else if (!x3m::fl::verified())
        state = "executable_mismatch";
    else {
        const uintptr_t base = x3m::fl::module_base();
        install_base = base;
        install_site = base + layout::site_rva;
        install_target = base + layout::target_rva;
        const char* refusal = gate(install_site, install_target, true);
        if (refusal)
            state = refusal;
        else if (!engine_anchor_matches(base))
            state = "engine_anchor_mismatch";
        else {
            engine_slot = base + layout::engine_slot_rva;
            result = install(install_site, install_target);
        }
    }
    log_status();
    SetLastError(error);
    return result;
}
bool active() {
    return observation.load();
}
bool recovery_required() {
    return site_.patched_in && !observation.load();
}
const char* status() {
    return state.load();
}
bool scope_descriptor(uintptr_t* descriptor, uint32_t* depth) {
    if (descriptor) *descriptor = 0;
    if (depth) *depth = 0;
    if (!descriptor || !depth || !observation.load()) return false;
    const DWORD error = GetLastError(); // TlsGetValue writes ERROR_SUCCESS on success
    Scope* scope = top();
    if (scope) {
        *descriptor = scope->args[0];
        *depth = scope->depth;
    }
    SetLastError(error);
    return scope != nullptr;
}
bool scope_node(uintptr_t* descriptor, uintptr_t* node) {
    if (descriptor) *descriptor = 0;
    if (node) *node = 0;
    if (!descriptor || !node || !observation.load()) return false;
    const DWORD error = GetLastError();
    Scope* scope = top();
    if (scope) {
        *descriptor = scope->args[0];
        *node = scope->args[1];
    }
    SetLastError(error);
    return scope != nullptr;
}
// Inside the scope the engine is still using the node and camera it passed: one bounded read of the node's
// first 0x154 bytes, the camera handle, the engine pointer and the registry. FL has no established world/
// view/projection slots for this snapshot and no established +0x13c alpha: `matrices` reads nothing more and
// the World/WorldBasis/View/Projection bits stay clear.
bool current(Snapshot* out, bool matrices) {
    (void)matrices;
    if (!out) return false;
    const DWORD error = GetLastError();
    *out = {};
    if (!observation.load()) {
        SetLastError(error);
        return false;
    }
    current_calls.fetch_add(1, std::memory_order_relaxed);
    Scope* scope = top();
    if (!scope) {
        SetLastError(error);
        return false;
    }
    current_scoped.fetch_add(1, std::memory_order_relaxed);
    out->session = session;
    out->scope_depth = scope->depth;
    out->mesh = scope->args[0];
    out->node = scope->args[1];
    out->camera = scope->args[2];
    uint32_t node[node_span / 4]{};
    if (out->node && read_memory(out->node, node, sizeof node)) {
        out->valid |= Node;
        out->parent = node[layout::node_parent / 4];
        out->node_handle = node[layout::node_handle / 4];
        out->model = node[layout::node_model / 4];
        out->lod = node[layout::node_lod / 4];
        out->flags12c = node[layout::node_flags12c / 4];
        out->flags130 = node[layout::node_flags130 / 4];
        std::memcpy(out->position, node + layout::node_position / 4, sizeof out->position);
        out->scale[0] = node[layout::node_scale / 4];
        std::memcpy(out->scale + 1, node + layout::node_scale_axes / 4, 12);
        for (unsigned i = 0; i < 3; ++i) std::memcpy(out->basis + i * 3, node + layout::node_basis / 4 + i * 4, 12);
    }
    if (out->camera && read_memory(out->camera + layout::camera_handle, &out->camera_handle, 4)) out->valid |= Camera;
    if (engine_slot && read_memory(engine_slot, &out->engine, 4) && out->engine &&
        read_memory(out->engine + layout::registry_offset, &out->registry, 4) && out->registry)
        out->valid |= Registry;
    SetLastError(error);
    return true;
}
// Restores only this module's five bytes, while no submission can run (never called by the production loader:
// the module is pinned once the redirect is live).
bool shutdown() {
    const DWORD error = GetLastError();
    if (!site_.patched_in) {
        SetLastError(error);
        return true;
    }
    if (observation.load() && top()) {
        state = "shutdown_in_scope";
        SetLastError(error);
        return false;
    }
    observation.store(false);
    if (tls_slot != TLS_OUT_OF_INDEXES && !set_top(nullptr)) {
        state = "shutdown_tls_failed";
        SetLastError(error);
        return false;
    }
    const bool restored = x3m::engine_patch::restore_call(site_);
    state = site_.status; // restored, restore_not_owned, restore_protect_failed, restore_failed
    SetLastError(error);
    return restored;
}
#ifdef X3M_OBJECT_TRACE_FIXTURE
bool fixture_install(void* callsite, void* expected_target, uintptr_t fixture_engine_slot) {
    const uintptr_t site = reinterpret_cast<uintptr_t>(callsite), target = reinterpret_cast<uintptr_t>(expected_target);
    if (site_.patched_in) {
        state = "already_installed";
        return false;
    }
    install_base = 0;
    install_site = site;
    install_target = target;
    if (const char* refusal = gate(site, target, false)) {
        state = refusal;
        return false;
    }
    engine_slot = fixture_engine_slot;
    return install(site, target);
}
void fixture_fail_next_tls_set() {
    fail_next_tls = true;
}
const char* fixture_gate(uintptr_t base) {
    if (const char* refusal = gate(base + layout::site_rva, base + layout::target_rva, true)) return refusal;
    return engine_anchor_matches(base) ? nullptr : "engine_anchor_mismatch";
}
void fixture_force_sample() {
    diagnostic.force = true;
}
#endif
}
