"""Host checks of the actual production publication and pixel-lease primitives."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
HOST=r'''
#include "lav_worker.h"
#include <thread>
#include <iostream>
#include <type_traits>
using namespace x3m::media;
static int checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<"line "<<__LINE__<<" failed: "<<#x<<"\n";return 1;}}while(0)
int main(){
 static_assert(!std::is_copy_constructible<FrameLease>::value,"lease aliases forbidden");
 static_assert(std::is_nothrow_move_constructible<FrameLease>::value,"lease transfer must not allocate");
 Command first{{1,0x1234567800000001ULL},9,10,{2,0,280,false},CommandKind::play,true};
 auto second=first;second.epoch=11;
 lav_detail::Mailbox<Command> mailbox;CHECK(mailbox.push(first));CHECK(!mailbox.push(second));
 lav_detail::Latest<Publication> cancellation;
 Publication desired{first.session,first.operation,second.epoch,true,false};cancellation.publish(desired);
 Publication read{};CHECK(cancellation.observe(read));CHECK(read.epoch==11&&!read.playing);CHECK(mailbox.occupied());
 Command popped{};CHECK(mailbox.pop(popped));CHECK(popped.epoch==10);CHECK(!mailbox.pop(popped));CHECK(mailbox.push(second));
 struct Whole {std::uint64_t a=0,b=0,c=0;};lav_detail::Latest<Whole> whole;std::atomic<bool> done{false};
 std::thread writer([&]{for(std::uint64_t i=1;i<=200000;++i)whole.publish({(i<<32)|i,~((i<<32)|i),i*0x100000001ULL});done.store(true,std::memory_order_release);});
 bool consistent=true;unsigned seen=0;Whole value{};
 do{if(whole.observe(value)){++seen;consistent=consistent&&value.b==~value.a&&value.c==value.a;}}while(!done.load(std::memory_order_acquire));
 if(whole.observe(value)){++seen;consistent=consistent&&value.b==~value.a&&value.c==value.a;}
 writer.join();CHECK(consistent);CHECK(seen>0);CHECK(value.a==200000ULL*0x100000001ULL);
 FrameIdentity id{first.session,first.operation,first.epoch};
 Publication active{first.session,first.operation,first.epoch,true,true};
 CHECK(lav_detail::admit_ready(WorkerState::ready,false,active,id));
 CHECK(!lav_detail::admit_ready(WorkerState::failed,false,active,id));
 CHECK(!lav_detail::admit_ready(WorkerState::unsafe_retained,false,active,id));
 CHECK(!lav_detail::admit_ready(WorkerState::ready,true,active,id));
 CHECK(!lav_detail::admit_ready(WorkerState::retired,false,active,id));
 auto stale=id;++stale.epoch;CHECK(!lav_detail::admit_ready(WorkerState::ready,false,active,stale));
 auto pool=std::make_shared<lav_detail::FrameStorage>(nullptr);std::weak_ptr<lav_detail::FrameStorage> weak=pool;
 for(unsigned i=0;i<3;++i){CHECK(pool->reserve(i,id,1,i));pool->slots[i].pixels[0]=static_cast<unsigned char>(i+31);pool->slots[i].view.start=i*400000;pool->slots[i].view.end=(i+1)*400000;pool->publish(i);}
 CHECK(!pool->all_free());CHECK(!pool->reserve(0,id,1,3));
 FrameLease a=pool->acquire(pool,0),b=pool->acquire(pool,1),c=pool->acquire(pool,2);
 CHECK(a&&b&&c);CHECK(!pool->acquire(pool,0));CHECK(!pool->reserve(0,id,1,3));CHECK(a.view().identity==id);
 FrameLease moved=std::move(a);CHECK(!a&&moved);CHECK(moved.view().bgra[0]==31);
 CHECK(!lav_detail::admit_ready(WorkerState::unsafe_retained,true,active,id));CHECK(moved.view().bgra[0]==31);
 b.release(LeaseRelease::revoked);CHECK(pool->reserve(1,id,1,3));pool->abandon_retired(1);
 c.release();pool.reset();CHECK(!weak.expired());CHECK(moved.view().bgra[0]==31);moved.release();CHECK(weak.expired());
 lav_detail::TerminalPublication terminal;CHECK(terminal.reset(id,1));WorkerEvent event{};event.identity=id;event.graph=1;event.flags=worker_failed;event.status=-1;
 CHECK(terminal.update(event,false));CHECK(!terminal.ready_for_next());WorkerEvent out{};CHECK(terminal.poll(out));CHECK(out.flags==worker_failed);CHECK(!terminal.ready_for_next());
 event.flags=worker_graph_retired;event.status=0;event.graph_guard_safe=event.graph_released=event.events_clean=true;CHECK(terminal.update(event,true));
 FrameIdentity next{id.session,id.operation,id.epoch+1};CHECK(!terminal.reset(next,2));
 CHECK(terminal.poll(out));CHECK(out.flags==(worker_failed|worker_graph_retired));CHECK(out.status==-1);CHECK(out.graph_released);CHECK(terminal.ready_for_next());CHECK(terminal.reset(next,2));
 event.identity=id;CHECK(!terminal.update(event,true));event.identity=next;event.flags=worker_source_eof;event.status=0;CHECK(terminal.update(event,false));
 event.flags=worker_graph_retired;CHECK(terminal.update(event,true));event.flags=worker_service_retired;CHECK(terminal.update(event,true));CHECK(terminal.poll(out));
 CHECK(out.flags==(worker_source_eof|worker_graph_retired|worker_service_retired));CHECK(out.identity==next);CHECK(!terminal.poll(out));CHECK(terminal.ready_for_next());
 // Actual no-graph failure publication policy: it must be final and remain
 // observable before a valid new command identity is allowed to replace it.
 lav_detail::TerminalPublication unresolved;CHECK(unresolved.reset(id,0));
 WorkerEvent missing{};missing.identity=id;missing.status=-2147024809;missing.graph_released=true;missing.graph_guard_safe=true;
 CHECK(unresolved.failure(missing));CHECK(!unresolved.ready_for_next());CHECK(!unresolved.reset(next,0));
 CHECK(unresolved.poll(out));CHECK(out.flags==(worker_failed|worker_graph_retired));CHECK(out.status==missing.status);
 CHECK(unresolved.ready_for_next());CHECK(unresolved.reset(next,1));
 // Actual quiescence predicate/publication, including cancellation before any
 // graph exists. Every physical/queue gate independently prevents the fact.
 lav_detail::AssignmentQuiescence quiet;Publication canceled=active;canceled.live=false;canceled.playing=false;
 CHECK(!quiet.poll(canceled,canceled,1));
 for(unsigned gate=0;gate<6;++gate){bool flags[6]={true,true,true,true,true,true};flags[gate]=false;
   CHECK(!quiet.publish(canceled,1,flags[0],flags[1],flags[2],flags[3],flags[4],flags[5]));
   CHECK(!quiet.poll(canceled,canceled,1));}
 CHECK(!quiet.publish(active,1,true,true,true,true,true,true));
 CHECK(quiet.publish(canceled,1,true,true,true,true,true,true));CHECK(quiet.poll(canceled,canceled,1));
 CHECK(!quiet.poll(canceled,active,2));CHECK(!quiet.poll(canceled,canceled,3)); // identical tuple, later publication
 auto foreign=canceled;++foreign.epoch;CHECK(!quiet.poll(foreign,canceled,1));
 auto wrong_playing=canceled;wrong_playing.playing=true;CHECK(!quiet.poll(wrong_playing,canceled,1));
 // A queued command (also representing the worker-local popped command), an
 // unacknowledged terminal, or a real held lease prevents a new idle fact.
 auto held_pool=std::make_shared<lav_detail::FrameStorage>(nullptr);
 CHECK(held_pool->reserve(0,id,1,0));held_pool->publish(0);auto held=held_pool->acquire(held_pool,0);
 CHECK(!quiet.publish(canceled,3,true,true,true,true,true,held_pool->all_free()));
 held.release();CHECK(quiet.publish(canceled,3,true,true,true,true,true,held_pool->all_free()));CHECK(quiet.poll(canceled,canceled,3));
 CHECK(!quiet.poll(canceled,canceled,0));
 std::cout<<checks<<" checks passed\n";return 0;
}
'''
class TransportPrimitives(unittest.TestCase):
    def test_actual_mailbox_cancellation_terminal_and_three_leases(self):
        with tempfile.TemporaryDirectory() as temporary:
            p=Path(temporary);(p/'host.cpp').write_text(HOST)
            subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pthread','-I',str(ROOT/'src/media'),str(p/'host.cpp'),'-o',str(p/'host')],check=True,capture_output=True)
            result=subprocess.run([str(p/'host')],check=True,capture_output=True,text=True)
            self.assertIn('checks passed',result.stdout)

if __name__=='__main__':unittest.main()
