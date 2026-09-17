// Host check of the FPS overlay accumulator: window arithmetic, ms rounding,
// refresh cadence, toggle and Reset semantics, no allocation. Pure ticks;
// production header included intact.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "../../src/proxy/fps_overlay.h"
static unsigned checks=0,failures=0,allocations=0;
void* operator new(std::size_t size){++allocations;if(void*p=std::malloc(size))return p;throw std::bad_alloc();}
void operator delete(void*p) noexcept{std::free(p);}
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line=%u %s\n",__LINE__,#x);}}while(0)
static constexpr std::uint64_t hz=1000000; // a 1 MHz QPC: one tick per microsecond
int main(){
    const auto before_allocations=allocations;
    char text[x3m::FpsOverlay::line_capacity]{};
    x3m::FpsOverlay::format(text,sizeof text,61.28,16.66,638);CHECK(!std::strcmp(text,"FPS 61.3  16.7 MS  DRAWS 638"));
    x3m::FpsOverlay::format(text,sizeof text,60.0,16.6666,0);CHECK(!std::strcmp(text,"FPS 60.0  16.7 MS  DRAWS 0"));
    x3m::FpsOverlay::format(text,sizeof text,-1.0,-5.0,1);CHECK(!std::strcmp(text,"FPS 0.0  0.0 MS  DRAWS 1"));
    x3m::FpsOverlay::format(text,sizeof text,1e9,1e9,1u<<31);CHECK(!std::strcmp(text,"FPS 9999.9  9999.9 MS  DRAWS 999999"));
    CHECK(std::strlen(text)<=36); // the notice's column count
    x3m::FpsOverlay::format(text,sizeof text,0.0/0.0,0.0/0.0,3);CHECK(!std::strcmp(text,"FPS 0.0  0.0 MS  DRAWS 3"));
    x3m::FpsOverlay overlay;
    CHECK(!overlay.requested()&&!overlay.visible()&&!overlay.toggle()&&!overlay.visible()); // unrequested: the key is inert
    overlay.configure(false,hz);CHECK(!overlay.visible());
    for(unsigned i=0;i<3000;++i)CHECK(!overlay.frame(std::uint64_t(i)*16667,600)); // never refreshes while unrequested
    CHECK(overlay.line()[0]=='\0');
    overlay.configure(true,hz);CHECK(overlay.requested()&&overlay.visible()&&overlay.line()[0]=='\0');
    // Steady 60 fps (16.667 ms), 600 draws: the first refresh at the first 250 ms bucket close.
    std::uint64_t now=1000, refreshes=0, first_refresh_frame=0;
    for(unsigned i=0;i<600;++i){now+=16667;if(overlay.frame(now,600)){++refreshes;if(!first_refresh_frame)first_refresh_frame=i;}}
    CHECK(first_refresh_frame==15); // frame 0 primes; the 15th interval (250.005 ms) closes the bucket
    CHECK(refreshes==39); // 599 intervals / 15 per bucket: ~4 refreshes per second
    CHECK(!std::strcmp(overlay.line(),"FPS 60.0  16.7 MS  DRAWS 600"));
    // A sliding window: 1 s of 30 fps after 10 s of 60 fps reads 30 fps within ~1.25 s.
    unsigned frames_to_30=0;bool saw_blend=false;
    for(unsigned i=0;i<120;++i){now+=33333;if(overlay.frame(now,300)){const double fps=std::atof(overlay.line()+4);if(fps>30.5&&fps<60.0)saw_blend=true;if(!frames_to_30&&fps<30.5)frames_to_30=i+1;}}
    CHECK(saw_blend&&frames_to_30>0&&frames_to_30<=38); // 38 frames at 33.3 ms = 1.27 s: the last 60-fps bucket has left the window
    CHECK(!std::strcmp(overlay.line(),"FPS 30.0  33.3 MS  DRAWS 300"));
    // Draws average over the window, rounded to nearest.
    for(unsigned i=0;i<120;++i){now+=33333;overlay.frame(now,i%2?301:300);}
    CHECK(!std::strncmp(overlay.line(),"FPS 30.0  33.3 MS  DRAWS 30",27));
    // Toggle off then on: a fresh window, so the hidden span never enters the interval.
    CHECK(!overlay.toggle()&&!overlay.visible());
    CHECK(overlay.toggle()&&overlay.visible()&&overlay.line()[0]=='\0');
    now+=5*hz; // five hidden seconds
    CHECK(!overlay.frame(now,10)); // primes only
    refreshes=0;for(unsigned i=0;i<60;++i){now+=16667;if(overlay.frame(now,10))++refreshes;}
    CHECK(refreshes==4&&!std::strcmp(overlay.line(),"FPS 60.0  16.7 MS  DRAWS 10")); // intervals 15, 30, 45, 60
    // Device Reset: window and text cleared, the overlay stays visible.
    overlay.reset();CHECK(overlay.visible()&&overlay.line()[0]=='\0');
    now+=hz;CHECK(!overlay.frame(now,1));
    for(unsigned i=0;i<14;++i){now+=16667;CHECK(!overlay.frame(now,1));}
    now+=16667;CHECK(overlay.frame(now,1)&&!std::strcmp(overlay.line(),"FPS 60.0  16.7 MS  DRAWS 1"));
    // A clock that does not advance never divides by zero and keeps the text.
    for(unsigned i=0;i<10;++i)CHECK(!overlay.frame(now,1));
    CHECK(!std::strcmp(overlay.line(),"FPS 60.0  16.7 MS  DRAWS 1"));
    // A stamp behind the previous one counts as a zero interval, not a wrap.
    CHECK(!overlay.frame(now-1,1));
    // One long stall (a load) dominates the window for at most one second after it.
    now+=3*hz;overlay.frame(now,1);
    CHECK(std::atof(overlay.line()+4)<10.0);
    for(unsigned i=0;i<90;++i){now+=16667;overlay.frame(now,1);}
    CHECK(!std::strcmp(overlay.line(),"FPS 60.0  16.7 MS  DRAWS 1"));
    // The second line's state: the first shown frame writes it, a change
    // rewrites it the same frame, an unchanged state is one compare.
    CHECK(overlay.shadows(-1));CHECK(!overlay.shadows(-1));CHECK(overlay.shadows(1));
    for(unsigned i=0;i<100;++i)CHECK(!overlay.shadows(1));
    CHECK(overlay.shadows(0)&&!overlay.shadows(0)&&overlay.shadows(1));
    overlay.reset();CHECK(overlay.shadows(1)); // Reset forgets the written state
    CHECK(!overlay.toggle()&&overlay.toggle()&&overlay.shadows(1)); // so does showing again
    // A draw failure keeps the mode on and reports once per episode; success
    // closes the episode, Reset and re-show clear it.
    CHECK(!overlay.draw_outcome(false)&&!overlay.draw_failed());
    CHECK(overlay.draw_outcome(true)&&overlay.draw_failed()&&overlay.visible());
    for(unsigned i=0;i<100;++i)CHECK(!overlay.draw_outcome(true)&&overlay.visible()); // retried, not re-logged
    CHECK(!overlay.draw_outcome(false)&&!overlay.draw_failed());
    CHECK(overlay.draw_outcome(true)); // a new episode after a success logs again
    overlay.reset();CHECK(!overlay.draw_failed()&&overlay.draw_outcome(true));
    CHECK(!overlay.toggle()&&overlay.toggle()&&!overlay.draw_failed());
    CHECK(allocations==before_allocations);
    std::printf("fps_overlay checks=%u failures=%u allocations=%u\n",checks,failures,allocations-before_allocations);
    return failures?1:0;
}
