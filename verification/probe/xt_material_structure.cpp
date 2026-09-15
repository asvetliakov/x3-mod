// Host-only XT transform driver. Original/derived shader bytes remain local.
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace x3m::renderer;
using Words=std::vector<std::uint32_t>;
unsigned checks=0;
void require(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
Words read(const std::string& file){std::ifstream f(file,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error(file);const auto bytes=f.tellg();require(bytes>0 && bytes%4==0,"original length");Words w(static_cast<std::size_t>(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(w.data()),bytes);return w;}
int main(int argc,char**argv){try{
 require(argc==3,"program directory and local output required");
 const std::array<const char*,14> pixels{{"fffdabd910793aba","e6794b6ec37ff71a","5f82ecacd39529cd","f1b0e820c7b488c3","6733b119142c8d42","496049cec2066ed3","d51cf763125cb85a","31445adb0a62d134","fd58e6b7e8cf969c","dd87737d697c6764","d22f2ce2c740e6a7","1de3d2dde345a7e3","75fb9c6b05e28ea2","edaef099780fcafe"}};
 unsigned variants=0,fill_programs=0,fill_applied_count=0;const auto begin=std::chrono::steady_clock::now();
 for(unsigned stage=0;stage<2;++stage)for(unsigned i=0;i<(stage?14u:2u);++i){
  const std::string id=stage?std::string("ps_")+pixels[i]:i?"vs_37c34a7478544c14":"vs_494fe349b8bc12ec";
  const auto original=read(std::string(argv[1])+"/"+id+".bin");
  const auto hash=material_motion_fingerprint(original.data(),original.size());
  const bool repaired=stage?linear_material_xt_default_pair(0x494fe349b8bc12ecull,hash):!i;
  auto transform=[&](const Words&w,const LinearMaterialConfig& c,Words&out,bool depth,bool linear){
   if(!stage)return repaired?linear_material_xt_default_vertex_variant(w.data(),w.size(),c,out,depth,linear):linear_material_vertex_variant(w.data(),w.size(),c,out,depth);
   return linear?linear_material_pixel_variant(w.data(),w.size(),c,out,depth):linear_material_xt_default_pixel_variant(w.data(),w.size(),c,out,depth,false);
  };
  for(bool depth:{false,true})for(bool linear:{false,true}){
   if(!linear&&!repaired)continue;
   for(unsigned gain:{0u,1u,4u,16u}){
    const LinearMaterialConfig config{float(gain),float(gain),float(gain)};Words out;
    require(transform(original,config,out,depth,linear)==LinearMaterialResult::Applied,id+" transform");
    auto alias=original;require(transform(alias,config,alias,depth,linear)==LinearMaterialResult::Applied&&alias==out,id+" alias");
    const auto path=std::string(argv[2])+"/"+id+"-"+std::to_string(depth)+"-"+std::to_string(linear)+"-"+std::to_string(gain)+".bin";
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(out.data()),out.size()*4);require(bool(f),"write local variant");++variants;
    // Constant fill (docs/architecture/fill-light.md). The XT law multiplies a
    // branch-dependent albedo, so the single MAD sits ahead of that branch.
    if(stage&&linear&&gain==1u){
     Words zero,filled;bool zero_applied=true,applied=false;
     require(linear_material_pixel_variant_fill(original.data(),original.size(),{1,1,1,0.f},zero,depth,zero_applied)==LinearMaterialResult::Applied&&!zero_applied&&zero==out,"fill zero byte-identical");
     require(linear_material_pixel_variant_fill(original.data(),original.size(),{1,1,1,0.06f},filled,depth,applied)==LinearMaterialResult::Applied,"fill admission");
     require(applied==(filled!=out),"only an applied fill changes the program");
     ++fill_programs;fill_applied_count+=applied;
     const auto fill_path=std::string(argv[2])+"/"+id+"-"+std::to_string(depth)+"-fill.dat";
     std::ofstream g(fill_path,std::ios::binary);g.write(reinterpret_cast<const char*>(filled.data()),filled.size()*4);require(bool(g),"write local fill variant");
     Words guard{91,92};const auto kept=guard;
     for(float wrong:{-0.001f,0.5001f,std::numeric_limits<float>::quiet_NaN()})
      require(linear_material_pixel_variant(original.data(),original.size(),{1,1,1,wrong},guard,depth)==LinearMaterialResult::InvalidConfig&&guard==kept,"invalid fill rollback");
    }
   }
   Words out{91,92},saved=out,broken=original;broken.back()^=1;
   require(transform(broken,{},out,depth,linear)==LinearMaterialResult::UnsupportedShader&&out==saved,id+" corruption rollback");
   require(transform(original,{std::numeric_limits<float>::quiet_NaN(),1,1},out,depth,linear)==LinearMaterialResult::InvalidConfig&&out==saved,"NaN gain rollback");
   require(transform(original,{-1,1,1},out,depth,linear)==LinearMaterialResult::InvalidConfig&&out==saved,"negative gain rollback");
   require(transform(original,{17,1,1},out,depth,linear)==LinearMaterialResult::InvalidConfig&&out==saved,"high gain rollback");
   Words plus,minus;require(transform(original,{0,0,0},plus,depth,linear)==LinearMaterialResult::Applied&&transform(original,{-0.f,-0.f,-0.f},minus,depth,linear)==LinearMaterialResult::Applied&&plus==minus,"signed zero gains");
  }
 }
 unsigned pairs=0;
 for(unsigned i=0;i<14;++i){const auto hash=std::stoull(pixels[i],nullptr,16);const bool repaired=linear_material_xt_default_pair(0x494fe349b8bc12ecull,hash);
  for(const auto vs:{0x494fe349b8bc12ecull,0x37c34a7478544c14ull,0x53a0a641107ed76cull}){
   const auto c=linear_material_pair_contract(vs,hash);const bool match=vs==(repaired?0x494fe349b8bc12ecull:0x37c34a7478544c14ull);
   require(bool(c.sampler_mask)==match,"exact XT pair mask");if(!match)continue;++pairs;
   require(c.sampler_mask==(repaired?0x1du:0x39u)&&c.bump==!repaired,"sampler and technique");
   require(c.scalar_transport_count==(repaired?0:2),"scalar count");
   if(!repaired)for(unsigned j=0;j<2;++j)require(c.scalar_transport[j].source_texcoord==6&&c.scalar_transport[j].source_component==j&&c.scalar_transport[j].destination_texcoord==1+j&&c.scalar_transport[j].destination_component==3,"scalar WRAP contract");
  }
 }
 Words guard{0,0},saved=guard;require(linear_material_pixel_variant(guard.data(),1393,{},guard)==LinearMaterialResult::UnsupportedShader&&guard==saved,"old bounded read guard");
 require(linear_material_xt_default_vertex_variant(nullptr,0,{},guard)==LinearMaterialResult::InvalidInput&&guard==saved,"null guard");
 const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
 std::cout<<"{\"programs\":16,\"pairs\":"<<pairs<<",\"variants\":"<<variants<<",\"checks\":"<<checks
          <<",\"fill_pixel_programs\":"<<fill_programs<<",\"fill_applied\":"<<fill_applied_count
          <<",\"elapsed_us_including_io\":"<<us<<"}\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
