// Native Windows d3d9 shader validator round trip (no device, no window):
// feeds each program file to d3d9.dll!Direct3DShaderValidatorCreate9, the
// runtime's own bytecode validator (IDirect3DShaderValidator9, interface layout
// as reverse-engineered by Wine dlls/d3d9/d3d9_main.c), one instruction per
// Instruction() call, and prints every validator message and the End() result.
// build: i686-w64-mingw32-g++ -O1 -static native_validate.cpp -o native_validate.exe
// usage (WSL interop): native_validate.exe <program.bin>...
#include <windows.h>
#include <cstdio>
#include <vector>
typedef HRESULT(WINAPI* validator_cb)(const char* file, int line, DWORD_PTR arg3, DWORD_PTR message_id,
                                      const char* message, void* context);
struct Validator;
struct ValidatorVtbl {
    HRESULT(WINAPI* QueryInterface)(Validator*, const IID&, void**);
    ULONG(WINAPI* AddRef)(Validator*);
    ULONG(WINAPI* Release)(Validator*);
    HRESULT(WINAPI* Begin)(Validator*, validator_cb, void*, DWORD_PTR);
    HRESULT(WINAPI* Instruction)(Validator*, const char*, int, const DWORD*, DWORD);
    HRESULT(WINAPI* End)(Validator*);
};
struct Validator {
    ValidatorVtbl* v;
};
static int messages;
static HRESULT WINAPI callback(const char*, int line, DWORD_PTR, DWORD_PTR id, const char* message, void*) {
    ++messages;
    std::printf("  msg instr=%d id=%lu: %s\n", line, (unsigned long)id, message ? message : "(null)");
    return S_OK;
}
int main(int argc, char** argv) {
    HMODULE d3d9 = LoadLibraryA("d3d9.dll");
    auto create = reinterpret_cast<Validator*(WINAPI*)()>(GetProcAddress(d3d9, "Direct3DShaderValidatorCreate9"));
    if (!create) { std::printf("no Direct3DShaderValidatorCreate9\n"); return 2; }
    for (int a = 1; a < argc; ++a) {
        FILE* f = std::fopen(argv[a], "rb");
        if (!f) { std::printf("%s: open failed\n", argv[a]); continue; }
        std::vector<DWORD> w;
        DWORD x;
        while (std::fread(&x, 4, 1, f) == 1) w.push_back(x);
        std::fclose(f);
        Validator* val = create();
        messages = 0;
        HRESULT hb = val->v->Begin(val, callback, nullptr, 0);
        std::printf("%s words=%zu begin=%08lx\n", argv[a], w.size(), (unsigned long)hb);
        HRESULT worst = S_OK;
        size_t at = 0;
        int index = 0;
        while (at < w.size()) {
            DWORD t = w[at], n;
            if (at == 0 || t == 0xffff) n = 1;
            else if ((t & 0xffff) == 0xfffe) n = ((t >> 16) & 0x7fff) + 1;
            else n = ((t >> 24) & 15) + 1;
            if (at + n > w.size()) break;
            HRESULT hr = val->v->Instruction(val, "variant", index, &w[at], n);
            if (hr != S_OK && worst == S_OK) {
                worst = hr;
                std::printf("  first non-S_OK Instruction=%08lx at index=%d dword=%zu\n", (unsigned long)hr, index, at);
            }
            if (t == 0xffff) break;
            at += n;
            ++index;
        }
        HRESULT he = val->v->End(val);
        std::printf("  end=%08lx messages=%d instruction_worst=%08lx\n", (unsigned long)he, messages, (unsigned long)worst);
        val->v->Release(val);
    }
    return 0;
}
