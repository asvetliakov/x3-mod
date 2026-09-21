#include "lattice_state_capture.h"
#include "capture.h"
#include "capture_state.h"
#include "motion_output.h"
#include "../ownership/clone_upload_observer.h"
#include "../ownership/d3d9_ownership.h"
#include "lattice_geometry_windows.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace x3m::lattice_state {
namespace {
// Every defined raster state relevant to this SM3 triangle profile, including
// vertex-output wrapping and the states omitted by the old F8 snapshot.
constexpr D3DRENDERSTATETYPE raster[]={
 D3DRS_ZENABLE,D3DRS_FILLMODE,D3DRS_SHADEMODE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,
 D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_CULLMODE,D3DRS_ZFUNC,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,
 D3DRS_DITHERENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SPECULARENABLE,
 D3DRS_STENCILENABLE,D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,D3DRS_STENCILPASS,D3DRS_STENCILFUNC,
 D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_CLIPPING,D3DRS_CLIPPLANEENABLE,
 D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,D3DRS_COLORWRITEENABLE,D3DRS_BLENDOP,
 D3DRS_SCISSORTESTENABLE,D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_ANTIALIASEDLINEENABLE,
 D3DRS_TWOSIDEDSTENCILMODE,D3DRS_CCW_STENCILFAIL,D3DRS_CCW_STENCILZFAIL,D3DRS_CCW_STENCILPASS,
 D3DRS_CCW_STENCILFUNC,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3,
 D3DRS_BLENDFACTOR,D3DRS_SRGBWRITEENABLE,D3DRS_DEPTHBIAS,D3DRS_SEPARATEALPHABLENDENABLE,
 D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,
 D3DRS_WRAP0,D3DRS_WRAP1,D3DRS_WRAP2,D3DRS_WRAP3,D3DRS_WRAP4,D3DRS_WRAP5,D3DRS_WRAP6,D3DRS_WRAP7,
 D3DRS_WRAP8,D3DRS_WRAP9,D3DRS_WRAP10,D3DRS_WRAP11,D3DRS_WRAP12,D3DRS_WRAP13,D3DRS_WRAP14,D3DRS_WRAP15};
constexpr D3DSAMPLERSTATETYPE sampler[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,
 D3DSAMP_BORDERCOLOR,D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER,D3DSAMP_MIPMAPLODBIAS,
 D3DSAMP_MAXMIPLEVEL,D3DSAMP_MAXANISOTROPY,D3DSAMP_SRGBTEXTURE,D3DSAMP_ELEMENTINDEX,D3DSAMP_DMAPOFFSET};
const char* pair_name(ownership::CloneUploadPairStatus status) noexcept {
    using S=ownership::CloneUploadPairStatus;
    switch(status){
    case S::None:return "not_attempted";case S::Copied:return "copied";case S::Unarmed:return "unarmed";
    case S::Closing:return "closing";case S::ActiveScope:return "active_scope";case S::Selector:return "selector";
    case S::Missing:return "missing";case S::Duplicate:return "duplicate";case S::StaleArm:return "stale_arm";
    case S::Device:return "device";case S::Binding:return "binding";case S::Revision:return "revision";
    case S::OpenMapping:return "open_mapping";case S::Dispatch:return "dispatch";case S::Capacity:return "capacity";
    }return "api_error";
}
std::uint64_t hash(const void* p,unsigned n) noexcept {
    auto* bytes=static_cast<const unsigned char*>(p);std::uint64_t out=14695981039346656037ull;
    for(unsigned i=0;i<n;++i){out^=bytes[i];out*=1099511628211ull;}return out;
}
std::uint64_t stamp() noexcept {LARGE_INTEGER q{};QueryPerformanceCounter(&q);return std::uint64_t(q.QuadPart);}
}
void Capture::arm(std::uint64_t device,std::uint64_t frame,std::uint64_t generation,bool scope_active,bool upload_requested) noexcept {
    geometry_.arm(upload_requested);policy_.arm();scope_active_=scope_active;if(!scope_active)refuse(Status::Unavailable);device_=device;frame_=frame;generation_=generation;query_ticks_=0;candidate_count_=0;
    for(auto& r:records_){r.count=r.used=0;r.draw=0;r.submitted=false;r.result=D3DERR_NOTAVAILABLE;}
}
bool Capture::add(Record& r,Kind kind,unsigned index,HRESULT hr,const void* p,unsigned bytes,bool required) noexcept {
    if(bytes%4||r.count==field_limit||!fits(r.used,FAILED(hr)?0:bytes/4,word_limit)){
        refuse(Status::Capacity);return false;
    }
    const unsigned words=FAILED(hr)?0:bytes/4;
    r.fields[r.count++]={kind,index,hr,r.used,words};
    if(words)std::memcpy(r.words+r.used,p,bytes);
    r.used+=words;
    // Unavailable optional bindings are represented explicitly by the caller;
    // unsuccessful required state queries make the packet incomplete.
    if(required&&FAILED(hr))refuse(Status::Unavailable);
    return true;
}
template<class Shader> std::uint64_t Capture::shader(Record& r,Kind kind,unsigned index,HRESULT hr,Shader* s) noexcept {
    if(FAILED(hr)||!s){add(r,kind,index,FAILED(hr)?hr:D3DERR_NOTAVAILABLE,nullptr,0);if(s)s->Release();return 0;}
    UINT bytes=0;hr=s->GetFunction(nullptr,&bytes);
    if(SUCCEEDED(hr)&&(!bytes||bytes%4||bytes/4>shader_word_limit||!fits(r.used,bytes/4,word_limit)))hr=D3DERR_NOTAVAILABLE;
    const UINT capacity=bytes;
    if(SUCCEEDED(hr)){hr=s->GetFunction(r.words+r.used,&bytes);if(SUCCEEDED(hr)&&(bytes!=capacity))hr=D3DERR_NOTAVAILABLE;}
    s->Release();
    if(FAILED(hr)){add(r,kind,index,hr,nullptr,0);return 0;}
    if(r.count==field_limit){refuse(Status::Capacity);return 0;}
    const auto h=hash(r.words+r.used,bytes);
    r.fields[r.count++]={kind,index,hr,r.used,bytes/4};r.used+=bytes/4;return h;
}
template<class Shader> std::uint64_t Capture::fingerprint(HRESULT hr,Shader* s) noexcept {
    UINT bytes=0;
    if(SUCCEEDED(hr))hr=s?s->GetFunction(nullptr,&bytes):D3DERR_NOTAVAILABLE;
    if(SUCCEEDED(hr)&&(!bytes||bytes%4||bytes/4>shader_word_limit))hr=D3DERR_NOTAVAILABLE;
    const UINT expected=bytes;
    if(SUCCEEDED(hr)){hr=s->GetFunction(selector_words_,&bytes);if(SUCCEEDED(hr)&&bytes!=expected)hr=D3DERR_NOTAVAILABLE;}
    if(s)s->Release();
    if(FAILED(hr)){refuse(Status::Unavailable);return 0;}
    return hash(selector_words_,bytes);
}
int Capture::original(IDirect3DDevice9* d,const Arguments& args,std::uint64_t draw) noexcept {
    if(policy_.status!=Status::Armed)return -1;
    if(busy_){refuse(Status::Ambiguous);return -1;}
    const int slot=signature(args);if(slot<0)return -1;
    busy_=true;struct Busy {bool& value;~Busy(){value=false;}} busy{busy_};
    if(++candidate_count_>64){refuse(Status::Capacity);return -1;}
    const auto begin=stamp();
    struct Time {std::uint64_t& total;std::uint64_t begin;~Time(){total+=stamp()-begin;}} time{query_ticks_,begin};
    object_trace::Snapshot scope{};const bool scoped=object_trace::current(&scope);
    Object object{scoped,scope.valid,scope.model,scope.lod,{scope.position[0],scope.position[1],scope.position[2]},
        scope.session,std::uint32_t(scope.node),scope.node_handle};
    if(!scoped||!(scope.valid&object_trace::Node)){refuse(Status::Unavailable);return -1;}
    if(!object_matches(object))return -1;
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    HRESULT hr=d->GetVertexShader(&vs);const auto vh=fingerprint(hr,vs);
    hr=d->GetPixelShader(&ps);const auto ph=fingerprint(hr,ps);
    if(policy_.status!=Status::Armed)return -1;
    if(vh!=source_vs||ph!=source_ps)return -1;
    IDirect3DVertexDeclaration9* decl=nullptr;hr=d->GetVertexDeclaration(&decl);
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT count=MAXD3DDECLLENGTH+1;
    if(SUCCEEDED(hr))hr=decl?decl->GetDeclaration(elements,&count):D3DERR_NOTAVAILABLE;
    if(decl)decl->Release();
    if(FAILED(hr)){refuse(Status::Unavailable);return -1;}
    bool declaration_matches=count==6;
    if(declaration_matches)for(unsigned i=0;i<6;++i){
        const D3DVERTEXELEMENT9 wanted=i==5?D3DVERTEXELEMENT9{0xff,0,D3DDECLTYPE_UNUSED,0,0,0}:
            D3DVERTEXELEMENT9{0,WORD(i*8),D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,BYTE(i==0?0:i==1?5:i==2?3:i==3?6:7),0};
        if(std::memcmp(elements+i,&wanted,sizeof wanted)){declaration_matches=false;break;}
    }
    if(policy_.status!=Status::Armed||!policy_.accept_match(unsigned(slot),object,vh,ph,declaration_matches)){sync_geometry();return -1;}
    // Commit only a full selector match. A later nonmatch must not clear a
    // previously retained slot; exact duplicates invalidate without overwriting.
    auto& r=records_[slot];r.count=r.used=0;r.draw=draw;r.submitted=false;
    value(r,Kind::SourceShader,0,S_OK,vh);value(r,Kind::SourceShader,1,S_OK,ph);
    add(r,Kind::Declaration,0,hr,elements,count*sizeof(elements[0]));
    value(r,Kind::Arguments,0,S_OK,args);
    const std::uint32_t object_words[]={scope.valid,unsigned(scoped),scope.scope_depth,
        std::uint32_t(scope.session),std::uint32_t(scope.session>>32),scope.model,scope.lod,
        std::uint32_t(scope.node),scope.node_handle,std::uint32_t(scope.camera),scope.camera_handle,
        std::uint32_t(scope.mesh),std::uint32_t(scope.registry),std::uint32_t(scope.engine),
        scope.flags12c,scope.flags130,scope.parent,scope.alpha13c};
    value(r,Kind::Object,0,S_OK,object_words);value(r,Kind::Object,1,S_OK,scope.position);
    value(r,Kind::Object,2,S_OK,scope.basis);value(r,Kind::Object,3,S_OK,scope.scale);
    for(unsigned i=0;i<4;++i){const std::uint32_t* rows=i==0?scope.world:i==1?scope.world_basis:i==2?scope.view:scope.projection;
        add(r,Kind::Object,4+i,(scope.valid&(8u<<i))?S_OK:D3DERR_NOTAVAILABLE,rows,64);}

    return slot;
}
void Capture::binding(Record& r,Kind kind,unsigned index,HRESULT hr,IDirect3DResource9* resource) noexcept {
    std::uint64_t id=0;const HRESULT identity=resource?query_resource_id(resource,&id):S_OK;
    const std::uint32_t words[]={resource?1u:0u,std::uint32_t(reinterpret_cast<std::uintptr_t>(resource)),
        std::uint32_t(id),std::uint32_t(id>>32),std::uint32_t(identity)};
    // A missing existing private-data ID is recorded, never assigned here.
    value(r,kind,index,hr,words);
}
void Capture::surface(Record& r,unsigned index,HRESULT hr,IDirect3DSurface9* s) noexcept {
    // D3DERR_NOTFOUND denotes an unbound optional MRT/depth slot, not a failed query.
    if(hr==D3DERR_NOTFOUND&&!s){add(r,Kind::Target,index,hr,nullptr,0,false);return;}
    binding(r,Kind::Target,index,hr,s);if(!s)return;
    D3DSURFACE_DESC desc{};hr=s->GetDesc(&desc);value(r,Kind::SurfaceDesc,index,hr,desc);
    IDirect3DBaseTexture9* parent=nullptr;hr=s->GetContainer(IID_IDirect3DBaseTexture9,reinterpret_cast<void**>(&parent));
    // Plain surfaces legitimately have no texture container. Preserve that HRESULT.
    std::uint64_t id=0;HRESULT identity=parent?query_resource_id(parent,&id):S_OK;
    const std::uint32_t container[]={std::uint32_t(hr),std::uint32_t(reinterpret_cast<std::uintptr_t>(parent)),
        std::uint32_t(id),std::uint32_t(id>>32),std::uint32_t(identity)};
    value(r,Kind::Container,index,S_OK,container);if(parent)parent->Release();s->Release();
}
void Capture::effective(int slot,IDirect3DDevice9* d,const D3DCAPS9& caps,GetTarget get_target,const MotionRoute& route) noexcept {
    if(slot<0||slot>1||policy_.status!=Status::Armed)return;
    if(busy_){refuse(Status::Ambiguous);return;}
    busy_=true;struct Busy {bool& value;~Busy(){value=false;}} busy{busy_};
    auto& r=records_[slot];const auto begin=stamp();
    const DWORD capability[]={caps.MaxStreams,caps.MaxUserClipPlanes,caps.MaxVertexShaderConst,caps.VertexShaderVersion,caps.PixelShaderVersion,caps.NumSimultaneousRTs};
    value(r,Kind::Caps,0,S_OK,capability);
    struct Time {std::uint64_t& total;std::uint64_t begin,excluded=0;~Time(){total+=stamp()-begin-excluded;}} time{query_ticks_,begin};
    const std::uint32_t route_words[]={unsigned(route.gate),unsigned(route.unmatched),route.routed,route.matched,route.scene,
        route.submit,route.evaluated,route.composition,unsigned(route.composition_policy),route.cutout,route.alpha_tested,
        route.linear_material,route.depth,route.rt_set,route.rt2_set,route.jittered,route.jitter_register,route.native_mip_bias,
        std::uint32_t(route.preparation_error),std::uint32_t(route.submission_error)};
    value(r,Kind::Route,0,S_OK,route_words);
    if(!route.submit){refuse(Status::SubmissionFailed);return;} // no post-route queries for a suppressed draw
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    HRESULT hr=d->GetVertexShader(&vs);shader(r,Kind::Shader,0,hr,vs);
    hr=d->GetPixelShader(&ps);shader(r,Kind::Shader,1,hr,ps);
    IDirect3DVertexDeclaration9* decl=nullptr;hr=d->GetVertexDeclaration(&decl);
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT count=MAXD3DDECLLENGTH+1;
    if(SUCCEEDED(hr))hr=decl?decl->GetDeclaration(elements,&count):D3DERR_NOTAVAILABLE;
    if(decl)decl->Release();
    if(count>MAXD3DDECLLENGTH+1)hr=D3DERR_NOTAVAILABLE;
    add(r,Kind::Declaration,1,hr,elements,SUCCEEDED(hr)?count*sizeof(elements[0]):0);
    bool geometry_layout=SUCCEEDED(hr)&&count==6;
    if(geometry_.requested&&geometry_layout)for(unsigned i=0;i<6;++i){
        const D3DVERTEXELEMENT9 wanted=i==5?D3DVERTEXELEMENT9{0xff,0,D3DDECLTYPE_UNUSED,0,0,0}:
            D3DVERTEXELEMENT9{0,WORD(i*8),D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,BYTE(i==0?0:i==1?5:i==2?3:i==3?6:7),0};
        geometry_layout=geometry_layout&&!std::memcmp(elements+i,&wanted,sizeof wanted);
    }
    for(unsigned stage=0;stage<2;++stage){
        float f[256*4]{};int integers[16*4]{};BOOL booleans[16]{};
        const UINT n=stage==0?std::min<UINT>(caps.MaxVertexShaderConst,256):D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion)>=3?224:32;
        hr=!n?D3DERR_NOTAVAILABLE:stage==0?d->GetVertexShaderConstantF(0,f,n):d->GetPixelShaderConstantF(0,f,n);
        add(r,Kind::ConstantsF,stage,hr,f,n*16);
        hr=stage==0?d->GetVertexShaderConstantI(0,integers,16):d->GetPixelShaderConstantI(0,integers,16);
        value(r,Kind::ConstantsI,stage,hr,integers);
        hr=stage==0?d->GetVertexShaderConstantB(0,booleans,16):d->GetPixelShaderConstantB(0,booleans,16);
        value(r,Kind::ConstantsB,stage,hr,booleans);
    }
    for(auto state:raster){DWORD v=0;hr=d->GetRenderState(state,&v);value(r,Kind::Render,unsigned(state),hr,v);}
    D3DVIEWPORT9 vp{};hr=d->GetViewport(&vp);value(r,Kind::Viewport,0,hr,vp);
    RECT scissor{};hr=d->GetScissorRect(&scissor);value(r,Kind::Scissor,0,hr,scissor);
    // All supported planes, even when disabled; unsupported indices are not queried.
    for(unsigned i=0;i<std::min<unsigned>(caps.MaxUserClipPlanes,6);++i){float plane[4]{};hr=d->GetClipPlane(i,plane);value(r,Kind::Clip,i,hr,plane);}
    if(!caps.MaxStreams||caps.MaxStreams>16)refuse(Status::Unavailable);
    IDirect3DVertexBuffer9* held_vertex=nullptr;
    IDirect3DIndexBuffer9* held_index=nullptr;
    for(unsigned i=0;i<std::min<unsigned>(caps.MaxStreams,16);++i){
        IDirect3DVertexBuffer9* b=nullptr;UINT offset=0,stride=0,freq=0;
        hr=d->GetStreamSource(i,&b,&offset,&stride);binding(r,Kind::Stream,i,hr,b);
        const HRESULT frequency=d->GetStreamSourceFreq(i,&freq);const UINT stream[]={offset,stride,freq};
        // offset/stride belong to GetStreamSource, not the independent
        // frequency query: never publish their zero initializers on failure.
        value(r,Kind::StreamFrequency,i,FAILED(hr)?hr:frequency,stream);
        if(SUCCEEDED(hr)&&((i==0&&(!b||offset||stride!=40||freq!=1))||(i!=0&&b)))refuse(Status::Unavailable);
        if(b){D3DVERTEXBUFFER_DESC desc{};hr=b->GetDesc(&desc);value(r,Kind::VertexDesc,i,hr,desc);
            if(i==0&&geometry_.requested){held_vertex=b;geometry_layout=geometry_layout&&SUCCEEDED(hr)&&desc.Size==geometry::sizes[slot][0];}
            else b->Release();}
    }
    IDirect3DIndexBuffer9* ib=nullptr;hr=d->GetIndices(&ib);binding(r,Kind::Indices,0,hr,ib);
    if(!ib)refuse(Status::Unavailable);
    if(ib){D3DINDEXBUFFER_DESC desc{};hr=ib->GetDesc(&desc);value(r,Kind::IndexDesc,0,hr,desc);
        if(geometry_.requested){held_index=ib;geometry_layout=geometry_layout&&SUCCEEDED(hr)&&desc.Size==geometry::sizes[slot][1]&&desc.Format==D3DFMT_INDEX16;}
        else ib->Release();}
    for(unsigned i=0;i<std::min<unsigned>(caps.NumSimultaneousRTs,4);++i){IDirect3DSurface9* s=nullptr;hr=get_target(d,i,&s);surface(r,i,hr,s);}
    IDirect3DSurface9* depth=nullptr;hr=d->GetDepthStencilSurface(&depth);surface(r,4,hr,depth);
    for(unsigned stage:{0u,3u}){
        for(auto state:sampler){DWORD v=0;hr=d->GetSamplerState(stage,state,&v);value(r,Kind::Sampler,stage*16+unsigned(state),hr,v);}
        IDirect3DBaseTexture9* texture=nullptr;hr=d->GetTexture(stage,&texture);binding(r,Kind::Texture,stage,hr,texture);
        if(!texture)continue;
        const auto type=texture->GetType();const DWORD lod=texture->GetLOD(),levels=texture->GetLevelCount();
        const DWORD meta[]={DWORD(type),lod,levels};value(r,Kind::TextureLOD,stage,S_OK,meta);
        if(levels>32||!levels||type!=D3DRTYPE_TEXTURE){refuse(Status::Unavailable);texture->Release();continue;}
        auto* t=static_cast<IDirect3DTexture9*>(texture);
        for(UINT level=0;level<levels;++level){D3DSURFACE_DESC desc{};hr=t->GetLevelDesc(level,&desc);value(r,Kind::TextureDesc,stage*32+level,hr,desc);}
        texture->Release();
    }
    if(geometry_.requested){
        // All other getter/descriptor/Release callbacks have completed. Keep
        // both owned binding references alive while observing weak identities.
        ownership::CloneUploadPairRequest request;
        ownership::BufferLockView vertex{},index{};
        ownership::CloneUploadPinView pin{};
        request.slot=unsigned(slot);request.vertex_key=held_vertex;request.index_key=held_index;
        const bool views=geometry_.can_copy(unsigned(slot))&&geometry_layout&&held_vertex&&held_index&&
            ownership::get_buffer_lock_view(held_vertex,&vertex)==S_OK&&
            ownership::get_buffer_lock_view(held_index,&index)==S_OK&&
            vertex.known&&index.known&&vertex.quiet()&&index.quiet()&&
            vertex.generation&&vertex.generation==index.generation;
        if(views){
            ownership::query_clone_upload_pin(d,&pin);
            request.arm=pin.arm;request.generation=vertex.generation;
            request.vertex={vertex.allocation_id,vertex.revision};
            request.index={index.allocation_id,index.revision};
        }
        // Clear owning slots BEFORE the final Releases. The caller's QueryScope
        // remains active; only weak keys survive, and B1 never dereferences them.
        auto* release_vertex=held_vertex;held_vertex=nullptr;
        auto* release_index=held_index;held_index=nullptr;
        if(release_vertex)release_vertex->Release();
        if(release_index)release_index->Release();
        sync_geometry();
        if(policy_.status==Status::Armed&&geometry_.can_copy(unsigned(slot))){
            ownership::CloneUploadPairResult copied;
            HRESULT copied_hr=S_FALSE;
            if(views){
                const auto copy_begin=stamp();
                copied_hr=ownership::copy_clone_upload_pair(d,request,
                    geometry_.destination(unsigned(slot),0),geometry::sizes[slot][0],
                    geometry_.destination(unsigned(slot),1),geometry::sizes[slot][1],&copied);
                const auto copy_elapsed=stamp()-copy_begin;
                geometry_.copy_ticks+=copy_elapsed;time.excluded+=copy_elapsed;
            }
            const bool exact=copied_hr==S_OK&&copied.status==ownership::CloneUploadPairStatus::Copied&&
                copied.record.owner==request.arm.owner&&copied.record.generation==request.generation&&
                copied.record.buffers[0]==request.vertex&&copied.record.buffers[1]==request.index;
            geometry_.copied(unsigned(slot),FAILED(copied_hr)||(copied.status==ownership::CloneUploadPairStatus::Copied&&!exact)?"api_error":pair_name(copied.status),
                exact,request.arm.serial,copied.record,copied.binding_revision_match_at_observation);
        }
    }
}
void Capture::result(int slot,HRESULT hr,bool submitted) noexcept {
    if(slot<0||slot>1)return;
    records_[slot].result=hr;records_[slot].submitted=submitted;
    if(!submitted||FAILED(hr))refuse(Status::SubmissionFailed);
}
const char* Capture::kind_name(Kind k) noexcept {
    constexpr const char* names[]={"source_shader","declaration","object","arguments","route","shader","constants_f","constants_i","constants_b",
        "render","sampler","viewport","scissor","clip","stream","stream_frequency","vertex_desc","indices","index_desc",
        "target","surface_desc","container","texture","texture_desc","texture_lod","caps"};
    return names[unsigned(k)];
}
bool Capture::publish(const wchar_t* directory) noexcept {
    if(busy_){refuse(Status::Partial);return false;} // a reentrant Present cannot publish/free an active record
    if(!pending())return true;
    for(unsigned slot=0;slot<2;++slot)if(policy_.matches[slot]&&!records_[slot].submitted)refuse(Status::Partial);
    const auto status=policy_.finish();
    // No application resource retained, no D3D work. Present's full boundary
    // covers CRT formatting/allocation; only successful final close gets a filename.
    char filename[128]{};
    snprintf(filename,sizeof filename,"lattice-state-%lu-%llu-%llu-%llu.json",GetCurrentProcessId(),device_,frame_,generation_);
    wchar_t path[1024]{},temporary[1032]{};
    const int length=swprintf(path,1024,L"%ls\\lattice-state-%lu-%llu-%llu-%llu.json",directory,GetCurrentProcessId(),device_,frame_,generation_);
    if(geometry_.requested){
        geometry::State state;
        state.pid=GetCurrentProcessId();state.device=device_;state.frame=frame_;state.generation=generation_;
        state.status=status;state.scope_active=scope_active_;state.candidates=candidate_count_;state.query_ticks=query_ticks_;
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);state.qpc_frequency=std::uint64_t(frequency.QuadPart);
        for(unsigned slot=0;slot<2;++slot){
            state.matches[slot]=policy_.matches[slot];state.draw[slot]=records_[slot].draw;
            state.result[slot]=std::uint32_t(records_[slot].result);state.submitted[slot]=records_[slot].submitted;
        }
        char binary[128]{};
        snprintf(binary,sizeof binary,"lattice-geometry-%lu-%llu-%llu-%llu.bin",GetCurrentProcessId(),device_,frame_,generation_);
        geometry::WindowsFiles files(directory,filename,binary);
        const auto fields=[](FILE* stream,void* context,unsigned slot) noexcept -> bool {
            const auto& record=static_cast<Capture*>(context)->records_[slot];
            fputc('[',stream);
            for(unsigned i=0;i<record.count;++i){const auto& q=record.fields[i];if(i)fputc(',',stream);
                fprintf(stream,"{\"kind\":\"%s\",\"index\":%u,\"hr\":\"%08lx\",\"words\":[",kind_name(q.kind),q.index,q.hr);
                for(unsigned j=0;j<q.count;++j){if(j)fputc(',',stream);fprintf(stream,"\"%08x\"",record.words[q.offset+j]);}
                fputs("]}",stream);
            }
            return fputc(']',stream)!=EOF&&!ferror(stream);
        };
        const auto published=geometry::publish(geometry_,state,binary,fields,this,files);
        log("lattice_state device=%llu frame=%llu generation=%llu selector=%s status=%s matches=%u,%u file_ok=%u file=%s bytes=%ld pid=%lu draw_input_coherence=unqualified schema=2 payload_copy_valid=%u payload_file=%s payload_bytes=%u payload_sha256=%s",
            device_,frame_,generation_,selector_name,name(status),policy_.matches[0],policy_.matches[1],published.file_ok,filename,
            published.json_bytes,GetCurrentProcessId(),published.payload_valid,published.payload_valid?binary:"-",
            published.payload_valid?geometry::payload_bytes:0,published.payload_valid?published.sha256:"-");
        geometry_.erase();policy_.status=Status::Off;return true;
    }
    FILE* f=nullptr;bool ok=false;long bytes=0;
    if(length>0 && length<1024){swprintf(temporary,1032,L"%ls.tmp",path);f=_wfopen(temporary,L"wb");}
    if(f){
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        fprintf(f,"{\"schema\":1,\"selector\":\"%s\",\"status\":\"%s\",\"device\":%llu,\"frame\":%llu,\"generation\":%llu,\"draw_input_coherence\":\"unqualified\",\"payload_copy_valid\":\"not_attempted\",\"scope_active_at_arm\":%s,\"candidates\":%u,\"query_ticks\":%llu,\"qpc_frequency\":%llu,\"matches\":[%u,%u],\"records\":[",selector_name,name(status),device_,frame_,generation_,scope_active_?"true":"false",candidate_count_,query_ticks_,std::uint64_t(frequency.QuadPart),policy_.matches[0],policy_.matches[1]);
        for(unsigned slot=0;slot<2;++slot){auto& r=records_[slot];if(slot)fputc(',',f);
            fprintf(f,"{\"slot\":%u,\"draw\":%llu,\"submitted\":%s,\"result\":\"%08lx\",\"fields\":[",slot,r.draw,r.submitted?"true":"false",r.result);
            for(unsigned i=0;i<r.count;++i){const auto& q=r.fields[i];if(i)fputc(',',f);
                fprintf(f,"{\"kind\":\"%s\",\"index\":%u,\"hr\":\"%08lx\",\"words\":[",kind_name(q.kind),q.index,q.hr);
                for(unsigned j=0;j<q.count;++j){if(j)fputc(',',f);fprintf(f,"\"%08x\"",r.words[q.offset+j]);}fputs("]}",f);
            }fputs("]}",f);
        }fputs("]}\n",f);bytes=ftell(f);const bool written=!ferror(f)&&bytes>0&&bytes<=1024*1024;const int closed=fclose(f);ok=written&&closed==0;
        if(ok)ok=MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING)!=0;
    }
    log("lattice_state device=%llu frame=%llu generation=%llu selector=%s status=%s matches=%u,%u file_ok=%u file=%s bytes=%ld pid=%lu draw_input_coherence=unqualified payload_copy_valid=not_attempted",device_,frame_,generation_,selector_name,name(status),policy_.matches[0],policy_.matches[1],ok,filename,bytes,GetCurrentProcessId());
    policy_.status=Status::Off;return true;
}
}
