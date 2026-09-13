#pragma once
// Original synthetic x86-addressed memory for the production native-anchor
// reader. No game bytes, Win32 process, or backend required.
#include "../../src/proxy/chase_camera_native.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace chase_native_host {
struct Memory {
    std::array<unsigned char,0x6000> bytes{};
    std::uintptr_t denied = 0;
    bool read(std::uintptr_t address,void* out,std::size_t count) const {
        if (address > bytes.size() || count > bytes.size()-address || (denied && address <= denied && denied-address < count)) return false;
        std::memcpy(out,bytes.data()+address,count);return true;
    }
    void word(unsigned address,std::uint32_t value) {std::memcpy(bytes.data()+address,&value,4);}
    void position(unsigned address,x3m::chase::Vec3 value) {
        std::int32_t p[3];x3m::chase::to_int(value,p);std::memcpy(bytes.data()+address,p,sizeof p);
    }
    void basis(unsigned address,const x3m::chase::Mat3& value) {
        std::int32_t rows[12]{};x3m::chase::to_fixed(value,rows);std::memcpy(bytes.data()+address,rows,sizeof rows);
    }
    Memory() {
        word(0x1048,7);word(0x1050,0x4000);word(0x1054,0x2000);word(0x1070,0x3000);word(0x2048,1);
        position(0x3030,{10,20,30});position(0x30b0,{-700,40,50});
        basis(0x4870,x3m::chase::exp_rotation({0,0.1,0}));
        basis(0x3040,x3m::chase::exp_rotation({0,0.2,0}));
        basis(0x30c0,x3m::chase::exp_rotation({0,0.3,0}));
    }
};
inline void run() {
    using namespace x3m::chase;
    unsigned checks=0,failures=0;
    auto check=[&](bool okay){++checks;if(!okay)++failures;};
    Memory m;NativeAnchor a;
    auto read=[&](std::uintptr_t reference=0x1000){return read_native_anchor(reference,[&](std::uintptr_t p,void* o,std::size_t n){return m.read(p,o,n);},&a);};
    auto angle=[&](){return length(log_rotation(a.basis));};
    check(read()&&a.base_domain&&a.position.x==10&&a.render_position.x==-700&&a.basis_valid&&std::fabs(angle()-0.1)<0.0001);
    m.word(0x1048,8);check(read()&&a.base_domain&&a.position.x==10&&std::fabs(angle()-0.2)<0.0001);
    m.word(0x2048,2);check(read()&&!a.base_domain&&a.position.x==-700&&std::fabs(angle()-0.3)<0.0001);
    m.word(0x1054,0);check(read()&&!a.base_domain&&a.position.x==-700);
    check(!read(0x1001));check(!read(0));check(!read(0xffffffe0u));
    m=Memory{};m.word(0x1070,0x3001);check(!read());
    m=Memory{};m.word(0x1054,0x2001);check(!read());
    m=Memory{};m.word(0x1054,0x8000);check(!read());
    m=Memory{};m.word(0x1050,0);check(read()&&!a.basis_valid&&a.position.x==10);
    m=Memory{};m.word(0x1050,0xffffff00u);check(read()&&!a.basis_valid&&a.position.x==10);
    m=Memory{};m.denied=0x4870;check(read()&&!a.basis_valid&&a.position.x==10);
    m=Memory{};m.denied=0x30b0;check(read()&&!a.render_position_valid&&a.position.x==10);
    m=Memory{};m.denied=0x3030;a.position={123,456,789};check(!read()&&a.position.x==123&&a.position.y==456&&a.position.z==789);
    // Native camera/anchor translate steadily while render-ready coordinates
    // have an independent alternating phase. The old +b0 anchor spuriously
    // drives the boom spring; matching the native branch eliminates it. The
    // render-domain branch retains its original translation invariance too.
    double fixed_error=0,old_error=0,render_error=0;unsigned fixed_clamps=0;
    for(unsigned base=0;base<2;++base){
        m=Memory{};m.word(0x2048,base?1:2);m.basis(0x4870,Mat3{});m.basis(0x30c0,Mat3{});
        Tunables t;t.offset_y=0;t.pitch_down_deg=0;
        State fixed,old; // compare native anchor without elevated framing
        for(unsigned frame=0;frame<120;++frame){
            const Vec3 current{0,0,double(frame)*50},render=current-Vec3{0,0,frame%2?250.0:50.0};
            m.position(0x3030,current);m.position(0x30b0,render);
            if(!read()){++failures;break;}
            Input in;in.ship_pos=a.position;in.vanilla_pos=(base?current:render)+Vec3{0,0,-17300};
            in.view_mode=2;in.ref_object=0x1000;in.sector=1;Pose good,bad;
            const Step g=step(fixed,in,1.0/60,t,&good);in.ship_pos=a.render_position;
            const Step b=step(old,in,1.0/60,t,&bad);
            if(g.verdict!=Verdict::Applied||b.verdict!=Verdict::Applied){++failures;break;}
            const double error=length(good.pos-in.vanilla_pos);
            if(base){fixed_error=std::fmax(fixed_error,error);old_error=std::fmax(old_error,length(bad.pos-in.vanilla_pos));}
            else render_error=std::fmax(render_error,error);
        }
        fixed_clamps+=unsigned(fixed.clamps);
    }
    check(fixed_error<1e-9&&old_error>50&&render_error<1e-9&&fixed_clamps==0);
    std::printf("N %u %u %.17g %.17g %.17g %u\n",checks,failures,fixed_error,old_error,render_error,fixed_clamps);
}
}
