#pragma once
// Capability decision of the ambient occlusion pass, kept free of d3d9.h so
// the host test (verification/analysis/test_ambient_occlusion_caps.py) can
// compile it with a native compiler. The pass fills the inputs from D3DCAPS9
// and CheckDeviceFormat; every gate is documented D3D9 (shader model 3
// versions, MaxPixelShader30InstructionSlots, render-target formats,
// post-pixel-shader blending on the owning format, the two blend factors).
#include <cstddef>
#include <cstdint>
namespace x3m::renderer {
struct AmbientOcclusionCapabilityInputs {
    std::uint32_t pixel_shader_version = 0, vertex_shader_version = 0; // D3DCAPS9::PixelShaderVersion / VertexShaderVersion
    std::uint32_t ps30_instruction_slots = 0;   // D3DCAPS9::MaxPixelShader30InstructionSlots
    std::uint32_t largest_program_slots = 0;    // ambient_occlusion_program_slots of the largest embedded program
    std::uint32_t simultaneous_targets = 0;     // D3DCAPS9::NumSimultaneousRTs
    std::uint32_t max_streams = 0;              // D3DCAPS9::MaxStreams
    std::int32_t r32f_target = -1;              // CheckDeviceFormat R32F render-target texture
    std::int32_t r16f_target = -1;              // CheckDeviceFormat R16F render-target texture
    std::int32_t target_blending = -1;          // CheckDeviceFormat owning format, RENDERTARGET | QUERY_POSTPIXELSHADER_BLENDING
    bool src_blend_zero = false;                // D3DPBLENDCAPS_ZERO in SrcBlendCaps
    bool dest_blend_srccolor = false;           // D3DPBLENDCAPS_SRCCOLOR in DestBlendCaps
};
constexpr std::uint32_t ambient_occlusion_ps_3_0 = 0xffff0300u, ambient_occlusion_vs_3_0 = 0xfffe0300u;
// Null when every gate passes, else the reason the device line reports
// (`ao=0 ao_reason=<reason>`). Order: shader model, slots, targets, blending.
inline const char* ambient_occlusion_capability(const AmbientOcclusionCapabilityInputs& in) noexcept {
    if (in.pixel_shader_version < ambient_occlusion_ps_3_0) return "ps_3_0";
    if (in.vertex_shader_version < ambient_occlusion_vs_3_0) return "vs_3_0";
    if (in.largest_program_slots == 0 || in.ps30_instruction_slots < in.largest_program_slots) return "ps_slots";
    if (in.simultaneous_targets == 0 || in.simultaneous_targets > 4 || in.max_streams == 0) return "device_caps";
    if (in.r32f_target < 0) return "r32f_target";
    if (in.r16f_target < 0) return "r16f_target";
    if (in.target_blending < 0) return "target_blending";
    if (!in.src_blend_zero || !in.dest_blend_srccolor) return "blend_factors";
    return nullptr;
}
// Conservative ps_3_0 instruction-slot count of a compiled program (the
// bloom gate's table, verification/probe/bloom_shader_limits.py: sincos 8,
// rep 3, nrm 3, pow 3, crs/dp2add/lrp/dsx/dsy/texldl 2, everything else 1;
// dcl/def/defi and comments 0). Zero for a malformed program.
inline std::uint32_t ambient_occlusion_program_slots(const std::uint32_t* words, std::size_t count) noexcept {
    if (!words || count < 2 || words[0] != ambient_occlusion_ps_3_0 || words[count - 1] != 0xffffu) return 0;
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
