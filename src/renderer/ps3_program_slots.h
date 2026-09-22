#pragma once
// The ps_3_0 slot counter the fog, sun-shadow-apply and sun-occlusion passes
// gate against D3DCAPS9::MaxPixelShader30InstructionSlots. Kept free of d3d9.h
// so host tests can compile it with a native compiler.
#include <cstddef>
#include <cstdint>
namespace x3m::renderer {
constexpr std::uint32_t ps3_version_token = 0xffff0300u; // ps_3_0 version token (D3DPS_VERSION(3, 0))
// Conservative ps_3_0 instruction-slot count of a compiled program (the
// bloom gate's table, verification/probe/bloom_shader_limits.py: sincos 8,
// rep 3, nrm 3, pow 3, crs/dp2add/lrp/dsx/dsy/texldl 2, everything else 1;
// dcl/def/defi and comments 0). Zero for a malformed program.
inline std::uint32_t ps3_program_slots(const std::uint32_t* words, std::size_t count) noexcept {
    if (!words || count < 2 || words[0] != ps3_version_token || words[count - 1] != 0xffffu) return 0;
    std::uint32_t slots = 0;
    std::size_t i = 1;
    while (i < count - 1) {
        const std::uint32_t token = words[i];
        if ((token & 0xffffu) == 0xfffeu) { i += 1 + ((token >> 16) & 0x7fffu); continue; } // comment
        const std::uint32_t op = token & 0xffffu, operands = (token >> 24) & 15u;
        std::uint32_t cost = 1;
        switch (op) {
        case 31: case 48: case 81: case 46: cost = 0; break;                   // dcl, defi, def, defb
        case 37: cost = 8; break;                                              // sincos
        case 38: case 36: case 32: cost = 3; break;                            // rep, nrm, pow
        case 33: case 90: case 18: case 91: case 92: case 95: cost = 2; break; // crs, dp2add, lrp, dsx, dsy, texldl
        default: break;
        }
        slots += cost;
        i += 1 + operands;
    }
    return i == count - 1 ? slots : 0;
}
} // namespace x3m::renderer
