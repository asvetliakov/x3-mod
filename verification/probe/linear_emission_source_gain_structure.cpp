// Host-only driver of the source-only encoded gain transformer
// (linear_emission_source_gain_variant). Original game shader bytes stay in
// the local corpus; the driver writes the variants for the Python oracle.
#include "../../src/renderer/linear_emission.cpp"
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
void require(bool value,const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
Words read(const std::string& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    require(bool(stream),"open local original"); const auto bytes=stream.tellg();
    require(bytes>0 && bytes%4==0,"original word alignment");
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
        require(argc==3,"usage: linear_emission_source_gain_structure <original directory> <local output directory>");
        const char* pixels[]={"8360f422de08b5bd","9975b706e5a1c999","ff2473e73a6bdfa1","8559522220507d5e","875e780adb131b16","39f3b4d5b6a5aaed","47e15e20d63b0e93","846c5c1a549f9491","c6dacb8f74b65c97","f0c91793a75e1203"};
        const std::uint64_t vertex[]={0xd5e1c75351ed3f04ull,0x32e75459998d0388ull,0x089091aab2d5eb13ull,0x5b7a3ccd9e7df00aull,0x6435a84d8ac5908eull,0x89193868c61c3846ull,0xa520be365951c9dcull,0xcfb2c31707d545bcull};
        const std::uint64_t pixel[]={0x8360f422de08b5bdull,0x9975b706e5a1c999ull,0xff2473e73a6bdfa1ull,0x8559522220507d5eull,0x875e780adb131b16ull,0x39f3b4d5b6a5aaedull,0x47e15e20d63b0e93ull,0x846c5c1a549f9491ull,0xc6dacb8f74b65c97ull,0xf0c91793a75e1203ull};
        const float gains[]={1,2,3.5f,8};
        unsigned variants=0, pairs=0, identical=0, max_arithmetic=0; long long create_ns=0;
        for (const char* name:pixels) {
            const auto original=read(std::string(argv[1])+"/ps_"+name+".bin");
            const auto saved=original;
            const auto selected=std::find_if(std::begin(profiles),std::end(profiles),
                [&](const auto& row) { return row.pixel==fingerprint(original.data(),original.size()); });
            require(selected!=std::end(profiles),"local original profile exists");
            const auto& profile=*selected;
            for (unsigned g=0;g<4;++g) {
                Words result{91,92};
                const auto begin=std::chrono::steady_clock::now();
                const auto status=linear_emission_source_gain_variant(original.data(),original.size(),gains[g],result);
                create_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
                if (status!=LinearEmissionResult::Applied) std::cerr<<name<<" status="<<static_cast<int>(status)<<'\n';
                require(status==LinearEmissionResult::Applied,"reviewed original admission");
                require(original==saved,"immutable input");
                if (g==0) { require(result==original,"gain 1 byte identity"); ++identical; }
                else {
                    require(result.size()==original.size()+10,"one DEF and one MUL added");
                    Shape result_shape;
                    require(structure(result.data(),result.size(),result_shape,profile.model) && result_shape.outputs==1 && result_shape.texture==1,"single native output retained");
                    max_arithmetic=std::max(max_arithmetic,result_shape.arithmetic);
                    // The added MUL must not touch alpha: a full-mask mutation of it is refused by the oracle, not the structure.
                    require(result[profile.native_output+6]==((3u<<24)|mul) && result[profile.native_output+7]==dst(temporary,0,7),"colour-lane MUL before the native output");
                }
                auto alias=original;
                require(linear_emission_source_gain_variant(alias.data(),alias.size(),gains[g],alias)==LinearEmissionResult::Applied && alias==result,"successful alias");
                write(std::string(argv[2])+"/ps_"+name+"-source-"+std::to_string(g)+".bin",result);
                ++variants;
            }
            for (float invalid:{0.f,0.999f,8.001f,-1.0f,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                Words result{91,92}; const auto before=result;
                require(linear_emission_source_gain_variant(original.data(),original.size(),invalid,result)==LinearEmissionResult::InvalidConfig && result==before,"invalid gain rollback");
                auto alias=original;
                require(linear_emission_source_gain_variant(alias.data(),alias.size(),invalid,alias)==LinearEmissionResult::InvalidConfig && alias==saved,"invalid gain alias rollback");
            }
            for (std::size_t offset:{std::size_t(0),std::size_t(2),original.size()-5,original.size()-1}) {
                auto broken=original; broken[offset]^=1; auto before=broken;
                require(linear_emission_source_gain_variant(broken.data(),broken.size(),2,broken)==LinearEmissionResult::UnsupportedShader && broken==before,"corrupted original alias rollback");
            }
            Words result{91,92}; const auto before=result;
            require(linear_emission_source_gain_variant(nullptr,original.size(),2,result)==LinearEmissionResult::InvalidInput && result==before,"null rollback");
            require(linear_emission_source_gain_variant(original.data(),1,2,result)==LinearEmissionResult::InvalidInput && result==before,"short rollback");
            require(linear_emission_source_gain_variant(original.data(),original.size()-1,2,result)==LinearEmissionResult::UnsupportedShader && result==before,"truncation rollback");
            require(linear_emission_source_gain_variant(original.data(),1109,2,result)==LinearEmissionResult::UnsupportedShader && result==before,"bounded read before hash");
        }
        const unsigned supported[]={0x201,0x006,0x018,0x006,0x1e0,0x201,0x018,0x1e0};
        for (unsigned v=0;v<8;++v) for (unsigned p=0;p<10;++p) {
            const bool expected=(supported[v]&(1u<<p))!=0;
            require(linear_emission_pair_reviewed(vertex[v],pixel[p])==expected,"exact twenty-pair matrix");
            if (expected) ++pairs;
        }
        Words authored{0xffff0200u,0xffffu}, result{91,92};
        require(linear_emission_source_gain_variant(authored.data(),authored.size(),2,result)==LinearEmissionResult::UnsupportedShader && result==Words({91,92}),"unreviewed valid framing");
        std::cout<<"{\"programs\":10,\"pairs\":"<<pairs<<",\"variants\":"<<variants<<",\"identical\":"<<identical<<",\"checks\":"<<checks
                 <<",\"creates_ns\":"<<create_ns<<",\"max_arithmetic\":"<<max_arithmetic<<"}\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
