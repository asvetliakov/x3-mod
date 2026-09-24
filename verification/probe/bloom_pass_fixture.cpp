// Standalone documented-D3D9 test of the real BloomPass. No proxy/game code.
// Runtime compilation belongs only to this fixture; authored bytecodes are
// retained and passed through the same BloomPrograms API used by production.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/bloom_pass.h"
#include "../../src/renderer/quad_vertex_program.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::renderer;
namespace {
float hostile_npatch=0.f;
DWORD hostile_adaptive=FALSE;
using NativeDraw=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
NativeDraw original_draw=nullptr;
unsigned npatch_draw_checks=0;
using NativePS=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DPixelShader9*);
NativePS original_set_ps=nullptr;bool fail_next_pixel_shader=false;unsigned failed_pixel_setters=0;
HRESULT WINAPI checked_set_ps(IDirect3DDevice9* d,IDirect3DPixelShader9* shader) {
    if(fail_next_pixel_shader) {fail_next_pixel_shader=false;++failed_pixel_setters;return E_FAIL;}
    return original_set_ps(d,shader);
}
HRESULT WINAPI checked_draw(IDirect3DDevice9* d,D3DPRIMITIVETYPE type,UINT count,const void* vertices,UINT stride) {
    // A real getter at the exact injected draw proves setup disabled inherited
    // tessellation even on devices where an incorrect quad happens to render.
    DWORD adaptive=TRUE;
    if(d->GetNPatchMode()!=0.f||FAILED(d->GetRenderState(D3DRS_ENABLEADAPTIVETESSELLATION,&adaptive))
        ||adaptive) return D3DERR_INVALIDCALL;
    ++npatch_draw_checks;return original_draw(d,type,count,vertices,stride);
}
template<class T> struct Com {
    T* p = nullptr;
    Com() = default; Com(const Com&) = delete; Com& operator=(const Com&) = delete;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};
