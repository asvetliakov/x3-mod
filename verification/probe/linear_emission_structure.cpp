// Host-only driver; original game shader bytes remain in a local corpus.
// Include the unchanged production TU so this host-only driver can also test
// structural refusal directly, independently of the public whole-hash guard.
#include "../../src/renderer/linear_emission.cpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
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
        require(argc==3,"usage: linear_emission_structure <original directory> <local output directory>");
        const char* pixels[]={"8360f422de08b5bd","9975b706e5a1c999","ff2473e73a6bdfa1","8559522220507d5e","875e780adb131b16"};
        const std::uint64_t vertex[]={0xd5e1c75351ed3f04ull,0x32e75459998d0388ull,0x089091aab2d5eb13ull};
        const std::uint64_t pixel[]={0x8360f422de08b5bdull,0x9975b706e5a1c999ull,0xff2473e73a6bdfa1ull,0x8559522220507d5eull,0x875e780adb131b16ull};
        const float gains[]={0,0.25f,1,4,16};
        unsigned variants=0, pairs=0; long long create_ns=0;
        for (const char* name:pixels) {
            const auto original=read(std::string(argv[1])+"/ps_"+name+".bin");
            const auto saved=original;
            const auto selected=std::find_if(std::begin(profiles),std::end(profiles),
                [&](const auto& row) { return row.pixel==fingerprint(original.data(),original.size()); });
            require(selected!=std::end(profiles),"local original profile exists");
            const auto& profile=*selected;
            Shape shape;
            require(structure(original.data(),original.size(),shape) && original_shape(original.data(),profile,shape),"independent original shape proof");
            // A structurally legal native source change must fail the exact
            // site/body proof, without consulting any original fingerprint.
            auto changed_source=original; changed_source[profile.native_output+2]^=1;
            Shape changed_shape;
            require(structure(changed_source.data(),changed_source.size(),changed_shape) &&
                    !original_shape(changed_source.data(),profile,changed_shape),"native source shape refusal without hash");
            auto changed_profile=profile; ++changed_profile.copy_before;
            require(!original_shape(original.data(),changed_profile,shape),"pre-fade boundary refusal without hash");
            auto comment=original; comment[2]=0xffff;
            Shape comment_shape;
            require(structure(comment.data(),comment.size(),comment_shape) && original_shape(comment.data(),profile,comment_shape),"comment payload remains opaque to shape proof");
            auto extra_output=original; extra_output[profile.native_output+1]^=1;
            Shape output_shape;
            require(!structure(extra_output.data(),extra_output.size(),output_shape) ||
                    !original_shape(extra_output.data(),profile,output_shape),"native output identity refusal without hash");
            for (unsigned g=0;g<5;++g) {
                Words result{91,92};
                const auto begin=std::chrono::steady_clock::now();
                const auto status=linear_emission_pixel_variant(original.data(),original.size(),{gains[g]},result);
                create_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
                if (status!=LinearEmissionResult::Applied) std::cerr<<name<<" status="<<static_cast<int>(status)<<'\n';
                require(status==LinearEmissionResult::Applied,"reviewed original admission");
                require(original==saved,"immutable input");
                if (g==0) {
                    // The tail's first POW: reject a vector exponent even
                    // though register/framing limits and input hash are moot.
                    auto wrong_power=result;
                    const auto first_power=original.size()-1+15+12;
                    wrong_power[first_power+3]=src(constant,30);
                    Shape wrong_shape;
                    require(!structure(wrong_power.data(),wrong_power.size(),wrong_shape),"POW scalar form refusal");
                }
                auto alias=original;
                require(linear_emission_pixel_variant(alias.data(),alias.size(),{gains[g]},alias)==LinearEmissionResult::Applied && alias==result,"successful alias");
                write(std::string(argv[2])+"/ps_"+name+"-"+std::to_string(g)+".bin",result);
                ++variants;
            }
            for (float invalid:{-1.0f,16.001f,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                Words result{91,92}; const auto before=result;
                require(linear_emission_pixel_variant(original.data(),original.size(),{invalid},result)==LinearEmissionResult::InvalidConfig && result==before,"invalid gain rollback");
                auto alias=original;
                require(linear_emission_pixel_variant(alias.data(),alias.size(),{invalid},alias)==LinearEmissionResult::InvalidConfig && alias==saved,"invalid gain alias rollback");
            }
            for (std::size_t offset:{std::size_t(0),std::size_t(2),original.size()-5,original.size()-1}) {
                auto broken=original; broken[offset]^=1; auto before=broken;
                require(linear_emission_pixel_variant(broken.data(),broken.size(),{},broken)==LinearEmissionResult::UnsupportedShader && broken==before,"corrupted original alias rollback");
            }
            Words result{91,92}; const auto before=result;
            require(linear_emission_pixel_variant(nullptr,original.size(),{},result)==LinearEmissionResult::InvalidInput && result==before,"null rollback");
            require(linear_emission_pixel_variant(original.data(),1,{},result)==LinearEmissionResult::InvalidInput && result==before,"short rollback");
            require(linear_emission_pixel_variant(original.data(),original.size()-1,{},result)==LinearEmissionResult::UnsupportedShader && result==before,"truncation rollback");
            require(linear_emission_pixel_variant(original.data(),1109,{},result)==LinearEmissionResult::UnsupportedShader && result==before,"bounded read before hash");
            Words positive,negative;
            require(linear_emission_pixel_variant(original.data(),original.size(),{0},positive)==LinearEmissionResult::Applied &&
                    linear_emission_pixel_variant(original.data(),original.size(),{-0.0f},negative)==LinearEmissionResult::Applied && positive==negative,"signed-zero gain canonicalization");
        }
        for (unsigned v=0;v<3;++v) for (unsigned p=0;p<5;++p) {
            const bool expected=v==(p==0?0u:p<3?1u:2u);
            require(linear_emission_pair_reviewed(vertex[v],pixel[p])==expected,"exact five-pair matrix");
            if (expected) ++pairs;
        }
        for (auto instance:{0x89193868c61c3846ull,0x5b7a3ccd9e7df00aull,0xa520be365951c9dcull})
            for (auto p:pixel) require(!linear_emission_pair_reviewed(instance,p),"INSTANCE sharing does not admit pair");
        require(!linear_emission_pair_reviewed(0,pixel[0]) && !linear_emission_pair_reviewed(vertex[0],0),"unknown pair");
        Words authored{0xffff0200u,0xffffu}, result{91,92};
        require(linear_emission_pixel_variant(authored.data(),authored.size(),{},result)==LinearEmissionResult::UnsupportedShader && result==Words({91,92}),"unreviewed valid framing");
        std::cout<<"{\"programs\":5,\"pairs\":"<<pairs<<",\"variants\":"<<variants<<",\"checks\":"<<checks
                 <<",\"initial_creates_ns\":"<<create_ns<<"}\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
