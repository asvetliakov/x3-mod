// Original CPU-only build tool. No D3D device, renderer, or game is created.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dx9shader.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5)
        return 2; // exact native compiler DLL, HLSL input, binary output [, profile (default ps_3_0)]
    const char* profile = argc == 5 ? argv[4] : "ps_3_0";
    if (std::strcmp(profile, "ps_3_0") && std::strcmp(profile, "vs_3_0")) return 2;
    std::ifstream input(argv[2], std::ios::binary);
    if (!input) return 3;
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    if (source.empty() || source.size() > 262144)
        return 4; // the include-expanded resolve with the mask fold is about 70 KB
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) return 5;
    auto address = GetProcAddress(module, "D3DXCompileShader");
    decltype(&D3DXCompileShader) compile = nullptr;
    static_assert(sizeof(compile) == sizeof(address));
    std::memcpy(&compile, &address, sizeof(compile));
    if (!compile) {
        FreeLibrary(module);
        return 6;
    }
    ID3DXBuffer *code = nullptr, *errors = nullptr;
    const HRESULT result = compile(source.data(), static_cast<UINT>(source.size()), nullptr, nullptr, "main", profile,
                                   D3DXSHADER_OPTIMIZATION_LEVEL3, &code, &errors, nullptr);
    if (errors) {
        std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(), stderr);
        errors->Release();
    }
    bool written = false;
    if (SUCCEEDED(result) && code) {
        std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
        output.write(static_cast<const char*>(code->GetBufferPointer()), code->GetBufferSize());
        output.close();
        written = bool(output);
    }
    if (code) code->Release();
    FreeLibrary(module);
    return written ? 0 : 7;
}
