#include "lattice_upload_hook.h"
#include "engine_patch.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "../ownership/clone_upload_abi.h"
#include <atomic>
#include <cstring>

static_assert(sizeof(void*) == 4, "Qualified x86 engine seam only");
namespace {
namespace patch = x3m::engine_patch;
patch::Site site;
alignas(4) volatile LONG armed = 0;
bool ready = false;
void* stub = nullptr;
std::atomic<const char*> state{"disabled"};
constexpr std::uintptr_t site_va = 0x004bcc2b;
constexpr unsigned prefix_size = 18;
constexpr unsigned char boundary[] = {
    // 004bcc19: this/vtable, output address, and five stdcall pushes.
    0x8b,0x47,0x14,0x8b,0x08,0x8d,0x54,0x24,0x10,0x52,
    0x8b,0x54,0x24,0x1c,0x52,0x56,0x55,0x50,
    // Entire claimed span: MOV EAX,[ECX+30]; CALL EAX.
    0x8b,0x41,0x30,0xff,0xd0,
    // 004bcc30: returned HRESULT consumption; no outgoing EFLAGS use.
    0x8b,0xf8,0x81,0xff,0x0e,0x00,0x07,0x80};
constexpr unsigned stub_reserve = 48;
constexpr unsigned claim_reserve = 5 + 5 + 4 + 6 + 8;

void* emit_stub() {
    patch::Emitter e(stub_reserve);
    if (!e.ok()) return nullptr;
    // Preserve the incoming flags around the routing test. No helper, x87,
    // MXCSR, LastError or nonvolatile-register work occurs on the tail path.
    e.byte(0x9c);                         // pushfd
    e.byte(0x83); e.byte(0x3d);            // cmp dword [armed],0
    e.dword(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&armed)));
    e.byte(0x00);
    e.byte(0x75); e.byte(0x06);            // jne observed
    e.byte(0x9d);                         // popfd
    e.byte(0xe9); e.rel32(site.tail);      // ordinary displaced tail exactly once
    e.byte(0x9d);                         // observed: popfd
    e.byte(0x8b); e.byte(0x41); e.byte(0x30); // mov eax,[ecx+30] BEFORE callbacks
    e.byte(0x50);                         // push captured target before native args
    e.byte(0xe8);
    e.rel32(reinterpret_cast<const void*>(&x3m::ownership::clone_mesh_upload_target));
    e.byte(0x83); e.byte(0xc4); e.byte(0x18); // remove target + original 20 argument bytes
    e.byte(0xe9); e.rel32(reinterpret_cast<const void*>(site.spec.address + 5));
    return e.finish();
}
bool rollback(const char* reason) {
    ready = false;
    InterlockedExchange(&armed, 0);
    if (!patch::restore(site)) {
        state.store(site.status);
        return false;
    }
    state.store(reason);
    return false;
}
bool install(std::uintptr_t address) {
    // Do not overwrite ownership metadata after any partially successful claim.
    if (site.patched_in) return ready;
    if (!patch::install_window_open()) { state = "late_claim"; return false; }
    unsigned char actual[sizeof(boundary)]{};
    if (address < prefix_size ||
        !patch::read_code(address-prefix_size, actual, sizeof actual) ||
        std::memcmp(actual, boundary, sizeof actual)) {
        state = "boundary_mismatch";
        return false;
    }
    // Both tail and observed stub must fit before mutating the engine bytes.
    if (patch::arena_used() > patch::arena_capacity() ||
        patch::arena_capacity()-patch::arena_used() < claim_reserve+stub_reserve) {
        state = "arena_full";
        return false;
    }
    site = {};
    const patch::SiteSpec spec{"final_clone_upload", address,
        {0x8b,0x41,0x30,0xff,0xd0}, 5, 20, 0};
    if (!patch::claim(site, spec)) { state = site.status; return false; }
    stub = emit_stub();
    if (!stub) return rollback("stub_failed");
    if (!patch::push_front(site, stub)) return rollback("chain_failed");
    ready = true;
    state = "installed_unarmed";
    return true;
}
}
namespace x3m::lattice_upload_hook {
bool initialize() {
    PreserveCpuState cpu;
    if (site.patched_in) return ready;
    if (!patch::install_window_open()) { state = "late_claim"; return false; }
    if (!object_trace::executable_verified()) { state = "executable_mismatch"; return false; }
    return install(site_va);
}
bool installed() { return site.patched_in; }
const char* status() { return state.load(); }
bool set_armed(bool enabled) {
    if (enabled && (!ready || !site.patched_in)) return false;
    InterlockedExchange(&armed, enabled ? 1 : 0);
    if (ready && site.patched_in) state = enabled ? "armed" : "installed_unarmed";
    return true;
}
bool shutdown_quiescent() {
    PreserveCpuState cpu;
    InterlockedExchange(&armed, 0);
    ready = false;
    const bool okay = patch::restore(site);
    state = site.status;
    return okay;
}
#ifdef X3M_LATTICE_UPLOAD_HOOK_FIXTURE
bool fixture_install(void* address) {
    PreserveCpuState cpu;
    return install(reinterpret_cast<std::uintptr_t>(address));
}
const void* fixture_stub() { return stub; }
const void* fixture_tail() { return site.tail; }
bool fixture_atomic_write() { return site.atomic_write; }
unsigned fixture_context(unsigned char* output, unsigned capacity) {
    if (!output || capacity < sizeof(boundary)) return 0;
    std::memcpy(output, boundary, sizeof boundary);
    return sizeof(boundary);
}
#endif
}
