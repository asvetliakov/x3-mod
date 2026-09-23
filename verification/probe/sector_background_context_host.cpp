// Exact production wrapper body, supplied by the Python driver.
#include "sector_background_memory.h"
#include <cstdarg>
#include <string>
using DWORD=unsigned long;
DWORD error=0;unsigned api_calls=0,gate_calls=0,epochs=0;
std::uint64_t clock_ms=0;
DWORD GetLastError(){++api_calls;return error;}
void SetLastError(DWORD v){++api_calls;error=v;}
std::uint64_t GetTickCount64(){++api_calls;++error;return clock_ms;}
namespace sector_background=x3m::sector_background;
namespace object_trace {bool verified=true;bool executable_verified(){++error;++gate_calls;return verified;}}
Memory memory;
namespace engine_memory {
void next_frame(){++error;++epochs;}
bool read(std::uintptr_t p,void* out,std::size_t n){++error;return memory(p,out,n);}
}
std::vector<std::string> logs;
void log(const char* fmt,...){++error;char line[4096];va_list args;va_start(args,fmt);std::vsnprintf(line,sizeof line,fmt,args);va_end(args);logs.emplace_back(line);}
bool sector_background_requested=false,volumetric_fog_requested=false,volumetric_fog_docked=false,volumetric_fog_prefill=false;
struct Device {std::uint64_t id=1,frame=0;sector_background::Diagnostic sector_background_evidence;sector_background::AnchorSpan fog_docked_span;unsigned fog_docked_logs=0;
    std::uint32_t fog_ready_sector=0,fog_ready_sector_id=0;std::uint64_t fog_ready_frame=0;
    struct Motion { unsigned samples=0;std::uint64_t frame=0;sector_background::Sample value;
      void volumetric_fog_sector_sample(std::uint64_t f,const sector_background::Sample& s){++error;++samples;frame=f;value=s;}
    } motion_output;
};
#include "sector_background_context_under_test_inc.h"
int main(){
    Device d;memory=setup();error=123;sector_background_context(d);
    check(error==123&&api_calls==0&&gate_calls==0&&epochs==0&&memory.requested.empty()&&logs.empty());
    sector_background_requested=true;object_trace::verified=false;sector_background_context(d);
    check(error==123&&epochs==0&&memory.requested.empty()&&logs.size()==1&&logs[0].find("status=foreign_executable")!=std::string::npos);
    object_trace::verified=true;++d.frame;sector_background_context(d);
    check(error==123&&epochs==1&&logs.size()==2&&logs.back().find("status=ready")!=std::string::npos);
    check(logs.back().find("name=\"bluewell\" dust=8 near=18000000 far=18500000")!=std::string::npos);
    check(logs.back().find("rate0=1 rate1=2 rate2=3 rate3=4 rate4=5 rate5=6 rate6=7 rate7=8")!=std::string::npos);
    check(logs.back().find("config768=3 far_floor=500000000 effective_far=500000000")!=std::string::npos);
    const auto reads=memory.requested.size();const auto calls=api_calls;const auto gates=gate_calls;
    clock_ms=1001;sector_background_context(d); // second BeginScene / Present: no work
    check(error==123&&memory.requested.size()==reads&&api_calls==calls&&gate_calls==gates&&logs.size()==2);
    clock_ms=999;++d.frame;sector_background_context(d);check(error==123&&logs.size()==2&&epochs==2);
    clock_ms=1000;++d.frame;sector_background_context(d);check(error==123&&logs.size()==3&&epochs==3);
    clock_ms=1001;++d.frame;memory.word(0x5054,0);sector_background_context(d);
    check(error==123&&logs.size()==4&&logs.back().find("status=no_sector")!=std::string::npos&&logs.back().find("index=-1")!=std::string::npos);
    ++d.frame;memory.word(0x5054,0x7000);memory.word(0x713c,83);sector_background_context(d);
    check(error==123&&logs.size()==5&&logs.back().find("status=bad_index")!=std::string::npos);
    ++d.frame;memory.word(0x608504,0);sector_background_context(d);
    check(error==123&&logs.size()==6&&logs.back().find("status=no_cockpit")!=std::string::npos);
    d.sector_background_evidence.invalidate();sector_background_context(d); // same frame after refused Reset
    check(error==123&&logs.size()==7);
    ++d.frame;memory=setup();memory.bytes.erase(0x10044);sector_background_context(d);
    check(error==123&&logs.back().find("status=read_failure")!=std::string::npos);
    sector_background_requested=false;const auto after=memory.requested.size();const auto apis=api_calls;
    ++d.frame;sector_background_context(d);check(error==123&&memory.requested.size()==after&&api_calls==apis);
    // Fog uses every first successful BeginScene value, even without diagnostic
    // logging or when its cadence suppresses a line. Present is observation only.
    memory=setup();volumetric_fog_requested=true;++d.frame;const auto old_logs=logs.size();
    sector_background_context(d,true);check(d.motion_output.samples==1&&d.motion_output.frame==d.frame&&error==123&&logs.size()==old_logs);
    sector_background_context(d,true);check(d.motion_output.samples==1&&error==123);
    ++d.frame;sector_background_context(d);check(d.motion_output.samples==1&&error==123);
    ++d.frame;sector_background_requested=true;sector_background_context(d,true);
    const auto logged=logs.size();++d.frame;sector_background_context(d,true);
    check(d.motion_output.samples==3&&d.motion_output.frame==d.frame&&error==123&&logs.size()==logged);
    // --fog-docked: ship -> station (class 2) -> sector; one line per walked span, LastError kept; off keeps the mismatch.
    memory=setup();memory.word(0x500c,0x8000);memory.word(0x8054,0x9000);memory.word(0x9048,2);memory.word(0x9054,0x7000);
    sector_background_requested=false;volumetric_fog_docked=true;++d.frame;const auto undocked=logs.size();sector_background_context(d,true);
    const auto& docked=d.motion_output.value;
    check(error==123&&docked.status==sb::Status::Ready&&docked.anchor_walk==sb::AnchorWalk::Found&&docked.anchor_depth==1&&logs.size()==undocked+1&&
          logs.back().find("volumetric_fog_docked device=1")!=std::string::npos&&logs.back().find("walk=found depth=1 ref_object=00008000 ref_parent=00009000 last=00007000 sector=00007000")!=std::string::npos);
    ++d.frame;sector_background_context(d,true);check(error==123&&logs.size()==undocked+1&&d.motion_output.value.status==sb::Status::Ready);
    volumetric_fog_docked=false;++d.frame;sector_background_context(d,true);
    check(error==123&&logs.size()==undocked+1&&d.motion_output.value.status==sb::Status::AnchorMismatch&&d.motion_output.value.anchor_walk==sb::AnchorWalk::None);
    std::printf("sector_background_context_host checks=%u failures=0\n",checks);
}
