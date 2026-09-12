// Synthetic PE32 game-address map; no game bytes beyond short hook-site patterns,
// no real keysets/CSP calls, no modification to any installed image.
#include "../../src/proxy/loading_trace.h"
#include "../../src/proxy/crypt_cache.h"
#include "loading_admission_witness.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
namespace x3m {void log(const char*,...) {}}
namespace cc=x3m::crypt_cache;using namespace x3m::loading_trace;
unsigned checks=0,failures=0,native_calls=0,observations=0;void** acquire_slot=nullptr;
void check(bool v,const char*n){++checks;if(!v)++failures;std::printf("CHECK %s %s\n",n,v?"PASS":"FAIL");}
BOOL WINAPI acq(HCRYPTPROV*,LPCSTR,LPCSTR,DWORD,DWORD){++native_calls;SetLastError(NTE_BAD_KEYSET);return FALSE;}
BOOL WINAPI rel(HCRYPTPROV,DWORD){++native_calls;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
BOOL WINAPI imp(HCRYPTPROV,const BYTE*,DWORD,HCRYPTKEY,DWORD,HCRYPTKEY*){++native_calls;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
BOOL WINAPI des(HCRYPTKEY){++native_calls;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
void observer(){++observations;check(!crypt_cache_enabled()&&!cc::enabled(),"partial group never activates cache");HCRYPTPROV out=0;unsigned before=native_calls;auto call=reinterpret_cast<cc::AcquireFn>(*acquire_slot);check(!call(&out,"X2EgosoftCSPContainer","Microsoft Base Cryptographic Provider v1.0",1,CRYPT_NEWKEYSET)&&GetLastError()==DWORD(NTE_BAD_KEYSET)&&native_calls==before+1,"partial group wrapper forwards native result");}
extern "C" BOOL __cdecl crypt_fixture_callsite(uintptr_t,HCRYPTPROV*,LPCSTR,LPCSTR,DWORD,DWORD);
asm(".text\n.globl _crypt_fixture_callsite\n_crypt_fixture_callsite:\n"
    "movl 4(%esp), %eax\n"
    "pushl 24(%esp)\n pushl 24(%esp)\n pushl 24(%esp)\n pushl 24(%esp)\n pushl 24(%esp)\n"
    "jmp *%eax\n");
int main(int argc,char**argv){if(argc!=2)return 2;const char*mode=argv[1];SetEnvironmentVariableA("X3M_TELEMETRY","0");SetEnvironmentVariableA("X3M_GZ_BUFFER","0");SetEnvironmentVariableA("X3M_CRYPT_CACHE","1");
 auto b=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x180000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));check(b!=nullptr,"synthetic address allocation");if(!b)return 2;
 const uintptr_t base=reinterpret_cast<uintptr_t>(b);fixture_crypt_image_base(reinterpret_cast<HMODULE>(b));
 auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(b);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(b+0x80);nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;nt->OptionalHeader.NumberOfRvaAndSizes=16;nt->OptionalHeader.SizeOfImage=0x180000;nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]={0x1000,40};
 auto descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(b+0x1000);descriptor->Name=0x1100;std::strcpy(reinterpret_cast<char*>(b+0x1100),"ADVAPI32.dll");descriptor->OriginalFirstThunk=0x1200;descriptor->FirstThunk=0x132000;
 auto names=reinterpret_cast<IMAGE_THUNK_DATA32*>(b+0x1200);auto slots=reinterpret_cast<IMAGE_THUNK_DATA32*>(b+0x132000);
 const unsigned indices[]={0,10,14,13};const char* symbols[]={"CryptAcquireContextA","CryptReleaseContext","CryptImportKey","CryptDestroyKey"};const void* originals[]={reinterpret_cast<void*>(&acq),reinterpret_cast<void*>(&rel),reinterpret_cast<void*>(&imp),reinterpret_cast<void*>(&des)};
 for(unsigned i=0;i<15;++i){names[i].u1.AddressOfData=0x1400+i*64;auto entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(b+0x1400+i*64);std::strcpy(reinterpret_cast<char*>(entry->Name),"OtherImport");slots[i].u1.Function=reinterpret_cast<uintptr_t>(&acq);}
 for(unsigned i=0;i<4;++i){auto entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(b+names[indices[i]].u1.AddressOfData);std::strcpy(reinterpret_cast<char*>(entry->Name),symbols[i]);slots[indices[i]].u1.Function=reinterpret_cast<uintptr_t>(originals[i]);}
 const uintptr_t pcs[]={0x4cac3e,0x4cac57,0x4cac85,0x4cae4d,0x4cae5a,0x4cae73};const unsigned si[]={0,0,14,13,10,0};
 for(unsigned i=0;i<6;++i){auto code=reinterpret_cast<unsigned char*>(base+pcs[i]-0x400000-6);code[0]=0xff;code[1]=0x15;uint32_t address=uint32_t(base)+0x132000+si[i]*4;std::memcpy(code+2,&address,4);}
 const unsigned char create_args[]={0x6a,0x08,0x6a,0x01,0x68,0x88,0x38,0x56,0x00,0x68,0xb4,0x38,0x56,0x00,0x8d,0x4c,0x24,0x28,0x51};std::memcpy(reinterpret_cast<void*>(base+0xcac3e),create_args,sizeof create_args);
 // Our synthetic verifier continuation returns to the fixture's caller. This
 // byte follows the validated acquire call and is not extracted game code.
 *reinterpret_cast<unsigned char*>(base+0xcac57)=0xc3;
 const unsigned char release_args[]={0x8b,0x54,0x24,0x18,0x6a,0x00,0x52};std::memcpy(reinterpret_cast<void*>(base+0xcae4d),release_args,sizeof release_args);
 if(!std::strcmp(mode,"bytes"))b[0xcac3e]^=1;
 if(!std::strncmp(mode,"missing",7)){unsigned i=unsigned(mode[7]-'0');auto entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(b+names[indices[i]].u1.AddressOfData);entry->Name[0]='?';}
 const unsigned fail=!std::strncmp(mode,"patch",5)?unsigned(mode[5]-'0'):0;acquire_slot=reinterpret_cast<void**>(&slots[0].u1.Function);fixture_crypt_patch_control(fail,observer);
 FlushInstructionCache(GetCurrentProcess(),b,0x180000);
 const bool wanted=!std::strcmp(mode,"complete");const bool installed=fixture_initialize(reinterpret_cast<HMODULE>(b));check(installed==wanted&&crypt_cache_enabled()==wanted&&cc::enabled()==wanted,"atomic group result matches expected qualification");
 for(unsigned i=0;i<4;++i)check((slots[indices[i]].u1.Function!=reinterpret_cast<uintptr_t>(originals[i]))==wanted,"all lifetime slots committed or rolled back");
 const Operation operations[]={Operation::CryptAcquire,Operation::CryptAcquire,Operation::CryptImport,Operation::CryptKeyDestroy,Operation::CryptRelease,Operation::CryptAcquire};
 for(unsigned i=0;i<6;++i){check(fixture_crypt_site(reinterpret_cast<void*>(base+pcs[i]-0x400000),operations[i])==wanted,"exact operation callsite classification");check(!fixture_crypt_site(reinterpret_cast<void*>(base+pcs[i]-0x400000+1),operations[i]),"adjacent unobserved caller rejected");}
 HCRYPTPROV out=0;auto direct=reinterpret_cast<cc::AcquireFn>(*acquire_slot);const unsigned old=native_calls;check(!direct(&out,"X2EgosoftCSPContainer","Microsoft Base Cryptographic Provider v1.0",1,CRYPT_NEWKEYSET)&&GetLastError()==DWORD(NTE_BAD_KEYSET)&&native_calls==old+1,"unqualified real IAT dispatch stays native");check(cc::statistics().acquires==0,"no cache activity from unqualified caller");
 if(wanted){const unsigned prior=native_calls;check(!crypt_fixture_callsite(base+0xcac51,&out,"X2EgosoftCSPContainer","Microsoft Base Cryptographic Provider v1.0",1,CRYPT_NEWKEYSET)&&GetLastError()==DWORD(NTE_BAD_KEYSET)&&native_calls==prior+1,"qualified mapped IAT call preserves native failure");check(cc::statistics().acquires==1,"actual wrapper return address admits the qualified game site");}
 const unsigned expected_observers=wanted?4:fail?fail-1:0;check(observations==expected_observers,"transaction observer inventory");
 shutdown();check(fixture_protection_debts()==0,"no protection restoration debt");for(unsigned i=0;i<4;++i)check(slots[indices[i]].u1.Function==reinterpret_cast<uintptr_t>(originals[i]),"quiescent shutdown restores native slot");
 VirtualFree(b,0,MEM_RELEASE);std::printf("CRYPT HOOK RESULT mode=%s checks=%u failures=%u\n",mode,checks,failures);return failures?1:0;
}
