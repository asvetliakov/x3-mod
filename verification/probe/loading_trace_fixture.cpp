// Tests main-module import patching and exact forwarded results, without X3.
#include "../../src/proxy/loading_trace.h"
#include <d3dx9.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#define PTR(T,n) reinterpret_cast<T*>(static_cast<uintptr_t>(n))
extern "C" {
void* __cdecl gzopen(const char*,const char*);
int __cdecl gzread(void*,void*,unsigned);
LONG __cdecl gzseek(void*,LONG,int);
int __cdecl inflate(void*,int);
void* __cdecl xmlReadMemory(const char*,int,const char*,const char*,int);
}
namespace x3m {void log(const char* format,...){va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');}}
using namespace x3m::loading_trace;
static unsigned checks=0,failures=0;
static void check(bool okay,const char* label){++checks;if(!okay){++failures;printf("FAIL %s last_error=%lu\n",label,GetLastError());}}
static const Sample& sample(const Snapshot& s,Operation op){return s[static_cast<unsigned>(op)];}
static PVOID* import_slot(const char* wanted) {
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;imports->Name;++imports) {
        auto names=reinterpret_cast<IMAGE_THUNK_DATA32*>(base+imports->OriginalFirstThunk);
        auto slots=reinterpret_cast<IMAGE_THUNK_DATA32*>(base+imports->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal))continue;
            auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            if(!strcmp(reinterpret_cast<const char*>(name->Name),wanted))return reinterpret_cast<PVOID*>(&slots->u1.Function);
        }
    }return nullptr;
}
static decltype(&ReadFile) fixture_raw_read=nullptr;
static BOOL WINAPI other_interceptor(HANDLE f,void* b,DWORD n,DWORD* r,OVERLAPPED* o){return fixture_raw_read(f,b,n,r,o);}
__attribute__((noinline)) static BOOL read_fresh_import(HANDLE file,void* data,DWORD* read){return ReadFile(file,data,4,read,nullptr);}
int main(){
    HMODULE self=GetModuleHandleW(nullptr);
    const uint64_t hash=fixture_fingerprint(self);
    check(hash!=0,"fixture_fingerprint");
    SetEnvironmentVariableW(L"X3M_TELEMETRY",nullptr);
    check(!fixture_initialize(self,hash)&&!active(),"disabled");
    SetEnvironmentVariableW(L"X3M_TELEMETRY",L"1");
    check(!fixture_initialize(self,hash^1)&&!active(),"wrong_fingerprint");
    check(!initialize()&&!active(),"production_gate_rejects_fixture");
    auto kernel=GetModuleHandleW(L"kernel32.dll");
    auto rawRead=reinterpret_cast<decltype(&ReadFile)>(GetProcAddress(kernel,"ReadFile"));
    fixture_raw_read=rawRead;
    auto rawSeek=reinterpret_cast<decltype(&SetFilePointer)>(GetProcAddress(kernel,"SetFilePointer"));
    auto rawOpen=reinterpret_cast<decltype(&CreateFileA)>(GetProcAddress(kernel,"CreateFileA"));
    auto user=GetModuleHandleW(L"user32.dll");
    auto rawCursor=reinterpret_cast<decltype(&SetCursor)>(GetProcAddress(user,"SetCursor"));
    const DWORD sentinel=0x2468;
    char temp[MAX_PATH]{};GetTempPathA(MAX_PATH,temp);strcat(temp,"x3-loading-fixture.tmp");
    HANDLE file=rawOpen(temp,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    check(file!=INVALID_HANDLE_VALUE,"open_fixture");
    DWORD written=0;check(WriteFile(file,"test",4,&written,nullptr)&&written==4,"write_fixture");
    rawSeek(file,0,nullptr,FILE_BEGIN);
    char baseline[8]{};DWORD read=0;SetLastError(sentinel);
    BOOL baseline_result=rawRead(file,baseline,4,&read,nullptr);DWORD baseline_error=GetLastError();
    check(baseline_result&&read==4,"baseline_read");rawSeek(file,0,nullptr,FILE_BEGIN);
    PVOID* read_slot=import_slot("ReadFile");MEMORY_BASIC_INFORMATION before{},after{};
    check(read_slot&&VirtualQuery(read_slot,&before,sizeof before),"iat_page_before");
    check(fixture_initialize(self,hash)&&active(),"install_named_imports");
    check(VirtualQuery(read_slot,&after,sizeof after)&&before.Protect==after.Protect,"iat_protection_restored_after_install");
    take_snapshot();
    char actual[8]{};read=0;SetLastError(sentinel);
    BOOL result=ReadFile(file,actual,4,&read,nullptr);DWORD error=GetLastError();
    check(result==baseline_result&&error==baseline_error&&read==4&&!memcmp(actual,baseline,4),"read_success_exact");
    SetLastError(sentinel);result=ReadFile(INVALID_HANDLE_VALUE,actual,4,&read,nullptr);error=GetLastError();
    SetLastError(sentinel);BOOL expected=rawRead(INVALID_HANDLE_VALUE,baseline,4,&read,nullptr);DWORD expected_error=GetLastError();
    check(result==expected&&error==expected_error&&!result,"read_failure_exact");
    SetLastError(sentinel);DWORD offset=SetFilePointer(file,2,nullptr,FILE_BEGIN);error=GetLastError();
    SetLastError(sentinel);DWORD expected_offset=rawSeek(file,2,nullptr,FILE_BEGIN);expected_error=GetLastError();
    check(offset==expected_offset&&error==expected_error,"seek_success_exact");
    SetLastError(sentinel);offset=SetFilePointer(INVALID_HANDLE_VALUE,0,nullptr,FILE_BEGIN);error=GetLastError();
    SetLastError(sentinel);expected_offset=rawSeek(INVALID_HANDLE_VALUE,0,nullptr,FILE_BEGIN);expected_error=GetLastError();
    check(offset==expected_offset&&error==expected_error,"seek_failure_exact");
    SetLastError(sentinel);HANDLE absent=CreateFileA("Z:\\definitely-nonexistent-x3-loading-fixture",GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);error=GetLastError();
    SetLastError(sentinel);HANDLE expected_absent=rawOpen("Z:\\definitely-nonexistent-x3-loading-fixture",GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);expected_error=GetLastError();
    check(absent==expected_absent&&absent==INVALID_HANDLE_VALUE&&error==expected_error,"open_failure_exact");
    HANDLE duplicate=CreateFileA(temp,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    check(duplicate!=INVALID_HANDLE_VALUE,"open_success");if(duplicate!=INVALID_HANDLE_VALUE)CloseHandle(duplicate);
    SetLastError(sentinel);rawCursor(nullptr);SetLastError(sentinel);HCURSOR expected_cursor=rawCursor(nullptr);expected_error=GetLastError();
    SetLastError(sentinel);HCURSOR cursor=SetCursor(nullptr);error=GetLastError();check(cursor==expected_cursor&&error==expected_error,"setcursor_exact");
    // Reference SetCursorPos so it has a named import, but do not move the user's cursor.
    if(GetEnvironmentVariableW(L"X3M_FIXTURE_MOVE_CURSOR",nullptr,0))SetCursorPos(0,0);
    ID3DXEffect* fx=nullptr;ID3DXBuffer* errors=nullptr;
    HRESULT hr=D3DXCreateEffect(PTR(IDirect3DDevice9,0x1000),PTR(void,0x2000),123,PTR(D3DXMACRO,0x3000),PTR(ID3DXInclude,0x4000),456,PTR(ID3DXEffectPool,0x5000),&fx,&errors);
    error=GetLastError();check(hr==S_FALSE&&error==0x4321&&fx==PTR(ID3DXEffect,0x6000)&&errors==PTR(ID3DXBuffer,0x7000),"effect_all_args_success");
    hr=D3DXCreateEffect(PTR(IDirect3DDevice9,0x1000),PTR(void,0x2000),123,PTR(D3DXMACRO,0x3000),PTR(ID3DXInclude,0x4000),0,PTR(ID3DXEffectPool,0x5000),&fx,&errors);
    error=GetLastError();check(hr==E_INVALIDARG&&error==0x8765,"effect_failure_exact");
    IDirect3DTexture9* texture=nullptr;
    hr=D3DXCreateTextureFromFileInMemoryEx(PTR(IDirect3DDevice9,0x1000),PTR(void,0x2000),321,12,13,14,15,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,16,17,18,PTR(D3DXIMAGE_INFO,0x3000),PTR(PALETTEENTRY,0x4000),&texture);
    error=GetLastError();check(hr==S_FALSE&&error==0x4321&&texture==PTR(IDirect3DTexture9,0x5000),"texture_all_args");
    IDirect3DCubeTexture9* cube=nullptr;
    hr=D3DXCreateCubeTextureFromFileInMemoryEx(PTR(IDirect3DDevice9,0x1000),PTR(void,0x2000),654,12,14,15,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,16,17,18,PTR(D3DXIMAGE_INFO,0x3000),PTR(PALETTEENTRY,0x4000),&cube);
    error=GetLastError();check(hr==S_FALSE&&error==0x4321&&cube==PTR(IDirect3DCubeTexture9,0x5000),"cube_all_args");
    hr=D3DXLoadSurfaceFromFileInMemory(PTR(IDirect3DSurface9,0x1000),PTR(PALETTEENTRY,0x2000),PTR(RECT,0x3000),PTR(void,0x4000),987,PTR(RECT,0x5000),16,18,PTR(D3DXIMAGE_INFO,0x6000));
    error=GetLastError();check(hr==S_FALSE&&error==0x4321,"surface_all_args");
    void* gz=gzopen(PTR(char,0x1000),PTR(char,0x2000));error=GetLastError();
    check(gz==PTR(void,0x3000)&&error==0x4321,"gzopen_args");
    void* badgz=gzopen(nullptr,PTR(char,0x2000));error=GetLastError();check(!badgz&&error==0x8765,"gzopen_failure");
    char codec[8]{};int n=gzread(gz,codec,7);error=GetLastError();check(n==4&&error==0x4321&&!memcmp(codec,"gzip",4),"gzread_args");
    n=gzread(nullptr,codec,7);error=GetLastError();check(n==-1&&error==0x8765,"gzread_failure");
    LONG pos=gzseek(gz,-33,2);error=GetLastError();check(pos==0x76543210&&error==0x4321,"gzseek_signed32_args");
    pos=gzseek(gz,-33,1);error=GetLastError();check(pos==-1&&error==0x8765,"gzseek_failure");
    n=inflate(PTR(void,0x1000),4);error=GetLastError();check(n==1&&error==0x4321,"inflate_args");
    n=inflate(PTR(void,0x1000),0);error=GetLastError();check(n==-5&&error==0x8765,"inflate_nonfatal");
    n=inflate(nullptr,4);error=GetLastError();check(n==-3&&error==0x8765,"inflate_failure");
    void* xml=xmlReadMemory(PTR(char,0x1000),123,PTR(char,0x2000),PTR(char,0x3000),0x180);error=GetLastError();check(xml==PTR(void,0x4000)&&error==0x4321,"xmlread_args");
    xml=xmlReadMemory(PTR(char,0x1000),123,PTR(char,0x2000),PTR(char,0x3000),0);error=GetLastError();check(!xml&&error==0x8765,"xmlread_failure");
    const auto data=take_snapshot();
    check(sample(data,Operation::FileRead).count==2&&sample(data,Operation::FileRead).failures==1&&sample(data,Operation::FileRead).bytes==4,"read_counters");
    check(sample(data,Operation::FileSeek).count==2&&sample(data,Operation::FileSeek).ambiguous==1,"seek_counters");
    check(sample(data,Operation::FileOpen).count==2&&sample(data,Operation::FileOpen).failures==1,"open_counters");
    check(sample(data,Operation::Effect).count==2&&sample(data,Operation::Effect).failures==1&&sample(data,Operation::Effect).bytes==246,"effect_counters");
    check(sample(data,Operation::Texture).bytes==321&&sample(data,Operation::CubeTexture).bytes==654&&sample(data,Operation::Surface).bytes==987,"texture_byte_counters");
    check(sample(data,Operation::CursorSet).count==1,"cursor_count");
    check(sample(data,Operation::GzOpen).count==2&&sample(data,Operation::GzOpen).failures==1,"gzopen_counters");
    check(sample(data,Operation::GzRead).count==2&&sample(data,Operation::GzRead).failures==1&&sample(data,Operation::GzRead).bytes==4,"gzread_counters");
    check(sample(data,Operation::GzSeek).count==2&&sample(data,Operation::GzSeek).failures==1,"gzseek_counters");
    check(sample(data,Operation::Inflate).count==3&&sample(data,Operation::Inflate).failures==1&&sample(data,Operation::Inflate).ambiguous==1,"inflate_counters");
    check(sample(data,Operation::XmlRead).count==2&&sample(data,Operation::XmlRead).failures==1&&sample(data,Operation::XmlRead).bytes==246,"xml_counters");
    check(sample(take_snapshot(),Operation::Effect).count==0,"snapshot_exchange");
    // Same-process synthetic overhead check; never a claim about game loading.
    auto rawInflate=reinterpret_cast<int (__cdecl*)(void*,int)>(GetProcAddress(GetModuleHandleW(L"zlib1.dll"),"inflate"));
    LARGE_INTEGER hz{},begin{},middle{},end{};QueryPerformanceFrequency(&hz);
    constexpr unsigned iterations=20000;volatile int sink=0;
    QueryPerformanceCounter(&begin);
    for(unsigned i=0;i<iterations;++i)sink=rawInflate(PTR(void,0x1000),4);
    QueryPerformanceCounter(&middle);
    for(unsigned i=0;i<iterations;++i)sink=inflate(PTR(void,0x1000),4);
    QueryPerformanceCounter(&end);
    printf("loading_benchmark synthetic=1 calls=%u raw_us_per_call=%.6f hooked_us_per_call=%.6f\n",iterations,
        double(middle.QuadPart-begin.QuadPart)*1e6/hz.QuadPart/iterations,
        double(end.QuadPart-middle.QuadPart)*1e6/hz.QuadPart/iterations);
    check(sink==1&&sample(take_snapshot(),Operation::Inflate).count==iterations,"benchmark_count_and_result");
    char pipe_name[128];snprintf(pipe_name,sizeof pipe_name,"\\\\.\\pipe\\x3_loading_%lu",GetCurrentProcessId());
    HANDLE server=CreateNamedPipeA(pipe_name,PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED,PIPE_TYPE_BYTE|PIPE_WAIT,1,64,64,0,nullptr);
    OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    check(server!=INVALID_HANDLE_VALUE&&ov.hEvent,"pending_pipe_setup");
    if(server!=INVALID_HANDLE_VALUE&&ov.hEvent){
        BOOL connected=ConnectNamedPipe(server,&ov);DWORD connection_error=GetLastError();
        check(connected||connection_error==ERROR_IO_PENDING,"pending_pipe_connect");
        HANDLE client=rawOpen(pipe_name,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        check(client!=INVALID_HANDLE_VALUE,"pending_pipe_client");
        DWORD transferred=0;GetOverlappedResult(server,&ov,&transferred,TRUE);ResetEvent(ov.hEvent);
        BOOL pending_result=ReadFile(server,actual,4,&transferred,&ov);DWORD pending_error=GetLastError();
        check(!pending_result&&pending_error==ERROR_IO_PENDING,"read_pending_exact");
        CancelIoEx(server,&ov);GetOverlappedResult(server,&ov,&transferred,TRUE);
        if(client!=INVALID_HANDLE_VALUE)CloseHandle(client);
        const auto pending_data=take_snapshot();
        check(sample(pending_data,Operation::FileRead).count==1&&sample(pending_data,Operation::FileRead).pending==1&&sample(pending_data,Operation::FileRead).failures==0,"pending_counter_not_failure");
    }
    if(server!=INVALID_HANDLE_VALUE)CloseHandle(server);
    if(ov.hEvent)CloseHandle(ov.hEvent);
    // Simulate another module replacing one of our hooks before teardown.
    DWORD old_protection=0,discard=0;
    check(VirtualProtect(read_slot,sizeof(PVOID),PAGE_READWRITE,&old_protection),"third_party_slot_writable");
    InterlockedExchangePointer(read_slot,reinterpret_cast<PVOID>(other_interceptor));
    check(VirtualProtect(read_slot,sizeof(PVOID),old_protection,&discard),"third_party_slot_protected");
    SetLastError(sentinel);shutdown();check(!active()&&GetLastError()==sentinel,"restore_and_error");
    check(*read_slot==reinterpret_cast<PVOID>(other_interceptor),"shutdown_preserves_other_hook");
    check(VirtualQuery(read_slot,&after,sizeof after)&&before.Protect==after.Protect,"iat_protection_restored_after_shutdown");
    VirtualProtect(read_slot,sizeof(PVOID),PAGE_READWRITE,&old_protection);
    InterlockedExchangePointer(read_slot,reinterpret_cast<PVOID>(rawRead));
    VirtualProtect(read_slot,sizeof(PVOID),old_protection,&discard);
    rawSeek(file,0,nullptr,FILE_BEGIN);read_fresh_import(file,actual,&read);
    check(sample(take_snapshot(),Operation::FileRead).count==0,"restored_import_not_intercepted");
    CloseHandle(file);
    printf("loading_fixture checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
