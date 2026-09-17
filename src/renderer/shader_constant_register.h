#pragma once
// Register of a named float constant from a D3D9 program's constant table
// (the documented D3DXSHADER_CONSTANTTABLE / D3DXSHADER_CONSTANTINFO layout
// inside the leading 'CTAB' comment; offsets are relative to the table, which
// starts after the FourCC). Pure, bounds-checked, no device access. Used for
// the per-program LightDir_Dir0 register of the depth replay
// (docs/verification/directional-shadows.md, "Run 38 A (run111) diagnosis":
// the engine's programs keep the sun at c4, c5 or c0).
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace x3m::renderer {
// -1: no table, no such constant, not in the float register set, or malformed.
inline int shader_float_constant_register(const std::uint32_t* words, std::size_t count, const char* name) noexcept {
    if (!words || count < 2 || !name || !*name) return -1;
    const std::size_t name_length = std::strlen(name);
    // The compiler emits the table as a leading comment; only comments are walked.
    for (std::size_t at = 1; at < count && (words[at] & 0xffffu) == 0xfffeu;) {
        const std::size_t length = (words[at] >> 16) & 0x7fffu;
        if (at + 1 + length > count) return -1;
        if (length >= 8 && words[at + 1] == 0x42415443u) { // 'CTAB' + the 28-byte header
            const auto* table = reinterpret_cast<const unsigned char*>(words + at + 2);
            const std::size_t bytes = (length - 1) * 4;
            const auto dword = [&](std::size_t offset) { std::uint32_t v; std::memcpy(&v, table + offset, 4); return v; };
            const auto word = [&](std::size_t offset) { std::uint16_t v; std::memcpy(&v, table + offset, 2); return v; };
            if (dword(0) != 28) return -1;
            const std::size_t constants = dword(12), info = dword(16);
            if (constants > 4096 || info > bytes || constants * 20 > bytes - info) return -1;
            for (std::size_t i = 0; i < constants; ++i) {
                const std::size_t entry = info + i * 20, text = dword(entry);
                if (text >= bytes || name_length + 1 > bytes - text) continue;
                if (std::memcmp(table + text, name, name_length + 1) != 0) continue;
                if (word(entry + 4) != 2 || word(entry + 8) < 1) return -1; // D3DXRS_FLOAT4, at least one register
                return int(word(entry + 6));
            }
            return -1;
        }
        at += 1 + length;
    }
    return -1;
}
} // namespace x3m::renderer
