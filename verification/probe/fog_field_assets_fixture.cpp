#include "fog_field_assets.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
bool fail_allocation = false;
std::vector<std::uint8_t> read(const char* path) {
    std::ifstream source(path, std::ios::binary | std::ios::ate);
    if (!source) throw std::runtime_error("open");
    const auto size = source.tellg(); source.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!source.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("read");
    return bytes;
}
void put32(std::vector<std::uint8_t>& p, std::size_t at, std::uint32_t value) {
    for (unsigned i=0;i<4;++i) p[at+i]=std::uint8_t(value>>(8*i));
}
std::uint32_t get32(const std::vector<std::uint8_t>& p, std::size_t at) {
    return std::uint32_t(p[at]) | std::uint32_t(p[at+1])<<8 |
           std::uint32_t(p[at+2])<<16 | std::uint32_t(p[at+3])<<24;
}
std::uint64_t full_checksum(const std::vector<std::uint16_t>& words) {
    std::uint64_t hash=14695981039346656037ull;
    for(const auto word:words){
        hash=(hash^std::uint8_t(word))*1099511628211ull;
        hash=(hash^std::uint8_t(word>>8))*1099511628211ull;
    }
    return hash;
}
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
using namespace x3m::renderer::fog_field;
void expect(Status status, const std::vector<std::uint8_t>& packet, const ProfileInfo& info,
            const char* message) {
    std::vector<std::uint16_t> output(3, 7);
    const auto result=decode_packet(packet.data(),packet.size(),info,output);
    require(result.status==status&&result.hresult<0,message);require(output.empty(),"failure clears output");
}
std::size_t first_literal(const std::vector<std::uint8_t>& packet) {
    std::size_t cursor=56;const auto runs=get32(packet,40);
    for(std::uint32_t run=0;run<runs;++run){
        const auto encoded=get32(packet,cursor);cursor+=4;
        if(encoded&0x80000000u)return cursor;
    }
    throw std::runtime_error("literal");
}
}

void* operator new(std::size_t size) {
    if(fail_allocation)throw std::bad_alloc();
    if(auto* p=std::malloc(size))return p;throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }

int main(int argc,char** argv) try {
    require(argc==int(std::size(family_profiles))+1,"arguments");
    using namespace x3m::renderer::fog_field;
    for(unsigned which=0;which<std::size(family_profiles);++which){
        const auto read_begin=std::chrono::steady_clock::now();
        const auto packet=read(argv[1+which]);
        const auto read_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-read_begin).count();
        const auto* info=profile_info(family_profiles[which].profile);
        require(info&&info->base_sigma>0,"profile info");
        std::vector<std::uint16_t> output;
        const auto decode_begin=std::chrono::steady_clock::now();
        const auto ok=decode_packet(packet.data(),packet.size(),*info,output);
        const auto decode_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-decode_begin).count();
        require(ok&&ok.decoded_bytes==info->decoded_bytes&&output.size()*2==info->decoded_bytes,"decode exact");
        const auto independent_hash=full_checksum(output);
        require(independent_hash==info->decoded_fnv1a,"independent full scan");
        std::printf("LOAD profile=%u packet_bytes=%zu decoded_bytes=%u decoded_fnv1a=%016llx read_ms=%.6f decode_ms=%.6f host_only=1\n",
                    which+1,packet.size(),info->decoded_bytes,
                    static_cast<unsigned long long>(independent_hash),read_ms,decode_ms);
        auto bad=packet;bad.pop_back();expect(Status::Truncated,bad,*info,"truncation");
        bad=packet;put32(bad,8,2);expect(Status::WrongVersion,bad,*info,"version");
        bad=packet;put32(bad,24,1559);expect(Status::MetadataMismatch,bad,*info,"dimensions");
        bad=packet;bad[44]^=1;expect(Status::MetadataMismatch,bad,*info,"header checksum");
        bad=packet;bad.push_back(0);expect(Status::TrailingBytes,bad,*info,"trailing");
        bad=packet;put32(bad,56,0);expect(Status::InvalidRun,bad,*info,"zero run");
        bad=packet;put32(bad,56,0x7fffffffu);expect(Status::InvalidRun,bad,*info,"overflow run");
        const auto literal=first_literal(packet);
        bad=packet;bad[literal]=0;bad[literal+1]=0x7c;expect(Status::InvalidHalf,bad,*info,"nonfinite half");
        bad=packet;bad[literal+1]|=0x80;expect(Status::InvalidHalf,bad,*info,"negative half");
        bad=packet;bad[literal]^=1;expect(Status::ChecksumMismatch,bad,*info,"decoded checksum");
        bad=packet;put32(bad,literal-4,0x80000001u);put32(bad,literal+8,0);
        expect(Status::InvalidRun,bad,*info,"partial failure");
        std::vector<std::uint16_t> allocation_output;
        fail_allocation=true;const auto allocation=decode_packet(packet.data(),packet.size(),*info,allocation_output);fail_allocation=false;
        require(allocation.status==Status::AllocationFailed&&allocation_output.empty(),"allocation failure");
    }
    std::vector<std::uint16_t> output(1,1);
    require(decode_from_resource(reinterpret_cast<void*>(1),Profile::Bluewell,output).status==Status::ResourceLoadFailed&&output.empty(),"host resource boundary");
    require(profile_info(Profile::None)==nullptr,"none profile");
    std::printf("PASS fog_field_assets decoder=%zu corruptions_per_profile=11 allocation=1 atomic=1 independent_fullscan=%zu\n", std::size(family_profiles), std::size(family_profiles));return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what());return 1; }
