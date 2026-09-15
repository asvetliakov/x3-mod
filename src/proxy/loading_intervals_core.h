#pragma once
#include <cstdint>

// Plain integer payload; one writer per slot. Read only after Gate is closed
// and drained. No atomics on 64-bit payload (x86 light handlers must avoid FP).
namespace x3m::loading_trace::intervals {
constexpr unsigned slot_limit=16, capacity=65536;
enum Flag : uint32_t { Allocation=1, Tls=2, Threads=4, Clock=8, Sequence=16 };
struct Record { uint64_t begin,end; uint32_t operation,reserved; };
static_assert(sizeof(Record)==24);
struct Ring {
    uint32_t tid=0,generation=0,count=0,completed=0;
    uint64_t lost_begin=0,lost_end=0;
    uint32_t flags=0,reserved=0;
    uint64_t last_end=0;
    void append(Record* records,unsigned limit,uint64_t begin,uint64_t end,unsigned operation) noexcept {
        if(!begin||end<begin||end<last_end){flags|=Clock;return;}
        last_end=end;
        if(completed==UINT32_MAX){flags|=Sequence;return;}
        const unsigned index=completed%limit;
        if(count==limit){
            const auto& old=records[index];
            if(!lost_begin||old.begin<lost_begin)lost_begin=old.begin;
            if(old.end>lost_end)lost_end=old.end;
        }else ++count;
        records[index].begin=begin;records[index].end=end;
        records[index].operation=operation;records[index].reserved=0;
        ++completed;
    }
};
static_assert(sizeof(Ring)==48);
// Atomic supplies acquire/release-capable read/CAS/exchange and increment /
// decrement. Accepted tokens publish their entire record before decrement.
// Closing may race a rejected late increment: that token never touches payload.
template<class Atomic> struct Gate {
    Atomic enabled,active;
    bool enter() noexcept {
        if(!enabled.read())return false;
        // Saturation refuses without wrapping; existing tokens keep drain blocked.
        auto n=active.read();
        for(;;){
            if(n==0x7fffffff)return false;
            const auto old=active.compare(n+1,n);
            if(old==n){
                // Reserve saturation as an intentionally abandoned token.
                // Even if other calls finish later, this capture cannot drain.
                if(n==0x7ffffffe)return false;
                break;
            }
            n=old;
        }
        return recheck();
    }
    bool recheck() noexcept {
        if(enabled.read())return true;
        active.decrement();return false;
    }
    void leave() noexcept {active.decrement();}
    void close() noexcept {enabled.exchange(0);}
    bool drained() noexcept {return !enabled.read()&&!active.read();}
};
struct Header {
    char magic[8];
    uint32_t schema,header_bytes,record_bytes,slots,ring_capacity,flags,registered,present_tid;
    uint64_t frequency,initialized,begin,end,device,reset,frame;
};
static_assert(sizeof(Header)==96);
}
