// Actual WindowsInput exercise. Tiny synthetic package, no COM/graph/media loads.
// Build separately with src/media/package_config.cpp; root owns Wine execution.
#include "../../src/media/package_config.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::media;
namespace {
unsigned checks=0,skips=0;
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
void check(bool ok,const char* label,DWORD error=0) {
    ++checks;
    std::cout<<"{\"event\":\"check\",\"name\":\""<<label<<"\",\"ok\":"<<(ok?"true":"false")<<",\"win32\":"<<error<<"}\n";
    if(!ok)throw std::runtime_error(label);
}
void win_check(bool ok,const char* label) {check(ok,label,GetLastError());}
std::wstring module_path(HMODULE module) {
    std::wstring value(32768,L'\0');auto size=GetModuleFileNameW(module,&value[0],static_cast<DWORD>(value.size()));
    win_check(size>0&&size<value.size(),"module_path");value.resize(size);return value;
}
std::wstring parent(const std::wstring& path) {return path.substr(0,path.find_last_of(L"\\/"));}
std::string read_small(const std::wstring& path) {
    Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
    win_check(h.value!=INVALID_HANDLE_VALUE,"fixture_read_open");
    char data[256];DWORD count=0;win_check(ReadFile(h.value,data,sizeof(data),&count,nullptr)!=FALSE,"fixture_read");return {data,count};
}
void write_new(const std::wstring& path,const std::string& data) {
    Handle h(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr));
    win_check(h.value!=INVALID_HANDLE_VALUE,"fixture_write_new");DWORD count=0;
    win_check(WriteFile(h.value,data.data(),static_cast<DWORD>(data.size()),&count,nullptr)&&count==data.size(),"fixture_write");
}
std::wstring canonical(const std::wstring& path,bool directory=false) {
    Handle h(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
                         OPEN_EXISTING,directory?FILE_FLAG_BACKUP_SEMANTICS:0,nullptr));
    win_check(h.value!=INVALID_HANDLE_VALUE,"canonical_open");std::wstring out(32768,L'\0');
    auto n=GetFinalPathNameByHandleW(h.value,&out[0],static_cast<DWORD>(out.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    win_check(n>0&&n<out.size(),"canonical_path");out.resize(n);return out;
}
bool same(const std::wstring& a,const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
struct SavedFile {
    std::wstring original,saved;
    explicit SavedFile(std::wstring p):original(std::move(p)),saved(original+L".fixture-held") {
        win_check(MoveFileExW(original.c_str(),saved.c_str(),0)!=FALSE,"save_fixture_file");
    }
    ~SavedFile() {
        DeleteFileW(original.c_str());
        if(!MoveFileExW(saved.c_str(),original.c_str(),0))std::cerr<<"fixture restore failed win32="<<GetLastError()<<'\n';
    }
};
PackageStatus load(HMODULE module,std::shared_ptr<const PackageConfig>& out,PackageError& error) {
    constexpr DWORD marker=0x7351ab24;SetLastError(marker);
    auto status=load_package_config(module,out,error);auto after=GetLastError();
    check(after==marker,"reader_preserves_last_error",after);
    std::cout<<"{\"event\":\"load\",\"status\":"<<unsigned(status)<<",\"error\":"<<unsigned(error.code)<<",\"win32\":"<<error.system<<"}\n";
    return status;
}
void expect_error(HMODULE module,PackageErrorCode expected,const char* label) {
    std::shared_ptr<const PackageConfig> out=std::make_shared<PackageConfig>();auto original=out;PackageError error;
    check(load(module,out,error)==PackageStatus::error&&error.code==expected,label,error.system);
    check(out==original,"error_preserves_output");
}
void expect_disabled(HMODULE module,const char* label) {
    std::shared_ptr<const PackageConfig> out=std::make_shared<PackageConfig>();PackageError error;
    check(load(module,out,error)==PackageStatus::disabled&&!out&&error.code==PackageErrorCode::none,label,error.system);
}
void deny_write(const std::wstring& path) {
    Handle h(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
    auto e=GetLastError();check(h.value==INVALID_HANDLE_VALUE&&e==ERROR_SHARING_VIOLATION,"pinned_write_denied",e);
}
std::string json_path(const std::wstring& path) {
    auto size=WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)throw std::runtime_error("fixture UTF16 path conversion");
    std::string utf8(static_cast<std::size_t>(size),'\0');
    if(WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),&utf8[0],size,nullptr,nullptr)!=size)
        throw std::runtime_error("fixture UTF16 path conversion");
    std::string out="\"";
    for(unsigned char c:utf8) {
        if(c=='\\'||c=='\"'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32) {
            const char* hex="0123456789abcdef";out+="\\u00";out+=hex[c>>4];out+=hex[c&15];
        } else out+=static_cast<char>(c);
    }
    out+='\"';return out;
}
void deny_rename(const std::wstring& path,bool directory=false) {
    auto target=path+L".fixture-renamed";
    auto moved=MoveFileExW(path.c_str(),target.c_str(),0)!=FALSE;
    // Win32 LastError is not meaningful on successful MoveFileExW. Preserve
    // the actual BOOL and log paths before any restoration changes the witness.
    auto error=moved?ERROR_SUCCESS:GetLastError();
    auto source_exists=GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;
    auto target_exists=GetFileAttributesW(target.c_str())!=INVALID_FILE_ATTRIBUTES;
    std::cout<<"{\"event\":\"rename_attempt\",\"directory\":"<<(directory?"true":"false")
        <<",\"source\":"<<json_path(path)<<",\"target\":"<<json_path(target)
        <<",\"moved\":"<<(moved?"true":"false")<<",\"source_exists\":"<<(source_exists?"true":"false")
        <<",\"target_exists\":"<<(target_exists?"true":"false")<<",\"win32\":";
    if(moved)std::cout<<"null";else std::cout<<error;
    std::cout<<"}\n";
    if(moved) {
        auto restored=MoveFileExW(target.c_str(),path.c_str(),0)!=FALSE;
        auto restore_error=restored?ERROR_SUCCESS:GetLastError();
        std::cout<<"{\"event\":\"unexpected_rename_restore\",\"source\":"<<json_path(target)
            <<",\"target\":"<<json_path(path)<<",\"restored\":"<<(restored?"true":"false")
            <<",\"win32\":"<<restore_error<<"}\n";
        check(restored,"unexpected_rename_restored",restore_error);
    }
    check(!moved&&(error==ERROR_SHARING_VIOLATION||error==ERROR_ACCESS_DENIED),
          directory?"pinned_directory_rename_denied":"pinned_file_rename_denied",error);
}
void allow_write(const std::wstring& path) {
    Handle h(CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
    win_check(h.value!=INVALID_HANDLE_VALUE,"released_write_open");
    char original=0;DWORD actual=0;win_check(ReadFile(h.value,&original,1,&actual,nullptr)&&actual==1,"released_read_byte");
    LARGE_INTEGER zero{};win_check(SetFilePointerEx(h.value,zero,nullptr,FILE_BEGIN)!=FALSE,"released_rewind");
    win_check(WriteFile(h.value,&original,1,&actual,nullptr)&&actual==1,"released_actual_write");
}
void allow_rename(const std::wstring& path) {
    auto target=path+L".fixture-renamed";
    win_check(MoveFileExW(path.c_str(),target.c_str(),0)!=FALSE,"released_rename");
    win_check(MoveFileExW(target.c_str(),path.c_str(),0)!=FALSE,"released_rename_restore");
}
using Symlink=BOOLEAN(WINAPI*)(LPCWSTR,LPCWSTR,DWORD);
bool symlink(Symlink api,const std::wstring& link,const std::wstring& target,bool directory) {
    if(!api){++skips;std::cout<<"{\"event\":\"unsupported\",\"name\":\"CreateSymbolicLinkW\",\"win32\":127}\n";return false;}
    DWORD flags=directory?SYMBOLIC_LINK_FLAG_DIRECTORY:0;
    bool ok=api(link.c_str(),target.c_str(),flags|2)!=FALSE;DWORD e=GetLastError();
    if(!ok&&e==ERROR_INVALID_PARAMETER){ok=api(link.c_str(),target.c_str(),flags)!=FALSE;e=GetLastError();}
    if(!ok&&(e==ERROR_PRIVILEGE_NOT_HELD||e==ERROR_NOT_SUPPORTED||e==ERROR_CALL_NOT_IMPLEMENTED||e==ERROR_INVALID_FUNCTION||e==ERROR_INVALID_PARAMETER)) {
        ++skips;std::cout<<"{\"event\":\"unsupported\",\"name\":\"symlink_privilege_or_backend\",\"directory\":"<<(directory?"true":"false")<<",\"win32\":"<<e<<"}\n";return false;
    }
    check(ok,"create_reparse_fixture",e);
    auto attr=GetFileAttributesW(link.c_str());
    win_check(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT),"symlink_exposes_reparse_attribute");return true;
}
}
int wmain() {
    std::cout.setf(std::ios::unitbuf);
    try {
        HMODULE module=nullptr;
        win_check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
              reinterpret_cast<LPCWSTR>(&checks),&module)!=FALSE,"pin_self_module");
        auto exe=module_path(module);auto root=parent(exe);
        check(exe.substr(exe.find_last_of(L"\\/")+1)==L"package_config_windows.exe","fixture_executable_name");
        check(root.find(L"Game relocated Ω日 space")!=std::wstring::npos,"unicode_space_relocation");
        check(read_small(root+L"\\.x3-media-package-config-fixture")=="x3-media-package-config-windows-fixture-v1\n","fixture_marker");
        auto cwd=root+L"\\unrelated cwd";
        win_check(SetCurrentDirectoryW(cwd.c_str())!=FALSE,"set_unrelated_cwd");
        check(read_small(cwd+L"\\x3-modern-install.json")=="{\"media\":null}\n","conflicting_cwd_selection");
        std::shared_ptr<const PackageConfig> config;PackageError error;
        check(load(module,config,error)==PackageStatus::ready&&config&&config->sources.size()==1,"module_relative_ready",error.system);
        auto manifest=config->provider_manifest;auto asset=config->sources[0].path;
        auto provider=parent(manifest);auto module_file=provider+L"\\LAVVideo.ax";
        auto install=root+L"\\x3-modern-install.json";auto package=provider+L"\\package.json";auto source_record=parent(asset)+L"\\source.json";
        check(config->sources[0].id==2&&config->sources[0].effective_flags==8,"exact_source_eligibility");
        check(same(manifest,canonical(root+L"\\x3-modern-media\\providers\\fixture-provider-v1\\provider.manifest")),"manifest_final_path_identity");
        check(asset.find(canonical(root,true)+L"\\")==0,"asset_final_path_under_module_root");
        for(const auto& path:{manifest,asset,module_file})check(!read_small(path).empty(),"reader_shared_read_compatible");
        for(const auto& path:{install,package,source_record,manifest,asset,module_file}){deny_write(path);deny_rename(path);}
        deny_rename(provider,true);
        auto last_owner=config;config.reset();deny_write(module_file);deny_rename(provider,true);
        last_owner.reset();
        for(const auto& path:{install,package,source_record,manifest,asset,module_file}){allow_write(path);allow_rename(path);}
        allow_rename(provider);
        {
            auto journal=root+L"\\x3-modern-transaction.json";write_new(journal,"fixture unresolved transaction\n");
            expect_error(module,PackageErrorCode::journal,"unresolved_journal_refused");
            win_check(DeleteFileW(journal.c_str())!=FALSE,"remove_fixture_journal");
        }
        {SavedFile save(install);expect_disabled(module,"missing_install_disabled");}
        {SavedFile save(install);write_new(install,"{\"schema\":2}\n");expect_disabled(module,"missing_media_disabled");}
        for(const auto& path:{package,source_record,module_file,asset}) {
            SavedFile save(path);expect_error(module,PackageErrorCode::missing_file,"missing_selected_file_refused");
        }
        {SavedFile save(install);write_new(install,std::string(package_document_limit+1,' '));expect_error(module,PackageErrorCode::too_large,"large_record_refused");}
        {SavedFile save(package);write_new(package,"{}\n");expect_error(module,PackageErrorCode::identity,"record_digest_mismatch_refused");}
        // File and directory reparse escapes use only our tiny owned fixture tree.
        auto symbol=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateSymbolicLinkW");
        Symlink api=nullptr;static_assert(sizeof(api)==sizeof(symbol),"Windows function pointer width");
        std::memcpy(&api,&symbol,sizeof(api));
        {
            SavedFile save(module_file);auto target=root+L"\\outside-selected-provider.bin";write_new(target,"outside provider fixture\n");
            if(symlink(api,module_file,target,false)) {
                expect_error(module,PackageErrorCode::reparse_point,"file_reparse_escape_refused");
                win_check(DeleteFileW(module_file.c_str())!=FALSE,"remove_file_reparse");
            }
            win_check(DeleteFileW(target.c_str())!=FALSE,"remove_file_reparse_target");
        }
        {
            auto target=root+L"\\outside-selected-provider";
            win_check(MoveFileExW(provider.c_str(),target.c_str(),0)!=FALSE,"move_directory_reparse_target");
            try {
                if(symlink(api,provider,target,true)) {
                    expect_error(module,PackageErrorCode::reparse_point,"directory_reparse_escape_refused");
                    win_check(RemoveDirectoryW(provider.c_str())!=FALSE,"remove_directory_reparse");
                }
            } catch(...) {RemoveDirectoryW(provider.c_str());MoveFileExW(target.c_str(),provider.c_str(),0);throw;}
            win_check(MoveFileExW(target.c_str(),provider.c_str(),0)!=FALSE,"restore_directory_reparse_target");
        }
        check(load(module,config,error)==PackageStatus::ready,"restored_package_ready",error.system);config.reset();
        allow_write(module_file);allow_rename(provider);
        std::cout<<"{\"event\":\"summary\",\"pass\":true,\"checks\":"<<checks<<",\"unsupported\":"<<skips<<",\"fixture\":\"synthetic-reader-only\"}\n";return 0;
    } catch(const std::exception& e) {
        std::cerr<<"fixture failed: "<<e.what()<<'\n';
        std::cout<<"{\"event\":\"summary\",\"pass\":false,\"checks\":"<<checks<<",\"unsupported\":"<<skips<<"}\n";return 1;
    }
}
