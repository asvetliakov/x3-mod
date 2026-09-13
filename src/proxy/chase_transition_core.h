#pragma once
#include "chase_transition.h"
#include <cstddef>
#include <cstring>

namespace x3m::chase_transition::detail {
constexpr unsigned lifetime_capacity=64, thread_capacity=8;
// These structures contain no platform calls. Production holds one SRW lock
// across each operation; host fixtures exercise this exact state machine.
struct Lifetime {
    std::uintptr_t cockpit=0;
    std::uint64_t generation=0;
    std::uint32_t constructor_thread=0;
    bool complete=false;
};
struct Thread {
    std::uint32_t id=0;
    std::uintptr_t cockpit=0, frame=0;
    Update token;
};
struct State {
    Lifetime lives[lifetime_capacity]{};
    Thread threads[thread_capacity]{};
    std::uint64_t next_generation=0, next_serial=0;
    std::uint64_t lifetime_overflow=0, thread_overflow=0;
    Lifetime* find(std::uintptr_t cockpit) noexcept {
        if(!cockpit)return nullptr;
        for(auto& l:lives)if(l.cockpit==cockpit)return &l;
        return nullptr;
    }
    void revoke(std::uintptr_t cockpit) noexcept {
        for(auto& t:threads)if(t.cockpit==cockpit){t.cockpit=0;t.frame=0;t.token={};}
    }
    std::uint64_t construct(std::uintptr_t cockpit,std::uint32_t thread) noexcept {
        if(!cockpit || (cockpit&3) || !thread)return 0;
        revoke(cockpit);
        auto* l=find(cockpit);
        if(!l)for(auto& empty:lives)if(!empty.cockpit){l=&empty;break;}
        if(!l){++lifetime_overflow;return 0;}
        // Counter exhaustion must never wrap and accidentally revive a token.
        *l={};
        if(next_generation==UINT64_MAX)return 0;
        *l={cockpit,++next_generation,thread,false};return l->generation;
    }
    bool complete(std::uintptr_t cockpit,std::uint32_t thread) noexcept {
        auto* l=find(cockpit);
        if(!l || l->constructor_thread!=thread || l->complete)return false;
        l->complete=true;return true;
    }
    void destroy(std::uintptr_t cockpit) noexcept {
        revoke(cockpit);if(auto* l=find(cockpit))*l={};
    }
    std::uint64_t generation(std::uintptr_t cockpit) noexcept {
        auto* l=find(cockpit);return l && l->complete?l->generation:0;
    }
    Thread* thread_slot(std::uint32_t id) noexcept {
        if(!id)return nullptr;
        for(auto& t:threads)if(t.id==id)return &t;
        for(auto& t:threads)if(!t.id){t.id=id;return &t;}
        // Only inactive slots may be reused. Active tokens are never evicted.
        for(auto& t:threads)if(!t.cockpit){t={};t.id=id;return &t;}
        ++thread_overflow;return nullptr;
    }
    Update begin(std::uintptr_t cockpit,std::uintptr_t frame,std::uint32_t id) noexcept {
        auto* t=thread_slot(id);if(!t)return {};
        t->cockpit=0;t->frame=0;t->token={};
        const auto gen=generation(cockpit);
        if(!gen || !frame || next_serial==UINT64_MAX)return {};
        t->cockpit=cockpit;t->frame=frame;t->token={gen,++next_serial,id};return t->token;
    }
    void end(std::uintptr_t frame,std::uint32_t id) noexcept {
        for(auto& t:threads)if(t.id==id && t.frame==frame){t.cockpit=0;t.frame=0;t.token={};}
    }
    Update current(std::uintptr_t cockpit,std::uint32_t id) noexcept {
        for(auto& t:threads)if(t.id==id && t.cockpit==cockpit && t.token.generation &&
            t.token.generation==generation(cockpit))return t.token;
        return {};
    }
};
// Preserve the first 16 and the last 32 changes between report calls. Reading
// drains under the same lock; overwritten middle records have an exact counter.
template<class T,unsigned First=16,unsigned Last=32> struct Window {
    T first[First]{},last[Last]{};
    unsigned first_used=0,last_used=0,next=0;
    std::uint64_t dropped=0;
    void push(const T& value) noexcept {
        if(first_used<First){first[first_used++]=value;return;}
        if(last_used<Last)++last_used;else ++dropped;
        last[next]=value;next=(next+1)%Last;
    }
};
struct Origin {
    // valid: task=1, expected native instruction/dispatcher=2, method=4,
    // bounded stack=8; flags: read/contract refusal=1, stack truncation=2,
    // more candidate pairs than retained=4. No guessed script source names.
    std::uint32_t valid=0,flags=0,task=0,pc=0,method=0,entry=0;
    std::uint32_t returns[4]{},methods[4]{};
    unsigned count=0;
};
template<class Reader,class CodeRange>
void provenance(std::uint32_t ebp,Origin& o,Reader bytes,CodeRange code_address) {
    auto field=[&](std::uintptr_t base,unsigned offset,auto& out){return bytes(base,offset,&out,sizeof out);};
    auto code_offset=[&](std::uint32_t code,std::uint32_t offset){std::uintptr_t at=0;unsigned char op=0;
        return code_address(code,offset,1,at)&&field(at,0,op);};
    std::uint32_t vm=0,code=0,command=0,return_pc=0;
    if(!field(ebp,4,return_pc)||return_pc!=0x4a3909||!field(ebp,0xc,o.task)||
       !field(ebp,0x10,command)||command!=0x30||!field(0x6085e4,0,vm)||
       !field(vm,8,code)||!field(o.task,0x1c,o.pc)||!field(o.task,0x3c,o.method)) {o.flags|=1;return;}
    o.valid|=1;
    std::uintptr_t at=0;unsigned char op[5]{};
    if(o.pc<5||!code_address(code,o.pc-5,5,at)||!bytes(at,0,op,5)){o.flags|=1;return;}
    const unsigned group=unsigned(op[1])|(unsigned(op[2])<<8);
    const unsigned cmd=unsigned(op[3])|(unsigned(op[4])<<8);
    std::uint32_t dispatch=0;
    // The native dispatch table precedes VM+1454. A verified table entry and
    // exact opcode/command are stronger provenance than the generic return PC.
    if(op[0]!=0x83||cmd!=0x30||group>(0x1454/24)-3||
       !field(vm,(group+2)*24,dispatch)||dispatch!=0x42d340){o.flags|=1;return;}
    o.valid|=2;
    if(o.method && !(o.method&3) && field(o.method,0,o.entry)&&code_offset(code,o.entry))o.valid|=4;
    else o.flags|=1;
    std::uint32_t base=0,capacity=0;std::int32_t index=0;
    if(!field(o.task,0x14,base)||!field(o.task,0x10,capacity)||!field(o.task,0x18,index)||
       index>0||capacity>1048576||index < -std::int32_t(capacity)) {o.flags|=1;return;}
    const auto used=std::uint32_t(-std::int64_t(index));
    if(used>base/5){o.flags|=1;return;}
    const unsigned cells=used<64?used:64;
    unsigned char stack[320]{};
    if(cells && !bytes(base-used*5,0,stack,cells*5)){o.flags|=1;return;}
    o.valid|=8;if(used>64)o.flags|=2;
    for(unsigned i=0;i+1<cells;++i)if(stack[i*5]==10 && stack[(i+1)*5]==3){
        std::uint32_t method=0,entry=0,ret=0;
        std::memcpy(&method,stack+i*5+1,4);std::memcpy(&ret,stack+(i+1)*5+1,4);
        if(!method||(method&3)||!field(method,0,entry)||!code_offset(code,entry)||!code_offset(code,ret)){o.flags|=1;continue;}
        if(o.count==4){o.flags|=4;break;}
        o.methods[o.count]=entry;o.returns[o.count++]=ret;
    }
}
}
