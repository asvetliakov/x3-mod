// Scratch: print D3DXDisassembleShader's "instruction slots used" line for each bytecode file.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dx9shader.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <sstream>
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) return 5;
    auto address = GetProcAddress(module, "D3DXDisassembleShader");
    decltype(&D3DXDisassembleShader) disassemble = nullptr;
    std::memcpy(&disassemble, &address, sizeof(disassemble));
    if (!disassemble) return 6;
    for (int i = 2; i < argc; ++i) {
        std::ifstream input(argv[i], std::ios::binary);
        std::vector<char> data{std::istreambuf_iterator<char>(input), {}};
        ID3DXBuffer* text = nullptr;
        if (FAILED(disassemble(reinterpret_cast<const DWORD*>(data.data()), FALSE, nullptr, &text)) || !text) { std::printf("%s FAILED\n", argv[i]); continue; }
        std::istringstream lines(static_cast<const char*>(text->GetBufferPointer()));
        std::string line;
        while (std::getline(lines, line)) if (line.find("instruction slots used") != std::string::npos) std::printf("%s %s\n", argv[i], line.c_str());
        text->Release();
    }
    FreeLibrary(module);
    return 0;
}
