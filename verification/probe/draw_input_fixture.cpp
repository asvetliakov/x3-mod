// Original shaders and geometry exercise the production reader through real
// ownership wrappers. Fixture-only lookup callbacks never exist in the DLL.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/proxy/draw_input.h"
#include "../../src/proxy/capture_state.h"
#include "../../src/ownership/d3d9_ownership.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace x3m { void log(const char*,...) {} }
namespace {
using namespace x3m;
unsigned checks=0, reads=0, state_checks=0;
void check(bool value,const char* name){++checks;std::printf("CHECK %s %s\n",name,value?"PASS":"FAIL");if(!value)throw std::runtime_error(name);}
void api(HRESULT hr,const char* name){if(FAILED(hr)){std::printf("API FAIL %s hr=%08lx\n",name,hr);throw std::runtime_error(name);}}
template<class T>struct Com{T* p=nullptr;~Com(){reset();}void reset(){if(p)p->Release();p=nullptr;}T* operator->()const{return p;}Com()=default;Com(const Com&)=delete;Com&operator=(const Com&)=delete;};
template<class T>T symbol(HMODULE m,const char* name){auto p=GetProcAddress(m,name);T out;static_assert(sizeof out==sizeof p);std::memcpy(&out,&p,sizeof p);if(!out)throw std::runtime_error(name);return out;}
using Assemble=HRESULT(WINAPI*)(LPCSTR,UINT,const D3DXMACRO*,LPD3DXINCLUDE,DWORD,LPD3DXBUFFER*,LPD3DXBUFFER*);
struct ShaderContract {std::vector<std::uint32_t> words;renderer::RigidPositionProfile profile{};};
std::array<ShaderContract,3> vertices;
std::vector<std::uint32_t> pixel;
renderer::PixelCoverageProfile pixel_profile{};
std::uint64_t hash(const void* data,std::size_t n){auto p=static_cast<const unsigned char*>(data);std::uint64_t h=14695981039346656037ull;while(n--){h^=*p++;h*=1099511628211ull;}return h;}
const renderer::RigidPositionProfile* vertex_lookup(const std::uint32_t* p,std::size_t n){for(auto& v:vertices)if(n==v.words.size()&&!std::memcmp(p,v.words.data(),n*4))return &v.profile;return nullptr;}
const renderer::PixelCoverageProfile* pixel_lookup(const std::uint32_t* p,std::size_t n){return n==pixel.size()&&!std::memcmp(p,pixel.data(),n*4)?&pixel_profile:nullptr;}
std::vector<std::uint32_t> assemble(Assemble fn,const std::string& source){Com<ID3DXBuffer> code,error;auto hr=fn(source.c_str(),UINT(source.size()),nullptr,nullptr,0,&code.p,&error.p);if(error.p)std::printf("ASSEMBLER %s\n",static_cast<char*>(error->GetBufferPointer()));api(hr,"assemble original shader");auto words=static_cast<std::uint32_t*>(code->GetBufferPointer());return {words,words+code->GetBufferSize()/4};}
constexpr D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_CULLMODE,D3DRS_COLORWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_STENCILENABLE,D3DRS_FILLMODE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_CLIPPING,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_POINTSIZE,D3DRS_SRGBWRITEENABLE};
// Collect all state the reader could consume, plus surrounding shader constants,
// scissor and unrelated sampler state. Owned getter references die before Reset.
std::vector<unsigned char> snapshot(IDirect3DDevice9* d){std::vector<unsigned char> out;auto add=[&](const auto& v){auto p=reinterpret_cast<const unsigned char*>(&v);out.insert(out.end(),p,p+sizeof v);};
    D3DCAPS9 caps{};api(d->GetDeviceCaps(&caps),"snapshot caps");
    for(auto rs:states){DWORD value=0;api(d->GetRenderState(rs,&value),"snapshot render state");add(value);}
    D3DVIEWPORT9 vp{};RECT rect{};api(d->GetViewport(&vp),"snapshot viewport");api(d->GetScissorRect(&rect),"snapshot scissor");add(vp);add(rect);
    float f[256*4]{};api(d->GetVertexShaderConstantF(0,f,std::min<DWORD>(caps.MaxVertexShaderConst,256u)),"snapshot constants");for(float value:f)add(value);
    for(UINT i=0;i<caps.MaxStreams;++i){Com<IDirect3DVertexBuffer9> vb;UINT offset=0,stride=0,freq=0;api(d->GetStreamSource(i,&vb.p,&offset,&stride),"snapshot stream");api(d->GetStreamSourceFreq(i,&freq),"snapshot frequency");add(vb.p);add(offset);add(stride);add(freq);}
    Com<IDirect3DIndexBuffer9> ib;api(d->GetIndices(&ib.p),"snapshot IB");add(ib.p);
    Com<IDirect3DVertexDeclaration9> decl;api(d->GetVertexDeclaration(&decl.p),"snapshot declaration");add(decl.p);
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> ps;api(d->GetVertexShader(&vs.p),"snapshot VS");api(d->GetPixelShader(&ps.p),"snapshot PS");add(vs.p);add(ps.p);
    for(UINT i=0;i<caps.NumSimultaneousRTs;++i){Com<IDirect3DSurface9> rt;auto hr=d->GetRenderTarget(i,&rt.p);if(hr!=D3DERR_NOTFOUND)api(hr,"snapshot RT");add(resource_id(rt.p));}Com<IDirect3DSurface9> ds;api(d->GetDepthStencilSurface(&ds.p),"snapshot DS");add(resource_id(ds.p));
    DWORD sampler=0;api(d->GetSamplerState(0,D3DSAMP_ADDRESSU,&sampler),"snapshot sampler");add(sampler);return out;}
