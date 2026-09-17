// Host-only driver of the hull-program emitter gain
// (linear_emission_hull_source_gain_variant, phase 3 of the emitter plan).
// Original game shader bytes stay in the local corpus; the driver writes the
// variants for the Python oracle.
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
        require(argc==3,"usage: linear_emission_hull_gain_structure <original directory> <local output directory>");
        const char* names[]={"5f82ecacd39529cd","6733b119142c8d42","fffdabd910793aba","496049cec2066ed3",
                             "e6794b6ec37ff71a","f1b0e820c7b488c3","0c1f3f0f440e4a0c","7c83ed50c9894e44",
                             "99153c144030c396","64bac8bb307eb896","c1452981fd0bff64","e70adc744a38ca59"};
        const float gains[]={1,2,3.5f,8};
        unsigned variants=0, identical=0, modulated=0, reviewed=0; long long create_ns=0;
        for (const char* name:names) {
            const auto original=read(std::string(argv[1])+"/ps_"+name+".bin");
            const auto saved=original;
            const auto hash=fingerprint(original.data(),original.size());
            require(linear_emission_hull_program_reviewed(hash),"local original is a reviewed hull program");
            ++reviewed;
            const auto& profile=hull_profiles[linear_emission_hull_program_index(hash)];
            require(profile.words==original.size(),"profile word count");
            if (profile.modulated) ++modulated;
            for (unsigned g=0;g<4;++g) {
                Words result{91,92};
                const auto begin=std::chrono::steady_clock::now();
                const auto status=linear_emission_hull_source_gain_variant(original.data(),original.size(),gains[g],result);
                create_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
                if (status!=LinearEmissionResult::Applied) std::cerr<<name<<" status="<<static_cast<int>(status)<<'\n';
                require(status==LinearEmissionResult::Applied,"reviewed original admission");
                require(original==saved,"immutable input");
                if (g==0) { require(result==original,"gain 1 byte identity"); ++identical; }
                else {
                    require(result.size()==original.size()+10,"one DEF and one MUL added");
                    // The DEF sits at the first declaration, the MUL immediately
                    // before the final colour instruction; everything else is verbatim.
                    require(std::equal(original.begin(),original.begin()+profile.definition,result.begin()),"header verbatim");
                    require(result[profile.definition]==((5u<<24)|def) &&
                            result[profile.definition+1]==(dst(constant,hull_gain_constant,15)) &&
                            result[profile.definition+2]==bits(gains[g]) && result[profile.definition+3]==0 &&
                            result[profile.definition+4]==0 && result[profile.definition+5]==0,"gain DEF");
                    require(std::equal(original.begin()+profile.definition,original.begin()+profile.emission,
                                       result.begin()+profile.definition+6),"body verbatim");
                    require(result[profile.emission+6]==((3u<<24)|mul) &&
                            result[profile.emission+7]==dst(temporary,hull_emission_temporary,7) &&
                            result[profile.emission+8]==src(temporary,hull_emission_temporary) &&
                            result[profile.emission+9]==lane(constant,hull_gain_constant,0),"colour-lane MUL at the emission term");
                    require(std::equal(original.begin()+profile.emission,original.end(),
                                       result.begin()+profile.emission+10),"tail verbatim");
                }
                auto alias=original;
                require(linear_emission_hull_source_gain_variant(alias.data(),alias.size(),gains[g],alias)==LinearEmissionResult::Applied && alias==result,"successful alias");
                write(std::string(argv[2])+"/ps_"+name+"-hull-"+std::to_string(g)+".bin",result);
                ++variants;
            }
            for (float invalid:{0.f,0.999f,8.001f,-1.0f,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                Words result{91,92}; const auto before=result;
                require(linear_emission_hull_source_gain_variant(original.data(),original.size(),invalid,result)==LinearEmissionResult::InvalidConfig && result==before,"invalid gain rollback");
                auto alias=original;
                require(linear_emission_hull_source_gain_variant(alias.data(),alias.size(),invalid,alias)==LinearEmissionResult::InvalidConfig && alias==saved,"invalid gain alias rollback");
            }
            for (std::size_t offset:{std::size_t(0),std::size_t(2),std::size_t(profile.definition),std::size_t(profile.emission)+1,original.size()-2}) {
                auto broken=original; broken[offset]^=1; auto before=broken;
                require(linear_emission_hull_source_gain_variant(broken.data(),broken.size(),2,broken)==LinearEmissionResult::UnsupportedShader && broken==before,"corrupted original alias rollback");
            }
            Words result{91,92}; const auto before=result;
            require(linear_emission_hull_source_gain_variant(nullptr,original.size(),2,result)==LinearEmissionResult::InvalidInput && result==before,"null rollback");
            require(linear_emission_hull_source_gain_variant(original.data(),1,2,result)==LinearEmissionResult::InvalidInput && result==before,"short rollback");
            require(linear_emission_hull_source_gain_variant(original.data(),original.size()-1,2,result)==LinearEmissionResult::UnsupportedShader && result==before,"truncation rollback");
            // The effects gain and the hull gain never admit each other's programs.
            require(linear_emission_source_gain_variant(original.data(),original.size(),2,result)==LinearEmissionResult::UnsupportedShader && result==before,"effects gain refuses a hull program");
            require(!linear_emission_pair_reviewed(hash,hash),"hull program is not an effects pair");
        }
        for (const auto& row:profiles)
            require(!linear_emission_hull_program_reviewed(row.pixel),"hull gain refuses the effects programs");
        Words authored{0xffff0300u,0xffffu}, result{91,92};
        require(linear_emission_hull_source_gain_variant(authored.data(),authored.size(),2,result)==LinearEmissionResult::UnsupportedShader && result==Words({91,92}),"unreviewed valid framing");
        // Blend law: only ADD ONE/ONE with blending on and sRGB write off.
        unsigned admitted=0, refused=0;
        for (std::uint32_t enable:{0u,1u}) for (std::uint32_t srgb:{0u,1u})
            for (std::uint32_t source:{1u,2u,4u,5u}) for (std::uint32_t destination:{1u,2u,4u,5u}) for (std::uint32_t op:{1u,2u}) {
                const auto verdict=linear_emission_hull_source_gain_blend(enable,srgb,source,destination,op);
                require(verdict!=SourceGainBlend::Screen,"no screen substitution in the hull population");
                if (enable && !srgb && source==2 && destination==2 && op==1) { require(verdict==SourceGainBlend::Admit,"ONE/ONE ADD admits"); ++admitted; }
                else { require(verdict==SourceGainBlend::Blend,"every other colour blend refuses"); ++refused; }
            }
        std::cout<<"{\"programs\":"<<reviewed<<",\"modulated\":"<<modulated<<",\"variants\":"<<variants
                 <<",\"identical\":"<<identical<<",\"checks\":"<<checks<<",\"creates_ns\":"<<create_ns
                 <<",\"blend_admitted\":"<<admitted<<",\"blend_refused\":"<<refused<<"}\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
