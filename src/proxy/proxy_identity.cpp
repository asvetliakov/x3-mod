#include "proxy_identity.h"
#include "capture.h"
#include "x3m_source_commit_inc.h"
#include <wincrypt.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <exception>
#include <string>
#include <vector>

// The compiled-in commit as a byte marker so a host tool (tools/manage.py) can
// read the provenance of a built DLL without loading it. External linkage keeps
// it in .rdata; nothing at runtime reads it.
extern "C" const char x3m_source_commit_marker[]="X3M_SOURCE_COMMIT=" X3M_SOURCE_COMMIT;

namespace x3m::proxy_identity {
namespace {
// UTF-8 for the log: the CRT's %ls conversion runs in the "C" locale and drops
// the whole line on a character it cannot represent (capture.cpp does the same).
std::string utf8(const std::wstring& text) {
    if(text.empty()) return std::string();
    const int size=WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,nullptr,0,nullptr,nullptr);
    if(size<2) return std::string();
    std::string out(static_cast<std::size_t>(size),'\0');
    const int written=WideCharToMultiByte(CP_UTF8,0,text.c_str(),-1,&out[0],size,nullptr,nullptr);
    if(written<2) return std::string();
    out.resize(static_cast<std::size_t>(written)-1);
    return out;
}
// The grammar is space-separated key=value: keep every field one printable
// ASCII token so a path or an option value can never split a field.
void sanitize(std::string& text) {
    for(char& c:text) if(static_cast<unsigned char>(c)<0x21u||static_cast<unsigned char>(c)>0x7eu) c='_';
    if(text.empty()) text="_";
}
bool hex_digit(char c) { return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }

// SHA-256 of a file through CryptoAPI (PROV_RSA_AES/CALG_SHA_256, documented
// since Windows XP SP3). Returns false on any failure; bytes is filled when the
// size is known even then.
bool hash_file(const std::wstring& path,std::string& hex,unsigned long long& bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if(GetFileSizeEx(file,&size)&&size.QuadPart>=0) bytes=static_cast<unsigned long long>(size.QuadPart);
    HCRYPTPROV provider=0; HCRYPTHASH hash=0;
    bool ok=CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)!=FALSE&&
        CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
    unsigned char buffer[32768]; DWORD count=0;
    while(ok){
        if(!ReadFile(file,buffer,sizeof buffer,&count,nullptr)){ok=false;break;}
        if(!count) break;
        ok=CryptHashData(hash,buffer,count,0)!=FALSE;
    }
    unsigned char digest[32]{}; DWORD digest_size=sizeof digest;
    ok=ok&&CryptGetHashParam(hash,HP_HASHVAL,digest,&digest_size,0)!=FALSE&&digest_size==32;
    if(ok){
        static constexpr char table[]="0123456789abcdef";
        hex.assign(64,'0');
        for(int i=0;i<32;++i){hex[std::size_t(i)*2]=table[digest[i]>>4];hex[std::size_t(i)*2+1]=table[digest[i]&0xf];}
    }
    if(hash) CryptDestroyHash(hash);
    if(provider) CryptReleaseContext(provider,0);
    CloseHandle(file);
    return ok;
}
// The "sha256" value recorded by tools/manage.py install in the manifest next
// to the DLL; "none" when there is no manifest or no readable field.
std::string manifest_sha256(const std::wstring& directory) {
    HANDLE file=CreateFileW((directory+L"\\x3-modern-install.json").c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE) return "none";
    std::string text(65536,'\0'); DWORD count=0;
    const bool read=ReadFile(file,&text[0],static_cast<DWORD>(text.size()),&count,nullptr)!=FALSE;
    CloseHandle(file);
    if(!read) return "none";
    text.resize(count);
    const std::size_t key=text.find("\"sha256\"");
    if(key==std::string::npos) return "none";
    const std::size_t colon=text.find(':',key+8);
    if(colon==std::string::npos) return "none";
    std::size_t at=text.find('"',colon);
    if(at==std::string::npos||at+65>text.size()) return "none";
    ++at;
    for(std::size_t i=0;i<64;++i) if(!hex_digit(text[at+i])) return "none";
    std::string value=text.substr(at,64);
    for(char& c:value) if(c>='A'&&c<='Z') c=char(c-'A'+'a');
    return value;
}
// What the mapped image says about itself, read from the module base with the
// documented PE structures only (IMAGE_DOS_HEADER/IMAGE_NT_HEADERS32). Under
// Wine a builtin DLL keeps the native file's FullDllName, so GetModuleFileNameW
// and the on-disk size cannot tell the two apart; the mapped image can
// (docs/architecture/effect-pass-replay.md, "bottle experiments").
struct ModuleImage {
    unsigned long size=0;      // OptionalHeader.SizeOfImage
    unsigned long stamp=0;     // FileHeader.TimeDateStamp
    unsigned long exports=0;   // export directory NumberOfFunctions, 0 when absent
    int wine_builtin=0;        // the 16-byte marker in the DOS header area
};
// Every read is bounds-checked against the structure that precedes it and
// against SizeOfImage; no file is opened and no pointer past the headers is
// dereferenced. The caller holds a module reference, so the image stays mapped.
ModuleImage module_image(HMODULE module) {
    ModuleImage info;
    if(!module) return info;
    const unsigned char* base=reinterpret_cast<const unsigned char*>(module);
    const IMAGE_DOS_HEADER* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return info;
    // The PE headers live in the first page of every image, which is mapped as
    // soon as the module base is valid; refuse anything that points elsewhere.
    const LONG lfanew=dos->e_lfanew;
    if(lfanew<static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))||
       lfanew>static_cast<LONG>(0x1000u-sizeof(IMAGE_NT_HEADERS32))) return info;
    const IMAGE_NT_HEADERS32* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE) return info;
    if(nt->FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER32)) return info;
    if(nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC) return info;
    info.size=nt->OptionalHeader.SizeOfImage;
    info.stamp=nt->FileHeader.TimeDateStamp;
    if(nt->OptionalHeader.NumberOfRvaAndSizes>IMAGE_DIRECTORY_ENTRY_EXPORT){
        const IMAGE_DATA_DIRECTORY& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        // The export directory is read only when it lies wholly inside the
        // mapped image; a truncated or absent directory reports zero.
        if(directory.VirtualAddress&&directory.Size>=sizeof(IMAGE_EXPORT_DIRECTORY)&&
           info.size>=sizeof(IMAGE_EXPORT_DIRECTORY)&&
           directory.VirtualAddress<=info.size-sizeof(IMAGE_EXPORT_DIRECTORY))
            info.exports=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base+directory.VirtualAddress)->NumberOfFunctions;
    }
    // Wine stamps "Wine builtin DLL" into the unused DOS header area of a
    // builtin module. Informational telemetry, never a prerequisite: a native
    // file and a Windows host simply report 0.
    static const char marker[]="Wine builtin DLL";
    const std::size_t marker_size=sizeof(marker)-1; // 16 bytes, no terminator
    for(std::size_t at=0x40;at+marker_size<=0x80;++at)
        if(std::memcmp(base+at,marker,marker_size)==0){info.wine_builtin=1;break;}
    return info;
}
// Shared body of the two log_loaded_module entry points; `module` may be null
// (the DLL is not loaded in this process, itself a finding).
void log_module(HMODULE module,const char* name) {
    std::string path_utf8="none",hex="none";
    unsigned long long bytes=0;
    ModuleImage image;
    LARGE_INTEGER begin{},end{},frequency{};
    QueryPerformanceCounter(&begin); QueryPerformanceFrequency(&frequency);
    // Nothing may escape into a caller on the attach or device-creation path.
    try {
        if(module){
            image=module_image(module);
            std::wstring path(32768,L'\0');
            const DWORD length=GetModuleFileNameW(module,&path[0],static_cast<DWORD>(path.size()));
            if(length&&length<path.size()){
                path.resize(length);
                path_utf8=utf8(path);
                sanitize(path_utf8);
                std::string full;
                hex=hash_file(path,full,bytes)&&full.size()==64?full.substr(0,16):std::string("unavailable");
            } else path_utf8="unavailable";
        }
    } catch(const std::exception&) { hex="unavailable"; }
    QueryPerformanceCounter(&end);
    const unsigned long long microseconds=frequency.QuadPart>0&&end.QuadPart>begin.QuadPart
        ?static_cast<unsigned long long>((end.QuadPart-begin.QuadPart)*1000000ll/frequency.QuadPart):0ull;
    log("loaded_module name=%s path=%s size=%llu sha256=%s hash_us=%llu image_size=%lu stamp=%08lx exports=%lu wine_builtin=%d",
        name,path_utf8.c_str(),bytes,hex.c_str(),microseconds,
        image.size,image.stamp,image.exports,image.wine_builtin);
}
// Longest value kept in the environment line: WINEDLLOVERRIDES and
// WINE_D3D_CONFIG can be arbitrarily long, and the line is provenance, not a
// transcript.
constexpr std::size_t kValueLimit=200;
bool is_option(const wchar_t* entry) { return _wcsnicmp(entry,L"X3M_",4)==0; }
// What the launcher sets for the child process: FEX_* (FEX emulation),
// WINE* (WINEDLLOVERRIDES, WINEDEBUG, WINE_D3D_CONFIG, WINEMSYNC, WINEESYNC)
// and CX_* (CrossOver). Recorded so a run can show which of them arrived.
bool is_environment(const wchar_t* entry) {
    return _wcsnicmp(entry,L"FEX_",4)==0||_wcsnicmp(entry,L"WINE",4)==0||_wcsnicmp(entry,L"CX_",3)==0;
}
// The name=value entries of the process environment accepted by `select`,
// sorted, values truncated to `limit` characters with a trailing ellipsis.
// Documented Win32 enumeration only (GetEnvironmentStringsW, released with
// FreeEnvironmentStringsW); one pass, bounded by the block the OS hands back,
// and no variable outside the filter is read or logged. Names are matched
// case-insensitively, as Win32 environment names are.
std::vector<std::string> collect(bool (*select)(const wchar_t*),std::size_t limit) {
    std::vector<std::string> entries;
    LPWCH block=GetEnvironmentStringsW();
    if(!block) return entries;
    for(const wchar_t* entry=block;*entry;entry+=std::wcslen(entry)+1){
        if(entry[0]==L'=') continue; // per-drive current directory pseudo-variables
        if(!select(entry)) continue;
        const std::string pair=utf8(entry);
        if(pair.empty()) continue;
        const std::size_t split=pair.find('=');
        std::string name=split==std::string::npos?pair:pair.substr(0,split);
        std::string value=split==std::string::npos?std::string():pair.substr(split+1);
        sanitize(name);
        if(!value.empty()) sanitize(value);
        if(value.size()>limit){value.resize(limit);value+="\xe2\x80\xa6";} // U+2026, kept out of sanitize
        entries.push_back(name+"="+value);
    }
    FreeEnvironmentStringsW(block);
    std::sort(entries.begin(),entries.end());
    return entries;
}
// Every X3M_* variable present in the process environment, name=value, sorted;
// values are not truncated, an option value is the option.
std::string options() {
    std::string line;
    for(const std::string& entry:collect(is_option,std::string::npos)){line+=' ';line+=entry;}
    return line;
}
// The emulation and Wine/CrossOver variables, same form, with the number of
// entries last so an empty list is still a positive statement.
std::string environment() {
    const std::vector<std::string> entries=collect(is_environment,kValueLimit);
    std::string line;
    for(const std::string& entry:entries){line+=' ';line+=entry;}
    line+=" count="+std::to_string(entries.size());
    return line;
}
}

