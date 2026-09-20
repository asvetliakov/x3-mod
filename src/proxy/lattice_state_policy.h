#pragma once
// Integer-only selector and bounded packet policy; host-testable without D3D.
#include <cstdint>
#include <cstddef>
namespace x3m::lattice_state {
constexpr const char* selector_name="run177_panel_position_v1";
constexpr std::uint64_t source_vs=0x4944d81dfe531b37ull, source_ps=0x5e0a10fe752b6140ull;
constexpr std::uint32_t position[3]={0x00bc6871,0x002dd083,0xff415956};
constexpr unsigned field_limit=256,word_limit=40000,shader_word_limit=16384;
struct Arguments {std::uint32_t topology=0,primitives=0,minimum=0,vertices=0,start=0;std::int32_t base=0;};
inline int signature(const Arguments& a) noexcept {
    if(a.topology!=4||a.base||a.minimum||a.start)return -1;
    return a.primitives==3784&&a.vertices==9680?0:a.primitives==940&&a.vertices==1267?1:-1;
}
struct Object {
    bool scoped=false;std::uint32_t valid=0,model=0,lod=0,position[3]{};
    std::uint64_t session=0;std::uint32_t node=0,handle=0;
};
inline bool object_matches(const Object& o) noexcept {
    return o.scoped&&(o.valid&1)&&o.model==0x54b3&&!o.lod&&
        o.position[0]==position[0]&&o.position[1]==position[1]&&o.position[2]==position[2];
}
enum class Status {Off,Armed,Complete,NoMatch,Partial,Ambiguous,Unavailable,Reset,Capacity,SubmissionFailed};
inline const char* name(Status s) noexcept {
    switch(s){case Status::Off:return "off";case Status::Armed:return "armed";case Status::Complete:return "complete";
    case Status::NoMatch:return "no_match";case Status::Partial:return "partial";case Status::Ambiguous:return "ambiguous";
    case Status::Unavailable:return "unavailable";case Status::Reset:return "reset";case Status::Capacity:return "capacity";
    case Status::SubmissionFailed:return "submission_failed";}return "invalid";
}
struct Policy {
    Status status=Status::Off;unsigned matches[2]{};Object first{};
    void arm() noexcept {status=Status::Armed;matches[0]=matches[1]=0;first={};}
    void refuse(Status s) noexcept {if(status==Status::Armed)status=s;}
    bool accept(unsigned slot,const Object& o) noexcept {
        if(status!=Status::Armed||slot>1)return false;
        if(matches[slot]++ || ((matches[0]+matches[1])>1 &&
           (o.session!=first.session||o.node!=first.node||o.handle!=first.handle))){status=Status::Ambiguous;return false;}
        if(matches[0]+matches[1]==1)first=o;
        return true;
    }
    bool accept_match(unsigned slot,const Object& o,std::uint64_t vs,std::uint64_t ps,bool declaration_matches) noexcept {
        if(vs!=source_vs||ps!=source_ps||!declaration_matches)return false;
        return accept(slot,o);
    }
    Status finish() noexcept {
        if(status==Status::Armed)status=matches[0]&&matches[1]?Status::Complete:
            matches[0]||matches[1]?Status::Partial:Status::NoMatch;
        return status;
    }
};
inline bool fits(unsigned used,unsigned count,unsigned capacity) noexcept {return used<=capacity&&count<=capacity-used;}
}
