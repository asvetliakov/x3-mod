// Fade-owner slot probe (docs/architecture/fade-rt2-ownership.md, X3M_FADE_RT2_OWNER): D3DXDisassembleShader's
// "instruction slots used" of every program the option changes, built with the production flags (i686, SSE2, 4-byte
// incoming stack), no device: the current-depth fragment, its thin-vote and fade-owner twins, and the pixel variants of
// the given original pixel programs with the options off, thin vote on, fade owner on and both on.
// usage: fade_owner_probe.exe <d3dx9_37.dll> <ps_*.bin>...
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../src/renderer/material_motion.h"
#include "../../src/renderer/current_depth_pixel_program.h"

namespace {
using Disassemble = HRESULT(WINAPI*)(const DWORD*, BOOL, LPCSTR, LPD3DXBUFFER*);
Disassemble disassemble = nullptr;
unsigned slots(const std::uint32_t* words, std::size_t count, const std::string& label, bool keep = false) {
    LPD3DXBUFFER text = nullptr;
    if (FAILED(disassemble(reinterpret_cast<const DWORD*>(words), FALSE, nullptr, &text)) || !text)
        throw std::runtime_error(label);
    const std::string listing(static_cast<const char*>(text->GetBufferPointer()), text->GetBufferSize());
    text->Release();
    if (keep) { // beside the executable (untracked: it holds the game program); the runner keeps our fragments' lines
                // only
        FILE* file = std::fopen((label + ".asm").c_str(), "wb");
        if (!file) throw std::runtime_error(label);
        std::fwrite(listing.data(), 1, std::strlen(listing.c_str()), file);
        std::fclose(file);
    }
    const auto at = listing.find("instruction slots used");
    unsigned n = 0;
    if (at != std::string::npos) {
        const auto begin = listing.rfind("approximately", at);
        if (begin != std::string::npos) n = unsigned(std::strtoul(listing.c_str() + begin + 13, nullptr, 10));
    }
    std::printf("SLOTS program=%s dwords=%u instruction_slots=%u\n", label.c_str(), unsigned(count), n);
    return n;
}
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    namespace r = x3m::renderer;
    try {
        if (argc < 3) throw std::runtime_error("usage: fade_owner_probe.exe <d3dx9_37.dll> <ps_*.bin>...");
        HMODULE d3dx = LoadLibraryA(argv[1]);
        if (!d3dx) throw std::runtime_error("d3dx9_37");
        disassemble = reinterpret_cast<Disassemble>(
            reinterpret_cast<void*>(GetProcAddress(d3dx, "D3DXDisassembleShader")));
        if (!disassemble) throw std::runtime_error("D3DXDisassembleShader");
        slots(r::current_depth_pixel_program(), std::size(r::current_depth_pixel_program()), "current_depth");
        slots(r::current_depth_thin_pixel_program(), std::size(r::current_depth_thin_pixel_program()),
              "current_depth_thin");
        slots(r::current_depth_owner_pixel_program(), std::size(r::current_depth_owner_pixel_program()),
              "current_depth_owner");
        for (int i = 2; i < argc; ++i) {
            FILE* file = std::fopen(argv[i], "rb");
            if (!file) throw std::runtime_error(argv[i]);
            std::vector<std::uint32_t> original(65536);
            const std::size_t count = std::fread(original.data(), 4, original.size(), file);
            std::fclose(file);
            original.resize(count);
            std::string name(argv[i]);
            name = name.substr(name.find_last_of("/\\") + 1);
            name = name.substr(0, name.find('.'));
            slots(original.data(), original.size(), name + "_original", true);
            const struct {
                const char* label;
                bool thin, owner;
            } modes[] = {
                {"off", false, false}, {"thin", true, false}, {"owner", false, true}, {"owner_thin", true, true}};
            for (const auto& m : modes) {
                r::material_motion_configure_thin_vote(m.thin);
                r::material_motion_configure_fade_owner(m.owner);
                std::vector<std::uint32_t> variant;
                const auto result = r::material_motion_pixel_variant(original.data(), original.size(), variant, true);
                if (result != r::MaterialMotionResult::Applied) {
                    std::printf("VARIANT program=%s mode=%s result=%d\n", name.c_str(), m.label, int(result));
                    continue;
                }
                slots(variant.data(), variant.size(), name + "_variant_" + m.label, true);
            }
            r::material_motion_configure_thin_vote(false);
            r::material_motion_configure_fade_owner(false);
        }
        std::printf("RESULT PASS\n");
        return 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL %s\n", e.what());
        return 1;
    }
}
