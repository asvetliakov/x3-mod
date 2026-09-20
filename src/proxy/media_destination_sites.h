#pragma once
#include "media_presentation_gate.h"
#include <cstdint>
namespace x3m::media_destination {
enum class SiteId : unsigned {table_initial_loader,table_allocate,table_destroy,slot_name_replace,table_grow,slot_materialize,wrapper_release,wrapper_cleanup,wrapper_surface_replace,whole_reset,forced_clear,recovery_clear,slot_publish_new,slot_publish_loaded,record_bind_matched,record_unbind_matched,record_inline_bind,restore_matched_search,reset_root_surface_publications,count};
constexpr unsigned site_count=unsigned(SiteId::count),lifecycle_count=12;
struct SiteSpec {std::uint32_t address,continuation;unsigned length,rel32;unsigned char bytes[16];};
// Qualified whole instructions; only first five bytes are atomically replaced.
inline constexpr SiteSpec sites[site_count]={
 {0x4f44a0,0x4f44a7,7,0,{0x6a,0xff,0x68,0x70,0x3,0x53,0x0}},
 {0x4f48f0,0x4f48f7,7,0,{0xf,0xbf,0xd,0xb4,0x69,0x60,0x0}},
 {0x4f4990,0x4f499a,10,0,{0x53,0x55,0x56,0x57,0x8b,0x3d,0xac,0x69,0x60,0x0}},
 {0x4f4bb0,0x4f4bb9,9,5,{0x66,0x85,0xc0,0xf,0x8c,0x8b,0x0,0x0,0x0}},
 {0x4f4cb0,0x4f4cb6,6,0,{0x64,0xa1,0x0,0x0,0x0,0x0}},
 {0x4f4160,0x4f4165,5,0,{0x83,0xec,0xc,0x53,0x55}},
 {0x4f38d0,0x4f38d6,6,0,{0x53,0x55,0x8b,0x6c,0x24,0xc}},
 {0x4dcc70,0x4dcc75,5,0,{0x85,0xf6,0x57,0x75,0x5}},
 {0x4dced0,0x4dced6,6,0,{0x55,0x8b,0xec,0x83,0xe4,0xf8}},
 {0x4da960,0x4da966,6,2,{0x56,0xe8,0x2a,0xb8,0xfe,0xff}},
 {0x4f4b30,0x4f4b38,8,4,{0x51,0x56,0x57,0xe8,0x38,0x54,0xfc,0xff}},
 {0x4f5200,0x4f5208,8,0,{0x51,0x83,0x3d,0xac,0x69,0x60,0x0,0x0}},
 {0x4f4320,0x4f4327,7,0,{0x83,0xc4,0xc,0x89,0x44,0xa,0x8}},
 {0x4f4471,0x4f447a,9,5,{0x89,0x44,0xb,0x8,0xe8,0xe6,0x9e,0xff,0xff}},
 {0x498570,0x49857b,11,0,{0x8b,0x4c,0x24,0x4,0x83,0x48,0x2c,0x4,0x89,0x48,0x30}},
 {0x4985c1,0x4985ce,13,0,{0x83,0xe1,0xfb,0xc7,0x40,0x30,0x0,0x0,0x0,0x0,0x89,0x48,0x2c}},
 {0x4f6639,0x4f6644,11,0,{0x83,0x48,0x2c,0x4,0x8b,0x4c,0x24,0x20,0x89,0x50,0x30}},
 {0x498bd0,0x498bd5,5,0,{0x39,0x70,0x10,0x74,0xa}},
 {0x4daa39,0x4daa48,15,0,{0x89,0x51,0x30,0x8b,0x40,0x4,0x8b,0xd,0x2c,0x8b,0x60,0x0,0x89,0x48,0x30}},
};
struct Frame {
    unsigned char xmm[128];std::uint32_t edi,esi,ebp,saved_esp,ebx,edx,ecx,eax,eflags,target;
    std::uint32_t input_esp() const noexcept {return saved_esp+8;}
};
static_assert(sizeof(Frame)==168&&offsetof(Frame,target)==164);
struct Routes {std::uint32_t forward[site_count]{},after[lifecycle_count]{};};
struct Memory {virtual ~Memory()=default;virtual bool read(std::uint32_t,void*,unsigned) noexcept=0;virtual bool write(std::uint32_t,const void*,unsigned) noexcept=0;};
struct BindingLookup {void* consumer=nullptr;bool(*find)(void*,std::uint32_t,media::SessionHandle&,media_playback::EngineKey&) noexcept=nullptr;};
class Observer {
public:
    Observer(Destination& d,Memory& m,const Routes& r,BindingLookup b) noexcept:destination_(d),memory_(m),routes_(r),binding_(b){}
    void dispatch(unsigned,Frame&,std::uint32_t thread) noexcept;
private:
    struct Return {State::Token token{};std::uint32_t stack=0,continuation=0,key=0,argument=0;unsigned site=0;std::uint64_t table=0,lifetime=0;};
    Destination& destination_;Memory& memory_;const Routes& routes_;BindingLookup binding_;
    Return returns_[State::max_depth]{};unsigned depth_=0;
    // Only accessed while the domain is held across an instruction-only replay.
    std::uint32_t first_wrapper_=0,first_surface_=0;
    bool read(std::uint32_t p,std::uint32_t& v) noexcept {return memory_.read(p,&v,4);}
    void enter(unsigned,Frame&,std::uint32_t) noexcept;
    void leave(unsigned,Frame&,std::uint32_t) noexcept;
    void publication(unsigned,Frame&,bool,std::uint32_t) noexcept;
};
struct Patch {
    std::uint32_t code=0,word_address=0,protection=0;unsigned code_size=0;
    unsigned char original[8]{},replacement[8]{};
    bool ready=false,may_redirect=false,flush_debt=false,protection_debt=false;
    bool emission_flush_debt=false,emission_protection_debt=false;
};
struct Group {Patch patches[site_count]{};Routes routes{};bool installed=false,ever_published=false;const char* status="empty";};
using Platform=media_presentation_gate::Platform;
bool encode_stub(unsigned char*,unsigned,std::uint32_t,unsigned,std::uint32_t,Routes&,unsigned&) noexcept;
bool stage(Platform&,Group&,std::uint32_t) noexcept;
bool install(Platform&,Group&) noexcept;
bool restore(Platform&,Group&,bool quiescent,bool no_live_copy) noexcept;
#ifdef _WIN32
bool bind_dispatcher(Observer*) noexcept;
std::uint32_t dispatcher_address() noexcept;
class NativeMemory final:public Memory {public:bool read(std::uint32_t,void*,unsigned) noexcept override;bool write(std::uint32_t,const void*,unsigned) noexcept override;};
#endif
}
