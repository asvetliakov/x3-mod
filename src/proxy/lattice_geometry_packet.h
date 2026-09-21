#pragma once
// Fixed CPU-only geometry attachment and manifest-last writer. No D3D calls,
// native pointers, locks, hashing or allocation occur in attachment methods.
#include "lattice_state_policy.h"
#include "../ownership/clone_upload_core.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace x3m::lattice_state::geometry {
constexpr unsigned payload_bytes=466224;
constexpr unsigned offsets[2][2]={{0,387200},{409904,460584}};
constexpr unsigned sizes[2][2]={{387200,22704},{50680,5640}};
using Payload=std::array<unsigned char,payload_bytes>;
using UploadRecord=ownership::clone_upload::Record;
enum class Attachment {Valid,NotSelected,AllocationFailure,CopyRefused,PacketInvalid,
                       SubmissionFailed,SiblingRefused,ExportFailed};
inline const char* name(Attachment value) noexcept {
    switch(value){
    case Attachment::Valid:return "valid";case Attachment::NotSelected:return "not_selected";
    case Attachment::AllocationFailure:return "allocation_failure";case Attachment::CopyRefused:return "copy_refused";
    case Attachment::PacketInvalid:return "packet_invalid";case Attachment::SubmissionFailed:return "submission_failed";
    case Attachment::SiblingRefused:return "sibling_refused";case Attachment::ExportFailed:return "export_failed";
    }return "invalid";
}
struct Upload {
    const char* pair_status="not_attempted";
    Attachment attachment=Attachment::NotSelected;
    Status invalidated_by=Status::Off;
    std::uint64_t arm_serial=0;
    UploadRecord record{};
};
struct Packet {
    std::unique_ptr<Payload> payload;
    Upload uploads[2];
    std::uint64_t copy_ticks=0;
    bool requested=false,blocked=false,dirty[2]{};
    ~Packet(){erase();}
    static void wipe(void* destination,std::size_t size) noexcept {
        auto* p=static_cast<volatile unsigned char*>(destination);
        while(size--)*p++=0;
    }
    void arm(bool enabled,bool simulate_allocation_failure=false) noexcept {
        erase();payload.reset();uploads[0]={};uploads[1]={};copy_ticks=0;
        requested=enabled;blocked=false;
        if(enabled&&!simulate_allocation_failure)payload.reset(new(std::nothrow) Payload{});
        if(enabled&&!payload)for(auto& u:uploads)u.attachment=Attachment::AllocationFailure;
    }
    void erase() noexcept {erase(0);erase(1);}
    void erase(unsigned slot) noexcept {
        if(slot>1)return;
        if(payload&&dirty[slot])wipe(payload->data()+offsets[slot][0],sizes[slot][0]+sizes[slot][1]);
        dirty[slot]=false;
    }
    void strip(unsigned slot,Attachment reason,Status status=Status::Off) noexcept {
        erase(slot);auto& u=uploads[slot];u.record={};u.arm_serial=0;
        u.attachment=reason;u.invalidated_by=status;
    }
    bool can_copy(unsigned slot) const noexcept {
        return requested&&payload&&!blocked&&slot<2&&uploads[slot].attachment==Attachment::NotSelected;
    }
    unsigned char* destination(unsigned slot,unsigned kind) noexcept {
        if(!can_copy(slot)||kind>1)return nullptr;
        dirty[slot]=true;return payload->data()+offsets[slot][kind];
    }
    // Called only after B1 has finished both writes. Failed or malformed results
    // wipe the entire pair; success cannot revive a terminally invalid packet.
    void copied(unsigned slot,const char* pair_status,bool success,std::uint64_t arm,
                const UploadRecord& r,bool binding_match) noexcept {
        if(slot>1||!requested)return;
        auto& u=uploads[slot];u.pair_status=pair_status;
        if(blocked){erase(slot);return;}
        const bool good=success&&binding_match&&payload&&arm&&r.producer_payload_valid&&
            r.owner&&r.generation&&r.invocation&&r.buffers[0].allocation&&r.buffers[1].allocation&&
            r.buffers[0].allocation!=r.buffers[1].allocation&&r.buffers[0].revision&&r.buffers[1].revision&&
            r.bytes[0]==sizes[slot][0]&&r.bytes[1]==sizes[slot][1];
        if(!good){if(success)u.pair_status="api_error";strip(slot,Attachment::CopyRefused);return;}
        u.arm_serial=arm;u.record=r;u.attachment=Attachment::Valid;
    }
    void invalidate(Status status) noexcept {
        if(!requested)return;
        if(blocked&&uploads[0].attachment==Attachment::PacketInvalid&&uploads[0].invalidated_by==status)return;
        blocked=true;
        for(unsigned i=0;i<2;++i)strip(i,Attachment::PacketInvalid,status);
    }
    bool finalize(Status status) noexcept {
        if(status!=Status::Complete){invalidate(status);return false;}
        bool good=!blocked&&payload&&uploads[0].attachment==Attachment::Valid&&uploads[1].attachment==Attachment::Valid;
        if(good){
            const auto& a=uploads[0];const auto& b=uploads[1];
            good=a.arm_serial==b.arm_serial&&a.record.owner==b.record.owner&&
                a.record.generation==b.record.generation&&a.record.invocation!=b.record.invocation;
            for(auto x:a.record.buffers)for(auto y:b.record.buffers)good=good&&x.allocation!=y.allocation;
        }
        if(!good)for(unsigned i=0;i<2;++i)
            if(uploads[i].attachment==Attachment::Valid)strip(i,Attachment::SiblingRefused);
        return good;
    }
    void export_failed() noexcept {
        blocked=true;
        for(unsigned i=0;i<2;++i)strip(i,Attachment::ExportFailed);
    }
};
static_assert(offsets[1][1]+sizes[1][1]==payload_bytes);
struct State {
    std::uint64_t pid=0,device=0,frame=0,generation=0,query_ticks=0,qpc_frequency=1;
    std::uint64_t draw[2]{};
    std::uint32_t result[2]{};
    unsigned candidates=0,matches[2]{};
    bool scope_active=false,submitted[2]{};
    Status status=Status::Off;
};
using FieldsWriter=bool(*)(FILE*,void*,unsigned) noexcept;
inline void upload_json(FILE* file,const Upload& u,unsigned slot) noexcept {
    const bool valid=u.attachment==Attachment::Valid;
    std::fprintf(file,"{\"pair_status\":\"%s\",\"attachment_status\":\"%s\",\"producer_payload_valid\":%s,\"binding_revision_match_at_observation\":%s",
        u.pair_status,name(u.attachment),valid?"true":"false",valid?"true":"false");
    if(valid){
        const auto& r=u.record;
        std::fprintf(file,",\"arm_serial\":\"%016llx\",\"owner\":\"%016llx\",\"generation\":\"%016llx\",\"invocation\":\"%016llx\"",
            static_cast<unsigned long long>(u.arm_serial),static_cast<unsigned long long>(r.owner),
            static_cast<unsigned long long>(r.generation),static_cast<unsigned long long>(r.invocation));
        for(unsigned k=0;k<2;++k)std::fprintf(file,",\"%s\":{\"allocation\":\"%016llx\",\"revision\":\"%016llx\",\"offset\":%u,\"bytes\":%u}",
            k?"index":"vertex",static_cast<unsigned long long>(r.buffers[k].allocation),
            static_cast<unsigned long long>(r.buffers[k].revision),offsets[slot][k],sizes[slot][k]);
    }else if(u.attachment==Attachment::PacketInvalid)
        std::fprintf(file,",\"invalidated_by\":\"%s\"",lattice_state::name(u.invalidated_by));
    else std::fputs(",\"invalidated_by\":null",file);
    std::fputc('}',file);
}
inline bool serialize(FILE* file,const State& s,const Packet& packet,bool valid,
                      const char* binary,const char* digest,FieldsWriter fields,void* context) noexcept {
    using U=unsigned long long;
    std::fprintf(file,"{\"schema\":2,\"pid\":%llu,\"selector\":\"%s\",\"status\":\"%s\",\"device\":%llu,\"frame\":%llu,\"generation\":%llu,\"draw_input_coherence\":\"unqualified\",\"payload_copy_valid\":%s,\"scope_active_at_arm\":%s,\"candidates\":%u,\"query_ticks\":%llu,\"qpc_frequency\":%llu,\"matches\":[%u,%u],",
        U(s.pid),selector_name,lattice_state::name(s.status),U(s.device),U(s.frame),U(s.generation),valid?"true":"false",
        s.scope_active?"true":"false",s.candidates,U(s.query_ticks),U(s.qpc_frequency),s.matches[0],s.matches[1]);
    std::fprintf(file,"\"geometry\":{\"format\":\"x3_lattice_geometry_v1\",\"scope\":\"producer_uploads_bound_at_observation\",\"status\":\"%s\",\"file\":",valid?"complete":"unavailable");
    if(valid)std::fprintf(file,"\"%s\",\"bytes\":%u,\"sha256\":\"%s\"",binary,payload_bytes,digest);
    else std::fputs("null,\"bytes\":0,\"sha256\":null",file);
    std::fprintf(file,",\"copy_ticks\":%llu},\"records\":[",U(packet.copy_ticks));
    for(unsigned slot=0;slot<2;++slot){
        if(slot)std::fputc(',',file);
        std::fprintf(file,"{\"slot\":%u,\"draw\":%llu,\"submitted\":%s,\"result\":\"%08x\",\"fields\":",slot,U(s.draw[slot]),s.submitted[slot]?"true":"false",s.result[slot]);
        if(!fields(file,context,slot))return false;
        std::fputs(",\"upload\":",file);upload_json(file,packet.uploads[slot],slot);std::fputc('}',file);
    }
    return std::fputs("]}\n",file)>=0&&!std::ferror(file);
}
struct Publication {bool file_ok=false,payload_valid=false;long json_bytes=0;char sha256[65]{};};
// Backend implements only this fixed two-file transaction. Production uses
// CryptoAPI plus exclusive Windows files; the host fixture injects each failure
// into these SAME eligibility/formatting/order/cleanup operations.
template<class Backend> Publication publish(Packet& packet,const State& state,
    const char* binary,FieldsWriter fields,void* context,Backend& backend) noexcept {
    Publication out;
    State final=state;
    if(final.status==Status::Complete&&(final.matches[0]!=1||final.matches[1]!=1))final.status=Status::Partial;
    if(final.status==Status::Complete&&(!final.submitted[0]||!final.submitted[1]||
        (final.result[0]&0x80000000u)||(final.result[1]&0x80000000u)))final.status=Status::SubmissionFailed;
    bool valid=packet.finalize(final.status);
    if(valid){
        valid=backend.hash(packet.payload->data(),payload_bytes,out.sha256);
        FILE* file=valid?backend.open_payload():nullptr;
        if(file){const bool written=backend.write_payload(file,packet.payload->data(),payload_bytes);
            const bool closed=backend.close_payload(file);valid=written&&closed;
        }else valid=false;
        if(valid)valid=backend.publish_payload();
        if(!valid){backend.remove_payload();packet.export_failed();out.sha256[0]=0;}
    }
    FILE* file=backend.open_json();
    if(file){
        const bool formatted=serialize(file,final,packet,valid,binary,out.sha256,fields,context);
        out.json_bytes=std::ftell(file);
        const bool written=formatted&&!std::ferror(file)&&out.json_bytes>0&&out.json_bytes<=1024*1024;
        const bool closed=backend.close_json(file);
        out.file_ok=written&&closed&&backend.publish_json();
    }
    if(!out.file_ok){backend.remove_payload();packet.export_failed();}
    backend.remove_temporaries();
    out.payload_valid=out.file_ok&&valid;
    if(!out.payload_valid)out.sha256[0]=0;
    return out;
}
}
