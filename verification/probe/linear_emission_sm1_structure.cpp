// Host-only structure proof. Game bytes/outputs remain in the local corpus.
#include "../../src/renderer/linear_emission_sm1.cpp"
#include <chrono>
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
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(words.data()),words.size()*4);require(bool(f),"write variant");
}
std::string hex(std::uint64_t h) { char b[17];std::snprintf(b,sizeof b,"%016llx",static_cast<unsigned long long>(h));return b; }
int main(int argc,char** argv) {
    try {
        require(argc==3,"originals/output directories");
        const float gains[]={0,.25f,1,4,16};unsigned variants=0,max_words=0,max_slots[4]={};long long ns=0;
        for(const auto& p:profiles) {
            auto original=read(std::string(argv[1])+"/ps_"+hex(p.hash)+".bin");const auto saved=original;
            require(original.size()==p.count && fingerprint(original.data(),original.size())==p.hash,"original identity");
            require(original_shape(original.data(),original.size(),p.scalar),"native shape");
            // Every executable word is proved without relying on a hash gate.
            for(unsigned at=39;at<original.size();++at) {
                auto broken=original;broken[at]^=1;
                require(!original_shape(broken.data(),broken.size(),p.scalar),"native shape mutation accepted");
            }
            auto broken=original;broken[1]^=1u<<16;
            require(!original_shape(broken.data(),broken.size(),p.scalar),"comment framing accepted");
            broken=original;broken[2]^=1;
            require(original_shape(broken.data(),broken.size(),p.scalar),"opaque comment interpreted");
            Words output={1,2,3};const Words sentinel=output;
            require(linear_emission_sm1_pixel_variant(broken.data(),broken.size(),{},output)==LinearEmissionResult::UnsupportedShader && output==sentinel,"opaque fingerprint bypass");
            for(unsigned g=0;g<5;++g) for(unsigned outputs=1;outputs<=4;++outputs) for(unsigned precision=0;precision<2;++precision) {
                if(outputs==4 && precision) continue;
                LinearEmissionSm1Config config{gains[g],static_cast<LinearEmissionSm1Outputs>(outputs),bool(precision)};
                const auto start=std::chrono::steady_clock::now();
                require(linear_emission_sm1_pixel_variant(original.data(),original.size(),config,output)==LinearEmissionResult::Applied,"promotion");
                ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
                require(original==saved,"input mutated");Budget budget;
                require(generated_shape(output,budget,outputs),"authored shape");
                max_slots[outputs-1]=std::max(max_slots[outputs-1],budget.arithmetic);
                max_words=std::max(max_words,unsigned(output.size()));
                const auto promoted=output;
                write(std::string(argv[2])+"/sm1_"+hex(p.hash)+"-"+std::to_string(g)+"-"+std::to_string(outputs)+"-"+std::to_string(precision)+".bin",output);
                auto alias=original;
                require(linear_emission_sm1_pixel_variant(alias.data(),alias.size(),config,alias)==LinearEmissionResult::Applied && alias==promoted,"alias success");
                require(linear_emission_sm1_pixel_variant(output.data(),output.size(),config,output)!=LinearEmissionResult::Applied && output==promoted,"re-promotion or alias failure");
                // Legal framing with illegal resource/output/read shapes must fail.
                for(std::size_t at=39;at<output.size()-1;) {
                    unsigned op=output[at]&0xffff,n=(output[at]>>24)&15;
                    if(op!=def && op!=dcl) {
                        auto bad=output;bad[at+1]=(bad[at+1]&~0x7ffu)|12u;Budget b;
                        require(!generated_shape(bad,b,outputs),"resource mutation accepted");
                        bad=output;bad[at]|=coissue;b={};
                        require(!generated_shape(bad,b,outputs),"SM1 coissue leaked into PS2");
                        if(outputs==4) {
                            bad=output;bad[at+1]|=pp;b={};
                            require(!generated_shape(bad,b,outputs),"packed precision relaxation");
                        }
                    }
                    at+=n+1;
                }
                ++variants;
            }
            for(float invalid:{-1.f,17.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                output=sentinel;require(linear_emission_sm1_pixel_variant(original.data(),original.size(),{invalid},output)==LinearEmissionResult::InvalidConfig && output==sentinel,"invalid gain");
            }
            output=sentinel;
            require(linear_emission_sm1_pixel_variant(original.data(),original.size(),{1,LinearEmissionSm1Outputs::PackedScreen,true},output)==LinearEmissionResult::InvalidConfig && output==sentinel,"packed PP refused");
            for(int invalid:{0,5,-1}) {
                output=sentinel;require(linear_emission_sm1_pixel_variant(original.data(),original.size(),{1,static_cast<LinearEmissionSm1Outputs>(invalid)},output)==LinearEmissionResult::InvalidConfig && output==sentinel,"invalid mode");
            }
            output=sentinel;require(linear_emission_sm1_pixel_variant(original.data(),60,{},output)==LinearEmissionResult::UnsupportedShader && output==sentinel,"bound");
            require(linear_emission_sm1_pixel_variant(nullptr,59,{},output)==LinearEmissionResult::InvalidInput && output==sentinel,"null");
            for(std::size_t n=0;n<original.size();++n) {output=sentinel;require(linear_emission_sm1_pixel_variant(original.data(),n,{},output)!=LinearEmissionResult::Applied && output==sentinel,"truncation");}
        }
        unsigned accepted=0;
        for(const auto& v:pairs) {
            const auto original=read(std::string(argv[1])+"/vs_"+hex(v.vertex)+".bin");
            require(fingerprint(original.data(),original.size())==v.vertex && original[0]==0xfffe0101u,"native VS identity/version");
            for(const auto& p:profiles) {
                const bool found=linear_emission_sm1_pair_reviewed(v.vertex,p.hash);
                require(found==(v.pixel==p.hash),"exact cross-pair admission");accepted+=found;
            }
            require(!linear_emission_pair_reviewed(v.vertex,v.pixel),"SM1 leaked into live SM2 registry");
        }
        require(accepted==9 && !linear_emission_sm1_pair_reviewed(0,0),"nine pair inventory");
        // Recreate every currently qualified SM2 output for independent Python
        // comparison with the accepted GPU record. Old source remains unchanged.
        const char* old[]={"8360f422de08b5bd","9975b706e5a1c999","ff2473e73a6bdfa1","8559522220507d5e","875e780adb131b16","39f3b4d5b6a5aaed","47e15e20d63b0e93","846c5c1a549f9491","c6dacb8f74b65c97","f0c91793a75e1203"};
        for(const auto name:old) {
            auto w=read(std::string(argv[1])+"/ps_"+name+".bin");Words result;
            require(linear_emission_sm1_pixel_variant(w.data(),w.size(),{},result)==LinearEmissionResult::UnsupportedShader,"SM2 admitted by probe registry");
            for(unsigned g=0;g<5;++g) for(unsigned c=0;c<2;++c) {
                require(linear_emission_pixel_variant(w.data(),w.size(),{gains[g],bool(c)},result)==LinearEmissionResult::Applied,"old SM2 transform");
                write(std::string(argv[2])+"/ps_"+name+"-"+std::to_string(g)+(c?"-coverage.bin":".bin"),result);
            }
        }
        std::cout<<"{\"variants\":"<<variants<<",\"pairs\":"<<accepted<<",\"checks\":"<<checks<<",\"max_words\":"<<max_words<<",\"max_slots\":["<<max_slots[0]<<','<<max_slots[1]<<','<<max_slots[2]<<','<<max_slots[3]<<"],\"create_ns\":"<<ns<<"}\n";
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
