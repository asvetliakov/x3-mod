#pragma once
#include "stamp_core.h"
namespace x3m::light_phases::detail {
constexpr unsigned buckets=3, window_frames=300, max_depth=8;
enum Caller : unsigned { Cockpit, Traversal, Unknown };
constexpr std::uint32_t cockpit_return=0x00421740, traversal_return=0x0047dff6;
inline unsigned classify(std::uint32_t address) noexcept {
    return address==cockpit_return?Cockpit:address==traversal_return?Traversal:Unknown;
}
// X3 fixture measured 107.1 ns/dispatch (2026-09-20), rounded to 107.
// Estimate excludes window reduction/logging; never subtract from raw time.
constexpr std::uint64_t dispatch_cost_ns=107;
using Gate=x3m::stamp::Gate;
struct Errors {
    std::uint64_t nested=0,overflow=0,mismatch=0,unmatched=0,clock_failures=0,clock_reversal=0,reentry=0,mode_refused=0;
};
struct Sample {
    bool valid=false;
    std::uint64_t frame=0,ticks[buckets]{},us[buckets]{},self_us=0;
    std::uint32_t entries[buckets]{},calls[buckets]{},stamps=0;
};
struct Accumulator {
    std::uint32_t tokens[max_depth]{},depth=0,caller=Unknown;
    std::uint64_t opened=0;
    bool poisoned=false;
    Sample pending{};
    Errors errors{}; // window counters survive take/discard
    void abandon() noexcept { errors.unmatched+=depth;depth=0;opened=0; }
    void refuse_mode() noexcept { ++errors.mode_refused;poisoned=true;abandon(); }
    void interrupted() noexcept { ++errors.reentry;poisoned=true;abandon(); }
    void stamp(unsigned site,std::uint32_t token,std::uint32_t return_address,std::uint64_t now) noexcept {
        ++pending.stamps;
        if(!now)++errors.clock_failures;
        if(site==0){
            const unsigned bucket=classify(return_address);
            ++pending.entries[bucket];
            if(depth){++errors.nested;poisoned=true;}
            if(depth==max_depth){++errors.overflow;poisoned=true;return;}
            tokens[depth++]=token;
            if(depth==1){caller=bucket;opened=now;}
            if(!now)poisoned=true;
            return;
        }
        if(site!=1||!depth||tokens[depth-1]!=token){
            ++errors.mismatch;poisoned=true;abandon();return;
        }
        --depth;
        if(!now)poisoned=true;
        if(depth)return;
        if(now&&opened&&now<opened){++errors.clock_reversal;poisoned=true;}
        if(!poisoned&&opened){pending.ticks[caller]+=now-opened;++pending.calls[caller];}
        opened=0;
        // Poison lasts to the frame boundary: even a superficially matching
        // exit after stack reuse/nonlocal unwind cannot rescue this frame.
    }
    void discard() noexcept { abandon();poisoned=false;pending={}; }
    // Failure invalidates the whole frame, including earlier complete calls.
    bool take(std::uint64_t frame,std::uint64_t frequency,Sample& out) noexcept {
        const bool valid=!poisoned&&!depth;
        out=valid?pending:Sample{};out.frame=frame;out.valid=valid;
        for(unsigned i=0;i<buckets;++i)out.us[i]=frequency?out.ticks[i]*1000000ull/frequency:0;
        out.self_us=std::uint64_t(out.stamps)*dispatch_cost_ns/1000;
        discard();return valid;
    }
};
struct Summary {
    std::uint64_t frame=0,stamps_p50=0,stamps_p95=0,self_p50=0,self_p95=0;
    std::uint64_t entries[buckets]{},calls[buckets]{},ticks[buckets]{};
    std::uint64_t calls_p50[buckets]{},calls_p95[buckets]{},us_p50[buckets]{},us_p95[buckets]{};
    unsigned frames=0,valid_frames=0,invalid_frames=0;
};
// Keep the shared 300-boundary cadence, but never put invalid or partial
// frames into timing/count/self-cost quantiles. Zero valid_frames means no
// timing evidence; its zero-valued percentiles are not a zero-time result.
class Window {
    Sample values[window_frames]{};
    std::uint64_t scratch[window_frames]{},column[window_frames]{};
    unsigned count_=0,valid_count_=0;
    std::uint64_t last_frame_=0;
    template<class F> std::uint64_t percentile(unsigned p,F get) noexcept {
        for(unsigned i=0;i<valid_count_;++i)column[i]=get(values[i]);
        return x3m::stamp::percentile(column,valid_count_,p,scratch);
    }
public:
    bool full() const noexcept{return count_==window_frames;}
    void reset() noexcept{count_=valid_count_=0;last_frame_=0;}
    void add(const Sample& sample) noexcept{
        if(full())return;
        ++count_;last_frame_=sample.frame;
        if(sample.valid)values[valid_count_++]=sample;
    }
    bool close(Summary& out) noexcept {
        if(!count_)return false;
        out={};out.frames=count_;out.frame=last_frame_;
        out.valid_frames=valid_count_;out.invalid_frames=count_-valid_count_;
        out.stamps_p50=percentile(50,[](const Sample& s){return s.stamps;});
        out.stamps_p95=percentile(95,[](const Sample& s){return s.stamps;});
        out.self_p50=percentile(50,[](const Sample& s){return s.self_us;});
        out.self_p95=percentile(95,[](const Sample& s){return s.self_us;});
        for(unsigned b=0;b<buckets;++b){
            for(unsigned i=0;i<valid_count_;++i){out.entries[b]+=values[i].entries[b];out.calls[b]+=values[i].calls[b];out.ticks[b]+=values[i].ticks[b];}
            out.calls_p50[b]=percentile(50,[b](const Sample& s){return s.calls[b];});
            out.calls_p95[b]=percentile(95,[b](const Sample& s){return s.calls[b];});
            out.us_p50[b]=percentile(50,[b](const Sample& s){return s.us[b];});
            out.us_p95[b]=percentile(95,[b](const Sample& s){return s.us[b];});
        }
        reset();return true;
    }
};
}
