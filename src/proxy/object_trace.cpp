#include "object_trace.h"
#include "config.h"
#include "engine_memory.h"
#include "executable_identity.h"
#include <excpt.h>
#include <array>
#include <cstring>
#include <cstddef>
#include <atomic>

static_assert(sizeof(void*) == 4, "Verified x86 callsite only");
namespace {
using Target = int(__cdecl*)(uintptr_t, uintptr_t, uintptr_t, uint32_t, uintptr_t, uintptr_t);
struct Scope {
    void* previous_seh;
    void* handler;
    Scope* parent;
    uintptr_t args[6];
    uint32_t depth;
};
static_assert(sizeof(Scope) == 40);
DWORD tls_slot = TLS_OUT_OF_INDEXES;
uintptr_t engine_slot = 0, world_slot = 0, basis_slot = 0, view_slot = 0, projection_slot = 0;
unsigned char* patched_site = nullptr;
std::array<unsigned char, 5> before_bytes{}, our_bytes{};
bool installed = false; // ownership record, even when observation is disabled
std::atomic<bool> observation{false};
DWORD original_protection = 0;
std::atomic<const char*> state{"disabled"};
uint64_t session = 0;
unsigned fixture_fail = 0;
#ifdef X3M_OBJECT_TRACE_FIXTURE
bool fail_next_tls = false;
#endif
bool set_top(void* value) {
#ifdef X3M_OBJECT_TRACE_FIXTURE
    if (fail_next_tls) {
        fail_next_tls = false;
        return false;
    }
#endif
    return TlsSetValue(tls_slot, value) != FALSE;
}

bool read_memory(uintptr_t address, void* out, size_t size) {
    return x3m::engine_memory::read(address, out, size);
}
Scope* top() {
    return tls_slot == TLS_OUT_OF_INDEXES ? nullptr : static_cast<Scope*>(TlsGetValue(tls_slot));
}
void pop(Scope* scope) {
    if (top() == scope && !set_top(scope->parent)) {
        observation.store(false);
        state.store("tls_restore_failed");
    }
}
}
extern "C" {
Target x3m_object_original = nullptr;
__attribute__((force_align_arg_pointer)) void __cdecl x3m_object_enter(Scope* scope, const uintptr_t* args) {
    const DWORD error = GetLastError();
    if (!observation.load()) {
        scope->parent = nullptr;
        scope->depth = 0;
        SetLastError(error);
        return;
    }
    scope->parent = top();
    scope->depth = scope->parent ? scope->parent->depth + 1 : 1;
    std::memcpy(scope->args, args, sizeof scope->args);
    if (!set_top(scope)) {
        observation.store(false);
        state.store("tls_enter_failed");
    }
    SetLastError(error);
}
__attribute__((force_align_arg_pointer)) void __cdecl x3m_object_leave(Scope* scope) {
    const DWORD error = GetLastError();
    pop(scope);
    SetLastError(error);
}
__attribute__((force_align_arg_pointer)) EXCEPTION_DISPOSITION __cdecl x3m_object_unwind(EXCEPTION_RECORD* record,
                                                                                         void* frame, CONTEXT*, void*) {
    if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        x3m_object_leave(static_cast<Scope*>(frame));
    return ExceptionContinueSearch;
}
// A real x86 SEH registration covers MSVC game exceptions as well as normal
// returns. GCC C++ destructors alone do not cover foreign SEH unwinding. Scope
// occupies EBP-48..-9; result occupies EBP-4. No backend prologue is relocated.
__attribute__((naked)) int __cdecl x3m_object_dispatch(uintptr_t, uintptr_t, uintptr_t, uint32_t, uintptr_t,
                                                       uintptr_t) {
    __asm__ __volatile__(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $48,%esp\n\t"
        "leal 8(%ebp),%edx\n\tleal -48(%ebp),%eax\n\tpushl %edx\n\tpushl %eax\n\t"
        "call _x3m_object_enter\n\taddl $8,%esp\n\t"
        "movl %fs:0,%eax\n\tmovl %eax,-48(%ebp)\n\t"
        "movl $_x3m_object_unwind,-44(%ebp)\n\tleal -48(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
        "pushl 28(%ebp)\n\tpushl 24(%ebp)\n\tpushl 20(%ebp)\n\tpushl 16(%ebp)\n\tpushl 12(%ebp)\n\tpushl 8(%ebp)\n\t"
        "call *_x3m_object_original\n\taddl $24,%esp\n\tmovl %eax,-4(%ebp)\n\t"
        "leal -48(%ebp),%eax\n\tpushl %eax\n\tcall _x3m_object_leave\n\taddl $4,%esp\n\t"
        "movl -48(%ebp),%eax\n\tmovl %eax,%fs:0\n\tmovl -4(%ebp),%eax\n\tleave\n\tret\n\t");
}
}
namespace x3m::object_trace {
namespace {
bool patch(void* site, void* target) {
    if (installed || !site || !target) {
        state = "invalid_patch_request";
        return false;
    }
    std::array<unsigned char, 5> code{};
    if (!read_memory(reinterpret_cast<uintptr_t>(site), code.data(), code.size()) || code[0] != 0xe8) {
        state = "callsite_mismatch";
        return false;
    }
    uint32_t displacement = 0;
    std::memcpy(&displacement, code.data() + 1, 4);
    if (reinterpret_cast<uintptr_t>(site) + 5 + displacement != reinterpret_cast<uintptr_t>(target)) {
        state = "target_mismatch";
        return false;
    }
    if (tls_slot == TLS_OUT_OF_INDEXES) tls_slot = TlsAlloc();
    if (tls_slot == TLS_OUT_OF_INDEXES) {
        state = "tls_unavailable";
        return false;
    }
    auto replacement = code;
    const uint32_t redirected = reinterpret_cast<uintptr_t>(&x3m_object_dispatch) -
                                (reinterpret_cast<uintptr_t>(site) + 5);
    std::memcpy(replacement.data() + 1, &redirected, 4);
    DWORD protection = 0;
    if (fixture_fail == 1 || !VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection)) {
        state = "protect_failed";
        return false;
    }
    // Publish ownership before mutation. Failed rollback must remain recoverable,
    // even though diagnostics are disabled and initialize() returns false.
    patched_site = static_cast<unsigned char*>(site);
    before_bytes = code;
    our_bytes = replacement;
    original_protection = protection;
    installed = true;
    observation.store(false);
    x3m_object_original = reinterpret_cast<Target>(target);
    std::memcpy(site, replacement.data(), 5);
    const bool flushed = fixture_fail != 2 && fixture_fail < 4 && FlushInstructionCache(GetCurrentProcess(), site, 5);
    DWORD unused = 0;
    const bool protected_again = fixture_fail != 3 && VirtualProtect(site, 5, protection, &unused);
    if (!flushed || !protected_again) {
        DWORD writable = 0;
        if (fixture_fail == 4 || !VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &writable)) {
            state = "rollback_protect_failed";
            return false;
        }
        std::memcpy(site, code.data(), 5);
        const bool rollback_flush = fixture_fail != 5 && FlushInstructionCache(GetCurrentProcess(), site, 5) != FALSE;
        const bool rollback_protect = fixture_fail != 6 && VirtualProtect(site, 5, protection, &unused) != FALSE;
        state = rollback_flush && rollback_protect ? "patch_rolled_back" : "rollback_failed";
        if (rollback_flush && rollback_protect) {
            installed = false;
            patched_site = nullptr;
        }
        return false;
    }
    ++session;
    observation.store(true);
    state = "active";
    return true;
}

