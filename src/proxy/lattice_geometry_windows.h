#pragma once
// Fixed schema-2 publication backend. Exclusively owns newly created temporary
// files; MoveFileW refuses replacement. Cleanup never deletes pre-existing files.
#include <windows.h>
#include <wincrypt.h>
#include <io.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdint>

namespace x3m::lattice_state::geometry {
class WindowsFiles {
    wchar_t paths_[2][1024]{},temporary_[2][1032]{};
    bool valid_=false,owned_temp_[2]{},owned_final_[2]{};
    FILE* open(unsigned index) noexcept {
        if(!valid_)return nullptr;
        HANDLE handle=CreateFileW(temporary_[index],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return nullptr;
        owned_temp_[index]=true;
        const int fd=_open_osfhandle(reinterpret_cast<intptr_t>(handle),_O_BINARY|_O_WRONLY);
        if(fd<0){CloseHandle(handle);return nullptr;}
        FILE* file=_fdopen(fd,"wb");
        if(!file)_close(fd);
        return file;
    }
    bool rename(unsigned index) noexcept {
        if(!owned_temp_[index]||!MoveFileW(temporary_[index],paths_[index]))return false;
        owned_temp_[index]=false;owned_final_[index]=true;return true;
    }
public:
    WindowsFiles(const wchar_t* directory,const char* json,const char* binary) noexcept {
        if(!directory)return;
        const char* names[]={binary,json};
        valid_=true;
        for(unsigned i=0;i<2;++i){
            const int n=swprintf(paths_[i],1024,L"%ls\\%hs",directory,names[i]);
            if(n<=0||n>=1024){valid_=false;break;}
            const int m=swprintf(temporary_[i],1032,L"%ls.tmp",paths_[i]);
            if(m<=0||m>=1032){valid_=false;break;}
        }
    }
    bool hash(const unsigned char* bytes,unsigned size,char* hex) noexcept {
        HCRYPTPROV provider=0;HCRYPTHASH digest=0;
        bool okay=CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)!=FALSE&&
            CryptCreateHash(provider,CALG_SHA_256,0,0,&digest)!=FALSE&&
            CryptHashData(digest,bytes,size,0)!=FALSE;
        unsigned char result[32]{};DWORD count=sizeof result;
        okay=okay&&CryptGetHashParam(digest,HP_HASHVAL,result,&count,0)!=FALSE&&count==sizeof result;
        if(okay){
            constexpr char digits[]="0123456789abcdef";
            for(unsigned i=0;i<32;++i){hex[2*i]=digits[result[i]>>4];hex[2*i+1]=digits[result[i]&15];}
            hex[64]=0;
        }
        if(digest)CryptDestroyHash(digest);
        if(provider)CryptReleaseContext(provider,0);
        return okay;
    }
    FILE* open_payload() noexcept {return open(0);}
    bool write_payload(FILE* file,const unsigned char* data,unsigned size) noexcept {
        return std::fwrite(data,1,size,file)==size&&!std::ferror(file);
    }
    bool close_payload(FILE* file) noexcept {return std::fclose(file)==0;}
    bool publish_payload() noexcept {return rename(0);}
    FILE* open_json() noexcept {return open(1);}
    bool close_json(FILE* file) noexcept {return std::fclose(file)==0;}
    bool publish_json() noexcept {return rename(1);}
    void remove_payload() noexcept {
        if(owned_final_[0]&&DeleteFileW(paths_[0]))owned_final_[0]=false;
        if(owned_temp_[0]&&DeleteFileW(temporary_[0]))owned_temp_[0]=false;
    }
    void remove_temporaries() noexcept {
        for(unsigned i=0;i<2;++i)if(owned_temp_[i]&&DeleteFileW(temporary_[i]))owned_temp_[i]=false;
    }
};
}
