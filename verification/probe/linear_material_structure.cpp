// Host-only structural driver. Reads copyrighted originals from a local corpus;
// optional emitted variants go only to a caller-supplied local temporary folder.
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include <algorithm>
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
int main(int argc,char** argv) {
    try {
        require(argc==3,"usage: linear_material_structure <original directory> <local output directory>");
        const char* names[]={"vs_53a0a641107ed76c","vs_719856ce0c213220","vs_badefd5143b3024f",
            "ps_8759c7838bbc86c2","ps_63f96eba9eea7880","ps_593e5dea9b3457d5",
            "ps_7a0bb00a8070496a","ps_8d5b2ba0fb4d13bf","ps_dab93928f26906f7",
            "ps_3b94320087e81945","ps_e3b7acc16da9932d","ps_7a14d4dcb28f27e5",
            "ps_8ab6188a40ca15ea","ps_8df6143d0e77d92e","ps_e16a9806ee3544c3"};
        unsigned variants=0, instructions[2][2]{}, slots[2][2]{};
        long long create_ns=0;
        auto begin=std::chrono::steady_clock::now();
        for (const char* raw_name:names) {
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
                    create_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-create_begin).count();
                    if (outcome!=LinearMaterialResult::Applied) std::cerr<<name<<" result="<<int(outcome)<<'\n';
                    require(outcome==LinearMaterialResult::Applied,"combined original admission");
                    unsigned executable=0, weighted=0;
                    for(std::size_t at=1;at<result.size()-1;) {
                        const auto token=result[at], op=token&0xffffu;
                        const unsigned count=op==0xfffeu?(token>>16)&0x7fffu:(token>>24)&15u;
                        if(op!=0xfffeu && op!=31 && op!=81 && op!=47 && op!=48) {
                            ++executable; weighted+=op==32 || op==36?3:op==18 || op==33?2:1;
                        }
                        at+=count+1;
                    }
                    instructions[vertex?0:1][depth]=std::max(instructions[vertex?0:1][depth],executable);
                    slots[vertex?0:1][depth]=std::max(slots[vertex?0:1][depth],weighted);
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
                require(transform(original.data(),1297,{},output,depth)==LinearMaterialResult::UnsupportedShader && output==saved,"bounded read guard rollback");
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
        const std::uint64_t vs[]={0x53a0a641107ed76cull,0x719856ce0c213220ull,0xbadefd5143b3024full};
        const std::uint64_t ps[]={0x8759c7838bbc86c2ull,0x63f96eba9eea7880ull,0x593e5dea9b3457d5ull,
            0x7a0bb00a8070496aull,0x8d5b2ba0fb4d13bfull,0xdab93928f26906f7ull,
            0x3b94320087e81945ull,0xe3b7acc16da9932dull,0x7a14d4dcb28f27e5ull,
            0x8ab6188a40ca15eaull,0x8df6143d0e77d92eull,0xe16a9806ee3544c3ull};
        unsigned pair_count=0;
        for(unsigned v=0;v<3;++v) for(unsigned p=0;p<12;++p) {
            const bool base=ps[p]==0x8759c7838bbc86c2ull || ps[p]==0x63f96eba9eea7880ull ||
                            ps[p]==0x3b94320087e81945ull || ps[p]==0xe3b7acc16da9932dull;
            const bool expected=v==0?base:!base;
            require(linear_material_pair_reviewed(vs[v],ps[p])==expected,"exact pair matrix");
            if(expected) ++pair_count;
        }
        require(!linear_material_pair_reviewed(0,ps[0]) && !linear_material_pair_reviewed(vs[0],0),"unknown pair");
        const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
        std::cout<<"{\"programs\":15,\"pairs\":"<<pair_count<<",\"variants\":"<<variants<<",\"checks\":"<<checks
                 <<",\"elapsed_us_including_io_and_negative_checks\":"<<elapsed
                 <<",\"initial_creates_ns\":"<<create_ns
                 <<",\"max_executable_instructions_depth_off_on\":{\"vs\":["<<instructions[0][0]<<','<<instructions[0][1]
                 <<"],\"ps\":["<<instructions[1][0]<<','<<instructions[1][1]<<"]}"
                 <<",\"max_static_weighted_slots_depth_off_on\":{\"vs\":["<<slots[0][0]<<','<<slots[0][1]
                 <<"],\"ps\":["<<slots[1][0]<<','<<slots[1][1]<<"]}}\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
