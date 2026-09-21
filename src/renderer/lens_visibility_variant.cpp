#include "lens_visibility_variant.h"
#include <cstring>
#include <new>

namespace x3m::renderer {
namespace {
constexpr std::uint32_t end_token = 0x0000ffffu, comment_opcode = 0xfffeu;
enum Opcode : std::uint32_t { Nop = 0, Mov = 1, Mul = 5, Call = 25, CallNz = 26, Loop = 27, Ret = 28, EndLoop = 29, Label = 30, Dcl = 31,
                              Rep = 38, EndRep = 39, If = 40, Ifc = 41, Else = 42, EndIf = 43, Break = 44, BreakC = 45, TexLd = 66,
                              Def = 81, DefI = 82, DefB = 83, BreakP = 96 };
enum Register : unsigned { Temp = 0, Const = 2, ColorOut = 8, Sampler = 10, Const2 = 11, Const3 = 12, Const4 = 13 };
constexpr std::uint32_t predicated = 1u << 28, relative = 1u << 13;
unsigned register_type(std::uint32_t token) noexcept { return ((token >> 28) & 7u) | ((token >> 8) & 0x18u); }
unsigned register_number(std::uint32_t token) noexcept { return token & 0x7ffu; }
bool has_destination(std::uint32_t opcode) noexcept {
    switch (opcode) {
    case Nop: case Call: case CallNz: case Loop: case Ret: case EndLoop: case Label: case Rep: case EndRep:
    case If: case Ifc: case Else: case EndIf: case Break: case BreakC: case BreakP: return false;
    default: return true;
    }
}
// Highest index below `limit` whose bit is clear; `limit` when none.
unsigned highest_free(const std::uint32_t* bits, unsigned limit, unsigned skip = ~0u) noexcept {
    for (unsigned i = limit; i-- > 0;) if (i != skip && !(bits[i >> 5] >> (i & 31) & 1u)) return i;
    return limit;
}
} // namespace
const char* lens_visibility_result_name(LensVisibilityResult r) noexcept {
    switch (r) {
    case LensVisibilityResult::Applied: return "applied"; case LensVisibilityResult::InvalidInput: return "invalid_input";
    case LensVisibilityResult::UnsupportedVersion: return "unsupported_version"; case LensVisibilityResult::UnsupportedShader: return "unsupported_shader";
    case LensVisibilityResult::NoOutput: return "no_output"; case LensVisibilityResult::ResourceLimit: return "resource_limit";
    case LensVisibilityResult::AllocationFailure: return "allocation_failure";
    }
    return "invalid";
}
LensVisibilityResult lens_visibility_pixel_variant(const std::uint32_t* original, std::size_t words, LensVisibilityScale scale,
                                                   std::vector<std::uint32_t>& output, LensVisibilityLayout* layout) noexcept {
    if (!original || words < 2 || words > (1u << 16)) return LensVisibilityResult::InvalidInput;
    const unsigned mask = scale == LensVisibilityScale::Rgb ? 0x7u : scale == LensVisibilityScale::Alpha ? 0x8u : scale == LensVisibilityScale::Both ? 0xfu : 0u;
    if (!mask) return LensVisibilityResult::InvalidInput;
    const std::uint32_t version = original[0];
    const bool sm3 = version == 0xffff0300u;
    if (!sm3 && version != 0xffff0200u && version != 0xffff0201u) return LensVisibilityResult::UnsupportedVersion;
    const unsigned temporaries = sm3 ? 32u : 12u, constants = sm3 ? 224u : 32u, samplers = 16u;
    std::uint32_t used_temp[1]{}, used_sampler[1]{}, used_const[7]{};
    unsigned written = 0, outputs = 0, depth = 0;
    std::size_t at = 1, body = 0; // body: the first token after the leading comments
    bool leading = true, ended = false;
    while (at < words) {
        const std::uint32_t token = original[at];
        if (token == end_token) { ended = at + 1 == words; break; }
        const std::uint32_t opcode = token & 0xffffu;
        if (opcode == comment_opcode) {
            const std::size_t length = (token >> 16) & 0x7fffu;
            if (length >= words - at) return LensVisibilityResult::InvalidInput;
            at += 1 + length; if (leading) body = at; continue;
        }
        if (leading) { body = at; leading = false; }
        if (token & 0x80000000u) return LensVisibilityResult::InvalidInput;
        const std::size_t length = (token >> 24) & 15u;
        if (length >= words - at) return LensVisibilityResult::InvalidInput;
        if (token & predicated) return LensVisibilityResult::UnsupportedShader;
        const std::uint32_t* p = original + at + 1;
        auto note = [&](std::uint32_t parameter) {
            if (!(parameter & 0x80000000u) || (parameter & relative)) return false;
            const unsigned type = register_type(parameter), number = register_number(parameter);
            if (type == Temp) { if (number >= 32) return false; used_temp[0] |= 1u << number; }
            else if (type == Const) { if (number >= 224) return false; used_const[number >> 5] |= 1u << (number & 31); }
            else if (type == Sampler) { if (number >= samplers) return false; used_sampler[0] |= 1u << number; }
            else if (type == Const2 || type == Const3 || type == Const4) return false;
            return true;
        };
        switch (opcode) {
        case Call: case CallNz: case Ret: case Label: return LensVisibilityResult::UnsupportedShader;
        case Def: if (length != 5 || !note(p[0])) return LensVisibilityResult::UnsupportedShader; break;
        case DefI: case DefB: break; // integer / boolean registers: never chosen below
        case Dcl: if (length != 2 || !note(p[1])) return LensVisibilityResult::UnsupportedShader; break;
        default: {
            for (std::size_t i = 0; i < length; ++i) if (!note(p[i])) return LensVisibilityResult::UnsupportedShader;
            if (length && has_destination(opcode) && register_type(p[0]) == ColorOut && register_number(p[0]) == 0) {
                if (depth) return LensVisibilityResult::UnsupportedShader;
                written |= (p[0] >> 16) & 0xfu; ++outputs;
            }
            if (opcode == If || opcode == Ifc || opcode == Loop || opcode == Rep) ++depth;
            else if (opcode == EndIf || opcode == EndLoop || opcode == EndRep) { if (!depth) return LensVisibilityResult::InvalidInput; --depth; }
        } }
        at += 1 + length;
    }
    if (!ended || depth) return LensVisibilityResult::InvalidInput;
    if (!outputs || written != 0xfu) return LensVisibilityResult::NoOutput;
    const std::size_t end = at;
    if (!body) body = 1;
    LensVisibilityLayout chosen;
    chosen.sampler = highest_free(used_sampler, samplers); chosen.constant = highest_free(used_const, constants);
    chosen.output_temporary = highest_free(used_temp, temporaries);
    chosen.fetch_temporary = highest_free(used_temp, temporaries, chosen.output_temporary);
    if (chosen.sampler == samplers || chosen.constant == constants || chosen.output_temporary == temporaries || chosen.fetch_temporary == temporaries)
        return LensVisibilityResult::ResourceLimit;
    const std::uint32_t K = chosen.constant, S = chosen.sampler, O = chosen.output_temporary, T = chosen.fetch_temporary;
    std::vector<std::uint32_t> result;
    try { result.reserve(words + 9 + 15); } catch (const std::bad_alloc&) { return LensVisibilityResult::AllocationFailure; }
    result.insert(result.end(), original, original + body);
    const float half = .5f, one = 1.f; std::uint32_t half_bits = 0, one_bits = 0;
    std::memcpy(&half_bits, &half, 4); std::memcpy(&one_bits, &one, 4);
    for (std::uint32_t w : {0x05000051u, 0xa00f0000u | K, half_bits, half_bits, 0u, one_bits, 0x0200001fu, 0x90000000u, 0xa00f0800u | S}) result.push_back(w);
    // The body, with every oC0 destination (always the first parameter) redirected to rO.
    for (std::size_t i = body; i < end;) {
        const std::uint32_t token = original[i], opcode = token & 0xffffu;
        const bool comment = opcode == comment_opcode;
        const std::size_t length = comment ? (token >> 16) & 0x7fffu : (token >> 24) & 15u;
        result.push_back(token);
        for (std::size_t k = 0; k < length; ++k) {
            std::uint32_t parameter = original[i + 1 + k];
            if (k == 0 && !comment && opcode != Def && opcode != DefI && opcode != DefB && opcode != Dcl && has_destination(opcode) &&
                register_type(parameter) == ColorOut && register_number(parameter) == 0)
                parameter = 0x80000000u | (parameter & 0x00ff0000u) | O;
            result.push_back(parameter);
        }
        i += 1 + length;
    }
    for (std::uint32_t w : {0x02000001u, 0x800f0000u | T, 0xa0e40000u | K,
                            0x03000042u, 0x800f0000u | T, 0x80e40000u | T, 0xa0e40800u | S,
                            0x03000005u, 0x80000000u | (std::uint32_t(mask) << 16) | O, 0x80e40000u | O, 0x80550000u | T,
                            0x02000001u, 0x800f0800u, 0x80e40000u | O, end_token}) result.push_back(w);
    output.swap(result);
    if (layout) *layout = chosen;
    return LensVisibilityResult::Applied;
}
} // namespace x3m::renderer