void log_identity(HMODULE self) {
    const DWORD saved=GetLastError();
    LARGE_INTEGER begin{},end{},frequency{};
    QueryPerformanceCounter(&begin); QueryPerformanceFrequency(&frequency);
    std::string hex="unavailable",path_utf8="unknown",manifest="none",option_line,environment_line=" count=0";
    unsigned long long bytes=0;
    // Nothing may escape into initialize_log: a bad_alloc on the 32 KiB buffers
    // or the environment block costs the header, never the session.
    try {
        std::wstring path(32768,L'\0');
        const DWORD length=GetModuleFileNameW(self,&path[0],static_cast<DWORD>(path.size()));
        if(length&&length<path.size()){
            path.resize(length);
            if(!hash_file(path,hex,bytes)) hex="unavailable";
            path_utf8=utf8(path);
            sanitize(path_utf8);
            const std::size_t separator=path.find_last_of(L"\\/");
            manifest=manifest_sha256(separator==std::wstring::npos?std::wstring(L"."):path.substr(0,separator));
        }
        option_line=options();
        environment_line=environment();
    } catch(const std::exception&) {
        hex="unavailable"; option_line.clear(); environment_line=" count=0";
    }
    QueryPerformanceCounter(&end);
    const unsigned long long microseconds=frequency.QuadPart>0&&end.QuadPart>begin.QuadPart
        ?static_cast<unsigned long long>((end.QuadPart-begin.QuadPart)*1000000ll/frequency.QuadPart):0ull;
    log("proxy_identity sha256=%s bytes=%llu path=%s manifest_sha256=%s source_commit=%s attach_us=%llu",
        hex.c_str(),bytes,path_utf8.c_str(),manifest.c_str(),X3M_SOURCE_COMMIT,microseconds);
    log("proxy_options%s",option_line.c_str());
    log("proxy_environment%s",environment_line.c_str());
    SetLastError(saved);
}

void log_loaded_module(const wchar_t* name) {
    const DWORD saved=GetLastError();
    std::string label="unknown";
    HMODULE module=nullptr;
    // GetModuleHandleExW takes a reference, so the module cannot unload between
    // the lookup and the file read; no PIN, the reference is released here.
    if(name&&!GetModuleHandleExW(0,name,&module)) module=nullptr;
    try { label=utf8(name?name:L""); sanitize(label); } catch(const std::exception&) { label="unknown"; }
    log_module(module,label.c_str());
    if(module) FreeLibrary(module);
    SetLastError(saved);
}

void log_loaded_module(HMODULE module,const char* name) {
    const DWORD saved=GetLastError();
    log_module(module,name&&name[0]?name:"unknown");
    SetLastError(saved);
}
}
