// Synthetic cdecl libraries for import-forwarding verification, not real codecs.
#include <windows.h>
#include <cstring>
#ifdef XML_ONLY
extern "C" __declspec(dllexport) void* __cdecl xmlReadMemory(const char* data,int size,const char* url,const char* encoding,int options) {
    bool okay=data==reinterpret_cast<const char*>(0x1000)&&size==123&&url==reinterpret_cast<const char*>(0x2000)&&encoding==reinterpret_cast<const char*>(0x3000)&&options==0x180;
    SetLastError(okay?0x4321:0x8765);return okay?reinterpret_cast<void*>(0x4000):nullptr;
}
#else
extern "C" __declspec(dllexport) void* __cdecl gzopen(const char* path,const char* mode) {
    bool okay=path==reinterpret_cast<const char*>(0x1000)&&mode==reinterpret_cast<const char*>(0x2000);
    SetLastError(okay?0x4321:0x8765);return okay?reinterpret_cast<void*>(0x3000):nullptr;
}
extern "C" __declspec(dllexport) int __cdecl gzread(void* file,void* data,unsigned size) {
    bool okay=file==reinterpret_cast<void*>(0x3000)&&size==7;
    if(okay)std::memcpy(data,"gzip",4);
    SetLastError(okay?0x4321:0x8765);return okay?4:-1;
}
extern "C" __declspec(dllexport) LONG __cdecl gzseek(void* file,LONG offset,int whence) {
    bool okay=file==reinterpret_cast<void*>(0x3000)&&offset==-33&&whence==2;
    SetLastError(okay?0x4321:0x8765);return okay?static_cast<LONG>(0x76543210):-1;
}
extern "C" __declspec(dllexport) int __cdecl inflate(void* stream,int flush) {
    bool okay=stream==reinterpret_cast<void*>(0x1000)&&flush==4;
    SetLastError(okay?0x4321:0x8765);return okay?1:flush==0?-5:-3;
}
#endif
