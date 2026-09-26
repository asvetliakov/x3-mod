#pragma once
// Exact native anchor selection for the external camera's ordinary follow
// branch. These are GAME layouts, not backend layouts. See the disassembly in
// docs/reverse-engineering/chase-camera-first-flight.md. The read callback is
// engine_memory::read in production; original synthetic memory in host tests.
#include "chase_camera_math.h"
#include <cstddef>
#include <cstring>

namespace x3m::chase {
struct NativeAnchor {
    Vec3 position, render_position;
    Mat3 basis, render_basis;
    std::uint32_t node = 0;
    bool base_domain = false; // owner(+0x54) exists and owner type(+0x48) == 1
    bool basis_valid = false, render_position_valid = false, render_basis_valid = false;
};
template <class Read>
inline bool native_read(Read& read, std::uintptr_t base, unsigned offset, void* out, std::size_t bytes) {
    // Every game pointer is a non-null, four-byte-aligned x86 address. Reject
    // arithmetic wrap before passing even a scalar field to the memory reader.
    constexpr std::uintptr_t last = UINT32_MAX;
    return base && !(base & 3) && base <= last && offset <= last - base && bytes <= last - base - offset &&
           read(base + offset, out, bytes);
}
template <class Read> inline bool read_native_anchor(std::uintptr_t reference, Read read, NativeAnchor* out) {
    unsigned char object[0x74];
    auto u32 = [](const unsigned char* bytes, unsigned off) {
        std::uint32_t value;
        std::memcpy(&value, bytes + off, 4);
        return value;
    };
    if (!out || !native_read(read, reference, 0, object, sizeof object)) return false;
    NativeAnchor sample;
    sample.node = u32(object, 0x70);
    const std::uint32_t owner = u32(object, 0x54);
    std::uint16_t owner_type = 0, reference_type = 0;
    std::memcpy(&reference_type, object + 0x48, 2);
    if (owner && !native_read(read, owner, 0x48, &owner_type, 2)) return false;
    sample.base_domain = owner && owner_type == 1; // FUN_0044fe20
    auto position = [&](unsigned off, Vec3* value) {
        std::int32_t p[3];
        if (!native_read(read, sample.node, off, p, sizeof p)) return false;
        *value = Vec3{double(p[0]), double(p[1]), double(p[2])};
        return true;
    };
    if (!position(sample.base_domain ? 0x30 : 0xb0, &sample.position)) return false;
    // Basis and the other position domain are diagnostics only. Missing
    // diagnostic memory must not reject a valid native camera anchor.
    if (sample.base_domain)
        sample.render_position_valid = position(0xb0, &sample.render_position);
    else {
        sample.render_position = sample.position;
        sample.render_position_valid = true;
    }
    std::int32_t rows[12];
    sample.render_basis_valid = native_read(read, sample.node, 0xc0, rows, sizeof rows);
    if (sample.render_basis_valid) sample.render_basis = from_fixed(rows);
    if (!sample.base_domain) {
        sample.basis = sample.render_basis;
        sample.basis_valid = sample.render_basis_valid;
    } else {
        // FUN_00450520: ships (type 7) use their simulation camera basis at
        // object data+0x870; other reference types use node+0x40.
        if (reference_type == 7) {
            sample.basis_valid = native_read(read, u32(object, 0x50), 0x870, rows, sizeof rows);
        } else
            sample.basis_valid = native_read(read, sample.node, 0x40, rows, sizeof rows);
        if (sample.basis_valid) sample.basis = from_fixed(rows);
    }
    *out = sample; // no partial publication if any required read failed
    return true;
}
}
