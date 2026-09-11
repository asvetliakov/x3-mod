#include "material_motion.h"
#include "rigid_motion_pixel_program.h"
#include <iterator>

namespace x3m::renderer {
namespace {
using Words = std::vector<std::uint32_t>;
constexpr std::uint32_t end = 0x0000ffff;
constexpr std::size_t vertex_header_end = 335, pixel_header_end = 1074;
constexpr std::size_t pixel_definition_end = 1047;
constexpr std::size_t position_begin = 450, position_end = 466;

std::uint64_t fingerprint(const std::uint32_t* words, std::size_t count) noexcept {
    std::uint64_t value = 14695981039346656037ull;
    for (std::size_t i = 0; i < count; ++i)
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value ^= (words[i] >> shift) & 255;
            value *= 1099511628211ull;
        }
    return value;
}

// Walk instruction boundaries, including opaque comments/preshader metadata.
// Offsets are meaningful only after the full-program qualification below.
bool framed(const std::uint32_t* words, std::size_t count,
            std::size_t header_end) noexcept {
    bool header_boundary = false;
    for (std::size_t at = 1; at < count;) {
        if (at == header_end) header_boundary = true;
        const auto token = words[at];
        const auto opcode = token & 0xffff;
        if (opcode == 0xffff)
            return token == end && at == count - 1 && header_boundary;
        const std::size_t operands = opcode == 0xfffe
            ? ((token >> 16) & 0x7fff) : ((token >> 24) & 15);
        if (operands > count - at - 1) return false;
        if (at < header_end && opcode != 0xfffe && opcode != 0x51 && opcode != 0x1f)
            return false;
        at += operands + 1;
    }
    return false;
}

unsigned register_type(std::uint32_t token) noexcept {
    return ((token >> 28) & 7) | ((token >> 8) & 0x18);
}
bool relocate_register(std::uint32_t& token) noexcept {
    if (!(token & 0x80000000u)) return false;
    // Our fixed authored program has no relative addressing. Never reinterpret
    // an extra relative operand or a literal as an ordinary register.
    if (token & 0x00002000u) return false;
    const auto type = register_type(token), index = token & 0x7ff;
    unsigned relocated;
    switch (type) {
    case 0: if (index > 2) return false; relocated = index + 5; break; // r5..7
    case 1: if (index != 0) return false; relocated = 5; break; // v5
    case 2: if (index > 4) return false; relocated = index + 216; break; // c216..220
    case 8: if (index != 0) return false; relocated = 1; break; // oC1
    default: return false;
    }
    token = (token & ~std::uint32_t(0x7ff)) | relocated;
    return true;
}

// Relocate only our authored motion program, not arbitrary game instructions.
// Preserve its finite/W/depth checks, previous-jitter and invalid-history ABI.
bool motion_fragment(Words& constants, Words& inputs, Words& body) {
    const auto& code = rigid_motion_pixel_program();
    if (code[0] != 0xffff0300u) return false;
    bool body_started = false;
    unsigned definitions = 0, declarations = 0, outputs = 0;
    for (std::size_t at = 1; at < std::size(code);) {
        const auto token = code[at], opcode = token & 0xffff;
        if (opcode == 0xffff)
            return token == end && at == std::size(code) - 1 &&
                definitions == 3 && declarations == 1 && outputs == 1;
        const std::size_t operands = opcode == 0xfffe
            ? ((token >> 16) & 0x7fff) : ((token >> 24) & 15);
        if (operands > std::size(code) - at - 1) return false;
        if (opcode == 0xfffe) { at += operands + 1; continue; }
        if (opcode == 0x51) {
            if (body_started || token != 0x05000051u || operands != 5 ||
                code[at + 1] != (0xa00f0002u + definitions)) return false;
            constants.push_back(token);
            auto destination = code[at + 1];
            if (!relocate_register(destination)) return false;
            constants.push_back(destination);
            constants.insert(constants.end(), code + at + 2, code + at + 6);
            ++definitions;
        } else if (opcode == 0x1f) {
            if (body_started || token != 0x0200001fu || operands != 2 ||
                code[at + 1] != 0x80000005u || code[at + 2] != 0x900f0000u)
                return false;
            inputs.insert(inputs.end(), {token, 0x80040005u, 0x900f0005u});
            ++declarations;
        } else {
            body_started = true;
            // The fixed program consists only of these straight-line operations.
            unsigned expected;
            switch (opcode) {
            case 1: case 6: expected = 2; break; // MOV, RCP
            case 2: case 5: case 8: case 9: case 11: expected = 3; break;
            case 4: case 88: expected = 4; break; // MAD, CMP
            default: return false;
            }
            if (token != (expected << 24 | opcode) || operands != expected) return false;
            body.push_back(token);
            for (std::size_t i = 1; i <= operands; ++i) {
                auto operand = code[at + i];
                if (i == 1 && register_type(operand) == 8) ++outputs;
                if (!relocate_register(operand)) return false;
                body.push_back(operand);
            }
        }
        at += operands + 1;
    }
    return false;
}
} // namespace

