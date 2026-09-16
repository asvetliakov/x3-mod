#pragma once
#include <cstdint>

// Value-only state for one proved main-loop thread. The runtime owns thread
// admission and the atomic lifecycle epoch; this core never calls native code.
namespace x3m::game_phases::detail {
constexpr unsigned phase_count=14, tape_capacity=96, stack_capacity=8, call_count=8;
struct Stamp {
    std::uint64_t qpc=0,user=0,kernel=0,query_end=0;
    bool cpu=false;
    std::uint64_t query_begin=0;
};
struct Request { std::uint32_t cockpit=0,target=0,mode=0; };
struct Pump {std::uint32_t active=0,flags=0;bool valid=false;};
struct Segment {
    Stamp begin{},end{};
    std::uint64_t loop=0;
    unsigned phase=0,input_part=0;
    Request request_begin{},request_end{};
    Pump pump{};
};
struct Present {
    std::uint64_t device=0,reset=0,frame=0,qpc=0;
    std::uintptr_t raw=0;
    std::uint32_t result=0;
    bool captured=false;
};
struct Frame {
    Present previous{},current{};
    std::uint64_t dispatch_begin=0,dispatch_end=0,covered=0;
    Segment segments[tape_capacity]{};
    unsigned used=0;
    bool overflow=false;
};
struct Witness {
    std::uint32_t caller=0,previous_target=0,previous_mode=0,view=0,args[6]{};
    std::uintptr_t end_esp=0;
};
struct Call {
    unsigned kind=0;
    Stamp begin{},end{};
    Request request{};
    Present present{};
    std::uint32_t result=0;
    Witness witness{};
    std::uint64_t loop=0,children=0;
    unsigned phase=0;
    bool first=false;
};
template<class T> struct Window {
    T first[4]{},recent[4]{};
    unsigned first_used=0,recent_used=0,next=0;
    std::uint64_t count=0;
    void add(const T& v) noexcept {
        ++count;
        if(first_used<4)first[first_used++]=v;
        else {recent[next]=v;next=(next+1)%4;if(recent_used<4)++recent_used;}
    }
    void clear() noexcept {first_used=recent_used=next=0;count=0;}
};
struct Metric {
    std::uint64_t count=0,total=0,maximum=0,begin=0,end=0,cpu_valid=0,user=0,kernel=0;
    void add(const Stamp& a,const Stamp& b) noexcept {
        if(!a.qpc||b.qpc<a.qpc)return;
        ++count;const auto elapsed=b.qpc-a.qpc;total+=elapsed;
        if(elapsed>maximum){maximum=elapsed;begin=a.qpc;end=b.qpc;}
        if(a.cpu&&b.cpu&&b.user>=a.user&&b.kernel>=a.kernel){++cpu_valid;user+=b.user-a.user;kernel+=b.kernel-a.kernel;}
    }
};
struct Token {
    unsigned kind=0;
    Stamp begin{};
    Request request{};
    std::uintptr_t raw=0;
    Present endpoint{};
    Frame staged{};
    bool bridged=false,detail=false;
    Witness witness{};
    std::uint64_t loop=0,children=0;
    unsigned phase=0;
};
struct Core {
    std::uint64_t frequency=0,loop=0,invalidated=0,unmatched=0,overflow=0,order_errors=0,clock_errors=0;
    std::uint64_t frame_count=0,frame_max=0,frame_max_begin=0,frame_max_end=0,frame_max_id=0,frame_max_device=0;
    Metric phases[phase_count]{},calls[call_count]{},input_parts[3]{};
    std::uint64_t ignored_joins=0;
    unsigned first_mask=0,pending_first=0,input_part=0;
    Call first_calls[4]{};
    Window<Frame> slow_frames;
    Window<Call> slow_calls;
    Segment tape[tape_capacity]{};
    Token stack[stack_capacity]{};
    unsigned used=0,depth=0,phase=0;
    bool phase_live=false,tape_overflow=false,anchor_valid=false;
    Stamp phase_begin{};
    Request request{},phase_request{};
    Pump pump{};
    Present anchor{};
    // The Input phase (6) of the loop in progress and of the last loop that
    // completed it, in ticks: the loop-phase group joins the latter to its
    // per-frame sample (X3M_LOOP_PHASES with X3M_GAME_PHASES).
    std::uint64_t input_ticks=0,input_last=0;
    bool input_valid=false;

