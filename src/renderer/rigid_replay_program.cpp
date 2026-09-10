#include "rigid_replay_program.h"

namespace x3m::renderer {
namespace {
// D3D9 token encoding, authored here from the documented operation contract.
// This is not a captured program or a fragment copied from a game shader.
constexpr std::uint32_t dp4 = 0x03000009;
constexpr std::uint32_t temporary = 0x80e40000; // r0.xyzw, unmodified
constexpr std::uint32_t row(unsigned n) { return 0xa0e40000u | n; }
constexpr std::uint32_t output(unsigned n, unsigned mask) {
    return 0xe0000000u | (mask << 16) | n;
}
constexpr std::array<std::uint32_t,54> program{{
    0xfffe0300,                         // vs_3_0
    0x05000051, 0xa00f0008, 0x3f800000, 0, 0, 0, // def c8,1,+0,+0,+0
    0x0200001f, 0x80000000, 0x900f0000, // dcl_position v0
    0x0200001f, 0x80000000, 0xe00f0000, // dcl_position o0
    0x0200001f, 0x80000005, 0xe00f0001, // dcl_texcoord o1
    0x04000004, 0x800f0000, 0x90240000, 0xa0400008, 0xa0150008,
    // mad r0, v0.xyzx, c8.xxxy, c8.yyyx (including X*+0+1 for W)
    dp4, output(0,1), temporary, row(0),
    dp4, output(0,2), temporary, row(1),
    dp4, output(0,4), temporary, row(2),
    dp4, output(0,8), temporary, row(3),
    dp4, output(1,1), temporary, row(4),
    dp4, output(1,2), temporary, row(5),
    dp4, output(1,4), temporary, row(6),
    dp4, output(1,8), temporary, row(7),
    0x0000ffff
}};
}
const std::array<std::uint32_t,54>& rigid_replay_program() noexcept { return program; }
const RigidPositionProfile* find_rigid_replay_profile(
        const std::uint32_t* words, std::size_t count) noexcept {
    const auto* p = find_rigid_position(words, count);
    return p && p->shader_version == 0xfffe0300u &&
        p->position_write_order == PositionWriteOrder::XYZW &&
        p->homogeneous_constructor == HomogeneousConstructor::MadXYZIdentityWFromX
        ? p : nullptr;
}
} // namespace x3m::renderer
