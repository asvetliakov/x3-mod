// Compile this fixture as one translation unit: the production implementation is
// included to test the private bounded parser/hash/path helpers directly, while
// all package acceptance cases invoke the same public reader used by Windows.
#include "../../src/media/package_config.cpp"
#include <iostream>
#include <fstream>
#include <iterator>
#include <set>
using namespace x3m::media;
namespace {
unsigned checks=0;
void check(bool ok,const char* name) {++checks;if(!ok){std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}}
const std::string hash(64,'a');
const std::string source_path="x3-modern-media/sources/"+hash+"/source.json";
const std::string package_path="x3-modern-media/providers/provider-v1/package.json";
std::string source_row() {
    return "\"id\":2,\"effective_flags\":8,\"original_relative\":\"mov/00002.dat\",\"original_sha256\":\""+hash+
    "\",\"original_bytes\":123,\"asset\":\"x3-modern-media/sources/"+hash+"/00002.mkv\",\"asset_sha256\":\""+hash+
    "\",\"asset_bytes\":456,\"codec\":\"mpeg1video\",\"timeline\":\"generated_timestamps\",\"derivation_record_sha256\":\""+hash+"\"";
}
struct MemoryInput final:PackageInput {
    std::map<std::string,std::string> docs;
    std::set<std::string> files;
    std::wstring root=L"C:\\Game space Ω日";
    bool journal=false,late_journal=false,throw_read=false;
    unsigned journal_calls=0,reads=0,resolves=0;
    bool journal_absent(PackageError& e) override {
        if(journal||(late_journal&&++journal_calls==2)){e.code=PackageErrorCode::journal;return false;}return true;
    }
    bool read_small(const std::string& p,std::string& b,bool& missing,PackageError&) override {
        ++reads;if(throw_read)throw std::runtime_error("test");auto it=docs.find(p);missing=it==docs.end();if(!missing)b=it->second;return true;
    }
    bool resolve_file(const std::string& p,std::wstring& out,PackageError& e) override {
        ++resolves;if(!files.count(p)){e.code=PackageErrorCode::missing_file;return false;}
        out=root+L"\\"+std::wstring(p.begin(),p.end());return true;
    }
    void rebind() {
        docs["x3-modern-install.json"]="{\"schema\":2,\"media\":{\"package_id\":\"provider-v1\",\"package_record_relative\":\""+package_path+
        "\",\"package_record_sha256\":\""+sha256(docs[package_path])+"\",\"sources\":[{\"id\":2,\"effective_flags\":8,\"source_record_relative\":\""+
        source_path+"\",\"source_record_sha256\":\""+sha256(docs[source_path])+"\"}]}}";
    }
    MemoryInput() {
        docs[source_path]="{\"schema\":1,\"kind\":\"x3-owned-media-source\",\"layout\":\"installed\",\"path_base\":\"game_root\","+source_row()+"}";
        std::string rows;
        auto add=[&](const char* name,const char* role,const char* origin) {
            if(!rows.empty())rows+=',';
            rows+='"'+std::string(name)+"\":{\"path\":\"providers/provider-v1/"+name+"\",\"bytes\":10,\"sha256\":\""+hash+
                "\",\"role\":\""+role+"\",\"origin\":\""+origin+"\"}";
            files.insert("x3-modern-media/providers/provider-v1/"+std::string(name));
        };
        for(auto& e:cohort)add(e.name,e.role,e.origin);
        for(auto n:{"notices/COPYING","notices/README.md","notices/FFmpeg-LICENSE.md"})add(n,"notice","notice");
        docs[package_path]="{\"schema\":1,\"kind\":\"x3-owned-media-package\",\"scope\":\"local_qualification\",\"layout\":\"installed\",\"path_base\":\"media_root\","
            "\"package_id\":\"provider-v1\",\"architecture\":\"x86\",\"profile\":\"lav081-strict-mpeg1-rgb32-v1\",\"distribution_qualified\":false,"
            "\"provider\":{\"directory\":\"providers/provider-v1\",\"manifest\":\"providers/provider-v1/provider.manifest\","
            "\"source_clsid\":\"{B98D13E7-55DB-4385-A33D-09FD1BA26338}\",\"video_clsid\":\"{EE30215D-164F-4A92-A4EB-9D4C13390F9F}\",\"files\":{"+rows+"}},\"sources\":[{"+source_row()+"}],\"provenance\":{\"commit\":\"local\"}}";
        files.insert("x3-modern-media/sources/"+hash+"/00002.mkv");rebind();
    }
};
void replace(std::string& text,const std::string& from,const std::string& to) {
    auto at=text.find(from);check(at!=std::string::npos,"mutation witness exists");text.replace(at,from.size(),to);
}
void reject(const std::shared_ptr<MemoryInput>& input,const char* name,PackageErrorCode code=PackageErrorCode::none) {
    auto sentinel=std::make_shared<PackageConfig>();std::shared_ptr<const PackageConfig> out=sentinel;PackageError error;
    check(read_package_config(input,out,error)==PackageStatus::error,name);check(out==sentinel,"failure atomic output");
    check(error.code!=PackageErrorCode::none,"explicit error");if(code!=PackageErrorCode::none)check(error.code==code,"expected error");
}
void valid(const std::shared_ptr<MemoryInput>& input) {
    std::shared_ptr<const PackageConfig> out;PackageError error;
    check(read_package_config(input,out,error)==PackageStatus::ready,"valid package");
    check(error.code==PackageErrorCode::none&&out&&out->sources.size()==1,"valid publication");
    check(out->sources[0].id==2&&out->sources[0].effective_flags==8,"exact eligibility");
    check(out->provider_manifest.find(input->root)==0&&out->sources[0].path.find(input->root)==0,"root relocation unicode spaces");
    check(input->reads==3&&input->resolves==16,"bounded record reads and file identity resolutions");
}
}
int main(int argc,char** argv) {
    if(argc==2) {
        // Record-only integration seam: real installer records, synthetic file
        // identity resolver. Windows confinement/open behavior is a separate run.
        auto input=std::make_shared<MemoryInput>();input->docs.clear();input->files.clear();
        auto load=[&](const std::string& relative) {
            std::ifstream file(std::string(argv[1])+"/"+relative,std::ios::binary);
            check(bool(file),"record integration input exists");
            std::string bytes((std::istreambuf_iterator<char>(file)),{});
            input->docs[relative]=bytes;return Parser(bytes).parse();
        };
        auto install=load("x3-modern-install.json");auto& media=field(install,"media");
        auto package=load(text_field(media,"package_record_relative"));
        auto record=load(text_field(array(field(media,"sources"))[0],"source_record_relative"));
        for(const auto& entry:field(field(package,"provider"),"files").members)
            input->files.insert("x3-modern-media/"+text_field(entry.second,"path"));
        input->files.insert(text_field(record,"asset"));valid(input);
        std::cout<<"media_package_config actual_installer_records checks="<<checks<<" PASS\n";return 0;
    }
    check(argc==1,"usage: fixture [record-only-install-root]");
    check(sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","sha empty");
    check(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","sha abc");
    check(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","sha NIST 56 bytes");
    check(sha256(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","sha million a");
    check(sha256(std::string(1,'a'))=="ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb","sha padding 1");
    check(sha256(std::string(55,'a'))=="9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318","sha padding 55");
    check(sha256(std::string(56,'a'))=="b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a","sha padding 56");
    check(sha256(std::string(63,'a'))=="7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34","sha padding 63");
    check(sha256(std::string(64,'a'))=="ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb","sha padding 64");
    check(sha256(std::string(65,'a'))=="635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0","sha padding 65");
    check(sha256(std::string(119,'a'))=="31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb","sha padding 119");
    check(sha256(std::string(120,'a'))=="2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c","sha padding 120");
    check(sha256(std::string(127,'a'))=="c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a","sha padding 127");
    check(sha256(std::string(128,'a'))=="6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e","sha padding 128");
    check(sha256(std::string(129,'a'))=="c12cb024a2e5551cca0e08fce8f1c5e314555cc3fef6329ee994a3db752166ae","sha padding 129");
    check(sha256(std::string(131072,'a'))=="b44ffb72fcc259676bd80495fef1b44b808ca8f1ffe1b1706a4d7911b0e31f11","sha padding 131072");
    valid(std::make_shared<MemoryInput>());
    {
        auto input=std::make_shared<MemoryInput>();
        replace(input->docs[package_path],"\"commit\":\"local\"","\"commit\":\"\\uD83D\\uDE00\",\"integer\":18446744073709551615");input->rebind();valid(input);
    }
    auto relocated=std::make_shared<MemoryInput>();relocated->root=L"D:\\Other ß中\\X3";valid(relocated);
    {
        auto input=std::make_shared<MemoryInput>();std::weak_ptr<PackageInput> weak=input;std::shared_ptr<const PackageConfig> out;PackageError e;
        check(read_package_config(input,out,e)==PackageStatus::ready,"pin setup");input.reset();check(!weak.expired(),"adapter pins retained");out.reset();check(weak.expired(),"adapter pins released");
    }
    for(bool absent:{false,true}) {
        auto input=std::make_shared<MemoryInput>();if(absent)input->docs.erase("x3-modern-install.json");else input->docs["x3-modern-install.json"]="{\"project\":\"legacy\"}";
        std::shared_ptr<const PackageConfig> out=std::make_shared<PackageConfig>();PackageError e;
        check(read_package_config(input,out,e)==PackageStatus::disabled&&!out,"missing media disabled");check(input->reads==1&&input->resolves==0,"disabled does not touch provider");
    }
    for(auto bad:{"", "{", "[]", "null", "{\"media\":null}", "{}x", "{\"x\":1,}", "{\"x\":[1,]}", "{\"x\":01}", "{\"x\":-1}", "{\"x\":1.0}", "{\"x\":1e1}", "{\"x\":18446744073709551616}", "{\"x\":true,\"x\":false}", "{\"x\":1,\"\\u0078\":2}", "{\"x\":\"\\uD800\"}", "{\"x\":\"\\uDC00\"}", "{\"x\":\"\\uD800\\u0041\"}", "{\"x\":\"\\v\"}"}) {
        auto input=std::make_shared<MemoryInput>();input->docs["x3-modern-install.json"]=bad;reject(input,"malformed/schema install");
    }
    for(auto bytes:{std::string("\xc0\x80",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2),std::string(1,'\0'),std::string("\x80",1)}) {
        auto input=std::make_shared<MemoryInput>();input->docs["x3-modern-install.json"]="{\"x\":\""+bytes+"\"}";reject(input,"invalid UTF8/control");
    }
    for(unsigned kind=0;kind<3;++kind) {
        auto input=std::make_shared<MemoryInput>();std::string data;
        if(kind==0)data=std::string(package_document_limit+1,' ');
        else if(kind==1)data="{\"x\":"+std::string(18,'[')+"0"+std::string(18,']')+"}";
        else {data="{\"x\":[0";for(unsigned i=0;i<4096;++i)data+=",0";data+="]}";}
        input->docs["x3-modern-install.json"]=data;reject(input,"document structural bounds");
    }
    for(const auto& mutation:std::vector<std::pair<std::string,std::string>>{
        {"\"schema\":1","\"schema\":2"},{"\"layout\":\"installed\"","\"layout\":\"staged\""},
        {"\"path_base\":\"media_root\"","\"path_base\":\"game_root\""},{"\"architecture\":\"x86\"","\"architecture\":\"x64\""},
        {"lav081-strict-mpeg1-rgb32-v1","unknown"},{"local_qualification","public"},{"\"distribution_qualified\":false","\"distribution_qualified\":true"},
        {"\"id\":2","\"id\":3"},{"\"effective_flags\":8","\"effective_flags\":9"},
        {"\"role\":\"source\"","\"role\":\"dependency\""},{"\"origin\":\"strict\"","\"origin\":\"official\""},
        {"\"bytes\":10","\"bytes\":0"},{"B98D13E7","B98D13E8"},{"notices/COPYING","unknown.dll"},
        {"\"path\":\"providers/provider-v1/LAVSplitter.ax\"","\"path\":\"../LAVSplitter.ax\""},
        {"\"directory\":\"providers/provider-v1\"","\"directory\":\"C:/outside\""},
        {"\"asset_bytes\":456","\"asset_bytes\":457"},{"generated_timestamps","original"}}) {
        auto input=std::make_shared<MemoryInput>();replace(input->docs[package_path],mutation.first,mutation.second);input->rebind();reject(input,"package policy");
    }
    for(auto bad:{"../escape","/absolute","C:/drive","\\\\server\\share",".","..","nul","COM1","trailing.","trailing ","a:b","a//b","a\\b"}) {
        auto input=std::make_shared<MemoryInput>();replace(input->docs["x3-modern-install.json"],"\"package_id\":\"provider-v1\"","\"package_id\":\""+std::string(bad)+"\"");reject(input,"path policy");
    }
    for(auto name:{"x3-modern-media/providers/provider-v1/LAVVideo.ax","x3-modern-media/providers/provider-v1/notices/COPYING"}) {
        auto input=std::make_shared<MemoryInput>();input->files.erase(name);reject(input,"missing cohort",PackageErrorCode::missing_file);
    }
    for(auto name:{package_path,source_path}) {
        auto input=std::make_shared<MemoryInput>();input->docs[name]+=" ";reject(input,"record digest mismatch",PackageErrorCode::identity);
        input=std::make_shared<MemoryInput>();input->docs.erase(name);reject(input,"missing record",PackageErrorCode::missing_file);
    }
    auto input=std::make_shared<MemoryInput>();input->journal=true;reject(input,"journal before read",PackageErrorCode::journal);check(input->reads==0,"journal avoids reads");
    input=std::make_shared<MemoryInput>();input->late_journal=true;reject(input,"journal before publication",PackageErrorCode::journal);
    input=std::make_shared<MemoryInput>();input->throw_read=true;reject(input,"exception boundary",PackageErrorCode::io);
    std::cout<<"media_package_config checks="<<checks<<" PASS\n";
}