void require(bool v, const char* label) { if (!v) throw std::runtime_error(label); }
void check(HRESULT h, const char* label) {
    if (FAILED(h)) { std::printf("API_FAIL name=%s hr=%08lx\n", label, (unsigned long)h); throw std::runtime_error(label); }
}
void bytes(const std::string& path, const void* p, size_t n) {
    std::ofstream out(path, std::ios::binary); require(bool(out.write((const char*)p, n)), "write bytes");
}
template<class T> T read(std::ifstream& f) { T v{}; require(bool(f.read((char*)&v, sizeof(v))), "truncated input"); return v; }
// Index of the case repeated after the native Reset; see main().
constexpr unsigned kResetCase = 35;
struct Case {
    unsigned w, h, mode, levels;
    float strength, sharp, threshold, exposure, authored_glow_gain, highlight_gain, scatter;
    // Zero is the unbounded feed; any positive value is the decoded-space
    // bloom source ceiling of docs/architecture/bloom-falloff.md.
    float source_clamp;
    float dither;   // c8.z of the write-back (X3M_HDR_DITHER): 0 or 1/255
    std::vector<unsigned short> pixels;
};
std::vector<Case> cases(const std::string& directory) {
    std::ifstream f(directory + "/cases.bin", std::ios::binary);
    char magic[8]; require(bool(f.read(magic, 8)) && !std::memcmp(magic, "X3BP0004", 8), "input magic");
    unsigned n = read<unsigned>(f); require(n == 47, "input count");
    std::vector<Case> out;
    for (unsigned i = 0; i < n; ++i) {
        Case c{}; c.w=read<unsigned>(f); c.h=read<unsigned>(f); c.mode=read<unsigned>(f);
        c.levels=read<unsigned>(f);
        c.strength=read<float>(f); c.sharp=read<float>(f); c.threshold=read<float>(f);
        c.exposure=read<float>(f); c.authored_glow_gain=read<float>(f); c.highlight_gain=read<float>(f);
        c.scatter=read<float>(f); c.source_clamp=read<float>(f); c.dither=read<float>(f);
        require(c.dither==0.f||c.dither==x3::temporal::kDisplayDitherAmplitude, "input dither");
        require(c.w >= 4 && c.w <= 64 && c.h >= 4 && c.h <= 64 && c.mode < 3, "input bounds");
        x3::temporal::BloomParams params{};params.strength=c.strength;params.threshold=c.threshold;
        params.levels=c.levels;params.scatter=c.scatter;
        params.authored_glow_gain=c.authored_glow_gain;params.highlight_gain=c.highlight_gain;
        if(c.source_clamp>0.f)params.source_clamp=c.source_clamp;
        require(x3::temporal::valid_bloom_params(params)&&c.exposure>0&&c.exposure<=65504.f,
                "input parameters");
        c.pixels.resize(c.w*c.h*4);
        require(bool(f.read((char*)c.pixels.data(), c.pixels.size()*2)), "input pixels"); out.push_back(std::move(c));
    }
    require(f.peek() == std::char_traits<char>::eof(), "trailing input"); return out;
}
struct Texture {
    Com<IDirect3DTexture9> texture; Com<IDirect3DSurface9> surface;
    Texture(IDirect3DDevice9* d, unsigned w, unsigned h, D3DFORMAT format, bool rt, bool gpu_source=false) {
        check(d->CreateTexture(w,h,1,rt?D3DUSAGE_RENDERTARGET:0,format,
              (rt||gpu_source)?D3DPOOL_DEFAULT:D3DPOOL_MANAGED,&texture.p,nullptr), "CreateTexture");
        check(texture->GetSurfaceLevel(0,&surface.p), "GetSurfaceLevel");
    }
    void upload(const Case& c) {
        D3DSURFACE_DESC desc{};check(surface->GetDesc(&desc),"Upload desc");
        Com<IDirect3DDevice9> device;Com<IDirect3DTexture9> staging;
        IDirect3DTexture9* target=texture.p;
        if(desc.Pool==D3DPOOL_DEFAULT) {
            check(texture->GetDevice(&device.p),"Upload device");
            check(device->CreateTexture(c.w,c.h,1,0,desc.Format,D3DPOOL_SYSTEMMEM,&staging.p,nullptr),"Upload staging");
            target=staging.p;
        }
        D3DLOCKED_RECT l{}; check(target->LockRect(0,&l,nullptr,0),"Lock input");
        for(unsigned y=0;y<c.h;++y) std::memcpy((char*)l.pBits+y*l.Pitch,c.pixels.data()+y*c.w*4,c.w*8);
        check(target->UnlockRect(0),"Unlock input");
        if(staging.p) check(device->UpdateTexture(staging.p,texture.p),"Update scene");
    }
};
std::vector<DWORD> image(IDirect3DDevice9* d, IDirect3DSurface9* s) {
    D3DSURFACE_DESC desc{}; check(s->GetDesc(&desc),"Image desc");
    Com<IDirect3DSurface9> staging;
    check(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging.p,nullptr),"Readback surface");
    check(d->GetRenderTargetData(s,staging.p),"GetRenderTargetData");
    D3DLOCKED_RECT l{}; check(staging->LockRect(&l,nullptr,D3DLOCK_READONLY),"Readback lock");
    std::vector<DWORD> out(desc.Width*desc.Height);
    for(unsigned y=0;y<desc.Height;++y) std::memcpy(out.data()+y*desc.Width,(char*)l.pBits+y*l.Pitch,desc.Width*4);
    check(staging->UnlockRect(),"Readback unlock"); return out;
}
const char* names[]={"quad_vs","bloom_extract_gamma_ps","bloom_extract_srgb_ps","bloom_extract_none_ps",
    "bloom_extract_even_gamma_ps","bloom_extract_even_srgb_ps","bloom_extract_even_none_ps",
    "bloom_down_ps","bloom_up_ps","bloom_agx_ps","taa_sharpen_ps","hdr_writeback_ps"};
