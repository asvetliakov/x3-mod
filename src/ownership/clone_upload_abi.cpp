#include "clone_upload_abi.h"
#include <d3dx9mesh.h>
#include <unwind.h>
#include <cstring>
#include <type_traits>

#if !defined(__i386__) || !defined(__USING_SJLJ_EXCEPTIONS__)
#error This boundary requires the qualified Win32 x86 GCC SJLJ ABI.
#endif

// Build this file twice: the normal object with -fno-exceptions, and the
// X3M_CLONE_UPLOAD_ABI_EH object with exceptions. The latter contains ONLY the
// catch/rethrow helper. Its assembler alias records the compiler-owned opaque
// SJLJ context without depending on its private layout. See the focused builder.
namespace {
namespace abi = x3m::ownership::clone_upload_abi;
struct Cpu {
    unsigned char x87[108];
    unsigned mxcsr;
    DWORD error;
};
struct alignas(16) Frame {
    void* previous;
    void* handler;
    SjLj_Function_Context* sjlj;
    unsigned prepared;
    Cpu incoming;
    Cpu outgoing;
    abi::Arguments arguments;
    HRESULT result;
    abi::Context context;
    abi::CloneTarget target;
    unsigned saved_target;
};
static_assert(sizeof(void*) == 4 && sizeof(Cpu) == 116);
static_assert(offsetof(Frame, sjlj) == 8 && offsetof(Frame, prepared) == 12);
static_assert(offsetof(Frame, incoming) == 16 && offsetof(Frame, outgoing) == 132);
static_assert(offsetof(Frame, arguments) == 248 && offsetof(Frame, result) == 268);
static_assert(offsetof(Frame, context) == 272 && offsetof(Frame, target) == 784 &&
              offsetof(Frame, saved_target) == 788 && sizeof(Frame) == 800);
static_assert(std::is_trivially_destructible_v<Frame>);
}
extern "C" void x3m_clone_original(Frame*);
extern "C" void x3m_clone_saved_original(Frame*);
extern "C" void x3m_clone_abort_cpp(Frame*) noexcept;
extern "C" void x3m_clone_body(Frame*);

#ifdef X3M_CLONE_UPLOAD_ABI_EH
#ifndef __EXCEPTIONS
#error The catch/rethrow object must enable exceptions.
#endif
// This alias is object-local: the no-EH object still refers to real libgcc.
asm(".set __Unwind_SjLj_Register, _x3m_clone_sjlj_register");
extern "C" __attribute__((noinline)) void x3m_clone_body(Frame* frame) {
    // Registration occurs before this body, inside the already captured shell.
    // Observer functions are noexcept and own their ordinary C++ failures.
    // Zeroed opaque bytes are abort-safe. prepare establishes its explicit
    // ready tag after trivial construction and before publication/callbacks.
    frame->prepared = 1;
    abi::observer.prepare(frame->context, frame->arguments);
    abi::observer.before_original(frame->context);
    try {
        if (frame->saved_target)
            x3m_clone_saved_original(frame);
        else
            x3m_clone_original(frame);
    } catch (...) {
        // SJLJ has already selected this catch. Remove our native FS frame
        // BEFORE rethrow escapes the handwritten caller nonlocally.
        x3m_clone_abort_cpp(frame);
        throw;
    }
    ID3DXMesh* output = SUCCEEDED(frame->result) && frame->arguments.output ? *frame->arguments.output : nullptr;
    abi::observer.finish(frame->context, frame->result, output);
    frame->prepared = 0;
}
#else
#ifdef __EXCEPTIONS
#error Compile the shell object with -fno-exceptions.
#endif
namespace {
__attribute__((always_inline)) inline void capture(Cpu& cpu) noexcept {
    asm volatile("fnsave %0\n\tstmxcsr %1" : "=m"(cpu.x87), "=m"(cpu.mxcsr)::"memory");
    cpu.error = GetLastError();
}
__attribute__((always_inline)) inline void restore(const Cpu& cpu) noexcept {
    SetLastError(cpu.error);
    asm volatile("frstor %0\n\tldmxcsr %1" ::"m"(cpu.x87), "m"(cpu.mxcsr) : "memory");
}
void abort_scope(Frame* frame) noexcept {
    if (frame->prepared) {
        frame->prepared = 0; // Idempotent even if cleanup causes reentry.
        abi::observer.abort(frame->context);
    }
}
}
extern "C" __attribute__((naked)) void x3m_clone_sjlj_register(SjLj_Function_Context*) {
    asm volatile("movl %fs:0,%eax\n\tmovl 4(%esp),%edx\n\t"
                 "movl %edx,8(%eax)\n\tjmp __Unwind_SjLj_Register");
}
extern "C" __attribute__((noinline)) void x3m_clone_abort_cpp(Frame* frame) noexcept {
    Cpu state;
    capture(state);
    abort_scope(frame);
    asm volatile("movl %0,%%fs:0" ::"r"(frame->previous) : "memory");
    restore(state);
}
extern "C" EXCEPTION_DISPOSITION __cdecl x3m_clone_unwind(EXCEPTION_RECORD* exception, void* registration, CONTEXT*,
                                                          void*) {
    Cpu state;
    capture(state);
    auto* frame = static_cast<Frame*>(registration);
    if (exception->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) {
        abort_scope(frame);
        // Native SEH does not run GCC SJLJ cleanups. Remove the helper's opaque
        // compiler registration with libgcc's declared API. Do not inspect it.
        if (frame->sjlj) {
            _Unwind_SjLj_Unregister(frame->sjlj);
            frame->sjlj = nullptr;
        }
    }
    restore(state);
    return ExceptionContinueSearch;
}
#ifdef X3M_LATTICE_UPLOAD_ABI_FIXTURE
// Test-only original; no injectable function pointer exists in production.
extern "C" HRESULT x3m_clone_fixture_original(ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*,
                                              ID3DXMesh**);
