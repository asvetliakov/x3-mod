// Host-only corpus/atomicity driver. Raw bytecode stays in the local directory.
#include "../../src/renderer/linear_material.h"
#include <filesystem>
#include <cstdlib>
#include <new>
#include "../../src/renderer/linear_distance_fade.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
static long allocation_budget=-1;
void* operator new(std::size_t size) {
    if (allocation_budget==0) throw std::bad_alloc();
    if (allocation_budget>0) --allocation_budget;
    if (auto* p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
using namespace x3m::renderer;
using Words=std::vector<std::uint32_t>;
Words read(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if (!f || f.tellg()<=0 || f.tellg()%4) throw std::runtime_error("input");
    Words w(static_cast<std::size_t>(f.tellg())/4); f.seekg(0);
    f.read(reinterpret_cast<char*>(w.data()),w.size()*4);return w;
}
LinearMaterialResult apply(bool vs,const Words& in,const LinearMaterialConfig& c,Words& out,bool depth,bool* extraction=nullptr) {
    if (extraction) return linear_material_pixel_variant_sun_share(in.data(),in.size(),c,out,depth,*extraction);
    auto r=vs?linear_material_vertex_variant(in.data(),in.size(),c,out,depth):linear_material_pixel_variant(in.data(),in.size(),c,out,depth);
    if (r==LinearMaterialResult::UnsupportedShader)
        r=vs?linear_material_xt_default_vertex_variant(in.data(),in.size(),c,out,depth,true):linear_material_xt_default_pixel_variant(in.data(),in.size(),c,out,depth,true);
    return r;
}
int main(int argc,char** argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--faults") {
            const auto original=read(argv[2]); const bool vs=original[0]==0xfffe0300u;
            LinearMaterialConfig c;c.selective_exposure=true;c.fill=.03f;
            for (long budget=0;budget<2000;++budget) {
                Words out{0x12345678};bool extracted=true;
                allocation_budget=budget;
                const auto r=apply(vs,original,c,out,true,vs?nullptr:&extracted);
                allocation_budget=-1;
                if (r==LinearMaterialResult::Applied) {
                    if (!vs && extracted) throw std::runtime_error("extraction incorrectly qualified");
                    std::cout<<"{\"allocation_failures\":"<<budget<<"}\n";return 0;
                }
                if (out!=Words{0x12345678} || (!vs && extracted)) throw std::runtime_error("allocation publication");
            }
            throw std::runtime_error("fault bound");
        }
        if (argc==4 && std::string(argv[1])=="--fade") {
            const auto original=read(argv[2]);const bool vs=original[0]==0xfffe0300u;
            LinearMaterialConfig c;c.selective_exposure=true;c.fill=.03f;Words out;
            const auto r=vs?linear_distance_fade_vertex_variant(original.data(),original.size(),c,out):linear_distance_fade_pixel_variant(original.data(),original.size(),c,out);
            if (r!=LinearMaterialResult::Applied) throw std::runtime_error("fade result");
            std::ofstream f(argv[3],std::ios::binary);f.write(reinterpret_cast<const char*>(out.data()),out.size()*4);return !f;
        }
        if (argc!=4) throw std::runtime_error("usage: corpus output selective");
        std::filesystem::create_directories(argv[2]); unsigned programs=0, variants=0, guards=0;
        for (const auto& file:std::filesystem::directory_iterator(argv[1])) {
            if (file.path().extension()!=".bin") continue;
            const auto name=file.path().stem().string(); const bool vs=name.rfind("vs_",0)==0;
            if (!vs && name.rfind("ps_",0)!=0) continue;
            const auto original=read(file.path()); Words out;
            LinearMaterialConfig c;
            if (apply(vs,original,c,out,true)!=LinearMaterialResult::Applied) continue;
            ++programs; c.selective_exposure=std::stoi(argv[3])!=0;
            for (int depth:{0,1}) for (int gain:{0,1,4,16}) for (int fill:{0,1}) {
                c.direct_gain=c.material_emissive_gain=c.lightmap_emissive_gain=float(gain); c.fill=fill?.03f:0.f;
                auto r=apply(vs,original,c,out,depth!=0);
                if (r!=LinearMaterialResult::Applied) throw std::runtime_error(name+" result "+std::to_string(int(r)));
                const auto dest=std::filesystem::path(argv[2])/(name+"-"+std::to_string(depth)+"-"+std::to_string(gain)+"-"+std::to_string(fill)+".bin");
                std::ofstream f(dest,std::ios::binary);f.write(reinterpret_cast<const char*>(out.data()),out.size()*4);if (!f) throw std::runtime_error("write");
                auto alias=original;
                if (apply(vs,alias,c,alias,depth!=0)!=r || alias!=out) throw std::runtime_error("alias");
                if (!vs && c.selective_exposure) {
                    Words extracted; bool proved=true;
                    if (apply(false,original,c,extracted,depth!=0,&proved)!=r || proved || extracted!=out) throw std::runtime_error("extraction atomicity");
                }
                ++variants;
            }
            for (unsigned mutation:{0u,1u,2u}) {
                auto broken=original;
                if (!mutation) broken.back()^=1; else if (mutation==1) broken.pop_back(); else broken[0]^=1;
                const auto saved=out;
                if (apply(vs,broken,c,out,true)==LinearMaterialResult::Applied || out!=saved) throw std::runtime_error("mutation rollback");
                ++guards;
            }
        }
        std::cout<<"{\"programs\":"<<programs<<",\"variants\":"<<variants<<",\"guards\":"<<guards<<"}\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
