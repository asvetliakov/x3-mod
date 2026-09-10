// Original legacy-caller ABI fixture. Never installs hooks or launches the game.
#include <windows.h>
#include <emmintrin.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
double input_values[2] = {1.25, 2.5};
double output_values[2] = {};
uintptr_t local_address = 0, call_stack = 0;
unsigned abi_errors = 0;
int object_cookie = 0x12345678;

// Volatile vector locals deliberately require an aligned SSE2 stack spill.
#define VECTOR_BODY \
    volatile __m128d local[2]; \
    local[0] = _mm_loadu_pd(input_values); \
    local[1] = _mm_add_pd(local[0], _mm_set1_pd(3.0)); \
    local_address = reinterpret_cast<uintptr_t>(&local[0]); \
    _mm_storeu_pd(out, local[1]); \
    return cookie == 0x76543210 ? 0x13572468 : 0

__attribute__((noinline)) int __stdcall std_callback(double* out, int cookie) {
    VECTOR_BODY;
}
__attribute__((noinline, thiscall)) int this_callback(int* object, double* out, int cookie) {
    if (object != &object_cookie || *object != 0x12345678) return 0;
    VECTOR_BODY;
}
__attribute__((noinline)) int __cdecl c_callback(double* out, int cookie) {
    VECTOR_BODY;
}
// Scalar arithmetic remains SSE; i686 floating-point return ABI still uses ST0.
__attribute__((noinline, noclone)) float __cdecl scalar_return(float a, float b) { return a * b + 0.5f; }

// Explicitly synthesize each 4-byte-aligned stack residue. kind=2 is cdecl;
// otherwise the callee must pop exactly the two stack arguments. EBP is the
// restoration anchor, and all three remaining nonvolatile GPRs carry sentinels.
__attribute__((naked)) int __cdecl legacy_call(void*, unsigned, unsigned) {
    __asm__ __volatile__(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "andl $-16,%esp\n\tsubl $64,%esp\n\taddl $8,%esp\n\taddl 16(%ebp),%esp\n\t"
        "movl $0x11223344,%ebx\n\tmovl $0x55667788,%esi\n\tmovl $0x12344321,%edi\n\t"
        "pushl $0x76543210\n\tpushl $_output_values\n\tmovl %esp,_call_stack\n\t"
        "movl $_object_cookie,%ecx\n\tcall *8(%ebp)\n\t"
        "cmpl $2,12(%ebp)\n\tjne 1f\n\taddl $8,%esp\n\t1:\n\t"
        "cmpl $0x13572468,%eax\n\tje 2f\n\torl $1,_abi_errors\n\t2:\n\t"
        "movl _call_stack,%edx\n\taddl $8,%edx\n\tcmpl %edx,%esp\n\tje 3f\n\torl $2,_abi_errors\n\t3:\n\t"
        "cmpl $0x11223344,%ebx\n\tje 4f\n\torl $4,_abi_errors\n\t4:\n\t"
        "cmpl $0x55667788,%esi\n\tje 5f\n\torl $8,_abi_errors\n\t5:\n\t"
        "cmpl $0x12344321,%edi\n\tje 6f\n\torl $16,_abi_errors\n\t6:\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tleave\n\tret\n\t");
}
}

__attribute__((force_align_arg_pointer)) LONG CALLBACK fault_handler(EXCEPTION_POINTERS* exception) {
    if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        const auto address = uintptr_t(exception->ContextRecord->Eip);
        const auto* instruction = reinterpret_cast<const unsigned char*>(address);
        if (address - reinterpret_cast<uintptr_t>(&std_callback) >= 128 ||
            instruction[0] != 0x0f || instruction[1] != 0x29 ||
            (exception->ContextRecord->Esp & 15) != 4) ExitProcess(74);
        static const char message[] = "EXPECTED_NEGATIVE_CONTROL_MISALIGNED_MOVAPS\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, sizeof(message)-1, &written, nullptr);
        ExitProcess(73); // No debugger/dialog; runner verifies this expected negative control.
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    AddVectoredExceptionHandler(1, fault_handler);
    const bool negative = argc == 2 && !std::strcmp(argv[1], "negative");
    void* functions[] = {reinterpret_cast<void*>(&std_callback), reinterpret_cast<void*>(&this_callback), reinterpret_cast<void*>(&c_callback)};
    const char* names[] = {"stdcall", "thiscall", "cdecl"};
    unsigned checks = 0;
    for (unsigned kind = 0; kind < 3; ++kind) {
        for (unsigned residue = 0; residue < 16; residue += 4) {
            if (negative && (kind != 0 || residue != 4)) continue;
            abi_errors = 0; output_values[0] = output_values[1] = 0;
            legacy_call(functions[kind], kind, residue);
            const bool ok = abi_errors == 0 && (call_stack & 15) == residue &&
                (local_address & 15) == 0 && output_values[0] == 4.25 && output_values[1] == 5.5;
            std::printf("CASE abi=%s pre_call_mod16=%u local_mod16=%u errors=%u result=%s\n",
                names[kind], unsigned(call_stack & 15), unsigned(local_address & 15), abi_errors, ok ? "PASS" : "FAIL");
            if (!ok) return 1;
            ++checks;
        }
    }
    const bool scalar_ok = scalar_return(1.25f, 2.0f) == 3.0f;
    std::printf("SCALAR_RETURN result=%s\n", scalar_ok ? "PASS" : "FAIL");
    std::printf("RESULT checks=%u status=%s\n", checks, scalar_ok ? "PASS" : "FAIL");
    return scalar_ok ? 0 : 1;
}
