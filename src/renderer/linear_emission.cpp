#include "linear_emission.h"
#include "shader_population.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace x3m::renderer {
namespace {
using Word = std::uint32_t;
using Words = std::vector<Word>;
constexpr Word end_token=0xffff, pp=0x200000, identity=0xe4;
constexpr unsigned temporary=0, color=1, constant=2, coordinate=3, output=8, sampler=10;
constexpr unsigned mov=1, add=2, mad=4, mul=5, dp4=9, minimum=10, maximum=11, dcl=31, power=32, texld=66, def=81, cmp=88;

// Derived whole-original identities and DWORD sites, never game shader words.
// Affine/no-affine and fade/no-fade are independent native source contracts.
enum class PixelModel : Word { Sm20 = 0xffff0200u, Sm2x = 0xffff0201u };
struct Profile {
    std::uint64_t pixel;
    unsigned words, declaration, copy_before, native_output;
    bool affine, fade;
    PixelModel model = PixelModel::Sm20;
};
constexpr Profile profiles[] = {
    {0x8360f422de08b5bdull,1108,1069,1100,1104,true,true},
    {0x9975b706e5a1c999ull,1108,1069,1100,1104,true,true},
    {0xff2473e73a6bdfa1ull,62,41,54,58,false,true},
    {0x8559522220507d5eull,1101,1069,1097,1097,true,false},
    {0x875e780adb131b16ull,55,41,51,51,false,false},
    {0x39f3b4d5b6a5aaedull,62,41,54,58,false,true},
    {0x47e15e20d63b0e93ull,62,41,54,58,false,true,PixelModel::Sm2x},
    {0x846c5c1a549f9491ull,1108,1069,1100,1104,true,true},
    {0xc6dacb8f74b65c97ull,1108,1069,1100,1104,true,true,PixelModel::Sm2x},
    {0xf0c91793a75e1203ull,1108,1069,1100,1104,true,true,PixelModel::Sm2x},
};
// Whole VS/PS identities establish the archive pair, independent of shared
// executable bodies or DEFAULT/INSTANCE names. Native VS is unchanged.
// One registry, one gain: the 2026-09-16 engine/effect family split is undone
// (docs/reverse-engineering/effect-shader-users.md: the engine glow is drawn
// by the same pair as the jump gate and the effect sprites, so no family is
// separable by shader identity).
struct Pair { std::uint64_t vertex, pixel; };
constexpr Pair pairs[] = {
    {0xd5e1c75351ed3f04ull,0x8360f422de08b5bdull},
    {0x32e75459998d0388ull,0x9975b706e5a1c999ull},
    {0x32e75459998d0388ull,0xff2473e73a6bdfa1ull},
    {0x089091aab2d5eb13ull,0x8559522220507d5eull},
    {0x089091aab2d5eb13ull,0x875e780adb131b16ull},
    {0x5b7a3ccd9e7df00aull,0x9975b706e5a1c999ull},
    {0x5b7a3ccd9e7df00aull,0xff2473e73a6bdfa1ull},
    {0x6435a84d8ac5908eull,0x39f3b4d5b6a5aaedull},
    {0x6435a84d8ac5908eull,0x47e15e20d63b0e93ull},
    {0x6435a84d8ac5908eull,0x846c5c1a549f9491ull},
    {0x6435a84d8ac5908eull,0xc6dacb8f74b65c97ull},
    {0x89193868c61c3846ull,0x8360f422de08b5bdull},
    {0x89193868c61c3846ull,0xf0c91793a75e1203ull},
    {0xa520be365951c9dcull,0x8559522220507d5eull},
    {0xa520be365951c9dcull,0x875e780adb131b16ull},
    {0xcfb2c31707d545bcull,0x39f3b4d5b6a5aaedull},
    {0xcfb2c31707d545bcull,0x47e15e20d63b0e93ull},
    {0xcfb2c31707d545bcull,0x846c5c1a549f9491ull},
    {0xcfb2c31707d545bcull,0xc6dacb8f74b65c97ull},
    {0xd5e1c75351ed3f04ull,0xf0c91793a75e1203ull},
};
static_assert(sizeof(pairs)/sizeof(pairs[0])==linear_emission_pair_count,"twenty reviewed pairs");
unsigned type(Word value) noexcept { return ((value>>28)&7)|((value>>8)&24); }
unsigned index(Word value) noexcept { return value&0x7ff; }
Word reg(unsigned kind,unsigned number) noexcept { return 0x80000000u|((kind&7)<<28)|((kind&24)<<8)|number; }
Word dst(unsigned kind,unsigned number,unsigned mask=7) noexcept { return reg(kind,number)|(mask<<16); }
Word src(unsigned kind,unsigned number,unsigned swizzle=identity,unsigned modifier=0) noexcept {
    return reg(kind,number)|(swizzle<<16)|(modifier<<24);
}
Word lane(unsigned kind,unsigned number,unsigned component) noexcept { return src(kind,number,component*0x55); }
Word bits(float value) noexcept { Word word; std::memcpy(&word,&value,sizeof word); return word; }
std::uint64_t fingerprint(const Word* words,std::size_t count) noexcept {
    std::uint64_t result=0xcbf29ce484222325ull;
    for (std::size_t i=0;i<count;++i)
        for (unsigned shift=0;shift<32;shift+=8) { result^=(words[i]>>shift)&255; result*=0x100000001b3ull; }
    return result;
}
void emit(Words& words,unsigned opcode,std::initializer_list<Word> operands) {
    words.push_back((static_cast<Word>(operands.size())<<24)|opcode);
    words.insert(words.end(),operands.begin(),operands.end());
}
struct Shape { unsigned arithmetic=0, texture=0, outputs=0; std::size_t first=0; };

// Only forms consumed or emitted here. Comments/preshaders and DEF payloads
// are opaque to instruction/register scans. PS2's separate 64 arithmetic / 32
// texture budgets apply; POW costs three slots, other admitted arithmetic one.
bool structure(const Word* code,std::size_t count,Shape& result,PixelModel model) noexcept {
    // Each exact profile selects its native version. PS2.x gets no extra
    // opcodes, swizzles, registers or cap assumptions: the selected originals
    // and authored tail fit the same conservative PS2.0 resource subset.
    if (count<2 || (model!=PixelModel::Sm20 && model!=PixelModel::Sm2x) || code[0]!=static_cast<Word>(model)) return false;
    bool executable=false, sampler_declared=false;
    for (std::size_t at=1;at<count;) {
        const auto token=code[at], op=token&0xffffu;
        if (token==end_token) return at==count-1 && result.first && result.arithmetic<=64 && result.texture<=32;
        const unsigned size=op==0xfffe?(token>>16)&0x7fff:(token>>24)&15;
        if (size>count-at-1) return false;
        if (op==0xfffe) { if (result.first) return false; at+=size+1; continue; }
        if (token!=((size<<24)|op)) return false;
        if (!result.first) result.first=at;
        if (op==def) {
            if (executable || size!=5 || code[at+1]!=dst(constant,index(code[at+1]),15) || index(code[at+1])>=32) return false;
        } else if (op==dcl) {
            if (executable || size!=2) return false;
            const auto declared=code[at+2];
            if (type(declared)==sampler) {
                if (sampler_declared || declared!=dst(sampler,0,15) || code[at+1]!=(0x80000000u|(2u<<27))) return false;
                sampler_declared=true;
            } else if (code[at+1]!=0x80000000u ||
                       (declared!=(dst(coordinate,0,3)|pp) && declared!=dst(color,0,1))) return false;
        } else {
            executable=true;
            const unsigned expected=(op==mov?2:op==cmp?4:3);
            if (size!=expected || (op!=mov && op!=mul && op!=dp4 && op!=minimum && op!=maximum && op!=power && op!=texld && op!=cmp)) return false;
            const auto target=code[at+1];
            const unsigned target_type=type(target), target_index=index(target), mask=(target>>16)&15;
            if (!mask || (target!=(dst(target_type,target_index,mask)) && target!=(dst(target_type,target_index,mask)|pp))) return false;
            if (target_type==temporary) { if (target_index>=12) return false; }
            else if (target_type==output) {
                if (target_index>2 || op!=mov || mask!=15 || (result.outputs&(1u<<target_index)) ||
                    (target_index!=0 && target!=dst(output,target_index,15))) return false;
                result.outputs|=1u<<target_index;
                // SM2 output MOV uses a full, unmodified temporary source.
                if (code[at+2]!=src(temporary,index(code[at+2]))) return false;
            } else return false;
            unsigned constant_read=32;
            for (unsigned i=2;i<=size;++i) {
                const auto value=code[at+i];
                const unsigned kind=type(value), number=index(value), modifier=(value>>24)&15;
                if (!(value&0x80000000u) || (value&0x2000u) || modifier>1) return false;
                if (value!=src(kind,number,(value>>16)&255,modifier)) return false;
                if (kind==temporary) { if (number>=12) return false; }
                else if (kind==constant) {
                    if (number>=32 || (constant_read!=32 && constant_read!=number)) return false;
                    constant_read=number;
                } else if ((kind==color || kind==coordinate || kind==sampler) && number==0) {
                    if ((kind==coordinate || kind==sampler) && op!=texld) return false;
                } else return false;
            }
            if (op==texld) {
                if (!sampler_declared || code[at+2]!=src(coordinate,0) || code[at+3]!=src(sampler,0)) return false;
                ++result.texture;
            } else {
                if (op==power) {
                    // PS2 POW requires replicated scalar sources; its exponent
                    // must not share the destination temporary register.
                    if (target_type!=temporary ||
                        (type(code[at+3])==temporary && index(code[at+3])==target_index)) return false;
                    for (unsigned operand=2;operand<=3;++operand) {
                        const unsigned swizzle=(code[at+operand]>>16)&255;
                        if (swizzle!=(swizzle&3)*0x55) return false;
                    }
                }
                result.arithmetic+=op==power?3:1;
            }
        }
        at+=size+1;
    }
    return false;
}

bool expect(const Word* code,std::size_t count,std::size_t& at,unsigned opcode,
            std::initializer_list<Word> operands) noexcept {
    if (at>=count || operands.size()>=count-at || code[at]!=((operands.size()<<24)|opcode) ||
        !std::equal(operands.begin(),operands.end(),code+at+1)) return false;
    at+=operands.size()+1;
    return true;
}
bool original_shape(const Word* code,const Profile& profile,const Shape& shape) noexcept {
    std::size_t at=shape.first;
    if (profile.affine && !expect(code,profile.words,at,def,{dst(constant,3,15),bits(1),bits(0),bits(0),bits(0)})) return false;
    if (at!=profile.declaration ||
        !expect(code,profile.words,at,dcl,{0x80000000u,dst(coordinate,0,3)|pp})) return false;
    if (profile.fade && !expect(code,profile.words,at,dcl,{0x80000000u,dst(color,0,1)})) return false;
    if (!expect(code,profile.words,at,dcl,{0x80000000u|(2u<<27),dst(sampler,0,15)}) ||
        !expect(code,profile.words,at,texld,{dst(temporary,0,15)|pp,src(coordinate,0),src(sampler,0)})) return false;
    if (profile.affine) {
        if (!expect(code,profile.words,at,mov,{dst(temporary,1)|pp,src(temporary,0)}) ||
            !expect(code,profile.words,at,mov,{dst(temporary,1,8),lane(constant,3,0)})) return false;
        for (unsigned component=0;component<3;++component)
            if (!expect(code,profile.words,at,dp4,{dst(temporary,0,1u<<component)|pp,src(temporary,1),src(constant,component)})) return false;
    }
    if (at!=profile.copy_before) return false;
    if (profile.fade && !expect(code,profile.words,at,mul,{dst(temporary,0)|pp,src(temporary,0),lane(color,0,0)})) return false;
    return at==profile.native_output &&
        expect(code,profile.words,at,mov,{dst(output,0,15)|pp,src(temporary,0)}) &&
        at==profile.words-1 && shape.outputs==1 && shape.texture==1 &&
        shape.arithmetic==(profile.affine?6u:1u)+(profile.fade?1u:0u);
}
void sanitize(Words& words) {
    // Ordered first operands implement S; MAX/MIN alone need not canonicalize
    // negative zero. The decode selection below explicitly preserves +0.
    emit(words,maximum,{dst(temporary,2),src(temporary,2),lane(constant,30,1)});
    emit(words,minimum,{dst(temporary,2),src(temporary,2),lane(constant,30,2)});
}
void source_output(Words& words,bool fade) {
    sanitize(words);
    emit(words,maximum,{dst(temporary,3),src(temporary,2),lane(constant,30,3)});
    for (unsigned component=0;component<3;++component)
        emit(words,power,{dst(temporary,3,1u<<component),lane(temporary,3,component),lane(constant,30,0)});
    emit(words,cmp,{dst(temporary,2),src(temporary,2,identity,1),lane(constant,30,1),src(temporary,3)});
    // This decoded-result cap precedes fade/gain. It is intentionally distinct
    // from the uncapped intermediate radiance policy in opaque materials.
    sanitize(words);
    if (fade) emit(words,mul,{dst(temporary,2),src(temporary,2),lane(color,0,0)});
    emit(words,mul,{dst(temporary,2),src(temporary,2),lane(constant,31,0)});
    sanitize(words);
    emit(words,mov,{dst(temporary,2,8),lane(constant,30,1)});
    emit(words,mov,{dst(output,1,15),src(temporary,2)});
}

// Phase 3 (docs/architecture/emitter-plan.md): the twelve exact ps_3_0 hull
// originals that draw the ADD ONE/ONE emitters the effects gain cannot reach
// (docs/reverse-engineering/effect-shader-users.md, "Additive emitters drawn
// by material programs"): six XT_standard_lighting and six standard_lighting
// material programs. Derived whole-original identities and DWORD sites, never
// game shader words. `definition` is the first DEF/DCL dword (the CTAB comment
// ends there) and `emission` the final colour instruction (`add oC0.xyz, r1,
// r0` or, XT, `mad oC0.xyz, r1, r2.z, r0`). The emitter art of every ONE/ONE
// material sits in the diffuse slot with the lightmap slot (the r0 sample)
// black or unbound (archive check, 2026-09-17), so the gain scales the whole
// colour output of the draw, not the r0 sample: the final colour instruction
// is redirected to r0 (dead after it: only the alpha MUL follows, reading r2.w
// and v0.w) and one `mul oC0.xyz, r0, c223.x` follows it.
struct HullProfile { std::uint64_t pixel; unsigned words, definition, emission; bool modulated; };
constexpr HullProfile hull_profiles[] = {
    {0x5f82ecacd39529cdull,1765,1307,1755,true},
    {0x6733b119142c8d42ull,1754,1307,1744,true},
    {0xfffdabd910793abaull,1648,1262,1638,true},
    {0x496049cec2066ed3ull,1780,1307,1770,true},
    {0xe6794b6ec37ff71aull,1674,1262,1664,true},
    {0xf1b0e820c7b488c3ull,1791,1307,1781,true},
    {0x0c1f3f0f440e4a0cull,1366,1100,1357,false},
    {0x7c83ed50c9894e44ull,1298,1091,1289,false},
    {0x99153c144030c396ull,1355,1100,1346,false},
    {0x64bac8bb307eb896ull,1392,1100,1383,false},
    {0xc1452981fd0bff64ull,1381,1100,1372,false},
    {0xe70adc744a38ca59ull,1324,1091,1315,false},
};
static_assert(sizeof(hull_profiles)/sizeof(hull_profiles[0])==linear_emission_hull_program_count,"twelve reviewed hull programs");
// The gain lives in the last ps_3_0 float constant, outside every constant the
// originals read (max c26) and outside the linear-material/exposure/fill
// reservations (c204..c222). A program that reads c223 is refused.
constexpr unsigned hull_gain_constant = 223, hull_emission_temporary = 0, sm3_pixel_slots = 512;
constexpr Word sm3_pixel = 0xffff0300u;
// Narrow SM3 walk: instruction boundaries from the length field, comments and
// DEF/DCL payloads excluded from the register scan. It establishes the sites
// and refuses relative addressing or any read of the gain constant.
// Documented ps_3_0 executable budget: 512 slots, macro instructions at their
// documented cost (the SM2 path's `structure` applies the 64/32 PS2 budgets the
// same way, and the Python oracle mirrors this table).
unsigned slot_cost(unsigned op) noexcept {
    switch (op) {
    case 18: case 24: case 33: return 2;                 // lrp, m3x2, crs
    case 21: case 23: case power: case 36: return 3;     // m4x3, m3x3, pow, nrm
    case 20: case 22: return 4;                          // m4x4, m3x4
    case 37: return 8;                                   // sincos
    default: return 1;
    }
}
bool texture_op(unsigned op) noexcept { return op==65 || op==texld || op==93 || op==95; }
bool hull_structure(const Word* code,std::size_t count,std::size_t& first_declaration,
                    unsigned& instructions,bool original=true) noexcept {
    if (count<2 || code[0]!=sm3_pixel) return false;
    first_declaration=0; instructions=0;
    unsigned slots=0;
    for (std::size_t at=1;at<count;) {
        const Word token=code[at];
        if (token==end_token) return at==count-1 && first_declaration!=0 && instructions!=0 && slots<=sm3_pixel_slots;
        const unsigned op=token&0xffffu;
        if (op==0xffffu) return false;
        if (op==0xfffeu) { // comment block: opaque payload, no registers
            if (first_declaration) return false;
            const unsigned size=(token>>16)&0x7fffu;
            if (size>count-at-1) return false;
            at+=size+1;
            continue;
        }
        const unsigned size=(token>>24)&15;
        if (size>count-at-1 || (token&0xf0ff0000u)) return false;
        ++instructions;
        if (op==def || op==dcl) {
            if (size!=(op==def?5u:2u)) return false;
            if (!first_declaration) first_declaration=at;
            if (original && op==def && index(code[at+1])==hull_gain_constant) return false;
            if (op==dcl && (code[at+2]&0x2000u)) return false;
        } else {
            slots+=texture_op(op) ? 1u : slot_cost(op);
            for (unsigned i=1;i<=size;++i) {
                const Word value=code[at+i];
                if (!(value&0x80000000u) || (value&0x2000u)) return false; // no relative addressing
                if (original && type(value)==constant && index(value)==hull_gain_constant) return false;
            }
        }
        at+=size+1;
    }
    return false;
}
const HullProfile* hull_profile_of(const Word* original,std::size_t count) noexcept {
    const auto hash=fingerprint(original,count);
    for (const auto& profile:hull_profiles)
        if (profile.pixel==hash && profile.words==count) return &profile;
    return nullptr;
}
// The pinned tail: the final colour instruction, then the native alpha MUL and
// the end token. Nothing after the colour site is rewritten except that
// instruction's destination; the alpha MUL and the end token are copied.
// `transformed`: the colour instruction writes r0.xyz (its _pp and mask kept)
// and the gain MUL writes oC0.xyz with the original destination token.
const Word hull_colour_destination=dst(output,0,7)|pp;
bool hull_tail(const Word* code,const HullProfile& profile,bool transformed=false) noexcept {
    const Word colour_destination=transformed ? (dst(temporary,hull_emission_temporary,7)|pp) : hull_colour_destination;
    const Word gain[] = {(3u<<24)|mul,hull_colour_destination,src(temporary,hull_emission_temporary),
                         lane(constant,hull_gain_constant,0)};
    const Word alpha[] = {(3u<<24)|mul,dst(output,0,8)|pp,lane(temporary,2,3),lane(color,0,3)};
    std::size_t at=profile.emission;
    if (profile.modulated) {
        const Word site[] = {(4u<<24)|mad,colour_destination,src(temporary,1),lane(temporary,2,2),
                             src(temporary,hull_emission_temporary)};
        if (!std::equal(std::begin(site),std::end(site),code+at)) return false;
        at+=sizeof site/sizeof site[0];
    } else {
        const Word site[] = {(3u<<24)|add,colour_destination,src(temporary,1),
                             src(temporary,hull_emission_temporary)};
        if (!std::equal(std::begin(site),std::end(site),code+at)) return false;
        at+=sizeof site/sizeof site[0];
    }
    if (transformed) {
        if (!std::equal(std::begin(gain),std::end(gain),code+at)) return false;
        at+=sizeof gain/sizeof gain[0];
    }
    return std::equal(std::begin(alpha),std::end(alpha),code+at) &&
           at+sizeof alpha/sizeof alpha[0]==profile.words-1;
}
} // namespace