ULONG references(IUnknown* p){p->AddRef();return p->Release();}
struct Fixture {
    Com<IDirect3D9> factory;Com<IDirect3DDevice9> device;
    std::array<Com<IDirect3DVertexShader9>,3> vs;Com<IDirect3DPixelShader9> ps,unknown_ps;
    Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;
    Com<IDirect3DVertexDeclaration9> float_decl,half_decl,bad_decl;
    D3DPRESENT_PARAMETERS pp{};DrawInputReader reader;object_trace::Snapshot scope{};renderer::SubmittedMatrix rows{};
    explicit Fixture(IDirect3D9* native,HWND window,bool finite=false){ownership::Options options;options.track_buffer_writes=true;options.capture_finite_positions=finite;api(ownership::wrap_factory(native,&factory.p,options),"wrap factory");pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24X8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        api(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device.p),"wrapped device");
        for(unsigned i=0;i<3;++i){api(device->CreateVertexShader(reinterpret_cast<const DWORD*>(vertices[i].words.data()),&vs[i].p),"create VS");}
        api(device->CreatePixelShader(reinterpret_cast<const DWORD*>(pixel.data()),&ps.p),"create PS");
        auto declaration=[&](BYTE type,IDirect3DVertexDeclaration9** out){D3DVERTEXELEMENT9 elements[]={{0,0,type,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};api(device->CreateVertexDeclaration(elements,out),"create declaration");};declaration(D3DDECLTYPE_FLOAT3,&float_decl.p);declaration(D3DDECLTYPE_FLOAT16_4,&half_decl.p);declaration(D3DDECLTYPE_FLOAT4,&bad_decl.p);
        api(device->CreateVertexBuffer(256,finite?D3DUSAGE_WRITEONLY:D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,finite?D3DPOOL_MANAGED:D3DPOOL_DEFAULT,&vb.p,nullptr),"create dynamic VB");api(device->CreateIndexBuffer(24,finite?D3DUSAGE_WRITEONLY:0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib.p,nullptr),"create IB");
        void* p=nullptr;api(vb->Lock(0,0,&p,finite?0:D3DLOCK_DISCARD),"initialize VB");std::memset(p,0,256);const float triangle[]={-.5f,-.5f,.5f, .5f,-.5f,.5f, 0,.5f,.5f};std::memcpy(p,triangle,sizeof triangle);api(vb->Unlock(),"close VB");api(ib->Lock(0,0,&p,0),"initialize IB");const unsigned short indices[12]={0,1,2};std::memcpy(p,indices,sizeof indices);api(ib->Unlock(),"close IB");
        scope.valid=object_trace::Node|object_trace::Camera|object_trace::Registry;scope.scope_depth=1;scope.node=0x1000;scope.camera=0x2000;scope.mesh=0x3000;scope.registry=0x4000;scope.node_handle=7;scope.camera_handle=9;scope.model=4;scope.lod=2;scope.session=123;
        rows={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};reader.fixture_profiles(vertex_lookup,pixel_lookup);baseline();
    }
    void baseline(){api(device->SetVertexShader(vs[0].p),"bind VS");api(device->SetPixelShader(ps.p),"bind PS");api(device->SetVertexDeclaration(float_decl.p),"bind declaration");api(device->SetStreamSource(0,vb.p,0,12),"bind stream");api(device->SetIndices(ib.p),"bind IB");
        for(unsigned i=0;i<3;++i)api(device->SetVertexShaderConstantF(vertices[i].profile.matrix_register,rows.data(),4),"set rows");
        const DWORD values[]={1,1,D3DCMP_LESSEQUAL,D3DCULL_NONE,15,0,0,0,D3DFILL_SOLID,0,0,1,0,0,0x3f800000,0};for(unsigned i=0;i<std::size(states);++i)api(device->SetRenderState(states[i],values[i]),"baseline state");D3DVIEWPORT9 vp{0,0,32,32,0,1};api(device->SetViewport(&vp),"baseline viewport");}
    DrawInput read(DrawArguments args={DrawMethod::Primitive,D3DPT_TRIANGLELIST,1,0,0,0,0},const object_trace::Snapshot* s=nullptr,bool missing_scope=false){auto before=snapshot(device.p);auto dref=references(device.p),vref=references(vb.p),iref=references(ib.p);auto result=reader.read(device.p,args,missing_scope?nullptr:s?s:&scope);++reads;check(snapshot(device.p)==before,"reader preserves queried caller state");++state_checks;check(references(device.p)==dref&&references(vb.p)==vref&&references(ib.p)==iref,"reader releases getter references");return result;}
};
// Original fault seam, restored before snapshots: one application getter can
// report failure after writing plausible data, so HRESULT cannot be ignored.
struct Fault {
    void** original;void* table[119];IDirect3DDevice9* device;
    enum Kind {Caps,Rows,Render,Stream,Declaration};static inline Kind kind=Caps;
    static inline HRESULT(WINAPI* caps)(IDirect3DDevice9*,D3DCAPS9*)=nullptr;
    static inline HRESULT(WINAPI* rows)(IDirect3DDevice9*,UINT,float*,UINT)=nullptr;
    static inline HRESULT(WINAPI* render)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD*)=nullptr;
    static inline HRESULT(WINAPI* stream)(IDirect3DDevice9*,UINT,IDirect3DVertexBuffer9**,UINT*,UINT*)=nullptr;
    static inline HRESULT(WINAPI* declaration)(IDirect3DDevice9*,IDirect3DVertexDeclaration9**)=nullptr;
    static HRESULT WINAPI fail_caps(IDirect3DDevice9* d,D3DCAPS9* p){caps(d,p);return E_FAIL;}
    static HRESULT WINAPI fail_rows(IDirect3DDevice9* d,UINT i,float* p,UINT n){rows(d,i,p,n);return E_FAIL;}
    static HRESULT WINAPI fail_render(IDirect3DDevice9* d,D3DRENDERSTATETYPE r,DWORD* p){render(d,r,p);return E_FAIL;}
    static HRESULT WINAPI fail_stream(IDirect3DDevice9* d,UINT i,IDirect3DVertexBuffer9** p,UINT* o,UINT* s){stream(d,i,p,o,s);return E_FAIL;}
    static HRESULT WINAPI fail_declaration(IDirect3DDevice9* d,IDirect3DVertexDeclaration9** p){declaration(d,p);return E_FAIL;}
    template<class T>void hook(unsigned slot,T replacement,T& saved){std::memcpy(&saved,&table[slot],sizeof saved);std::memcpy(&table[slot],&replacement,sizeof replacement);}
    Fault(IDirect3DDevice9* d,Kind k):original(*reinterpret_cast<void***>(d)),device(d){std::copy(original,original+119,table);kind=k;switch(k){case Caps:hook(7,&fail_caps,caps);break;case Rows:hook(95,&fail_rows,rows);break;case Render:hook(58,&fail_render,render);break;case Stream:hook(101,&fail_stream,stream);break;case Declaration:hook(88,&fail_declaration,declaration);break;}*reinterpret_cast<void***>(d)=table;}
    ~Fault(){*reinterpret_cast<void***>(device)=original;}
};
// GetDesc may populate plausible bytes and still fail. Exercise the exact
// application resource returned by the ownership getter, not a fake buffer.
template<class Buffer,class Descriptor> struct DescriptorFault {
    using GetDesc=HRESULT(WINAPI*)(Buffer*,Descriptor*);
    static inline GetDesc get_desc=nullptr;
    static inline bool populated=false;
    void** original;void* table[14];Buffer* buffer;
    static HRESULT WINAPI fail(Buffer* b,Descriptor* out){const auto hr=get_desc(b,out);populated=SUCCEEDED(hr)&&out&&out->Size>0;return E_FAIL;}
    explicit DescriptorFault(Buffer* b):original(*reinterpret_cast<void***>(b)),buffer(b){std::copy(original,original+14,table);std::memcpy(&get_desc,&table[13],sizeof get_desc);auto replacement=&fail;std::memcpy(&table[13],&replacement,sizeof replacement);populated=false;*reinterpret_cast<void***>(b)=table;}
    ~DescriptorFault(){*reinterpret_cast<void***>(buffer)=original;}
};
void run(Fixture& f){auto good=f.read();const unsigned intrinsic=renderer::PositionReviewed|renderer::GeometryUnchanged|renderer::CoverageSupported;check(!good.blockers&&good.observation.proofs==intrinsic&&!good.replay_source.qualified()&&!good.vertex_finite_verified,"valid synthetic draw has intrinsic proofs but no archive replay token");check(!good.observation.key.indexed&&!good.observation.key.index_buffer&&!good.observation.key.index_revision&&!good.observation.key.index_format,"nonindexed ignores bound IB");check(good.observation.key.node==f.scope.node&&good.observation.key.model==4&&good.observation.key.lod==2&&!good.observation.key.object_lifetime&&!good.observation.key.camera_lifetime,"scope fields never invent lifetime");
    for(unsigned i=0;i<3;++i){auto rows=f.rows;std::uint32_t bits=0x80000000;std::memcpy(&rows[1],&bits,4);rows[3]=0.123456789f;rows[6]=-7.125f;api(f.device->SetVertexShader(f.vs[i].p),"select row register shader");api(f.device->SetVertexShaderConstantF(vertices[i].profile.matrix_register,rows.data(),4),"submit nontrivial rows");auto d=f.read();check(!d.blockers&&!std::memcmp(rows.data(),d.observation.submitted_wvp.data(),sizeof rows),"c0 c6 c24 submitted rows copied bitwise");}f.baseline();
    const DrawArguments ordinary{DrawMethod::Primitive,D3DPT_TRIANGLELIST,1,0,0,0,0};auto missing=f.read(ordinary,nullptr,true);check(missing.blockers==ObjectScope&&!(missing.observation.proofs&renderer::LifetimeVerified),"missing scope cannot prove lifetime");auto partial=f.scope;partial.valid&=~object_trace::Registry;check(f.read(ordinary,&partial).blockers==ObjectScope,"partial scope rejected");
    api(f.device->BeginScene(),"begin actual draw");auto submitted=f.read();api(f.device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1),"actual successful draw");DrawInputReader::complete(submitted,S_OK);check((submitted.observation.proofs&renderer::SubmissionSucceeded)&&!(submitted.observation.proofs&renderer::LifetimeVerified),"successful submission still lacks lifetime");DrawInputReader::complete(submitted,E_FAIL);check((submitted.blockers&SubmissionFailure)&&!(submitted.observation.proofs&renderer::SubmissionSucceeded),"failed submission revokes success");api(f.device->EndScene(),"end actual draw");
    DrawArguments indexed{DrawMethod::Indexed,D3DPT_TRIANGLELIST,1,0,0,0,3};auto indexed_read=f.read(indexed);check(!indexed_read.blockers&&indexed_read.observation.key.indexed&&indexed_read.observation.key.index_revision==1&&indexed_read.observation.key.index_format==D3DFMT_INDEX16,"indexed range and revision captured");
    void* p=nullptr;api(f.ib->Lock(0,2,&p,0),"pending IB write");check(!f.read().blockers,"nonindexed ignores pending bound IB");check(f.read(indexed).blockers&BufferRevision,"indexed pending IB rejected");api(f.ib->Unlock(),"close IB write");check(f.read(indexed).observation.key.index_revision==2,"indexed changed revision reported");
    api(f.device->BeginScene(),"indexed begin scene");api(f.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1),"actual indexed submission");api(f.device->EndScene(),"indexed end scene");check(true,"actual indexed submission succeeds");
    api(f.device->SetStreamSource(0,f.vb.p,12,12),"nonzero stream offset");auto offset=f.read();check(!offset.blockers&&offset.observation.key.stream_offset==12,"nonzero stream offset preserved");f.baseline();
    Com<IDirect3DIndexBuffer9> index32;api(f.device->CreateIndexBuffer(12,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&index32.p,nullptr),"create INDEX32");api(index32->Lock(0,0,&p,0),"initialize INDEX32");const DWORD wide_indices[]={0,1,2};std::memcpy(p,wide_indices,sizeof wide_indices);api(index32->Unlock(),"close INDEX32");api(f.device->SetIndices(index32.p),"bind INDEX32");auto wide=f.read(indexed);check(!wide.blockers&&wide.observation.key.index_format==D3DFMT_INDEX32,"INDEX32 accepted with exact range");api(f.device->SetIndices(nullptr),"missing index buffer");check(f.read(indexed).blockers&BufferDescription,"indexed missing IB rejected");check(!f.read().blockers,"nonindexed missing IB accepted");f.baseline();
    DrawArguments strip{DrawMethod::Primitive,D3DPT_TRIANGLESTRIP,1,0,0,0,0};check(!f.read(strip).blockers,"triangle strip accepted");
    Com<IDirect3DVertexBuffer9> unwritten;api(f.device->CreateVertexBuffer(256,0,0,D3DPOOL_MANAGED,&unwritten.p,nullptr),"create unwritten VB");api(f.device->SetStreamSource(0,unwritten.p,0,12),"bind unwritten VB");check(f.read().blockers&BufferRevision,"zero write revision rejected");f.baseline();
    api(f.vb->Lock(0,8,&p,D3DLOCK_NOOVERWRITE),"pending VB write");check(f.read().blockers&BufferRevision,"pending VB rejected");api(f.vb->Unlock(),"close VB write");check(f.read().observation.key.vertex_revision==good.observation.key.vertex_revision+1,"VB revision changes after write");
    api(f.vb->Lock(0,0,&p,D3DLOCK_DISCARD),"write half geometry");const unsigned short half[12]={0xb800,0xb800,0x3800,0x4700,0x3800,0xb800,0x3800,0x4700,0,0x3800,0x3800,0x4700};std::memcpy(p,half,sizeof half);api(f.vb->Unlock(),"close half geometry");api(f.device->SetVertexDeclaration(f.half_decl.p),"half declaration");api(f.device->SetStreamSource(0,f.vb.p,0,8),"half stride");auto h=f.read();check(!h.blockers&&h.observation.key.position_type==D3DDECLTYPE_FLOAT16_4&&h.observation.key.stride==8,"native half4 input accepted");api(f.device->BeginScene(),"half begin scene");api(f.device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1),"native half4 draw");api(f.device->EndScene(),"half end scene");f.baseline();
    api(f.device->SetVertexDeclaration(f.bad_decl.p),"unsupported declaration");check(f.read().blockers&PositionLayout,"float4 layout rejected");f.baseline();
    for(auto method:{DrawMethod::UserMemory,DrawMethod::IndexedUserMemory,static_cast<DrawMethod>(99)}){auto a=indexed;a.method=method;auto d=f.read(a);check((d.blockers&UserMemory)&&!(d.observation.proofs&(renderer::GeometryUnchanged|renderer::CoverageSupported)),"UP lacks buffer and coverage proof");}
    std::array<DrawArguments,6> bad_ranges{{{DrawMethod::Primitive,D3DPT_TRIANGLELIST,0,0,0,0,0},{DrawMethod::Primitive,D3DPT_TRIANGLELIST,0xffffffffu,0xffffffffu,0,0,0},{DrawMethod::Indexed,D3DPT_TRIANGLELIST,1,0,-1,0,3},{DrawMethod::Indexed,D3DPT_TRIANGLELIST,1,0,0,0,0},{DrawMethod::Indexed,D3DPT_TRIANGLELIST,1,0xffffffffu,0,0,3},{DrawMethod::Indexed,D3DPT_TRIANGLELIST,1,0,INT_MAX,0xffffffffu,0xffffffffu}}};for(auto a:bad_ranges)check(f.read(a).blockers&DrawRange,"invalid or overflowing range rejected");
    const std::pair<D3DRENDERSTATETYPE,DWORD> bad_states[]={{D3DRS_ALPHATESTENABLE,1},{D3DRS_ALPHABLENDENABLE,1},{D3DRS_STENCILENABLE,1},{D3DRS_SCISSORTESTENABLE,1},{D3DRS_DEPTHBIAS,0x3a83126f},{D3DRS_SLOPESCALEDEPTHBIAS,0x3f800000},{D3DRS_ZWRITEENABLE,0},{D3DRS_ZFUNC,D3DCMP_ALWAYS},{D3DRS_COLORWRITEENABLE,8},{D3DRS_FILLMODE,D3DFILL_WIREFRAME}};for(auto state:bad_states){api(f.device->SetRenderState(state.first,state.second),"hostile raster state");auto d=f.read();check((d.blockers&RasterState)&&!(d.observation.proofs&renderer::CoverageSupported),"unsupported raster coverage rejected");f.baseline();}
    D3DVIEWPORT9 vp{1,0,31,32,0,1};api(f.device->SetViewport(&vp),"partial viewport");check(f.read().blockers&TargetLayout,"partial viewport rejected");f.baseline();
    api(f.device->SetPixelShader(nullptr),"unknown pixel shader");check(f.read().blockers&PixelCoverage,"unreviewed pixel shader rejected");f.baseline();api(f.device->SetVertexShader(nullptr),"unknown vertex shader");auto unknown=f.read();check((unknown.blockers&PositionProgram)&&!(unknown.observation.proofs&renderer::PositionReviewed),"unknown position program rejected");f.baseline();
    for(std::uint32_t bits:{0x7f800000u,0xff800000u,0x7fc12345u,0x7f812345u,0x5f000000u}){auto nonfinite=f.rows;std::memcpy(&nonfinite[0],&bits,4);api(f.device->SetVertexShaderConstantF(0,nonfinite.data(),4),"nonfinite or excessive rows");auto rejected=f.read();check((rejected.blockers&SubmittedRows)&&!std::memcmp(rejected.observation.submitted_wvp.data(),nonfinite.data(),sizeof nonfinite),"invalid submitted rows rejected without bit normalization");}f.baseline();
    for(auto kind:{Fault::Caps,Fault::Rows,Fault::Render,Fault::Stream,Fault::Declaration}){auto before=snapshot(f.device.p);auto refs=references(f.device.p);DrawInput d;{Fault fault(f.device.p,kind);d=f.reader.read(f.device.p,{DrawMethod::Primitive,D3DPT_TRIANGLELIST,1,0,0,0,0},&f.scope);}check(d.blockers!=0&&!(d.observation.proofs&renderer::CoverageSupported),"failed getter cannot claim coverage");check(snapshot(f.device.p)==before&&references(f.device.p)==refs,"failed getter preserves state and releases outputs");}
    {auto before=snapshot(f.device.p);auto refs=references(f.vb.p);DrawInput d;using Failure=DescriptorFault<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>;{Failure fault(f.vb.p);d=f.reader.read(f.device.p,ordinary,&f.scope);check(Failure::populated,"failed VB GetDesc supplies plausible descriptor");}check((d.blockers&BufferDescription)&&!(d.observation.proofs&(renderer::GeometryUnchanged|renderer::CoverageSupported)),"failed VB description refuses geometry and coverage");check(snapshot(f.device.p)==before&&references(f.vb.p)==refs,"failed VB description preserves state and releases references");}
    {auto before=snapshot(f.device.p);auto refs=references(f.ib.p);DrawInput d;using Failure=DescriptorFault<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>;{Failure fault(f.ib.p);d=f.reader.read(f.device.p,indexed,&f.scope);check(Failure::populated,"failed IB GetDesc supplies plausible descriptor");}check((d.blockers&BufferDescription)&&!(d.observation.proofs&(renderer::GeometryUnchanged|renderer::CoverageSupported)),"failed IB description refuses geometry and coverage");check(snapshot(f.device.p)==before&&references(f.ib.p)==refs,"failed IB description preserves state and releases references");}
    auto null=f.reader.read(nullptr,{},nullptr);check(null.blockers==QueryFailure&&!null.observation.proofs,"null device explicit failure");
    api(f.device->SetStreamSource(0,nullptr,0,0),"unbind before Reset");f.vb.reset();api(f.device->Reset(&f.pp),"Reset after all reader calls");check(true,"Reset succeeds without retained DEFAULT references");
}
// No geometry readback or extra Lock at query time. The observer learns only
// from these ordinary application uploads to MANAGED+WRITEONLY resources.
void run_finite(Fixture& f){
    const DrawArguments ordinary{DrawMethod::Primitive,D3DPT_TRIANGLELIST,1,0,0,0,0};
    const DrawArguments indexed{DrawMethod::Indexed,D3DPT_TRIANGLELIST,1,0,0,0,3};
    auto expect=[&](bool finite,const char* label,const DrawArguments& args){
        auto input=f.read(args);
        const bool position_identity=!finite||(input.finite_positions.state==ownership::FiniteStatus::Finite&&
            input.finite_positions.requested&&input.finite_positions.generation&&
            input.finite_positions.revision==input.observation.key.vertex_revision);
        const bool indexed_identity=args.method!=DrawMethod::Indexed||!finite||
            (input.index_range_verified&&input.indices.known&&input.indices.requested&&
             input.indices.revision==input.observation.key.index_revision);
        check(input.vertex_finite_verified==finite&&position_identity&&indexed_identity,label);
        return input;
    };
    auto upload_vertices=[&](const void* data,std::size_t bytes){
        void* mapped=nullptr;api(f.vb->Lock(0,0,&mapped,0),"managed full VB upload");
        std::memset(mapped,0,256);std::memcpy(mapped,data,bytes);
        api(f.vb->Unlock(),"managed full VB publication");
    };
    auto upload_indices=[&](unsigned short maximum){
        const unsigned short indices[12]={0,1,maximum};void* mapped=nullptr;
        api(f.ib->Lock(0,0,&mapped,0),"managed full IB upload");
        std::memcpy(mapped,indices,sizeof indices);api(f.ib->Unlock(),"managed full IB publication");
    };
    auto positive=expect(true,"managed FLOAT3 ordinary upload proves finite XYZ",ordinary);
    check(!positive.replay_source.qualified(),"finite payload cannot upgrade synthetic shader into archive replay source");
    const auto original_revision=positive.observation.key.vertex_revision;
    const unsigned short half[12]={0xb800,0xb800,0x3800,0x7e01,0x3800,0xb800,0x3800,0x7c01,0,0x3800,0x3800,0xfc00};
    upload_vertices(half,sizeof half);api(f.device->SetVertexDeclaration(f.half_decl.p),"finite half declaration");api(f.device->SetStreamSource(0,f.vb.p,0,8),"finite half stride");
    expect(true,"half XYZ finite despite NaN and infinity stored W",ordinary);
    auto bad_half=std::array<unsigned short,12>{};std::copy(std::begin(half),std::end(half),bad_half.begin());bad_half[4]=0x7c01;
    upload_vertices(bad_half.data(),sizeof bad_half);auto rejected_half=expect(false,"half nonfinite XYZ refuses finite attestation",ordinary);
    check(rejected_half.finite_positions.state==ownership::FiniteStatus::NonFinite,"half XYZ rejection is positive nonfinite evidence");
    f.baseline();const std::uint32_t bad_float[9]={0x7f812345,0,0,0,0,0,0,0,0};
    upload_vertices(bad_float,sizeof bad_float);auto rejected_float=expect(false,"FLOAT3 signaling NaN refuses finite attestation",ordinary);
    check(rejected_float.finite_positions.state==ownership::FiniteStatus::NonFinite,"FLOAT3 rejection is positive nonfinite evidence");
    const float triangle[9]={-.5f,-.5f,.5f,.5f,-.5f,.5f,0,.5f,.5f};
    upload_vertices(triangle,sizeof triangle);positive=expect(true,"full finite replacement restores ordinary attestation",ordinary);
    ownership::FinitePositionRequest stale{};stale.expected_revision=original_revision;stale.stride=12;stale.vertex_count=3;stale.position_type=D3DDECLTYPE_FLOAT3;
    ownership::FinitePositionView stale_view{};api(ownership::get_finite_position_view(f.vb.p,stale,&stale_view),"stale finite position query");
    check(stale_view.state==ownership::FiniteStatus::Unknown&&stale_view.reason==ownership::FiniteEvidenceReason::RevisionMismatch,"old VB revision cannot reuse newer evidence");
    auto valid_indexed=expect(true,"indexed finite proof requires actual index certificate",indexed);
    const auto original_index_revision=valid_indexed.observation.key.index_revision;
    upload_indices(5);auto outside=expect(false,"actual indices outside declared API min count refuse finite proof",indexed);
    check(outside.indices.known&&outside.indices.minimum==0&&outside.indices.maximum==5&&!outside.index_range_verified,
          "known actual IB extrema fail the narrower declared interval");
    expect(true,"nonindexed finite proof ignores bound bad IB",ordinary);
    void* mapped=nullptr;api(f.ib->Lock(0,2,&mapped,0),"pending managed IB");
    expect(false,"pending IB cannot certify indexed finite positions",indexed);
    api(f.ib->Unlock(),"partial managed IB publication");
    expect(false,"partial IB update leaves whole allocation certificate unknown",indexed);
    upload_indices(2);expect(true,"full IB upload restores indexed finite proof",indexed);
    ownership::IndexRangeRequest old_indices{};old_indices.expected_revision=original_index_revision;old_indices.format=D3DFMT_INDEX16;old_indices.index_count=3;
    ownership::IndexRangeView stale_indices{};api(ownership::get_index_range_view(f.ib.p,old_indices,&stale_indices),"stale index query");
    check(!stale_indices.known&&stale_indices.reason==ownership::FiniteEvidenceReason::RevisionMismatch,"old IB revision cannot reuse newer certificate");
    api(f.vb->Lock(0,12,&mapped,0),"pending managed VB");expect(false,"pending VB cannot certify finite positions",ordinary);
    std::memcpy(mapped,triangle,12);api(f.vb->Unlock(),"partial managed VB publication");
    expect(true,"aligned finite partial VB upload preserves unaffected finite cells",ordinary);
    auto* native_vb=ownership::borrowed_native_buffer_for_lock_contract(f.vb.p);
    auto* native_ib=ownership::borrowed_native_buffer_for_lock_contract(f.ib.p);
    ownership::FinitePositionView native_view{};ownership::IndexRangeView native_indices{};
    auto current=stale;current.expected_revision=f.read().observation.key.vertex_revision;
    check(native_vb&&FAILED(ownership::get_finite_position_view(native_vb,current,&native_view))&&native_view.state==ownership::FiniteStatus::Unknown,"native VB pointer cannot claim observer-owned evidence");
    check(native_ib&&FAILED(ownership::get_index_range_view(native_ib,old_indices,&native_indices))&&!native_indices.known,"native IB pointer cannot claim observer-owned certificate");
    api(f.device->SetStreamSource(0,nullptr,0,0),"finite unbind before Reset");api(f.device->SetIndices(nullptr),"finite index unbind before Reset");
    api(f.device->Reset(&f.pp),"finite observer Reset with managed allocations retained");f.baseline();
    expect(false,"Reset invalidates old upload attestation",ordinary);
}
}
int main(int argc,char**argv){std::setvbuf(stdout,nullptr,_IONBF,0);int result=1;HMODULE d3d=nullptr,d3dx=nullptr;WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3DrawInputFixture";RegisterClassA(&cls);HWND window=CreateWindowA(cls.lpszClassName,"X3 draw input fixture",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);
    try{if(argc!=2)throw std::runtime_error("expected native D3DX path");d3d=LoadLibraryA("d3d9.dll");d3dx=LoadLibraryA(argv[1]);check(d3d&&d3dx&&window,"native libraries and hidden window");auto fn=symbol<Assemble>(d3dx,"D3DXAssembleShader");const unsigned registers[]={0,6,24};for(unsigned i=0;i<3;++i){const auto r=registers[i];auto source=std::string("vs_2_0\ndcl_position v0\ndef c31, 1, 0, 0, 0\nmov r0.xyz, v0\nmov r0.w, c31.x\ndp4 oPos.x, r0, c")+std::to_string(r)+"\ndp4 oPos.y, r0, c"+std::to_string(r+1)+"\ndp4 oPos.z, r0, c"+std::to_string(r+2)+"\ndp4 oPos.w, r0, c"+std::to_string(r+3)+"\n";auto& v=vertices[i];v.words=assemble(fn,source);v.profile={hash(v.words.data(),v.words.size()*4),std::uint32_t(v.words.size()),std::uint16_t(r),true};}pixel=assemble(fn,"ps_2_0\ndef c0, 0.25, 0.5, 0.75, 1\nmov oC0, c0\n");pixel_profile={hash(pixel.data(),pixel.size()*4),std::uint32_t(pixel.size())};auto create=symbol<IDirect3D9*(WINAPI*)(UINT)>(d3d,"Direct3DCreate9");{Fixture fixture(create(D3D_SDK_VERSION),window);run(fixture);}{Fixture fixture(create(D3D_SDK_VERSION),window,true);run_finite(fixture);}std::printf("RESULT PASS checks=%u reads=%u state_checks=%u\n",checks,reads,state_checks);result=0;}catch(const std::exception&e){std::printf("RESULT FAIL %s checks=%u\n",e.what(),checks);}if(window)DestroyWindow(window);UnregisterClassA(cls.lpszClassName,cls.hInstance);if(d3dx)FreeLibrary(d3dx);if(d3d)FreeLibrary(d3d);return result;}
