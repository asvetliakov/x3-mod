// Included inside namespace x3m::media_engine. Portable encoder used verbatim by
// the native adapter and ABI fixture, not a separately modeled assembler.
namespace {
struct Emitter {
    unsigned char* bytes;unsigned capacity,used=0;std::uint32_t base;bool good=true;unsigned default_field=0;
    void byte(unsigned value) noexcept {if(used<capacity)bytes[used++]=static_cast<unsigned char>(value);else good=false;}
    void word(std::uint32_t value) noexcept {for(unsigned i=0;i<4;++i)byte(value>>(i*8));}
    void rel(unsigned opcode,std::uint32_t target) noexcept {byte(opcode);word(target-(base+used+4));}
    void xmm(bool load) noexcept {for(unsigned i=0;i<8;++i){byte(0x0f);byte(load?0x10:0x11);byte(0x44|(i<<3));byte(0x24);byte(i*16);}}
    void envelope(unsigned id,std::uint32_t helper) noexcept {
        // LEA reserves the branch-target slot without changing input EFLAGS.
        byte(0x8d);byte(0x64);byte(0x24);byte(0xfc);byte(0x9c);byte(0x60);
        byte(0x81);byte(0xec);word(128);xmm(false);byte(0xfc);
        byte(0x68);word(id);byte(0x8d);byte(0x44);byte(0x24);byte(4);byte(0x50);
        // Safe original replay remains the default even before context binding.
        // At this point two helper args are pushed, so target is ESP+172.
        byte(0xc7);byte(0x84);byte(0x24);word(172);default_field=used;word(0);
        rel(0xe8,helper);byte(0x83);byte(0xc4);byte(8);
        xmm(true);byte(0x81);byte(0xc4);word(128);byte(0x61);byte(0x9d);byte(0xc3);
    }
    void drop(unsigned n,std::uint32_t target) noexcept {byte(0x8d);byte(0x64);byte(0x24);byte(n);rel(0xe9,target);}
};
}
bool encode_stub(unsigned char* out,unsigned capacity,std::uint32_t base,unsigned index,
                 std::uint32_t helper,Routes& routes,unsigned& size) noexcept {
    if(!out||!base||!helper||index>=site_count||capacity<2048||base>UINT32_MAX-capacity)return false;
    Emitter e{out,capacity,0,base,true};e.envelope(index,helper);
    const auto entry_default=e.default_field;
    routes.forward[index]=base+e.used;
    const auto& s=sites[index];
    unsigned guard=0;while(guard<return_count&&guarded_sites[guard]!=static_cast<SiteId>(index))++guard;
    const auto replay=[&](std::uint32_t target){
        if(s.original_call){e.rel(0xe9,s.original_call);return;}
        const auto start=e.used;
        for(unsigned i=0;i<s.length;++i)e.byte(s.bytes[i]);
        if(s.rel32){
            std::uint32_t old=0;std::memcpy(&old,s.bytes+s.rel32,4);
            const auto adjusted=s.address+s.rel32+4+old-(base+start+s.rel32+4);
            std::memcpy(out+start+s.rel32,&adjusted,4);
        }
        e.rel(0xe9,target);
    };
    replay(guard<return_count?routes.after[guard]:s.continuation);
    routes.unobserved[index]=routes.forward[index];
    if(!s.original_call&&guard<return_count){
        routes.unobserved[index]=base+e.used;replay(s.continuation);
    }
    // Before binding, and for foreign never-owned identities, exact original
    // replay cannot enter an after-handler without a corresponding owner token.
    std::memcpy(out+entry_default,&routes.unobserved[index],4);
    if(index==0){
        routes.return_plain=base+e.used;e.byte(0xc3);
        routes.manager_drop4=base+e.used;e.drop(4,0x4984be);
        routes.manager_drop8=base+e.used;e.drop(8,0x4984be);
        routes.manager_next8=base+e.used;e.drop(8,0x4984b5);
        routes.manager_error8=base+e.used;e.drop(8,0x498473);
        routes.speech_drop4=base+e.used;e.drop(4,0x498fd2);
        routes.destroy_free=base+e.used;
        e.byte(0x56);e.byte(0x57);e.byte(0x8b);e.byte(0xf0);e.byte(0x33);e.byte(0xff);
        e.rel(0xe9,0x4d1dd3);
        for(unsigned i=0;i<return_count;++i){
            routes.after[i]=base+e.used;e.envelope(unsigned(SiteId::pump_return)+i,helper);
            const std::uint32_t fallback=i==10?0x498dd8u:i==9?0x498fd2u:i==1?routes.manager_drop4:
                (i==0||(i>=5&&i<=8))?0x4984beu:0x498362u;
            std::memcpy(out+e.default_field,&fallback,4);
        }
    }
    size=e.used;return e.good;
}
