// Host-only driver of the original-shading share producer
// (linear_material_original_sun_share_pixel_variant, docs/architecture/
// legacy-sun-application.md 1). Reads every ps_/vs_ program of the local
// corpus, writes the share variants beside the plain motion and fill
// controls in a caller-supplied local folder, and prints one JSON object
// with per-program rows (seed/final DWORDs for the Python oracle). Including
// the implementation keeps the synthetic planner refusals out of the public
// API. No game bytes are embedded; unreviewed programs are reported.
#include "../../src/renderer/linear_material.cpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace x3m::renderer;
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
// Independent weighted-slot walk (Microsoft SM3 slot table) and the highest
// temporary the program touches.
struct Budget { unsigned instructions, slots, max_temp; };
Budget budget(const Words& result) {
    Budget b{0,0,0}; std::array<unsigned,16> dimensions{};
    for(std::size_t at=1;at<result.size()-1;) {
        const auto token=result[at], op=token&0xffffu;
        const unsigned count=op==0xfffeu?(token>>16)&0x7fffu:(token>>24)&15u;
        if(op==31 && kind(result[at+2])==10) dimensions.at(index(result[at+2]))=(result[at+1]>>27)&15;
        if(op!=0xfffeu && op!=31 && op!=81) {
            ++b.instructions;
            for (unsigned n=1;n<=count;++n) if (kind(result[at+n])==temp) b.max_temp=std::max(b.max_temp,index(result[at+n]));
            switch(op) {
            case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9:
            case 10: case 11: case 12: case 14: case 15: case 35: case 42: case 43: case 46: case 88:
                b.slots+=1; break;
            case 18: case 39: case 90: b.slots+=2; break;
            case 32: case 36: case 38: case 40: case 41: b.slots+=3; break;
            case 66: {
                const auto dimension=dimensions.at(index(result[at+3]));
                require(dimension==2 || dimension==3,"known sample slot dimension");
                b.slots+=dimension==3?4:1; break;
            }
            default: require(false,"known SM3 slot cost");
            }
        }
        at+=count+1;
    }
    require(result.back()==0xffffu,"END token");
    return b;
}
// Synthetic planner witnesses: one seed MAD, one albedo multiply, one final
// MAD with a non-sun addend; mutations exercise the contract's refusals.
struct Synthetic { Words code; Structure s; unsigned seed=0, site=0, final=0; };
Synthetic synthetic(unsigned mutation=0, bool branch=false) {
    Synthetic f; f.code={0xffff0300u};
    emit(f.code,dcl,{0x80000005u,dst(input,0)});
    if (mutation==5) emit(f.code,def,{dst(constant,221,xyzw),0,0,0,0});
    if (mutation==6) emit(f.code,mov,{dst(temp,3),src(temp,20)});
    f.seed=static_cast<unsigned>(f.code.size()+3);
    emit(f.code,mad,{dst(temp,0)|(mutation==1?sat:0u),lane(temp,1,3),src(constant,5),src(temp,2)});
    if (branch) emit(f.code,40,{src(14,0)});
    f.site=static_cast<unsigned>(f.code.size());
    emit(f.code,mul,{dst(temp,0)|pp,src(temp,0),src(temp,mutation==2?0:3)});
    if (mutation==3) emit(f.code,texld,{dst(temp,4),src(temp,0),src(10,0)});
    if (branch) { emit(f.code,42,{}); emit(f.code,mov,{dst(temp,0),src(temp,4)}); emit(f.code,43,{}); }
    f.final=static_cast<unsigned>(f.code.size());
    emit(f.code,mad,{dst(color_output,0)|pp,src(temp,0),src(temp,5),src(temp,6)});
    emit(f.code,mul,{dst(color_output,0,8)|pp,lane(temp,1,3),lane(constant,7,0)});
    f.code.push_back(end_token);
    if (mutation==3) { Words with{0xffff0300u}; emit(with,dcl,{0x90000000u,dst(10,0,xyzw)}); with.insert(with.end(),f.code.begin()+1,f.code.end()); f.code.swap(with); f.seed+=3; f.site+=3; f.final+=3; }
    // Mutation 6 reads a private carrier; only the non-original walk admits r20.
    require(structure(f.code.data(),f.code.size(),false,f.s,mutation!=6,default_abi,7,false,false,branch),"synthetic structure");
    return f;
}
unsigned synthetic_checks() {
    const auto plan=[](const Synthetic& f, std::array<unsigned,2> seeds, unsigned fill_site, unsigned fill_sum, std::vector<SunEdit>* out=nullptr) {
        std::vector<SunEdit> edits; Word final_destination=0;
        const bool ok=original_share_resources_free(f.code.data(),f.s) &&
            original_sun_plan(f.code.data(),f.s,seeds,f.final,fill_site,fill_sum,edits,final_destination);
        if (out) *out=edits;
        if (ok) require(final_destination==(dst(temp,11)|pp),"final redirected into r11 keeping _pp");
        return ok;
    };
    unsigned refused=0;
    { const auto f=synthetic(); std::vector<SunEdit> edits;
      require(plan(f,{f.seed,0},0,0,&edits),"valid synthetic plan");
      // init(8) at the seed MAD + seed MUL, one propagation, the final MAD's parallel, the reduction edit.
      require(edits.size()==4 && edits[0].at==f.seed-3 && edits[0].words.size()==8*3+4 && edits[1].at==f.site &&
              edits[2].at==f.final && edits[3].at==f.final+5 && edits[3].words.size()>3,"plan edit sites");
      require(plan(f,{f.seed,0},f.site,0),"fill site with a tracked lobe sum");
      refused+=!plan(f,{f.seed,0},f.site,3); // untracked fill sum
      refused+=!plan(f,{0,0},0,0);            // no seed
      refused+=!plan(f,{f.seed,f.seed},0,0);  // duplicate seed site
      refused+=!plan(f,{f.seed+1,0},0,0);     // seed on the scalar operand
      refused+=!plan(f,{f.seed-3,0},0,0);     // seed offset off the operand
    }
    for (unsigned mutation:{1u,2u,3u,5u,6u}) { const auto f=synthetic(mutation); refused+=!plan(f,{f.seed,0},0,0); }
    { const auto f=synthetic(0,true); refused+=!plan(f,{f.seed,0},0,0); } // final inside a branch is fine, the other arm kills; here the branch arm MOV reads untracked r4: accepted
    require(refused==10,"synthetic refusals: untracked fill sum, no/duplicate/misplaced seeds, _sat, sun*sun, sun texcoord, c221 DEF, r20 read");
    // Reserved-range check on emitted words: a motion-like body touching r11
    // or c212/c215/c221 is refused; the authored fill block passes as itself.
    { Words body; emit(body,mov,{dst(temp,9),src(temp,5)}); emit(body,mov,{dst(color_output,2),src(temp,9)});
      require(original_share_range_free(body.data(),0,body.size(),false,0,0),"motion body inside r0-r10");
      Words bad=body; emit(bad,mov,{dst(temp,11),src(temp,5)});
      require(!original_share_range_free(bad.data(),0,bad.size(),false,0,0),"motion body reaching r11 refused"); ++refused;
      Words reads=body; emit(reads,mov,{dst(temp,9),lane(constant,221,0)});
      require(!original_share_range_free(reads.data(),0,reads.size(),false,0,0),"motion body reading c221 refused"); ++refused;
      Words block; original_fill_block(block,2,5);
      require(original_share_range_free(block.data(),0,block.size(),true,2,5),"authored fill block inside r12/r13/sum, c215/light");
      require(!original_share_range_free(block.data(),0,block.size(),true,3,5),"fill block on another sum refused"); ++refused; }
    return refused;
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"usage: original_sun_share_structure <original directory> <local output directory>");
        const fs::path corpus=argv[1], out=argv[2];
        const float fills[]={0.0f,0.05f};
        const unsigned refusals=synthetic_checks();
        std::vector<std::string> names;
        for (const auto& entry:fs::directory_iterator(corpus)) {
            const auto name=entry.path().filename().string();
            if (entry.is_regular_file() && name.size()>7 && name.compare(name.size()-4,4,".bin")==0 &&
                (name.rfind("ps_",0)==0 || name.rfind("vs_",0)==0)) names.push_back(name.substr(0,name.size()-4));
        }
        std::sort(names.begin(),names.end());
        require(!names.empty(),"corpus has programs");
        std::cout<<"{\"rows\":[";
        bool first=true; unsigned applied=0, supported=0, max_slots=0, max_temp=0; long long create_ns=0;
        for (const auto& name:names) {
            const auto original=read(corpus/(name+".bin"));
            const auto saved=original;
            Words probe{91,92}; bool entered=true;
            const auto begin=std::chrono::steady_clock::now();
            const auto status=linear_material_original_sun_share_pixel_variant(original.data(),original.size(),0.05f,probe,true,entered);
            create_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
            require(original==saved,"immutable input");
            if (status!=LinearMaterialResult::Applied) {
                require(probe==Words({91,92}) && !entered,"refusal leaves output intact and reports no share");
                std::cout<<(first?"":",")<<"{\"name\":\""<<name<<"\",\"words\":"<<original.size()<<",\"status\":"<<int(status)<<"}";
                first=false; continue;
            }
            ++supported;
            const auto hash=material_motion_fingerprint(original.data(),original.size());
            const XtPixel* xt=original.size()>=1561?xt_pixel(hash):nullptr;
            const Pixel* p=xt?nullptr:pixel_for(hash,original.size());
            require(xt||p,"admitted program has a row");
            std::vector<unsigned> seeds; unsigned final_rgb=xt?xt->final_rgb:p->final_rgb, light=xt?6u:p->light0;
            if (xt) { for (const auto& u:xt->lights) if (u.value==6) seeds.push_back(u.operand); }
            else for (unsigned k=0;k<2;++k) if (p->color_source[k]) seeds.push_back(p->color_source[k]);
            unsigned slots[2][2]{}, instructions[2][2]{}, control_slots[2][2]{}, temps[2][2]{};
            bool share_all=true;
            for (bool depth:{false,true}) {
                Words motion;
                require(material_motion_pixel_variant(original.data(),original.size(),motion,depth)==MaterialMotionResult::Applied,"motion control");
                write(out/(name+"-motion-"+std::to_string(depth)+".bin"),motion);
                for (unsigned f=0;f<2;++f) {
                    Words control{7}; bool fill_applied=true;
                    require(linear_material_original_fill_pixel_variant(original.data(),original.size(),fills[f],control,depth,fill_applied)==LinearMaterialResult::Applied &&
                            fill_applied==(f>0),"fill control");
                    if (f==0) require(control==motion,"K=0 fill control is the motion variant");
                    write(out/(name+"-ofill-"+std::to_string(f)+"-"+std::to_string(depth)+".bin"),control);
                    Words result{0x12345678}; bool share=false;
                    require(linear_material_original_sun_share_pixel_variant(original.data(),original.size(),fills[f],result,depth,share)==LinearMaterialResult::Applied,"share admission for every K");
                    require(original==saved,"immutable input");
                    share_all=share_all&&share;
                    if (!share) require(result==control,"a refused plan keeps the fill/motion variant");
                    const auto b=budget(result), cb=budget(control);
                    require(b.slots<=512 && b.max_temp<=23,"SM3 slot budget and r0-r23");
                    slots[depth][f]=b.slots; instructions[depth][f]=b.instructions; control_slots[depth][f]=cb.slots; temps[depth][f]=b.max_temp;
                    max_slots=std::max(max_slots,b.slots); max_temp=std::max(max_temp,b.max_temp);
                    Words alias=original; bool alias_share=false;
                    require(linear_material_original_sun_share_pixel_variant(alias.data(),alias.size(),fills[f],alias,depth,alias_share)==LinearMaterialResult::Applied &&
                            alias==result && alias_share==share,"input/output alias");
                    write(out/(name+"-oshare-"+std::to_string(f)+"-"+std::to_string(depth)+".bin"),result);
                }
            }
            for (float invalid:{-0.001f,0.5001f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                Words guard{91,92}; bool guard_share=true;
                require(linear_material_original_sun_share_pixel_variant(original.data(),original.size(),invalid,guard,true,guard_share)==LinearMaterialResult::InvalidConfig &&
                        guard==Words({91,92}) && !guard_share,"invalid fill rollback");
                Words alias=original;
                require(linear_material_original_sun_share_pixel_variant(alias.data(),alias.size(),invalid,alias,true,guard_share)==LinearMaterialResult::InvalidConfig && alias==saved,"invalid fill alias rollback");
            }
            for (std::size_t offset:{std::size_t(0),std::size_t(2),original.size()/2,original.size()-1}) {
                auto broken=original; broken[offset]^=1; const auto before=broken; bool broken_share=true;
                require(linear_material_original_sun_share_pixel_variant(broken.data(),broken.size(),0.05f,broken,true,broken_share)!=LinearMaterialResult::Applied &&
                        broken==before && !broken_share,"corrupted original alias rollback");
            }
            {
                Words guard{91,92}; bool guard_share=true;
                require(linear_material_original_sun_share_pixel_variant(nullptr,original.size(),0.05f,guard,true,guard_share)==LinearMaterialResult::InvalidInput && guard==Words({91,92}) && !guard_share,"null rollback");
                require(linear_material_original_sun_share_pixel_variant(original.data(),1,0.05f,guard,true,guard_share)==LinearMaterialResult::InvalidInput && guard==Words({91,92}),"short rollback");
                require(linear_material_original_sun_share_pixel_variant(original.data(),original.size()-1,0.05f,guard,true,guard_share)!=LinearMaterialResult::Applied && guard==Words({91,92}),"truncation rollback");
            }
            if (share_all) ++applied;
            std::cout<<(first?"":",")<<"{\"name\":\""<<name<<"\",\"words\":"<<original.size()<<",\"status\":0,\"share_applied\":"<<(share_all?1:0)
                     <<",\"xt\":"<<(xt?1:0)<<",\"light\":"<<light<<",\"final_rgb\":"<<final_rgb<<",\"seeds\":[";
            for (std::size_t k=0;k<seeds.size();++k) std::cout<<(k?",":"")<<seeds[k];
            std::cout<<"],\"slots\":[["<<slots[0][0]<<','<<slots[0][1]<<"],["<<slots[1][0]<<','<<slots[1][1]<<"]]"
                     <<",\"control_slots\":[["<<control_slots[0][0]<<','<<control_slots[0][1]<<"],["<<control_slots[1][0]<<','<<control_slots[1][1]<<"]]"
                     <<",\"instructions\":[["<<instructions[0][0]<<','<<instructions[0][1]<<"],["<<instructions[1][0]<<','<<instructions[1][1]<<"]]"
                     <<",\"max_temp\":[["<<temps[0][0]<<','<<temps[0][1]<<"],["<<temps[1][0]<<','<<temps[1][1]<<"]]}";
            first=false;
        }
        Words authored{0xffff0300u,0xffffu}, result{91,92}; bool authored_share=true;
        require(linear_material_original_sun_share_pixel_variant(authored.data(),authored.size(),0.05f,result,true,authored_share)==LinearMaterialResult::UnsupportedShader &&
                result==Words({91,92}) && !authored_share,"unreviewed valid framing");
        std::cout<<"],\"programs\":"<<names.size()<<",\"supported\":"<<supported<<",\"applied\":"<<applied<<",\"max_slots\":"<<max_slots<<",\"max_temp\":"<<max_temp
                 <<",\"synthetic_refusals\":"<<refusals<<",\"checks\":"<<checks<<",\"creates_ns\":"<<create_ns<<"}\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
