#include "../../src/ownership/buffer_lock_observation.h"
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
using x3m::ownership::BufferLockObservation;
unsigned checks=0;
void check(bool value){++checks;if(!value)std::abort();}
int main(){
    BufferLockObservation state;check(!state.quiet());state.allocation_id=1;check(state.quiet());
    std::mutex mutex;std::condition_variable cv;unsigned stage=0;
    std::thread native([&]{
        std::unique_lock<std::mutex> lock(mutex);
        state.begin_lock(12,24,0,7);stage=1;cv.notify_all();
        cv.wait(lock,[&]{return stage==2;}); // native-entry barrier
        state.revision=1;state.pending_locks=1;stage=3;cv.notify_all();
        cv.wait(lock,[&]{return stage==4;}); // result publication barrier
        state.complete_lock(true);stage=5;cv.notify_all();
    });
    {
        std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[&]{return stage==1;});
        check(state.attempt_serial==1&&state.in_flight_locks==1&&!state.pending_locks&&!state.revision&&!state.quiet());
        check(state.last_offset==12&&state.last_size==24&&state.last_thread==7);
        stage=2;cv.notify_all();cv.wait(lock,[&]{return stage==3;});
        check(state.pending_locks==1&&state.revision==1&&state.in_flight_locks==1&&!state.quiet());
        // A second failed native call must not overwrite the first publication.
        state.begin_lock(0,0,0x10,9);state.complete_lock(false);
        check(state.attempt_serial==2&&state.failed_locks==1&&state.in_flight_locks==1&&state.revision==1);
        stage=4;cv.notify_all();cv.wait(lock,[&]{return stage==5;});
        check(state.pending_locks==1&&!state.in_flight_locks&&!state.quiet());
    }
    native.join();
    state.begin_unlock();check(state.unlock_serial==1&&state.in_flight_unlocks==1&&!state.quiet());
    state.complete_unlock(false);check(state.failed_unlocks==1&&state.pending_locks==1&&!state.in_flight_unlocks);
    state.begin_unlock();state.pending_locks=0;check(!state.quiet());state.complete_unlock(true);check(state.quiet());
    state.begin_lock(0,0,0x10,7);state.pending_locks=1;state.complete_lock(true);
    check(state.readonly_attempts==2&&state.writable_attempts==1&&state.revision==1);
    state.begin_unlock();state.pending_locks=0;state.complete_unlock(true);check(state.quiet()&&state.attempt_serial==3);
    for(unsigned flag:{0u,0x800u,0x2000u,0x1000u,0x3010u}){state.begin_lock(0,4,flag,1);state.complete_lock(false);}
    check(state.ordinary_attempts==3&&state.discard_attempts==2&&state.nooverwrite_attempts==2&&state.failed_locks==6);
    check(state.readonly_attempts==3&&state.writable_attempts==5);
    for(auto member:{&BufferLockObservation::attempt_serial,&BufferLockObservation::readonly_attempts,
        &BufferLockObservation::writable_attempts,&BufferLockObservation::ordinary_attempts,
        &BufferLockObservation::discard_attempts,&BufferLockObservation::nooverwrite_attempts}){
        BufferLockObservation s;s.allocation_id=1;s.*member=UINT64_MAX;
        s.begin_lock(0,0,member==&BufferLockObservation::readonly_attempts?0x10:
            member==&BufferLockObservation::discard_attempts?0x2000:
            member==&BufferLockObservation::nooverwrite_attempts?0x1000:0,0);
        s.complete_lock(true);check(s.*member==UINT64_MAX&&s.saturated&&!s.quiet());
    }
    state={};state.allocation_id=2;state.unlock_serial=UINT64_MAX;state.begin_unlock();state.complete_unlock(true);check(state.saturated&&!state.quiet());
    state={};state.failed_locks=UINT64_MAX;state.begin_lock(0,0,0,0);state.complete_lock(false);check(state.saturated&&state.failed_locks==UINT64_MAX);
    state={};state.failed_unlocks=UINT64_MAX;state.begin_unlock();state.complete_unlock(false);check(state.saturated&&state.failed_unlocks==UINT64_MAX);
    state={};state.in_flight_locks=UINT32_MAX;state.begin_lock(0,0,0,0);state.complete_lock(true);check(state.saturated);
    state={};state.in_flight_unlocks=UINT32_MAX;state.begin_unlock();state.complete_unlock(true);check(state.saturated);
    state={};state.complete_lock(true);check(state.ambiguous);state={};state.complete_unlock(true);check(state.ambiguous);
    std::printf("buffer_lock_observation checks=%u failures=0\n",checks);
}
