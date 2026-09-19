#include "sector_background_memory.h"
#include <cstring>
int main(){
    auto m=setup();auto v=sb::sample(m);
    check(v.status==sb::Status::Ready&&v.cockpit==0x5000&&v.sector==0x7000&&v.record==0x10044&&v.row_valid);
    check(v.name_valid&&!std::strcmp(v.family,"bluewell")&&v.dust==8&&v.fog_near==18000000&&v.fog_far==18500000);
    check(v.camera_check==sb::Check::Match&&v.anchor_check==sb::Check::Unavailable&&v.effective_far==500000000);
    check(v.stardust==25&&v.rates[0]==1&&v.rates[7]==8&&v.neb==12&&v.stars==13);
    unsigned rows=0;for(auto r:m.requested)if(r.first>=0x10000&&r.first<0x10164){++rows;check(r.first==0x10044&&r.second==0x120);}check(rows==1);
    auto b=m;b.bytes.erase(0x608504);check(sb::sample(b).status==sb::Status::ReadFailure);
    b=m;b.word(0x608504,0);check(sb::sample(b).status==sb::Status::NoCockpit);
    b=m;b.word(0x1010,0);check(sb::sample(b).status==sb::Status::NoCockpit); // stale handle0 never matches
    b=m;b.word(0x2004,3);check(sb::sample(b).status==sb::Status::Malformed);
    b=m;b.word(0x2004,0);check(sb::sample(b).status==sb::Status::Malformed);
    b=m;b.word(0x2004,131072);check(sb::sample(b).status==sb::Status::Malformed);
    b=m;b.word(0x4004,8);check(sb::sample(b).status==sb::Status::Missing);
    b.word(0x4000,0x4000);check(sb::sample(b).status==sb::Status::Cycle);
    b=m;for(unsigned i=0;i<33;++i){const auto p=0x4000+16*i;b.word(p,p+16);b.word(p+4,8);b.word(p+8,0);}check(sb::sample(b).status==sb::Status::Limit);
    b=m;b.word(0x4008,0x5001);check(sb::sample(b).status==sb::Status::Malformed);
    b=m;b.word(0x4008,0xfffffffc);check(sb::sample(b).status==sb::Status::ReadFailure);
    b=m;b.word(0x5054,0);b.word(0x500c,0x8000);b.word(0x8054,0x7000);v=sb::sample(b);
    check(v.status==sb::Status::NoSector&&v.sector==0&&v.ref_sector==0x7000&&!v.row_valid); // never substitute
    b=m;b.word(0x5054,0x7001);check(sb::sample(b).status==sb::Status::Malformed);
    b=m;b.word(0x7048,2);check(sb::sample(b).status==sb::Status::BadClass);
    b=m;b.word(0x607040,0);check(sb::sample(b).status==sb::Status::Loading);
    b=m;b.word(0x606fc0,0);check(sb::sample(b).status==sb::Status::Loading);
    b=m;b.word(0x607040,4097);check(sb::sample(b).status==sb::Status::BadCount);
    b=m;b.word(0x607040,0xffffffff);check(sb::sample(b).status==sb::Status::BadCount);
    b=m;b.word(0x713c,83);v=sb::sample(b);check(v.status==sb::Status::BadIndex&&!v.row_valid&&v.index==83);
    b.word(0x713c,0xffffffff);check(sb::sample(b).status==sb::Status::BadIndex);
    b=m;b.word(0x606fc0,0xfffffffc);check(sb::sample(b).status==sb::Status::ReadFailure);
    b=m;b.word(0x606fc0,0xfffffe00);b.word(0x713c,1);check(sb::sample(b).status==sb::Status::ReadFailure);
    b=m;b.bytes.erase(0x10163);check(sb::sample(b).status==sb::Status::ReadFailure); // full row required
    b=m;b.word(0x10134,65);check(sb::sample(b).status==sb::Status::BadDust);
    b.word(0x10134,0xffffffff);check(sb::sample(b).status==sb::Status::BadDust);
    b=m;b.word(0x1014c,17999999);check(sb::sample(b).status==sb::Status::BadFog);
    b=m;b.word(0x10148,0);check(sb::sample(b).status==sb::Status::BadFog);
    b.word(0x1014c,0);b.word(0x6270,0);v=sb::sample(b);check(v.row_valid&&v.status==sb::Status::Ready&&v.camera_check==sb::Check::Inactive);
    b=m;b.word(0x1014c,2000000001);check(sb::sample(b).status==sb::Status::BadFog);
    b=m;b.word(0x10148,0xffffffff);check(sb::sample(b).status==sb::Status::BadFog);
    b=m;b.word(0x6370,500000000);v=sb::sample(b);check(v.status==sb::Status::CameraMismatch&&v.camera_check==sb::Check::Mismatch); // floor does not mask raw mismatch
    b=m;b.word(0x30768,2);v=sb::sample(b);check(v.camera_check==sb::Check::Match&&v.effective_far==100000000);
    b.word(0x30768,1);check(sb::sample(b).effective_far==18500000);
    b.word(0x30768,3);b.word(0x1014c,600000000);b.word(0x6370,600000000);v=sb::sample(b);check(v.status==sb::Status::Ready&&v.effective_far==600000000);
    b=m;b.bytes.erase(0x30768);v=sb::sample(b);check(!v.config_valid&&v.camera_check==sb::Check::Match);
    b=m;b.word(0x5058,0);v=sb::sample(b);check(!v.camera_valid&&v.camera_check==sb::Check::Unavailable&&v.row_valid);
    b=m;b.word(0x500c,0x8000);b.word(0x8054,0x7000);check(sb::sample(b).anchor_check==sb::Check::Match);
    b.word(0x8054,0x9000);check(sb::sample(b).status==sb::Status::AnchorMismatch);
    b=m;for(unsigned i=0;i<32;++i)b.bytes[0x20001+i]='A';v=sb::sample(b);check(!v.name_valid&&v.family[0]==0&&v.name_pointer==0x20001);
    b=m;b.bytes[0x20001]='\n';check(!sb::sample(b).name_valid);
    b=m;b.word(0x10044,0xffffffff);check(!sb::sample(b).name_valid);
    b=m;b.bytes[0x20001]='"';b.bytes[0x20002]='\\';b.bytes[0x20003]=0;check(!std::strcmp(sb::sample(b).family,"\\\"\\\\"));
    // Reallocation and sector/index mutations are resolved anew, with no old address reuse.
    b=m;for(unsigned i=0;i<0x120;++i)b.bytes[0x40044+0xdb8+i]=m.bytes[0x10044+i];
    b.word(0x606fc0,0x40000);b.word(0x713c,1);b.word(0x5054,0x9000);
    for(unsigned i=0;i<0x148;++i)b.bytes[0x9000+i]=b.bytes[0x7000+i];
    b.word(0x40044+0xdb8+0xf0,16);v=sb::sample(b);check(v.status==sb::Status::Ready&&v.sector==0x9000&&v.index==1&&v.table==0x40000&&v.dust==16);
    const auto reads=m.requested.size();std::uint32_t word=0;
    check(!sb::field(m,0xfffffffc,8,word)&&m.requested.size()==reads);
    check(!sb::span(m,4,UINT64_MAX,&word,4)&&m.requested.size()==reads);
    check(!sb::span(m,0xfffffffc,0,&word,8)&&m.requested.size()==reads);
    check(!sb::field(m,0x1001,0,word)&&m.requested.size()==reads);
    sb::Diagnostic d;v=sb::sample(m);check(d.begin(0)&&!d.begin(0)&&d.begin(1));
    check(d.emit(v,100)&&!d.emit(v,1099)&&d.emit(v,1100));
    auto changed=v;changed.sector+=4;check(d.emit(changed,1101));
    changed.status=sb::Status::NoSector;check(d.emit(changed,1102));
    changed=v;changed.table+=4;check(d.emit(changed,1103));
    changed=v;changed.index++;check(d.emit(changed,1104));
    changed=v;changed.dust++;check(d.emit(changed,1105));
    changed=v;changed.fog_near++;check(d.emit(changed,1106));
    changed=v;changed.fog_far++;check(d.emit(changed,1107));
    changed=v;changed.cockpit+=4;check(d.emit(changed,1108));
    d.invalidate();check(d.begin(1)&&d.emit(v,1109));
    std::printf("sector_background_host checks=%u failures=0 sample_bytes=%zu reads_ready=%zu\n",checks,sizeof(sb::Sample),reads);
}
