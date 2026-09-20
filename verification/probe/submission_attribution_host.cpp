#include "../../src/proxy/frame_phases_core.h"
#include "../../src/proxy/pass_phases_core.h"
#include "../../src/proxy/residual_phases_core.h"
#include "../../src/renderer/readback_timing.h"
#include <cstdio>
#include <cstdlib>
#include <new>
static bool refuse_allocation = false;
void* operator new(std::size_t n) { if (refuse_allocation) std::abort(); if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
static unsigned checks = 0;
static void check(bool value) { ++checks; if (!value) { std::fprintf(stderr,"check %u failed\n",checks); std::exit(1); } }
struct Run {
    x3m::frame_phases::detail::Tracker frame;
    x3m::pass_phases::detail::Accumulator pass;
    x3m::residual_phases::detail::Accumulator residual;
    void stamp(unsigned index, std::uint64_t now) { pass.stamp(index,now,frame.submission_ticks_at(now),frame.submit_begin); }
    void material(std::uint64_t now) { residual.material(now,pass.end_clock,pass.begin_clock,pass.begin_armed,frame.submission_ticks_at(now),pass.end_submission,pass.begin_submission,frame.submit_begin!=0); }
    void draw(std::uint64_t now) { for(unsigned i=0;i<4;++i) stamp(i,now+10*i); }
};
int main() {
    refuse_allocation = true;
    for (const auto gap : {100ull, 100000ull}) {
        Run r; r.frame.restart(1);
        r.frame.site(7,90); r.frame.site(8,100);
        r.material(110); r.draw(120); r.material(160); r.draw(170);
        r.frame.site(9,210);
        // An actual pass outside the main view is excluded from the same-frame
        // complement. Its setup and preparation still have exhaustive buckets.
        r.material(220); r.draw(230);
        r.frame.site(7,300+gap); r.frame.site(8,310+gap);
        r.material(320+gap); r.draw(330+gap); r.frame.site(9,370+gap);
        x3m::residual_phases::detail::Sample rs;
        r.residual.take(1,1000000,1000+gap,20,170,2,r.pass.passes,r.pass.begin_clock,r.pass.begin_armed,rs,r.pass.begin_submission);
        x3m::pass_phases::detail::Sample ps;
        r.pass.take(1,1000000,170,ps);
        check(rs.interval_us[0]==30); // independent of the long composite gap
        check(rs.interval_us[4]==60+gap && rs.interval_us[1]==30 && rs.interval_us[5]==10);
        check(ps.scoped_us==90 && ps.outside_us==30 && ps.sum_us==120 && ps.complement_us==80);
        check(ps.outside_passes==1 && ps.crossing_passes==0);
        check(r.residual.outside_materials==1 && r.residual.prepare_skipped==1 && !r.residual.setup_skipped);
        check(!r.pass.orphans && !r.pass.clock_errors && !r.pass.clock_failures && !r.pass.scope_errors && !r.pass.complement_underflow);
        check(!r.residual.scope_errors && !r.residual.clock_errors && !r.residual.clock_failures && !r.residual.other_underflow);
        check(!r.frame.order_errors && !r.frame.clock_errors && !r.frame.unmatched);
        check(!r.pass.end_clock && !r.pass.begin_clock && !r.pass.end_submission && !r.pass.scoped_ticks);
    }
    // A span can cross several complete views without a material in between.
    Run r; r.frame.restart(1); r.frame.site(7,10);r.frame.site(8,20);
    r.material(30);r.draw(40);r.frame.site(9,80);
    r.frame.site(7,90);r.frame.site(8,100);r.frame.site(9,120);
    r.frame.site(7,1000);r.frame.site(8,1010);r.material(1020);
    check(r.residual.ticks[0]==40 && r.residual.ticks[4]==910);
    // Crossing pass: explicitly counted and clipped at both boundaries.
    Run cross;cross.frame.restart(1);cross.frame.site(7,10);cross.frame.site(8,20);
    cross.stamp(0,30);cross.stamp(1,40);cross.frame.site(9,50);cross.stamp(2,60);cross.stamp(3,70);
    check(cross.pass.scoped_ticks==20 && cross.pass.outside_ticks==20 && cross.pass.crossing_passes==1);
    cross.pass.stamp(3,0);check(!cross.pass.end_clock && cross.pass.clock_failures==1);
    Run envelop;envelop.frame.restart(1);envelop.stamp(0,10);
    envelop.frame.site(7,20);envelop.frame.site(8,30);envelop.stamp(1,40);
    envelop.frame.site(9,50);envelop.stamp(2,60);envelop.stamp(3,70);
    check(envelop.pass.scoped_ticks==20 && envelop.pass.crossing_passes==1 && envelop.pass.outside_passes==0);
    // Reduction must use paired same-frame residuals, not marginal medians.
    x3m::pass_phases::detail::Window w;
    for(unsigned i=0;i<3;++i) { x3m::pass_phases::detail::Accumulator a; a.scoped_ticks=(i==1?100:10); x3m::pass_phases::detail::Sample s; a.take(i,1000000,i==0?100:110,s);w.add(s); }
    x3m::pass_phases::detail::Summary summary;check(w.close(summary));
    check(summary.attribution_p50[2]==90 && summary.view_submit_p50-summary.attribution_p50[0]==100);
    // All three boundaries run even after a transfer/lock/unlock failure: the
    // failed operation's bucket remains measured, later skipped phases retain
    // only boundary overhead. Total is exhaustive on each path.
    using T=x3m::renderer::ReadbackTiming;
    for(unsigned failure=0;failure<=T::Count;++failure) {
        T t;t.begin(true,100);std::uint64_t now=100;
        for(unsigned i=0;i<T::Count;++i) { now+=(i<=failure?10:1);t.end(T::Phase(i),now); }
        check(t.total()==now-100 && !t.clock_errors);
    }
    for(unsigned bad=0;bad<=T::Count;++bad) {
        T t;t.begin(true,bad==0?0:100);
        for(unsigned i=0;i<T::Count;++i)t.end(T::Phase(i),bad==i+1?0:110+10*i);
        check(t.clock_errors && !t.total());
    }
    T backward;backward.begin(true,100);backward.end(T::TransferLock,99);check(backward.clock_errors && !backward.total());
    T off;off.begin(false,0);for(unsigned i=0;i<T::Count;++i)off.end(T::Phase(i),0);check(!off.clock_errors&&!off.total());
    refuse_allocation = false;
    std::printf("submission_attribution_host checks=%u failures=0\n",checks);
}
