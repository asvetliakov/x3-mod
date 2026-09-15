// Host-only driver. Original and generated shader bytes remain local inputs.
#include "../../src/renderer/linear_material.h"
#include <cstdlib>
#include <new>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
// Fault every create-time allocation in turn, only in the dedicated host mode.
static long allocation_budget=-1;
void* operator new(std::size_t bytes) {
    if (allocation_budget==0) throw std::bad_alloc();
    if (allocation_budget>0) --allocation_budget;
    if (auto* p=std::malloc(bytes?bytes:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
using namespace x3m::renderer;
using Words=std::vector<std::uint32_t>;
Words read(const std::string& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    if (!f || f.tellg()<=0 || f.tellg()%4) throw std::runtime_error("input");
    Words w(static_cast<std::size_t>(f.tellg())/4); f.seekg(0);
    f.read(reinterpret_cast<char*>(w.data()),w.size()*4); if (!f) throw std::runtime_error("read"); return w;
}
void write(const std::string& p,const Words& w) {
    std::ofstream f(p,std::ios::binary); f.write(reinterpret_cast<const char*>(w.data()),w.size()*4);
    if (!f) throw std::runtime_error("write");
}
int main(int argc,char** argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--faults") {
            const auto original=read(argv[2]); unsigned failures=0;
            for (long budget=0;budget<2000;++budget) {
                Words output{0x12345678}; bool extracted=true;
                allocation_budget=budget;
                const auto r=linear_material_pixel_variant_sun_share(original.data(),original.size(),{},output,true,extracted);
                allocation_budget=-1;
                if (r==LinearMaterialResult::Applied) {
                    if (!extracted) throw std::runtime_error("fault success flag");
                    std::cout<<"{\"allocation_failures\":"<<failures<<"}\n";return 0;
                }
                if (output!=Words{0x12345678} || extracted) throw std::runtime_error("fault rollback");
                ++failures;
            }
            throw std::runtime_error("fault bound");
        }
        if (argc!=5) throw std::runtime_error("usage: original output-prefix depth fill");
        auto original=read(argv[1]); Words base,share;
        LinearMaterialConfig c; c.fill=std::stof(argv[4]); bool extracted=false;
        const auto a=linear_material_pixel_variant(original.data(),original.size(),c,base,std::stoi(argv[3])!=0);
        const auto b=linear_material_pixel_variant_sun_share(original.data(),original.size(),c,share,std::stoi(argv[3])!=0,extracted);
        if (a!=LinearMaterialResult::Applied || b!=LinearMaterialResult::Applied) throw std::runtime_error("transform "+std::to_string(int(a))+" "+std::to_string(int(b)));
        write(std::string(argv[2])+"-base.bin",base); write(std::string(argv[2])+"-share.bin",share);
        auto aliased=original; bool alias_extracted=false;
        if (linear_material_pixel_variant_sun_share(aliased.data(),aliased.size(),c,aliased,std::stoi(argv[3])!=0,alias_extracted)!=b ||
            aliased!=share || alias_extracted!=extracted) throw std::runtime_error("alias");
        const auto retained=share; original[original.size()-1]^=1; extracted=true;
        if (linear_material_pixel_variant_sun_share(original.data(),original.size(),c,share,true,extracted)==LinearMaterialResult::Applied ||
            share!=retained || extracted) throw std::runtime_error("mutation rollback");
        std::cout << "{\"extracted\":" << (alias_extracted?"true":"false") << ",\"words\":" << retained.size() << "}\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
