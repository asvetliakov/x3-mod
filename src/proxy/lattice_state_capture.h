#pragma once
#include "lattice_state_policy.h"
#include "object_trace.h"
#include "lattice_geometry_packet.h"
#include <d3d9.h>
namespace x3m {struct MotionRoute;}
namespace x3m::lattice_state {
using GetTarget=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9**);
// Only descriptor/query operations, no resource byte access or GPU work. The
// caller owns the capture mutex and full CPU envelope around these methods.
class Capture {
public:
    void arm(std::uint64_t device,std::uint64_t frame,std::uint64_t generation,bool scope_active,bool upload_requested=false) noexcept;
    void invalidate() noexcept {refuse(Status::Reset);}
    bool can_select(const Arguments& a) const noexcept {return policy_.status==Status::Armed && signature(a)>=0;}
    int original(IDirect3DDevice9*,const Arguments&,std::uint64_t draw) noexcept;
    void effective(int slot,IDirect3DDevice9*,const D3DCAPS9&,GetTarget,const MotionRoute&) noexcept;
    void result(int slot,HRESULT,bool submitted) noexcept;
    bool publish(const wchar_t* directory) noexcept; // Present only; no COM retained
    bool pending() const noexcept {return policy_.status!=Status::Off;}
private:
    enum class Kind {SourceShader,Declaration,Object,Arguments,Route,Shader,ConstantsF,ConstantsI,ConstantsB,
        Render,Sampler,Viewport,Scissor,Clip,Stream,StreamFrequency,VertexDesc,Indices,IndexDesc,
        Target,SurfaceDesc,Container,Texture,TextureDesc,TextureLOD,Caps};
    struct Field {Kind kind;unsigned index;HRESULT hr;unsigned offset,count;};
    struct Record {
        std::uint64_t draw=0;HRESULT result=D3DERR_NOTAVAILABLE;bool submitted=false;
        unsigned count=0,used=0;Field fields[field_limit]{};std::uint32_t words[word_limit]{};
    } records_[2];
    std::uint32_t selector_words_[shader_word_limit]{}; // nonmatches never overwrite accepted records
    geometry::Packet geometry_;
    void refuse(Status value) noexcept {policy_.refuse(value);sync_geometry();}
    void sync_geometry() noexcept {
        if(policy_.status!=Status::Armed&&policy_.status!=Status::Off&&policy_.status!=Status::Complete)geometry_.invalidate(policy_.status);
    }
    Policy policy_{};std::uint64_t device_=0,frame_=0,generation_=0;
    std::uint64_t query_ticks_=0;unsigned candidate_count_=0;bool scope_active_=false,busy_=false;
    bool add(Record&,Kind,unsigned,HRESULT,const void*,unsigned bytes,bool required=true) noexcept;
    template<class T> void value(Record& r,Kind k,unsigned i,HRESULT hr,const T& v) noexcept {add(r,k,i,hr,&v,sizeof v);}
    void binding(Record&,Kind,unsigned,HRESULT,IDirect3DResource9*) noexcept;
    void surface(Record&,unsigned,HRESULT,IDirect3DSurface9*) noexcept;
    template<class Shader> std::uint64_t fingerprint(HRESULT hr,Shader*) noexcept;
    template<class Shader> std::uint64_t shader(Record&,Kind,unsigned,HRESULT,Shader*) noexcept;
    static const char* kind_name(Kind) noexcept;
};
static_assert(sizeof(Capture)<=448*1024,"bounded state packet allocation");
}
