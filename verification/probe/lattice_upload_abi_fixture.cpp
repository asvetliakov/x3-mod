// Synthetic original tests ONLY the new manual boundary. No wrapped COM call
// or inherited admission/registry native-exception recovery is certified here.
#include "../../src/ownership/clone_upload_abi.h"
#include <windows.h>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace abi = x3m::ownership::clone_upload_abi;
using x3m::ownership::clone_mesh_upload;
struct Image { unsigned char x87[108]; unsigned mxcsr; DWORD error; };
extern "C" {
Image lattice_input{}, lattice_outgoing{}, lattice_seen{}, lattice_returned{};
unsigned lattice_mode = 0;
HRESULT lattice_hr = S_OK;
}
namespace {
unsigned checks = 0, failures = 0, calls = 0, prepares = 0, finishes = 0, aborts = 0;
unsigned observer_failures = 0;
bool active = false;
ID3DXMesh* destination = reinterpret_cast<ID3DXMesh*>(0x567800);
ID3DXMesh* output = nullptr;
D3DVERTEXELEMENT9 declaration[] = {{0,0,D3DDECLTYPE_FLOAT3,0,0,0}, D3DDECL_END()};
abi::Arguments expected{reinterpret_cast<ID3DXMesh*>(0x123400), 0x12345678,
    declaration, reinterpret_cast<IDirect3DDevice9*>(0x987600), &output};
struct OriginalError { unsigned value; };
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
void* fs_head() {
    void* p;
    asm volatile("movl %%fs:0,%0" : "=r"(p));
    return p;
}
void hostile() noexcept {
    const unsigned csr = 0x5f80;
    asm volatile("fninit\n\tfldpi\n\tldmxcsr %0" :: "m"(csr) : "memory");
    SetLastError(0xbadc0de);
}
void prepare(abi::Context& context, const abi::Arguments& args) noexcept {
    ++prepares;
    bool zero = true;
    for (unsigned char c : context.bytes) zero &= c == 0;
    check(zero, "opaque context initialized");
    check(args.source == expected.source && args.options == expected.options &&
          args.declaration == expected.declaration && args.device == expected.device &&
          args.output == expected.output, "prepare gets all exact arguments");
    active = true;
    context.bytes[0] = 1;
#ifdef X3M_LATTICE_UPLOAD_ABI_PREPARE_CONTROL
    X3M_LATTICE_UPLOAD_ABI_PREPARE_CONTROL();
#endif
    if (lattice_mode == 4) RaiseException(0xe3450131, 0, 0, nullptr);
    try {
        if (lattice_mode == 1) throw std::runtime_error("observer only");
    } catch (...) { ++observer_failures; }
    hostile();
}
void before(abi::Context& context) noexcept {
    check(active && context.bytes[0] == 1, "before sees live prepared context");
    hostile();
}
void finish(abi::Context& context, HRESULT result, ID3DXMesh* mesh) noexcept {
    ++finishes;
    check(active && context.bytes[0] == 1, "finish sees live context");
    check(result == lattice_hr, "finish exact HRESULT");
    check(mesh == (SUCCEEDED(result) && expected.output ? destination : nullptr),
          "finish successful output selection");
    active = false;
    context.bytes[0] = 0;
    hostile();
}
void observer_abort(abi::Context& context) noexcept {
    ++aborts;
    check(active && context.bytes[0] == 1, "abort sees initialized active context");
    active = false;
    context.bytes[0] = 0;
    hostile();
}
bool same(const Image& a, const Image& b) {
    // FNSAVE reserved halfwords have no computational meaning. Compare the
    // entire 80-byte register payload and all defined environment bytes.
    for (unsigned i = 0; i != 108; ++i) {
        if (i == 2 || i == 3 || i == 6 || i == 7 || i == 10 || i == 11 ||
            i == 26 || i == 27) continue;
        if (a.x87[i] != b.x87[i]) return false;
    }
    return a.mxcsr == b.mxcsr && a.error == b.error;
}
void seed(Image& image, unsigned short control, unsigned mxcsr, DWORD error) {
    // A FULL live x87 stack tests payload preservation, not just control words.
    asm volatile("fninit\n\tfldcw %1\n\tfld1\n\tfldz\n\tfldpi\n\tfldl2e\n\t"
                 "fldl2t\n\tfldlg2\n\tfldln2\n\tfld1\n\tfnsave %0"
                 : "=m"(image.x87) : "m"(control) : "memory");
    image.mxcsr = mxcsr;
    image.error = error;
}
std::jmp_buf recovery;
unsigned seh_seen = 0;
}
namespace x3m::ownership::clone_upload_abi {
const Observer observer{::prepare, ::before, ::finish, ::observer_abort};
}
extern "C" HRESULT lattice_original_action(ID3DXMesh* source, DWORD options,
    const D3DVERTEXELEMENT9* decl, IDirect3DDevice9* device, ID3DXMesh** out) {
    ++calls;
    check(source == expected.source && options == expected.options &&
          decl == expected.declaration && device == expected.device && out == expected.output,
          "actual original receives all five exact arguments and output storage");
    check(active, "original sees active observer");
    if (lattice_mode == 2) throw OriginalError{0xaabbccdd};
    if (lattice_mode == 3) RaiseException(0xe3450131, 0, 0, nullptr);
    if (out) *out = destination;
    return lattice_hr;
}
// Naked synthetic entry captures BEFORE its own C++ SJLJ registration. Return
// state is planted AFTER the action's unregister; shell must retain it exactly.
extern "C" __attribute__((naked)) HRESULT x3m_clone_fixture_original(
    ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**) {
    asm volatile(
        "fnsave _lattice_seen\n\tstmxcsr _lattice_seen+108\n\t"
        "pushl %ebp\n\tmovl %esp,%ebp\n\t"
        "call _GetLastError@0\n\tmovl %eax,_lattice_seen+112\n\t"
        "pushl 24(%ebp)\n\tpushl 20(%ebp)\n\tpushl 16(%ebp)\n\t"
        "pushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall _lattice_original_action\n\t"
        "addl $20,%esp\n\tpushl %eax\n\tpushl _lattice_outgoing+112\n\t"
        "call _SetLastError@4\n\tfrstor _lattice_outgoing\n\t"
        "ldmxcsr _lattice_outgoing+108\n\tpopl %eax\n\tleave\n\tret");
}
using Upload = HRESULT(*)(ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*,
                          IDirect3DDevice9*, ID3DXMesh**);
