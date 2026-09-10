#include "draw_input.h"
#include "capture_state.h"
#include "../ownership/d3d9_ownership.h"
#include <cstring>

namespace x3m {
namespace {
template<class T> struct Ref {
    T* p=nullptr;
    ~Ref(){if(p)p->Release();}
    Ref()=default;
    Ref(const Ref&)=delete;
    Ref& operator=(const Ref&)=delete;
};
std::uint64_t hash(const void* data,std::size_t size) noexcept {
    auto bytes=static_cast<const unsigned char*>(data);
    std::uint64_t value=14695981039346656037ull;
    for(std::size_t i=0;i<size;++i){value^=bytes[i];value*=1099511628211ull;}
    return value;
}
template<class T> std::size_t program(T* shader,std::uint32_t* words,std::size_t capacity) noexcept {
    if(!shader)return 0;
    UINT bytes=0;
    if(FAILED(shader->GetFunction(nullptr,&bytes))||!bytes||bytes%4||bytes>capacity*4)return 0;
    const UINT expected=bytes;
    if(FAILED(shader->GetFunction(words,&bytes))||bytes!=expected)return 0;
    return bytes/4;
}
bool revision(IDirect3DResource9* resource,std::uint64_t& out) noexcept {
    ownership::BufferContentView view{};
    const auto hr=ownership::get_buffer_content_view(resource,&view);
    if(FAILED(hr)||FAILED(view.status)||!view.requested||!view.known||view.ambiguous||view.pending_locks||!view.revision)return false;
    out=view.revision;return true;
}
bool surface(IDirect3DSurface9* value,D3DSURFACE_DESC& desc,std::uint64_t& id) {
    return value&&SUCCEEDED(value->GetDesc(&desc))&&(id=resource_id(value))!=0;
}
std::uint32_t bits(float value) noexcept {
    std::uint32_t result;std::memcpy(&result,&value,sizeof result);return result;
}
}
DrawInput DrawInputReader::read(IDirect3DDevice9* device,const DrawArguments& args,
                                const object_trace::Snapshot* scope) noexcept {
    DrawInput result{};
    auto& observation=result.observation;auto& key=observation.key;
    auto reject=[&](std::uint32_t bits){result.blockers|=bits;};
    if(!device){reject(QueryFailure);return result;}
    try {
        key.indexed=args.method==DrawMethod::Indexed;
        key.topology=args.topology;key.first=args.first;key.primitives=args.primitives;
        if(key.indexed){key.base_vertex=args.base_vertex;key.min_vertex=args.minimum_vertex;key.vertex_count=args.vertex_count;}
        if(args.method!=DrawMethod::Primitive&&args.method!=DrawMethod::Indexed)reject(UserMemory);
        if(!args.primitives||(args.topology!=D3DPT_TRIANGLELIST&&args.topology!=D3DPT_TRIANGLESTRIP))reject(DrawRange);
        if(scope && (scope->valid&(object_trace::Node|object_trace::Camera|object_trace::Registry))==
                    (object_trace::Node|object_trace::Camera|object_trace::Registry) &&
           scope->scope_depth&&scope->node&&scope->camera&&scope->mesh&&scope->registry&&
           scope->node_handle&&scope->camera_handle){
            key.node=scope->node;key.camera=scope->camera;key.mesh=scope->mesh;
            key.node_handle=scope->node_handle;key.camera_handle=scope->camera_handle;
            key.model=scope->model;key.lod=scope->lod;
        }else reject(ObjectScope);
        // Session, addresses and handles are NOT lifetime tokens. Only a future
        // verified lifecycle producer may set LifetimeVerified/nonzero lifetimes.
        Ref<IDirect3DVertexShader9> vs;Ref<IDirect3DPixelShader9> ps;
        if(FAILED(device->GetVertexShader(&vs.p))||FAILED(device->GetPixelShader(&ps.p)))reject(QueryFailure);
        auto words=program(vs.p,shader_words_.data(),shader_words_.size());
        const renderer::RigidPositionProfile* profile=nullptr;
        if(words){
            result.vertex_program=hash(shader_words_.data(),words*4);
            result.position_path=renderer::classify_vertex_position(shader_words_.data(),words);
            result.replay_source=renderer::qualify_rigid_replay_source(shader_words_.data(),words);
#ifdef X3M_DRAW_INPUT_FIXTURE
            profile=position_lookup_(shader_words_.data(),words);
#else
            profile=renderer::find_rigid_position(shader_words_.data(),words);
#endif
        }
        UINT matrix_register=0;
        if(profile){key.position_program=profile->hash;matrix_register=profile->matrix_register;}
        else reject(PositionProgram);
        words=program(ps.p,shader_words_.data(),shader_words_.size());
        bool pixel_reviewed=false;
        if(words){
            result.pixel_program=hash(shader_words_.data(),words*4);
#ifdef X3M_DRAW_INPUT_FIXTURE
            pixel_reviewed=pixel_lookup_(shader_words_.data(),words)!=nullptr;
#else
            pixel_reviewed=renderer::find_pixel_coverage(shader_words_.data(),words)!=nullptr;
#endif
        }
        if(!pixel_reviewed)reject(PixelCoverage);
        // Clear all scratch regardless of shader length; no game programs persist
        // in a reader between calls, including rejected or shorter programs.
        shader_words_.fill(0);
        if(profile){
            if(FAILED(device->GetVertexShaderConstantF(matrix_register,observation.submitted_wvp.data(),4)))reject(SubmittedRows|QueryFailure);
            for(const auto value:observation.submitted_wvp)
                if((bits(value)&0x7fffffffu)>bits(1e15f))reject(SubmittedRows);
        }else reject(SubmittedRows);

        Ref<IDirect3DVertexDeclaration9> declaration;
        D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT count=MAXD3DDECLLENGTH+1;
        bool layout=false;
        if(SUCCEEDED(device->GetVertexDeclaration(&declaration.p))&&declaration.p&&
           SUCCEEDED(declaration.p->GetDeclaration(elements,&count))&&count>1&&count<=MAXD3DDECLLENGTH+1&&
           elements[count-1].Stream==0xff){
            unsigned positions=0;
            for(UINT i=0;i<count-1;++i){const auto& element=elements[i];
                if(element.Stream==0xff){positions=2;break;}
                if(element.Usage==D3DDECLUSAGE_POSITION&&element.UsageIndex==0){
                    ++positions;key.position_offset=element.Offset;key.position_type=element.Type;
                    layout=element.Stream==0&&element.Method==D3DDECLMETHOD_DEFAULT&&
                           (element.Type==D3DDECLTYPE_FLOAT3||element.Type==D3DDECLTYPE_FLOAT16_4);
                }
            }
            layout=layout&&positions==1;
            if(layout)key.declaration=hash(elements,count*sizeof(elements[0]));
        }
        if(!layout)reject(PositionLayout);
        Ref<IDirect3DVertexBuffer9> vb;UINT frequency=0;D3DVERTEXBUFFER_DESC vb_desc{};
        const HRESULT stream_hr=device->GetStreamSource(0,&vb.p,&key.stream_offset,&key.stride);
        const HRESULT frequency_hr=device->GetStreamSourceFreq(0,&frequency);
        if(FAILED(stream_hr)||!vb.p||FAILED(vb.p->GetDesc(&vb_desc))||!(key.vertex_buffer=resource_id(vb.p)))reject(BufferDescription);
        if(FAILED(frequency_hr)||frequency!=1)reject(PositionLayout);
        if(!revision(vb.p,key.vertex_revision))reject(BufferRevision);
        const UINT position_bytes=key.position_type==D3DDECLTYPE_FLOAT16_4?8:12;
        if(!key.stride||key.position_offset>key.stride||position_bytes>key.stride-key.position_offset)reject(PositionLayout);
        // Widen before all range arithmetic. No buffer payload reads/locks, even
        // for WRITEONLY resources. Indexed metadata uses the API min/count;
        // the separate finite gate also requires observed actual IB extrema.
        std::uint64_t last=0;
        std::int64_t first_vertex=args.first;
        const std::uint64_t consumed=args.topology==D3DPT_TRIANGLELIST?std::uint64_t(args.primitives)*3:std::uint64_t(args.primitives)+2;
        if(key.indexed){
            const std::int64_t first=std::int64_t(args.base_vertex)+args.minimum_vertex;
            first_vertex=first;
            if(first<0||!args.vertex_count)reject(DrawRange);
            else last=std::uint64_t(first)+args.vertex_count-1;
            Ref<IDirect3DIndexBuffer9> ib;D3DINDEXBUFFER_DESC ib_desc{};
            if(FAILED(device->GetIndices(&ib.p))||!ib.p||FAILED(ib.p->GetDesc(&ib_desc))||!(key.index_buffer=resource_id(ib.p)))reject(BufferDescription);
            key.index_format=ib_desc.Format;
            if(!revision(ib.p,key.index_revision))reject(BufferRevision);
            const UINT bytes=ib_desc.Format==D3DFMT_INDEX16?2:ib_desc.Format==D3DFMT_INDEX32?4:0;
            if(!bytes||(std::uint64_t(args.first)+consumed)>ib_desc.Size/bytes)reject(DrawRange);
            if(!(result.blockers&(BufferDescription|BufferRevision|DrawRange|UserMemory))){
                ownership::IndexRangeRequest request{};
                request.expected_revision=key.index_revision;request.format=ib_desc.Format;
                request.start_index=args.first;request.index_count=consumed;
                const auto hr=ownership::get_index_range_view(ib.p,request,&result.indices);
                const auto& view=result.indices;
                result.index_range_verified=hr==S_OK&&view.status==S_OK&&view.requested&&
                    view.generation&&view.revision==key.index_revision&&view.known&&
                    view.minimum<=view.maximum&&view.minimum>=args.minimum_vertex&&
                    std::uint64_t(view.maximum)<std::uint64_t(args.minimum_vertex)+args.vertex_count&&
                    first_vertex>=0;
            }
        }else{
            // A bound IB is irrelevant to DrawPrimitive. Its fields remain zero.
            last=std::uint64_t(args.first)+consumed-1;
        }
        const std::uint64_t start=std::uint64_t(key.stream_offset)+key.position_offset;
        if(!key.stride||start>vb_desc.Size||position_bytes>vb_desc.Size-start||
           last>(vb_desc.Size-start-position_bytes)/key.stride)reject(DrawRange);

        // Query classifications only; no additional Lock, payload read or shader
        // execution occurs here. Whole-IB bounds may conservatively reject a
        // subdraw. Never use declared min/count alone as index content evidence.
        constexpr auto geometry_blockers=PositionLayout|BufferDescription|BufferRevision|DrawRange|UserMemory;
        if(!key.indexed&&!(result.blockers&geometry_blockers))result.index_range_verified=true;
        if(!(result.blockers&geometry_blockers)&&result.index_range_verified){
            ownership::FinitePositionRequest request{};
            request.expected_revision=key.vertex_revision;request.stream_offset=key.stream_offset;
            request.stride=key.stride;request.position_offset=key.position_offset;
            request.first_vertex=first_vertex;request.vertex_count=key.indexed?args.vertex_count:consumed;
            request.position_type=static_cast<D3DDECLTYPE>(key.position_type);
            const auto hr=ownership::get_finite_position_view(vb.p,request,&result.finite_positions);
            const auto& view=result.finite_positions;
            result.vertex_finite_verified=hr==S_OK&&view.status==S_OK&&view.requested&&
                view.state==ownership::FiniteStatus::Finite&&view.generation&&
                view.revision==key.vertex_revision&&(!key.indexed||view.generation==result.indices.generation);
        }else{
            result.finite_positions.reason=(result.blockers&PositionLayout)?ownership::FiniteEvidenceReason::InvalidLayout:
                (result.blockers&(DrawRange|UserMemory))?ownership::FiniteEvidenceReason::InvalidRange:
                (result.blockers&BufferRevision)?ownership::FiniteEvidenceReason::TrackingUnavailable:
                (result.blockers&BufferDescription)?ownership::FiniteEvidenceReason::MissingAllocation:
                ownership::FiniteEvidenceReason::IndexUnknown;
        }

        D3DCAPS9 caps{};
        if(FAILED(device->GetDeviceCaps(&caps))||caps.NumSimultaneousRTs<1||caps.NumSimultaneousRTs>4||caps.MaxStreams<1||caps.MaxStreams>16)reject(TargetLayout|QueryFailure);
        else {
            for(UINT i=1;i<caps.MaxStreams;++i){UINT f=0;if(FAILED(device->GetStreamSourceFreq(i,&f))||f!=1)reject(PositionLayout);}
            for(UINT i=1;i<caps.NumSimultaneousRTs;++i){Ref<IDirect3DSurface9> extra;const auto hr=device->GetRenderTarget(i,&extra.p);if(extra.p||(FAILED(hr)&&hr!=D3DERR_NOTFOUND))reject(TargetLayout);}
        }
        Ref<IDirect3DSurface9> color,depth;D3DSURFACE_DESC color_desc{},depth_desc{};D3DVIEWPORT9 viewport{};
        if(FAILED(device->GetRenderTarget(0,&color.p))||FAILED(device->GetDepthStencilSurface(&depth.p))||
           !surface(color.p,color_desc,result.color_target)||!surface(depth.p,depth_desc,result.depth_target)||
           FAILED(device->GetViewport(&viewport)))reject(TargetLayout|QueryFailure);
        if(!color_desc.Width||!color_desc.Height||color_desc.MultiSampleType!=D3DMULTISAMPLE_NONE||
           depth_desc.Format!=D3DFMT_D24X8||depth_desc.MultiSampleType!=D3DMULTISAMPLE_NONE||
           depth_desc.Width!=color_desc.Width||depth_desc.Height!=color_desc.Height||
           viewport.X||viewport.Y||viewport.Width!=color_desc.Width||viewport.Height!=color_desc.Height||
            (bits(viewport.MinZ)&0x7fffffffu)!=0||bits(viewport.MaxZ)!=bits(1.0f))reject(TargetLayout);
        result.width=color_desc.Width;result.height=color_desc.Height;
        key.draw_domain=result.color_target; // Caller epoch must additionally pin DS/coordinate regime.
        struct State {D3DRENDERSTATETYPE name;DWORD value;};
        State states[]={{D3DRS_ZENABLE,1},{D3DRS_ZWRITEENABLE,1},{D3DRS_ALPHATESTENABLE,0},
            {D3DRS_ALPHABLENDENABLE,0},{D3DRS_STENCILENABLE,0},{D3DRS_FILLMODE,D3DFILL_SOLID},
            {D3DRS_SCISSORTESTENABLE,0},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_CLIPPING,1}};
        for(const auto state:states){DWORD value=0;if(FAILED(device->GetRenderState(state.name,&value))||value!=state.value)reject(RasterState);}
        DWORD zfunc=0,cull=0,write=0,bias=0,slope=0;
        if(FAILED(device->GetRenderState(D3DRS_ZFUNC,&zfunc))||
           FAILED(device->GetRenderState(D3DRS_CULLMODE,&cull))||
           FAILED(device->GetRenderState(D3DRS_COLORWRITEENABLE,&write))||
           FAILED(device->GetRenderState(D3DRS_DEPTHBIAS,&bias))||
           FAILED(device->GetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,&slope)))reject(RasterState|QueryFailure);
        if((zfunc!=D3DCMP_LESS&&zfunc!=D3DCMP_LESSEQUAL)||cull<D3DCULL_NONE||cull>D3DCULL_CCW||
           (write&7)!=7||(bias&0x7fffffffu)||(slope&0x7fffffffu))reject(RasterState);
        result.cull=static_cast<D3DCULL>(cull);
        if(!(result.blockers&(PositionProgram|PositionLayout|SubmittedRows)))observation.proofs|=renderer::PositionReviewed;
        if(!(result.blockers&(BufferDescription|BufferRevision|DrawRange|UserMemory)))observation.proofs|=renderer::GeometryUnchanged;
        if(!(result.blockers&(PositionProgram|PositionLayout|BufferDescription|DrawRange|SubmittedRows|PixelCoverage|RasterState|TargetLayout|UserMemory)))observation.proofs|=renderer::CoverageSupported;
        if(result.blockers&QueryFailure)observation.proofs=0;
        if(result.blockers&PositionLayout)result.vertex_finite_verified=false;
    }catch(...){shader_words_.fill(0);reject(QueryFailure);observation.proofs=0;result.vertex_finite_verified=false;result.index_range_verified=false;}
    return result;
}
void DrawInputReader::complete(DrawInput& input,HRESULT result) noexcept {
    if(SUCCEEDED(result))input.observation.proofs|=renderer::SubmissionSucceeded;
    else {input.blockers|=SubmissionFailure;input.observation.proofs&=~renderer::SubmissionSucceeded;}
}
} // namespace x3m
