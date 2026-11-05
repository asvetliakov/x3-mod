#include <windows.h>
#include <d3dx9.h>
#include <cstdio>
#include <cstdint>
static const std::uint32_t ps_words[] = {
#include "/home/asvetl/x3-mod/src/renderer/engine_plume_pixel_program_inc.h"
};
int main(int argc, char** argv) {
    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) return 2;
    using Fn = HRESULT(WINAPI*)(const DWORD*, BOOL, LPCSTR, LPD3DXBUFFER*);
    Fn f = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "D3DXDisassembleShader")));
    if (!f) return 3;
    LPD3DXBUFFER b = nullptr;
    if (FAILED(f(reinterpret_cast<const DWORD*>(ps_words), FALSE, nullptr, &b))) return 4;
    fwrite(b->GetBufferPointer(), 1, b->GetBufferSize() - 1, stdout);
    return 0;
}
