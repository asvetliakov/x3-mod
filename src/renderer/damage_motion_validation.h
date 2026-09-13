#pragma once
#include "motion_output_profiles.h"

namespace x3m::renderer::detail {
// Owned damage BUMPMAP contract. These are derived instruction offsets and
// register semantics, not copied shader programs. This extra linear walk runs
// only at shader creation, never per draw. Identity is checked by the caller;
// do not extend this contract to arbitrary IFC programs. The complete walk
// admits only known executable opcode/arity pairs and the seven exact flow
// sites, proving that the appended no-texture epilogue is reached once after
// every join. Original instructions (including partial precision) stay intact.
inline bool damage_pixel_structure(const MotionOutputProfile& row, const std::uint32_t* words,
                            std::size_t count) noexcept {
    const bool two_sided = row.pixel_fingerprint == 0x31445adb0a62d134ull;
    if (!two_sided && row.pixel_fingerprint != 0xd51cf763125cb85aull) return false;
    const std::size_t flow[] = {two_sided ? 1411u : 1389u, two_sided ? 1417u : 1395u,
        two_sided ? 1436u : 1418u, two_sided ? 1442u : 1424u,
        two_sided ? 1627u : 1601u, two_sided ? 1689u : 1663u,
        two_sided ? 1701u : 1675u};
    const unsigned flow_ops[] = {40, 43, 41, 43, 40, 42, 43};
    const auto mad_at = two_sided ? 1418u : 1396u;
    const auto cmp_at = two_sided ? 1423u : 1405u;
    const auto rgb_at = two_sided ? 1736u : 1710u;
    const auto alpha_at = rgb_at + 5u;
    if (count != (two_sided ? 1746u : 1720u)) return false;
    unsigned next_flow = 0, depth = 0, outputs = 0, initializer_sites = 0, body = 0;
    bool literal = false;
    // Documented register encodings; source modifiers and swizzles compared
    // in full so relative/predicate/negate/precision substitutions cannot hide.
    const auto zero = 0xa0000000u | ((two_sided ? 0xaau : 0x55u) << 16) | 25u;
    const auto two = 0xa0000000u | ((two_sided ? 0xffu : 0xaau) << 16) | 25u;
    const auto register_type = [](std::uint32_t token) { return ((token >> 28) & 7) | ((token >> 8) & 0x18); };
    const auto instruction = [&](std::size_t at, std::uint32_t token, std::size_t length) {
        const auto op = token & 0xffff;
        if (at < row.pixel_declaration_insert_dword) {
            // c25 supplies 1, 0 and 2 to the native threshold calculation.
            if (at == 1313) {
                if (token != 0x05000051u || words[at + 1] != 0xa00f0019u ||
                    words[at + 2] != 0x3f800000u ||
                    words[at + 3] != (two_sided ? 0xbf800000u : 0u) ||
                    words[at + 4] != (two_sided ? 0u : 0x40000000u) ||
                    words[at + 5] != (two_sided ? 0x40000000u : 0xbf800000u)) return false;
                literal = true;
            }
            // Header layout/register ownership is revalidated by the generic walk.
            return op == 65534 || op == 81 || op == 31;
        }
        if (op == 65534) return false; // exact owned executable region contains no comments
        if (next_flow < 7 && at == flow[next_flow]) {
            const unsigned expected = flow_ops[next_flow];
            if (op != expected) return false;
            if (op == 40) {
                if (depth || token != 0x01000028u || words[at + 1] != (0xe0e40800u | (next_flow == 4 ? 1u : 0u))) return false;
                depth = 1;
            } else if (op == 41) {
                // if_ne r1.y, -r1.y; full-precision mov r1.z, c25.<zero>.
                if (depth || token != 0x02050029u || words[at + 1] != 0x80550001u || words[at + 2] != 0x81550001u) return false;
                depth = 1;
            } else {
                if (depth != 1 || token != expected) return false;
                if (op == 43) depth = 0;
            }
            ++next_flow;
            return true;
        }
        unsigned arity = 0;
        switch (op) {
        case 1: case 6: case 7: case 36: arity = 2; break; // mov rcp rsq nrm
        case 2: case 5: case 8: case 9: case 11: case 32: case 66: arity = 3; break;
        case 4: case 18: case 88: case 90: arity = 4; break;
        default: return false; // every other flow, kill, unknown opcode refuses
        }
        if (token != ((arity << 24) | op) || length != arity) return false;
        if (at > flow[2] && at < flow[3]) {
            if (at != flow[2] + 3 || token != 0x02000001u ||
                words[at + 1] != 0x80040001u || words[at + 2] != zero) return false;
            ++body;
        }
        if (at == mad_at) {
            if (depth || token != 0x04000004u || words[at + 1] != 0x80240001u ||
                words[at + 2] != two || words[at + 3] != 0x81000000u || words[at + 4] != 0xa0000019u) return false;
            ++initializer_sites;
        }
        if (at == cmp_at) {
            if (depth || token != 0x04000058u || words[at + 1] != 0x80020001u ||
                words[at + 2] != 0x80aa0001u || words[at + 3] != zero || words[at + 4] != 0xa0000019u) return false;
            ++initializer_sites;
        }
        const auto destination = words[at + 1];
        // Both scalars survive until the IFC: no intervening instruction may
        // replace r1.y/z, other than the recorded unconditional CMP initializer.
        if (at > mad_at && at < flow[2] && at != cmp_at &&
            register_type(destination) == 0 && (destination & 0x7ffu) == 1 &&
            (destination & 0x00060000u)) return false;
        if (register_type(destination) == 8) {
            if (depth || next_flow != 7) return false;
            if (at == rgb_at) {
                if (token != 0x04000004u || destination != 0x80270800u) return false;
            } else if (at == alpha_at) {
                if (token != 0x03000005u || destination != 0x80280800u) return false;
            } else return false;
            ++outputs;
        }
        return true; // generic walk below checks every operand/reserved register
    };
    if (!words || words[0] != 0xffff0300u || words[count - 1] != 0xffffu) return false;
    for (std::size_t at = 1; at < count - 1;) {
        const auto token = words[at];
        const std::size_t length = (token & 0xffff) == 0xfffe ? (token >> 16) & 0x7fff : (token >> 24) & 15;
        if (length > count - at - 2 || !instruction(at, token, length)) return false;
        at += length + 1;
    }
    return literal && next_flow == 7 && depth == 0 && outputs == 2 && initializer_sites == 2 && body == 1;
}

} // namespace x3m::renderer::detail