struct Programs {
    Com<ID3DXBuffer> code[12]; BloomPrograms bundle{};
    Programs(const char* compiler_path,const std::string& dir) {
        HMODULE m=LoadLibraryA(compiler_path); require(m,"Load compiler");
        FARPROC symbol=GetProcAddress(m,"D3DXCompileShader"); decltype(&D3DXCompileShader) compile=nullptr;
        static_assert(sizeof(symbol)==sizeof(compile)); std::memcpy(&compile,&symbol,sizeof(symbol)); require(compile,"Compile symbol");
        BloomBytecode* slots[]={&bundle.vertex,&bundle.extract[0],&bundle.extract[1],&bundle.extract[2],
            &bundle.extract[3],&bundle.extract[4],&bundle.extract[5],&bundle.down,&bundle.up,&bundle.candidate,&bundle.sharpen,&bundle.copy};
        for(unsigned i=0;i<12;++i) {
            std::ifstream f(dir+"/"+names[i]+".hlsl"); std::string source{std::istreambuf_iterator<char>(f),{}};
            require(!source.empty(),"Shader source"); Com<ID3DXBuffer> errors;
            HRESULT hr=compile(source.data(),source.size(),nullptr,nullptr,"main",i?"ps_3_0":"vs_3_0",
                D3DXSHADER_OPTIMIZATION_LEVEL3,&code[i].p,&errors.p,nullptr);
            if(errors.p) std::printf("COMPILER %s\n",(char*)errors->GetBufferPointer());
            check(hr,"Compile");
            *slots[i]={static_cast<DWORD*>(code[i]->GetBufferPointer()),code[i]->GetBufferSize()/4};
            bytes(dir+"/"+names[i]+".cso",code[i]->GetBufferPointer(),code[i]->GetBufferSize());
        }
        // Keep the compiler loaded until process exit: its COM buffer release
        // methods must remain mapped. It is fixture-only, never production.
    }
};
// Independent wider state census: include untouched sentinels as well as the
// pass's changed set, and retain references until comparison has completed.
const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,
 D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_CULLMODE,D3DRS_FILLMODE,
 D3DRS_COLORWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,
 D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_DITHERENABLE,D3DRS_WRAP0,
 D3DRS_MULTISAMPLEMASK,D3DRS_CLIPPING,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_ALPHAREF,
 D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3,D3DRS_ENABLEADAPTIVETESSELLATION};
const D3DSAMPLERSTATETYPE samplers[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,
 D3DSAMP_BORDERCOLOR,D3DSAMP_MAGFILTER,D3DSAMP_MINFILTER,D3DSAMP_MIPFILTER,
 D3DSAMP_MIPMAPLODBIAS,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MAXANISOTROPY,D3DSAMP_SRGBTEXTURE};
