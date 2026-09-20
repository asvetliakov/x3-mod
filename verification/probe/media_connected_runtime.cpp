// Connected production runtime fixture. Engine records/continuations are authored;
// worker, clock, package reader, Consumer, native Destination and D3D are real.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include "../../src/proxy/media_root.h"
#include "../../src/proxy/media_presentation_gate_win32.h"
#include "../../src/ownership/d3d9_ownership.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
namespace e=x3m::media_engine;namespace p=x3m::media_playback;namespace m=x3m::media;
namespace d=x3m::media_destination;namespace s=x3m::media_services;namespace g=x3m::media_presentation_gate;
namespace st=x3m::media_startup;
namespace x3m::object_trace {bool executable_verified(){return true;}}
namespace x3m::engine_patch {bool install_window_open(){return true;}}
// Same audited image layout as the independent consumer/destination fixtures.
__attribute__((section(".x3map"),used)) unsigned char connected_map[0x21f000]{};
static unsigned checks=0,failures=0,pass=0,capture_count=0;
static std::uint64_t frequency=0,largest_pass=0;
static std::wstring output;
static std::atomic<bool> finished{false};
static std::atomic<DWORD> boundary_begin{0};
static std::uint64_t tick() noexcept {LARGE_INTEGER v{};QueryPerformanceCounter(&v);return std::uint64_t(v.QuadPart);}
struct CounterTrace {bool active=false;unsigned count=0;std::uint64_t values[8]{};} counter_trace;
static std::uint64_t counter(void*) noexcept{const auto value=tick();if(counter_trace.active){if(counter_trace.count<8)counter_trace.values[counter_trace.count]=value;++counter_trace.count;}return value;}
static unsigned addr(const void* p){return reinterpret_cast<unsigned>(p);}
static void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("CXR_FAIL label=%s pass=%u\n",label,pass);}}
static void need(bool value,const char* label){check(value,label);if(!value){std::fflush(stdout);ExitProcess(2);}}
static void event(const char* name,unsigned record=0,std::uint64_t a=0,std::uint64_t b=0){std::printf("CXR_EVENT name=%s pass=%u qpc=%llu record=%u a=%llu b=%llu\n",name,pass,(unsigned long long)tick(),record,(unsigned long long)a,(unsigned long long)b);}
static void messages(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
static DWORD WINAPI watchdog(void*){const DWORD begin=GetTickCount();while(!finished.load(std::memory_order_acquire)){
 const DWORD now=GetTickCount(),inside=boundary_begin.load(std::memory_order_acquire);
 if(DWORD(now-begin)>90000||(inside&&DWORD(now-inside)>10000)){std::fprintf(stderr,"CXR_TIMEOUT whole_or_call\n");std::fflush(nullptr);ExitProcess(3);}Sleep(10);}return 0;}
struct StartupPlatform final:st::Platform {
 st::Controller* controller=nullptr;HMODULE module=nullptr;HANDLE thread=nullptr;
 static DWORD WINAPI boot(void* p){auto& self=*static_cast<StartupPlatform*>(p);self.controller->bootstrap(self.module);return 0;}
 bool read_entry(std::uintptr_t,std::uint32_t(&out)[6]) noexcept override{const unsigned words[6]={0x4d8494,0x20,0,0,0,0x402ee1};std::memcpy(out,words,sizeof words);return true;}
 bool qualified() noexcept override{return true;}
 std::uint64_t now() noexcept override{return tick();}
 bool launch(st::Controller& c) noexcept override{controller=&c;thread=CreateThread(nullptr,0,boot,this,0,nullptr);return thread!=nullptr;}
};
struct EngineMemory final:e::Memory,d::Memory {
 unsigned allocations=0,frees=0;std::array<p::Shell32,2> shells{};bool used[2]{};
 bool read(unsigned a,void* out,unsigned n) noexcept override{if(!a||!out)return false;std::memcpy(out,reinterpret_cast<void*>(a),n);return true;}
 bool write(unsigned a,const void* in,unsigned n) noexcept override{if(!a||!in)return false;std::memcpy(reinterpret_cast<void*>(a),in,n);return true;}
 unsigned allocate_shell() noexcept override{for(unsigned i=0;i<2;++i)if(!used[i]){used[i]=true;++allocations;return addr(&shells[i]);}return 0;}
 void release_unpublished_shell(unsigned a) noexcept override{for(unsigned i=0;i<2;++i)if(a==addr(&shells[i])&&used[i]){used[i]=false;++frees;return;}}
};
struct Input {e::Frame frame{};unsigned args[24]{};Input(){frame.saved_esp=addr(args)-8;}};
static_assert(offsetof(Input,args)==sizeof(e::Frame));
struct Graphics {
 HMODULE module=nullptr;HWND window=nullptr;IDirect3D9* factory=nullptr;IDirect3DDevice9* device=nullptr;IDirect3DSurface9* surfaces[2]{};
 void setup(){module=LoadLibraryW(L"d3d9.dll");need(module,"native_d3d9");using Create=IDirect3D9*(WINAPI*)(UINT);Create create=nullptr;auto proc=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&proc,sizeof create);need(create,"factory_export");need(x3m::ownership::wrap_factory(create(D3D_SDK_VERSION),&factory)==S_OK,"canonical_factory");
 window=CreateWindowExW(0,L"STATIC",L"Connected media fixture",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);need(window,"owner_window");D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;
 need(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device)==S_OK,"canonical_device");
 for(unsigned i=0;i<2;++i){need(device->CreateOffscreenPlainSurface(512,512,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&surfaces[i],nullptr)==S_OK,"canonical_surface");D3DSURFACE_DESC desc{};need(surfaces[i]->GetDesc(&desc)==S_OK,"surface_descriptor");need(desc.Width==512&&desc.Height==512&&desc.Format==D3DFMT_A8R8G8B8&&desc.Pool==D3DPOOL_SYSTEMMEM,"surface_shape");std::printf("CXR_SURFACE index=%u key=%u device=%u width=%u height=%u format=%u pool=%u usage=%u\n",i,addr(surfaces[i]),addr(device),desc.Width,desc.Height,unsigned(desc.Format),unsigned(desc.Pool),unsigned(desc.Usage));}
 need(surfaces[0]!=surfaces[1],"distinct_native_destinations");}
};
struct Gate {
 g::Admission admission;g::Win32Platform platform;g::Transaction transaction;unsigned renderer[8]{},root=0,available=1;unsigned char* body=nullptr;
 void setup(){body=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));need(body,"gate_storage");root=addr(renderer);
 g::Site site{addr(body),addr(&root),addr(body)+5,addr(body)+128,addr(&available),{0xa1,0,0,0,0,0x8b,0x48,0x18},{0xc7,0x05,0,0,0,0,0,0,0,0,0xc3}};
 std::memcpy(site.entry_bytes+1,&site.renderer_word,4);std::memcpy(site.defer_bytes+2,&site.unavailable_word,4);
 std::memcpy(body,site.entry_bytes,8);body[8]=0xc3;std::memcpy(body+128,site.defer_bytes,11);
 need(FlushInstructionCache(GetCurrentProcess(),body,4096),"gate_flush");need(admission.qualify_owner(GetCurrentThreadId()),"gate_owner");
 need(g::stage(platform,transaction,site,admission)&&g::install(platform,transaction,admission)&&admission.enable(GetCurrentThreadId()),"gate_installed");}
};
struct ClockObservation {bool pending=false;const char* reason="";unsigned position=0,reads=0;std::uint64_t schedule=0,before=0,after=0,rate=0,generation=0,revision=0,counter[4]{};};
struct Record {
 p::Record32 bytes{};m::SessionHandle session{};p::EngineKey key{};
 unsigned name=0,lifetime=0,calls=0,slot=0;bool allocated=false,playing=false;std::uint64_t operation=0,epoch=0,last_sequence=0,writes=0,terminal_qpc=0,last_schedule=0;unsigned start_ms=0,last_position=0;bool advancing_logged=false;ClockObservation observation{};
};
struct World;
static e::OwnerDomain owner_domain(){e::OwnerDomain value{};need(e::query_native_owner(value),"native_owner_domain");return value;}
static bool lookup(void* consumer,unsigned record,m::SessionHandle& h,p::EngineKey& k) noexcept{return static_cast<e::Consumer*>(consumer)->binding_owner(record,h,k);}
struct World {
 EngineMemory memory;Graphics graphics;Gate gate;StartupPlatform startup_platform;st::Controller startup{startup_platform};p::Adapter adapter;
 d::NativeIdentitySource identity;d::Destination destination{gate.admission,identity,GetCurrentThreadId()};
 const e::OwnerDomain owner=owner_domain();
 x3m::media_root::Ingress ingress{destination,memory,owner,&e::native_execution_point};
 s::MediaServices services{adapter,destination,startup,{frequency,nullptr,counter}};
 e::Routes routes; e::Consumer consumer{adapter,ingress,services,routes};d::Routes destination_routes;
 d::Observer observer{destination,memory,destination_routes,{&consumer,lookup}};
 std::array<unsigned char,64> wrappers[2];std::array<unsigned char,16032> table{};
 Record a,b; e::Readiness readiness{};
 struct ContinuityObservation {const char* phase=nullptr;std::uint64_t qpc=0,operation=0,epoch=0,rate=0,generation=0,revision=0;unsigned position=0,assigned=0,draining=0;} continuity_rows[3];unsigned continuity_count=0;
 World(){a.name=1;a.slot=0;b.name=2;b.slot=1;}
 void encode(){for(unsigned i=0;i<e::site_count;++i){auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));need(code,"consumer_code");unsigned n=0;need(e::encode_stub(code,4096,addr(code),i,e::dispatcher_address(),routes,n),"consumer_encode");DWORD old=0;need(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)&&FlushInstructionCache(GetCurrentProcess(),code,n),"consumer_seal");}
 for(unsigned i=0;i<d::site_count;++i)destination_routes.forward[i]=d::sites[i].continuation; // handler-only authored observation boundary
 need(e::bind_dispatcher(&consumer),"consumer_owner_bind");need(consumer.bind_ingress(ingress),"actual_shared_record_ingress");}
 void table_publication(){
 // Real Observer lifecycle reads these engine globals. Only fixture-owned PE
 // image memory is ever written; preserve the original fixed ABI addresses.
 const unsigned globals=0x6069ac;MEMORY_BASIC_INFORMATION info{};need(VirtualQuery(reinterpret_cast<void*>(globals),&info,sizeof info)==sizeof info&&info.Type==MEM_IMAGE&&info.State==MEM_COMMIT&&info.AllocationBase==GetModuleHandleW(nullptr),"table_globals_owned_image");
 alignas(4) unsigned char storage[sizeof(d::Frame)+4+96]{};auto* before=new(storage)d::Frame{};before->saved_esp=addr(storage+sizeof(d::Frame))-8;
 const unsigned continuation=0x4f48f7;std::memcpy(storage+sizeof(d::Frame),&continuation,4);const auto site=unsigned(d::SiteId::table_allocate);destination_routes.after[site]=continuation;
 observer.dispatch(site,*before,GetCurrentThreadId());
 const unsigned key=addr(table.data());const short base=2,dynamic=0;std::memcpy(reinterpret_cast<void*>(globals),&key,4);std::memcpy(reinterpret_cast<void*>(0x6069b0),&base,2);std::memcpy(reinterpret_cast<void*>(0x6069b4),&dynamic,2);
 auto* after=new(storage+4)d::Frame{};after->saved_esp=addr(storage+4+sizeof(d::Frame))-8;observer.dispatch(2*d::site_count+site,*after,GetCurrentThreadId());need(after->target==continuation,"table_allocate_return");event("table_observed");}
 void setup(){graphics.setup();gate.setup();encode();destination.set_device_key(addr(graphics.device),GetCurrentThreadId());need(d::bind_reset_observer(&destination),"exclusive_reset_observer");
 table_publication();
 for(unsigned i=0;i<2;++i){wrappers[i].fill(0);const auto surface=addr(graphics.surfaces[i]);std::memcpy(wrappers[i].data()+0x30,&surface,4);d::Frame frame{};frame.ecx=addr(table.data());frame.edx=i*16;frame.eax=addr(wrappers[i].data());
 const auto site=unsigned(d::SiteId::slot_publish_new);observer.dispatch(site,frame,GetCurrentThreadId());
 std::memcpy(table.data()+i*16+8,&frame.eax,4); // authored displaced slot store under actual observer domain
 observer.dispatch(site+d::site_count,frame,GetCurrentThreadId());}
 need(destination.ready(),"native_destination_ready");
 // Readiness is scoped to this handler fixture, not game installation/SEH proof.
 readiness={true,true,true,true,true,true,true,true};
 need(!consumer.enable(readiness),"admission_before_workers_refused");
 need(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&counter),&startup_platform.module),"bootstrap_module_pin");
 need(startup.configure(&s::MediaServices::bootstrap,&services),"bootstrap_registered");st::Entry entry;startup.enter(entry,4);startup.leave(entry,1);event("bootstrap_scheduled");}
 void maintenance(){messages();const auto state=startup.snapshot().status;need(state!=st::Status::callback_failed&&state!=st::Status::launch_failed,"bootstrap_not_failed");boundary_begin.store(GetTickCount());services.maintenance_on_owner(readiness);boundary_begin.store(0);const auto diag=services.diagnostics();need(!diag.quarantined,"no_worker_quarantine");need(!gate.admission.depth(),"copy_depth_zero");}
 void bind(Record& r){d::Frame frame{};frame.eax=addr(&r.bytes);const auto site=unsigned(d::SiteId::record_bind_matched);
 observer.dispatch(site,frame,GetCurrentThreadId());
 // Exact displaced MOV/OR/store value effects, enclosed by production observer.
 frame.ecx=r.slot;r.bytes.flags|=4;r.bytes.slot=frame.ecx;
 observer.dispatch(site+d::site_count,frame,GetCurrentThreadId());
 need(r.bytes.slot==r.slot&&(r.bytes.flags&4),"record_bound_fields");}
 bool construct(Record& r){r.bytes={};r.bytes.flags=4;r.bytes.slot=0x3d;r.calls=0;r.playing=false;r.last_sequence=r.writes=0;r.terminal_qpc=0;r.advancing_logged=false;r.last_schedule=0;
 Input in;in.frame.esi=addr(&r.bytes);in.args[1]=2;in.args[2]=8;consumer.dispatch(e::SiteId::construct,in.frame);
 need(in.frame.target==routes.return_plain,"construct_owned_return");if(!in.frame.eax)return false;
 r.bytes.shell=in.frame.eax;r.bytes.source=2;r.bytes.flags=8;r.allocated=true;++r.lifetime;
 need(consumer.binding_owner(addr(&r.bytes),r.session,r.key),"binding_owner_live");bind(r);
 std::printf("CXR_RECORD name=%u lifetime=%u record=%u shell=%u slot=%u session_slot=%u session_generation=%llu key_generation=%llu\n",r.name,r.lifetime,addr(&r.bytes),r.bytes.shell,r.slot,r.session.slot,(unsigned long long)r.session.generation,(unsigned long long)r.key.generation);return true;}
 void rate(Record& r,unsigned value){Input in;in.frame.eax=addr(&r.bytes);in.args[1]=value;consumer.dispatch(e::SiteId::rate,in.frame);need(in.frame.target==0x498697&&in.frame.eax==0,"actual_rate_transaction");event("rate",r.name,value);}
 void play(Record& r,unsigned start,int end){Input in;in.frame.esi=addr(&r.bytes);p::PlayValues32 values{};values.callback_index=1;values.callback_context=r.name;values.source=2;values.start_minutes=start/60000;values.start_seconds=start/1000%60;values.start_milliseconds=start%1000;
 const unsigned finite=end>0?unsigned(end):0;values.end_minutes=finite/60000;values.end_seconds=finite/1000%60;values.end_milliseconds=finite%1000;std::memcpy(reinterpret_cast<unsigned char*>(in.args)+0x18,&values,sizeof values);
 consumer.dispatch(e::SiteId::explicit_play,in.frame);need(in.frame.target==0x498d7a,"play_commit_continuation");
 // Authored ordinary engine commit after the real transaction. No old callback
 // exists for these fresh lifetimes; replacement callback semantics are separate.
 need(!r.bytes.callback_index,"fresh_callback_slot");r.start_ms=start;r.bytes.start_ms=int(start);r.bytes.end_ms=end>0?end:-1;r.bytes.flags|=2;r.bytes.callback_context=r.name;r.bytes.callback_index=1;r.playing=true;
 m::Snapshot snapshot;need(adapter.snapshot(r.session,snapshot),"play_snapshot");r.operation=snapshot.publication.operation;r.epoch=snapshot.publication.epoch;
 std::printf("CXR_PLAY name=%u lifetime=%u operation=%llu epoch=%llu start=%u end=%d qpc=%llu\n",r.name,r.lifetime,(unsigned long long)r.operation,(unsigned long long)r.epoch,start,end,(unsigned long long)tick());}
 void complete(Record& r){
 // Manager's authored nonloop continuation owns callback delivery. The real
 // Consumer before/after guard receives its actual normalized stack locations.
 alignas(4) unsigned char storage[sizeof(e::Frame)+4+96]{};auto* before=new(storage)e::Frame{};before->saved_esp=addr(storage+sizeof(e::Frame))-8;before->edi=addr(&r.bytes);before->ecx=r.bytes.callback_context;
 consumer.dispatch(e::SiteId::end_callback,*before);need(before->target==routes.forward[unsigned(e::SiteId::end_callback)],"end_callback_guard_admitted");
 native_callback(r,1,"endpoint");
 auto* after=new(storage+4)e::Frame{};after->saved_esp=addr(storage+4+sizeof(e::Frame))-8;consumer.dispatch(e::SiteId::end_return,*after);need(after->target==0x498466,"end_callback_return_live");
 r.bytes.flags&=~2u;r.bytes.callback_context=0;r.bytes.callback_index=0;r.playing=false;r.terminal_qpc=tick();}
 static void native_callback(Record& r,unsigned status,const char* reason){++r.calls;std::printf("CXR_CALLBACK name=%u lifetime=%u operation=%llu epoch=%llu status=%u count=%u reason=%s qpc=%llu\n",r.name,r.lifetime,(unsigned long long)r.operation,(unsigned long long)r.epoch,status,r.calls,reason,(unsigned long long)tick());}
 void flush_clock(Record& r){auto& o=r.observation;if(!o.pending)return;o.pending=false;
 std::printf("CXR_CLOCK name=%u lifetime=%u reason=%s operation=%llu epoch=%llu position=%u schedule=%llu before=%llu after=%llu rate=%llu generation=%llu revision=%llu reads=%u c0=%llu c1=%llu c2=%llu c3=%llu\n",r.name,r.lifetime,o.reason,(unsigned long long)r.operation,(unsigned long long)r.epoch,o.position,(unsigned long long)o.schedule,(unsigned long long)o.before,(unsigned long long)o.after,(unsigned long long)o.rate,(unsigned long long)o.generation,(unsigned long long)o.revision,o.reads,(unsigned long long)o.counter[0],(unsigned long long)o.counter[1],(unsigned long long)o.counter[2],(unsigned long long)o.counter[3]);}
 bool pump(Record& r,bool defer_clock=false){if(!r.allocated||!r.playing)return false;Input in;const auto pump_begin=tick();counter_trace.count=0;counter_trace.active=true;in.frame.ecx=addr(&r.bytes);in.frame.eax=r.bytes.shell;boundary_begin.store(GetTickCount());consumer.dispatch(e::SiteId::pump,in.frame);counter_trace.active=false;const auto pump_end=tick();boundary_begin.store(0);need(counter_trace.count==(in.frame.eax==1?3u:4u),"actual_pump_counter_read_contract");// Build/runner audit the exact Consumer.publish -> Services.publish ->
 // schedule(now) -> optional terminal publish call shape. Runtime count/order
 // and public post-pump position are checked independently; End freezes the
 // scheduler position across the terminal fourth publish/Clock.stop.
 r.last_schedule=counter_trace.values[2];
 need(in.frame.target==routes.return_plain,"pump_live_return");need(in.frame.eax==1||in.frame.eax==2,"pump_no_runtime_failure");const auto diag=services.diagnostics();const unsigned worker=r.name==2?1:0;
 const auto sequence=diag.presented[worker];const bool wrote=sequence&&sequence!=r.last_sequence;if(wrote){r.last_sequence=sequence;++r.writes;}
 m::Snapshot snapshot;need(adapter.snapshot(r.session,snapshot),"pump_snapshot");need(snapshot.publication.operation==r.operation&&snapshot.publication.epoch==r.epoch,"pump_identity_stable");
 unsigned position=0;need(services.position(r.session,position),"actual_position_observed");r.last_position=position;
 const bool observed_write=wrote&&(r.name==2||r.lifetime==2||r.writes==1);
 const bool first_advance=!r.advancing_logged&&position>r.start_ms;if(first_advance)r.advancing_logged=true;
 if(observed_write||in.frame.eax==2||first_advance){auto& o=r.observation;need(!o.pending,"bounded_clock_observation");o.pending=true;o.reason=in.frame.eax==2?"terminal":observed_write?"selected":"advance";o.position=position;o.schedule=r.last_schedule;o.before=pump_begin;o.after=pump_end;o.rate=diag.rate_numerator[worker];o.generation=diag.clock_generation[worker];o.revision=diag.revision[worker];o.reads=counter_trace.count;for(unsigned i=0;i<4;++i)o.counter[i]=i<counter_trace.count?counter_trace.values[i]:0;}
 // B's observation is emitted by the caller only AFTER the first-write cancel.
 if(!defer_clock||in.frame.eax==2)flush_clock(r);
 if(in.frame.eax==2){need(r.bytes.callback_index==1,"engine_callback_present");complete(r);}return wrote;}
 void continuity(const char* phase){m::Snapshot snap;unsigned position=0;need(adapter.snapshot(b.session,snap)&&services.position(b.session,position),"B_continuity_snapshot");const auto diag=services.diagnostics();need(continuity_count<3,"bounded_continuity_observation");auto& o=continuity_rows[continuity_count++];o={phase,tick(),snap.publication.operation,snap.publication.epoch,diag.rate_numerator[1],diag.clock_generation[1],diag.revision[1],position,diag.assigned,diag.draining};}
 void flush_continuity(){for(unsigned i=0;i<continuity_count;++i){const auto& o=continuity_rows[i];std::printf("CXR_CONTINUITY phase=%s qpc=%llu operation=%llu epoch=%llu position=%u rate=%llu generation=%llu revision=%llu assigned=%u draining=%u\n",o.phase,(unsigned long long)o.qpc,(unsigned long long)o.operation,(unsigned long long)o.epoch,o.position,(unsigned long long)o.rate,(unsigned long long)o.generation,(unsigned long long)o.revision,o.assigned,o.draining);}continuity_count=0;}
 void retire(Record& r){need(r.allocated,"retire_allocated");Input in;in.frame.esi=addr(&r.bytes);consumer.dispatch(e::SiteId::retire_record,in.frame);m::SessionHandle ignored{};p::EngineKey ignored_key{};need(!consumer.binding_owner(addr(&r.bytes),ignored,ignored_key),"retired_binding_refused");
 // Original4984d0 notifies (context,1) after retirement ingress and before
 // clearing callback fields. Keep this cancellation separate from natural end.
 if(r.bytes.callback_index&&r.bytes.callback_context)native_callback(r,1,"retired");
 r.playing=false;r.bytes.flags&=~2u;r.bytes.callback_index=0;r.bytes.callback_context=0;
 Input destroy;destroy.frame.eax=r.bytes.shell;consumer.dispatch(e::SiteId::destroy_shell,destroy.frame);need(destroy.frame.target==routes.destroy_free,"owned_destructor_no_com");memory.release_unpublished_shell(r.bytes.shell);r.allocated=false;event("retire",r.name,r.lifetime,r.last_sequence);}
 void capture(Record& r,const char* why){need(capture_count<32,"bounded_readbacks");D3DLOCKED_RECT map{};boundary_begin.store(GetTickCount());const auto hr=graphics.surfaces[r.slot]->LockRect(&map,nullptr,D3DLOCK_READONLY);need(hr==S_OK&&map.pBits&&map.Pitch>=2048,"readback_lock");
 static std::array<unsigned char,512*512*4> pixels;for(unsigned y=0;y<512;++y)std::memcpy(pixels.data()+y*2048,static_cast<unsigned char*>(map.pBits)+y*map.Pitch,2048);const auto unlock=graphics.surfaces[r.slot]->UnlockRect();need(unlock==S_OK,"readback_unlock");boundary_begin.store(0);
 wchar_t name[48];std::swprintf(name,48,L"capture-%02u.bgra",capture_count);const auto path=output+L"/"+name;HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);need(file!=INVALID_HANDLE_VALUE,"capture_create_new");DWORD wrote=0;need(WriteFile(file,pixels.data(),DWORD(pixels.size()),&wrote,nullptr)&&wrote==pixels.size(),"capture_write");need(CloseHandle(file),"capture_close");
 std::printf("CXR_CAPTURE index=%u name=%u lifetime=%u sequence=%llu operation=%llu epoch=%llu reason=%s qpc=%llu pass=%u pitch=%d lock_hr=%ld unlock_hr=%ld position=%u schedule=%llu\n",capture_count++,r.name,r.lifetime,(unsigned long long)r.last_sequence,(unsigned long long)r.operation,(unsigned long long)r.epoch,why,(unsigned long long)tick(),pass,int(map.Pitch),long(hr),long(unlock),r.last_position,(unsigned long long)r.last_schedule);}
};
int wmain(int argc,wchar_t** argv){std::setvbuf(stdout,nullptr,_IONBF,0);if(argc!=3||std::wcscmp(argv[1],L"--output")){std::fprintf(stderr,"--output DIRECTORY required\n");return 2;}output=argv[2];need(GetFileAttributesW(output.c_str())!=INVALID_FILE_ATTRIBUTES,"output_directory");LARGE_INTEGER f{};need(QueryPerformanceFrequency(&f)&&f.QuadPart>0,"real_qpc");frequency=std::uint64_t(f.QuadPart);auto* world=new World;
 HANDLE watch=CreateThread(nullptr,0,watchdog,nullptr,0,nullptr);need(watch,"watchdog");world->setup();const auto begin=tick();std::printf("CXR_HEADER kind=media_connected_v1 frequency=%llu main_thread=%lu source=2 workers=2 slots_per_worker=3 copy=native clock=production engine=authored_handler_frames\n",(unsigned long long)frequency,GetCurrentThreadId());
 unsigned phase=0,hold_passes=0;std::uint64_t first_b=0,b_operation=0,b_epoch=0,old_key=0,old_session=0,hold_begin=0;bool b_progress=false,a2_started=false;auto previous=tick();
 while(phase<5&&tick()-begin<frequency*80){++pass;const auto now=tick();if(now-previous>largest_pass)largest_pass=now-previous;previous=now;world->maintenance();
 if(phase==0&&world->services.ready()){need(world->startup.snapshot().status==st::Status::ready,"startup_actual_ready");need(world->consumer.enable(world->readiness),"consumer_ready_enable");need(world->construct(world->a)&&world->construct(world->b),"two_owned_records");need(world->services.diagnostics().assigned==2,"two_assignments");world->rate(world->b,10000);world->play(world->a,0,-1);const auto ready=world->startup.snapshot();std::printf("CXR_STARTUP requested=%llu begin=%llu end=%llu ready=%llu\n",(unsigned long long)ready.requested_qpc,(unsigned long long)ready.bootstrap_begin_qpc,(unsigned long long)ready.bootstrap_end_qpc,(unsigned long long)ready.ready_qpc);event("ready");phase=1;}
 if(phase>=1&&phase<=3){const bool aw=world->pump(world->a);
 if(aw&&(world->a.lifetime==2||world->a.writes==1))world->capture(world->a,"selected");
 if(phase==1&&world->a.writes){world->play(world->b,10000,10359);phase=2;}
 const bool bw=world->pump(world->b,true);
 if(phase==2&&bw&&!first_b){first_b=world->b.last_sequence;b_operation=world->b.operation;b_epoch=world->b.epoch;need(world->b.playing,"overlap_still_active");old_key=world->a.key.generation;old_session=world->a.session.generation;
 // No diagnostic readback or file I/O between B's first real write and A cancel.
 world->continuity("before_cancel");world->retire(world->a);world->continuity("after_cancel");need(world->services.diagnostics().draining==1,"exact_cancellation_draining");event("overlap_cancel",2,first_b,b_operation);
 need(!world->construct(world->a),"reuse_while_draining_refused");world->continuity("after_refusal");world->flush_continuity();event("draining_refusal",1);phase=3;}
 world->flush_clock(world->b);
 if(bw){if(first_b&&world->b.last_sequence>first_b){b_progress=true;need(world->b.operation==b_operation&&world->b.epoch==b_epoch,"B_progress_after_A_cancel");}world->capture(world->b,"selected");}
 if(phase==3&&!a2_started&&world->services.diagnostics().draining==0){const auto vacant=world->services.diagnostics();need(vacant.assigned==1,"A_assignment_vacant");event("vacant",1,vacant.assigned,vacant.draining);need(world->construct(world->a),"quiescent_reuse_admitted");need(world->a.key.generation!=old_key&&world->a.session.generation!=old_session,"reuse_new_identity");world->play(world->a,1939320,-1);a2_started=true;event("reused",1,world->a.key.generation,world->a.session.generation);}
 if(phase==3&&a2_started&&!world->a.playing&&!world->b.playing){need(b_progress,"B_presented_after_cancel");need(world->a.calls==1&&world->b.calls==1,"natural_callback_once");need(world->a.writes>=2&&world->b.writes>=2,"both_terminal_presentations");world->capture(world->a,"terminal");world->capture(world->b,"terminal");hold_begin=tick();phase=4;}}
 if(phase==4){++hold_passes;need(world->a.calls==1&&world->b.calls==1,"no_duplicate_callback");if(hold_passes>=20&&tick()-hold_begin>=frequency/4){world->capture(world->a,"hold");world->capture(world->b,"hold");world->retire(world->a);world->retire(world->b);world->services.close_admission_and_cancel_on_owner();phase=5;}}
 Sleep(1);}
 need(phase==5,"scenario_complete");const auto drain_begin=tick();while(world->services.diagnostics().assigned&&tick()-drain_begin<frequency*15){++pass;world->maintenance();Sleep(1);}const auto final=world->services.diagnostics();need(!final.assigned&&!final.draining&&!final.leases&&!final.quarantined,"final_assignments_quiescent");need(world->memory.allocations==world->memory.frees,"authored_shell_accounting");need(!world->gate.admission.depth(),"final_gate_depth");
 finished.store(true,std::memory_order_release);need(WaitForSingleObject(watch,5000)==WAIT_OBJECT_0,"watchdog_stopped");CloseHandle(watch);
 std::printf("CXR_RESULT checks=%u failures=%u captures=%u passes=%u max_pass_qpc=%llu assigned=%u draining=%u leases=%u B_after_cancel=%u A2_callbacks=%u B_callbacks=%u\n",checks,failures,capture_count,pass,(unsigned long long)largest_pass,final.assigned,final.draining,final.leases,unsigned(b_progress),world->a.calls,world->b.calls);std::fflush(stdout);
 // Process-lifetime service/module objects intentionally survive until exit.
 return failures?1:0;}
