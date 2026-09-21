// Uses the accepted ABI fixture's image comparison/observer/SEH controls, but
// executes the actual generated engine site and the production saved-target
// shell. No game process or resource wrapper is involved.
#include <initializer_list>
void lattice_hook_prepare() noexcept;
#define X3M_LATTICE_UPLOAD_ABI_PREPARE_CONTROL lattice_hook_prepare
#define main lattice_abi_regression_main
#include "lattice_upload_abi_fixture.cpp"
#undef main
#include "../../src/proxy/lattice_upload_hook.h"
#include "../../src/proxy/engine_patch.h"

namespace hook = x3m::lattice_upload_hook;
namespace patch = x3m::engine_patch;
namespace {
void* table[13]{};
struct FakeMesh { void** vtable; } mesh{table};
unsigned char* code_site = nullptr;
unsigned char boundary_bytes[64]{};
unsigned boundary_size = 0;
unsigned wrong_target_calls = 0;
bool expected_observer = false;
unsigned protect_fail_mode = 0, protect_site_calls = 0, fail_flushes = 0;
}
extern "C" {
void* lattice_hook_site = nullptr;
unsigned lattice_hook_regs[6]{}, lattice_hook_before_sp = 0;
unsigned lattice_hook_alignment = 0;
}
// Test-only imports replace ONLY engine_patch.o's two IAT references using
// objcopy. They otherwise call the actual documented Windows APIs.
extern "C" BOOL WINAPI lattice_hook_protect(void* address, SIZE_T size,
                                             DWORD protection, DWORD* previous) {
    if (address == code_site && size == 5) {
        ++protect_site_calls;
        if ((protect_fail_mode == 1 && protect_site_calls == 1) ||
            (protect_fail_mode == 2 && protect_site_calls >= 2)) {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
    }
    return VirtualProtect(address, size, protection, previous);
}
extern "C" BOOL WINAPI lattice_hook_flush(HANDLE process, const void* address, SIZE_T size) {
    if (address == code_site && size == 5 && fail_flushes) {
        --fail_flushes;
        SetLastError(ERROR_INVALID_ACCESS);
        return FALSE;
    }
    return FlushInstructionCache(process, address, size);
}
extern "C" {
BOOL (WINAPI *lattice_hook_protect_iat)(void*, SIZE_T, DWORD, DWORD*) = lattice_hook_protect;
BOOL (WINAPI *lattice_hook_flush_iat)(HANDLE, const void*, SIZE_T) = lattice_hook_flush;
}
// This dependency is fixture-owned; production uses the real executable gate.
namespace x3m::object_trace { bool executable_verified() { return false; } }
extern "C" HRESULT STDMETHODCALLTYPE lattice_wrong_original(
    ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**) {
    ++wrong_target_calls;
    return E_UNEXPECTED;
}
void lattice_hook_prepare() noexcept {
    if (lattice_mode == 7) table[12] = reinterpret_cast<void*>(&lattice_wrong_original);
    if (lattice_mode == 8) hook::set_armed(false);
}
extern "C" HRESULT lattice_hook_action(ID3DXMesh* source, DWORD options,
    const D3DVERTEXELEMENT9* decl, IDirect3DDevice9* device, ID3DXMesh** out) {
    ++calls;
    check(source == expected.source && options == expected.options &&
          decl == expected.declaration && device == expected.device && out == expected.output,
          "hook original exact five args and output storage");
    check(active == expected_observer, "hook original observer state matches route");
    if (lattice_mode == 2) throw OriginalError{0xaabbccdd};
    if (lattice_mode == 3) RaiseException(0xe3450131, 0, 0, nullptr);
    if (out) *out = destination;
    return lattice_hr;
}
extern "C" __attribute__((naked)) HRESULT STDMETHODCALLTYPE lattice_hook_original(
    ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**) {
    asm volatile(
        "fnsave _lattice_seen\n\tstmxcsr _lattice_seen+108\n\t"
        "pushl %ebp\n\tmovl %esp,%ebp\n\t"
        "call _GetLastError@0\n\tmovl %eax,_lattice_seen+112\n\t"
        "pushl 24(%ebp)\n\tpushl 20(%ebp)\n\tpushl 16(%ebp)\n\t"
        "pushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall _lattice_hook_action\n\t"
        "addl $20,%esp\n\tpushl %eax\n\tpushl _lattice_outgoing+112\n\t"
        "call _SetLastError@4\n\tfrstor _lattice_outgoing\n\t"
        "ldmxcsr _lattice_outgoing+108\n\tpopl %eax\n\tleave\n\tret $20");
}
extern "C" void lattice_hook_resume();
extern "C" __attribute__((naked)) HRESULT lattice_hook_invoke(
    ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**) {
    asm volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl _lattice_hook_alignment,%esp\n\tmovl %esp,_lattice_hook_before_sp\n\t"
        "movl %ebp,_lattice_hook_regs+20\n\t"
        "pushl _lattice_input+112\n\tcall _SetLastError@4\n\t"
        "frstor _lattice_input\n\tldmxcsr _lattice_input+108\n\t"
        "pushl 24(%ebp)\n\tpushl 20(%ebp)\n\tpushl 16(%ebp)\n\t"
        "pushl 12(%ebp)\n\tpushl 8(%ebp)\n\t"
        "movl $0x11112222,%ebx\n\tmovl $0x33334444,%esi\n\tmovl $0x55556666,%edi\n\t"
        "movl 8(%ebp),%eax\n\tmovl (%eax),%ecx\n\tjmp *_lattice_hook_site\n\t"
        ".globl _lattice_hook_resume\n_lattice_hook_resume:\n\t"
        "fnsave _lattice_returned\n\tstmxcsr _lattice_returned+108\n\t"
        "movl %esp,_lattice_hook_regs\n\tmovl %ebx,_lattice_hook_regs+4\n\t"
        "movl %esi,_lattice_hook_regs+8\n\tmovl %edi,_lattice_hook_regs+12\n\t"
        "movl %ebp,_lattice_hook_regs+16\n\t"
        "pushl %eax\n\tcall _GetLastError@0\n\tmovl %eax,_lattice_returned+112\n\t"
        "frstor _lattice_returned\n\tldmxcsr _lattice_returned+108\n\tpopl %eax\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tleave\n\tret");
}
namespace {
void reset_target() { table[12] = reinterpret_cast<void*>(&lattice_hook_original); }
bool pristine() { return !std::memcmp(code_site, boundary_bytes+18, 5); }
void controls(bool observing, unsigned mode, HRESULT result, unsigned align = 0) {
    expected_observer = observing;
    lattice_mode = mode;
    lattice_hr = result;
    lattice_hook_alignment = align;
    expected.output = &output;
    output = nullptr;
    reset_target();
    const unsigned previous_calls = calls, previous_prepares = prepares,
                   previous_finishes = finishes, previous_aborts = aborts;
    void* chain = fs_head();
    check(lattice_hook_invoke(expected.source, expected.options, expected.declaration,
          expected.device, expected.output) == result, "hook exact HRESULT");
    check(calls == previous_calls+1 && wrong_target_calls == 0, "one captured original target");
    check(output == destination, "hook output written exactly to original storage");
    check(prepares == previous_prepares+unsigned(observing) &&
          finishes == previous_finishes+unsigned(observing) && aborts == previous_aborts,
          "observer count including disabled/unarmed zero-work path");
    check(lattice_hook_regs[0] == lattice_hook_before_sp, "stdcall20 exact stack balance");
    check(lattice_hook_regs[1] == 0x11112222 && lattice_hook_regs[2] == 0x33334444 &&
          lattice_hook_regs[4] == lattice_hook_regs[5], "EBX ESI EBP nonvolatiles preserved");
    check(lattice_hook_regs[3] == static_cast<unsigned>(result), "original continuation consumes EAX into EDI");
    check(same(lattice_seen, lattice_input), "hook incoming computational state LastError");
    check(same(lattice_returned, lattice_outgoing), "hook outgoing computational state LastError");
    check(!active && fs_head() == chain, "hook ordinary FS and observer cleanup");
}
void dump(const char* path, const void* bytes, unsigned count) {
    if (!path) return;
    FILE* file = std::fopen(path, "wb");
    check(file != nullptr, "open emitter evidence file");
    if (file) { check(std::fwrite(bytes, 1, count, file) == count, "write emitter evidence"); std::fclose(file); }
}
}
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    seed(lattice_input, 0x027f, 0x3f80, 0x12345678);
    seed(lattice_outgoing, 0x077f, 0x7f80, 0x87654321);
    expected.source = reinterpret_cast<ID3DXMesh*>(&mesh);
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    check(memory != nullptr, "allocate synthetic engine seam");
    if (!memory) return 2;
    code_site = memory+35; // offset3, as at 004bcc2b: whole patch fits one qword.
    lattice_hook_site = code_site;
    boundary_size = hook::fixture_context(boundary_bytes, sizeof boundary_bytes);
    check(boundary_size == 31, "whole validated caller context size");
    std::memcpy(code_site-18, boundary_bytes, boundary_size);
    code_site[13] = 0xe9;
    const auto continuation = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&lattice_hook_resume)-
        reinterpret_cast<std::uintptr_t>(code_site+18));
    std::memcpy(code_site+14, &continuation, 4);
    DWORD old = 0;
    check(VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &old) != FALSE, "seal synthetic site");
    FlushInstructionCache(GetCurrentProcess(), memory, 4096);
    controls(false, 0, S_OK); // Exact unpatched execution baseline.
    check(!hook::set_armed(true), "uninstalled cannot arm");
    check(!hook::initialize() && !std::strcmp(hook::status(), "executable_mismatch"),
          "production EXE gate refuses fixture image");
    check(VirtualProtect(memory,4096,PAGE_EXECUTE_READWRITE,&old) != FALSE, "open wrong-byte test");
    code_site[1] ^= 1;
    check(!hook::fixture_install(code_site) && !hook::installed(), "wrong claimed bytes refuse");
    code_site[1] ^= 1;
    code_site[-1] ^= 1;
    check(!hook::fixture_install(code_site) && !hook::installed(), "wrong surrounding instruction refuses");
    code_site[-1] ^= 1;
    VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old);
    protect_fail_mode = 1; protect_site_calls = 0;
    check(!hook::fixture_install(code_site) && !hook::installed() && pristine(), "prewrite protection refusal");
    protect_fail_mode = 0; protect_site_calls = 0; fail_flushes = 1;
    check(!hook::fixture_install(code_site) && !hook::installed() && pristine(), "postwrite failure restores all five bytes");
    check(!std::strcmp(hook::status(), "patch_rolled_back"), "rollback result reported");
    protect_fail_mode = 2; protect_site_calls = 0;
    check(!hook::fixture_install(code_site) && hook::installed() && !pristine(), "rollback failure retains live site ownership");
    check(!std::strcmp(hook::status(), "rollback_failed") && !hook::set_armed(true), "failed rollback cannot enable observer");
    controls(false, 0, E_OUTOFMEMORY); // Incomplete install still forwards ordinary tail.
    check(!hook::shutdown_quiescent() && hook::installed(), "failed restore retains ownership");
    protect_fail_mode = 0; protect_site_calls = 0;
    check(hook::shutdown_quiescent() && !hook::installed() && pristine(), "quiescent retry restores entire span");
    check(hook::fixture_install(code_site) && hook::installed(), "install validated whole-instruction seam");
    check(hook::fixture_atomic_write(), "five-byte claim uses atomic qword path");
    dump(argc>1 ? argv[1] : nullptr, hook::fixture_stub(), 34);
    dump(argc>2 ? argv[2] : nullptr, hook::fixture_tail(), 10);
    controls(false, 0, S_OK);
    check(hook::set_armed(true), "arm installed observer route");
    for (unsigned alignment : {0u,4u,8u,12u}) {
        controls(true, 0, S_OK, alignment);
        controls(true, 0, E_OUTOFMEMORY, alignment);
    }
    controls(true, 1, S_OK); // Observer-injected C++ refusal still forwards.
    controls(true, 7, S_OK); // Callback rewrites slot; saved original must win.
    check(table[12] == reinterpret_cast<void*>(&lattice_wrong_original), "callback really changed public target slot");
    controls(true, 8, S_OK); // Entry can be disabled while current scope finishes.
    controls(false, 0, S_OK);
    check(hook::set_armed(true), "rearm routing only for fixture");
    lattice_upload = lattice_hook_invoke;
    expected_observer = true;
    reset_target();
    cpp_escape();
    controls(true, 0, S_OK);
    for (volatile unsigned mode = 3; mode != 5; ++mode) {
        lattice_mode = mode;
        reset_target();
        const unsigned aborted = aborts, finished = finishes, originals = calls, unwinds = seh_seen;
        void* chain = fs_head();
        if (setjmp(recovery) == 0) { invoke(true); check(false, "hook native exception must propagate"); }
        check(seh_seen == unwinds+1 && aborts == aborted+1 && finishes == finished && !active,
              "hook original/prepare actual native unwind cleanup");
        check(calls == originals+unsigned(mode == 3), "unwinding original called exactly once when reached");
        check(fs_head() == chain, "hook native unwind restores FS chain");
        controls(true, 0, S_OK);
    }
    reset_target(); cpp_escape(); controls(true, 0, S_OK);
    check(hook::shutdown_quiescent() && pristine() && !hook::installed(), "final five-byte rollback");
    controls(false, 0, S_OK);
    patch::close_install_window("fixture_first_present");
    const unsigned arena_before = patch::arena_used();
    check(!hook::fixture_install(code_site) && !std::strcmp(hook::status(), "late_claim") &&
          pristine() && arena_before == patch::arena_used(), "closed install window refuses before allocation/mutation");
    std::printf("{\"checks\":%u,\"failures\":%u,\"original_calls\":%u,\"prepares\":%u,"
                "\"finishes\":%u,\"aborts\":%u,\"native_unwinds\":%u,\"wrong_target_calls\":%u,"
                "\"inherited_wrapper_unwind_tested\":false,\"native_windows_verified\":false}\n",
                checks,failures,calls,prepares,finishes,aborts,seh_seen,wrong_target_calls);
    VirtualFree(memory,0,MEM_RELEASE);
    return failures ? 1 : 0;
}