bool linear_emission_config_valid(const LinearEmissionConfig& config) noexcept {
    return std::isfinite(config.gain) && config.gain>=0 && config.gain<=16;
}
bool linear_emission_source_gain_valid(float gain) noexcept {
    return std::isfinite(gain) && gain>=1 && gain<=8;
}
SourceGainBlend linear_emission_source_gain_blend(std::uint32_t blend_enable,std::uint32_t srgb_write,
    std::uint32_t src,std::uint32_t dst,std::uint32_t op) noexcept {
    constexpr std::uint32_t blend_one=2, blend_inv_src_color=4, op_add=1; // D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD
    if (!blend_enable || srgb_write || src!=blend_one || op!=op_add) return SourceGainBlend::Blend;
    if (dst==blend_inv_src_color) return SourceGainBlend::Screen;
    if (dst!=blend_one) return SourceGainBlend::Blend;
    return SourceGainBlend::Admit;
}
unsigned linear_emission_pair_index(std::uint64_t vertex,std::uint64_t pixel) noexcept {
    for (unsigned i=0;i<linear_emission_pair_count;++i)
        if (pairs[i].vertex==vertex && pairs[i].pixel==pixel) return i;
    return linear_emission_pair_count;
}
bool linear_emission_pair_reviewed(std::uint64_t vertex,std::uint64_t pixel) noexcept {
    return linear_emission_pair_index(vertex,pixel)<linear_emission_pair_count;
}
LinearEmissionResult linear_emission_pixel_variant(const Word* original,std::size_t count,
    const LinearEmissionConfig& config,Words& output_words) noexcept {
    if (!original || count<2) return LinearEmissionResult::InvalidInput;
    if (!linear_emission_config_valid(config)) return LinearEmissionResult::InvalidConfig;
    if (count>1108) return LinearEmissionResult::UnsupportedShader;
    const auto hash=fingerprint(original,count);
    const Profile* selected=nullptr;
    for (const auto& profile:profiles) if (profile.pixel==hash && profile.words==count) { selected=&profile; break; }
    if (!selected) return LinearEmissionResult::UnsupportedShader;
    const auto& profile=*selected;
    Shape original_structure;
    if (!structure(original,count,original_structure,profile.model) || !original_shape(original,profile,original_structure)) return LinearEmissionResult::ProfileMismatch;
    try {
        Words result;
        result.reserve(count+(config.coverage?88:82));
        result.insert(result.end(),original,original+profile.declaration);
        emit(result,def,{dst(constant,30,15),bits(2.2f),bits(0),bits(65504),bits(1e-10f)});
        emit(result,def,{dst(constant,31,15),bits(config.gain==0?0:config.gain),bits(config.coverage?1.0f:0.0f),bits(0),bits(0)});
        result.insert(result.end(),original+profile.declaration,original+profile.copy_before);
        emit(result,mov,{dst(temporary,2),src(temporary,0)});
        result.insert(result.end(),original+profile.copy_before,original+count-1);
        source_output(result,profile.fade);
        if (config.coverage) {
            // r3 is dead after emission output. Constant coverage deliberately
            // ignores radiance, fade, native alpha and gain, including zero.
            // PS2 color outputs require a full unmodified temporary MOV.
            emit(result,mov,{dst(temporary,3,15),lane(constant,31,1)});
            emit(result,mov,{dst(output,2,15),src(temporary,3)});
        }
        result.push_back(end_token);
        Shape transformed;
        if (!structure(result.data(),result.size(),transformed,profile.model) || transformed.outputs!=(config.coverage?7u:3u) || transformed.texture!=1 ||
            transformed.arithmetic!=original_structure.arithmetic+(profile.fade?22u:21u)+(config.coverage?2u:0u)) return LinearEmissionResult::ResourceLimit;
        output_words.swap(result);
        return LinearEmissionResult::Applied;
    } catch (...) { return LinearEmissionResult::AllocationFailure; }
}
LinearEmissionResult linear_emission_source_gain_variant(const Word* original,std::size_t count,
    float gain,Words& output_words) noexcept {
    if (!original || count<2) return LinearEmissionResult::InvalidInput;
    if (!linear_emission_source_gain_valid(gain)) return LinearEmissionResult::InvalidConfig;
    if (count>1108) return LinearEmissionResult::UnsupportedShader;
    const auto hash=fingerprint(original,count);
    const Profile* selected=nullptr;
    for (const auto& profile:profiles) if (profile.pixel==hash && profile.words==count) { selected=&profile; break; }
    if (!selected) return LinearEmissionResult::UnsupportedShader;
    const auto& profile=*selected;
    Shape original_structure;
    if (!structure(original,count,original_structure,profile.model) || !original_shape(original,profile,original_structure)) return LinearEmissionResult::ProfileMismatch;
    try {
        Words result;
        if (gain==1) {
            // Byte identity: the option at gain 1 is the native program.
            result.assign(original,original+count);
            output_words.swap(result);
            return LinearEmissionResult::Applied;
        }
        result.reserve(count+10);
        result.insert(result.end(),original,original+profile.declaration);
        emit(result,def,{dst(constant,31,15),bits(gain),bits(0),bits(0),bits(0)});
        result.insert(result.end(),original+profile.declaration,original+profile.native_output);
        // Colour lanes only; the native alpha in r0.w reaches oC0 unchanged.
        emit(result,mul,{dst(temporary,0),src(temporary,0),lane(constant,31,0)});
        result.insert(result.end(),original+profile.native_output,original+count);
        Shape transformed;
        if (!structure(result.data(),result.size(),transformed,profile.model) || transformed.outputs!=1u || transformed.texture!=1 ||
            transformed.arithmetic!=original_structure.arithmetic+1u) return LinearEmissionResult::ResourceLimit;
        output_words.swap(result);
        return LinearEmissionResult::Applied;
    } catch (...) { return LinearEmissionResult::AllocationFailure; }
}
unsigned linear_emission_hull_program_index(std::uint64_t pixel) noexcept {
    for (unsigned i=0;i<linear_emission_hull_program_count;++i)
        if (hull_profiles[i].pixel==pixel) return i;
    return linear_emission_hull_program_count;
}
bool linear_emission_hull_program_reviewed(std::uint64_t pixel) noexcept {
    return linear_emission_hull_program_index(pixel)<linear_emission_hull_program_count;
}
SourceGainBlend linear_emission_hull_source_gain_blend(std::uint32_t blend_enable,std::uint32_t srgb_write,
    std::uint32_t src,std::uint32_t dst,std::uint32_t op) noexcept {
    constexpr std::uint32_t blend_one=2, op_add=1; // D3DBLEND_ONE, D3DBLENDOP_ADD
    if (!blend_enable || srgb_write || src!=blend_one || dst!=blend_one || op!=op_add) return SourceGainBlend::Blend;
    return SourceGainBlend::Admit;
}
LinearEmissionResult linear_emission_hull_source_gain_variant(const Word* original,std::size_t count,
    float gain,Words& output_words) noexcept {
    if (!original || count<2) return LinearEmissionResult::InvalidInput;
    if (!linear_emission_source_gain_valid(gain)) return LinearEmissionResult::InvalidConfig;
    const auto* selected=hull_profile_of(original,count);
    if (!selected) return LinearEmissionResult::UnsupportedShader;
    const auto& profile=*selected;
    std::size_t first_declaration=0; unsigned instructions=0;
    if (!hull_structure(original,count,first_declaration,instructions) ||
        first_declaration!=profile.definition || !hull_tail(original,profile))
        return LinearEmissionResult::ProfileMismatch;
    try {
        Words result;
        if (gain==1) {
            // Byte identity: the option at gain 1 is the native program.
            result.assign(original,original+count);
            output_words.swap(result);
            return LinearEmissionResult::Applied;
        }
        result.reserve(count+10);
        result.insert(result.end(),original,original+profile.definition);
        emit(result,def,{dst(constant,hull_gain_constant,15),bits(gain),bits(0),bits(0),bits(0)});
        result.insert(result.end(),original+profile.definition,original+profile.emission);
        // Whole colour output of the draw: the final colour instruction keeps
        // its opcode, _pp, mask and operands but writes r0.xyz (r0 is dead
        // after it; the pinned tail proves only the alpha MUL follows, which
        // reads r2.w and v0.w), then one MUL scales it into oC0.xyz with the
        // original destination token (same _pp). r0.w, r1 (the lit term as
        // computed), r2.w and the native alpha MUL are untouched, so the
        // draw's alpha lane and every other emitted original word are verbatim.
        const std::size_t site=profile.modulated ? 5u : 4u;
        result.push_back(original[profile.emission]);
        result.push_back(dst(temporary,hull_emission_temporary,7)|pp);
        result.insert(result.end(),original+profile.emission+2,original+profile.emission+site);
        emit(result,mul,{hull_colour_destination,src(temporary,hull_emission_temporary),
                         lane(constant,hull_gain_constant,0)});
        result.insert(result.end(),original+profile.emission+site,original+count);
        std::size_t transformed_declaration=0; unsigned transformed_instructions=0;
        HullProfile moved=profile;
        moved.definition=profile.definition; moved.emission=profile.emission+6; moved.words=profile.words+10;
        if (result.size()!=count+10 || !hull_structure(result.data(),result.size(),transformed_declaration,transformed_instructions,false) ||
            transformed_declaration!=profile.definition || transformed_instructions!=instructions+2 ||
            !hull_tail(result.data(),moved,true)) return LinearEmissionResult::ResourceLimit;
        output_words.swap(result);
        return LinearEmissionResult::Applied;
    } catch (...) { return LinearEmissionResult::AllocationFailure; }
}
// Shader-population provider (src/renderer/shader_population.h): the twenty
// linear-emission pairs and their pixel originals, enumerated in place.
const ShaderTable* linear_emission_shader_tables(std::size_t& count) noexcept {
    static constexpr ShaderTable tables[] = {
        {"linear_emission_pixel", sizeof profiles / sizeof profiles[0],
         [](std::size_t i) noexcept -> std::uint64_t { return profiles[i].pixel; }},
        {"linear_emission_pairs", (sizeof pairs / sizeof pairs[0]) * 2,
         [](std::size_t i) noexcept -> std::uint64_t {
             return (i & 1u) ? pairs[i / 2].pixel : pairs[i / 2].vertex;
         }},
    };
    count = sizeof tables / sizeof tables[0];
    return tables;
}
} // namespace x3m::renderer