    void invalidate() noexcept {
        ++invalidated;
        phase_live=anchor_valid=false;used=depth=0;tape_overflow=false;
        anchor={};request={};pump={};input_part=0;input_ticks=0;input_valid=false;
        for(auto& t:stack){t.raw=0;t.bridged=t.detail=false;t.endpoint={};}
    }
    bool valid(const Stamp& at) noexcept {
        if(at.qpc)return true;
        ++clock_errors;invalidate();return false;
    }
    void segment(const Stamp& at) noexcept {
        if(!phase_live)return;
        if(at.qpc<phase_begin.qpc){++clock_errors;invalidate();return;}
        phases[phase].add(phase_begin,at);
        if(phase==6){input_parts[input_part].add(phase_begin,at);input_ticks+=at.qpc-phase_begin.qpc;}
        if(anchor_valid&&at.qpc>phase_begin.qpc){
            if(used<tape_capacity)tape[used++]={phase_begin,at,loop,phase,input_part,phase_request,request,pump};
            else {++overflow;tape_overflow=true;}
        }
        phase_begin=at;phase_request=request;
    }
    void boundary(unsigned index,const Stamp& at) noexcept {
        if(!valid(at))return;
        if(index==14){invalidate();return;}
        if(index>=phase_count)return;
        if(index==0){
            if(depth||(phase_live&&phase!=13)){++order_errors;invalidate();}
            segment(at);pump={};++loop;phase=0;phase_begin=at;phase_request=request;phase_live=true;return;
        }
        if(!phase_live){++unmatched;return;}
        if(index!=phase+1){++order_errors;invalidate();return;}
        segment(at);if(!phase_live)return;
        if(phase==6){input_last=input_ticks;input_valid=true;}
        phase=index;input_part=0;input_ticks=0;phase_begin=at;phase_request=request;
    }
    void begin(unsigned kind,const Stamp& at,Request r={},std::uintptr_t raw=0) noexcept {
        if(!valid(at))return;
        if(kind>=call_count){++unmatched;invalidate();return;}
        if(!phase_live){++unmatched;return;}
        if(depth==stack_capacity){++overflow;invalidate();return;}
        auto& t=stack[depth++];t.kind=kind;t.begin=at;t.request=r;t.raw=raw;
        t.bridged=t.detail=false;t.endpoint={};t.witness={};t.loop=loop;t.phase=phase;t.children=0;
        if(kind<2)request=r;
    }
    // Called only with owned Device metadata, before ctx.frame is advanced.
    void bridge(const Present& p,Stamp sample={}) noexcept {
        if(!depth||stack[depth-1].kind!=3||stack[depth-1].raw!=p.raw||!p.raw||!p.device||!p.qpc){++unmatched;return;}
        auto& t=stack[depth-1];
        if(t.bridged||p.qpc<t.begin.qpc||(anchor_valid&&p.qpc<anchor.qpc)){++order_errors;invalidate();return;}
        t.endpoint=p;t.bridged=true;
        if(anchor_valid&&(anchor.device!=p.device||anchor.reset!=p.reset||anchor.raw!=p.raw)){
            // A new device can start a new tape, but cannot finish the old one.
            anchor_valid=false;used=0;tape_overflow=false;++unmatched;
        }
        if(!phase_live){++unmatched;return;}
        // CPU sampling follows the owned endpoint slightly. Preserve the exact
        // frame QPC while retaining the real query bracket as its uncertainty.
        if(sample.qpc<p.qpc)sample.cpu=false;
        sample.qpc=p.qpc;
        segment(sample);if(!phase_live)return;
        if(anchor_valid){
            if(p.frame!=anchor.frame+1){++order_errors;used=0;tape_overflow=true;}
            else {
                const auto elapsed=p.qpc-anchor.qpc;++frame_count;
                if(elapsed>frame_max){frame_max=elapsed;frame_max_begin=anchor.qpc;frame_max_end=p.qpc;frame_max_id=p.frame;frame_max_device=p.device;}
                if(frequency&&elapsed>=frequency/20){
                    auto& f=t.staged;f.previous=anchor;f.current=p;f.used=used;f.overflow=tape_overflow;
                    f.dispatch_begin=t.begin.qpc;f.dispatch_end=0;f.covered=0;
                    for(unsigned i=0;i<used;++i){f.segments[i]=tape[i];f.covered+=tape[i].end.qpc-tape[i].begin.qpc;}
                    t.detail=true;
                }
            }
        }
        anchor=p;anchor_valid=true;used=0;tape_overflow=false;
    }
    void end(unsigned kind,const Stamp& at,std::uint32_t result=0) noexcept {
        if(!valid(at))return;
        if(!depth||stack[depth-1].kind!=kind){++unmatched;invalidate();return;}
        auto& t=stack[depth-1];
        if(at.qpc<t.begin.qpc){++clock_errors;invalidate();return;}
        if(kind==3&&(!t.bridged||result!=t.endpoint.result||at.qpc<t.endpoint.qpc)){
            ++unmatched;invalidate();return;
        }
        calls[kind].add(t.begin,at);
        const bool first=kind>=4&&!(first_mask&(1u<<kind));
        const Call record{kind,t.begin,at,t.request,t.endpoint,result,t.witness,t.loop,t.children,t.phase,false};
        if(first){first_mask|=1u<<kind;pending_first|=1u<<kind;first_calls[kind-4]=record;first_calls[kind-4].first=true;}
        if(frequency&&at.qpc-t.begin.qpc>=frequency/100)slow_calls.add(record);
        if(depth>1)stack[depth-2].children+=at.qpc-t.begin.qpc;
        if(t.detail){t.staged.dispatch_end=at.qpc;slow_frames.add(t.staged);}
        t.raw=0;t.bridged=t.detail=false;--depth;
    }
    void input_boundary(unsigned part,const Stamp& at) noexcept {
        if(!valid(at))return;
        if(!phase_live||phase!=6||part!=input_part+1||part>2){++order_errors;invalidate();return;}
        segment(at);if(phase_live)input_part=part;
    }
    bool targeted() const noexcept {return phase_live&&(phase==4||phase==6||phase==8);}
    bool within(unsigned kind) const noexcept {
        for(unsigned i=0;i<depth;++i)if(stack[i].kind==kind)return true;
        return false;
    }
    Request request_for(unsigned kind) const noexcept {
        for(unsigned i=depth;i-->0;)if(stack[i].kind==kind)return stack[i].request;
        return {};
    }
    void target_begin(unsigned kind,const Stamp& at,Request r,Witness w) noexcept {
        if(!targeted()||kind<4||kind>=call_count)return;
        const auto previous=depth;begin(kind,at,r);
        if(depth!=previous+1)return;
        auto& t=stack[depth-1];t.witness=w;t.endpoint=anchor_valid?anchor:Present{};
        if(kind==4)request=r;
    }
    void target_end(unsigned kind,const Stamp& at,std::uintptr_t esp,std::uint32_t result) noexcept {
        // Shared native joins have legitimate untracked arrivals, including a
        // nested mode2/null call while a mode3 publisher is open.
        if(!depth||stack[depth-1].kind!=kind||stack[depth-1].witness.end_esp!=esp){++ignored_joins;return;}
        end(kind,at,result);
    }
    void clear_window() noexcept {
        for(auto& m:phases)m={};
        for(auto& m:calls)m={};
        for(auto& m:input_parts)m={};
        ignored_joins=0;pending_first=0;
        slow_frames.clear();slow_calls.clear();frame_count=frame_max=frame_max_begin=frame_max_end=frame_max_id=frame_max_device=0;
        invalidated=unmatched=overflow=order_errors=clock_errors=0;
    }
};
// Loading-phase markers derived from Present cadence alone; no engine site.
// Splash frames present about a second apart, the menu load and the save load
// stop Present for several seconds (docs/reverse-engineering/loading-observations.md,
// "Phase markers"). The first stall at or above the threshold ends with the menu
// shown; the next one is the save (or new-game) load, whose begin marker is the
// last Present before it and is therefore reported late, with its own stamp.
// Gaps across a device change or an in-place Reset (reset generation, which
// the device id does not track) are not stalls. One caller thread (Present path).
struct LoadingPhases {
    enum Name : unsigned { MenuShown=0, SaveLoadBegin=1, SaveLoadComplete=2, NameCount=3 };
    struct Marker { unsigned name=0; std::uint64_t frame=0,qpc=0,stall=0; };
    std::uint64_t stall_ticks=0,last_device=0,last_reset=0,last_frame=0,last_qpc=0;
    unsigned emitted=0; // bit per Name; each marker is written once per process
    bool have_last=false;
    // Returns the number of markers written to out (capacity 2), oldest first.
    unsigned present(std::uint64_t device,std::uint64_t reset,std::uint64_t frame,std::uint64_t qpc,Marker* out) noexcept {
        unsigned n=0;
        if(have_last&&device==last_device&&reset==last_reset&&qpc>=last_qpc&&stall_ticks&&qpc-last_qpc>=stall_ticks){
            const auto gap=qpc-last_qpc;
            if(!(emitted&(1u<<MenuShown))){out[n++]={MenuShown,frame,qpc,gap};emitted|=1u<<MenuShown;}
            else if(!(emitted&(1u<<SaveLoadBegin))){
                out[n++]={SaveLoadBegin,last_frame,last_qpc,0};
                out[n++]={SaveLoadComplete,frame,qpc,gap};
                emitted|=(1u<<SaveLoadBegin)|(1u<<SaveLoadComplete);
            }
        }
        last_device=device;last_reset=reset;last_frame=frame;last_qpc=qpc;have_last=true;
        return n;
    }
};
}
