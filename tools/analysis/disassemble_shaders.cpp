// Offline shader inspection through the game's D3DX helper, without D3D devices.
// Bytecode and resulting disassembly are local research inputs: never commit them.
#include <windows.h>
#include <d3dx9shader.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: disassemble_shaders.exe d3dx.dll input_directory output_directory\n");
        return 2;
    }
    HMODULE library = LoadLibraryA(argv[1]);
    if (!library) {
        std::fprintf(stderr, "LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }
    char module_path[MAX_PATH]{};
    GetModuleFileNameA(library, module_path, MAX_PATH);
    std::printf("disassembler_module=%s\n", module_path);
    using Disassemble = HRESULT(WINAPI*)(const DWORD*, BOOL, const char*, ID3DXBuffer**);
    const FARPROC symbol = GetProcAddress(library, "D3DXDisassembleShader");
    Disassemble disassemble = nullptr;
    static_assert(sizeof(disassemble) == sizeof(symbol));
    std::memcpy(&disassemble, &symbol, sizeof(disassemble));
    if (!disassemble) {
        std::fprintf(stderr, "GetProcAddress failed: %lu\n", GetLastError());
        FreeLibrary(library);
        return 1;
    }
    WIN32_FIND_DATAA file{};
    HANDLE files = FindFirstFileA((std::string(argv[2]) + "\\*.bin").c_str(), &file);
    if (files == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "FindFirstFile failed for %s: %lu\n", argv[2], GetLastError());
        FreeLibrary(library);
        return 1;
    }
    unsigned processed = 0, failures = 0;
    do {
        std::string name(file.cFileName);
        if (name.rfind("vs_", 0) != 0 && name.rfind("ps_", 0) != 0) continue;
        FILE* input = std::fopen((std::string(argv[2]) + "\\" + name).c_str(), "rb");
        if (!input) {
            ++failures;
            continue;
        }
        std::fseek(input, 0, SEEK_END);
        long length = std::ftell(input);
        std::rewind(input);
        if (length < 8 || length > 1024 * 1024 || length % 4) {
            std::fclose(input);
            ++failures;
            continue;
        }
        std::vector<DWORD> code(static_cast<size_t>(length) / 4);
        const bool read_ok = std::fread(code.data(), 1, static_cast<size_t>(length), input) ==
                             static_cast<size_t>(length);
        std::fclose(input);
        // The API has no length parameter: require the shader END token and use
        // only trusted locally captured programs, not arbitrary untrusted files.
        if (!read_ok || code.back() != 0x0000ffff) {
            ++failures;
            continue;
        }
        ID3DXBuffer* text = nullptr;
        const HRESULT result = disassemble(code.data(), FALSE, nullptr, &text);
        bool saved = false;
        if (SUCCEEDED(result) && text) {
            FILE* output = std::fopen((std::string(argv[3]) + "\\" + name + ".txt").c_str(), "wb");
            if (output) {
                const size_t size = text->GetBufferSize();
                saved = std::fwrite(text->GetBufferPointer(), 1, size, output) == size;
                if (std::fclose(output) != 0) saved = false;
            }
        }
        if (text) text->Release();
        std::printf("%s result=%08lx saved=%u\n", name.c_str(), result, saved ? 1u : 0u);
        if (saved)
            ++processed;
        else
            ++failures;
    } while (FindNextFileA(files, &file));
    FindClose(files);
    FreeLibrary(library);
    std::printf("processed=%u failures=%u\n", processed, failures);
    return failures ? 1 : 0;
}
