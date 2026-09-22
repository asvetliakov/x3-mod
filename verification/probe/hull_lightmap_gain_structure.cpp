// Host-only driver of the hull self-illumination gain transformer
// (linear_material_hull_lightmap_gain_pixel_variant, --hull-lightmap-gain;
// docs/reverse-engineering/hull-self-illumination.md 5) and of the hull
// emissive widening built on it (--hull-emissive-widening,
// docs/architecture/hull-emissive-widening.md 2, 8.3). Reads every ps_/vs_
// program of the local corpus, writes the fill variant (K = 0 and 0.05, the
// control), the gained variant (G = 4) and the widened dynamic-gain variant for
// both depth modes next to them in a caller-supplied local folder, and prints
// one JSON object with per-program rows for the Python oracle. No game bytes
// are embedded; unreviewed programs are reported, never asserted.
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::renderer;
using Words=std::vector<std::uint32_t>;
namespace fs=std::filesystem;
unsigned checks=0;
void require(bool condition,const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
Words read(const fs::path& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    require(bool(stream),"open original"); const auto bytes=stream.tellg();
    require(bytes>0 && bytes%4==0,"aligned original");
    Words result(static_cast<std::size_t>(bytes)/4); stream.seekg(0);
    stream.read(reinterpret_cast<char*>(result.data()),bytes); require(bool(stream),"read original"); return result;
}
void write(const fs::path& path,const Words& words) {
    std::ofstream stream(path,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(words.data()),static_cast<std::streamsize>(words.size()*4));
    require(bool(stream),"write local variant");
}
// Far fade (--light-map-far-fade): the dynamic variant is the constant-gain one
// without its `def c223` (6 words) and with the MUL's c223.x operand replaced
// by c217.w (the motion ABI's free per-draw lane). Built here from the static
// variant, independently of the transformer.
unsigned dynamic_checks=0;
Words expected_dynamic(const Words& fixed) {
    Words result; bool dropped=false; unsigned replaced=0;
    for (std::size_t i=1;i<fixed.size();) {
        if (!dropped && i+5<fixed.size() && fixed[i]==0x05000051u && (fixed[i+1]&0x70000000u)==0x20000000u && (fixed[i+1]&0x7ffu)==223u) { dropped=true; i+=6; continue; }
        result.push_back(fixed[i]==0xa00000dfu ? (++replaced,0xa0ff00d9u) : fixed[i]); ++i;
    }
    result.insert(result.begin(),fixed[0]);
    require(dropped && replaced==1,"static variant carries one c223 DEF and one c223.x read");
    ++dynamic_checks; return result;
}
// Independent weighted-slot walk (Microsoft SM3 slot table; the same one the
// original-fill structural driver uses).
std::pair<unsigned,unsigned> weighted_slots(const Words& result) {
    unsigned executable=0, weighted=0;
    std::array<unsigned,16> texture_dimensions{};
    for(std::size_t at=1;at<result.size()-1;) {
        const auto token=result[at], op=token&0xffffu;
        const unsigned count=op==0xfffeu?(token>>16)&0x7fffu:(token>>24)&15u;
        if(op==31 && (((result[at+2]>>28)&7)|((result[at+2]>>8)&24))==10)
            texture_dimensions.at(result[at+2]&0x7ff)=(result[at+1]>>27)&15;
        if(op!=0xfffeu && op!=31 && op!=81) {
            ++executable;
            switch(op) {
            case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9:
            case 10: case 11: case 12: case 14: case 15: case 35: case 42: case 43: case 46: case 88:
                weighted+=1; break;
            case 18: case 39: case 90: case 91: case 92: weighted+=2; break;
            case 32: case 36: case 38: case 40: case 41: case 93: weighted+=3; break;
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
    require(result.back()==0xffffu,"END token");
    return {executable,weighted};
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"usage: hull_lightmap_gain_structure <original directory> <local output directory>");
        const fs::path corpus=argv[1], out=argv[2];
        const float fills[]={0.0f,0.05f}, gain=4.0f;
        // Hull emissive widening K = 3, B = 3 (docs/architecture/hull-emissive-widening.md 8.3): the block adds
        // 131 DWORDs (113 in the block, 18 in three DEFs), 27 instructions and 35 weighted slots to the gained variant.
        const HullLightmapWiden widen{3.0f,3.0f};
        constexpr unsigned widen_words=131, widen_instructions=27, widen_slots_added=35; // 113 block words + three 6-word DEFs
        // Flow-control depth at the light-map fetch (linear_material_flow_control_depth) on synthetic ps_3_0
        // programs: the fetch `texld r0, v1, s2` inside `if b0`, `rep i0` and `loop aL, i0` is at depth 1 (the
        // widening refuses such a site with FlowControl), after the closing endif/endrep/endloop at 0, outside any
        // block at 0; a `label`/`call` before the fetch is -2 (Subroutine); off-boundary and malformed sites -1.
        // The Python oracle compares this table (behaviour, not source text).
        const Words fetch_words={0x03000042u,0x800f0000u,0x90e40001u,0xa0e40802u};
        auto program=[&](std::initializer_list<Words> parts){ Words w{0xffff0300u}; for (const auto& part:parts) w.insert(w.end(),part.begin(),part.end()); w.push_back(0x0000ffffu); return w; };
        const Words if_open={0x01000028u,0xe0000800u}, endif_={0x0000002bu}, rep_open={0x01000026u,0xf0e40000u}, endrep_={0x00000027u},
                    loop_open={0x0200001bu,0xf0e40800u,0xf0e40000u}, endloop_={0x0000001du}, label_={0x0100001eu,0xa0e41000u}, ret_={0x0000001cu}, call_={0x01000019u,0xa0e41000u};
        struct FlowCase { const char* name; Words code; std::size_t site; int expected; };
        const FlowCase flow_cases[]={
            {"inside_if",program({if_open,fetch_words,endif_}),3,1},{"after_endif",program({if_open,endif_,fetch_words}),4,0},
            {"inside_rep",program({rep_open,fetch_words,endrep_}),3,1},{"inside_loop",program({loop_open,fetch_words,endloop_}),4,1},
            {"after_endloop",program({loop_open,endloop_,fetch_words}),5,0},{"nested",program({if_open,loop_open,fetch_words,endloop_,endif_}),6,2},
            {"outside",program({fetch_words}),1,0},{"after_label_ret",program({label_,ret_,fetch_words}),4,-2},{"after_call",program({call_,fetch_words}),3,-2},
            {"off_boundary",program({if_open,fetch_words,endif_}),4,-1},{"site_zero",program({fetch_words}),0,-1}};
        std::string flow_json; unsigned flow_checks=0;
        for (const auto& fc:flow_cases) {
            const int depth=linear_material_flow_control_depth(fc.code.data(),fc.code.size(),fc.site);
            require(depth==fc.expected,fc.name); ++flow_checks;
            flow_json+=std::string(flow_json.empty()?"":",")+"\""+fc.name+"\":"+std::to_string(depth);
        }
        require(linear_material_flow_control_depth(nullptr,0,1)==-1,"null program: -1"); ++flow_checks;
        std::vector<std::string> names;
        for (const auto& entry:fs::directory_iterator(corpus)) {
            const auto name=entry.path().filename().string();
            if (entry.is_regular_file() && name.size()>7 && name.compare(name.size()-4,4,".bin")==0 &&
                (name.rfind("ps_",0)==0 || name.rfind("vs_",0)==0)) names.push_back(name.substr(0,name.size()-4));
        }
        std::sort(names.begin(),names.end());
        require(!names.empty(),"corpus has programs");
        std::cout<<"{\"rows\":[";
        bool first=true; unsigned applied=0, supported=0, untouched=0; long long create_ns=0; unsigned max_slots=0, max_widen_slots=0;
        for (const auto& name:names) {
            const auto original=read(corpus/(name+".bin"));
            const auto saved=original;
            Words probe{91,92}; bool entered=true, gained=true;
            const auto begin=std::chrono::steady_clock::now();
            const auto status=linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),0.05f,gain,probe,true,entered,gained);
            create_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
            require(original==saved,"immutable input");
            if (status!=LinearMaterialResult::Applied) {
                require(probe==Words({91,92}) && !entered && !gained,"refusal leaves output intact and reports no fill or gain");
                std::cout<<(first?"":",")<<"{\"name\":\""<<name<<"\",\"words\":"<<original.size()<<",\"status\":"<<int(status)<<"}";
                first=false; continue;
            }
            ++supported;
            unsigned base_slots[2][2]{}, variant_slots[2][2]{}, base_instructions[2][2]{}, variant_instructions[2][2]{}, widen_slots[2][2]{}, share_widen_slots[2]{};
            bool gain_applied_any=false, gain_refused_any=false, widen_applied_any=false;
            for (bool depth:{false,true}) {
                for (unsigned f=0;f<2;++f) {
                    // Control: the fill variant of the same original (K = 0 is the plain motion variant).
                    Words base; bool base_fill=true;
                    require(linear_material_original_fill_pixel_variant(original.data(),original.size(),fills[f],base,depth,base_fill)==LinearMaterialResult::Applied,"fill control");
                    write(out/(name+"-hlbase-"+std::to_string(f)+"-"+std::to_string(depth)+".bin"),base);
                    std::tie(base_instructions[f][depth],base_slots[f][depth])=weighted_slots(base);
                    // Gain 1: the control byte for byte, no gain reported.
                    Words identity{0x12345678}; bool identity_fill=true, identity_gain=true;
                    require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),fills[f],1.0f,identity,depth,identity_fill,identity_gain)==LinearMaterialResult::Applied &&
                            identity==base && identity_fill==base_fill && !identity_gain,"G=1 is the fill variant byte for byte");
                    // Gain 4.
                    Words result{0x12345678}; bool fill_applied=true, gain_applied=true;
                    const auto outcome=linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),fills[f],gain,result,depth,fill_applied,gain_applied);
                    require(outcome==LinearMaterialResult::Applied,"reviewed original admission for every K and G");
                    require(original==saved,"immutable input");
                    require(fill_applied==base_fill,"the fill report is the control's");
                    if (!gain_applied) { gain_refused_any=true; require(result==base,"a program without the term keeps the fill variant byte for byte"); }
                    else {
                        gain_applied_any=true;
                        require(result.size()==base.size()+10,"one DEF and one MUL added");
                        const auto [instructions,slots]=weighted_slots(result);
                        variant_instructions[f][depth]=instructions; variant_slots[f][depth]=slots;
                        require(slots<=512,"minimum SM3 static slot budget");
                        max_slots=std::max(max_slots,slots);
                    }
                    {
                        Words dynamic{0x12345678}; bool dynamic_fill=!fill_applied, dynamic_gain=!gain_applied;
                        require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),fills[f],gain,dynamic,depth,dynamic_fill,dynamic_gain,true)==LinearMaterialResult::Applied &&
                                dynamic_fill==fill_applied && dynamic_gain==gain_applied,"dynamic gain: the same verdict");
                        require(dynamic==(gain_applied?expected_dynamic(result):base),"dynamic gain: no DEF, the MUL reads c217.w; untouched programs stay the fill variant");
                        // Hull emissive widening on the dynamic-gain variant: the
                        // same verdict as the gain, +131 DWORDs and +35 weighted
                        // slots (the Python oracle rebuilds the bytes); the static
                        // gain composes identically (+131 over the static variant).
                        Words widened{0x12345678}; bool widen_fill=!fill_applied, widen_gain=!gain_applied, widen_applied=!gain_applied;
                        require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),fills[f],gain,widened,depth,widen_fill,widen_gain,true,&widen,&widen_applied)==LinearMaterialResult::Applied &&
                                widen_fill==fill_applied && widen_gain==gain_applied && widen_applied==gain_applied,"widening: the gain's verdict");
                        if (gain_applied) {
                            widen_applied_any=true;
                            require(widened.size()==dynamic.size()+widen_words,"widening: +131 DWORDs");
                            const auto [instructions,slots]=weighted_slots(widened);
                            require(instructions==variant_instructions[f][depth]+widen_instructions && slots==variant_slots[f][depth]+widen_slots_added && slots<=512,"widening: +27 instructions, +35 slots, within 512");
                            widen_slots[f][depth]=slots; max_widen_slots=std::max(max_widen_slots,slots);
                            Words widened_static{0x12345678}; bool sf=false, sg=false, sw=false;
                            require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),fills[f],gain,widened_static,depth,sf,sg,false,&widen,&sw)==LinearMaterialResult::Applied &&
                                    sw && widened_static.size()==result.size()+widen_words && expected_dynamic(widened_static)==widened,"widening composes with the static gain (DEF c223, c223.x -> c217.w is the dynamic widened variant)");
                        } else require(widened==base,"widening: a program without the term keeps the fill variant byte for byte");
                        write(out/(name+"-hlwiden-"+std::to_string(f)+"-"+std::to_string(depth)+".bin"),widened);
                        Words invalid{91,92}; bool ifill=true, igain=true, iwiden=true;
                        require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),fills[f],1.0f,invalid,depth,ifill,igain,true,&widen,&iwiden)==LinearMaterialResult::InvalidConfig &&
                                invalid==Words({91,92}) && !ifill && !igain && !iwiden,"widening without a gain is InvalidConfig");
                        // K and B bounds: 1 < K <= 8, 1 <= B <= K, both finite.
                        for (const HullLightmapWiden bad:{HullLightmapWiden{1.0f,1.0f},HullLightmapWiden{8.5f,1.0f},HullLightmapWiden{3.0f,0.5f},HullLightmapWiden{3.0f,3.5f},
                                                          HullLightmapWiden{std::numeric_limits<float>::quiet_NaN(),1.0f},HullLightmapWiden{3.0f,std::numeric_limits<float>::infinity()}}) {
                            Words rejected{91,92}; bool rf=true, rg=true, rw=true;
                            require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),fills[f],gain,rejected,depth,rf,rg,true,&bad,&rw)==LinearMaterialResult::InvalidConfig &&
                                    rejected==Words({91,92}) && !rf && !rg && !rw,"widening with K or B out of range is InvalidConfig");
                        }
                        // B = 1 and B = K differ only in the c210.z literal (B - 1).
                        if (gain_applied) {
                            const HullLightmapWiden unit{widen.k,1.0f}; Words plain{0x12345678}; bool pf=false, pg=false, pw=false;
                            require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),fills[f],gain,plain,depth,pf,pg,true,&unit,&pw)==LinearMaterialResult::Applied && pw && plain.size()==widened.size(),"B = 1 variant");
                            unsigned differing=0; std::size_t where=0;
                            for (std::size_t i=0;i<plain.size();++i) if (plain[i]!=widened[i]) { ++differing; where=i; }
                            require(differing==1 && plain[where]==0u && widened[where]==0x40000000u && where>=4 && widened[where-4]==0x05000051u && (widened[where-3]&0x7ffu)==210u,"B = 1 against B = 3: only the c210.z literal (0 against 2)");
                        }
                    }
                    Words alias=original; bool alias_fill=false, alias_gain=false;
                    require(linear_material_hull_lightmap_gain_pixel_variant(alias.data(),alias.size(),fills[f],gain,alias,depth,alias_fill,alias_gain)==LinearMaterialResult::Applied &&
                            alias==result && alias_fill==fill_applied && alias_gain==gain_applied,"input/output alias");
                    write(out/(name+"-hlgain-"+std::to_string(f)+"-"+std::to_string(depth)+".bin"),result);
                }
            }
            require(gain_applied_any!=gain_refused_any,"one verdict per program across K and depth");
            // The sun-share producer composed with the gain (the lane's variant):
            // gain 1 is the share variant byte for byte, the same verdict, +10 words.
            unsigned share_slots[2]{}, share_variant_slots[2]{}; bool share_gain_any=false, share_refused_any=false;
            for (unsigned f=0;f<2;++f) {
                Words share; bool share_applied=false;
                require(linear_material_original_sun_share_pixel_variant(original.data(),original.size(),fills[f],share,true,share_applied)==LinearMaterialResult::Applied && share_applied,"share control");
                write(out/(name+"-hlshare-"+std::to_string(f)+".bin"),share);
                share_slots[f]=weighted_slots(share).second;
                Words identity{0x12345678}; bool identity_share=false, identity_gain=true;
                require(linear_material_original_sun_share_pixel_variant(original.data(),original.size(),fills[f],identity,true,identity_share,1.0f,&identity_gain)==LinearMaterialResult::Applied &&
                        identity==share && identity_share && !identity_gain,"G=1 is the share variant byte for byte");
                Words result{0x12345678}; bool result_share=false, result_gain=false;
                require(linear_material_original_sun_share_pixel_variant(original.data(),original.size(),fills[f],result,true,result_share,gain,&result_gain)==LinearMaterialResult::Applied && result_share,"gained share admission");
                require(original==saved,"immutable input");
                if (!result_gain) { share_refused_any=true; require(result==share,"a program without the term keeps the share variant byte for byte"); }
                else {
                    share_gain_any=true; require(result.size()==share.size()+10,"one DEF and one MUL added to the share variant");
                    share_variant_slots[f]=weighted_slots(result).second; require(share_variant_slots[f]<=512,"minimum SM3 static slot budget");
                    max_slots=std::max(max_slots,share_variant_slots[f]);
                }
                {
                    Words dynamic{0x12345678}; bool dynamic_share=false, dynamic_gain=!result_gain;
                    require(linear_material_original_sun_share_pixel_variant(original.data(),original.size(),fills[f],dynamic,true,dynamic_share,gain,&dynamic_gain,true)==LinearMaterialResult::Applied &&
                            dynamic_share && dynamic_gain==result_gain && dynamic==(result_gain?expected_dynamic(result):share),"dynamic gained share: no DEF, the MUL reads c217.w");
                    Words widened{0x12345678}; bool widen_share=false, widen_gain=!result_gain, widen_applied=!result_gain;
                    require(linear_material_original_sun_share_pixel_variant(original.data(),original.size(),fills[f],widened,true,widen_share,gain,&widen_gain,true,&widen,&widen_applied)==LinearMaterialResult::Applied &&
                            widen_share && widen_gain==result_gain && widen_applied==result_gain,"widened share: the gain's verdict");
                    if (result_gain) {
                        require(widened.size()==dynamic.size()+widen_words,"widened share: +131 DWORDs");
                        share_widen_slots[f]=weighted_slots(widened).second;
                        require(share_widen_slots[f]==share_variant_slots[f]+widen_slots_added && share_widen_slots[f]<=512,"widened share: +35 slots, within 512");
                        max_widen_slots=std::max(max_widen_slots,share_widen_slots[f]);
                    } else require(widened==share,"widened share: a program without the term keeps the share variant byte for byte");
                    write(out/(name+"-hlsharewiden-"+std::to_string(f)+".bin"),widened);
                }
                write(out/(name+"-hlsharegain-"+std::to_string(f)+".bin"),result);
            }
            require(share_gain_any==gain_applied_any && share_refused_any==gain_refused_any,"the share producer takes the same verdict");
            for (float invalid:{0.999f,0.0f,-4.0f,8.001f,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                Words guard{91,92}; bool guard_fill=true, guard_gain=true;
                require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),0.05f,invalid,guard,true,guard_fill,guard_gain)==LinearMaterialResult::InvalidConfig &&
                        guard==Words({91,92}) && !guard_fill && !guard_gain,"invalid gain rollback");
                Words alias=original;
                require(linear_material_hull_lightmap_gain_pixel_variant(alias.data(),alias.size(),0.05f,invalid,alias,true,guard_fill,guard_gain)==LinearMaterialResult::InvalidConfig &&
                        alias==saved,"invalid gain alias rollback");
            }
            for (std::size_t offset:{std::size_t(0),std::size_t(2),original.size()/2,original.size()-1}) {
                auto broken=original; broken[offset]^=1; const auto before=broken; bool broken_fill=true, broken_gain=true;
                require(linear_material_hull_lightmap_gain_pixel_variant(broken.data(),broken.size(),0.05f,gain,broken,true,broken_fill,broken_gain)!=LinearMaterialResult::Applied &&
                        broken==before && !broken_fill && !broken_gain,"corrupted original alias rollback");
            }
            {
                Words guard{91,92}; bool guard_fill=true, guard_gain=true;
                require(linear_material_hull_lightmap_gain_pixel_variant(nullptr,original.size(),0.05f,gain,guard,true,guard_fill,guard_gain)==LinearMaterialResult::InvalidInput && guard==Words({91,92}) && !guard_fill && !guard_gain,"null rollback");
                require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),1,0.05f,gain,guard,true,guard_fill,guard_gain)==LinearMaterialResult::InvalidInput && guard==Words({91,92}),"short rollback");
                require(linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size()-1,0.05f,gain,guard,true,guard_fill,guard_gain)!=LinearMaterialResult::Applied && guard==Words({91,92}),"truncation rollback");
            }
            if (gain_applied_any) ++applied; else ++untouched;
            auto pair=[](const unsigned (&v)[2][2],unsigned f){ return "["+std::to_string(v[f][0])+","+std::to_string(v[f][1])+"]"; };
            std::cout<<(first?"":",")<<"{\"name\":\""<<name<<"\",\"words\":"<<original.size()<<",\"status\":0,\"gain_applied\":"<<(gain_applied_any?1:0)<<",\"widen_applied\":"<<(widen_applied_any?1:0)
                     <<",\"share_gain_applied\":"<<(share_gain_any?1:0)<<",\"share_slots\":["<<share_slots[0]<<','<<share_slots[1]<<"],\"share_variant_slots\":["<<share_variant_slots[0]<<','<<share_variant_slots[1]<<']'
                     <<",\"base_slots\":["<<pair(base_slots,0)<<','<<pair(base_slots,1)<<"],\"variant_slots\":["<<pair(variant_slots,0)<<','<<pair(variant_slots,1)<<']'
                     <<",\"base_instructions\":["<<pair(base_instructions,0)<<','<<pair(base_instructions,1)<<"],\"variant_instructions\":["<<pair(variant_instructions,0)<<','<<pair(variant_instructions,1)<<']'
                     <<",\"widen_slots\":["<<pair(widen_slots,0)<<','<<pair(widen_slots,1)<<"],\"share_widen_slots\":["<<share_widen_slots[0]<<','<<share_widen_slots[1]<<"]}";
            first=false;
        }
        Words authored{0xffff0300u,0xffffu}, result{91,92}; bool authored_fill=true, authored_gain=true;
        require(linear_material_hull_lightmap_gain_pixel_variant(authored.data(),authored.size(),0.05f,gain,result,true,authored_fill,authored_gain)==LinearMaterialResult::UnsupportedShader &&
                result==Words({91,92}) && !authored_fill && !authored_gain,"unreviewed valid framing");
        std::cout<<"],\"programs\":"<<names.size()<<",\"supported\":"<<supported<<",\"applied\":"<<applied<<",\"untouched\":"<<untouched<<",\"gain\":"<<gain
                 <<",\"max_variant_slots\":"<<max_slots<<",\"max_widen_slots\":"<<max_widen_slots<<",\"flow_control_checks\":"<<flow_checks<<",\"flow_control\":{"<<flow_json<<"},\"dynamic_checks\":"<<dynamic_checks<<",\"checks\":"<<checks<<",\"creates_ns\":"<<create_ns<<"}\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