std::uint64_t material_motion_fingerprint(const std::uint32_t* words, std::size_t count) noexcept {
    return words ? fingerprint(words, count) : 0;
}
bool material_motion_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    for (const auto& pair : material_motion_reviewed_pairs)
        if (vertex && pixel && pair.vertex == vertex && pair.pixel == pixel) return true;
    return false;
}

MaterialMotionResult material_motion_vertex_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, std::vector<std::uint32_t>& output) noexcept {
    if (!vertex || vertex_words < 2) return MaterialMotionResult::InvalidInput;
    if (vertex_words != 526 || fingerprint(vertex, vertex_words) != 0x53a0a641107ed76cull)
        return MaterialMotionResult::UnsupportedShader;
    if (vertex[0] != 0xfffe0300u || !framed(vertex, vertex_words, vertex_header_end))
        return MaterialMotionResult::ProfileMismatch;
    for (unsigned lane = 0; lane < 4; ++lane) {
        const auto at = position_begin + 4 * lane;
        if (vertex[at] != 0x03000009u ||
            vertex[at + 1] != (0xe0000000u | (1u << (16 + lane))) ||
            vertex[at + 2] != 0x80e40001u || vertex[at + 3] != 0xa0e40018u + lane)
            return MaterialMotionResult::ProfileMismatch;
    }
    try {
        Words variant;
        variant.reserve(vertex_words + 19);
        variant.insert(variant.end(), vertex, vertex + vertex_header_end);
        // New output o6 is TEXCOORD4; existing declarations and math are intact.
        variant.insert(variant.end(), {0x0200001fu, 0x80040005u, 0xe00f0006u});
        variant.insert(variant.end(), vertex + vertex_header_end, vertex + position_end);
        for (unsigned lane = 0; lane < 4; ++lane)
            variant.insert(variant.end(), {0x03000009u,
                0xe0000006u | (1u << (16 + lane)), 0x80e40001u, 0xa0e400fcu + lane});
        variant.insert(variant.end(), vertex + position_end, vertex + vertex_words);
        output.swap(variant);
        return MaterialMotionResult::Applied;
    } catch (...) { return MaterialMotionResult::AllocationFailure; }
}

MaterialMotionResult material_motion_pixel_variant(const std::uint32_t* pixel,
    std::size_t pixel_words, std::vector<std::uint32_t>& output) noexcept {
    if (!pixel || pixel_words < 2) return MaterialMotionResult::InvalidInput;
    if (pixel_words != 1260 || fingerprint(pixel, pixel_words) != 0x8759c7838bbc86c2ull)
        return MaterialMotionResult::UnsupportedShader;
    if (pixel[0] != 0xffff0300u || !framed(pixel, pixel_words, pixel_header_end))
        return MaterialMotionResult::ProfileMismatch;
    try {
        Words constants, inputs, body;
        if (!motion_fragment(constants, inputs, body)) return MaterialMotionResult::ProfileMismatch;
        Words variant;
        variant.reserve(pixel_words + constants.size() + inputs.size() + body.size());
        variant.insert(variant.end(), pixel, pixel + pixel_definition_end);
        variant.insert(variant.end(), constants.begin(), constants.end());
        variant.insert(variant.end(), pixel + pixel_definition_end, pixel + pixel_header_end);
        variant.insert(variant.end(), inputs.begin(), inputs.end());
        variant.insert(variant.end(), pixel + pixel_header_end, pixel + pixel_words - 1);
        variant.insert(variant.end(), body.begin(), body.end());
        variant.push_back(end);
        output.swap(variant);
        return MaterialMotionResult::Applied;
    } catch (...) { return MaterialMotionResult::AllocationFailure; }
}

MaterialMotionResult material_motion_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, const std::uint32_t* pixel, std::size_t pixel_words,
    MaterialMotionVariant& output) noexcept {
    if (!vertex || !pixel || vertex_words < 2 || pixel_words < 2)
        return MaterialMotionResult::InvalidInput;
    // Qualify both fingerprints before either stage transforms, so a wrong pair
    // reports UnsupportedShader whichever stage is wrong and nothing is published.
    if (vertex_words != 526 || pixel_words != 1260 ||
        fingerprint(vertex, vertex_words) != 0x53a0a641107ed76cull ||
        fingerprint(pixel, pixel_words) != 0x8759c7838bbc86c2ull)
        return MaterialMotionResult::UnsupportedShader;
    MaterialMotionVariant variant;
    const auto vertex_result = material_motion_vertex_variant(vertex, vertex_words, variant.vertex);
    if (vertex_result != MaterialMotionResult::Applied) return vertex_result;
    const auto pixel_result = material_motion_pixel_variant(pixel, pixel_words, variant.pixel);
    if (pixel_result != MaterialMotionResult::Applied) return pixel_result;
    output.vertex.swap(variant.vertex);
    output.pixel.swap(variant.pixel);
    return MaterialMotionResult::Applied;
}
} // namespace x3m::renderer
