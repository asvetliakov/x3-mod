// Compile the exact capture object_context body against original synthetic reads.
#include "object_capture.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
using DWORD=unsigned long;
DWORD error=0;DWORD GetLastError(){return error;}void SetLastError(DWORD v){error=v;}
namespace object_capture=x3m::object_capture;
namespace object_trace {
enum Valid : uint32_t { Node=1,Camera=2,Registry=4,World=8,WorldBasis=16,View=32,Projection=64 };
struct Snapshot {
    uint32_t valid=0,scope_depth=0;uint64_t session=0;
    uintptr_t mesh=0,node=0,camera=0,registry=0,engine=0;
    uint32_t node_handle=0,camera_handle=0,model=0,lod=0,flags12c=0,flags130=0;
    uint32_t parent=0,alpha13c=0,position[3]{},basis[9]{},scale[4]{};
    uint32_t world[16]{},world_basis[16]{},view[16]{},projection[16]{};
};
bool on=true,scoped=true;Snapshot sample;
bool active(){++error;return on;}
bool current(Snapshot* out){++error;*out=sample;return scoped;}
}
std::map<uintptr_t,unsigned char> memory;unsigned reads=0;
void word(uintptr_t p,uint32_t n){for(unsigned i=0;i<4;++i)memory[p+i]=static_cast<unsigned char>(n>>(8*i));}
void block(uintptr_t p,unsigned n){for(unsigned i=0;i<n;++i)memory[p+i]=0;}
namespace engine_memory {
bool read(uintptr_t p,void* out,std::size_t n){++reads;++error;
    for(std::size_t i=0;i<n;++i)if(!memory.count(p+i))return false;
    for(std::size_t i=0;i<n;++i)static_cast<unsigned char*>(out)[i]=memory[p+i];return true;}
}
std::vector<std::string> logs;
void log(const char* fmt,...){++error;char text[4096];va_list args;va_start(args,fmt);std::vsnprintf(text,sizeof text,fmt,args);va_end(args);logs.emplace_back(text);}
struct Device{uint64_t id=1,frame=0,reset_generation=0,draws=1;object_capture::Cache object_evidence;};
#include "object_capture_context_under_test_inc.h"
unsigned checks=0;
void check(bool v){++checks;if(!v){std::fprintf(stderr,"FAIL check=%u\n",checks);std::exit(1);}}
unsigned count(const char* p){unsigned n=0;for(const auto& l:logs)n+=l.rfind(p,0)==0;return n;}
int main(){
    Device d;error=123;object_trace::on=false;object_context(d);check(error==123&&reads==0&&logs.empty());
    object_trace::on=true;object_trace::scoped=false;object_context(d);check(error==123&&reads==0&&count("object_context ")==1&&count("object_target ")==0);
    object_trace::scoped=true;object_trace::sample.valid=3;object_trace::sample.node=0x9000;object_trace::sample.node_handle=12;
    object_trace::sample.parent=0x8000;object_trace::sample.alpha13c=128;object_trace::sample.camera=0x6000;object_trace::sample.camera_handle=15;
    word(0x608504,0x1000);word(0x1000,0x2000);word(0x1010,7);word(0x2000,0x3000);word(0x2004,8);word(0x301c,0x4000);
    word(0x4000,0);word(0x4004,7);word(0x4008,0x5000);word(0x5058,0x6000);word(0x51e0,0x7000);
    word(0x7008,9);word(0x7070,0x8000);block(0x8000,44);word(0x8028,11);
    block(0x6000,0x374);word(0x601c,0xa000);word(0xa02c,0x3f800000);word(0x606f34,0xb000);word(0xb768,3);
    object_context(d);check(error==123&&count("object_target ")==1&&count("object_ancestry ")==1&&count("object_ancestor ")==2&&count("object_fade ")==1);
    bool match=false,alpha=false;for(const auto& l:logs){match|=l.find("status=match count=2")!=std::string::npos;alpha|=l.find("alpha13c=00000080")!=std::string::npos;}check(match&&alpha);
    const auto before=reads;object_context(d);check(error==123&&reads==before&&count("object_target ")==1&&count("object_ancestry ")==1&&count("object_evidence ")==2);
    ++d.frame;word(0x51e0,0);object_context(d);check(error==123&&reads>before&&count("object_target ")==2&&d.object_evidence.selected.status==object_capture::Status::NoTarget);
    const auto epoch=d.object_evidence.epoch;d.object_evidence.invalidate();object_context(d);check(error==123&&d.object_evidence.epoch==epoch+1&&count("object_target ")==3);
    ++d.frame;memory.erase(0x8000);object_context(d);check(error==123);bool failed=false;for(const auto& l:logs)failed|=l.find("status=read_failure count=1")!=std::string::npos;check(failed);
    std::printf("object_capture_context_host checks=%u failures=0\n",checks);
}
