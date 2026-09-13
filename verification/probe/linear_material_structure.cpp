// Host-only structural driver. Reads copyrighted originals from a local corpus;
// optional emitted variants go only to a caller-supplied local temporary folder.
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::renderer;
using Words=std::vector<std::uint32_t>;
unsigned checks=0;
void require(bool condition,const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
Words read(const std::string& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    require(bool(stream),"open original"); const auto bytes=stream.tellg();
    require(bytes>0 && bytes%4==0,"aligned original");
    Words result(static_cast<std::size_t>(bytes)/4); stream.seekg(0);
    stream.read(reinterpret_cast<char*>(result.data()),bytes); require(bool(stream),"read original"); return result;
}
void write(const std::string& path,const Words& words) {
    std::ofstream stream(path,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(words.data()),static_cast<std::streamsize>(words.size()*4));
    require(bool(stream),"write local variant");
}
bool bump_program(const std::string& name) {
    constexpr std::uint64_t ids[]={0x4944d81dfe531b37ull,0x19a246a56e9d9700ull,0x44c4a41ca92ae2e3ull,0xca6bfa4a6cca7e2aull,0x5e0a10fe752b6140ull,0x63379470db8d2a86ull,0x68915563dd0aac9aull,0xd086fde54698070cull,0xf17fffd88d134b04ull,0x0c1f3f0f440e4a0cull,0x4f052209611387f0ull,0x64bac8bb307eb896ull,0x789449ffd931d23eull,0x99153c144030c396ull,0xabf3c0fad53456d8ull,0xb0f9313b77cc78eeull,0xc1452981fd0bff64ull,0xcf449bcb069aec4full,0xd514bf852d8a9c58ull,0xdff6a3d360603fa2ull,0xf1d14a7dbf7c6173ull};
    const auto id=std::stoull(name.substr(3),nullptr,16);
    return std::find(std::begin(ids),std::end(ids),id)!=std::end(ids);
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"usage: linear_material_structure <original directory> <local output directory>");
        const char* names[]={"vs_53a0a641107ed76c","vs_719856ce0c213220","vs_badefd5143b3024f",
            "ps_8759c7838bbc86c2","ps_63f96eba9eea7880","ps_593e5dea9b3457d5",
            "ps_7a0bb00a8070496a","ps_8d5b2ba0fb4d13bf","ps_dab93928f26906f7",
            "ps_3b94320087e81945","ps_e3b7acc16da9932d","ps_7a14d4dcb28f27e5",
            "ps_8ab6188a40ca15ea","ps_8df6143d0e77d92e","ps_e16a9806ee3544c3",
            "vs_4944d81dfe531b37","vs_19a246a56e9d9700","vs_44c4a41ca92ae2e3",
            "ps_ca6bfa4a6cca7e2a","ps_5e0a10fe752b6140","ps_63379470db8d2a86",
            "ps_68915563dd0aac9a","ps_d086fde54698070c","ps_f17fffd88d134b04",
            "vs_494fe349b8bc12ec",
            "ps_02606104fa59fb29",
            "ps_0c1f3f0f440e4a0c",
            "ps_1d638938d93421b3",
            "ps_462342e3e5781384",
            "ps_4f052209611387f0",
            "ps_55826dc176afe464",
            "ps_64bac8bb307eb896",
            "ps_789449ffd931d23e",
            "ps_7c83ed50c9894e44",
            "ps_827d8d2d617bedce",
            "ps_99153c144030c396",
            "ps_abf3c0fad53456d8",
            "ps_b0f9313b77cc78ee",
            "ps_bd4d51c08486c6e0",
            "ps_c1452981fd0bff64",
            "ps_cf449bcb069aec4f",
            "ps_d514bf852d8a9c58",
            "ps_db644b73b68c0547",
            "ps_de2dd381fa64193d",
            "ps_dff6a3d360603fa2",
            "ps_e70adc744a38ca59",
            "ps_f1d14a7dbf7c6173",
            "ps_f6a501717c3e5ca8",
            "ps_ff32b602a271c327"};
        unsigned variants=0, instructions[2][2]{}, slots[2][2]{}, family_slots[2][2][2]{};
        long long create_ns=0, family_create_ns[2]{};
        unsigned family_creates[2]{};
        auto begin=std::chrono::steady_clock::now();
        for (unsigned program_index=0;program_index<std::size(names);++program_index) {
            const char* raw_name=names[program_index];
            const unsigned family=bump_program(raw_name)?1:0;
            const std::string name(raw_name); const bool vertex=name[0]=='v';
            const auto original=read(std::string(argv[1])+"/"+name+".bin");
            auto transform=vertex?linear_material_vertex_variant:linear_material_pixel_variant;
            for (bool depth:{false,true}) {
                Words motion;
                require((vertex?material_motion_vertex_variant(original.data(),original.size(),motion,depth):
                    material_motion_pixel_variant(original.data(),original.size(),motion,depth))==MaterialMotionResult::Applied,"motion control");
                write(std::string(argv[2])+"/"+name+"-motion-"+std::to_string(depth)+".bin",motion);
                for (float gain_value:{0.0f,1.0f,4.0f,16.0f}) {
                    const LinearMaterialConfig config{gain_value,gain_value,gain_value};
                    Words result{0x12345678};
                    const auto create_begin=std::chrono::steady_clock::now();
                    const auto outcome=transform(original.data(),original.size(),config,result,depth);
                    const auto duration=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-create_begin).count();
                    create_ns+=duration; family_create_ns[family]+=duration; ++family_creates[family];
                    if (outcome!=LinearMaterialResult::Applied) std::cerr<<name<<" result="<<int(outcome)<<'\n';
                    require(outcome==LinearMaterialResult::Applied,"combined original admission");
                    unsigned executable=0, weighted=0;
                    std::array<unsigned,16> texture_dimensions{};
                    for(std::size_t at=1;at<result.size()-1;) {
                        const auto token=result[at], op=token&0xffffu;
                        const unsigned count=op==0xfffeu?(token>>16)&0x7fffu:(token>>24)&15u;
                        if(op==31 && (((result[at+2]>>28)&7)|((result[at+2]>>8)&24))==10)
                            texture_dimensions.at(result[at+2]&0x7ff)=(result[at+1]>>27)&15;
                        if(op!=0xfffeu && op!=31 && op!=81) {
                            ++executable;
                            // Independent documented table; unknown forms do
                            // not silently inherit the common unit cost.
                            switch(op) {
                            case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9:
                            case 10: case 11: case 12: case 35: case 42: case 43: case 46: case 88:
                                weighted+=1; break;
                            case 18: case 39: case 90: weighted+=2; break;
                            case 32: case 36: case 38: case 40: weighted+=3; break;
                            case 66: {
                                const auto dimension=texture_dimensions.at(result[at+3]&0x7ff);
                                require(dimension==2 || dimension==3,"known sample slot dimension");
                                weighted+=dimension==3?4:1; break;
                            }
                            default: require(false,"known SM3 slot cost");
                            }
                        }
                        at+=count+1;
                    }
                    instructions[vertex?0:1][depth]=std::max(instructions[vertex?0:1][depth],executable);
                    slots[vertex?0:1][depth]=std::max(slots[vertex?0:1][depth],weighted);
                    family_slots[family][vertex?0:1][depth]=std::max(family_slots[family][vertex?0:1][depth],weighted);
                    require(weighted<=512,"minimum SM3 static slot budget");
                    Words alias=original;
                    require(transform(alias.data(),alias.size(),config,alias,depth)==LinearMaterialResult::Applied && alias==result,"input/output alias");
                    write(std::string(argv[2])+"/"+name+"-"+std::to_string(depth)+"-"+std::to_string(int(gain_value))+".bin",result);
                    ++variants;
                }
                for (float invalid:{-1.0f,16.001f,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
                                    std::numeric_limits<float>::quiet_NaN()})
                    for (unsigned field=0;field<3;++field) {
                        LinearMaterialConfig config;
                        if (field==0) config.direct_gain=invalid;
                        if (field==1) config.material_emissive_gain=invalid;
                        if (field==2) config.lightmap_emissive_gain=invalid;
                        Words output{91,92}; const auto saved=output;
                        require(transform(original.data(),original.size(),config,output,depth)==LinearMaterialResult::InvalidConfig && output==saved,"invalid gain rollback");
                    }
                Words output{91,92}; const auto saved=output;
                require(transform(nullptr,original.size(),{},output,depth)==LinearMaterialResult::InvalidInput && output==saved,"null rollback");
                require(transform(original.data(),original.size()-1,{},output,depth)==LinearMaterialResult::UnsupportedShader && output==saved,"truncated rollback");
                require(transform(original.data(),1393,{},output,depth)==LinearMaterialResult::UnsupportedShader && output==saved,"bounded read guard rollback");
                for (unsigned at:{0u,2u,static_cast<unsigned>(original.size()-4),static_cast<unsigned>(original.size()-1)}) {
                    auto broken=original; broken[at]^=1;
                    require(transform(broken.data(),broken.size(),{},output,depth)==LinearMaterialResult::UnsupportedShader && output==saved,"whole-original mutation rollback");
                }
                // Canonicalize -0 gain DEFs so multiply-by-zero produces +0.
                Words positive,negative;
                require(transform(original.data(),original.size(),{0,0,0},positive,depth)==LinearMaterialResult::Applied &&
                    transform(original.data(),original.size(),{-0.0f,-0.0f,-0.0f},negative,depth)==LinearMaterialResult::Applied && positive==negative,"gain signed-zero canonicalization");
            }
        }
        const std::uint64_t vs[]={0x53a0a641107ed76cull,0x719856ce0c213220ull,0xbadefd5143b3024full,0x4944d81dfe531b37ull,0x19a246a56e9d9700ull,0x44c4a41ca92ae2e3ull,0x494fe349b8bc12ecull};
        const std::uint64_t ps[]={0x8759c7838bbc86c2ull,0x63f96eba9eea7880ull,0x593e5dea9b3457d5ull,
            0x7a0bb00a8070496aull,0x8d5b2ba0fb4d13bfull,0xdab93928f26906f7ull,
            0x3b94320087e81945ull,0xe3b7acc16da9932dull,0x7a14d4dcb28f27e5ull,
            0x8ab6188a40ca15eaull,0x8df6143d0e77d92eull,0xe16a9806ee3544c3ull,
            0xca6bfa4a6cca7e2aull,0x5e0a10fe752b6140ull,0x63379470db8d2a86ull,
            0x68915563dd0aac9aull,0xd086fde54698070cull,0xf17fffd88d134b04ull,
            0x02606104fa59fb29ull,0x0c1f3f0f440e4a0cull,0x1d638938d93421b3ull,0x462342e3e5781384ull,0x4f052209611387f0ull,0x55826dc176afe464ull,0x64bac8bb307eb896ull,0x789449ffd931d23eull,0x7c83ed50c9894e44ull,0x827d8d2d617bedceull,0x99153c144030c396ull,0xabf3c0fad53456d8ull,0xb0f9313b77cc78eeull,0xbd4d51c08486c6e0ull,0xc1452981fd0bff64ull,0xcf449bcb069aec4full,0xd514bf852d8a9c58ull,0xdb644b73b68c0547ull,0xde2dd381fa64193dull,0xdff6a3d360603fa2ull,0xe70adc744a38ca59ull,0xf1d14a7dbf7c6173ull,0xf6a501717c3e5ca8ull,0xff32b602a271c327ull};
        // Independent family/shape matrix, not derived from the production table.
        struct Group { std::array<std::uint64_t,6> pixels; unsigned base_vs, toggle_vs, samplers; };
        const Group groups[]={
            {{0x8759c7838bbc86c2ull,0x63f96eba9eea7880ull,0x593e5dea9b3457d5ull,0x7a0bb00a8070496aull,0x8d5b2ba0fb4d13bfull,0xdab93928f26906f7ull},1,6,15}, // Argon DEFAULT
            {{0x3b94320087e81945ull,0xe3b7acc16da9932dull,0x7a14d4dcb28f27e5ull,0x8ab6188a40ca15eaull,0x8df6143d0e77d92eull,0xe16a9806ee3544c3ull},1,6,15}, // Shared DEFAULT
            {{0xca6bfa4a6cca7e2aull,0x5e0a10fe752b6140ull,0x63379470db8d2a86ull,0x68915563dd0aac9aull,0xd086fde54698070cull,0xf17fffd88d134b04ull},8,48,31}, // Argon BUMP
            {{0x462342e3e5781384ull,0x827d8d2d617bedceull,0x02606104fa59fb29ull,0x1d638938d93421b3ull,0xbd4d51c08486c6e0ull,0xde2dd381fa64193dull},1,6,15}, // Split DEFAULT
            {{0x7c83ed50c9894e44ull,0xe70adc744a38ca59ull,0xdb644b73b68c0547ull,0xff32b602a271c327ull,0xf6a501717c3e5ca8ull,0x55826dc176afe464ull},64,6,15}, // Standard DEFAULT
            {{0x0c1f3f0f440e4a0cull,0x64bac8bb307eb896ull,0x789449ffd931d23eull,0x4f052209611387f0ull,0xabf3c0fad53456d8ull,0xcf449bcb069aec4full},8,48,31}, // Standard BUMP
            {{0x99153c144030c396ull,0xc1452981fd0bff64ull,0xb0f9313b77cc78eeull,0xd514bf852d8a9c58ull,0xdff6a3d360603fa2ull,0xf1d14a7dbf7c6173ull},8,48,31}, // Standard LOW
        };
        unsigned pair_count=0;
        for(unsigned v=0;v<std::size(vs);++v) for(unsigned p=0;p<std::size(ps);++p) {
            unsigned expected_mask=0, memberships=0;
            for(const auto& group:groups) for(unsigned shape=0;shape<6;++shape) if(group.pixels[shape]==ps[p]) {
                ++memberships;
                if((shape<2?group.base_vs:group.toggle_vs)&(1u<<v)) expected_mask=group.samplers;
            }
            require(memberships==1,"unique independently reviewed pixel shape");
            require(linear_material_pair_reviewed(vs[v],ps[p])==(expected_mask!=0),"exact pair and cross-family matrix");
            require(linear_material_sampler_mask(vs[v],ps[p])==expected_mask,"exact sampler contract");
            if(expected_mask) ++pair_count;
        }
        require(!linear_material_sampler_mask(0,ps[0]) && !linear_material_sampler_mask(vs[0],0),"unknown pair");
        for (const auto& negative:std::array<std::array<std::uint64_t,2>,3>{{
            {{0x53a0a641107ed76cull,0xef2bf556f207b8bdull}},
            {{0x4944d81dfe531b37ull,0x3006f8030a467739ull}},
            {{0x19a246a56e9d9700ull,0x042c9ae16f41feffull}}}}) {
            require(material_motion_profile(negative[0],negative[1])!=nullptr,"motion-reviewed negative witness");
            require(!linear_material_sampler_mask(negative[0],negative[1]) &&
                    !linear_material_pair_reviewed(negative[0],negative[1]),"uncovered material pair");
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
        std::cout<<"{\"programs\":49,\"pairs\":"<<pair_count<<",\"variants\":"<<variants<<",\"checks\":"<<checks
                 <<",\"elapsed_us_including_io_and_negative_checks\":"<<elapsed
                 <<",\"initial_creates_ns\":"<<create_ns
                 <<",\"create_counts_default_bump\":["<<family_creates[0]<<','<<family_creates[1]<<']'
                 <<",\"create_ns_default_bump\":["<<family_create_ns[0]<<','<<family_create_ns[1]<<']'
                 <<",\"weighted_slots_default_vs_ps_depth_off_on\":[["<<family_slots[0][0][0]<<','<<family_slots[0][0][1]<<"],["<<family_slots[0][1][0]<<','<<family_slots[0][1][1]<<"]]"
                 <<",\"weighted_slots_bump_vs_ps_depth_off_on\":[["<<family_slots[1][0][0]<<','<<family_slots[1][0][1]<<"],["<<family_slots[1][1][0]<<','<<family_slots[1][1][1]<<"]]"
                 <<",\"max_executable_instructions_depth_off_on\":{\"vs\":["<<instructions[0][0]<<','<<instructions[0][1]
                 <<"],\"ps\":["<<instructions[1][0]<<','<<instructions[1][1]<<"]}"
                 <<",\"max_static_weighted_slots_depth_off_on\":{\"vs\":["<<slots[0][0]<<','<<slots[0][1]
                 <<"],\"ps\":["<<slots[1][0]<<','<<slots[1][1]<<"]}}\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
