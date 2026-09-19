#pragma once
// Read-only, frame-scoped diagnostic. Layout proof: sector-fog.md section 11.
// No Windows/D3D dependencies, engine calls, retained pointers or allocation.
#include <cstddef>
#include <cstdint>

namespace x3m::sector_background {
enum class Status : unsigned {
    Ready, ForeignExecutable, ReadFailure, Malformed, Cycle, Limit, Missing,
    NoCockpit, NoSector, Loading, BadClass, BadCount, BadIndex, BadDust, BadFog,
    CameraMismatch, AnchorMismatch
};
inline const char* name(Status s) {
    switch(s) {
    case Status::Ready:return "ready";case Status::ForeignExecutable:return "foreign_executable";
    case Status::ReadFailure:return "read_failure";case Status::Malformed:return "malformed";
    case Status::Cycle:return "cycle";case Status::Limit:return "limit";case Status::Missing:return "missing";
    case Status::NoCockpit:return "no_cockpit";case Status::NoSector:return "no_sector";
    case Status::Loading:return "loading";case Status::BadClass:return "bad_class";
    case Status::BadCount:return "bad_count";case Status::BadIndex:return "bad_index";
    case Status::BadDust:return "bad_dust";case Status::BadFog:return "bad_fog";
    case Status::CameraMismatch:return "camera_mismatch";case Status::AnchorMismatch:return "anchor_mismatch";
    }
    return "invalid";
}
enum class Check : unsigned { Unavailable, Inactive, Match, Mismatch };
inline const char* name(Check s) {
    switch(s) {case Check::Unavailable:return "unavailable";case Check::Inactive:return "inactive";
    case Check::Match:return "match";case Check::Mismatch:return "mismatch";}
    return "invalid";
}
// Perform arithmetic in 64 bits even on x86; no wrapped read is delegated to
// the memory backend. Names need byte alignment; object fields require dwords.
template<class Read> bool span(Read& read,std::uint32_t base,std::uint64_t offset,void* out,std::size_t size) {
    if(offset>std::uint64_t(UINT32_MAX)-base)return false;
    const std::uint64_t at=std::uint64_t(base)+offset;
    return base && at<=UINT32_MAX && size<=std::uint64_t(UINT32_MAX)+1-at &&
        read(std::uintptr_t(at),out,size);
}
template<class Read,class T> bool field(Read& read,std::uint32_t base,unsigned offset,T& out) {
    return !(base&3) && span(read,base,offset,&out,sizeof out);
}
struct Sample {
    Status status=Status::ReadFailure;
    Check camera_check=Check::Unavailable,anchor_check=Check::Unavailable;
    std::uint32_t registry=0,handle=0,cockpit=0,sector=0,table=0,record=0,name_pointer=0;
    std::uint32_t neb=0,stars=0,camera=0,flags270=0,ref_object=0,ref_sector=0;
    std::int32_t index=-1,count=0,dust=-1,fog_near=0,fog_far=0,stardust=0,rates[8]{};
    std::int32_t cam_near=0,cam_far=0,config=0,far_floor=0,effective_far=0;
    std::int16_t class48=0;
    bool row_valid=false,name_valid=false,camera_valid=false,config_valid=false;
    char family[65]{}; // escaped printable name, never a truncated untrusted string
};
template<class Read> Sample sample(Read& read) {
    Sample out;std::uint32_t header=0,bucket[2]{},link=0;
    if(!field(read,0x608504,0,out.registry))return out;
    if(!out.registry){out.status=Status::NoCockpit;return out;}
    if(out.registry&3){out.status=Status::Malformed;return out;}
    if(!field(read,out.registry,0x10,out.handle))return out;
    if(!out.handle){out.status=Status::NoCockpit;return out;}
    if(!field(read,out.registry,0,header))return out;
    if(!header||(header&3)){out.status=Status::Malformed;return out;}
    if(!field(read,header,0,bucket))return out;
    const auto n=bucket[1];
    if(!n||n>65536||(n&(n-1))||!bucket[0]||(bucket[0]&3)){out.status=Status::Malformed;return out;}
    if(!field(read,bucket[0],4*((n-1)&out.handle),link))return out;
    std::uint32_t seen[32]{};unsigned walked=0;
    while(link) {
        if(link&3){out.status=Status::Malformed;return out;}
        for(unsigned i=0;i<walked;++i)if(seen[i]==link){out.status=Status::Cycle;return out;}
        if(walked==32){out.status=Status::Limit;return out;}
        seen[walked++]=link;std::uint32_t row[3]{};
        if(!field(read,link,0,row))return out;
        if(row[1]==out.handle){out.cockpit=row[2];break;}
        link=row[0];
    }
    if(!link){out.status=Status::Missing;return out;}
    if(!out.cockpit){out.status=Status::NoCockpit;return out;}
    if(out.cockpit&3){out.status=Status::Malformed;return out;}
    if(!field(read,out.cockpit,0x54,out.sector))return out;
    // Independent cross-check only. A missing ship never prevents the direct
    // cockpit->sector sample; a ship sector never replaces a missing sector.
    if(field(read,out.cockpit,0xc,out.ref_object)&&out.ref_object&&
       field(read,out.ref_object,0x54,out.ref_sector)&&out.ref_sector&&!(out.ref_sector&3))
        out.anchor_check=out.sector==out.ref_sector?Check::Match:Check::Mismatch;
    if(!out.sector){out.status=Status::NoSector;return out;}
    if(out.sector&3){out.status=Status::Malformed;return out;}
    if(!field(read,out.sector,0x48,out.class48))return out;
    if(out.class48!=1){out.status=Status::BadClass;return out;}
    if(!field(read,0x607040,0,out.count)||!field(read,0x606fc0,0,out.table))return out;
    if(!out.table||!out.count){out.status=Status::Loading;return out;}
    if(out.count<0||out.count>4096){out.status=Status::BadCount;return out;}
    if(out.table&3){out.status=Status::Malformed;return out;}
    if(!field(read,out.sector,0x13c,out.index))return out;
    if(out.index<0||out.index>=out.count){out.status=Status::BadIndex;return out;}
    const std::uint64_t offset=std::uint64_t(out.index)*0xdb8+0x44;
    std::uint32_t row[0x120/4]{};
    if(!span(read,out.table,offset,row,sizeof row))return out;
    out.record=std::uint32_t(std::uint64_t(out.table)+offset);
    out.name_pointer=row[0];out.dust=std::int32_t(row[0xf0/4]);
    out.fog_near=std::int32_t(row[0x104/4]);out.fog_far=std::int32_t(row[0x108/4]);
    out.stardust=std::int32_t(row[0x10c/4]);
    for(unsigned i=0;i<8;++i)out.rates[i]=std::int32_t(row[0xd0/4+i]);
    if(out.dust<0||out.dust>64){out.status=Status::BadDust;return out;}
    if(!((out.fog_near==0&&out.fog_far==0)||(out.fog_near>0&&out.fog_near<=out.fog_far&&out.fog_far<=2000000000))) {
        out.status=Status::BadFog;return out;
    }
    out.row_valid=true;
    char raw[32]{};
    if(span(read,out.name_pointer,0,raw,sizeof raw)) {
        unsigned length=0;
        while(length<sizeof raw&&raw[length]>=0x20&&raw[length]<=0x7e)++length;
        if(length<sizeof raw&&raw[length]=='\0') {
            unsigned to=0;
            for(unsigned i=0;i<length;++i){if(raw[i]=='"'||raw[i]=='\\')out.family[to++]='\\';out.family[to++]=raw[i];}
            out.family[to]='\0';out.name_valid=true;
        }
    }
    if(!field(read,out.sector,0x140,out.neb)||!field(read,out.sector,0x144,out.stars))return out;
    if(field(read,out.cockpit,0x58,out.camera)&&out.camera&&!(out.camera&3)) {
        std::int32_t fog[2]{};
        if(field(read,out.camera,0x270,out.flags270)&&field(read,out.camera,0x36c,fog)) {
            out.camera_valid=true;out.cam_near=fog[0];out.cam_far=fog[1];
            out.camera_check=(out.flags270&0x10000)?
                (out.cam_near==out.fog_near&&out.cam_far==out.fog_far?Check::Match:Check::Mismatch):Check::Inactive;
        }
    }
    std::uint32_t config=0;
    if(field(read,0x606f34,0,config)&&field(read,config,0x768,out.config)) {
        out.config_valid=true;
        out.far_floor=out.config>=3?500000000:(out.config==2?100000000:0);
        out.effective_far=out.fog_far>out.far_floor?out.fog_far:out.far_floor;
    }
    // The floor is applied in registers at 004c2c34..004c2c63, NOT written to
    // the camera (0042156e/00421574 copy the raw pair). Never excuse a raw mismatch.
    out.status=out.camera_check==Check::Mismatch?Status::CameraMismatch:
        out.anchor_check==Check::Mismatch?Status::AnchorMismatch:Status::Ready;
    return out;
}
inline bool same_tuple(const Sample& a,const Sample& b) {
    return a.status==b.status&&a.camera_check==b.camera_check&&a.anchor_check==b.anchor_check&&
        a.cockpit==b.cockpit&&a.sector==b.sector&&a.index==b.index&&a.table==b.table&&
        a.dust==b.dust&&a.fog_near==b.fog_near&&a.fog_far==b.fog_far;
}
struct Diagnostic {
    bool sampled=false,logged=false;
    std::uint64_t frame=0,last_ms=0;
    Sample previous{}; // value copy only, never dereferenced again
    bool begin(std::uint64_t f) {
        if(sampled&&frame==f)return false;
        sampled=true;frame=f;return true;
    }
    bool emit(const Sample& value,std::uint64_t now_ms) {
        const bool due=!logged||!same_tuple(value,previous)||now_ms-last_ms>=1000;
        if(due){logged=true;last_ms=now_ms;previous=value;}
        return due;
    }
    void invalidate(){sampled=logged=false;previous={};}
};
}
