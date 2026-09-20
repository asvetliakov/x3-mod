// Authored fixture-only state/fault qualification. Included after fixture utilities.
// Intercepts documented COM methods with exact WINAPI signatures; no code patch,
// native/backend object fields, or private exported entry point is used.
#include "../../src/renderer/quad_vertex_program.h"
#include "../../src/proxy/cpu_state.h"
#include <array>
#include <algorithm>
#include <memory>
#include <utility>
namespace { namespace fog_spatial_state {
using Device=IDirect3DDevice9*;
struct Gap:std::runtime_error{using std::runtime_error::runtime_error;};
struct Hooks;
inline Hooks* active=nullptr;
struct Fault{unsigned slot=0,at=0;HRESULT error=E_FAIL;bool after=false;};
struct Hooks {
    Device device;IDirect3D9* api;void** native;void** api_native;
    void* methods[119]{};void* api_methods[17]{};
    std::array<unsigned,119> calls{};
    Fault fault{},fault2{};bool observed_lost=false;unsigned calls_after_lost=0,injected_count=0,copy_inside_scene=0,draw_outside_scene=0;bool native_scene_open=false;
    enum CapsMode{Real,Ps2,Vs2,Slots,ConditionalNpot,Square,SmallAtlas,NoMinLinear,NoMagLinear,NoStretch,TooManyStreams} caps_mode=Real;
    D3DFORMAT refuse_format=D3DFMT_UNKNOWN;DWORD refuse_usage=0;
    const Fault* tick(unsigned slot){if(observed_lost)++calls_after_lost;const unsigned n=++calls[slot];if(n==fault.at&&slot==fault.slot)return &fault;if(n==fault2.at&&slot==fault2.slot)return &fault2;return nullptr;}
    HRESULT injected(const Fault& f){++injected_count;SetLastError(0xbad19);if(f.error==D3DERR_DEVICELOST||f.error==D3DERR_DEVICENOTRESET)observed_lost=true;return f.error;}
    void clear(){calls={};fault={};fault2={};observed_lost=false;calls_after_lost=0;injected_count=0;copy_inside_scene=draw_outside_scene=0;}
    unsigned writes()const{unsigned n=0;for(unsigned slot:{23u,31u,34u,37u,39u,41u,42u,47u,57u,59u,65u,67u,69u,75u,83u,86u,87u,91u,92u,94u,100u,102u,104u,106u,107u,109u})n+=calls[slot];return n;}
    template<unsigned Slot,class... Args> static HRESULT WINAPI intercept(Device d,Args... args){
        auto& h=*active;const Fault* fail=h.tick(Slot);
        if constexpr(Slot==34){if(h.native_scene_open)++h.copy_inside_scene;}
        if constexpr(Slot==83){if(!h.native_scene_open)++h.draw_outside_scene;}
        if(fail&&!fail->after)return h.injected(*fail);
        using Function=HRESULT(WINAPI*)(Device,Args...);
        const HRESULT hr=reinterpret_cast<Function>(h.native[Slot])(d,args...);
        if constexpr(Slot==41){if(SUCCEEDED(hr))h.native_scene_open=true;}
        if constexpr(Slot==42){if(SUCCEEDED(hr))h.native_scene_open=false;}
        return fail&&SUCCEEDED(hr)?h.injected(*fail):hr;
    }
    static HRESULT WINAPI caps(Device d,D3DCAPS9* out){
        auto& h=*active;using Fn=HRESULT(WINAPI*)(Device,D3DCAPS9*);
        HRESULT hr=reinterpret_cast<Fn>(h.native[7])(d,out);if(FAILED(hr))return hr;
        switch(h.caps_mode){
        case Ps2:out->PixelShaderVersion=D3DPS_VERSION(2,0);break;
        case Vs2:out->VertexShaderVersion=D3DVS_VERSION(2,0);break;
        case Slots:out->MaxPixelShader30InstructionSlots=1;break;
        case ConditionalNpot:out->TextureCaps|=D3DPTEXTURECAPS_POW2|D3DPTEXTURECAPS_NONPOW2CONDITIONAL;break;
        case Square:out->TextureCaps|=D3DPTEXTURECAPS_SQUAREONLY;break;
        case SmallAtlas:out->MaxTextureWidth=1024;break;
        case NoMinLinear:out->TextureFilterCaps&=~D3DPTFILTERCAPS_MINFLINEAR;break;
        case NoMagLinear:out->TextureFilterCaps&=~D3DPTFILTERCAPS_MAGFLINEAR;break;
        case NoStretch:out->DevCaps2&=~D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES;break;
        case TooManyStreams:out->MaxStreams=17;break;
        default:break;
        }return hr;
    }
    static HRESULT WINAPI format(IDirect3D9* a,UINT adapter,D3DDEVTYPE type,D3DFORMAT display,DWORD usage,D3DRESOURCETYPE resource,D3DFORMAT fmt){
        auto& h=*active;if(fmt==h.refuse_format&&usage==h.refuse_usage)return D3DERR_NOTAVAILABLE;
        using Fn=HRESULT(WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,D3DFORMAT,DWORD,D3DRESOURCETYPE,D3DFORMAT);
        return reinterpret_cast<Fn>(h.api_native[10])(a,adapter,type,display,usage,resource,fmt);
    }
    Hooks(Device d,IDirect3D9* a):device(d),api(a),native(*reinterpret_cast<void***>(d)),api_native(*reinterpret_cast<void***>(a)){
        if(active)throw std::runtime_error("nested device interception");
        active=this;
        std::memcpy(methods,native,sizeof methods);std::memcpy(api_methods,api_native,sizeof api_methods);
#define HOOK(slot,...) methods[slot]=reinterpret_cast<void*>(&intercept<slot,__VA_ARGS__>)
        HOOK(23,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
        HOOK(31,IDirect3DBaseTexture9*,IDirect3DBaseTexture9*);
        HOOK(34,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE);
        HOOK(37,DWORD,IDirect3DSurface9*);HOOK(39,IDirect3DSurface9*);
        methods[41]=reinterpret_cast<void*>(&intercept<41>);methods[42]=reinterpret_cast<void*>(&intercept<42>);
        HOOK(47,const D3DVIEWPORT9*);HOOK(48,D3DVIEWPORT9*);HOOK(57,D3DRENDERSTATETYPE,DWORD);HOOK(59,D3DSTATEBLOCKTYPE,IDirect3DStateBlock9**);
        HOOK(65,DWORD,IDirect3DBaseTexture9*);HOOK(67,DWORD,D3DTEXTURESTAGESTATETYPE,DWORD);HOOK(69,DWORD,D3DSAMPLERSTATETYPE,DWORD);HOOK(75,const RECT*);
        HOOK(83,D3DPRIMITIVETYPE,UINT,const void*,UINT);HOOK(86,const D3DVERTEXELEMENT9*,IDirect3DVertexDeclaration9**);HOOK(87,IDirect3DVertexDeclaration9*);
        HOOK(91,const DWORD*,IDirect3DVertexShader9**);HOOK(92,IDirect3DVertexShader9*);HOOK(94,UINT,const float*,UINT);
        HOOK(100,UINT,IDirect3DVertexBuffer9*,UINT,UINT);HOOK(101,UINT,IDirect3DVertexBuffer9**,UINT*,UINT*);HOOK(102,UINT,UINT);HOOK(103,UINT,UINT*);HOOK(104,IDirect3DIndexBuffer9*);
        HOOK(106,const DWORD*,IDirect3DPixelShader9**);HOOK(107,IDirect3DPixelShader9*);HOOK(109,UINT,const float*,UINT);
#undef HOOK
        methods[7]=reinterpret_cast<void*>(&caps);api_methods[10]=reinterpret_cast<void*>(&format);
        *reinterpret_cast<void***>(d)=methods;*reinterpret_cast<void***>(a)=api_methods;
    }
    ~Hooks(){*reinterpret_cast<void***>(device)=native;*reinterpret_cast<void***>(api)=api_native;active=nullptr;}
};
struct Snapshot {
    struct Field{std::string name;size_t offset,size;};
    std::vector<unsigned char> bytes;std::vector<Field> fields;std::string label;
    void tag(const std::string& name){label=name;}
    template<class T>void add(const T& value){fields.push_back({label,bytes.size(),sizeof value});const auto* p=reinterpret_cast<const unsigned char*>(&value);bytes.insert(bytes.end(),p,p+sizeof value);}
    template<class T>void object(T* value){add(reinterpret_cast<std::uintptr_t>(value));if(value)value->Release();}
    explicit Snapshot(Device d,const D3DCAPS9& caps){
        for(UINT i=0;i<caps.NumSimultaneousRTs;++i){tag("rt["+std::to_string(i)+"]");IDirect3DSurface9* s=nullptr;HRESULT hr=d->GetRenderTarget(i,&s);add(hr);object(s);}
        tag("depth_binding");IDirect3DSurface9* ds=nullptr;add(d->GetDepthStencilSurface(&ds));object(ds);
        tag("viewport");D3DVIEWPORT9 vp{};check(d->GetViewport(&vp),"state viewport");add(vp);tag("scissor");RECT rect{};check(d->GetScissorRect(&rect),"state scissor");add(rect);
        tag("vertex_shader");IDirect3DVertexShader9* vs=nullptr;check(d->GetVertexShader(&vs),"state vs");object(vs);
        tag("pixel_shader");IDirect3DPixelShader9* ps=nullptr;check(d->GetPixelShader(&ps),"state ps");object(ps);
        tag("vertex_declaration");IDirect3DVertexDeclaration9* decl=nullptr;check(d->GetVertexDeclaration(&decl),"state declaration");object(decl);tag("fvf");DWORD fvf=0;check(d->GetFVF(&fvf),"state fvf");add(fvf);
        for(UINT i=0;i<caps.MaxStreams;++i){IDirect3DVertexBuffer9* vb=nullptr;UINT offset=0,stride=0,freq=0;check(d->GetStreamSource(i,&vb,&offset,&stride),"state stream");check(d->GetStreamSourceFreq(i,&freq),"state stream freq");tag("stream["+std::to_string(i)+"].buffer");object(vb);tag("stream["+std::to_string(i)+"].offset");add(offset);tag("stream["+std::to_string(i)+"].stride");add(stride);tag("stream["+std::to_string(i)+"].frequency");add(freq);}
        tag("indices");IDirect3DIndexBuffer9* ib=nullptr;check(d->GetIndices(&ib),"state indices");object(ib);
        for(UINT i=0;i<20;++i){UINT sampler=i<16?i:D3DVERTEXTEXTURESAMPLER0+i-16;IDirect3DBaseTexture9* t=nullptr;check(d->GetTexture(sampler,&t),"state texture");tag("texture["+std::to_string(sampler)+"]");object(t);
            for(UINT k=1;k<=13;++k){tag("sampler["+std::to_string(sampler)+"].state["+std::to_string(k)+"]");DWORD value=0;add(d->GetSamplerState(sampler,D3DSAMPLERSTATETYPE(k),&value));add(value);}}
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_INDEXEDVERTEXBLENDENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ANTIALIASEDLINEENABLE,D3DRS_VERTEXBLEND,D3DRS_FILLMODE,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3,D3DRS_MULTISAMPLEMASK,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP}){DWORD value=0;check(d->GetRenderState(state,&value),"state render");tag("render["+std::to_string(state)+"]");add(value);}
        for(UINT i=0;i<8;++i){DWORD value=0;check(d->GetRenderState(D3DRENDERSTATETYPE(D3DRS_WRAP0+i),&value),"state wrap");tag("wrap["+std::to_string(i)+"]");add(value);}
        for(UINT i=0;i<8;++i)for(auto state:{D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS}){DWORD value=0;check(d->GetTextureStageState(i,state,&value),"state stage");tag("stage["+std::to_string(i)+"].state["+std::to_string(state)+"]");add(value);}
        float pc[88]{},vc[32]{};check(d->GetPixelShaderConstantF(0,pc,22),"state ps constants");check(d->GetVertexShaderConstantF(0,vc,8),"state vs constants");tag("ps_float_constants");add(pc);tag("vs_float_constants");add(vc);
    }
    bool operator==(const Snapshot& other)const{return bytes==other.bytes;}
    void differences(const Snapshot& other)const{
        if(bytes.size()!=other.bytes.size()){std::printf("STATE_DIFF layout_before=%u layout_after=%u\n",unsigned(bytes.size()),unsigned(other.bytes.size()));return;}
        unsigned mismatches=0,printed=0;
        for(const auto& field:fields){
            unsigned changed=0;size_t first=field.size;
            for(size_t i=0;i<field.size;++i)if(bytes[field.offset+i]!=other.bytes[field.offset+i]){++changed;first=std::min(first,i);}
            if(!changed)continue;
            ++mismatches;if(printed++>=12)continue;
            const size_t word_offset=first&~size_t(3);DWORD before=0,after=0;
            std::memcpy(&before,bytes.data()+field.offset+word_offset,std::min(size_t(4),field.size-word_offset));
            std::memcpy(&after,other.bytes.data()+field.offset+word_offset,std::min(size_t(4),field.size-word_offset));
            std::printf("STATE_DIFF field=%s field_offset=%u changed_bytes=%u word_offset=%u before=%08lx after=%08lx\n",field.name.c_str(),unsigned(field.offset),changed,unsigned(word_offset),(unsigned long)before,(unsigned long)after);
        }
        std::printf("STATE_DIFF_SUMMARY mismatched_fields=%u emitted=%u\n",mismatches,std::min(printed,12u));
    }
};
std::vector<unsigned char> surface_bytes(Device d,IDirect3DSurface9* surface,UINT stride,bool lockable_depth=false){
    D3DSURFACE_DESC desc{};check(surface->GetDesc(&desc),"surface bytes desc");Com<IDirect3DSurface9> copy;IDirect3DSurface9* source=surface;
    if(!lockable_depth){check(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy.p,nullptr),"surface bytes staging");check(d->GetRenderTargetData(surface,copy.p),"surface bytes read");source=copy.p;}
    D3DLOCKED_RECT lock{};check(source->LockRect(&lock,nullptr,D3DLOCK_READONLY),"surface bytes lock");std::vector<unsigned char> bytes(size_t(desc.Width)*desc.Height*stride);
    for(UINT y=0;y<desc.Height;++y)std::memcpy(bytes.data()+size_t(y)*desc.Width*stride,static_cast<char*>(lock.pBits)+y*lock.Pitch,desc.Width*stride);
    check(source->UnlockRect(),"surface bytes unlock");return bytes;
}
struct Inputs {
    std::vector<float> constants,depth;std::vector<std::uint16_t> scene;UINT w=64,h=48;
    fog_field::Profile profile=fog_field::Profile::None;
    explicit Inputs(const std::string& cases){
        std::ifstream list(cases);std::string id,dir,profile_text;
        if(!std::getline(list,id)||!std::getline(list,dir)||!std::getline(list,profile_text))throw std::runtime_error("state input list");
        profile=static_cast<fog_field::Profile>(std::stoul(profile_text));constants=read<float>(dir+"/constants.f32");
        if(constants.size()!=32)throw std::runtime_error("state constants");
        auto source_depth=read<float>(dir+"/depth.rgba32f");auto source_scene=read<std::uint16_t>(dir+"/scene.rgba16f");
        UINT sw=UINT(constants[4]),sh=UINT(constants[5]);if(!sw||!sh||source_depth.size()!=size_t(sw)*sh*4||source_scene.size()!=source_depth.size())throw std::runtime_error("state input size");
        depth.resize(size_t(w)*h*4);scene.resize(depth.size());
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)for(UINT k=0;k<4;++k){
            size_t from=(size_t(y*sh/h)*sw+x*sw/w)*4+k,to=(size_t(y)*w+x)*4+k;
            depth[to]=source_depth[from];scene[to]=source_scene[from];
        }
        constants[4]=float(w);constants[5]=float(h);constants[6]=float((w+1)/2);constants[7]=float((h+1)/2);
    }
};
FogFrame make_frame(FogPass& pass,IDirect3DTexture9* depth,IDirect3DSurface9* scene,const std::vector<float>& c){
    FogFrame f;f.depth_share=depth;f.target=scene;f.width=UINT(c[4]);f.height=UINT(c[5]);
    f.profile=pass.field_profile();f.recipe_id=pass.field_recipe();f.field_generation=pass.field_generation();
    f.main_target=f.linear_depth_current=f.caller_scene_known=f.caller_queries_idle=true;f.caller_scene_open=false;
    f.params.m00=c[0];f.params.m11=c[1];f.params.m20=c[2];f.params.m21=c[3];f.params.density_scale=1;
    for(unsigned i=0;i<3;++i){f.params.world.origin_mod[i]=c[8+i];f.params.world.sun_world[i]=c[12+i];for(unsigned j=0;j<3;++j)f.params.world.inverse_columns[3*i+j]=c[16+4*i+j];}
    f.params.world.valid=true;return f;
}
struct Scene {
    Device d;const Inputs& input;D3DCAPS9 caps;
    Com<IDirect3DTexture9> rt0,rt1,rt2,spare,scene_upload;
    Com<IDirect3DSurface9> s0,s1,s2,z;
    Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> ps;
    Scene(Device device,const Inputs& data,const D3DCAPS9& dc,const std::vector<DWORD>& shader):d(device),input(data),caps(dc){
        if(caps.NumSimultaneousRTs<3)throw Gap("three_mrt_fixture");
        if(!(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS))throw Gap("mixed_mrt_bitdepths_fixture");
        upload(d,input.w,input.h,D3DFMT_A16B16G16R16F,8,input.scene.data(),&rt0.p,true);check(rt0->GetSurfaceLevel(0,&s0.p),"state rt0 surface");
        upload(d,input.w,input.h,D3DFMT_A16B16G16R16F,8,input.scene.data(),&rt1.p,true);check(rt1->GetSurfaceLevel(0,&s1.p),"state rt1 surface");
        upload(d,input.w,input.h,D3DFMT_A32B32G32R32F,16,input.depth.data(),&rt2.p,true);check(rt2->GetSurfaceLevel(0,&s2.p),"state rt2 surface");
        check(d->CreateTexture(4,4,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&spare.p,nullptr),"state spare texture");
        HRESULT hr=d->CreateDepthStencilSurface(input.w,input.h,D3DFMT_D16_LOCKABLE,D3DMULTISAMPLE_NONE,0,FALSE,&z.p,nullptr);
        if(FAILED(hr))throw Gap("D16_LOCKABLE_depth_bytes");
        check(d->SetRenderTarget(0,s0.p),"state initial rt0");check(d->SetDepthStencilSurface(z.p),"state initial depth");
        check(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.375f,0),"state depth pattern");
        check(d->CreateVertexBuffer(128,0,0,D3DPOOL_DEFAULT,&vb.p,nullptr),"state vertex buffer");check(d->CreateIndexBuffer(12,0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib.p,nullptr),"state index buffer");
        check(d->CreateVertexShader(reinterpret_cast<const DWORD*>(x3m::renderer::quad_vertex_program()),&vs.p),"state vertex shader");check(d->CreatePixelShader(shader.data(),&ps.p),"state pixel shader");
        check(d->CreateTexture(input.w,input.h,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&scene_upload.p,nullptr),"state upload");
        D3DLOCKED_RECT lock{};check(scene_upload->LockRect(0,&lock,nullptr,0),"state upload lock");for(UINT y=0;y<input.h;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,input.scene.data()+size_t(y)*input.w*4,input.w*8);check(scene_upload->UnlockRect(0),"state upload unlock");
    }
    void refill(){
        // UpdateTexture consumes the source dirty region. Every reuse must mark
        // the full immutable source dirty, even though CPU bytes did not change.
        check(scene_upload->AddDirtyRect(nullptr),"state refill dirty region");
        check(d->UpdateTexture(scene_upload.p,rt0.p),"state scene refill");
    }
    FogFrame frame(FogPass& pass)const{return make_frame(pass,rt2.p,s0.p,input.constants);}
    void hostile(){
        check(d->SetRenderTarget(0,s0.p),"hostile rt0");check(d->SetRenderTarget(1,s1.p),"hostile rt1");check(d->SetRenderTarget(2,s2.p),"hostile rt2");check(d->SetDepthStencilSurface(z.p),"hostile depth");
        for(UINT i=0;i<16;++i)check(d->SetTexture(i,spare.p),"hostile texture");
        for(UINT i=0;i<4;++i)check(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,spare.p),"hostile vertex texture");
        check(d->SetVertexShader(vs.p),"hostile vs");check(d->SetPixelShader(ps.p),"hostile ps");check(d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE),"hostile fvf");check(d->SetIndices(ib.p),"hostile index");
        for(UINT i=0;i<caps.MaxStreams;++i){check(d->SetStreamSource(i,vb.p,4,16),"hostile stream");check(d->SetStreamSourceFreq(i,1),"hostile stream freq");}
        if(caps.MaxStreams>1){check(d->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|3),"hostile indexed frequency");check(d->SetStreamSourceFreq(1,D3DSTREAMSOURCE_INSTANCEDATA|1),"hostile instance frequency");}
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPING,D3DRS_LIGHTING,D3DRS_DITHERENABLE})check(d->SetRenderState(state,TRUE),"hostile render");
        check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW),"hostile cull");check(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME),"hostile fill");check(d->SetRenderState(D3DRS_COLORWRITEENABLE,1),"hostile color mask");check(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0),"hostile sample mask");
        for(UINT i=0;i<8;++i)check(d->SetRenderState(D3DRENDERSTATETYPE(D3DRS_WRAP0+i),D3DWRAP_U|D3DWRAP_V),"hostile wrap");
        RECT sc{3,5,13,17};check(d->SetScissorRect(&sc),"hostile scissor");D3DVIEWPORT9 vp{2,3,19,17,.2f,.8f};check(d->SetViewport(&vp),"hostile viewport");
        for(UINT i=0;i<7;++i){
            for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})check(d->SetSamplerState(i,state,D3DTADDRESS_WRAP),"hostile address");
            for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER})check(d->SetSamplerState(i,state,D3DTEXF_LINEAR),"hostile filter");
            float bias=-.75f;DWORD bits=0;std::memcpy(&bits,&bias,4);check(d->SetSamplerState(i,D3DSAMP_MIPMAPLODBIAS,bits),"hostile LOD");check(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,TRUE),"hostile sRGB");check(d->SetSamplerState(i,D3DSAMP_MAXMIPLEVEL,2),"hostile maxmip");check(d->SetSamplerState(i,D3DSAMP_MAXANISOTROPY,1),"hostile anisotropy");
        }
        float constants[88];for(unsigned i=0;i<88;++i)constants[i]=float(i)*.25f-2;
        check(d->SetPixelShaderConstantF(0,constants,22),"hostile ps constants");check(d->SetVertexShaderConstantF(0,constants,8),"hostile vs constants");
    }
    void unbind(IDirect3DSurface9* backbuffer){
        for(UINT i=0;i<16;++i)check(d->SetTexture(i,nullptr),"unbind texture");
        for(UINT i=0;i<4;++i)check(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+i,nullptr),"unbind vertex texture");
        check(d->SetDepthStencilSurface(nullptr),"unbind depth");for(UINT i=1;i<caps.NumSimultaneousRTs;++i)check(d->SetRenderTarget(i,nullptr),"unbind MRT");check(d->SetRenderTarget(0,backbuffer),"unbind rt0");
        check(d->SetIndices(nullptr),"unbind index");for(UINT i=0;i<caps.MaxStreams;++i){check(d->SetStreamSource(i,nullptr,0,0),"unbind stream");check(d->SetStreamSourceFreq(i,1),"unbind stream freq");}
        check(d->SetPixelShader(nullptr),"unbind ps");check(d->SetVertexShader(nullptr),"unbind vs");check(d->SetVertexDeclaration(nullptr),"unbind declaration");
    }
};
// COM reference-count return values are used only as fixture diagnostics,
// never as a runtime capability or ownership decision.
std::vector<ULONG> caller_references(Device d,const Scene& s){
    std::vector<ULONG> values;
    for(IUnknown* object:{static_cast<IUnknown*>(d),static_cast<IUnknown*>(s.rt0.p),static_cast<IUnknown*>(s.rt1.p),static_cast<IUnknown*>(s.rt2.p),static_cast<IUnknown*>(s.s0.p),static_cast<IUnknown*>(s.s1.p),static_cast<IUnknown*>(s.s2.p),static_cast<IUnknown*>(s.z.p),static_cast<IUnknown*>(s.spare.p),static_cast<IUnknown*>(s.vb.p),static_cast<IUnknown*>(s.ib.p),static_cast<IUnknown*>(s.vs.p),static_cast<IUnknown*>(s.ps.p)}){object->AddRef();values.push_back(object->Release());}
    return values;
}
struct Protected {
    Snapshot state;std::vector<unsigned char> rt1,rt2,depth;
    Protected(Device d,const Scene& s):state(d,s.caps),rt1(surface_bytes(d,s.s1.p,8)),rt2(surface_bytes(d,s.s2.p,16)),depth(surface_bytes(d,s.z.p,2,true)){}
    static bool compare_bytes(const char* name,const std::vector<unsigned char>& before,const std::vector<unsigned char>& after){
        if(before==after)return true;
        size_t first=std::min(before.size(),after.size()),changed=0;
        for(size_t i=0;i<std::min(before.size(),after.size());++i)if(before[i]!=after[i]){first=std::min(first,i);++changed;}
        std::printf("SURFACE_DIFF name=%s bytes_before=%u bytes_after=%u changed_bytes=%u first_offset=%u before=%02x after=%02x\n",name,unsigned(before.size()),unsigned(after.size()),unsigned(changed),unsigned(first),first<before.size()?before[first]:0,first<after.size()?after[first]:0);
        return false;
    }
    bool aux_same(Device d,const Scene& s)const{
        // Evaluate every surface so a first failure cannot hide another witness.
        const bool a=compare_bytes("rt1",rt1,surface_bytes(d,s.s1.p,8));
        const bool b=compare_bytes("rt2",rt2,surface_bytes(d,s.s2.p,16));
        const bool c=compare_bytes("depth",depth,surface_bytes(d,s.z.p,2,true));return a&&b&&c;
    }
    bool same(Device d,const Scene& s)const{
        const Snapshot after(d,s.caps);const bool state_equal=state==after;
        if(!state_equal)state.differences(after);
        const bool aux_equal=aux_same(d,s);
        if(!state_equal||!aux_equal)std::printf("PROTECTED_DIFF state_identical=%u auxiliary_bytes_identical=%u\n",unsigned(state_equal),unsigned(aux_equal));
        return state_equal&&aux_equal;
    }
};
} } // anonymous / fog_spatial_state
