#include "linear_emission_sm1.h"
#include "shader_population.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace x3m::renderer {
namespace {
using Word = std::uint32_t;
using Words = std::vector<Word>;
constexpr unsigned temporary = 0, color = 1, constant = 2, coordinate = 3, output = 8, sampler = 10;
constexpr unsigned mov = 1, mul = 5, dp3 = 8, minimum = 10, maximum = 11, dcl = 31, power = 32, tex = 66, def = 81,
                   cmp = 88;
constexpr Word end = 0xffff, comment = 0xfffe, coissue = 0x40000000u, pp = 0x200000u;
struct Profile {
    std::uint64_t hash;
    unsigned count;
    bool scalar;
};
// clang-format off
constexpr Profile profiles[]={
    {0x078494828322bccaull,59,true},{0x2ea025492d370c8eull,59,true},
    {0xa5c3495e27270b4aull,59,true},{0x84d3de8887c963c5ull,49,false},
    {0xd4a26efb7c603931ull,49,false},{0xec1f5c4a2f4e1445ull,49,false}};
struct Pair { std::uint64_t vertex,pixel; };
constexpr Pair pairs[]={
    {0x0d44b36d48d24f7aull,0x078494828322bccaull},
    {0x637dadcb5efa3288ull,0x078494828322bccaull},
    {0x6da1b1b6ed63ec82ull,0x2ea025492d370c8eull},
    {0xed42e0742e47dca4ull,0x2ea025492d370c8eull},
    {0x88620f88d6e0a00eull,0xa5c3495e27270b4aull},
    {0xf9755e1154244f58ull,0xa5c3495e27270b4aull},
    {0x1b6863a088a177afull,0x84d3de8887c963c5ull},
    {0x21a2c13be7f989c3ull,0xd4a26efb7c603931ull},
    {0x5e484a06672e28fbull,0xec1f5c4a2f4e1445ull}};
// clang-format on
Word reg(unsigned kind, unsigned n) noexcept {
    return 0x80000000u | ((kind & 7) << 28) | ((kind & 24) << 8) | n;
}
Word dst(unsigned kind, unsigned n, unsigned mask = 7) noexcept {
    return reg(kind, n) | (mask << 16);
}
Word src(unsigned kind, unsigned n, unsigned swizzle = 0xe4, unsigned modifier = 0) noexcept {
    return reg(kind, n) | (swizzle << 16) | (modifier << 24);
}
Word lane(unsigned kind, unsigned n, unsigned c) noexcept {
    return src(kind, n, c * 0x55);
}
Word bits(float f) noexcept {
    Word v;
    std::memcpy(&v, &f, 4);
    return v;
}
unsigned kind(Word v) noexcept {
    return ((v >> 28) & 7) | ((v >> 8) & 24);
}
unsigned number(Word v) noexcept {
    return v & 0x7ff;
}
std::uint64_t fingerprint(const Word* code, std::size_t count) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < count; ++i)
        for (unsigned shift = 0; shift < 32; shift += 8) {
            h ^= (code[i] >> shift) & 255;
            h *= 0x100000001b3ull;
        }
    return h;
}
bool expect(const Word* code, std::size_t count, std::size_t& at, Word token,
            std::initializer_list<Word> operands) noexcept {
    if (at >= count || operands.size() >= count - at || code[at] != token ||
        !std::equal(operands.begin(), operands.end(), code + at + 1))
        return false;
    at += operands.size() + 1;
    return true;
}
// Exact native two-body proof independently callable by the host fixture.
// No literal shader token array: operands are constructed from their roles.
bool original_shape(const Word* code, std::size_t count, bool scalar) noexcept {
    if (!code || count != (scalar ? 59u : 49u) || code[0] != 0xffff0101u) return false;
    std::size_t at = 1;
    while (at < count && (code[at] & 0xffffu) == comment) {
        const unsigned size = (code[at] >> 16) & 0x7fff;
        if (code[at] != (comment | (size << 16)) || size > count - at - 1) return false;
        at += size + 1;
    }
    if (at != 39) return false;
    if (scalar && !expect(code, count, at, def, {dst(constant, 0, 15), bits(1), bits(0), bits(0), bits(0)}))
        return false;
    if (!expect(code, count, at, tex, {dst(coordinate, 0, 15)})) return false;
    if (scalar && !expect(code, count, at, dp3, {dst(temporary, 0), src(constant, 0), src(color, 0)})) return false;
    if (!expect(code, count, at, mul,
                {dst(temporary, 0), src(coordinate, 0), scalar ? src(temporary, 0) : lane(color, 0, 3)}) ||
        !expect(code, count, at, mov | coissue, {dst(temporary, 0, 8), lane(coordinate, 0, 3)}))
        return false;
    return at == count - 1 && code[at] == end;
}
void emit(Words& words, unsigned op, std::initializer_list<Word> operands) {
    words.push_back((static_cast<Word>(operands.size()) << 24) | op);
    words.insert(words.end(), operands.begin(), operands.end());
}
void sanitize(Words& words) {
    emit(words, maximum, {dst(temporary, 2), src(temporary, 2), lane(constant, 30, 1)});
    emit(words, minimum, {dst(temporary, 2), src(temporary, 2), lane(constant, 30, 2)});
}
void energy(Words& words, bool scalar) {
    // Same established safe source decode, decoded cap BEFORE h/gain, then S.
    emit(words, mov, {dst(temporary, 2), src(temporary, 1)});
    sanitize(words);
    emit(words, maximum, {dst(temporary, 3), src(temporary, 2), lane(constant, 30, 3)});
    for (unsigned c = 0; c < 3; ++c)
        emit(words, power, {dst(temporary, 3, 1u << c), lane(temporary, 3, c), lane(constant, 30, 0)});
    emit(words, cmp, {dst(temporary, 2), src(temporary, 2, 0xe4, 1), lane(constant, 30, 1), src(temporary, 3)});
    sanitize(words);
    emit(words, mul, {dst(temporary, 2), src(temporary, 2), lane(color, 0, scalar ? 0 : 3)});
    emit(words, mul, {dst(temporary, 2), src(temporary, 2), lane(constant, 31, 0)});
    sanitize(words);
    emit(words, mov, {dst(temporary, 2, 8), lane(constant, 30, 1)});
    emit(words, mov, {dst(output, 1, 15), src(temporary, 2)});
}
// Step E (screen-emission-region.md): M = (1, 0, 0, a) and each channel plane
// P_c = (q_c, q_c, q_c, q_c). Under ONE/INVSRCALPHA the red lane accumulates the
// native encoded value exactly as the game does and the blue lane q + (1 - q)
// old is the modified flag (nonzero once any fragment had q_c != 0 in domain);
// the green lane is masked off by the pass and keeps decode(A) from the plane
// initialization. No per-fragment decode: the pass decodes once at publication.
// Out-of-domain q (signed, > 1, HDR) is not clamped: the red lane must stay the
// native value, and a clamped copy for the blue lane would cost two more
// instructions per plane against constants; such fragments may cancel the flag
// to exactly 0 or overflow it to non-finite, and the publication then copies A
// or writes the non-finite encode. The bracket never refuses on it; the packed
// corpus keeps these as boundary rows outside qualification.
void packed_screen(Words& words) {
    emit(words, mov, {dst(temporary, 1), src(constant, 31, 0xe9)}); // (1,0,0)
    emit(words, mov, {dst(temporary, 1, 8), lane(temporary, 0, 3)});
    emit(words, mov, {dst(output, 0, 15), src(temporary, 1)});
    for (unsigned c = 0; c < 3; ++c) {
        emit(words, mov, {dst(temporary, 1, 15), lane(temporary, 0, c)});
        emit(words, mov, {dst(output, c + 1, 15), src(temporary, 1)});
    }
}
struct Budget {
    unsigned arithmetic = 0, textures = 0, outputs = 0;
};
// Bounded authored-PS2 validator: rejects unknown forms/resources, tracks
// initialized temporary lanes and exactly one full MOV per requested output.
bool generated_shape(const Words& words, Budget& budget, unsigned outputs) noexcept {
    if (words.size() < 2 || words[0] != 0xffff0200u || outputs < 1 || outputs > 4) return false;
    unsigned live[4] = {}, defs = 0, decls = 0, color_mask = 0;
    bool executable = false;
    for (std::size_t at = 1; at < words.size();) {
        const Word token = words[at];
        const unsigned op = token & 0xffff;
        if (token == end)
            return at == words.size() - 1 && budget.outputs == ((1u << outputs) - 1) && budget.textures == 1 &&
                   budget.arithmetic <= 64 && decls == 7;
        unsigned size = op == comment ? (token >> 16) & 0x7fff : (token >> 24) & 15;
        if (size > words.size() - at - 1) return false;
        if (op == comment) {
            if (executable || defs || decls || token != (comment | (size << 16))) return false;
            at += size + 1;
            continue;
        }
        if (token != ((size << 24) | op)) return false;
        if (op == def) {
            if (executable || size != 5) return false;
            const auto d = words[at + 1];
            const unsigned n = number(d);
            if ((n != 0 && n != 30 && n != 31) || d != dst(constant, n, 15) || (defs & (1u << n))) return false;
            defs |= 1u << n;
        } else if (op == dcl) {
            if (executable || size != 2) return false;
            const auto d = words[at + 2];
            unsigned bit = 0;
            if (d == dst(coordinate, 0, 3) && words[at + 1] == 0x80000000u) bit = 1;
            if ((d == dst(color, 0, 7) || d == dst(color, 0, 8)) && words[at + 1] == 0x80000000u) bit = 2;
            if (d == dst(sampler, 0, 15) && words[at + 1] == 0x90000000u) bit = 4;
            if (!bit || (decls & bit)) return false;
            decls |= bit;
            if (bit == 2) color_mask = (d >> 16) & 15;
        } else {
            executable = true;
            if (decls != 7 ||
                size != (op == mov   ? 2u
                         : op == cmp ? 4u
                                     : 3u) ||
                (op != mov && op != mul && op != dp3 && op != minimum && op != maximum && op != power && op != tex &&
                 op != cmp))
                return false;
            const Word d = words[at + 1];
            const unsigned k = kind(d), n = number(d), mask = (d >> 16) & 15;
            if (!mask || (d != dst(k, n, mask) && d != (dst(k, n, mask) | pp)) ||
                (((budget.outputs & 1) || outputs == 4) && (d & pp)))
                return false;
            if (k == temporary) {
                if (n >= 4) return false;
            } else if (k != output || n >= outputs || op != mov || d != dst(output, n, 15) ||
                       (budget.outputs & (1u << n)))
                return false;
            unsigned constant_port = 32;
            for (unsigned j = 2; j <= size; ++j) {
                const Word s = words[at + j];
                const unsigned sk = kind(s), sn = number(s), sw = (s >> 16) & 255, mod = (s >> 24) & 15;
                if (mod > 1 || s != src(sk, sn, sw, mod)) return false;
                if (sk == temporary) {
                    if (sn >= 4) return false;
                    unsigned read = 0;
                    for (unsigned c = 0; c < 4; ++c)
                        if ((op == dp3 ? 7u : mask) & (1u << c)) read |= 1u << ((sw >> (2 * c)) & 3);
                    if ((live[sn] & read) != read) return false;
                } else if (sk == constant) {
                    if (sn >= 32 || !(defs & (1u << sn)) || (constant_port != 32 && constant_port != sn)) return false;
                    constant_port = sn;
                } else if ((sk == color || sk == coordinate || sk == sampler) && sn == 0) {
                    if ((sk == coordinate || sk == sampler) && op != tex) return false;
                    if (sk == color)
                        for (unsigned c = 0; c < 4; ++c)
                            if (((op == dp3 ? 7u : mask) & (1u << c)) && !(color_mask & (1u << ((sw >> (2 * c)) & 3))))
                                return false;
                } else
                    return false;
            }
            if (k == output) {
                if (words[at + 2] != src(temporary, number(words[at + 2]))) return false;
                budget.outputs |= 1u << n;
            } else
                live[n] |= mask;
            if (op == tex) {
                if (k != temporary || words[at + 2] != src(coordinate, 0) || words[at + 3] != src(sampler, 0))
                    return false;
                ++budget.textures;
            } else {
                if (op == power)
                    for (unsigned j = 2; j <= 3; ++j) {
                        const auto s = words[at + j];
                        const unsigned sw = (s >> 16) & 255;
                        if (sw != (sw & 3) * 0x55 || (j == 3 && kind(s) == temporary && number(s) == n)) return false;
                    }
                budget.arithmetic += op == power ? 3u : 1u;
            }
        }
        at += size + 1;
    }
    return false;
}
} // namespace

