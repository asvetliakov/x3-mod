// Read-only module/memory inventory for a specified Windows process in Wine.
// No writes, injection, thread suspension or process/window control. Sampled
// addresses are queried for allocation metadata only; no payload bytes exported.
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv){
    if(argc<2){fprintf(stderr,"usage: process_modules.exe WINDOWS_PID [HEX_ADDRESS ...]\n");return 2;}
    const DWORD pid=strtoul(argv[1],nullptr,10);
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snapshot==INVALID_HANDLE_VALUE){printf("module_error pid=%lu error=%lu\n",pid,GetLastError());return 1;}
    MODULEENTRY32W module{};module.dwSize=sizeof module;
    if(Module32FirstW(snapshot,&module))do{
        printf("module base=%p bytes=%lu name=%ls path=%ls\n",module.modBaseAddr,module.modBaseSize,module.szModule,module.szExePath);
    }while(Module32NextW(snapshot,&module));
    else printf("module_error pid=%lu error=%lu\n",pid,GetLastError());
    CloseHandle(snapshot);
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
    if(!process){printf("process_error pid=%lu error=%lu\n",pid,GetLastError());return 1;}
    FILETIME creation{},exit{},kernel{},user{};
    if(GetProcessTimes(process,&creation,&exit,&kernel,&user)){
        ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
        printf("process_cpu kernel_100ns=%llu user_100ns=%llu\n",k.QuadPart,u.QuadPart);
    }
    for(int i=2;i<argc;++i){
        const auto address=reinterpret_cast<void*>(strtoul(argv[i],nullptr,16));
        MEMORY_BASIC_INFORMATION memory{};
        if(VirtualQueryEx(process,address,&memory,sizeof memory))
            printf("memory address=%p base=%p allocation=%p bytes=%lu state=%08lx protect=%08lx type=%08lx\n",address,memory.BaseAddress,memory.AllocationBase,static_cast<DWORD>(memory.RegionSize),memory.State,memory.Protect,memory.Type);
        else printf("memory_error address=%p error=%lu\n",address,GetLastError());
    }
    CloseHandle(process);return 0;
}