// The executable identity (executable_identity.h,
// docs/reverse-engineering/executable-identity.md): the mapped headers and
// section table at the preferred base, one whole-instruction anchor per engine
// global the proxy reads, and the file size on disk. No file hash: the LAA bit
// and the PE CheckSum are free, so the 4GB-patched image verifies. Each hook
// still compares its own site bytes before it writes. Evaluated once per
// process (about 40 short reads and one GetFileSizeEx); shared with every
// module that reads engine globals.
bool verified_image() {
    // Racing first callers compute the same value; the atomic makes the publish well-defined.
    static std::atomic<int> cached{-1};
    const int known = cached.load(std::memory_order_acquire);
    if (known >= 0) return known == 1;
    HMODULE module = GetModuleHandleW(nullptr);
    const bool valid = reinterpret_cast<uintptr_t>(module) == executable_identity::image_base &&
                       executable_identity::known_structure(read_memory) &&
                       executable_identity::anchors_match(read_memory) && executable_identity::known_file_size(module);
    cached.store(valid ? 1 : 0, std::memory_order_release);
    return valid;
}
}
bool executable_verified() {
    const DWORD error = GetLastError();
    const bool valid = verified_image();
    SetLastError(error);
    return valid;
}
bool large_address_aware() {
    const DWORD error = GetLastError();
    const bool laa = executable_identity::large_address_aware(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
                                                              read_memory);
    SetLastError(error);
    return laa;
}
bool initialize() {
    const DWORD error = GetLastError();
    if (installed) {
        const bool enabled = observation.load();
        SetLastError(error);
        return enabled;
    }
    wchar_t setting[4]{};
    if (x3m::config::get(L"X3M_OBJECT_TRACE", setting, 4) != 1 || setting[0] != L'1') {
        state = "disabled";
        SetLastError(error);
        return false;
    }
    const uintptr_t base = 0x400000;
    bool valid = verified_image();
    static constexpr unsigned char expected[] = {0xe8, 0x23, 0xaf, 0xff, 0xff};
    unsigned char call[5]{};
    valid = valid && read_memory(base + 0xc5228, call, 5) && !std::memcmp(call, expected, 5);
    if (!valid) {
        state = "executable_mismatch";
        SetLastError(error);
        return false;
    }
    engine_slot = base + 0x208518;
    world_slot = base + 0x208a44;
    basis_slot = base + 0x208a48;
    view_slot = base + 0x208a40;
    projection_slot = base + 0x208a38;
    const bool result = patch(reinterpret_cast<void*>(base + 0xc5228), reinterpret_cast<void*>(base + 0xc0150));
    SetLastError(error);
    return result;
}
bool active() {
    return observation.load();
}
bool recovery_required() {
    return installed && !observation.load();
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
bool current(Snapshot* out, bool matrices) {
    if (!out) return false;
    const DWORD error = GetLastError();
    *out = {};
    if (!observation.load()) {
        SetLastError(error);
        return false;
    }
    Scope* scope = top();
    if (!scope) {
        SetLastError(error);
        return false;
    }
    out->session = session;
    out->scope_depth = scope->depth;
    out->mesh = scope->args[0];
    out->node = scope->args[1];
    out->camera = scope->args[2];
    // Snapshot finite fixed-size regions only. No pointer walks or game strings.
    uint32_t node[0x150 / 4]{};
    if (read_memory(out->node, node, sizeof node)) {
        if (matrices) {
            out->parent = node[0x18 / 4];
            out->alpha13c = node[0x13c / 4];
        }
        out->valid |= Node;
        out->node_handle = node[0x28 / 4];
        out->model = node[0x140 / 4];
        out->lod = node[0x14c / 4];
        out->flags12c = node[0x12c / 4];
        out->flags130 = node[0x130 / 4];
        std::memcpy(out->position, node + 0xb0 / 4, sizeof out->position);
        out->scale[0] = node[0x70 / 4];
        std::memcpy(out->scale + 1, node + 0x80 / 4, 12);
        for (unsigned i = 0; i < 3; ++i) std::memcpy(out->basis + i * 3, node + 0xc0 / 4 + i * 4, 12);
    }
    if (read_memory(out->camera + 0x28, &out->camera_handle, 4)) out->valid |= Camera;
    if (read_memory(engine_slot, &out->engine, 4) && read_memory(out->engine + 0xc, &out->registry, 4) && out->registry)
        out->valid |= Registry;
    // Diagnostics only (the route submits rows from the shader-constant shadow):
    // eight reads the per-draw path skips.
    if (matrices) {
        auto matrix = [&](uintptr_t slot, uint32_t* result, Valid bit) {
            uintptr_t address = 0;
            if (read_memory(slot, &address, 4) && read_memory(address, result, 64)) out->valid |= bit;
        };
        matrix(world_slot, out->world, World);
        matrix(basis_slot, out->world_basis, WorldBasis);
        matrix(view_slot, out->view, View);
        matrix(projection_slot, out->projection, Projection);
    }
    SetLastError(error);
    return true;
}
bool shutdown() {
    const DWORD error = GetLastError();
    if (!installed) {
        SetLastError(error);
        return true;
    }
    unsigned char code[5]{};
    DWORD protection = 0, unused = 0;
    if ((observation.load() && top()) || !read_memory(reinterpret_cast<uintptr_t>(patched_site), code, 5) ||
        (std::memcmp(code, our_bytes.data(), 5) && std::memcmp(code, before_bytes.data(), 5))) {
        state = "shutdown_not_owned_or_active";
        SetLastError(error);
        return false;
    }
    observation.store(false);
    if (!set_top(nullptr)) {
        state = "shutdown_tls_failed";
        SetLastError(error);
        return false;
    }
    if (fixture_fail == 1 || !VirtualProtect(patched_site, 5, PAGE_EXECUTE_READWRITE, &protection)) {
        state = "shutdown_protect_failed";
        SetLastError(error);
        return false;
    }
    std::memcpy(patched_site, before_bytes.data(), 5);
    const bool flush = fixture_fail != 2 && FlushInstructionCache(GetCurrentProcess(), patched_site, 5) != FALSE;
    const bool protect = fixture_fail != 3 && VirtualProtect(patched_site, 5, original_protection, &unused) != FALSE;
    state = flush && protect ? "restored" : "restore_failed";
    if (flush && protect) {
        installed = false;
        patched_site = nullptr;
    }
    SetLastError(error);
    return flush && protect;
}

#ifdef X3M_OBJECT_TRACE_FIXTURE
bool fixture_install(void* site, void* target, const FixtureAddresses& addresses, unsigned fail_stage) {
    engine_slot = addresses.engine_slot;
    world_slot = addresses.world_slot;
    basis_slot = addresses.basis_slot;
    view_slot = addresses.view_slot;
    projection_slot = addresses.projection_slot;
    fixture_fail = fail_stage;
    const bool result = patch(site, target);
    fixture_fail = 0;
    return result;
}
bool fixture_shutdown(unsigned fail_stage) {
    fixture_fail = fail_stage;
    const bool result = shutdown();
    fixture_fail = 0;
    return result;
}
void fixture_fail_next_tls_set() {
    fail_next_tls = true;
}
#endif
}
