#include "linear_material.h"
#include "material_motion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace x3m::renderer {
namespace {
using Word = std::uint32_t;
using Words = std::vector<Word>;
constexpr Word end_token = 0xffffu, relative = 0x2000u, pp = 0x200000u, sat = 0x100000u;
constexpr unsigned temp = 0, input = 1, constant = 2, output_reg = 6, color_output = 8;
constexpr unsigned mov = 1, add = 2, mad = 4, mul = 5, min_op = 10, max_op = 11;
constexpr unsigned slt = 12, dcl = 31, pow_op = 32, abs_op = 35, texld = 66, def = 81, cmp = 88;
constexpr unsigned xyz = 7, xyzw = 15, identity = 0xe4;

// Derived original-program contracts, including comments and END. The complete
// original-site proof is docs/reverse-engineering/linear-material-profiles.json.
// Only identities/offsets/resource facts are recorded here, never game bytes.
struct Pixel {
    std::uint64_t hash;
    unsigned words;
    std::array<unsigned, 4> texture; // s0, s1(data), s2, s3
    unsigned affine_end, clamp, final_rgb, clamp_temporary;
    unsigned light0, light1; // light1 == 0 means the one-directional contract.
    std::array<unsigned, 4> color_source; // ORIGINAL source operand DWORDs.
    std::array<unsigned, 13> rgb; // Full-precision radiance destinations only.
};
constexpr Pixel pixels[] = {
    {0x8759c7838bbc86c2ull,1260,{1197,1175,1242,1229},1217,1206,1251,1,5,7,
     {1173,1186,1161,1169},{1158,1166,1170,1183,1188,1192,1206,1221,1225,1233,1237,1251}},
    {0x63f96eba9eea7880ull,1292,{1229,1207,1274,1261},1249,1238,1283,1,5,7,
     {1205,1218,1193,1201},{1190,1198,1202,1215,1220,1224,1238,1253,1257,1265,1269,1283}},
    {0x593e5dea9b3457d5ull,264,{225,204,246,233},0,217,255,1,2,0,
     {223,0,0,0},{217,220,229,237,241,255,0,0,0,0,0,0}},
    {0x7a0bb00a8070496aull,1215,{1151,1138,1197,1184},1171,1160,1206,1,5,0,
     {1178,0,0,0},{1160,1175,1180,1188,1192,1206,0,0,0,0,0,0}},
    {0x8d5b2ba0fb4d13bfull,1183,{1119,1106,1165,1152},1139,1128,1174,1,5,0,
     {1146,0,0,0},{1128,1143,1148,1156,1160,1174,0,0,0,0,0,0}},
    {0xdab93928f26906f7ull,296,{257,236,278,265},0,249,287,1,2,0,
     {255,0,0,0},{249,252,261,269,273,287,0,0,0,0,0,0}},
    {0x3b94320087e81945ull,1264,{1192,1175,1246,1233},1214,1218,1255,2,5,7,
     {1173,1186,1161,1169},{1158,1166,1170,1183,1188,1201,1218,1221,1225,1229,1237,1241,1255}},
    {0xe3b7acc16da9932dull,1296,{1224,1207,1278,1265},1246,1250,1287,2,5,7,
     {1205,1218,1193,1201},{1190,1198,1202,1215,1220,1233,1250,1253,1257,1261,1269,1273,1287}},
    {0x7a14d4dcb28f27e5ull,1187,{1114,1106,1169,1156},1136,1140,1178,1,5,0,
     {1150,0,0,0},{1140,1143,1147,1152,1160,1164,1178}},
    {0x8ab6188a40ca15eaull,1219,{1146,1138,1201,1188},1168,1172,1210,1,5,0,
     {1182,0,0,0},{1172,1175,1179,1184,1192,1196,1210}},
    {0x8df6143d0e77d92eull,268,{220,204,250,237},0,217,259,2,2,0,
     {231,0,0,0},{217,224,228,233,241,245,259}},
    {0xe16a9806ee3544c3ull,300,{252,236,282,269},0,249,291,2,2,0,
     {263,0,0,0},{249,256,260,265,273,277,291}},
};
struct Vertex { std::uint64_t hash; unsigned words; bool loop; };
constexpr Vertex vertices[] = {{0x53a0a641107ed76cull,526,true},
    {0x719856ce0c213220ull,526,true},{0xbadefd5143b3024full,481,false}};
// Explicit archive pair contract: base shaders never gain toggle-VS admission
// from table position. This bounded per-draw lookup allocates no memory.
struct Pair { std::uint64_t vertex, pixel; };
constexpr Pair pairs[] = {
    {0x53a0a641107ed76cull,0x8759c7838bbc86c2ull},
    {0x53a0a641107ed76cull,0x63f96eba9eea7880ull},
    {0x53a0a641107ed76cull,0x3b94320087e81945ull},
    {0x53a0a641107ed76cull,0xe3b7acc16da9932dull},
    {0x719856ce0c213220ull,0x593e5dea9b3457d5ull},
    {0x719856ce0c213220ull,0x7a0bb00a8070496aull},
    {0x719856ce0c213220ull,0x8d5b2ba0fb4d13bfull},
    {0x719856ce0c213220ull,0xdab93928f26906f7ull},
    {0x719856ce0c213220ull,0x7a14d4dcb28f27e5ull},
    {0x719856ce0c213220ull,0x8ab6188a40ca15eaull},
    {0x719856ce0c213220ull,0x8df6143d0e77d92eull},
    {0x719856ce0c213220ull,0xe16a9806ee3544c3ull},
    {0xbadefd5143b3024full,0x593e5dea9b3457d5ull},
    {0xbadefd5143b3024full,0x7a0bb00a8070496aull},
    {0xbadefd5143b3024full,0x8d5b2ba0fb4d13bfull},
    {0xbadefd5143b3024full,0xdab93928f26906f7ull},
    {0xbadefd5143b3024full,0x7a14d4dcb28f27e5ull},
    {0xbadefd5143b3024full,0x8ab6188a40ca15eaull},
    {0xbadefd5143b3024full,0x8df6143d0e77d92eull},
    {0xbadefd5143b3024full,0xe16a9806ee3544c3ull},
};
unsigned kind(Word token) noexcept { return ((token >> 28) & 7) | ((token >> 8) & 24); }
unsigned index(Word token) noexcept { return token & 0x7ff; }
unsigned mask(Word token) noexcept { return (token >> 16) & 15; }
unsigned length(Word token) noexcept { return (token & 0xffff) == 0xfffe ? (token >> 16) & 0x7fff : (token >> 24) & 15; }
Word reg(unsigned type, unsigned number) noexcept { return 0x80000000u | ((type & 7u) << 28) | ((type & 24u) << 8) | number; }
Word dst(unsigned type, unsigned number, unsigned lanes = xyz) noexcept { return reg(type, number) | (lanes << 16); }
Word src(unsigned type, unsigned number, unsigned sw = identity, unsigned modifier = 0) noexcept {
    return reg(type, number) | (sw << 16) | (modifier << 24);
}
Word lane(unsigned type, unsigned number, unsigned component) noexcept { return src(type, number, component * 0x55); }
Word bits(float value) noexcept { Word result; std::memcpy(&result, &value, sizeof result); return result; }
float canonical_gain(float value) noexcept { return value == 0.0f ? 0.0f : value; }
void emit(Words& out, unsigned opcode, std::initializer_list<Word> operands) {
    out.push_back((static_cast<Word>(operands.size()) << 24) | opcode);
    out.insert(out.end(), operands.begin(), operands.end());
}
void definitions(Words& out, bool vertex, const LinearMaterialConfig& config) {
    const auto base = vertex ? 248u : 212u;
    emit(out, def, {dst(constant,base,xyzw),bits(2.2f),bits(0.0f),bits(65504.0f),bits(1e-10f)});
    emit(out, def, {dst(constant,base+1,xyzw),bits(canonical_gain(config.direct_gain)),
        bits(vertex ? canonical_gain(config.material_emissive_gain) : 1.0f/2.2f),
        bits(vertex ? 0.0f : canonical_gain(config.lightmap_emissive_gain)),bits(vertex ? 0.0f : 1e-22f)});
}
struct Source { Word value, address = 0; };
void sanitize(Words& out, bool vertex, unsigned target, Source source) {
    const unsigned base = vertex ? 248 : 212;
    // Input first in MAX/MIN is intentional: DX9's documented ordered
    // comparisons map NaN/-Inf to +0 and +Inf to the finite source cap.
    if (source.value & relative)
        emit(out,max_op,{dst(temp,target),source.value,source.address,lane(constant,base,1)});
    else emit(out,max_op,{dst(temp,target),source.value,lane(constant,base,1)});
    emit(out,min_op,{dst(temp,target),src(temp,target),lane(constant,base,2)});
}
void transfer(Words& out, bool vertex, unsigned target, Source source, bool encode = false) {
    const unsigned base = vertex ? 248 : 212, scratch = vertex ? 8 : 9;
    sanitize(out,vertex,target,source);
    // VS3 has no CMP. A strict-positive SLT mask times a finite positive POW
    // yields exact +0 for either signed zero without evaluating POW(0,...).
    if (vertex) emit(out,slt,{dst(temp,9),lane(constant,base,1),src(temp,target)});
    emit(out,max_op,{dst(temp,scratch),src(temp,target),lane(constant,encode ? base+1 : base,3)});
    for (unsigned component=0; component<3; ++component)
        emit(out,pow_op,{dst(temp,scratch,1u<<component),lane(temp,scratch,component),
                        lane(constant,encode ? base+1 : base,encode ? 1 : 0)});
    if (vertex) emit(out,mul,{dst(temp,target),src(temp,scratch),src(temp,9)});
    else emit(out,cmp,{dst(temp,target),src(temp,target,identity,1),lane(constant,base,1),src(temp,scratch)});
}
void gain(Words& out, bool vertex, unsigned target, unsigned component) {
    emit(out,mul,{dst(temp,target),src(temp,target),lane(constant,vertex ? 249 : 213,component)});
}

struct Instruction { std::size_t at; unsigned opcode, count; };
struct Structure {
    std::vector<Instruction> instructions;
    std::vector<unsigned char> boundary;
    std::size_t first_declaration = 0;
};
// This narrow SM3 walk also excludes comments/DEF literal words from register
// scans. The existing motion transformer performs its independent full proof.
bool structure(const Word* code, std::size_t words, bool vertex, Structure& result, bool original) {
    if (words < 2 || code[0] != (vertex ? 0xfffe0300u : 0xffff0300u)) return false;
    result.boundary.assign(words,0);
    unsigned slots=0;
    for (std::size_t at=1; at<words;) {
        result.boundary[at]=1;
        const Word token=code[at]; const unsigned op=token&0xffff, n=length(token);
        if (token==end_token) return at==words-1 && slots<=512 && result.first_declaration!=0;
        if (op==0xffff || n>words-at-1 || (op!=0xfffe && (token & 0x50000000u))) return false;
        if (op==0xfffe) { at+=n+1; continue; }
        result.instructions.push_back({at,op,n});
        if (op==dcl) {
            if (n!=2) return false;
            if (!result.first_declaration) result.first_declaration=at;
            const auto type=kind(code[at+2]), number=index(code[at+2]);
            if (original && ((type==(vertex ? output_reg : input) && number==(vertex ? 8u : 7u)) ||
                ((code[at+1]&31)==5 && ((code[at+1]>>16)&15)==6 && type==(vertex ? output_reg : input)))) return false;
            if ((type==output_reg && number>=12) || (type==input && !vertex && number>=10)) return false;
        } else if (op==def || op==47 || op==48) {
            if (n!=(op==47 ? 2u : 5u)) return false;
            if (op==def) {
                const auto number=index(code[at+1]);
                if (kind(code[at+1])!=constant || number>=(vertex ? 256u : 224u)) return false;
                if (original && number>=(vertex ? 248u : 212u) && number<=(vertex ? 249u : 213u)) return false;
            }
        } else {
            const bool no_destination=op==38 || op==39 || op==40 || op==42 || op==43;
            if (n==0 && !no_destination) return false;
            for (unsigned offset=1; offset<=n; ++offset) {
                const Word parameter=code[at+offset];
                if (!(parameter&0x80000000u)) return false;
                const auto type=kind(parameter), number=index(parameter);
                if ((type==temp && number>=32) || (type==constant && number>=(vertex ? 256u : 224u)) ||
                    (type==output_reg && number>=12) || (type==input && !vertex && number>=10) ||
                    (type==color_output && number>=4)) return false;
                if (original && ((type==temp && number>=(vertex ? 7u : 8u)) ||
                    (type==constant && number>=(vertex ? 248u : 212u) && number<=(vertex ? 249u : 213u)) ||
                    (type==(vertex ? output_reg : input) && number==(vertex ? 8u : 7u)))) return false;
                if (parameter&relative) {
                    if (++offset>n || !vertex || kind(parameter)!=constant || index(parameter)>2 ||
                        code[at+offset]!=src(3,0,255)) return false;
                }
            }
            // Macro instruction costs in the documented SM3 profiles. These
            // fifteen originals and our fragments do not use matrix macros.
            slots += op==36 || op==pow_op ? 3 : op==18 || op==33 ? 2 : 1;
        }
        at+=n+1;
    }
    return false;
}
bool exact(const Word* code, const Structure& s, unsigned at, unsigned opcode,
           Word destination, std::initializer_list<Word> sources) noexcept {
    if (at>=s.boundary.size() || !s.boundary[at] || code[at] != ((sources.size()+1)<<24 | opcode) ||
        code[at+1]!=destination) return false;
    return std::equal(sources.begin(),sources.end(),code+at+2);
}
bool no_write(const Word* code, const Structure& s, unsigned number, unsigned lanes,
              unsigned begin, unsigned end) noexcept {
    for (const auto& instruction:s.instructions)
        if (instruction.at>begin && instruction.at<end && instruction.count && instruction.opcode!=dcl &&
            instruction.opcode!=def && kind(code[instruction.at+1])==temp &&
            index(code[instruction.at+1])==number && (mask(code[instruction.at+1])&lanes)) return false;
    return true;
}
bool vertex_sites(const Word* code, const Structure& s, bool loop) noexcept {
    const unsigned point=loop?428:389, emissive=loop?443:397, alpha=loop?500:455;
    if (loop) {
        if (!exact(code,s,point,mul,dst(temp,5),{lane(temp,3,3),src(constant,1)|relative,src(3,0,255)}) ||
            !exact(code,s,emissive,add,dst(output_reg,1),{src(temp,0),src(constant,40)})) return false;
    } else if (!exact(code,s,point,mul,dst(temp,1),{lane(temp,1,2),src(constant,5)}) ||
               !exact(code,s,emissive,mad,dst(output_reg,1),{src(temp,1),lane(temp,1,3),src(constant,19)})) return false;
    if (!exact(code,s,alpha,mul,dst(output_reg,1,8),{lane(temp,0,3),lane(constant,loop?39:18,0)}) ||
        !exact(code,s,alpha+5,mov,dst(output_reg,1,8),{lane(constant,loop?39:18,0)})) return false;
    unsigned writes=0;
    for (const auto& instruction:s.instructions)
        if (instruction.count && instruction.opcode!=dcl && kind(code[instruction.at+1])==output_reg && index(code[instruction.at+1])==1) {
            if (instruction.at!=emissive && instruction.at!=alpha && instruction.at!=alpha+5) return false;
            ++writes;
        }
    return writes==3;
}
bool pixel_sites(const Word* code, const Structure& s, const Pixel& p) noexcept {
    for (unsigned sampler=0; sampler<4; ++sampler)
        if (!exact(code,s,p.texture[sampler],texld,dst(temp,sampler?0:1,xyzw)|pp,
                   {src(input,sampler==3?4:1),src(10,sampler)})) return false;
    if (!exact(code,s,p.clamp,mov,dst(temp,p.clamp_temporary)|pp|sat,{src(input,0)}) ||
        !exact(code,s,p.final_rgb,add,dst(color_output,0)|pp,{src(temp,1),src(temp,0)}) ||
        !exact(code,s,p.final_rgb+4,mul,dst(color_output,0,8)|pp,{lane(temp,2,3),lane(input,0,3)}) ||
        !exact(code,s,p.texture[2]+4,18,dst(temp,2,8)|pp,
               {lane(constant,p.affine_end?3:0,0),lane(temp,0,3),lane(temp,1,3)})) return false;
    if (!no_write(code,s,1,8,p.texture[0],p.texture[2]+4) ||
        !no_write(code,s,0,8,p.texture[2],p.texture[2]+4) ||
        !no_write(code,s,2,8,p.texture[2]+4,p.final_rgb+4)) return false;
    if (p.affine_end)
        for (unsigned lane_index=0; lane_index<3; ++lane_index)
            if (!exact(code,s,p.affine_end-8+lane_index*4,9,dst(temp,3,1u<<lane_index)|pp,
                       {src(temp,2),src(constant,lane_index)})) return false;
    unsigned outputs=0, textures=0, directional=0;
    for (const auto& instruction:s.instructions) {
        const unsigned at=static_cast<unsigned>(instruction.at);
        if (instruction.opcode==texld) ++textures;
        if (instruction.count && instruction.opcode!=dcl && kind(code[at+1])==color_output) {
            if (at!=p.final_rgb && at!=p.final_rgb+4) return false;
            ++outputs;
        }
        if (std::find(p.rgb.begin(),p.rgb.end(),at)!=p.rgb.end()) {
            if (mask(code[at+1])!=xyz || !(code[at+1]&pp) ||
                (instruction.opcode!=mov && instruction.opcode!=mul && instruction.opcode!=add && instruction.opcode!=mad)) return false;
        }
        if (instruction.opcode==dcl || instruction.opcode==def) continue;
        for (unsigned operand=2; operand<=instruction.count; ++operand) {
            const auto value=code[at+operand];
            if (kind(value)==constant && (index(value)==p.light0 || (p.light1 && index(value)==p.light1))) {
                const unsigned source_at=at+operand;
                const auto found=std::find(p.color_source.begin(),p.color_source.end(),source_at);
                if (found==p.color_source.end() || value!=src(constant,index(value))) return false;
                const auto ordinal=static_cast<unsigned>(found-p.color_source.begin());
                if (index(value)!=(ordinal<2 ? p.light0 : p.light1)) return false;
                ++directional;
            }
        }
    }
    for (unsigned at:p.rgb) if (at && (at>=s.boundary.size() || !s.boundary[at])) return false;
    return outputs==2 && textures==4 && directional==(p.light1?4u:1u);
}

struct Insertion { std::size_t at, begin, end; };
// Prove the exact original -> ordinary-motion partition before editing. All
// copied original spans must match byte-for-byte. Ambiguous/new temporal
// layouts refuse instead of deriving offsets by searching shader contents.
bool motion_insertions(const Word* original, std::size_t words, const Words& motion,
    const MotionOutputProfile& row, bool vertex, bool depth, const Structure& s,
    std::vector<Insertion>& insertions) {
    insertions.reserve(vertex ? 2 : 3);
    if (vertex) {
        const std::size_t declarations=3+(depth?3:0), arithmetic=16+(depth?8:0);
        if (motion.size()!=words+declarations+arithmetic) return false;
        insertions.push_back({row.vertex_declaration_insert_dword,row.vertex_declaration_insert_dword,
                              row.vertex_declaration_insert_dword+declarations});
        insertions.push_back({row.vertex_arithmetic_insert_dword,row.vertex_arithmetic_insert_dword+declarations,
                              row.vertex_arithmetic_insert_dword+declarations+arithmetic});
    } else {
        const std::size_t declarations=3+(depth?3:0), definitions_count=18;
        if (motion.size()<=words+definitions_count+declarations) return false;
        const std::size_t body=motion.size()-words-definitions_count-declarations;
        insertions.push_back({row.pixel_definition_insert_dword,row.pixel_definition_insert_dword,
                              row.pixel_definition_insert_dword+definitions_count});
        insertions.push_back({row.pixel_declaration_insert_dword,row.pixel_declaration_insert_dword+definitions_count,
                              row.pixel_declaration_insert_dword+definitions_count+declarations});
        insertions.push_back({row.pixel_append_dword,row.pixel_append_dword+definitions_count+declarations,
                              row.pixel_append_dword+definitions_count+declarations+body});
    }
    std::size_t original_at=0, motion_at=0;
    for (const auto& insertion:insertions) {
        if (insertion.at>=words || !s.boundary[insertion.at] || insertion.at<original_at ||
            insertion.begin!=motion_at+insertion.at-original_at || insertion.end<=insertion.begin || insertion.end>motion.size()) return false;
        if (!std::equal(original+original_at,original+insertion.at,motion.begin()+motion_at)) return false;
        original_at=insertion.at; motion_at=insertion.end;
    }
    return motion.size()-motion_at==words-original_at &&
        std::equal(original+original_at,original+words,motion.begin()+motion_at);
}
const Pixel* pixel_for(std::uint64_t hash, std::size_t words) noexcept {
    for (const auto& p:pixels) if (p.hash==hash && p.words==words) return &p;
    return nullptr;
}
const Vertex* vertex_for(std::uint64_t hash, std::size_t words) noexcept {
    for (const auto& v:vertices) if (v.hash==hash && v.words==words) return &v;
    return nullptr;
}
const MotionOutputProfile* selected_row(bool vertex, std::uint64_t hash) noexcept {
    if (vertex) {
        for (const auto& pixel:pixels) if (linear_material_pair_reviewed(hash,pixel.hash))
            return material_motion_profile(hash,pixel.hash);
    } else {
        for (const auto& v:vertices) if (linear_material_pair_reviewed(v.hash,hash))
            return material_motion_profile(v.hash,hash);
    }
    return nullptr;
}
LinearMaterialResult transform(const Word* original, std::size_t words, const LinearMaterialConfig& config,
    Words& output, bool current_depth, bool vertex) noexcept {
    if (!original || words<2) return LinearMaterialResult::InvalidInput;
    if (!linear_material_config_valid(config)) return LinearMaterialResult::InvalidConfig;
    // Bound the read before hashing; none of the fifteen original programs exceeds
    // 1296 DWORDs, including opaque CTAB/preshader comments.
    if (words>1296) return LinearMaterialResult::UnsupportedShader;
    const auto hash=material_motion_fingerprint(original,words);
    const auto* v=vertex?vertex_for(hash,words):nullptr;
    const auto* p=vertex?nullptr:pixel_for(hash,words);
    if ((!v && vertex) || (!p && !vertex)) return LinearMaterialResult::UnsupportedShader;
    const auto* row=selected_row(vertex,hash);
    if (!row || row->vertex_output_register!=6 || row->pixel_input_register!=5 ||
        row->pixel_temporary_base!=5 || row->vertex_constant_base!=252 || row->pixel_constant_base!=216 ||
        row->texcoord_index!=4 || row->vertex_depth_output_register!=7 || row->pixel_depth_input_register!=6 ||
        row->depth_texcoord_index!=5 || !row->depth_output) return LinearMaterialResult::ProfileMismatch;
    try {
        Structure original_structure;
        if (!structure(original,words,vertex,original_structure,true) ||
            !(vertex?vertex_sites(original,original_structure,v->loop):pixel_sites(original,original_structure,*p)))
            return LinearMaterialResult::ProfileMismatch;
        Words motion;
        const auto motion_result=vertex ? material_motion_vertex_variant_for(*row,original,words,motion,current_depth) :
                                         material_motion_pixel_variant_for(*row,original,words,motion,current_depth);
        if (motion_result!=MaterialMotionResult::Applied)
            return motion_result==MaterialMotionResult::AllocationFailure ? LinearMaterialResult::AllocationFailure : LinearMaterialResult::ProfileMismatch;
        const bool depth=vertex?material_motion_vertex_exports_depth(*row,current_depth):material_motion_pixel_writes_depth(*row,current_depth);
        std::vector<Insertion> insertions;
        if (!motion_insertions(original,words,motion,*row,vertex,depth,original_structure,insertions)) return LinearMaterialResult::ProfileMismatch;
        Words combined;
        combined.reserve(motion.size()+400);
        combined.push_back(original[0]);
        std::size_t insertion_index=0;
        for (std::size_t at=1; at<words;) {
            while (insertion_index<insertions.size() && insertions[insertion_index].at==at) {
                const auto& insertion=insertions[insertion_index++];
                combined.insert(combined.end(),motion.begin()+insertion.begin,motion.begin()+insertion.end);
            }
            if (at==original_structure.first_declaration) definitions(combined,vertex,config);
            const auto declaration_at=vertex?row->vertex_declaration_insert_dword:row->pixel_declaration_insert_dword;
            if (at==declaration_at) {
                emit(combined,dcl,{0x80000005u|(6u<<16),dst(vertex?output_reg:input,vertex?8:7)});
                if (!vertex) {
                    transfer(combined,false,12,{src(constant,p->light0)}); gain(combined,false,12,0);
                    if (p->light1) { transfer(combined,false,13,{src(constant,p->light1)}); gain(combined,false,13,0); }
                }
            }
            if (original[at]==end_token) { combined.push_back(end_token); ++at; continue; }
            const unsigned op=original[at]&0xffff, n=length(original[at]);
            if (vertex && at==(v->loop?428u:389u)) {
                transfer(combined,true,7,{original[at+3],v->loop?original[at+4]:0}); gain(combined,true,7,0);
            }
            if (vertex && at==(v->loop?443u:397u)) {
                sanitize(combined,true,7,{original[at+(v->loop?3:4)]});
                // Material emissive already includes native strength: no POW.
                // ABS canonicalizes a signed-zero sanitizer result explicitly.
                emit(combined,abs_op,{dst(temp,7),src(temp,7)}); gain(combined,true,7,1);
            }
            const auto copied=combined.size();
            combined.insert(combined.end(),original+at,original+at+n+1);
            if (vertex) {
                if (at==(v->loop?428u:389u)) {
                    combined[copied+3]=src(temp,7);
                    if (v->loop) { combined.erase(combined.begin()+copied+4); combined[copied]=(3u<<24)|mul; }
                }
                if (at==(v->loop?443u:397u)) {
                    combined[copied+1]=dst(output_reg,8);
                    combined[copied+(v->loop?3:4)]=src(temp,7);
                }
            } else if (op!=0xfffe) {
                if (std::find(p->rgb.begin(),p->rgb.end(),at)!=p->rgb.end()) combined[copied+1]&=~pp;
                if (at==p->clamp) {
                    combined[copied+1]&=~sat;
                    combined[copied+2]=src(input,7);
                }
                for (unsigned ordinal=0; ordinal<p->color_source.size(); ++ordinal)
                    if (p->color_source[ordinal]>at && p->color_source[ordinal]<=at+n)
                        combined[copied+p->color_source[ordinal]-at]=src(temp,ordinal<2?12:13);
                if (at==(p->affine_end?p->affine_end:p->texture[0]))
                    transfer(combined,false,p->affine_end?3:1,{src(temp,p->affine_end?3:1)});
                if (at==p->texture[3]) transfer(combined,false,0,{src(temp,0)});
                if (at==p->texture[2]) { transfer(combined,false,0,{src(temp,0)}); gain(combined,false,0,2); }
                if (at==p->final_rgb) {
                    combined[copied+1]=dst(temp,11);
                    transfer(combined,false,11,{src(temp,11)},true);
                    emit(combined,mov,{dst(color_output,0),src(temp,11)});
                }
            }
            at+=n+1;
        }
        if (insertion_index!=insertions.size()) return LinearMaterialResult::ProfileMismatch;
        Structure final_structure;
        if (!structure(combined.data(),combined.size(),vertex,final_structure,false)) return LinearMaterialResult::ResourceLimit;
        output.swap(combined);
        return LinearMaterialResult::Applied;
    } catch (...) { return LinearMaterialResult::AllocationFailure; }
}
} // namespace

bool linear_material_config_valid(const LinearMaterialConfig& config) noexcept {
    for (float value:{config.direct_gain,config.material_emissive_gain,config.lightmap_emissive_gain})
        if (!std::isfinite(value) || value<0.0f || value>16.0f) return false;
    return true;
}
bool linear_material_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    for (const auto& pair:pairs)
        if (pair.vertex==vertex && pair.pixel==pixel) return true;
    return false;
}
LinearMaterialResult linear_material_vertex_variant(const Word* original, std::size_t words,
    const LinearMaterialConfig& config, Words& output, bool current_depth) noexcept {
    return transform(original,words,config,output,current_depth,true);
}
LinearMaterialResult linear_material_pixel_variant(const Word* original, std::size_t words,
    const LinearMaterialConfig& config, Words& output, bool current_depth) noexcept {
    return transform(original,words,config,output,current_depth,false);
}
} // namespace x3m::renderer
