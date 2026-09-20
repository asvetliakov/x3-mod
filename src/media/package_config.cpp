#include "package_config.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace x3m::media {
namespace {
struct Failure { PackageErrorCode code; };
[[noreturn]] void fail(PackageErrorCode code) { throw Failure{code}; }
void require(bool value, PackageErrorCode code = PackageErrorCode::invalid_schema) {
    if (!value) fail(code);
}
struct Json {
    enum Type { object, array, string, number, boolean, null_value } type = null_value;
    std::map<std::string, Json> members;
    std::vector<Json> elements;
    std::string text;
    bool flag = false;
};
// Strict UTF-8, excluding overlong sequences, surrogate scalars and > U+10FFFF.
std::uint32_t scalar(const std::string& s, std::size_t& p) {
    require(p < s.size(), PackageErrorCode::malformed_json);
    auto lead = static_cast<unsigned char>(s[p++]);
    if (lead < 128) return lead;
    unsigned count = lead >= 0xc2 && lead <= 0xdf ? 1 :
                     lead >= 0xe0 && lead <= 0xef ? 2 :
                     lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
    require(count != 0 && s.size() - p >= count, PackageErrorCode::malformed_json);
    std::uint32_t result = lead & ((1u << (6 - count)) - 1);
    for (unsigned i = 0; i < count; ++i) {
        auto byte = static_cast<unsigned char>(s[p++]);
        require((byte & 0xc0) == 0x80, PackageErrorCode::malformed_json);
        result = (result << 6) | (byte & 63);
    }
    require(result >= (count == 1 ? 128u : count == 2 ? 2048u : 65536u) &&
            result <= 0x10ffff && !(result >= 0xd800 && result <= 0xdfff),
            PackageErrorCode::malformed_json);
    return result;
}
void append_utf8(std::string& out, std::uint32_t c) {
    if (c < 128) out += static_cast<char>(c);
    else if (c < 2048) { out += char(0xc0 | (c >> 6)); out += char(0x80 | (c & 63)); }
    else if (c < 65536) {
        out += char(0xe0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63));
    } else {
        out += char(0xf0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 63));
        out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63));
    }
}
std::uint64_t uint_text(const std::string& s) {
    require(!s.empty()); std::uint64_t value = 0;
    for (char c : s) {
        require(c >= '0' && c <= '9');
        unsigned digit = unsigned(c - '0');
        require(value <= (std::numeric_limits<std::uint64_t>::max() - digit) / 10);
        value = value * 10 + digit;
    }
    return value;
}
class Parser {
    const std::string& bytes; std::size_t pos = 0; unsigned nodes = 0;
    void whitespace() { while (pos < bytes.size() && (bytes[pos]==' ' || bytes[pos]=='\t' || bytes[pos]=='\r' || bytes[pos]=='\n')) ++pos; }
    char take() { require(pos < bytes.size(), PackageErrorCode::malformed_json); return bytes[pos++]; }
    unsigned hex4() {
        unsigned n = 0;
        for (unsigned i=0;i<4;++i) {
            char c=take(); unsigned d = c>='0'&&c<='9' ? unsigned(c-'0') : c>='a'&&c<='f' ? unsigned(c-'a'+10) : c>='A'&&c<='F' ? unsigned(c-'A'+10) : 16;
            require(d<16,PackageErrorCode::malformed_json); n=(n<<4)|d;
        } return n;
    }
    std::string quoted() {
        require(take()=='"',PackageErrorCode::malformed_json); std::string out;
        while (true) {
            auto start=pos; auto c=scalar(bytes,pos);
            if(c=='"') return out;
            require(c>=32,PackageErrorCode::malformed_json);
            if(c!='\\') {out.append(bytes,start,pos-start);continue;}
            char escape=take();
            switch(escape) {
            case '"': case '\\': case '/': out+=escape;break;
            case 'b':out+='\b';break; case 'f':out+='\f';break; case 'n':out+='\n';break;
            case 'r':out+='\r';break; case 't':out+='\t';break;
            case 'u': {
                auto cp=hex4();
                if(cp>=0xd800&&cp<=0xdbff) {
                    require(take()=='\\'&&take()=='u',PackageErrorCode::malformed_json);
                    auto low=hex4();require(low>=0xdc00&&low<=0xdfff,PackageErrorCode::malformed_json);
                    cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
                } else require(!(cp>=0xdc00&&cp<=0xdfff),PackageErrorCode::malformed_json);
                append_utf8(out,cp);break;
            }
            default:fail(PackageErrorCode::malformed_json);
            }
        }
    }
    Json value(unsigned depth) {
        require(depth<=16 && ++nodes<=4096,PackageErrorCode::malformed_json);
        whitespace(); require(pos<bytes.size(),PackageErrorCode::malformed_json); Json j;
        char c=bytes[pos];
        if(c=='{'||c=='[') {
            ++pos; j.type=c=='{'?Json::object:Json::array; whitespace(); char close=c=='{'?'}':']';
            if(pos<bytes.size()&&bytes[pos]==close){++pos;return j;}
            while(true) {
                whitespace();
                if(c=='{') {
                    auto key=quoted(); whitespace();require(take()==':',PackageErrorCode::malformed_json);
                    auto item=value(depth+1);
                    require(j.members.emplace(std::move(key),std::move(item)).second,PackageErrorCode::malformed_json);
                } else j.elements.push_back(value(depth+1));
                whitespace();auto sep=take();if(sep==close)return j;
                require(sep==',',PackageErrorCode::malformed_json);
            }
        }
        if(c=='"') {j.type=Json::string;j.text=quoted();return j;}
        if(c=='t'||c=='f'||c=='n') {
            const char* token=c=='t'?"true":c=='f'?"false":"null";
            for(const char* p=token;*p;++p)require(take()==*p,PackageErrorCode::malformed_json);
            j.type=c=='n'?Json::null_value:Json::boolean;j.flag=c=='t';return j;
        }
        // Runtime records use only unsigned integer numbers. A minus, fraction or
        // exponent is rejected, including in uninterpreted provenance fields.
        require(c>='0'&&c<='9',PackageErrorCode::malformed_json);
        auto start=pos++; if(c!='0')while(pos<bytes.size()&&bytes[pos]>='0'&&bytes[pos]<='9')++pos;
        j.type=Json::number;j.text=bytes.substr(start,pos-start);uint_text(j.text);return j;
    }
public:
    explicit Parser(const std::string& s):bytes(s){}
    Json parse() {
        require(bytes.size()<=package_document_limit,PackageErrorCode::too_large);
        auto result=value(0);whitespace();require(pos==bytes.size(),PackageErrorCode::malformed_json);return result;
    }
};
const Json& field(const Json& j,const char* name) {
    require(j.type==Json::object);auto found=j.members.find(name);require(found!=j.members.end());return found->second;
}
std::string string(const Json& j) {require(j.type==Json::string);return j.text;}
std::string text_field(const Json& j,const char* key) {return string(field(j,key));}
void equals(const Json& j,const char* key,const char* expected) {require(text_field(j,key)==expected);}
std::uint64_t number(const Json& j) {require(j.type==Json::number);return uint_text(j.text);}
void integer(const Json& j,const char* key,std::uint64_t n) {require(number(field(j,key))==n);}
const std::vector<Json>& array(const Json& j) {require(j.type==Json::array);return j.elements;}
std::string digest(const Json& j,const char* name) {
    auto s=text_field(j,name);require(s.size()==64);
    for(char c:s)require((c>='0'&&c<='9')||(c>='a'&&c<='f'));
    return s;
}
std::string lower(std::string s) {for(char& c:s)if(c>='A'&&c<='Z')c+=char('a'-'A');return s;}
void component(const std::string& part) {
    require(!part.empty() && part.size()<=240 && part!="." && part!=".." && part.back()!='.' && part.back()!=' ',PackageErrorCode::invalid_path);
    for(std::size_t p=0;p<part.size();) {
        auto c=scalar(part,p);
        require(c>=32 && c!=127 && c!='<'&&c!='>'&&c!=':'&&c!='"'&&c!='|'&&c!='?'&&c!='*'&&c!='\\'&&c!='/',PackageErrorCode::invalid_path);
    }
    auto base=lower(part.substr(0,part.find('.')));
    require(base!="con"&&base!="prn"&&base!="aux"&&base!="nul"&&base!="clock$" &&
            !(base.size()==4 && (base.substr(0,3)=="com"||base.substr(0,3)=="lpt") && base[3]>='0'&&base[3]<='9'),PackageErrorCode::invalid_path);
}
void path(const std::string& s) {
    require(!s.empty()&&s.size()<=2000,PackageErrorCode::invalid_path);
    std::size_t begin=0;while(true){auto end=s.find('/',begin);component(s.substr(begin,end==std::string::npos?end:end-begin));if(end==std::string::npos)break;begin=end+1;}
}
// SHA-256 is used only for the bounded JSON records; never for providers/video.
std::string sha256(const std::string& bytes) {
    constexpr std::uint32_t k[]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::uint32_t state[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    auto rotate=[](std::uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));};
    const std::size_t blocks=(bytes.size()+9+63)/64;
    for(std::size_t block=0;block<blocks;++block) {
        std::uint32_t w[64]{};
        for(unsigned i=0;i<64;++i) {
            auto pos=block*64+i;unsigned char c=0;
            if(pos<bytes.size())c=static_cast<unsigned char>(bytes[pos]);
            else if(pos==bytes.size())c=0x80;
            else if(pos>=blocks*64-8)c=static_cast<unsigned char>((std::uint64_t(bytes.size())*8) >> ((blocks*64-1-pos)*8));
            w[i/4]|=std::uint32_t(c)<<(24-(i%4)*8);
        }
        for(unsigned i=16;i<64;++i) {
            auto s0=rotate(w[i-15],7)^rotate(w[i-15],18)^(w[i-15]>>3);
            auto s1=rotate(w[i-2],17)^rotate(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for(unsigned i=0;i<64;++i) {
            auto t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            auto t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
    const char* hex="0123456789abcdef";std::string out;out.reserve(64);
    for(auto n:state)for(int shift=28;shift>=0;shift-=4)out+=hex[(n>>shift)&15];
    return out;
}
Json document(PackageInput& input,const std::string& name,bool& missing,PackageError& error,const std::string& expected={}) {
    path(name);std::string bytes;
    if(!input.read_small(name,bytes,missing,error)) {if(error.code==PackageErrorCode::none)error.code=PackageErrorCode::io;throw Failure{error.code};}
    if(missing)return {};
    require(bytes.size()<=package_document_limit,PackageErrorCode::too_large);
    if(!expected.empty())require(sha256(bytes)==expected,PackageErrorCode::identity);
    return Parser(bytes).parse();
}
Json mandatory(PackageInput& input,const std::string& name,PackageError& error,const std::string& expected) {
    bool missing=false;auto j=document(input,name,missing,error,expected);require(!missing,PackageErrorCode::missing_file);return j;
}
std::wstring resolve(PackageInput& input,const std::string& name,PackageError& error) {
    path(name);std::wstring result;
    if(!input.resolve_file(name,result,error)) {if(error.code==PackageErrorCode::none)error.code=PackageErrorCode::io;throw Failure{error.code};}
    require(!result.empty(),PackageErrorCode::identity);return result;
}
void source(const Json& row) {
    integer(row,"id",2);integer(row,"effective_flags",8);equals(row,"original_relative","mov/00002.dat");
    auto original=digest(row,"original_sha256");digest(row,"asset_sha256");digest(row,"derivation_record_sha256");
    require(number(field(row,"original_bytes"))>0&&number(field(row,"asset_bytes"))>0);
    equals(row,"codec","mpeg1video");equals(row,"timeline","generated_timestamps");
    require(text_field(row,"asset")=="x3-modern-media/sources/"+original+"/00002.mkv",PackageErrorCode::invalid_path);
}
void same_source(const Json& a,const Json& b) {
    for(auto name:{"original_relative","original_sha256","asset","asset_sha256","codec","timeline","derivation_record_sha256"})
        require(text_field(a,name)==text_field(b,name));
    for(auto name:{"id","effective_flags","original_bytes","asset_bytes"})require(number(field(a,name))==number(field(b,name)));
}
struct Expected {const char* name;const char* role;const char* origin;};
constexpr Expected cohort[]={
    {"LAVSplitter.ax","source","official"},{"LAVVideo.ax","video","official"},
    {"avfilter-lav-11.dll","dependency","official"},{"swscale-lav-9.dll","dependency","official"},{"libbluray.dll","dependency","official"},
    {"avcodec-lav-62.dll","dependency","strict"},{"avformat-lav-62.dll","dependency","strict"},
    {"avutil-lav-60.dll","dependency","strict"},{"swresample-lav-6.dll","dependency","strict"},
    {"provider.manifest","manifest","official"},{"LAVFilters.Dependencies.manifest","manifest","official"}
};
PackageStatus read_impl(const std::shared_ptr<PackageInput>& input,std::shared_ptr<PackageConfig>& result,PackageError& error) {
    require(bool(input));if(!input->journal_absent(error)){if(error.code==PackageErrorCode::none)error.code=PackageErrorCode::journal;throw Failure{error.code};}
    bool missing=false;auto install=document(*input,"x3-modern-install.json",missing,error);
    if(missing)return PackageStatus::disabled;
    require(install.type==Json::object);
    auto media_it=install.members.find("media");if(media_it==install.members.end())return PackageStatus::disabled;
    integer(install,"schema",2);const auto& media=media_it->second;
    auto id=text_field(media,"package_id");component(id);
    const auto prefix="providers/"+id+"/";
    const auto package_relative="x3-modern-media/"+prefix+"package.json";
    require(text_field(media,"package_record_relative")==package_relative,PackageErrorCode::invalid_path);digest(media,"package_record_sha256");
    auto package=mandatory(*input,package_relative,error,digest(media,"package_record_sha256"));
    integer(package,"schema",1);equals(package,"kind","x3-owned-media-package");equals(package,"layout","installed");equals(package,"path_base","media_root");
    require(text_field(package,"package_id")==id);equals(package,"architecture","x86");equals(package,"profile","lav081-strict-mpeg1-rgb32-v1");
    equals(package,"scope","local_qualification");const auto& distribution=field(package,"distribution_qualified");require(distribution.type==Json::boolean&&!distribution.flag);
    const auto& provider=field(package,"provider");require(text_field(provider,"directory")=="providers/"+id);
    require(text_field(provider,"manifest")==prefix+"provider.manifest",PackageErrorCode::invalid_path);
    require(lower(text_field(provider,"source_clsid"))=="{b98d13e7-55db-4385-a33d-09fd1ba26338}");
    require(lower(text_field(provider,"video_clsid"))=="{ee30215d-164f-4a92-a4eb-9d4c13390f9f}");
    const auto& files=field(provider,"files");require(files.type==Json::object&&files.members.size()==14);
    std::vector<std::string> seen;
    for(const auto& pair:files.members) {
        path(pair.first);auto folded=lower(pair.first);require(std::find(seen.begin(),seen.end(),folded)==seen.end(),PackageErrorCode::invalid_path);seen.push_back(folded);
        const auto& row=pair.second;require(text_field(row,"path")==prefix+pair.first,PackageErrorCode::invalid_path);
        digest(row,"sha256");require(number(field(row,"bytes"))>0);
        auto expected=std::find_if(std::begin(cohort),std::end(cohort),[&](const Expected& e){return pair.first==e.name;});
        if(expected!=std::end(cohort)){equals(row,"role",expected->role);equals(row,"origin",expected->origin);}
        else {require(pair.first=="notices/COPYING"||pair.first=="notices/README.md"||pair.first=="notices/FFmpeg-LICENSE.md");equals(row,"role","notice");equals(row,"origin","notice");}
        resolve(*input,"x3-modern-media/"+prefix+pair.first,error);
    }
    for(const auto& expected:cohort)require(files.members.count(expected.name)==1);
    const auto& selections=array(field(media,"sources"));const auto& sources=array(field(package,"sources"));require(selections.size()==1&&sources.size()==1);
    const auto& selection=selections[0];integer(selection,"id",2);integer(selection,"effective_flags",8);digest(selection,"source_record_sha256");
    source(sources[0]);auto source_record="x3-modern-media/sources/"+text_field(sources[0],"original_sha256")+"/source.json";
    require(text_field(selection,"source_record_relative")==source_record,PackageErrorCode::invalid_path);
    auto record=mandatory(*input,source_record,error,digest(selection,"source_record_sha256"));integer(record,"schema",1);equals(record,"kind","x3-owned-media-source");equals(record,"layout","installed");equals(record,"path_base","game_root");source(record);same_source(record,sources[0]);
    auto config=std::make_shared<PackageConfig>();config->provider_manifest=resolve(*input,"x3-modern-media/"+prefix+"provider.manifest",error);
    config->sources.push_back({2,8,resolve(*input,text_field(record,"asset"),error)});
    if(!input->journal_absent(error)){if(error.code==PackageErrorCode::none)error.code=PackageErrorCode::journal;throw Failure{error.code};}
    result=std::move(config);return PackageStatus::ready;
}
} // namespace
PackageStatus read_package_config(std::shared_ptr<PackageInput> input,std::shared_ptr<const PackageConfig>& output,PackageError& error) noexcept {
    error={};
    try {std::shared_ptr<PackageConfig> result;auto status=read_impl(input,result,error);if(result)result->input_=std::move(input);output=std::move(result);return status;}
    catch(const Failure& f){error.code=f.code;}
    catch(const std::bad_alloc&){error.code=PackageErrorCode::allocation;}
    catch(...){error.code=PackageErrorCode::io;}
    return PackageStatus::error;
}
#ifdef _WIN32
namespace {
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    Handle()=default; explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(Handle&& other) noexcept:value(other.value){other.value=INVALID_HANDLE_VALUE;}
    Handle& operator=(Handle&& other) noexcept {
        if(this!=&other){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=other.value;other.value=INVALID_HANDLE_VALUE;}return *this;
    }
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
std::wstring wide(const std::string& s) {
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    require(size>0,PackageErrorCode::invalid_path);std::wstring result(size,L'\0');
    require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),&result[0],size)==size,PackageErrorCode::invalid_path);
    std::replace(result.begin(),result.end(),L'/',L'\\');return result;
}
bool os_error(PackageError& error,PackageErrorCode code=PackageErrorCode::io) {
    error={code,GetLastError()};return false;
}
std::wstring final_name(HANDLE handle) {
    std::wstring name(32768,L'\0');
    auto size=GetFinalPathNameByHandleW(handle,&name[0],static_cast<DWORD>(name.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    require(size>0&&size<name.size(),PackageErrorCode::identity);name.resize(size);return name;
}
bool same_name(const std::wstring& a,const std::wstring& b) {
    return CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;
}
class WindowsInput final:public PackageInput {
    struct Entry {Handle handle;std::wstring absolute;bool directory=false;};
    Handle root_;std::wstring root_path_;std::map<std::string,Entry> entries_;
    bool open(const std::string& relative,Entry*& result,bool& missing,PackageError& error) {
        path(relative);missing=false;std::size_t start=0;
        while(true) {
            auto end=relative.find('/',start);bool directory=end!=std::string::npos;
            auto key=relative.substr(0,end);auto found=entries_.find(key);
            if(found==entries_.end()) {
                auto absolute=root_path_+L"\\"+wide(key);
                // Attribute-only opens do not participate in sharing restrictions
                // (CreateFileW dwShareMode). LIST_DIRECTORY requests read access so
                // omitting SHARE_DELETE protects the selected directory's name.
                // Files deny concurrent writes; DLL/COM/source readers share READ.
                Handle h(CreateFileW(absolute.c_str(),directory?(FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES):GENERIC_READ,
                    directory?(FILE_SHARE_READ|FILE_SHARE_WRITE):FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr));
                if(h.value==INVALID_HANDLE_VALUE) {
                    auto code=GetLastError();if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND){missing=true;return true;}
                    return os_error(error);
                }
                BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(h.value,&info))return os_error(error);
                if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){error={PackageErrorCode::reparse_point,0};return false;}
                if(bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory){error={PackageErrorCode::identity,0};return false;}
                if(!same_name(final_name(h.value),absolute)){error={PackageErrorCode::identity,0};return false;}
                found=entries_.emplace(key,Entry{std::move(h),std::move(absolute),directory}).first;
            } else if(found->second.directory!=directory){error={PackageErrorCode::identity,0};return false;}
            if(!directory){result=&found->second;return true;}start=end+1;
        }
    }
