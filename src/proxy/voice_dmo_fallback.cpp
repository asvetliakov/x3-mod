#include "voice_dmo_fallback.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "cpu_state.h"
#include "object_trace.h"
#include "capture.h"
#include <objbase.h>
#include <atomic>

static_assert(sizeof(void*)==4,"Reviewed x86 game ABI only");
namespace x3m::voice_dmo_fallback {
namespace {
// Documented GUIDs (dmodshow.h / dmoreg.h / wmcodecdsp.h are not in this MinGW).
constexpr GUID kIID_IDMOWrapperFilter={0x52d6f586,0x9f0f,0x4824,{0x8f,0xc8,0xe3,0x2c,0xa0,0x49,0x30,0xc2}};
constexpr GUID kCLSID_CWMADecMediaObject={0x2eeb4adf,0x4578,0x4d10,{0xbc,0xa7,0xbb,0x95,0x5f,0x56,0x32,0x0a}};
constexpr GUID kDMOCATEGORY_AUDIO_DECODER={0x57f2db8b,0xe6bb,0x4513,{0x9d,0x43,0xdc,0xd2,0xa6,0x59,0x31,0x25}};
struct IDMOWrapperFilterLocal : public IUnknown { virtual HRESULT STDMETHODCALLTYPE Init(REFCLSID clsid,REFCLSID category)=0; };
constexpr std::uint32_t kClassNotRegistered=0x80040154; // REGDB_E_CLASSNOTREG
constexpr std::uint32_t kDecoderSlot=0x9c;               // media object +0x9c: the wrapper's IBaseFilter (section 12)
// 004cfd44 `call edx` (IDMOWrapperFilter::Init, vtable+0x0c); the site is the
// return: `mov esi,eax` / `cmp esi,0x8007000e`, eight bytes, no relative branch.
// EAX holds Init's HRESULT and EBX the media object; POPAD restores EAX from the
// frame, so a successful retry is delivered to the game as Init's own result.
constexpr engine_patch::SiteSpec kSite={"voice_dmo_init_after",0x004cfd46,{0x8b,0xf0,0x81,0xfe,0x0e,0x00,0x07,0x80},8,0,0};
struct Record { std::uint32_t object,filter,qi_hr,init_hr; };
constexpr unsigned kRing=8;
std::atomic<bool> active{false};
bool initialized=false;
engine_patch::Site patch;
std::atomic<std::uint32_t> hits{0},activations{0},retries_ok{0},retries_failed{0},skipped{0},written{0};
Record ring[kRing];
std::uint32_t reported=0;
struct ErrorGuard { DWORD value=GetLastError();~ErrorGuard(){SetLastError(value);} };
// Main thread only (the constructor runs there, startup note section 1); no
// formatting, no allocation: integers into a ring that report() prints.
void handle(std::uint32_t* regs) noexcept {
    if(!active.load(std::memory_order_acquire))return;
    hits.fetch_add(1,std::memory_order_relaxed);
    if(regs[7]!=kClassNotRegistered)return; // PUSHAD: EAX is the last pushed
    activations.fetch_add(1,std::memory_order_relaxed);
    Record r{regs[4],0,0,0}; // EBX = media object
    IUnknown* filter=nullptr;
    if(r.object>UINT32_MAX-kDecoderSlot-4||!engine_memory::read(r.object+kDecoderSlot,&filter,sizeof filter)||!filter){skipped.fetch_add(1,std::memory_order_relaxed);return;}
    r.filter=reinterpret_cast<std::uint32_t>(filter);
    IDMOWrapperFilterLocal* view=nullptr;
    r.qi_hr=std::uint32_t(filter->QueryInterface(kIID_IDMOWrapperFilter,reinterpret_cast<void**>(&view)));
    if(FAILED(HRESULT(r.qi_hr))||!view)skipped.fetch_add(1,std::memory_order_relaxed);
    else {
        r.init_hr=std::uint32_t(view->Init(kCLSID_CWMADecMediaObject,kDMOCATEGORY_AUDIO_DECODER));
        view->Release();
        if(SUCCEEDED(HRESULT(r.init_hr))){retries_ok.fetch_add(1,std::memory_order_relaxed);regs[7]=r.init_hr;}
        else retries_failed.fetch_add(1,std::memory_order_relaxed); // EAX untouched: the game retries Init as it does today
    }
    const std::uint32_t n=written.load(std::memory_order_relaxed);
    ring[n%kRing]=r;written.store(n+1,std::memory_order_release);
}
}
}

