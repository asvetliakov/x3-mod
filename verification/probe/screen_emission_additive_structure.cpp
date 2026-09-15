// Host-only structure driver for the AdditiveGain outputs of the nine SM1
// bullet pairs (--screen-emission-additive, screen-emission-region.md
// "Additive option"). Separate from linear_emission_sm1_structure.cpp so the
// SM1 sweep's pinned counters keep their meaning. Game bytes and the written
// programs stay in the local corpus; only the report goes to the oracle.
#include "../../src/renderer/linear_emission_sm1.cpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace x3m::renderer;
unsigned checks=0;
void require(bool b,const char* why) { ++checks;if(!b) throw std::runtime_error(why); }
Words read(const std::string& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f),"open original");
    auto n=f.tellg();require(n>0 && n%4==0,"word alignment");Words w(static_cast<std::size_t>(n)/4);f.seekg(0);
    f.read(reinterpret_cast<char*>(w.data()),n);require(bool(f),"read original");return w;
}
void write(const std::string& path,const Words& words) {
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(words.data()),
        static_cast<std::streamsize>(words.size()*4));require(bool(f),"write variant");
}
std::string hex(std::uint64_t h) { char b[17];std::snprintf(b,sizeof b,"%016llx",static_cast<unsigned long long>(h));return b; }
int main(int argc,char** argv) {
    try {
        require(argc==3,"usage: screen_emission_additive_structure <original directory> <local output directory>");
        const float gains[]={1.f,2.f,3.5f,8.f};
        unsigned variants=0,identical=0,max_words=0,max_arithmetic=0;long long ns=0;
        std::string rows;
        for(unsigned index=0;index<9;++index) {
            const auto& pair=pairs[index];
            require(linear_emission_sm1_pair_reviewed(pair.vertex,pair.pixel),"reviewed pair");
            const auto vertex=read(std::string(argv[1])+"/vs_"+hex(pair.vertex)+".bin");
            require(fingerprint(vertex.data(),vertex.size())==pair.vertex && vertex[0]==0xfffe0101u,"native VS identity");
            const auto original=read(std::string(argv[1])+"/ps_"+hex(pair.pixel)+".bin");
            const auto saved=original;
            require(fingerprint(original.data(),original.size())==pair.pixel && original[0]==0xffff0101u,"native PS identity");
            const Profile* profile=nullptr;
            for(const auto& p:profiles) if(p.hash==pair.pixel) profile=&p;
            require(profile && original.size()==profile->count,"profile row");
            // Reference: the same native PS2 path without the gain. The oracle
            // compares the AdditiveGain words against it word for word.
            Words native;
            require(linear_emission_sm1_pixel_variant(original.data(),original.size(),
                {1.f,LinearEmissionSm1Outputs::Native,false},native)==LinearEmissionResult::Applied,"native reference");
            write(std::string(argv[2])+"/additive_"+std::to_string(index)+"-native.bin",native);
            for(unsigned g=0;g<4;++g) {
                LinearEmissionSm1Config config{gains[g],LinearEmissionSm1Outputs::AdditiveGain,false};
                Words output={91,92};const Words sentinel=output;
                // The runtime binds the original program at G = 1 and creates no
                // variant (motion_output.cpp: screen_additive_gain_ != 1.f), so
                // the bound program at G = 1 is the original's bytes.
                if(gains[g]==1.f) { output=original;++identical; }
                else {
                    const auto start=std::chrono::steady_clock::now();
                    const auto status=linear_emission_sm1_pixel_variant(original.data(),original.size(),config,output);
                    ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                    if(status!=LinearEmissionResult::Applied) std::cerr<<hex(pair.pixel)<<" status="<<static_cast<int>(status)<<'\n';
                    require(status==LinearEmissionResult::Applied,"additive promotion");
                    require(original==saved,"input mutated");
                    require(output.size()==native.size()+10,"one DEF and one MUL added");
                    Budget budget;
                    require(generated_shape(output,budget,1) && budget.outputs==1 && budget.textures==1,"authored single-output shape");
                    max_arithmetic=std::max(max_arithmetic,budget.arithmetic);
                    auto alias=original;
                    require(linear_emission_sm1_pixel_variant(alias.data(),alias.size(),config,alias)==LinearEmissionResult::Applied && alias==output,"alias success");
                    Words again=sentinel;
                    require(linear_emission_sm1_pixel_variant(output.data(),output.size(),config,again)!=LinearEmissionResult::Applied && again==sentinel,"re-promotion refused");
                }
                max_words=std::max(max_words,unsigned(output.size()));
                write(std::string(argv[2])+"/additive_"+std::to_string(index)+"-"+std::to_string(g)+".bin",output);
                ++variants;
            }
            // Out-of-range gain and the partial-precision request are refused
            // with the caller's output preserved, including when it aliases.
            Words output={91,92};const Words sentinel=output;
            for(float invalid:{0.f,0.5f,0.999f,8.001f,16.f,-1.f,
                    std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                require(linear_emission_sm1_pixel_variant(original.data(),original.size(),
                    {invalid,LinearEmissionSm1Outputs::AdditiveGain,false},output)==LinearEmissionResult::InvalidConfig && output==sentinel,"invalid additive gain");
                auto alias=original;
                require(linear_emission_sm1_pixel_variant(alias.data(),alias.size(),
                    {invalid,LinearEmissionSm1Outputs::AdditiveGain,false},alias)==LinearEmissionResult::InvalidConfig && alias==saved,"invalid additive gain alias rollback");
            }
            require(linear_emission_sm1_pixel_variant(original.data(),original.size(),
                {2.f,LinearEmissionSm1Outputs::AdditiveGain,true},output)==LinearEmissionResult::InvalidConfig && output==sentinel,"additive partial precision refused");
            require(linear_emission_sm1_pixel_variant(nullptr,original.size(),
                {2.f,LinearEmissionSm1Outputs::AdditiveGain,false},output)==LinearEmissionResult::InvalidInput && output==sentinel,"null rollback");
            for(std::size_t n=0;n<original.size();++n) {
                require(linear_emission_sm1_pixel_variant(original.data(),n,
                    {2.f,LinearEmissionSm1Outputs::AdditiveGain,false},output)!=LinearEmissionResult::Applied && output==sentinel,"truncation rollback");
            }
            rows+=std::string(rows.empty()?"":",")+"{\"vertex\":\""+hex(pair.vertex)+"\",\"pixel\":\""+hex(pair.pixel)+
                  "\",\"scalar\":"+(profile->scalar?"true":"false")+",\"original_words\":"+std::to_string(original.size())+"}";
        }
        // A PS2 program of the live emission route is outside this registry.
        {
            auto w=read(std::string(argv[1])+"/ps_8360f422de08b5bd.bin");Words result={7};const Words sentinel=result;
            require(linear_emission_sm1_pixel_variant(w.data(),w.size(),
                {2.f,LinearEmissionSm1Outputs::AdditiveGain,false},result)==LinearEmissionResult::UnsupportedShader &&
                result==sentinel,"SM2 original refused by the SM1 registry");
        }
        std::cout<<"{\"pairs\":9,\"programs\":6,\"gains\":4,\"variants\":"<<variants<<",\"identical\":"<<identical
                 <<",\"checks\":"<<checks<<",\"max_words\":"<<max_words<<",\"max_arithmetic\":"<<max_arithmetic
                 <<",\"create_ns\":"<<ns<<",\"rows\":["<<rows<<"]}\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
