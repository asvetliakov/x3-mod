// Internal algorithm witnesses using authored synthetic programs only. Including
// the implementation keeps this test seam out of the production public API.
#include "../../src/renderer/linear_material.cpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace x3m::renderer;
unsigned checks=0;
void require(bool ok) { ++checks; if (!ok) throw std::runtime_error("check "+std::to_string(checks)); }
void write(const std::string& path,const Words& words) {
    std::ofstream f(path,std::ios::binary); f.write(reinterpret_cast<const char*>(words.data()),words.size()*4); require(bool(f));
}
struct Fixture {
    Words original,converted; Structure source,output; std::vector<SunSite> sites; unsigned seed=0,final=0;
};
Fixture fixture(bool branch=false,unsigned mutation=0) {
    Fixture f; f.original={0xffff0300u};
    emit(f.original,dcl,{0x80000005u,dst(input,0)});
    f.seed=static_cast<unsigned>(f.original.size()+3);
    emit(f.original,mad,{dst(temp,0),lane(temp,1,3),src(constant,0),src(temp,2)});
    if (branch) emit(f.original,40,{src(14,0)});
    emit(f.original,mul,{dst(temp,0),src(temp,0,mutation==1?0:identity),src(temp,mutation==2?0:3)});
    if (branch) {emit(f.original,42,{});emit(f.original,mov,{dst(temp,0),src(temp,4)});emit(f.original,43,{});}
    f.final=static_cast<unsigned>(f.original.size());
    emit(f.original,mad,{dst(color_output,0),src(temp,0),src(temp,5),src(temp,6)});
    f.original.push_back(end_token);
    require(structure(f.original.data(),f.original.size(),false,f.source,true,default_abi,7,false,false,branch));
    f.converted={0xffff0300u}; definitions(f.converted,false,{});
    for (const auto& i:f.source.instructions) {
        const auto at=f.converted.size(); f.sites.push_back({i.at,at});
        f.converted.insert(f.converted.end(),f.original.begin()+i.at,f.original.begin()+i.at+i.count+1);
        if (f.seed>i.at && f.seed<=i.at+i.count) f.converted[at+f.seed-i.at]=src(temp,12);
        if (i.at==f.final) f.converted[at+1]=dst(temp,11);
    }
    f.converted.push_back(end_token);
    require(structure(f.converted.data(),f.converted.size(),false,f.output,false,default_abi,7,false,false,branch));
    return f;
}
int main(int argc,char** argv) {
    try {
        require(argc==2);
        for (const bool branch:{false,true}) {
            auto f=fixture(branch); bool extracted=false;
            require(sun_share_variant(f.original.data(),f.source,f.converted,f.output,f.sites,{f.seed,0},f.final,extracted));
            require(extracted); write(std::string(argv[1])+(branch?"/branch.bin":"/straight.bin"),f.converted);
        }
        for (unsigned mutation:{1u,2u}) {
            auto f=fixture(false,mutation); bool extracted=true;
            require(sun_share_variant(f.original.data(),f.source,f.converted,f.output,f.sites,{f.seed,0},f.final,extracted));
            require(!extracted); write(std::string(argv[1])+"/refused-"+std::to_string(mutation)+".bin",f.converted);
        }
        auto f=fixture(); std::vector<SunEdit> edits;
        require(!sun_plan(f.original.data(),f.source,f.converted,f.sites,{f.seed+1,0},f.final,edits));
        require(!sun_plan(f.original.data(),f.source,f.converted,f.sites,{f.seed,f.seed},f.final,edits));
        require(!sun_plan(f.original.data(),f.source,f.converted,f.sites,{0,0},f.final,edits));
        auto missing=f.sites; missing.erase(missing.begin()+1);
        require(!sun_plan(f.original.data(),f.source,f.converted,missing,{f.seed,0},f.final,edits));
        for (unsigned r=16;r<=23;++r) {
            auto c=f.converted; const auto at=c.size()-1; c.pop_back();emit(c,mov,{dst(temp,r),src(temp,0)});c.push_back(end_token);
            auto s=f.output;s.instructions.push_back({at,mov,2}); require(!sun_resources_free(c,s));
        }
        for (unsigned form=0;form<3;++form) {
            auto c=f.converted; const auto at=c.size()-1;c.pop_back();
            if (form==0) emit(c,def,{dst(constant,221,xyzw),0,0,0,0});
            if (form==1) emit(c,mov,{dst(temp,0),src(constant,221)});
            if (form==2) emit(c,dcl,{0x80000005u,dst(constant,221)});
            auto s=f.output;s.instructions.push_back({at,form==0?def:form==1?mov:dcl,form==0?5u:2u});c.push_back(end_token);
            require(!sun_resources_free(c,s));
        }
        Words reduction{0xffff0300u}; definitions(reduction,false,{});
        emit(reduction,def,{dst(constant,221,xyzw),bits(.2126f),bits(.7152f),bits(.0722f),bits(0x1p-20f)});
        sun_reduction(reduction);emit(reduction,mov,{dst(color_output,2,2),lane(temp,23,3)});reduction.push_back(end_token);
        write(std::string(argv[1])+"/reduction.bin",reduction);
        std::cout<<"{\"checks\":"<<checks<<"}\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
