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
// Every X3M_* variable present in the process environment, name=value, sorted.
// Names are matched case-insensitively (Win32 environment names are); no
// unrelated variable is read or logged.
std::string options() {
    LPWCH block=GetEnvironmentStringsW();
    if(!block) return std::string();
    std::vector<std::string> entries;
    for(const wchar_t* entry=block;*entry;entry+=std::wcslen(entry)+1){
        if(entry[0]==L'=') continue; // per-drive current directory pseudo-variables
        if(_wcsnicmp(entry,L"X3M_",4)!=0) continue;
        const std::string pair=utf8(entry);
        if(pair.empty()) continue;
        const std::size_t split=pair.find('=');
        std::string name=split==std::string::npos?pair:pair.substr(0,split);
        std::string value=split==std::string::npos?std::string():pair.substr(split+1);
        sanitize(name);
        if(!value.empty()) sanitize(value);
        entries.push_back(name+"="+value);
    }
    FreeEnvironmentStringsW(block);
    std::sort(entries.begin(),entries.end());
    std::string line;
    for(const std::string& entry:entries){line+=' ';line+=entry;}
    return line;
}
}

void log_identity(HMODULE self) {
    const DWORD saved=GetLastError();
    LARGE_INTEGER begin{},end{},frequency{};
    QueryPerformanceCounter(&begin); QueryPerformanceFrequency(&frequency);
    std::string hex="unavailable",path_utf8="unknown",manifest="none",option_line;
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
    } catch(const std::exception&) {
        hex="unavailable"; option_line.clear();
    }
    QueryPerformanceCounter(&end);
    const unsigned long long microseconds=frequency.QuadPart>0&&end.QuadPart>begin.QuadPart
        ?static_cast<unsigned long long>((end.QuadPart-begin.QuadPart)*1000000ll/frequency.QuadPart):0ull;
    log("proxy_identity sha256=%s bytes=%llu path=%s manifest_sha256=%s source_commit=%s attach_us=%llu",
        hex.c_str(),bytes,path_utf8.c_str(),manifest.c_str(),X3M_SOURCE_COMMIT,microseconds);
    log("proxy_options%s",option_line.c_str());
    SetLastError(saved);
}
}