#endif
extern "C" __attribute__((noinline)) void x3m_clone_original(Frame* frame) {
    const auto& args = frame->arguments;
    restore(frame->incoming);
#ifdef X3M_LATTICE_UPLOAD_ABI_FIXTURE
    frame->result = x3m_clone_fixture_original(args.source, args.options, args.declaration, args.device, args.output);
#else
    frame->result = args.source->CloneMesh(args.options, args.declaration, args.device, args.output);
#endif
    capture(frame->outgoing);
}
extern "C" __attribute__((noinline)) void x3m_clone_saved_original(Frame* frame) {
    const auto& args = frame->arguments;
    const auto target = frame->target;
    restore(frame->incoming);
    frame->result = target(args.source, args.options, args.declaration, args.device, args.output);
    capture(frame->outgoing);
}
// Both naked entrypoints capture before GetLastError/native registration/EH.
// The target is stored separately from the immutable five original arguments.
#define X3M_CLONE_ENTRY_PREFIX                                                                                         \
    "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\t"                                                                   \
    "subl $816,%esp\n\tandl $-16,%esp\n\tmovl %esp,%ebx\n\t"                                                           \
    "fnsave 16(%ebx)\n\tstmxcsr 124(%ebx)\n\t"                                                                         \
    "call _GetLastError@0\n\tmovl %eax,128(%ebx)\n\t"                                                                  \
    "movl %fs:0,%eax\n\tmovl %eax,0(%ebx)\n\t"                                                                         \
    "movl $_x3m_clone_unwind,4(%ebx)\n\tmovl $0,8(%ebx)\n\t"                                                           \
    "movl $0,12(%ebx)\n\tmovl %ebx,%fs:0\n\t"
#define X3M_CLONE_ENTRY_SUFFIX                                                                                         \
    "leal 272(%ebx),%eax\n\tpushl $512\n\tpushl $0\n\tpushl %eax\n\t"                                                  \
    "call _memset\n\taddl $12,%esp\n\t"                                                                                \
    "pushl %ebx\n\tcall _x3m_clone_body\n\taddl $4,%esp\n\t"                                                           \
    "movl 0(%ebx),%eax\n\tmovl %eax,%fs:0\n\t"                                                                         \
    "pushl 244(%ebx)\n\tcall _SetLastError@4\n\t"                                                                      \
    "frstor 132(%ebx)\n\tldmxcsr 240(%ebx)\n\tmovl 268(%ebx),%eax\n\t"                                                 \
    "movl -4(%ebp),%ebx\n\tleave\n\tret"
namespace x3m::ownership {
__attribute__((naked)) HRESULT clone_mesh_upload(ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*,
                                                 ID3DXMesh**) {
    asm volatile(X3M_CLONE_ENTRY_PREFIX "movl $0,784(%ebx)\n\tmovl $0,788(%ebx)\n\t"
                                        "movl 8(%ebp),%eax\n\tmovl %eax,248(%ebx)\n\t"
                                        "movl 12(%ebp),%eax\n\tmovl %eax,252(%ebx)\n\t"
                                        "movl 16(%ebp),%eax\n\tmovl %eax,256(%ebx)\n\t"
                                        "movl 20(%ebp),%eax\n\tmovl %eax,260(%ebx)\n\t"
                                        "movl 24(%ebp),%eax\n\tmovl %eax,264(%ebx)\n\t" X3M_CLONE_ENTRY_SUFFIX);
}
__attribute__((naked)) HRESULT clone_mesh_upload_target(abi::CloneTarget, ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*,
                                                        IDirect3DDevice9*, ID3DXMesh**) {
    asm volatile(X3M_CLONE_ENTRY_PREFIX "movl 8(%ebp),%eax\n\tmovl %eax,784(%ebx)\n\tmovl $1,788(%ebx)\n\t"
                                        "movl 12(%ebp),%eax\n\tmovl %eax,248(%ebx)\n\t"
                                        "movl 16(%ebp),%eax\n\tmovl %eax,252(%ebx)\n\t"
                                        "movl 20(%ebp),%eax\n\tmovl %eax,256(%ebx)\n\t"
                                        "movl 24(%ebp),%eax\n\tmovl %eax,260(%ebx)\n\t"
                                        "movl 28(%ebp),%eax\n\tmovl %eax,264(%ebx)\n\t" X3M_CLONE_ENTRY_SUFFIX);
}
}
#undef X3M_CLONE_ENTRY_PREFIX
#undef X3M_CLONE_ENTRY_SUFFIX
#endif
