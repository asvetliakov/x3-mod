// Included by game_phase_cpu_fixture.cpp: actual gate/emitter ABI qualification.
// This fixture uses authored allocator bodies, never game code or a decoder.
#include <initializer_list>
static DWORD WINAPI media_skip_foreign_thread(LPVOID target){
    const auto caller=reinterpret_cast<std::uint32_t(*)()>(target);
    SetLastError(0x24681357);
    for(unsigned i=0;i<1000;++i)if(caller()!=0||GetLastError()!=0x24681357)return 1;
    return 0;
}
static void media_skip_checks(){
    MediaBody r=make_media_body();check(r.body!=nullptr,"ID2 skip allocator emitted");if(!r.body)return;
    patch::SiteSpec specs[media_marker::Count];if(!media_specs(r,specs))return;
    unsigned char original[64];std::memcpy(original,r.body,r.length);
    void* callers[]={r.selector,r.other,r.speech,r.script,r.query,r.savegame};
    Snapshot failed[6]{},passed[6]{},after{};
    media_fixture_id=2;media_fixture_flags=0;
    for(unsigned i=0;i<6;++i){
        media_fixture_result=0;invoke(callers[i],failed[i]);
        media_fixture_result=media_record;invoke(callers[i],passed[i]);
    }
    const char* status=nullptr;
    check(media_log_open(),"ID2 skip stand-in log opened");
    char lines[8192];media_log_take(lines,sizeof lines);
    // Both default-only and trace/cache configurations must suppress before
    // owner admission. The gate remains stateless on owner and foreign calls.
    for(unsigned diagnostics=0;diagnostics<2;++diagnostics){
        check(media::fixture_install(specs,r.addresses,diagnostics,diagnostics,1,&status),"ID2 skip installed independently of trace and cache");
        check(bool(media::active)==bool(diagnostics),"default skip has no per-frame diagnostic work");
        const auto* gate=media::fixture_gate();const auto* pending=media::fixture_pending();
        media_fixture_id=2;media_fixture_result=media_record;media_reset();
        for(std::uint32_t flags : {0u,8u}){
            media_fixture_flags=flags;
            for(unsigned i=0;i<6;++i){invoke(callers[i],after);compare(failed[i],after);}
        }
        check(media_body_calls==0,"ID2 skipped for all callers before owner admission");
        check(gate->owner.load()==0&&gate->early.load()==0&&gate->foreign.load()==0&&pending->depth==0&&pending->max_depth==0&&pending->last_return==0&&media::fixture_attempts_frame()==0&&media::fixture_refused()==0&&media::fixture_cache()->used==0,"ID2 skip touches no diagnostic state");
        check(media_log_take(lines,sizeof lines)==0,"ID2 skip writes no trace even with diagnostics enabled");
        media::frame(1);
        // Six caller forms, both default/explicit flags, four concurrent real
        // threads each: read-only gate traffic, no shared diagnostic writes.
        for(std::uint32_t flags : {0u,8u})for(void* target : callers){
            media_fixture_flags=flags;
            HANDLE threads[4]{};
            for(auto& thread:threads){thread=CreateThread(nullptr,0,&media_skip_foreign_thread,target,0,nullptr);check(thread!=nullptr,"ID2 foreign worker started");}
            for(HANDLE thread:threads)if(thread){
                WaitForSingleObject(thread,INFINITE);DWORD code=1;GetExitCodeThread(thread,&code);CloseHandle(thread);
                check(code==0,"ID2 foreign result and LastError preserved");
            }
        }
        check(media_body_calls==0&&pending->depth==0&&pending->max_depth==0&&pending->last_return==0&&gate->foreign.load()==0&&media::fixture_attempts_frame()==0,"ID2 foreign calls never enter allocator");
        media_fixture_id=2;
        for(std::uint32_t flags : {0u,8u}){
            media_fixture_flags=flags;
            for(unsigned i=0;i<6;++i){invoke(callers[i],after);compare(failed[i],after);}
        }
        check(media_body_calls==0&&pending->depth==0&&pending->max_depth==0&&pending->last_return==0,"admitted owner ID2 calls have no return observer");
        // Explicit nonvideo overrides of ID2, and other speech/music/movie IDs,
        // still execute the allocator exactly once with the original flags.
        for(std::uint32_t flags : {1u,9u,0x10u,0x90u,0x110u,0xffffffffu}){
            media_fixture_flags=flags;
            for(unsigned i=0;i<6;++i){
                media_reset();invoke(callers[i],after);compare(passed[i],after);
                check(media_body_calls==1&&media_body_arg==2&&media_body_flags==flags&&media_body_ebx_ok==1,"ID2 non8 override passes native allocator unchanged");
            }
        }
        for(std::uint32_t id : {1u,3u,144u,244u,800u,810u,811u,812u,2004u,8404u,8509u,10001u})for(std::uint32_t flags : {0u,8u,0x90u,0x110u}){
            media_fixture_id=id;media_fixture_flags=flags;media_reset();invoke(r.speech,after);compare(passed[2],after);
            check(media_body_calls==1&&media_body_arg==id&&media_body_flags==flags,"other video speech and music IDs pass unchanged");
        }
        check(media::fixture_uninstall(),"ID2 skip restored after policy matrix");
        check(!std::memcmp(original,r.body,r.length),"skip rollback restores complete native span");
        media_fixture_id=2;media_fixture_flags=0;media_reset();invoke(r.selector,after);compare(passed[0],after);
        check(media_body_calls==1,"rollback restores native ID2 construction");
    }
    // The positive pass matrix intentionally wrote diagnostics. Consume those
    // lines before handing the shared log to the independent early-call test.
    media_log_take(lines,sizeof lines);
    // Outer allowed ID1 construction simulates COM reentry with ID2. The
    // nested skip must neither push nor pop the outer return observer.
    check(media::fixture_install(specs,r.addresses,false,true,1,&status),"nested ID2 fixture installed");
    media::frame(1);media_fixture_id=1;media_fixture_flags=0;media_fixture_result=media_record;
    media_reenter_call=reinterpret_cast<void(*)()>(r.speech);media_reenter=1;media_reenter_result=media_record;
    media_reset();invoke(r.selector,after);compare(passed[0],after);
    const auto* pending=media::fixture_pending();
    check(media_body_calls==1&&media_reenter_depth==1&&pending->max_depth==1&&pending->depth==0&&pending->lost==0&&pending->mismatched==0,"nested ID2 does not add a return observer");
    media::detail::Entry entry{};
    check(media::fixture_pop_trace(&entry)&&entry.id==1&&entry.result==media_record&&!media::fixture_pop_trace(&entry),"nested skip leaves only original outer outcome");
    media_reenter_call=nullptr;
    check(media::fixture_uninstall(),"nested ID2 fixture restored");
    // Repeated no-diagnostic calls never retry or access a clock/cache. Report
    // total call time for the real production stub, not a game FPS estimate.
    check(media::fixture_install(specs,r.addresses,false,false,1,&status),"default skip benchmark installed");
    constexpr unsigned loops=20000,trials=7;
    LARGE_INTEGER frequency{};check(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0,"ID2 benchmark clock available");
    const auto caller=reinterpret_cast<std::uint32_t(*)()>(r.selector);
    const auto timed=[&]{
        std::uint64_t best=~std::uint64_t(0);
        for(unsigned t=0;t<trials;++t){
            LARGE_INTEGER begin{},end{};QueryPerformanceCounter(&begin);
            for(unsigned i=0;i<loops;++i)caller();
            QueryPerformanceCounter(&end);const auto elapsed=std::uint64_t(end.QuadPart-begin.QuadPart);
            if(elapsed<best)best=elapsed;
        }
        return number(best)*1e9/number(std::uint64_t(frequency.QuadPart))/loops;
    };
    media_fixture_id=2;media_fixture_flags=0;media_reset();const double skip_ns=timed();
    check(media_body_calls==0&&media::fixture_pending()->max_depth==0&&media::fixture_attempts_frame()==0,"repeated default ID2 skip never retries construction");
    media_fixture_id=812;const double pass_ns=timed();
    check(media_body_calls==loops*trials&&media::fixture_pending()->max_depth==0,"default other IDs pass without return observation");
    check(media::fixture_uninstall(),"ID2 skip benchmark restored");
    std::printf("MEDIA CUE SKIP BENCH loops=%u trials=%u skip_ns_per_call=%.1f default_pass_ns_per_call=%.1f original_calls_on_skip=0 foreign_calls=96000\n",loops,trials,skip_ns,pass_ns);
}