public:
    bool initialize(HMODULE module,PackageError& error) {
        if(!module){error={PackageErrorCode::identity,0};return false;}
        std::wstring module_path(32768,L'\0');
        auto size=GetModuleFileNameW(module,&module_path[0],static_cast<DWORD>(module_path.size()));
        if(!size||size>=module_path.size())return os_error(error,PackageErrorCode::identity);
        module_path.resize(size);auto slash=module_path.find_last_of(L"\\/");
        if(slash==std::wstring::npos){error={PackageErrorCode::identity,0};return false;}
        auto directory=module_path.substr(0,slash);
        root_=Handle(CreateFileW(directory.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
            OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(root_.value==INVALID_HANDLE_VALUE)return os_error(error);
        BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(root_.value,&info))return os_error(error);
        if(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
            error={PackageErrorCode::reparse_point,0};return false;
        }
        root_path_=final_name(root_.value);
        while(!root_path_.empty()&&root_path_.back()==L'\\')root_path_.pop_back();
        return true;
    }
    bool journal_absent(PackageError& error) override {
        auto attributes=GetFileAttributesW((root_path_+L"\\x3-modern-transaction.json").c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES){error={PackageErrorCode::journal,0};return false;}
        if(GetLastError()==ERROR_FILE_NOT_FOUND)return true;
        return os_error(error);
    }
    bool read_small(const std::string& relative,std::string& bytes,bool& missing,PackageError& error) override {
        Entry* entry=nullptr;if(!open(relative,entry,missing,error)||missing)return missing;
        LARGE_INTEGER size{};if(!GetFileSizeEx(entry->handle.value,&size))return os_error(error);
        if(size.QuadPart<0||size.QuadPart>static_cast<LONGLONG>(package_document_limit)){error={PackageErrorCode::too_large,0};return false;}
        std::string buffer(static_cast<std::size_t>(size.QuadPart),'\0');
        LARGE_INTEGER offset{};if(!SetFilePointerEx(entry->handle.value,offset,nullptr,FILE_BEGIN))return os_error(error);
        DWORD actual=0;if(!ReadFile(entry->handle.value,buffer.empty()?nullptr:&buffer[0],static_cast<DWORD>(buffer.size()),&actual,nullptr))return os_error(error);
        if(actual!=buffer.size()){error={PackageErrorCode::identity,0};return false;}
        bytes=std::move(buffer);return true;
    }
    bool resolve_file(const std::string& relative,std::wstring& absolute,PackageError& error) override {
        Entry* entry=nullptr;bool missing=false;if(!open(relative,entry,missing,error))return false;
        if(missing){error={PackageErrorCode::missing_file,0};return false;}absolute=entry->absolute;return true;
    }
};
} // namespace
PackageStatus load_package_config(HMODULE module,std::shared_ptr<const PackageConfig>& output,PackageError& error) noexcept {
    const auto last_error=GetLastError();PackageStatus status=PackageStatus::error;error={};
    try {auto input=std::make_shared<WindowsInput>();if(input->initialize(module,error))status=read_package_config(std::move(input),output,error);}
    catch(const Failure& f){error.code=f.code;}
    catch(const std::bad_alloc&){error.code=PackageErrorCode::allocation;}
    catch(...){error.code=PackageErrorCode::io;}
    SetLastError(last_error);return status;
}
#endif
} // namespace x3m::media
