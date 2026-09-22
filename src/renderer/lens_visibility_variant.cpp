#include "lens_visibility_variant.h"
#include <cstring>
#include <new>

namespace x3m::renderer {
namespace {
constexpr std::uint32_t end_token = 0x0000ffffu, comment_opcode = 0xfffeu;
enum Opcode : std::uint32_t { Nop = 0, Mov = 1, Add = 2, Mul = 5, Rcp = 6, Dp4 = 9, Call = 25, CallNz = 26, Loop = 27, Ret = 28, EndLoop = 29, Label = 30, Dcl = 31,
                              Rep = 38, EndRep = 39, If = 40, Ifc = 41, Else = 42, EndIf = 43, Break = 44, BreakC = 45, Cmp = 88, TexLd = 66,
                              Def = 81, DefI = 82, DefB = 83, BreakP = 96 };
enum Register : unsigned { Temp = 0, Const = 2, Texture = 3, RastOut = 4, TexCrdOut = 6, ColorOut = 8, Sampler = 10, Const2 = 11, Const3 = 12, Const4 = 13 };
constexpr std::uint32_t predicated = 1u << 28, relative = 1u << 13;
unsigned register_type(std::uint32_t token) noexcept { return ((token >> 28) & 7u) | ((token >> 8) & 0x18u); }
unsigned register_number(std::uint32_t token) noexcept { return token & 0x7ffu; }
std::uint32_t float_bits(float f) noexcept { std::uint32_t b = 0; std::memcpy(&b, &f, 4); return b; }
bool has_destination(std::uint32_t opcode) noexcept {
    switch (opcode) {
    case Nop: case Call: case CallNz: case Loop: case Ret: case EndLoop: case Label: case Rep: case EndRep:
    case If: case Ifc: case Else: case EndIf: case Break: case BreakC: case BreakP: return false;
    default: return true;
    }
}
// Highest index below `limit` whose bit is clear; `limit` when none.
unsigned highest_free(const std::uint32_t* bits, unsigned limit, unsigned skip = ~0u, unsigned skip2 = ~0u) noexcept {
    for (unsigned i = limit; i-- > 0;) if (i != skip && i != skip2 && !(bits[i >> 5] >> (i & 31) & 1u)) return i;
    return limit;
}
// One walk of a ps_2_x / ps_3_0 program: framing, register use, the oC0 writes, the body range.
struct PixelScan {
    bool sm3 = false;
    unsigned temporaries = 12, constants = 32, samplers = 16;
    std::uint32_t used_temp[1]{}, used_sampler[1]{}, used_const[7]{}, used_texcoord[1]{};
    std::size_t body = 0, end = 0;
};
LensVisibilityResult scan_pixel(const std::uint32_t* original, std::size_t words, PixelScan& scan) noexcept {
    if (!original || words < 2 || words > (1u << 16)) return LensVisibilityResult::InvalidInput;
    const std::uint32_t version = original[0];
    scan.sm3 = version == 0xffff0300u;
    if (!scan.sm3 && version != 0xffff0200u && version != 0xffff0201u) return LensVisibilityResult::UnsupportedVersion;
    scan.temporaries = scan.sm3 ? 32u : 12u; scan.constants = scan.sm3 ? 224u : 32u; scan.samplers = 16u;
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
            if (type == Temp) { if (number >= 32) return false; scan.used_temp[0] |= 1u << number; }
            else if (type == Const) { if (number >= 224) return false; scan.used_const[number >> 5] |= 1u << (number & 31); }
            else if (type == Sampler) { if (number >= scan.samplers) return false; scan.used_sampler[0] |= 1u << number; }
            else if (type == Texture && !scan.sm3) { if (number >= 8) return false; scan.used_texcoord[0] |= 1u << number; }
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
    scan.end = at; scan.body = body ? body : 1;
    return LensVisibilityResult::Applied;
}
// The body with every oC0 destination (always the first parameter) redirected to rO.
void copy_body_redirected(const std::uint32_t* original, std::size_t body, std::size_t end, std::uint32_t O, std::vector<std::uint32_t>& result) {
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
}
constexpr std::uint32_t dst_temp(unsigned n, unsigned mask) noexcept { return 0x80000000u | (mask << 16) | n; }
constexpr std::uint32_t src_temp(unsigned n, unsigned swizzle = 0xe4) noexcept { return 0x80000000u | (swizzle << 16) | n; }
constexpr std::uint32_t src_const(unsigned n, unsigned swizzle = 0xe4, bool negate = false) noexcept { return 0xa0000000u | (negate ? 1u << 24 : 0u) | (swizzle << 16) | n; }
constexpr std::uint32_t src_texcoord(unsigned n, unsigned swizzle = 0xe4) noexcept { return 0xb0000000u | (swizzle << 16) | n; }
constexpr std::uint32_t src_sampler(unsigned n) noexcept { return 0xa0e40800u | n; }
constexpr unsigned swz(unsigned x, unsigned y, unsigned z, unsigned w) noexcept { return x | (y << 2) | (z << 4) | (w << 6); }
LensVisibilityResult build_pixel(const std::uint32_t* original, std::size_t words, LensVisibilityScale scale, bool clip, unsigned texcoord, float dx_u, float dy_v, bool core_f,
                                 std::vector<std::uint32_t>& output, LensVisibilityLayout* layout) noexcept {
    PixelScan scan;
    const LensVisibilityResult scanned = scan_pixel(original, words, scan);
    if (scanned != LensVisibilityResult::Applied) return scanned;
    const unsigned mask = scale == LensVisibilityScale::Rgb ? 0x7u : scale == LensVisibilityScale::Alpha ? 0x8u : scale == LensVisibilityScale::Both ? 0xfu : 0u;
    if (!mask) return LensVisibilityResult::InvalidInput;
    if (clip) {
        if (scan.sm3) return LensVisibilityResult::UnsupportedVersion; // the clip pair is vs_2_x / ps_2_x (the lens scene's model)
        if (texcoord >= 8 || (scan.used_texcoord[0] >> texcoord & 1u)) return LensVisibilityResult::ResourceLimit;
        if (!(dx_u > 0.f) || !(dy_v > 0.f) || !(dx_u < .5f) || !(dy_v < .5f)) return LensVisibilityResult::InvalidInput;
    }
    LensVisibilityLayout chosen;
    chosen.sampler = highest_free(scan.used_sampler, scan.samplers); chosen.constant = highest_free(scan.used_const, scan.constants);
    chosen.output_temporary = highest_free(scan.used_temp, scan.temporaries);
    chosen.fetch_temporary = highest_free(scan.used_temp, scan.temporaries, chosen.output_temporary);
    if (chosen.sampler == scan.samplers || chosen.constant == scan.constants || chosen.output_temporary == scan.temporaries || chosen.fetch_temporary == scan.temporaries)
        return LensVisibilityResult::ResourceLimit;
    // The clip's extra registers: RT2's sampler, two constants, four temporaries (uv, sum, tap, offset uv).
    unsigned C = 0, Dc = 0, Q = 0, S = 0, T2 = 0, E = 0;
    if (clip) {
        chosen.depth_sampler = highest_free(scan.used_sampler, scan.samplers, chosen.sampler);
        C = highest_free(scan.used_const, scan.constants, chosen.constant); Dc = highest_free(scan.used_const, scan.constants, chosen.constant, C);
        Q = highest_free(scan.used_temp, scan.temporaries, chosen.output_temporary, chosen.fetch_temporary);
        std::uint32_t taken[1] = {scan.used_temp[0] | (1u << chosen.output_temporary) | (1u << chosen.fetch_temporary) | (1u << Q)};
        S = highest_free(taken, scan.temporaries); if (S < scan.temporaries) taken[0] |= 1u << S;
        T2 = highest_free(taken, scan.temporaries); if (T2 < scan.temporaries) taken[0] |= 1u << T2;
        E = highest_free(taken, scan.temporaries);
        if (chosen.depth_sampler == scan.samplers || C == scan.constants || Dc == scan.constants || Q == scan.temporaries || S == scan.temporaries || T2 == scan.temporaries || E == scan.temporaries)
            return LensVisibilityResult::ResourceLimit;
    }
    const std::uint32_t K = chosen.constant, Sm = chosen.sampler, O = chosen.output_temporary, T = chosen.fetch_temporary;
    std::vector<std::uint32_t> result;
    try { result.reserve(words + 9 + 15 + (clip ? 64 : 0)); } catch (const std::bad_alloc&) { return LensVisibilityResult::AllocationFailure; }
    result.insert(result.end(), original, original + scan.body);
    const std::uint32_t half = float_bits(.5f), one = float_bits(1.f);
    for (std::uint32_t w : {0x05000051u, 0xa00f0000u | K, half, half, 0u, one, 0x0200001fu, 0x90000000u, 0xa00f0800u | Sm}) result.push_back(w);
    if (clip) {
        // cC = (0.5, -0.5, 0.5 + dx / 2, 0.5 + dy / 2): the fragment's clip-derived uv is a texel EDGE under D3D9's pixel
        // centre convention (pixel i sits at NDC (i + 0.5) / W, so uv = i / W after the shift); the half texel puts the
        // centre tap on the texel centre on any implementation.
        for (std::uint32_t w : {0x05000051u, 0xa00f0000u | C, half, float_bits(-.5f), float_bits(.5f + .5f * dx_u), float_bits(.5f + .5f * dy_v),
                                0x05000051u, 0xa00f0000u | Dc, float_bits(dx_u), float_bits(dy_v), float_bits(1.f / 9.f), 0u,
                                0x0200001fu, 0x90000000u, 0xa00f0800u | chosen.depth_sampler,
                                0x0200001fu, 0x80000000u, 0xb00f0000u | texcoord}) result.push_back(w);
    }
    copy_body_redirected(original, scan.body, scan.end, O, result);
    // f: the 1x1 fetch.
    for (std::uint32_t w : {0x02000001u, dst_temp(T, 0xf), src_const(K), 0x03000042u, dst_temp(T, 0xf), src_temp(T), src_sampler(Sm)}) result.push_back(w);
    if (clip) {
        // uv = (t.xy / t.w) * (0.5, -0.5) + (0.5 + dx / 2, 0.5 + dy / 2), whole registers written (ps_2_0 texld reads a full temporary).
        for (std::uint32_t w : {0x02000006u, dst_temp(Q, 0x8), src_texcoord(texcoord, 0xff),
                                0x03000005u, dst_temp(Q, 0xf), src_texcoord(texcoord), src_temp(Q, 0xff),
                                0x04000004u, dst_temp(Q, 0xf), src_temp(Q), src_const(C, swz(0, 1, 2, 2)), src_const(C, swz(2, 3, 2, 2))}) result.push_back(w);
        // Nine taps (the fragment, one and two steps along +-x and +-y; the step is a whole pixel, so every tap
        // sits on a texel centre): open = 1/9 where .r < 0 (the sentinel), else 0; summed into rS.x.
        const std::uint32_t zero = src_const(K, 0xaa), ninth = src_const(Dc, 0xaa);
        auto tap = [&](std::uint32_t source, bool first) {
            for (std::uint32_t w : {0x03000042u, dst_temp(T2, 0xf), source, src_sampler(chosen.depth_sampler),
                                    0x04000058u, dst_temp(first ? S : T2, 0xf), src_temp(T2, 0x00), zero, ninth}) result.push_back(w);
            if (!first) for (std::uint32_t w : {0x03000002u, dst_temp(S, 0xf), src_temp(S), src_temp(T2)}) result.push_back(w);
        };
        tap(src_temp(Q), true);
        const unsigned dx0 = swz(0, 3, 3, 3), zdy = swz(3, 1, 3, 3);
        for (auto [swizzle, negate] : {std::pair{dx0, false}, std::pair{dx0, true}, std::pair{zdy, false}, std::pair{zdy, true}}) {
            for (std::uint32_t w : {0x03000002u, dst_temp(E, 0xf), src_temp(Q), src_const(Dc, swizzle, negate)}) result.push_back(w);
            tap(src_temp(E), false);
            for (std::uint32_t w : {0x03000002u, dst_temp(E, 0xf), src_temp(E), src_const(Dc, swizzle, negate)}) result.push_back(w);
            tap(src_temp(E), false);
        }
        // rT.y = open_px, or open_px * f when the caller asks for the product (X3M_SUN_OCCLUSION_CORE_F).
        if (core_f) for (std::uint32_t w : {0x03000005u, dst_temp(T, 0x2), src_temp(T, 0x55), src_temp(S, 0x00)}) result.push_back(w);
        else for (std::uint32_t w : {0x02000001u, dst_temp(T, 0x2), src_temp(S, 0x00)}) result.push_back(w);
    }
    for (std::uint32_t w : {0x03000005u, dst_temp(O, mask), src_temp(O), src_temp(T, 0x55), 0x02000001u, 0x800f0800u, src_temp(O), end_token}) result.push_back(w);
    output.swap(result);
    if (layout) *layout = chosen;
    return LensVisibilityResult::Applied;
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
    return build_pixel(original, words, scale, false, 8, 0.f, 0.f, false, output, layout);
}
LensVisibilityResult lens_visibility_pixel_clip_variant(const std::uint32_t* original, std::size_t words, LensVisibilityScale scale, unsigned texcoord,
                                                        float dx_u, float dy_v, bool core_f, std::vector<std::uint32_t>& output, LensVisibilityLayout* layout) noexcept {
    return build_pixel(original, words, scale, true, texcoord, dx_u, dy_v, core_f, output, layout);
}
// vs_2_x: every oPos write (RastOut 0) is one of exactly four dp4 of one temporary with c[K + lane], outside flow
// control, unmodified sources; the writes are redirected to rP and oPos / oTn are written from rP at the end.
// The position source of the dp4s must hold (position.xyz, 1) so that the local origin maps to the rows' .w column
// (sun_occlusion::core::classify_body). Two shapes are recognised, both at depth 0 and unmodified: `mov rS, vP` (vP the
// dcl_position0 input; a position stream carries w = 1: FLOAT3 is expanded so by D3D, and the engine's FLOAT4 positions
// are affine) and the fingerprinted program's `mad rS, vP.xyzx, cA.xxxy, cA.yyyx` with cA a `def` whose lanes evaluate
// to mul = (1, 1, 1, 0), add = (0, 0, 0, 1). Any other last write of rS before the first dp4, or a write to rS between
// the dp4s, leaves origin_known false: the caller then treats the body as another record's (untouched).
LensVisibilityResult lens_visibility_vertex_variant(const std::uint32_t* original, std::size_t words, unsigned texcoord,
                                                    std::vector<std::uint32_t>& output, unsigned* matrix_register, bool* origin_known) noexcept {
    if (origin_known) *origin_known = false;
    if (!original || words < 2 || words > (1u << 16) || texcoord >= 8) return LensVisibilityResult::InvalidInput;
    const std::uint32_t version = original[0];
    if (version != 0xfffe0200u && version != 0xfffe0201u) return LensVisibilityResult::UnsupportedVersion;
    std::uint32_t used_temp[1]{}, used_texcoord[1]{};
    unsigned lanes = 0, source_temp = ~0u, matrix = ~0u, depth = 0;
    unsigned position_input = ~0u;                    // dcl_position0 v<n>
    std::uint8_t origin_of[32]{};                     // per temporary: 0 unknown, 1 the last full write left (position.xyz, 1)
    float defs[256][4]{}; std::uint32_t defined[8]{}; // def constants
    bool origin = false, origin_clobbered = false;
    std::size_t at = 1, body = 0;
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
        switch (opcode) {
        case Call: case CallNz: case Ret: case Label: return LensVisibilityResult::UnsupportedShader;
        case Def:
            if (length == 5 && register_type(p[0]) == Const && register_number(p[0]) < 256) {
                const unsigned n = register_number(p[0]); defined[n >> 5] |= 1u << (n & 31);
                std::memcpy(defs[n], p + 1, 16);
            }
            break;
        case Dcl:
            if (length == 2 && (p[0] & 0x1fu) == 0 && ((p[0] >> 16) & 0xfu) == 0 && register_type(p[1]) == 1 /* input */) position_input = register_number(p[1]);
            break;
        case DefI: case DefB: break;
        default:
            for (std::size_t i = 0; i < length; ++i) {
                const std::uint32_t parameter = p[i];
                if (!(parameter & 0x80000000u)) return LensVisibilityResult::UnsupportedShader;
                const unsigned type = register_type(parameter), number = register_number(parameter);
                if (type == Temp) { if (number >= 32) return LensVisibilityResult::UnsupportedShader; used_temp[0] |= 1u << number; }
                else if (type == TexCrdOut && i == 0) { if (number >= 8) return LensVisibilityResult::UnsupportedShader; used_texcoord[0] |= 1u << number; }
            }
            // A write to a temporary: does it leave (position.xyz, 1)? Any write to the dp4 source after the first dp4 spoils it.
            if (length && has_destination(opcode) && register_type(p[0]) == Temp && !(p[0] & relative)) {
                const unsigned t = register_number(p[0]), mask = (p[0] >> 16) & 0xfu;
                if (source_temp != ~0u && t == source_temp) origin_clobbered = true;
                bool known = false;
                auto plain = [&](std::uint32_t src, unsigned type) { return register_type(src) == type && !(src & relative) && !((src >> 24) & 0xfu); };
                auto lane = [](std::uint32_t src, unsigned k) { return (src >> (16 + 2 * k)) & 3u; };
                if (mask == 0xfu && depth == 0 && !((p[0] >> 20) & 0xfu)) {
                    if (opcode == Mov && length == 2 && plain(p[1], 1) && register_number(p[1]) == position_input && ((p[1] >> 16) & 0xffu) == 0xe4u) known = true;
                    else if (opcode == 4 /* mad */ && length == 4 && plain(p[1], 1) && register_number(p[1]) == position_input && plain(p[2], Const) && plain(p[3], Const)) {
                        const unsigned a = register_number(p[2]), b = register_number(p[3]);
                        if (a < 256 && b < 256 && (defined[a >> 5] >> (a & 31) & 1u) && (defined[b >> 5] >> (b & 31) & 1u)) {
                            known = true;
                            for (unsigned k = 0; k < 4; ++k) {
                                const float mul = defs[a][lane(p[2], k)], add = defs[b][lane(p[3], k)];
                                if (k < 3) known = known && lane(p[1], k) == k && mul == 1.f && add == 0.f;
                                else known = known && mul == 0.f && add == 1.f;
                            }
                        }
                    }
                }
                origin_of[t] = known ? 1 : 0;
            }
            if (length && has_destination(opcode) && register_type(p[0]) == RastOut && register_number(p[0]) == 0) {
                // dp4 oPos.<one lane>, r<same>, c<K + lane>; no modifiers, no relative addressing, depth 0.
                const unsigned mask = (p[0] >> 16) & 0xfu, lane = mask == 1 ? 0 : mask == 2 ? 1 : mask == 4 ? 2 : mask == 8 ? 3 : 4;
                if (opcode != Dp4 || length != 3 || depth || lane == 4 || (lanes & mask) || (p[0] & relative) || ((p[0] >> 20) & 0xfu)) return LensVisibilityResult::UnsupportedShader;
                if (register_type(p[1]) != Temp || ((p[1] >> 16) & 0xffu) != 0xe4u || ((p[1] >> 24) & 0xfu) || (p[1] & relative)) return LensVisibilityResult::UnsupportedShader;
                if (register_type(p[2]) != Const || ((p[2] >> 16) & 0xffu) != 0xe4u || ((p[2] >> 24) & 0xfu) || (p[2] & relative)) return LensVisibilityResult::UnsupportedShader;
                const unsigned temp = register_number(p[1]), constant = register_number(p[2]);
                if (constant < lane) return LensVisibilityResult::UnsupportedShader;
                if (source_temp == ~0u) { source_temp = temp; matrix = constant - lane; origin = origin_of[temp] == 1; }
                else if (temp != source_temp || constant - lane != matrix) return LensVisibilityResult::UnsupportedShader;
                lanes |= mask;
                if (lanes == 0xfu) source_temp = ~0u - 1; // complete: later writes to the source no longer matter
            }
            if (opcode == If || opcode == Ifc || opcode == Loop || opcode == Rep) ++depth;
            else if (opcode == EndIf || opcode == EndLoop || opcode == EndRep) { if (!depth) return LensVisibilityResult::InvalidInput; --depth; }
        }
        at += 1 + length;
    }
    if (!ended || depth) return LensVisibilityResult::InvalidInput;
    if (lanes != 0xfu) return LensVisibilityResult::NoOutput;
    if (origin_known) *origin_known = origin && !origin_clobbered;
    if (used_texcoord[0] >> texcoord & 1u) return LensVisibilityResult::ResourceLimit;
    const unsigned P = highest_free(used_temp, 12);
    if (P == 12) return LensVisibilityResult::ResourceLimit;
    const std::size_t end = at;
    if (!body) body = 1;
    std::vector<std::uint32_t> result;
    try { result.reserve(words + 6); } catch (const std::bad_alloc&) { return LensVisibilityResult::AllocationFailure; }
    result.insert(result.end(), original, original + body);
    for (std::size_t i = body; i < end;) {
        const std::uint32_t token = original[i], opcode = token & 0xffffu;
        const bool comment = opcode == comment_opcode;
        const std::size_t length = comment ? (token >> 16) & 0x7fffu : (token >> 24) & 15u;
        result.push_back(token);
        for (std::size_t k = 0; k < length; ++k) {
            std::uint32_t parameter = original[i + 1 + k];
            if (k == 0 && !comment && opcode == Dp4 && register_type(parameter) == RastOut && register_number(parameter) == 0)
                parameter = 0x80000000u | (parameter & 0x00ff0000u) | P;
            result.push_back(parameter);
        }
        i += 1 + length;
    }
    for (std::uint32_t w : {0x02000001u, 0xc00f0000u, src_temp(P), 0x02000001u, 0xe00f0000u | texcoord, src_temp(P), end_token}) result.push_back(w);
    output.swap(result);
    if (matrix_register) *matrix_register = matrix;
    return LensVisibilityResult::Applied;
}
unsigned lens_visibility_free_texcoord(const std::uint32_t* vertex, std::size_t vertex_words, const std::uint32_t* pixel, std::size_t pixel_words) noexcept {
    PixelScan scan;
    if (scan_pixel(pixel, pixel_words, scan) != LensVisibilityResult::Applied || scan.sm3) return 8;
    if (!vertex || vertex_words < 2 || vertex_words > (1u << 16) || (vertex[0] != 0xfffe0200u && vertex[0] != 0xfffe0201u)) return 8;
    std::uint32_t used[1] = {scan.used_texcoord[0]};
    for (std::size_t at = 1; at < vertex_words;) {
        const std::uint32_t token = vertex[at];
        if (token == end_token) break;
        const std::uint32_t opcode = token & 0xffffu;
        const std::size_t length = opcode == comment_opcode ? (token >> 16) & 0x7fffu : (token >> 24) & 15u;
        if (length >= vertex_words - at) return 8;
        if (opcode != comment_opcode && opcode != Def && opcode != DefI && opcode != DefB && opcode != Dcl && length && has_destination(opcode)) {
            const std::uint32_t d = vertex[at + 1];
            if (register_type(d) == TexCrdOut && register_number(d) < 8) used[0] |= 1u << register_number(d);
        }
        at += 1 + length;
    }
    return highest_free(used, 8);
}
} // namespace x3m::renderer