struct State {
    Com<IDirect3DSurface9> rt[4],depth; Com<IDirect3DBaseTexture9> tex[3];
    Com<IDirect3DVertexShader9> vs; Com<IDirect3DPixelShader9> ps;
    Com<IDirect3DVertexDeclaration9> decl; Com<IDirect3DVertexBuffer9> stream;
    std::vector<DWORD> values;
    State(IDirect3DDevice9* d,unsigned mrt) {
        auto append=[&](const void* p,size_t n){ const size_t offset=values.size();values.resize(offset+n/4);std::memcpy(values.data()+offset,p,n); };
        for(unsigned i=0;i<mrt;++i) { HRESULT h=d->GetRenderTarget(i,&rt[i].p); require(SUCCEEDED(h)||h==D3DERR_NOTFOUND,"Get RT"); }
        HRESULT dh=d->GetDepthStencilSurface(&depth.p); require(SUCCEEDED(dh)||dh==D3DERR_NOTFOUND,"Get depth");
        D3DVIEWPORT9 vp{}; RECT sc{}; check(d->GetViewport(&vp),"Get VP"); check(d->GetScissorRect(&sc),"Get scissor"); append(&vp,sizeof(vp)); append(&sc,sizeof(sc));
        for(auto s:states) { DWORD v=0;check(d->GetRenderState(s,&v),"Get RS");values.push_back(v); }
        for(unsigned i=0;i<3;++i) { check(d->GetTexture(i,&tex[i].p),"Get texture"); for(auto s:samplers) { DWORD v=0;check(d->GetSamplerState(i,s,&v),"Get sampler");values.push_back(v); } }
        float c[32*4]{}; check(d->GetPixelShaderConstantF(0,c,32),"Get constants");append(c,sizeof(c));
        check(d->GetVertexShader(&vs.p),"Get VS");check(d->GetPixelShader(&ps.p),"Get PS");check(d->GetVertexDeclaration(&decl.p),"Get declaration");
        UINT off=0,stride=0,freq=0; DWORD fvf=0;check(d->GetStreamSource(0,&stream.p,&off,&stride),"Get stream");check(d->GetStreamSourceFreq(0,&freq),"Get freq");check(d->GetFVF(&fvf),"Get FVF");
        values.insert(values.end(),{off,stride,freq,fvf,DWORD(d->GetSoftwareVertexProcessing())});
        const float npatch=d->GetNPatchMode();append(&npatch,sizeof(npatch));
    }
    bool same(const State& b) const {
        for(unsigned i=0;i<4;++i) if(rt[i].p!=b.rt[i].p) return false;
        for(unsigned i=0;i<3;++i) if(tex[i].p!=b.tex[i].p) return false;
        return values==b.values && depth.p==b.depth.p && vs.p==b.vs.p && ps.p==b.ps.p && decl.p==b.decl.p && stream.p==b.stream.p;
    }
};
struct HostBindings {
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> ps;
    Com<IDirect3DVertexDeclaration9> declaration;
    HostBindings(IDirect3DDevice9* d,const BloomPrograms& p) {
        check(d->CreateVertexShader(p.vertex.words,&vs.p),"Host VS");
        check(d->CreatePixelShader(p.copy.words,&ps.p),"Host PS");
        check(d->CreateVertexDeclaration(quad_declaration,&declaration.p),"Host declaration");
    }
};
void seed(IDirect3DDevice9* d,Texture& main,Texture& sentinel,IDirect3DTexture9* secondary,IDirect3DSurface9* mrt,
          IDirect3DVertexBuffer9* vb,const HostBindings& host,const Case& c,bool outgoing) {
    check(d->SetRenderTarget(0,main.surface.p),"Seed main");check(d->SetRenderTarget(1,mrt),"Seed MRT");check(d->SetDepthStencilSurface(nullptr),"Seed null depth");
    D3DVIEWPORT9 vp{outgoing?1u:0u,outgoing?1u:0u,outgoing?c.w-2:c.w,outgoing?c.h-2:c.h,.125f,.875f};
    if(!outgoing) {vp.MinZ=0;vp.MaxZ=1;} check(d->SetViewport(&vp),"Seed VP");
    RECT sc{1,1,LONG(c.w-1),LONG(c.h-1)};check(d->SetScissorRect(&sc),"Seed scissor");
    const DWORD hostile[]={TRUE,TRUE,TRUE,TRUE,TRUE,D3DCULL_CW,D3DFILL_WIREFRAME,8,TRUE,TRUE,TRUE,
        TRUE,1,TRUE,D3DWRAP_U,0x55aa55aa,TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,173,5,6,9,hostile_adaptive};
    static_assert(sizeof(hostile)/4==sizeof(states)/sizeof(states[0]));
    for(unsigned i=0;i<sizeof(hostile)/4;++i) check(d->SetRenderState(states[i],hostile[i]),"Seed RS");
    const DWORD sv[]={D3DTADDRESS_WRAP,D3DTADDRESS_MIRROR,D3DTADDRESS_CLAMP,0x10203040,
        D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_POINT,0x3e800000,0,1,TRUE};
    for(unsigned stage=0;stage<3;++stage) {
        check(d->SetTexture(stage,stage==1?secondary:sentinel.texture.p),"Seed texture");
        for(unsigned i=0;i<sizeof(sv)/4;++i) check(d->SetSamplerState(stage,samplers[i],sv[i]),"Seed sampler");
    }
    float constants[32*4];for(unsigned i=0;i<128;++i) constants[i]=float(i+1)*.03125f+(outgoing?2.f:0.f);
    check(d->SetPixelShaderConstantF(0,constants,32),"Seed constants");
    check(d->SetVertexShader(host.vs.p),"Seed VS");check(d->SetPixelShader(host.ps.p),"Seed PS");
    if(outgoing) check(d->SetVertexDeclaration(host.declaration.p),"Seed declaration");
    else check(d->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1),"Seed FVF");
    check(d->SetStreamSource(0,vb,4,20),"Seed stream");check(d->SetStreamSourceFreq(0,1),"Seed frequency");
    check(d->SetSoftwareVertexProcessing(TRUE),"Seed SWVP");
    check(d->SetNPatchMode(hostile_npatch),"Seed NPatch");
}
void assert_state(IDirect3DDevice9* d,const State& before,unsigned mrt,const char* label) { State after(d,mrt);require(before.same(after),label); }
const DWORD original=0x6b193957; // distinct real original RGB and alpha
void fill_report(const std::string& label,DWORD requested,const std::vector<DWORD>& pixels) {
    unsigned mismatches=0,first=0xffffffffu;DWORD observed=requested;
    for(unsigned i=0;i<pixels.size();++i) if(pixels[i]!=requested) {
        if(!mismatches) {first=i;observed=pixels[i];}++mismatches;
    }
    std::printf("FILL label=%s requested=%08lx mismatches=%u first=%u observed=%08lx\n",
        label.c_str(),(unsigned long)requested,mismatches,first,(unsigned long)observed);
}
void exact_image(const std::vector<DWORD>& actual,const std::vector<DWORD>& baseline,const char* label) {
    require(actual.size()==baseline.size(),"Baseline dimensions");
    for(unsigned i=0;i<actual.size();++i) if(actual[i]!=baseline[i]) {
        std::printf("IMAGE_MISMATCH label=%s pixel=%u expected=%08lx actual=%08lx\n",label,i,
                    (unsigned long)baseline[i],(unsigned long)actual[i]);
        require(false,label);
    }
}
void neutral_fill_control(IDirect3DDevice9* d,const std::string& dir) {
    Texture target(d,8,6,D3DFMT_A8R8G8B8,true);
    check(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE),"Neutral sRGB");
    check(d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"Neutral mask");
    check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"Neutral blend");
    check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE),"Neutral scissor");
    check(d->ColorFill(target.surface.p,nullptr,original),"Neutral fill");
    auto pixels=image(d,target.surface.p);bytes(dir+"/neutral_fill.bgra8",pixels.data(),pixels.size()*4);
    fill_report("neutral",original,pixels);
    exact_image(pixels,std::vector<DWORD>(pixels.size(),original),"Neutral ColorFill identity");
}
unsigned run_case(IDirect3DDevice9* d,const D3DCAPS9& caps,void* const* native,const BloomPrograms& programs,
                  const Case& c,unsigned index,const std::string& dir,bool faults,
                  BloomPass& pass,BloomCandidate* reset_token=nullptr,bool post_reset=false) {
    if(!pass.enabled()) check(pass.attach(d,native,caps,programs),"Attach");
    require(pass.enabled(),pass.caps().reason);
    const unsigned base_refs=pass.references();HostBindings host(d,programs);
    Texture main(d,c.w,c.h,D3DFMT_A8R8G8B8,true),scene(d,c.w,c.h,D3DFMT_A16B16G16R16F,false,true),
        sentinel(d,c.w,c.h,D3DFMT_A16B16G16R16F,false),mrt(d,c.w,c.h,D3DFMT_A8R8G8B8,true);
    scene.upload(c);sentinel.upload(c);Com<IDirect3DVertexBuffer9> vb;
    check(d->CreateVertexBuffer(128,0,0,D3DPOOL_MANAGED,&vb.p,nullptr),"Create stream");
    BloomPrepare input{};input.scene=scene.texture.p;input.decode=static_cast<x3::temporal::AgxDecode>(c.mode);
    input.sharpen=c.sharp;input.filter.levels=c.levels;input.filter.threshold=c.threshold;
    input.filter.scatter=c.scatter;
    input.filter.strength=c.strength;input.filter.authored_glow_gain=c.authored_glow_gain;
    input.filter.highlight_gain=c.highlight_gain;
    if(c.source_clamp>0.f)input.filter.source_clamp=c.source_clamp;
    require(x3::temporal::prepare(input.agx,c.exposure,0,input.decode,x3::temporal::AgxLook::none),"AgX constants");
    x3::temporal::set_dither(input.agx,c.dither>0.f);
    auto& boundary=input.boundary;boundary.main=main.surface.p;check(main.surface->GetDesc(&boundary.main_desc),"Main desc");
    boundary.frame=7;boundary.reset=post_reset?3:2;boundary.thread=GetCurrentThreadId();boundary.admitted=true;
    unsigned checks=0;
    const unsigned runs=faults?16:1;
    for(unsigned test=0;test<runs;++test) {
        if(!pass.enabled()) {pass.shutdown();check(pass.attach(d,native,caps,programs),"Reattach");}
        seed(d,main,sentinel,scene.texture.p,mrt.surface.p,vb.p,host,c,false);
        check(d->ColorFill(main.surface.p,nullptr,0x42112233),"Initial fill");
        check(d->ColorFill(mrt.surface.p,nullptr,0x87563412),"MRT fill");
        check(d->BeginScene(),"BeginScene");
        BloomPreparation prep;
        { State before(d,caps.NumSimultaneousRTs);
          if(test==10) pass.set_fault(BloomFault::PrepareDraw);
          if(test==11) pass.set_fault(BloomFault::PrepareRestore);
          if(test==12) {pass.before_reset();pass.set_fault(BloomFault::Allocate);}
          if(test==13) pass.set_fault(BloomFault::Save);
          prep=pass.prepare(input);
          if(test!=11) assert_state(d,before,caps.NumSimultaneousRTs,"Prepare state restore");
        }
        require(pass.transient_views()==0,"Prepare transient leak");
        if(test>=10&&test<=13) {
            require(!prep.ready&&!pass.valid(prep.candidate),"Failure produced ticket");
            if(test==11) require(!prep.state_preserved&&!pass.enabled(),"Prepare restoration classification");
            check(d->EndScene(),"End failed prepare");++checks;std::printf("CONTROL test=%u pass=1\n",test);continue;
        }
        require(prep.ready&&pass.valid(prep.candidate),prep.reason);
        // Simulated original executes once, changes genuine outgoing state and
        // pixels. This fixture does not claim game bridge/CPU/cache coverage.
        seed(d,main,sentinel,scene.texture.p,mrt.surface.p,vb.p,host,c,true);
        check(d->ColorFill(main.surface.p,nullptr,original),"Original fill");
        const auto saved_token=prep.candidate;
        if(test==1) pass.set_fault(BloomFault::Backup);
        if(test>=2&&test<=5) pass.set_fault(BloomFault::CommitDrawAfterWrite);
        if(test==3||test==5) pass.set_fault(BloomFault::CommitRestore);
        if(test==4) pass.set_fault(BloomFault::Recovery);
        if(test==5) pass.set_fault(BloomFault::RecoveryRestore);
        if(test==14) {pass.set_fault(BloomFault::Setup);pass.set_fault(BloomFault::Recovery);}
        const unsigned draws_before=npatch_draw_checks,setters_before=failed_pixel_setters;
        if(test==15) {fail_next_pixel_shader=true;pass.set_fault(BloomFault::Recovery);}
        if(test==6) ++boundary.frame;
        if(test==7) ++boundary.reset;
        if(test==8) pass.before_reset();
        if(test==9) {
            seed(d,main,sentinel,scene.texture.p,mrt.surface.p,vb.p,host,c,false);
            auto other=pass.prepare(input);require(other.ready&&!pass.valid(saved_token),"Reprepare revoke");
            seed(d,main,sentinel,scene.texture.p,mrt.surface.p,vb.p,host,c,true);
        }
        const std::string prefix=(post_reset?"reset_":"")+std::string("c")+std::to_string(index)+"_t"+std::to_string(test);
        // These are the genuine observed original images, captured after all
        // simulated-original and token-control work and before the first post
        // call. Never substitute the requested ColorFill DWORD as the backup
        // oracle. Retain both sides even when a later assertion fails.
        const auto baseline=image(d,main.surface.p),mrt_baseline=image(d,mrt.surface.p);
        bytes(dir+"/"+prefix+"_original.bgra8",baseline.data(),baseline.size()*4);
        bytes(dir+"/"+prefix+"_mrt_original.bgra8",mrt_baseline.data(),mrt_baseline.size()*4);
        fill_report(prefix+"_original",original,baseline);
        fill_report(prefix+"_mrt",0x87563412,mrt_baseline);
        BloomCommit result;std::vector<DWORD> pixels,mrt_pixels;
        { State outgoing(d,caps.NumSimultaneousRTs);result=pass.commit(saved_token,boundary);
          pixels=image(d,main.surface.p);mrt_pixels=image(d,mrt.surface.p);
          bytes(dir+"/"+prefix+"_post.bgra8",pixels.data(),pixels.size()*4);
          bytes(dir+"/"+prefix+"_mrt_post.bgra8",mrt_pixels.data(),mrt_pixels.size()*4);
          if(test!=5) assert_state(d,outgoing,caps.NumSimultaneousRTs,"Outgoing state restore");
          else {State actual(d,caps.NumSimultaneousRTs);require(!outgoing.same(actual),"Partial restoration fault did not alter state");}
        }
        require(!pass.valid(saved_token)&&pass.transient_views()==0,"Commit token/transient leak");
        auto repeated=pass.commit(saved_token,boundary);
        const auto repeated_pixels=image(d,main.surface.p);
        bytes(dir+"/"+prefix+"_repeated.bgra8",repeated_pixels.data(),repeated_pixels.size()*4);
        require(!repeated.committed&&!repeated.write_attempted,"Repeated token wrote");
        exact_image(repeated_pixels,pixels,"Repeated token changed image");
        if(test==15) require(failed_pixel_setters==setters_before+1&&!fail_next_pixel_shader
            &&npatch_draw_checks==draws_before,"Draw setter failure issued a native draw");
        if(test==14||test==15) require(!result.write_attempted&&result.original_preserved&&result.state_preserved
            &&result.recovery==S_FALSE&&pass.enabled(),"Setup-only failure attempted rollback");
        if(test==0) require(result.committed&&result.state_preserved&&result.write_attempted,"Success flags");
        else require(!result.committed,"Fault published committed");
        if(test==2||test==3) require(result.original_preserved&&result.state_preserved&&result.write_attempted,"Rollback flags");
        if(test==3) require(result.requalification_required&&!pass.enabled(),"Restore failure must disable");
        if(test==4) require(!result.original_preserved&&result.state_preserved&&!pass.enabled(),"Recovery failure flags");
        if(test==5) require(result.original_preserved&&!result.state_preserved&&!pass.enabled(),"Recovery state flags");
        if(test==1||test>=6) require(!result.write_attempted&&result.original_preserved,"Rejected boundary wrote");
        check(d->EndScene(),"EndScene");
        for(unsigned i=0;i<pixels.size();++i) require((pixels[i]>>24)==(baseline[i]>>24),"Original alpha changed");
        if(test==0||test==4) require(pixels!=baseline,"Candidate did not write real RGB");
        else exact_image(pixels,baseline,"Original backup not recovered exactly");
        exact_image(mrt_pixels,mrt_baseline,"Secondary MRT was written");
        if(test==0) bytes(dir+(post_reset?"/reset_case.bgra8":"/case_"+std::to_string(index)+".bgra8"),pixels.data(),pixels.size()*4);
        ++checks;if(faults) std::printf("CONTROL test=%u pass=1\n",test);
        boundary.frame=7;boundary.reset=2;
    }
    if(reset_token) {
        seed(d,main,sentinel,scene.texture.p,mrt.surface.p,vb.p,host,c,false);
        check(d->BeginScene(),"Begin pre-Reset candidate");
        auto pending=pass.prepare(input);require(pending.ready&&pass.valid(pending.candidate),"Pre-Reset candidate");
        *reset_token=pending.candidate;check(d->EndScene(),"End pre-Reset candidate");
    }
    pass.before_reset();
    if(reset_token) require(!pass.valid(*reset_token),"Reset failed to revoke live candidate");
    require(pass.resource_bytes()==0&&pass.references()==base_refs&&pass.transient_views()==0,"Reset resource cleanup");
    if(!reset_token) {pass.shutdown();require(pass.references()==0&&pass.transient_views()==0&&!pass.enabled(),"Shutdown reference cleanup");}
    check(d->SetTexture(0,nullptr),"Unbind0");check(d->SetTexture(1,nullptr),"Unbind1");check(d->SetTexture(2,nullptr),"Unbind2");
    check(d->SetRenderTarget(1,nullptr),"Unbind MRT");check(d->SetStreamSource(0,nullptr,0,0),"Unbind stream");
    std::printf("%s index=%u width=%u height=%u checks=%u pass=1\n",post_reset?"RESET_CASE":"CASE",index,c.w,c.h,checks);return checks;
}
}
#ifndef X3M_BLOOM_PASS_FIXTURE_ENTRY
#define X3M_BLOOM_PASS_FIXTURE_ENTRY main
#endif
int X3M_BLOOM_PASS_FIXTURE_ENTRY(int argc,char** argv) {
    try {
        require(argc==3,"usage: fixture compiler.dll directory");const std::string dir=argv[2];auto input=cases(dir);Programs programs(argv[1],dir);
        HMODULE module=LoadLibraryA("d3d9.dll");require(module,"Load D3D9");auto address=GetProcAddress(module,"Direct3DCreate9");
        IDirect3D9*(WINAPI* create)(UINT)=nullptr;std::memcpy(&create,&address,sizeof(create));require(create,"D3D symbol");
        Com<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);require(api.p,"Create D3D9");
        for(const char* name:{"d3d9.dll","wined3d.dll","d3dx9_37.dll"}) {
            HMODULE loaded=GetModuleHandleA(name);if(!loaded) continue;
            char path[32768]{};DWORD length=GetModuleFileNameA(loaded,path,sizeof(path));
            require(length&&length<sizeof(path),"Module path");std::printf("MODULE name=%s path=%s\n",name,path);
        }
        HWND window=CreateWindowA("STATIC","BloomPass fixture",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);require(window,"Window");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferWidth=64;pp.BackBufferHeight=64;
        pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.hDeviceWindow=window;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;check(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_MIXED_VERTEXPROCESSING,&pp,&device.p),"Create mixed device");
        neutral_fill_control(device.p,dir);
        D3DCAPS9 caps{};check(device->GetDeviceCaps(&caps),"Caps");require(caps.NumSimultaneousRTs>=2&&caps.NumSimultaneousRTs<=4,"MRT control requires 2..4");
        void** actual=*reinterpret_cast<void***>(device.p);void* native[119];std::memcpy(native,actual,sizeof(native));
        original_draw=reinterpret_cast<NativeDraw>(native[83]);native[83]=reinterpret_cast<void*>(&checked_draw);
        original_set_ps=reinterpret_cast<NativePS>(native[107]);native[107]=reinterpret_cast<void*>(&checked_set_ps);
        HRESULT npatch=device->SetNPatchMode(2.5f);
        if(SUCCEEDED(npatch)&&device->GetNPatchMode()==2.5f) hostile_npatch=2.5f;
        else check(device->SetNPatchMode(0.f),"Disable unsupported NPatch");
        std::printf("NPATCH accepted=%u hr=%08lx\n",hostile_npatch>1?1u:0u,(unsigned long)npatch);
        HRESULT adaptive=device->SetRenderState(D3DRS_ENABLEADAPTIVETESSELLATION,TRUE);
        DWORD observed_adaptive=FALSE;
        if(SUCCEEDED(adaptive)) check(device->GetRenderState(D3DRS_ENABLEADAPTIVETESSELLATION,&observed_adaptive),"Read adaptive probe");
        if(SUCCEEDED(adaptive)&&observed_adaptive==TRUE) hostile_adaptive=TRUE;
        else check(device->SetRenderState(D3DRS_ENABLEADAPTIVETESSELLATION,FALSE),"Disable unsupported adaptive tessellation");
        std::printf("ADAPTIVE accepted=%u hr=%08lx\n",unsigned(hostile_adaptive),(unsigned long)adaptive);
        unsigned checks=0;BloomPass pass;BloomCandidate reset_token{};
        for(unsigned i=0;i<input.size();++i) checks+=run_case(device.p,caps,native,programs.bundle,input[i],i,dir,i==0,
            pass,i+1==input.size()?&reset_token:nullptr);
        // Remove the final main binding before the real Reset. No fixture state
        // snapshots, candidate pins or BloomPass DEFAULT-pool surfaces survive.
        Com<IDirect3DSurface9> back;check(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p),"Backbuffer");
        check(device->SetRenderTarget(0,back.p),"Restore backbuffer");back.p->Release();back.p=nullptr;
        require(pass.enabled()&&pass.references()>0,"Pass must remain attached over native Reset");
        check(device->Reset(&pp),"Native Reset");
        require(!pass.valid(reset_token),"Native Reset resurrected ticket");
        BloomBoundary rejected{};rejected.admitted=true;rejected.frame=7;rejected.reset=3;rejected.thread=GetCurrentThreadId();
        auto old=pass.commit(reset_token,rejected);require(!old.committed&&!old.write_attempted,"Pre-Reset token wrote after Reset");
        // The post-Reset repeat is pinned to the odd-geometry, generic-extraction
        // authored case (9x7, decode none), not to whatever case is last.
        require(kResetCase<input.size()&&input[kResetCase].w==9&&input[kResetCase].h==7
                &&input[kResetCase].mode==2,"Post-Reset case identity");
        checks+=run_case(device.p,caps,native,programs.bundle,input[kResetCase],kResetCase,dir,false,pass,nullptr,true);
        DestroyWindow(window);
        require(npatch_draw_checks>0,"Missing injected draw NPatch assertions");
        std::printf("NPATCH_DRAWS checked=%u pass=1\n",npatch_draw_checks);
        std::printf("RESULT PASS cases=%u controls=16 checks=%u reset=1 reset_cases=1\n",
                    unsigned(input.size()),checks);return 0;
    } catch(const std::exception& e) {std::printf("RESULT FAIL reason=%s\n",e.what());return 1;}
}
