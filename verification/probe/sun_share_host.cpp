#include "../../src/renderer/material_motion.h"
#include "../../src/renderer/sun_share_frame.h"
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <new>
static bool fail_allocation=false;
void* operator new(std::size_t n){if(fail_allocation)throw std::bad_alloc();if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
using namespace x3m::renderer;
static unsigned checks=0;
void check(bool value){++checks;if(!value){std::fprintf(stderr,"failed %u\n",checks);std::exit(1);}}
int main(){
    using Words=std::vector<std::uint32_t>;
    const Words original={0xffff0300,0x05000051,0xa00f0000,0x3f000000,0,0,0x3f800000,
        0x02000001,0x800f0800,0xa0e40000,0x02000001,0x800f0801,0xa0e40000,
        0x02000001,0x800f0802,0xa0e40000,0xffff};
    auto program=original;check(material_motion_invalid_sun_share(program));check(program.size()==original.size()+9);
    check(std::equal(original.begin()+1,original.end()-1,program.begin()+7));
    check(program[program.size()-3]==0x80020802&&program[program.size()-2]==0xa00000dd);
    const auto augmented=program;check(!material_motion_invalid_sun_share(program)&&program==augmented);
    for(unsigned mutation=0;mutation<5;++mutation){program=original;
        if(mutation==0)program[0]=0xffff0200;
        if(mutation==1)program[program.size()-3]=0x800f0800;
        if(mutation==2)program[program.size()-2]=0xa00000dd;
        if(mutation==3)program[1]=0x0f000051;
        if(mutation==4)program[program.size()-2]=0xa0002000;
        const auto saved=program;check(!material_motion_invalid_sun_share(program)&&program==saved);
    }
    program=original;fail_allocation=true;const bool changed=material_motion_invalid_sun_share(program);fail_allocation=false;check(!changed&&program==original);
    SunShareFrame f;check(!f.publish(true,true,true));f.draw(true,true,false);check(f.publish(true,true,false));
    f.draw(false,false,false);check(f.publish(true,true,false)); // Failed native calls are no writes.
    f.draw(true,false,false,true);check(f.publish(true,true,false)); // Nonreceiver depth writes -1 share.
    f.draw(true,false,true);check(!f.publish(true,true,false));check(f.publish(true,true,true));
    f.draw(true,false,false);check(!f.publish(true,true,true)&&f.untracked==1); // Native blend after receiver.
    check(f.reasons[0]==1); // Reason buckets: default folds to unknown.
    check(f.draw(true,false,false,false,SunUntrackedReason::Blended)&&f.untracked==2&&f.reasons[6]==1);
    check(!f.draw(true,false,false,true,SunUntrackedReason::Blended)&&f.reasons[6]==1); // A depth writer is never untracked.
    check(!f.draw(false,false,false,false,SunUntrackedReason::Blended)&&f.untracked==2); // Failed native call.
    check(f.draw(true,false,false,false,SunUntrackedReason(200))&&f.untracked==3&&f.reasons[0]==2); // Out-of-range folds to unknown.
    // A color writer that wrote no depth (z test or z write off) is never an
    // untracked writer: it is counted non_writers, never a reason bucket.
    check(!f.draw(true,false,false,false,SunUntrackedReason::NoZWrite,false)&&f.untracked==3&&f.non_writers==1&&f.reasons[5]==0);
    check(!f.draw(true,false,false,false,SunUntrackedReason::Blended,false)&&f.non_writers==2&&f.reasons[6]==1);
    f={};f.draw(true,true,false);check(!f.draw(true,false,false,false,SunUntrackedReason::Unregistered,false)&&f.publish(true,true,false)&&f.non_writers==1&&!f.untracked); // Non-writers alone keep the frame available.
    check(f.draw(true,false,false,false,SunUntrackedReason::ReadFailed,true)&&!f.publish(true,true,false)); // Unknown z state stays a writer (fail closed).
    f={};check(!f.draw(true,false,false,false,SunUntrackedReason::Unknown,false)&&!f.non_writers&&!f.untracked); // Before the first receiver nothing is counted.
    check(std::strcmp(sun_untracked_reason_name(unsigned(SunUntrackedReason::Blended)),"blended")==0&&std::strcmp(sun_untracked_reason_name(unsigned(SunUntrackedReason::ReadFailed)),"read_failed")==0
          &&sun_untracked_reason_count==16&&std::strcmp(sun_untracked_reason_name(sun_untracked_reason_count),"unknown")==0);
    f={};f.draw(true,false,false);f.draw(true,true,false);check(f.publish(true,true,false)); // Earlier background.
    check(!f.publish(false,true,true));check(!f.publish(true,false,true));
    f.failed=true;check(!f.publish(true,true,true)); // Late creation/bind failure poisons only lane frame.
    f={};f.draw(true,false,true);f.draw(true,true,false);check(!f.publish(true,true,false));
    f={};check(!f.available&&!f.published&&!f.coverage_required&&!f.receivers); // frame/reset invalidation.
    const auto begin=std::chrono::steady_clock::now();
    volatile unsigned sink=0;unsigned random=unsigned(begin.time_since_epoch().count());
    for(unsigned i=0;i<1000000;++i){random=random*1664525u+1013904223u;SunShareFrame frame;frame.draw(true,random&1,false);frame.draw(true,false,random&2);sink+=frame.publish(true,true,random&4);}
    const auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count();
    std::printf("PASS checks=%u synthetic_bookkeeping_ns=%.3f sink=%u\n",checks,elapsed/1000000,sink);
}