bool linear_emission_sm1_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    for (const auto& p : pairs)
        if (p.vertex == vertex && p.pixel == pixel) return true;
    return false;
}
LinearEmissionResult linear_emission_sm1_pixel_variant(const Word* original, std::size_t count,
                                                       const LinearEmissionSm1Config& config,
                                                       Words& output_words) noexcept {
    if (!original || count < 2) return LinearEmissionResult::InvalidInput;
    // AdditiveGain authors the one-output native path with a colour gain
    // (validated as one output below); its gain must be finite in [1, 8].
    const bool additive = config.outputs == LinearEmissionSm1Outputs::AdditiveGain;
    const unsigned outputs = additive ? 1u : static_cast<unsigned>(config.outputs);
    if (!std::isfinite(config.gain) || config.gain < 0 || config.gain > 16 || outputs < 1 || outputs > 4 ||
        (outputs == 4 && config.native_partial_precision))
        return LinearEmissionResult::InvalidConfig;
    if (additive && (config.gain < 1 || config.gain > 8 || config.native_partial_precision))
        return LinearEmissionResult::InvalidConfig;
    if (count > 59) return LinearEmissionResult::UnsupportedShader;
    const auto hash = fingerprint(original, count);
    const Profile* profile = nullptr;
    for (const auto& p : profiles)
        if (p.hash == hash && p.count == count) {
            profile = &p;
            break;
        }
    if (!profile) return LinearEmissionResult::UnsupportedShader;
    if (!original_shape(original, count, profile->scalar)) return LinearEmissionResult::ProfileMismatch;
    try {
        Words result;
        result.reserve(outputs == 4 ? 256 : 192);
        result.push_back(0xffff0200u);
        result.insert(result.end(), original + 1, original + 39); // opaque CTAB/comments
        if (profile->scalar) emit(result, def, {dst(constant, 0, 15), bits(1), bits(0), bits(0), bits(0)});
        if (outputs > 1) {
            emit(result, def, {dst(constant, 30, 15), bits(2.2f), bits(0), bits(65504), bits(1e-10f)});
            emit(result, def,
                 {dst(constant, 31, 15), bits(config.gain == 0 ? 0 : config.gain), bits(1), bits(0), bits(0)});
        }
        if (additive) emit(result, def, {dst(constant, 31, 15), bits(config.gain), bits(1), bits(0), bits(0)});
        emit(result, dcl, {0x80000000u, dst(coordinate, 0, 3)});
        emit(result, dcl, {0x80000000u, dst(color, 0, profile->scalar ? 7 : 8)});
        emit(result, dcl, {0x90000000u, dst(sampler, 0, 15)});
        const Word precision = config.native_partial_precision ? pp : 0;
        emit(result, tex, {dst(temporary, 1, 15) | precision, src(coordinate, 0), src(sampler, 0)});
        if (profile->scalar) emit(result, dp3, {dst(temporary, 0) | precision, src(constant, 0), src(color, 0)});
        emit(result, mul,
             {dst(temporary, 0) | precision, src(temporary, 1),
              profile->scalar ? src(temporary, 0) : lane(color, 0, 3)});
        emit(result, mov, {dst(temporary, 0, 8) | precision, lane(temporary, 1, 3)});
        if (additive)
            emit(result, mul, {dst(temporary, 0), src(temporary, 0), lane(constant, 31, 0)}); // colour lanes only
        if (outputs != 4) emit(result, mov, {dst(output, 0, 15), src(temporary, 0)});
        if (outputs > 1 && outputs != 4) energy(result, profile->scalar);
        if (outputs == 4) packed_screen(result);
        if (outputs == 3) {
            emit(result, mov, {dst(temporary, 3, 15), lane(constant, 31, 1)});
            emit(result, mov, {dst(output, 2, 15), src(temporary, 3)});
        }
        result.push_back(end);
        Budget budget;
        const unsigned expected = outputs == 4 ? (profile->scalar ? 12u : 11u)
                                               : (profile->scalar ? 4u : 3u) + (outputs > 1 ? 22u : 0u) +
                                                     (outputs == 3 ? 2u : 0u) + (additive ? 1u : 0u);
        if (!generated_shape(result, budget, outputs) || budget.arithmetic != expected)
            return LinearEmissionResult::ResourceLimit;
        output_words.swap(result);
        return LinearEmissionResult::Applied;
    } catch (...) {
        return LinearEmissionResult::AllocationFailure;
    }
}
// Shader-population provider (src/renderer/shader_population.h): the six SM1
// pixel originals and the nine bullet pairs, enumerated in place.
const ShaderTable* linear_emission_sm1_shader_tables(std::size_t& count) noexcept {
    static constexpr ShaderTable tables[] = {
        {"linear_emission_sm1_pixel", sizeof profiles / sizeof profiles[0],
         [](std::size_t i) noexcept -> std::uint64_t { return profiles[i].hash; }},
        {"linear_emission_sm1_pairs", (sizeof pairs / sizeof pairs[0]) * 2,
         [](std::size_t i) noexcept -> std::uint64_t { return (i & 1u) ? pairs[i / 2].pixel : pairs[i / 2].vertex; }},
    };
    count = sizeof tables / sizeof tables[0];
    return tables;
}
} // namespace x3m::renderer
