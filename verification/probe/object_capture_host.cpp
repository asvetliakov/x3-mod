#include "object_capture.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <cstdlib>
namespace oc=x3m::object_capture;
unsigned checks=0;
void check(bool v){++checks;if(!v){std::fprintf(stderr,"FAIL check=%u\n",checks);std::exit(1);}}
struct Memory {
    std::map<std::uint32_t,unsigned char> bytes;unsigned reads=0;
    std::vector<std::uint32_t> requested;
    void word(std::uint32_t at,std::uint32_t n){for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<unsigned char>(n>>(8*i));}
    void block(std::uint32_t at,unsigned n){for(unsigned i=0;i<n;++i)bytes[at+i]=0;}
    bool operator()(std::uintptr_t at,void* out,std::size_t n){++reads;requested.push_back(std::uint32_t(at));
        for(std::size_t i=0;i<n;++i)if(!bytes.count(std::uint32_t(at+i)))return false;
        for(std::size_t i=0;i<n;++i)static_cast<unsigned char*>(out)[i]=bytes[std::uint32_t(at+i)];return true;}
};
Memory setup() {
    Memory m;m.word(0x100,0x1000);m.word(0x1000,0x2000);m.word(0x1010,7);
    m.word(0x2000,0x3000);m.word(0x2004,8);m.word(0x301c,0x4000);
    m.word(0x4000,0);m.word(0x4004,7);m.word(0x4008,0x5000);
    m.word(0x5058,0x6000);m.word(0x51e0,0x7000);
    m.word(0x7008,9);m.word(0x7070,0x8000);m.word(0x8028,11);
    return m;
}
int main(){
    auto m=setup();auto t=oc::target(m,0x100);
    check(t.status==oc::Status::Ready&&t.cockpit==0x5000&&t.root==0x8000&&t.root_handle==11&&t.target_id==9&&t.camera==0x6000);
    auto broken=m;broken.bytes.erase(0x8028);check(oc::target(broken,0x100).status==oc::Status::ReadFailure);
    broken=m;broken.word(0x51e0,0);check(oc::target(broken,0x100).status==oc::Status::NoTarget);
    broken=m;broken.word(0x2004,3);check(oc::target(broken,0x100).status==oc::Status::Malformed);
    broken=m;broken.word(0x4004,8);check(oc::target(broken,0x100).status==oc::Status::Missing);
    broken.word(0x4000,0x4000);check(oc::target(broken,0x100).status==oc::Status::Cycle);
    broken=m;broken.word(0x4008,0xfffffffc);check(oc::target(broken,0x100).status==oc::Status::ReadFailure);
    broken=m;
    for(unsigned i=0;i<33;++i){const auto p=0x4000+i*16;broken.word(p,p+16);broken.word(p+4,8);broken.word(p+8,0);}
    check(oc::target(broken,0x100).status==oc::Status::Limit);
    std::uint32_t out=0;const auto reads=m.reads;
    check(!oc::field(m,0xfffffffcu,8,out)&&m.reads==reads);
    check(!oc::field(m,0x101,0,out)&&m.reads==reads);
    check(!oc::field(m,0,0,out)&&m.reads==reads);
    auto a=oc::ancestry(m,0x8000,11,0,t);check(a.status==oc::Status::Match&&a.count==1&&m.reads==reads);
    m.block(0x8000,44);m.word(0x8028,11);
    a=oc::ancestry(m,0x9000,12,0x8000,t);check(a.status==oc::Status::Match&&a.count==2&&a.links[1].handle==11);
    auto stale=t;stale.root_handle=99;
    a=oc::ancestry(m,0x9000,12,0x8000,stale);check(a.status==oc::Status::End);
    a=oc::ancestry(m,0x9000,12,0x9000,t);check(a.status==oc::Status::Cycle&&a.count==1);
    a=oc::ancestry(m,0x9000,12,0xa000,t);check(a.status==oc::Status::ReadFailure&&a.count==1);
    a=oc::ancestry(m,0x9000,12,0xffffffff,t);check(a.status==oc::Status::ReadFailure&&a.count==1);
    for(unsigned i=0;i<17;++i){const auto p=0xa000+i*64;m.block(p,44);m.word(p+0x18,p+64);m.word(p+0x28,20+i);}
    a=oc::ancestry(m,0xa000,20,0xa040,t);check(a.status==oc::Status::Limit&&a.count==16);
    m.word(0xa000+15*64+0x18,0);a=oc::ancestry(m,0xa000,20,0xa040,t);check(a.status==oc::Status::End&&a.count==16);
    t.status=oc::Status::NoTarget;a=oc::ancestry(m,0x8000,11,0,t);check(a.status==oc::Status::End);
    oc::Cache cache;
    check(cache.begin(3,4,m,0x100));const auto r=m.reads;
    check(!cache.begin(3,4,m,0x100)&&m.reads==r);
    bool fresh=false;check(cache.node(1,2,3,fresh)==1&&fresh);
    check(cache.node(1,2,3,fresh)==1&&!fresh);
    check(cache.node(1,9,3,fresh)==2&&fresh); // same address, different lifetime witness
    check(cache.node(1,9,4,fresh)==3&&fresh); // parent changed within frame
    for(unsigned i=3;i<oc::Cache::node_capacity;++i)check(cache.node(4*i,7,8,fresh)==i+1&&fresh);
    check(cache.node(0x10000,1,2,fresh)==0&&!fresh&&m.reads==r);
    check(cache.node(1,2,3,fresh)==1&&!fresh); // full cache still finds known key
    for(unsigned i=0;i<oc::Cache::camera_capacity;++i)check(cache.camera(4*i,7,fresh)==i+1&&fresh);
    check(cache.camera(0x1000,7,fresh)==0&&!fresh);
    check(cache.begin(4,4,m,0x100)&&cache.node_count==0&&cache.camera_count==0&&m.reads>r);
    check(cache.node(1,2,3,fresh)==1&&fresh);
    m.word(0x51e0,0);check(cache.begin(4,5,m,0x100)&&cache.selected.status==oc::Status::NoTarget);
    cache.invalidate();check(!cache.valid&&cache.node_count==0&&cache.selected.status==oc::Status::ReadFailure);
    const auto old_epoch=cache.epoch;check(cache.begin(4,5,m,0x100)&&cache.epoch==old_epoch+1); // refused Reset invalidates even unchanged production generation
    m.block(0x6000,0x374);m.word(0x601c,0x9000);m.word(0x6270,0x10000);m.word(0x6030,0x80000000);
    m.word(0x636c,0x7fffffff);m.word(0x6370,0xffffffff);m.word(0x902c,0x7fc01234);
    m.word(0x200,0xb000);m.word(0xb768,3);
    auto f=oc::fade(m,0x6000,0x200);check(f.valid==7&&f.flags==0x10000&&f.position[0]==0x80000000&&f.near_bits==0x7fffffff&&f.far_bits==0xffffffff&&f.scale_bits==0x7fc01234&&f.config==3);
    m.bytes.erase(0x902c);f=oc::fade(m,0x6000,0x200);check(f.valid==5&&f.scale_bits==0);
    m.bytes.erase(0x6000);f=oc::fade(m,0x6000,0x200);check(f.valid==4&&f.context==0&&f.flags==0);
    m.bytes.erase(0x200);f=oc::fade(m,0x6000,0x200);check(f.valid==0);
    std::printf("object_capture_host checks=%u failures=0 cache_bytes=%zu\n",checks,sizeof(oc::Cache));
}