extern "C" { Upload lattice_upload = clone_mesh_upload; }
extern "C" __attribute__((naked)) HRESULT lattice_invoke(
    ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**) {
    asm volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl _lattice_input+112\n\t"
        "call _SetLastError@4\n\tfrstor _lattice_input\n\tldmxcsr _lattice_input+108\n\t"
        "pushl 24(%ebp)\n\tpushl 20(%ebp)\n\tpushl 16(%ebp)\n\t"
        "pushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall *_lattice_upload\n\t"
        "fnsave _lattice_returned\n\tstmxcsr _lattice_returned+108\n\t"
        "addl $20,%esp\n\tpushl %eax\n\tcall _GetLastError@0\n\t"
        "movl %eax,_lattice_returned+112\n\tpopl %eax\n\tleave\n\tret");
}
extern "C" EXCEPTION_DISPOSITION __cdecl lattice_outer_handler(
    EXCEPTION_RECORD* record, void* frame, CONTEXT*, void*) {
    if (!(record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) &&
        record->ExceptionCode == 0xe3450131) {
        ++seh_seen;
        // An actual OS unwind, not a catch-all or a CONTEXT EIP shortcut.
        RtlUnwind(frame, nullptr, nullptr, nullptr);
        void* previous = *static_cast<void**>(frame);
        asm volatile("movl %0,%%fs:0" :: "r"(previous) : "memory");
        std::longjmp(recovery, 1);
    }
    return ExceptionContinueSearch;
}
extern "C" __attribute__((naked)) HRESULT lattice_outer(
    ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**) {
    asm volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tsubl $8,%esp\n\t"
        "movl %fs:0,%eax\n\tmovl %eax,-8(%ebp)\n\t"
        "movl $_lattice_outer_handler,-4(%ebp)\n\tleal -8(%ebp),%eax\n\tmovl %eax,%fs:0\n\t"
        "pushl 24(%ebp)\n\tpushl 20(%ebp)\n\tpushl 16(%ebp)\n\t"
        "pushl 12(%ebp)\n\tpushl 8(%ebp)\n\tcall _lattice_invoke\n\t"
        "movl -8(%ebp),%edx\n\tmovl %edx,%fs:0\n\tleave\n\tret");
}
namespace {
HRESULT invoke(bool outer = false) {
    return (outer ? lattice_outer : lattice_invoke)(expected.source, expected.options,
        expected.declaration, expected.device, expected.output);
}
void ordinary(unsigned mode, HRESULT hr, bool null_output = false) {
    lattice_mode = mode;
    lattice_hr = hr;
    expected.output = null_output ? nullptr : &output;
    output = nullptr;
    const unsigned count = calls, finished = finishes, aborted = aborts;
    void* chain = fs_head();
    check(invoke() == hr, "unchanged original HRESULT");
    check(calls == count + 1 && finishes == finished + 1 && aborts == aborted,
          "exactly one original and finish, no abort");
    check(!active && fs_head() == chain, "normal scope and FS restored");
    check(same(lattice_seen, lattice_input), "incoming x87 payload/environment MXCSR LastError");
    check(same(lattice_returned, lattice_outgoing), "outgoing x87 payload/environment MXCSR LastError");
    check(null_output || output == destination, "original output storage retained even on failed HRESULT");
}
void cpp_escape() {
    lattice_mode = 2;
    expected.output = &output;
    const unsigned aborted = aborts, finished = finishes;
    void* chain = fs_head();
    bool caught = false;
    try { invoke(); }
    catch (const OriginalError& error) { caught = error.value == 0xaabbccdd; }
    catch (...) { check(false, "original exception type changed"); }
    check(caught, "original C++ payload propagates unchanged");
    check(aborts == aborted + 1 && finishes == finished && !active,
          "C++ escape aborts exactly once");
    check(fs_head() == chain, "C++ SJLJ escape removes native registration");
}
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    seed(lattice_input, 0x027f, 0x3f80, 0x12345678);
    seed(lattice_outgoing, 0x077f, 0x7f80, 0x87654321);
    ordinary(0, S_OK);
    ordinary(0, S_FALSE);
    ordinary(0, E_OUTOFMEMORY);
    ordinary(0, S_OK, true);
    ordinary(1, S_OK);
    check(observer_failures == 1, "observer C++ refusal still forwards original normally");
    cpp_escape();
    ordinary(0, S_OK);
    for (volatile unsigned mode = 3; mode != 5; ++mode) {
        lattice_mode = mode;
        const unsigned aborted = aborts, finished = finishes, unwinds = seh_seen;
        void* chain = fs_head();
        if (setjmp(recovery) == 0) {
            invoke(true);
            check(false, "native exception must reach outer handler");
        }
        check(seh_seen == unwinds + 1 && aborts == aborted + 1 &&
              finishes == finished && !active,
              "real native unwind in original/prepare aborts exactly once");
        check(fs_head() == chain, "native unwind restores original FS chain");
        ordinary(0, S_OK);
    }
    // This is essential: it detects a stale SJLJ helper registration left by
    // native unwind, which a later HRESULT-only call might otherwise conceal.
    cpp_escape();
    ordinary(0, S_OK);
    std::printf("{\"checks\":%u,\"failures\":%u,\"original_calls\":%u,"
                "\"prepares\":%u,\"finishes\":%u,\"aborts\":%u,\"native_unwinds\":%u,"
                "\"inherited_wrapper_unwind_tested\":false,\"native_windows_verified\":false}\n",
                checks, failures, calls, prepares, finishes, aborts, seh_seen);
    return failures ? 1 : 0;
}