extern "C" __attribute__((force_align_arg_pointer)) void __cdecl
x3m_voice_dmo_fallback_enter(std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr=0x1f80;asm volatile("ldmxcsr %0" :: "m"(mxcsr):"memory");
    x3m::voice_dmo_fallback::handle(regs);
}
namespace x3m::voice_dmo_fallback {
namespace {
// Same frame as the game-phase stubs: PUSHFD, PUSHAD, CLD, XMM0-7 saved, the
// PUSHAD frame pointer as the one argument, then everything restored and
// `jmp [next]` into the displaced instructions.
void* emit(void*** next_out) {
    engine_patch::Emitter e(192);if(!e.ok())return nullptr;void* start=e.here();
    e.byte(0x9c);e.byte(0x60);e.byte(0xfc);
    e.byte(0x81);e.byte(0xec);e.dword(0x80);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x11);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x8d);e.byte(0x84);e.byte(0x24);e.dword(0x80);e.byte(0x50);
    e.byte(0xe8);e.rel32(reinterpret_cast<const void*>(&x3m_voice_dmo_fallback_enter));
    e.byte(0x83);e.byte(0xc4);e.byte(4);
    for(unsigned i=0;i<8;++i){e.byte(0x0f);e.byte(0x10);e.byte(static_cast<unsigned char>(0x44|(i<<3)));e.byte(0x24);e.byte(static_cast<unsigned char>(i*16));}
    e.byte(0x81);e.byte(0xc4);e.dword(0x80);e.byte(0x61);e.byte(0x9d);
    const auto next=(reinterpret_cast<std::uintptr_t>(e.here())+9)&~std::uintptr_t(3);
    e.byte(0xff);e.byte(0x25);e.dword(std::uint32_t(next));
    while(e.ok()&&reinterpret_cast<std::uintptr_t>(e.here())<next)e.byte(0xcc);
    *next_out=reinterpret_cast<void**>(next);e.dword(0);return e.finish()?start:nullptr;
}
bool install(const char*& status) {
    if(!engine_patch::install_window_open()){status="install_window_closed";return false;}
    if(!object_trace::executable_verified()){status="executable_unverified";return false;}
    if(!engine_patch::verify_bytes(kSite.address,kSite.expected,kSite.length)){status="preflight_bytes";return false;}
    if(!engine_patch::claim(patch,kSite)){status=patch.status;return false;}
    void** next=nullptr;void* stub=emit(&next);
    if(stub&&next&&engine_patch::store_pointer(next,*patch.entry)&&engine_patch::push_front(patch,stub)){status="ok";return true;}
    status=engine_patch::restore(patch)?"stub_chain_failed":"rollback_failed_inert";
    return false;
}
}
bool initialize() {
    ErrorGuard error;
    if(initialized)return active.load(std::memory_order_acquire);
    initialized=true;wchar_t value[4]{};
    if(GetEnvironmentVariableW(L"X3M_VOICE_DMO_FALLBACK",value,4)!=1||value[0]!=L'1')return false;
    const char* status="unknown";
    const bool installed=install(status);
    active.store(installed,std::memory_order_release);
    log("voice_dmo_fallback requested=1 installed=%u status=%s site=%08lx length=%u patched=%u site_status=%s condition=%08lx retry_clsid=2eeb4adf-4578-4d10-bca7-bb955f56320a",
        unsigned(installed),status,static_cast<unsigned long>(kSite.address),kSite.length,unsigned(patch.patched_in),patch.status,static_cast<unsigned long>(kClassNotRegistered));
    return installed;
}
void report() {
    if(!active.load(std::memory_order_acquire))return;
    const std::uint32_t n=written.load(std::memory_order_acquire);
    if(n==reported)return;
    ErrorGuard error;
    if(n-reported>kRing){log("voice_dmo_fallback_overflow lost=%u",n-reported-kRing);reported=n-kRing;}
    for(;reported!=n;++reported){
        const Record& r=ring[reported%kRing];
        log("voice_dmo_fallback activation=%u object=%08lx filter=%08lx qi_hr=%08lx init_hr=%08lx hits=%u activations=%u retries_ok=%u retries_failed=%u skipped=%u",
            reported+1,static_cast<unsigned long>(r.object),static_cast<unsigned long>(r.filter),static_cast<unsigned long>(r.qi_hr),static_cast<unsigned long>(r.init_hr),
            hits.load(std::memory_order_relaxed),activations.load(std::memory_order_relaxed),retries_ok.load(std::memory_order_relaxed),retries_failed.load(std::memory_order_relaxed),skipped.load(std::memory_order_relaxed));
    }
}
}
