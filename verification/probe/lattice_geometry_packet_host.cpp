#include "../../src/proxy/lattice_geometry_packet.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace g=x3m::lattice_state::geometry;
using x3m::lattice_state::Status;
namespace fs=std::filesystem;
namespace {
unsigned checks=0,failures=0;
void check(bool value){++checks;if(!value)++failures;}
std::string mode;
std::string fields[2];
bool write_fields(FILE* file,void*,unsigned slot) noexcept {
    if(mode=="json_write"&&slot==1)return false;
    return std::fwrite(fields[slot].data(),1,fields[slot].size(),file)==fields[slot].size();
}
struct Files {
    fs::path paths[2],temporary[2];
    bool owned_temp[2]{},owned_final[2]{};
    std::string digest;
    unsigned hashes=0,payload_opens=0,json_publishes=0;
    FILE* open(unsigned i){
        if(mode==(i?"json_open":"payload_open"))return nullptr;
        int fd=::open(temporary[i].c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);
        if(fd<0)return nullptr;
        owned_temp[i]=true;
        FILE* file=fdopen(fd,"wb");if(!file)::close(fd);return file;
    }
    bool rename(unsigned i){
        if(mode==(i?"json_rename":"payload_rename"))return false;
        // POSIX link+unlink supplies no-replace semantics for the host backend.
        if(::link(temporary[i].c_str(),paths[i].c_str()))return false;
        ::unlink(temporary[i].c_str());owned_temp[i]=false;owned_final[i]=true;
        if(i)++json_publishes;return true;
    }
    bool hash(const unsigned char* bytes,unsigned size,char* output){
        ++hashes;check(size==g::payload_bytes);check(bytes!=nullptr);
        if(mode=="hash")return false;
        // Python supplies the digest of the same authored bytes and the ACTUAL
        // reader rehashes the emitted binary. Native CryptoAPI stays a separate
        // Windows backend/cross-compilation check, not a host crypto claim.
        std::memcpy(output,digest.c_str(),65);return true;
    }
    FILE* open_payload(){++payload_opens;return open(0);}
    bool write_payload(FILE* f,const unsigned char* bytes,unsigned size){
        unsigned count=mode=="payload_write"?size-1:size;
        return std::fwrite(bytes,1,count,f)==size&&!std::ferror(f);
    }
    bool close_payload(FILE* f){const bool ok=std::fclose(f)==0;return ok&&mode!="payload_close";}
    bool publish_payload(){return rename(0);}
    FILE* open_json(){return open(1);}
    bool close_json(FILE* f){const bool ok=std::fclose(f)==0;return ok&&mode!="json_close";}
    bool publish_json(){
        // Successful payload metadata is never finalized before its sidecar.
        if(owned_final[0])check(fs::file_size(paths[0])==g::payload_bytes);
        return rename(1);
    }
    void remove_payload(){
        if(owned_final[0]){::unlink(paths[0].c_str());owned_final[0]=false;}
        if(owned_temp[0]){::unlink(temporary[0].c_str());owned_temp[0]=false;}
    }
    void remove_temporaries(){for(unsigned i=0;i<2;++i)if(owned_temp[i]){::unlink(temporary[i].c_str());owned_temp[i]=false;}}
};
g::UploadRecord record(unsigned slot){
    constexpr std::uint64_t high=0x1000000000000000ull;
    g::UploadRecord r;r.owner=high+2;r.generation=high+3;r.invocation=high+10+slot;
    r.buffers[0]={high+100+slot*2,high+4};r.buffers[1]={high+101+slot*2,high+5};
    r.bytes[0]=g::sizes[slot][0];r.bytes[1]=g::sizes[slot][1];r.producer_payload_valid=true;return r;
}
void fill(g::Packet& packet,unsigned slot){
    auto* vertex=packet.destination(slot,0);auto* index=packet.destination(slot,1);
    check(vertex&&index);
    if(!vertex||!index)return;
    check(vertex==packet.payload->data()+g::offsets[slot][0]);
    check(index==packet.payload->data()+g::offsets[slot][1]);
    for(unsigned i=0;i<g::sizes[slot][0];++i)vertex[i]=static_cast<unsigned char>((i+17*slot)%251);
    const unsigned vertices=slot?1267:9680;
    for(unsigned i=0;i<g::sizes[slot][1];i+=2){unsigned value=(i/2)%vertices;index[i]=value&255;index[i+1]=value>>8;}
}
}
int main(int argc,char** argv){
    if(argc!=6)return 2;
    mode=argv[1];const fs::path directory=argv[2];fs::create_directories(directory);
    for(unsigned i=0;i<2;++i){std::ifstream input(argv[3+i]);fields[i]=std::string(std::istreambuf_iterator<char>(input),{});}
    g::Packet packet;packet.arm(true,mode=="allocation_failure");
    constexpr std::uint64_t arm=0x1000000000000001ull;
    // Reverse draw arrival order must still produce the fixed slot directory.
    for(unsigned slot:{1u,0u})if(packet.can_copy(slot)){
        fill(packet,slot);auto r=record(slot);std::uint64_t serial=arm;
        const bool refused=mode.rfind("refuse_",0)==0&&slot==1;
        if(slot==1){
            if(mode=="owner_mismatch")++r.owner;if(mode=="generation_mismatch")++r.generation;
            if(mode=="arm_mismatch")++serial;if(mode=="same_invocation")--r.invocation;
            if(mode=="allocation_collision")r.buffers[0]=record(0).buffers[0];
            if(mode=="malformed_bytes")--r.bytes[1];
        }
        packet.copied(slot,refused?argv[1]+7:"copied",!refused,serial,r,true);
        packet.copy_ticks+=slot?9:7;
    }
    g::State state;state.pid=123;state.device=1;state.frame=8;state.generation=0;
    state.status=Status::Complete;state.scope_active=true;state.candidates=2;
    state.matches[0]=state.matches[1]=1;state.draw[0]=1;state.draw[1]=2;
    state.submitted[0]=state.submitted[1]=true;state.query_ticks=100;state.qpc_frequency=1000;
    const struct {const char* text;Status status;} invalidations[]={
        {"reset",Status::Reset},{"ambiguous",Status::Ambiguous},{"partial",Status::Partial},
        {"unavailable",Status::Unavailable},{"capacity",Status::Capacity},
        {"submission_failed",Status::SubmissionFailed},{"no_match",Status::NoMatch},
        {"reentrant_partial",Status::Partial}};
    for(const auto& invalid:invalidations)if(mode==invalid.text){
        state.status=invalid.status;packet.invalidate(state.status);
        // Late success is hostile: it must not revive either metadata or bytes.
        packet.copied(0,"copied",true,arm,record(0),true);
        check(packet.blocked&&packet.uploads[0].record.invocation==0);
    }
    if(mode=="partial"||mode=="reentrant_partial"){state.matches[1]=0;state.candidates=1;state.draw[1]=0;state.submitted[1]=false;}
    if(mode=="no_match"){state.matches[0]=state.matches[1]=0;state.candidates=0;state.draw[0]=state.draw[1]=0;}
    if(mode=="ambiguous"){state.matches[0]=2;state.candidates=3;}
    if(mode=="capacity")state.candidates=65;
    if(mode=="submission_failed")state.result[1]=0x80004005u;
    if(mode=="suppressed")state.submitted[1]=false; // Shared writer's terminal defense.
    Files files;files.paths[0]=directory/"lattice-geometry-123-1-8-0.bin";
    files.paths[1]=directory/"lattice-state-123-1-8-0.json";files.digest=argv[5];
    for(unsigned i=0;i<2;++i)files.temporary[i]=files.paths[i].string()+".tmp";
    if(mode.rfind("collision_",0)==0){
        const unsigned i=mode.find("json")!=std::string::npos?1:0;
        std::ofstream existing(mode.find("temp")!=std::string::npos?files.temporary[i]:files.paths[i]);existing<<"pre-existing";
    }
    const auto result=g::publish(packet,state,"lattice-geometry-123-1-8-0.bin",write_fields,nullptr,files);
    check(files.json_publishes==unsigned(result.file_ok));
    if(!result.payload_valid&&packet.payload){bool zero=true;for(auto v:*packet.payload)zero=zero&&!v;check(zero);}
    if(mode=="okay")check(result.file_ok&&result.payload_valid&&files.hashes==1&&files.payload_opens==1);
    std::printf("{\"checks\":%u,\"failures\":%u,\"file_ok\":%s,\"payload_valid\":%s,\"hashes\":%u,\"payload_opens\":%u,\"json_bytes\":%ld}\n",
        checks,failures,result.file_ok?"true":"false",result.payload_valid?"true":"false",files.hashes,files.payload_opens,result.json_bytes);
    return failures?1:0;
}
