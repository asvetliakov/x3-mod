// Canonical D3D9 ownership contract fixture. All resources are disposable; no X3.
// Pointer identity is compared only while the native object remains reachable.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#ifdef X3M_OWNERSHIP_WRAPPED
#include "../../src/ownership/d3d9_ownership.h"
#endif
static unsigned failures=0,checks=0;
static void expect(const char* name,bool pass){++checks;std::printf("CHECK %s %s\n",name,pass?"PASS":"FAIL");failures+=!pass;}
static bool ok(const char* name,HRESULT result){std::printf("RESULT %s %08lx\n",name,result);expect(name,SUCCEEDED(result));return SUCCEEDED(result);}
template<class T>static void release(T*& value){if(value){value->Release();value=nullptr;}}
static void identity(IUnknown* object,const char* name){if(!object){expect(name,false);return;}IUnknown* first=nullptr;IUnknown* second=nullptr;if(ok(name,object->QueryInterface(IID_PPV_ARGS(&first)))){ok("IUnknown repeat",first->QueryInterface(IID_PPV_ARGS(&second)));expect("IUnknown canonical",first==second&&first==object);release(second);release(first);}}
template<class T>static void owner(T* child,IDirect3DDevice9* device){if(!child){expect("non-null child",false);return;}IDirect3DDevice9* obtained=nullptr;if(ok("child GetDevice",child->GetDevice(&obtained))){expect("child canonical device",obtained==device);release(obtained);}}
// An implicit backbuffer's private-data marker dies with the backend device.
// It holds no resource/device reference, so it cannot manufacture a COM cycle.
static unsigned backend_destroyed;
static const GUID backend_marker={0x19f70851,0x2f68,0x4d58,{0xb1,0x07,0x11,0xf3,0x73,0xee,0x72,0x01}};
struct BackendMarker final:IUnknown{
    ULONG refs=1;
    HRESULT WINAPI QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG WINAPI AddRef()override{return ++refs;}
    ULONG WINAPI Release()override{const ULONG remaining=--refs;if(!remaining){++backend_destroyed;delete this;}return remaining;}
};
static void mark_backend_lifetime(IDirect3DDevice9* device){
    IDirect3DSurface9* back=nullptr;if(!ok("backend lifetime backbuffer",device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)))return;
    auto* marker=new BackendMarker;ok("backend lifetime marker",back->SetPrivateData(backend_marker,marker,sizeof(IUnknown*),D3DSPD_IUNKNOWN));marker->Release();release(back);
}
#ifdef X3M_OWNERSHIP_WRAPPED
static unsigned renderer_destroyed;
static const GUID history_marker={0x7347cf93,0x1869,0x4b80,{0xaa,0x53,0x81,0xb4,0x09,0x27,0x80,0x11}};
struct HistoryMarker final:IUnknown{
    ULONG refs=1;
    HRESULT WINAPI QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG WINAPI AddRef()override{return ++refs;}
    ULONG WINAPI Release()override{const ULONG remaining=--refs;if(!remaining){++renderer_destroyed;delete this;}return remaining;}
};
static void attach_history(IDirect3DDevice9* device){
    IDirect3DDevice9* raw=x3m::ownership::borrowed_native_device(device);expect("borrowed native renderer device",raw&&raw!=device);if(!raw)return;
    IDirect3DTexture9* history=nullptr;if(!ok("native renderer FP16 history",raw->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&history,nullptr)))return;
    auto* marker=new HistoryMarker;ok("history lifetime marker",history->SetPrivateData(history_marker,marker,sizeof(IUnknown*),D3DSPD_IUNKNOWN));marker->Release();
    const unsigned before=renderer_destroyed;
    if(ok("adopt native renderer history",x3m::ownership::retain_renderer_resource(device,history)))expect("adopted history remains alive",renderer_destroyed==before);else history->Release();
}
static void renderer_seam(IDirect3DDevice9* device){
    expect("borrowed null safely rejected",x3m::ownership::borrowed_native_device(nullptr)==nullptr);
    expect("borrowed foreign pointer safely rejected",x3m::ownership::borrowed_native_device(reinterpret_cast<IDirect3DDevice9*>(1))==nullptr);
    expect("null renderer resource rejected",FAILED(x3m::ownership::retain_renderer_resource(device,nullptr)));
    IDirect3DTexture9* app=nullptr;if(ok("application texture for seam rejection",device->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&app,nullptr))){expect("application wrapper rejected as renderer resource",FAILED(x3m::ownership::retain_renderer_resource(device,app)));D3DSURFACE_DESC desc{};ok("rejected resource reference preserved",app->GetLevelDesc(0,&desc));release(app);}
}
#endif
static IDirect3D9* factory(){
    HMODULE module=LoadLibraryA("d3d9.dll");auto address=module?GetProcAddress(module,"Direct3DCreate9"):nullptr;
    IDirect3D9*(WINAPI*create)(UINT)=nullptr;std::memcpy(&create,&address,sizeof create);IDirect3D9* native=create?create(D3D_SDK_VERSION):nullptr;if(!native)return nullptr;
#ifdef X3M_OWNERSHIP_WRAPPED
    IDirect3D9* wrapped=nullptr;
    expect("factory null output rejected",FAILED(x3m::ownership::wrap_factory(native,nullptr)));
    expect("failed factory adoption preserves native reference",native->GetAdapterCount()>0);
    // Step B locked-prefix bounds on: exercises the Lock/Unlock scan path (locked_prefix_case).
    x3m::ownership::Options options{};options.locked_prefix_bounds=true;
    if(!ok("wrap_factory",x3m::ownership::wrap_factory(native,&wrapped,options))){native->Release();return nullptr;}return wrapped;
#else
    return native;
#endif
}
static void ex_factory_case(){
    HMODULE module=GetModuleHandleA("d3d9.dll");auto address=module?GetProcAddress(module,"Direct3DCreate9Ex"):nullptr;
    HRESULT(WINAPI*create_ex)(UINT,IDirect3D9Ex**)=nullptr;std::memcpy(&create_ex,&address,sizeof create_ex);
    if(!create_ex){std::puts("OBSERVE Ex_factory_export=unavailable");return;}
    IDirect3D9Ex* native=nullptr;const HRESULT created=create_ex(D3D_SDK_VERSION,&native);std::printf("OBSERVE Ex_factory_creation=%08lx\n",created);
    if(SUCCEEDED(created)&&native){
#ifdef X3M_OWNERSHIP_WRAPPED
        IDirect3D9* out=nullptr;const HRESULT wrapped=x3m::ownership::wrap_factory(native,&out);
        expect("Ex factory rejected at ownership boundary",FAILED(wrapped)&&out==nullptr);
        if(SUCCEEDED(wrapped)){release(out);native=nullptr;}
        else expect("Ex rejection preserves caller reference",native->GetAdapterCount()>0);
#endif
        release(native);
    }
}
static void texture_case(IDirect3DDevice9* device){
    IDirect3DTexture9* texture=nullptr;if(!ok("managed texture",device->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr)))return;
    owner(texture,device);identity(texture,"texture identity");
    IDirect3DBaseTexture9* base=nullptr;IDirect3DResource9* resource=nullptr;
    ok("texture QI base",texture->QueryInterface(IID_PPV_ARGS(&base)));ok("texture QI resource",texture->QueryInterface(IID_PPV_ARGS(&resource)));
    IUnknown *a=nullptr,*b=nullptr,*c=nullptr;texture->QueryInterface(IID_PPV_ARGS(&a));if(base)base->QueryInterface(IID_PPV_ARGS(&b));if(resource)resource->QueryInterface(IID_PPV_ARGS(&c));expect("texture/base/resource IUnknown identity",a==b&&b==c&&a==texture);release(a);release(b);release(c);release(base);release(resource);
    IDirect3DSurface9* surface=nullptr;ok("texture surface",texture->GetSurfaceLevel(0,&surface));if(surface){owner(surface,device);IDirect3DTexture9* container=nullptr;ok("surface texture container",surface->GetContainer(IID_PPV_ARGS(&container)));expect("surface texture container canonical",container==texture);release(container);}
    ok("bind texture",device->SetTexture(0,texture));release(texture);
    IDirect3DBaseTexture9* bound=nullptr;ok("regain bound texture",device->GetTexture(0,&bound));expect("bound texture regained after external zero",bound!=nullptr);owner(bound,device);identity(bound,"regained texture identity");
    IDirect3DBaseTexture9* second=nullptr;ok("overlapping texture getter",device->GetTexture(0,&second));expect("overlapping texture getters canonical",second==bound);release(second);
    if(surface){IDirect3DBaseTexture9* container=nullptr;ok("held surface parent after texture regain",surface->GetContainer(IID_PPV_ARGS(&container)));expect("regained texture and held surface parent canonical",container==bound);release(container);}release(surface);release(bound);
    // Stateblock is now the only non-device retention source after unbinding.
    IDirect3DStateBlock9* state=nullptr;ok("stateblock captures texture",device->CreateStateBlock(D3DSBT_ALL,&state));if(state)owner(state,device);
    ok("unbind texture",device->SetTexture(0,nullptr));if(state)ok("stateblock restore",state->Apply());
    ok("stateblock regained texture",device->GetTexture(0,&bound));expect("stateblock restored texture",bound!=nullptr);if(bound){owner(bound,device);identity(bound,"stateblock regained texture identity");}release(bound);release(state);ok("final texture unbind",device->SetTexture(0,nullptr));
}
static void buffer_shader_case(IDirect3DDevice9* device){
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    ok("vertex buffer",device->CreateVertexBuffer(64,0,D3DFVF_XYZRHW,D3DPOOL_MANAGED,&vb,nullptr));ok("index buffer",device->CreateIndexBuffer(6,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));
    if(vb){owner(vb,device);ok("bind stream",device->SetStreamSource(0,vb,0,16));release(vb);UINT offset=99,stride=99;ok("get stream after external zero",device->GetStreamSource(0,&vb,&offset,&stride));expect("canonical stream and offsets",vb!=nullptr&&offset==0&&stride==16);release(vb);ok("unbind stream",device->SetStreamSource(0,nullptr,0,0));}
    if(ib){owner(ib,device);ok("bind indices",device->SetIndices(ib));release(ib);ok("get indices after external zero",device->GetIndices(&ib));expect("indices regained",ib!=nullptr);if(ib){owner(ib,device);identity(ib,"regained index identity");}release(ib);ok("unbind indices",device->SetIndices(nullptr));}
    const DWORD code[]={0xffff0200,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};IDirect3DPixelShader9* shader=nullptr;
    if(ok("pixel shader",device->CreatePixelShader(code,&shader))){owner(shader,device);ok("bind pixel shader",device->SetPixelShader(shader));release(shader);ok("get pixel shader after external zero",device->GetPixelShader(&shader));expect("pixel shader regained",shader!=nullptr);if(shader){owner(shader,device);identity(shader,"regained shader identity");}release(shader);ok("unbind pixel shader",device->SetPixelShader(nullptr));}
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITIONT,0},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;
    if(ok("vertex declaration",device->CreateVertexDeclaration(elements,&decl))){owner(decl,device);ok("bind declaration",device->SetVertexDeclaration(decl));release(decl);ok("get declaration after external zero",device->GetVertexDeclaration(&decl));expect("declaration regained",decl!=nullptr);if(decl){owner(decl,device);identity(decl,"regained declaration identity");}release(decl);ok("unbind declaration",device->SetVertexDeclaration(nullptr));}
}
static void surface_case(IDirect3DDevice9* device){
    IDirect3DSurface9 *back=nullptr,*target=nullptr,*depth=nullptr;
    ok("save backbuffer for surface binding",device->GetRenderTarget(0,&back));
    ok("standalone render target",device->CreateRenderTarget(64,64,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok("standalone depth",device->CreateDepthStencilSurface(64,64,D3DFMT_D24X8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));
    if(target&&depth){
        owner(target,device);owner(depth,device);ok("bind standalone target",device->SetRenderTarget(0,target));ok("bind standalone depth",device->SetDepthStencilSurface(depth));release(target);release(depth);
        ok("regain target after external zero",device->GetRenderTarget(0,&target));ok("regain depth after external zero",device->GetDepthStencilSurface(&depth));
        if(target){owner(target,device);identity(target,"regained target identity");}if(depth){owner(depth,device);identity(depth,"regained depth identity");}
        IDirect3DSurface9* again=nullptr;ok("overlapping target getter",device->GetRenderTarget(0,&again));expect("target overlapping identity",again==target);release(again);
        ok("clear wrapped bound surfaces",device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff102030,1,0));
        ok("restore original target",device->SetRenderTarget(0,back));ok("unbind standalone depth",device->SetDepthStencilSurface(nullptr));
    }
    release(target);release(depth);release(back);ok("present forwarded",device->Present(nullptr,nullptr,nullptr,nullptr));
}
static void failure_output_case(IDirect3DDevice9* device){
    // Sentinels are valid held objects. A failed output slot is observed only;
    // its value is neither dereferenced nor assumed to transfer a reference.
    IDirect3DTexture9* texture=nullptr;if(!ok("sentinel texture",device->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr)))return;
    IDirect3DTexture9* created=texture;
    HRESULT result=device->CreateTexture(0,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&created,nullptr);
    std::printf("OBSERVE failed_CreateTexture_dimensions=%08lx unchanged=%d null=%d\n",result,created==texture,created==nullptr);expect("invalid texture dimensions fail",FAILED(result));if(SUCCEEDED(result)&&created!=texture)release(created);
    created=texture;result=device->CreateTexture(8,8,1,0,D3DFORMAT(0xffffffffu),D3DPOOL_MANAGED,&created,nullptr);
    std::printf("OBSERVE failed_CreateTexture_format=%08lx unchanged=%d null=%d\n",result,created==texture,created==nullptr);expect("invalid texture format fails",FAILED(result));if(SUCCEEDED(result)&&created!=texture)release(created);
    IDirect3DBaseTexture9* queried=texture;result=device->GetTexture(200,&queried);
    std::printf("OBSERVE GetTexture_invalid_stage=%08lx unchanged=%d null=%d\n",result,queried==texture,queried==nullptr);if(SUCCEEDED(result))release(queried);
    IDirect3DSurface9* back=nullptr;ok("sentinel backbuffer",device->GetRenderTarget(0,&back));IDirect3DSurface9* target=back;result=device->GetRenderTarget(99,&target);
    std::printf("OBSERVE failed_GetRenderTarget=%08lx unchanged=%d null=%d\n",result,target==back,target==nullptr);expect("invalid render target index fails",FAILED(result));if(SUCCEEDED(result))release(target);release(back);
    IDirect3DSwapChain9* implicit=nullptr;ok("sentinel swapchain",device->GetSwapChain(0,&implicit));IDirect3DSwapChain9* chain=implicit;result=device->GetSwapChain(99,&chain);
    std::printf("OBSERVE failed_GetSwapChain=%08lx unchanged=%d null=%d\n",result,chain==implicit,chain==nullptr);expect("invalid swapchain index fails",FAILED(result));if(SUCCEEDED(result))release(chain);release(implicit);
    IDirect3DVertexBuffer9* vb=nullptr;ok("sentinel stream buffer",device->CreateVertexBuffer(64,0,0,D3DPOOL_MANAGED,&vb,nullptr));IDirect3DVertexBuffer9* stream=vb;UINT offset=42,stride=24;result=device->GetStreamSource(99,&stream,&offset,&stride);
    std::printf("OBSERVE GetStreamSource_invalid=%08lx unchanged=%d null=%d offset=%u stride=%u\n",result,stream==vb,stream==nullptr,offset,stride);if(SUCCEEDED(result))release(stream);release(vb);
    IDirect3DSurface9* surface=nullptr;ok("sentinel texture surface",texture->GetSurfaceLevel(0,&surface));
    if(surface){const GUID unknown={0x92f54ef2,0xbbcc,0x47c3,{0xa9,0x77,0x43,0xfe,0x58,0x16,0x33,0xab}};void* container=texture;result=surface->GetContainer(unknown,&container);std::printf("OBSERVE failed_GetContainer=%08lx unchanged=%d null=%d\n",result,container==texture,container==nullptr);expect("unknown container interface fails",FAILED(result));if(SUCCEEDED(result)&&container)static_cast<IUnknown*>(container)->Release();release(surface);}
    release(texture);
}
static void query_case(IDirect3DDevice9* device){
    ok("CreateQuery null output capability",device->CreateQuery(D3DQUERYTYPE_EVENT,nullptr));
    IDirect3DQuery9* query=nullptr;if(ok("event query",device->CreateQuery(D3DQUERYTYPE_EVENT,&query))){owner(query,device);identity(query,"query identity");expect("query type unchanged",query->GetType()==D3DQUERYTYPE_EVENT);expect("query data size unchanged",query->GetDataSize()==sizeof(BOOL));ok("event query Issue",query->Issue(D3DISSUE_END));const HRESULT status=query->GetData(nullptr,0,0);expect("query GetData result valid",status==S_OK||status==S_FALSE);release(query);}
}
static void container_case(IDirect3DDevice9* device){
    IDirect3DCubeTexture9* cube=nullptr;if(ok("cube texture",device->CreateCubeTexture(8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cube,nullptr))){IDirect3DSurface9* face=nullptr;ok("cube face",cube->GetCubeMapSurface(D3DCUBEMAP_FACE_POSITIVE_X,0,&face));if(face){owner(face,device);IDirect3DCubeTexture9* parent=nullptr;ok("cube face container",face->GetContainer(IID_PPV_ARGS(&parent)));expect("cube container canonical",parent==cube);release(parent);release(face);}release(cube);}
    IDirect3DVolumeTexture9* texture=nullptr;if(ok("volume texture",device->CreateVolumeTexture(8,8,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr))){IDirect3DVolume9* volume=nullptr;ok("volume level",texture->GetVolumeLevel(0,&volume));if(volume){owner(volume,device);IDirect3DVolumeTexture9* parent=nullptr;ok("volume container",volume->GetContainer(IID_PPV_ARGS(&parent)));expect("volume container canonical",parent==texture);release(parent);release(volume);}release(texture);}
}
static void swapchain_case(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS pp){
    IDirect3DSwapChain9* implicit=nullptr;ok("implicit swapchain",device->GetSwapChain(0,&implicit));if(implicit){owner(implicit,device);IDirect3DSurface9 *a=nullptr,*b=nullptr;ok("device backbuffer",device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&a));ok("swapchain backbuffer",implicit->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&b));expect("implicit backbuffer canonical",a==b);if(a){IDirect3DSwapChain9* container=nullptr;const auto hr=a->GetContainer(IID_PPV_ARGS(&container));std::printf("OBSERVE implicit backbuffer GetContainer swapchain=%08lx equal=%d\n",hr,container==implicit);if(SUCCEEDED(hr))expect("implicit backbuffer container canonical",container==implicit);release(container);}release(a);release(b);release(implicit);ok("regain implicit swapchain",device->GetSwapChain(0,&implicit));expect("implicit swapchain regained",implicit!=nullptr);if(implicit){IDirect3DSwapChain9* same=nullptr;ok("overlapping implicit getter",device->GetSwapChain(0,&same));expect("overlapping implicit canonical",same==implicit);release(same);}release(implicit);}
    IDirect3DSwapChain9* additional=nullptr;if(ok("additional swapchain",device->CreateAdditionalSwapChain(&pp,&additional))){owner(additional,device);const UINT count=device->GetNumberOfSwapChains();std::printf("OBSERVE chains_with_additional=%u\n",count);IDirect3DSwapChain9* same=nullptr;const HRESULT enumerated=device->GetSwapChain(1,&same);std::printf("OBSERVE additional_enumeration=%08lx\n",enumerated);if(SUCCEEDED(enumerated))expect("additional chain canonical",same==additional);release(same);
        IDirect3DSurface9* back=nullptr;ok("additional backbuffer",additional->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&back));if(back){IDirect3DSwapChain9* container=nullptr;ok("additional backbuffer container",back->GetContainer(IID_PPV_ARGS(&container)));expect("additional container canonical",container==additional);release(container);release(back);}release(additional);std::printf("OBSERVE chains_after_additional_release=%u\n",device->GetNumberOfSwapChains());}
}
#ifdef X3M_OWNERSHIP_WRAPPED
// Step B locked-prefix bound (docs/architecture/screen-emission-region.md):
// the ownership Lock/Unlock scan of a marked DISCARD-locked dynamic vertex
// buffer with the bullet layout (FLOAT3 at 0, stride 24, 6144 x 24 bytes),
// the record state machine (unmarked, marked, pending, nested, non-DISCARD,
// NaN tail), erase at final Release and clear at Reset.
static void locked_prefix_case(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS pp){
    using namespace x3m::ownership;
    constexpr UINT bytes=147456;
    auto write=[&](IDirect3DVertexBuffer9* vb,unsigned quads,float tail,DWORD flags){
        void* data=nullptr;if(!ok("prefix lock",vb->Lock(0,bytes,&data,flags)))return;
        float* words=static_cast<float*>(data);for(unsigned n=0;n<bytes/4;++n)words[n]=tail;
        for(unsigned q=0;q<quads;++q)for(unsigned k=0;k<6;++k){float* v=words+(q*6+k)*6;v[0]=float(q)*.01f+((k&1)?.5f:-.5f);v[1]=(k>>1)?.25f:-.25f;v[2]=1+float(q)*.001f;}
        ok("prefix unlock",vb->Unlock());};
    auto view=[&](IDirect3DVertexBuffer9* vb,UINT count,bool mark){LockedPrefixView v{};const HRESULT hr=get_locked_prefix_view(vb,count,mark,&v);
        // PREFIX, not OBSERVE: wrapped-only, so no baseline counterpart to compare.
        std::printf("PREFIX view count=%u mark=%u hr=%08lx requested=%u known=%u reason=%u checkpoint=%lu revision=%llu\n",count,mark,hr,v.requested,v.known,v.reason,static_cast<unsigned long>(v.checkpoint),static_cast<unsigned long long>(v.revision));
        expect("prefix view recognised",SUCCEEDED(hr)&&v.requested);return v;};
    auto used=[]{LockedPrefixStatistics s{};get_locked_prefix_statistics(&s);return s.used;};
    IDirect3DVertexBuffer9* vb=nullptr;if(!ok("prefix dynamic buffer",device->CreateVertexBuffer(bytes,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb,nullptr)))return;
    const unsigned used0=used();
    expect("unmarked buffer unknown",view(vb,102,false).reason==1);
    write(vb,17,0.f,D3DLOCK_DISCARD);
    expect("unmarked lock not recorded",view(vb,102,false).reason==1&&used()==used0);
    expect("first draw marks and is refused",view(vb,102,true).reason==1&&used()==used0+1);
    write(vb,17,0.f,D3DLOCK_DISCARD);
    LockedPrefixView v=view(vb,102,true);expect("scanned prefix bound",v.known&&v.checkpoint==1&&v.revision==1);
    expect("covering box holds the quads and the zero tail",v.centre[0]-v.half[0]<=-.5&&v.centre[0]+v.half[0]>=.66-1e-5&&v.centre[1]-v.half[1]<=-.25&&v.centre[1]+v.half[1]>=.25&&v.centre[2]-v.half[2]<=0&&v.centre[2]+v.half[2]>=1.016-1e-4);
    v=view(vb,96,true);expect("exact checkpoint excludes the tail",v.known&&v.checkpoint==0&&v.centre[2]-v.half[2]>=1-1e-6);
    expect("draw past the scan refused",view(vb,6145,true).reason==5);
    void* outer=nullptr;ok("prefix outer lock",vb->Lock(0,bytes,&outer,D3DLOCK_DISCARD));expect("locked buffer pending",view(vb,102,true).reason==2);
    void* inner=nullptr;ok("prefix nested lock",vb->Lock(0,bytes,&inner,D3DLOCK_DISCARD));expect("nested lock invalid",view(vb,102,true).reason==3);
    ok("prefix nested unlock",vb->Unlock());ok("prefix outer unlock",vb->Unlock());expect("nested lock stays invalid",view(vb,102,true).reason==3);
    write(vb,17,0.f,0);expect("non-DISCARD lock invalid",view(vb,102,true).reason==3);
    write(vb,17,__builtin_nanf(""),D3DLOCK_DISCARD);expect("NaN tail inside the covering checkpoint refused",view(vb,102,true).reason==6&&view(vb,96,true).known);
    write(vb,17,0.f,D3DLOCK_DISCARD);v=view(vb,102,true);expect("relearned after every lock advanced the revision",v.known&&v.revision==6);
    release(vb);expect("final release erases the record",used()==used0);
    IDirect3DVertexBuffer9* sys=nullptr;if(!ok("prefix systemmem buffer",device->CreateVertexBuffer(bytes,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_SYSTEMMEM,&sys,nullptr)))return;
    view(sys,102,true);write(sys,17,0.f,D3DLOCK_DISCARD);expect("systemmem prefix bound before reset",view(sys,102,false).known&&used()==used0+1);
    ok("prefix reset",device->Reset(&pp));
    expect("reset clears the records",used()==0&&view(sys,102,false).reason==1);
    write(sys,17,0.f,D3DLOCK_DISCARD);expect("after reset a lock without a mark is ignored",view(sys,102,false).reason==1&&used()==0);
    view(sys,102,true);write(sys,17,0.f,D3DLOCK_DISCARD);expect("relearned after reset",view(sys,102,false).known);
    release(sys);expect("systemmem release erases the record",used()==0);
}
#endif
static void reset_case(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS pp){
#ifdef X3M_OWNERSHIP_WRAPPED
    unsigned before=renderer_destroyed;attach_history(device);
#endif
    IDirect3DTexture9* retained=nullptr;ok("default texture before reset",device->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&retained,nullptr));
    HRESULT failed=device->Reset(&pp);std::printf("OBSERVE reset_retained_default=%08lx\n",failed);expect("reset rejects retained default resource",FAILED(failed));
#ifdef X3M_OWNERSHIP_WRAPPED
    expect("renderer history released before failed reset",renderer_destroyed==before+1);
    before=renderer_destroyed;auto* rejected=new HistoryMarker;
    expect("renderer adoption disabled after failed reset",FAILED(x3m::ownership::retain_renderer_resource(device,rejected)));
    expect("failed renderer adoption leaves caller reference alive",renderer_destroyed==before);rejected->Release();
    expect("caller releases rejected renderer reference",renderer_destroyed==before+1);
#endif
    release(retained);ok("reset retry after resource release",device->Reset(&pp));
#ifdef X3M_OWNERSHIP_WRAPPED
    before=renderer_destroyed;attach_history(device);
#endif
    ok("separate successful reset",device->Reset(&pp));
#ifdef X3M_OWNERSHIP_WRAPPED
    expect("renderer history released before successful reset",renderer_destroyed==before+1);
#endif
    IDirect3DSurface9* back=nullptr;ok("backbuffer after reset",device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));if(back)owner(back,device);release(back);
}
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);IDirect3D9* api=factory();if(!api)return 2;identity(api,"factory identity");ex_factory_case();
    WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="X3OwnershipFixture";RegisterClassA(&wc);
    HWND window=CreateWindowA(wc.lpszClassName,"X3 ownership verification",WS_OVERLAPPEDWINDOW,80,80,160,160,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(window,SW_SHOWNOACTIVATE);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    for(unsigned iteration=0;iteration<2;++iteration){
        std::printf("ITERATION %u\n",iteration);IDirect3DDevice9* device=nullptr;if(!ok("create device",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|(iteration?D3DCREATE_PUREDEVICE:0),&pp,&device)))break;
        identity(device,"device identity");IDirect3D9* parent=nullptr;ok("device GetDirect3D",device->GetDirect3D(&parent));expect("canonical factory parent",parent==api);release(parent);
        texture_case(device);buffer_shader_case(device);surface_case(device);failure_output_case(device);query_case(device);container_case(device);swapchain_case(device,pp);reset_case(device,pp);
#ifdef X3M_OWNERSHIP_WRAPPED
        locked_prefix_case(device,pp);
#endif
        const unsigned backend_before_final=backend_destroyed;mark_backend_lifetime(device);expect("implicit backbuffer marker survives released caller reference",backend_destroyed==backend_before_final);
#ifdef X3M_OWNERSHIP_WRAPPED
        renderer_seam(device);const unsigned history_before_final=renderer_destroyed;attach_history(device);
#endif
        // A public child must preserve GetDevice after the caller releases its
        // device reference. Never call the released pointer to recover ownership.
        IDirect3DTexture9* child=nullptr;ok("retained child",device->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&child,nullptr));auto original=device;std::printf("OBSERVE device_release_with_child=%lu\n",device->Release());device=nullptr;
        expect("backend stays alive through retained public child",backend_destroyed==backend_before_final);
#ifdef X3M_OWNERSHIP_WRAPPED
        expect("renderer history lives while public child retains device",renderer_destroyed==history_before_final);
#endif
        if(child){ok("recover device from child",child->GetDevice(&device));expect("recovered canonical device",device==original);release(child);}
        if(device)expect("final device references zero",device->Release()==0);
        expect("native backend destroyed after final logical device release",backend_destroyed==backend_before_final+1);
#ifdef X3M_OWNERSHIP_WRAPPED
        expect("renderer history destroyed at final logical device release",renderer_destroyed==history_before_final+1);
#endif
    }
    // Factory must remain recoverable through a public device after the caller
    // drops its own factory reference; this also exercises the parent chain.
    IDirect3DDevice9* last_device=nullptr;
    if(ok("device retaining factory",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&last_device))){
        auto original_factory=api;api->Release();api=nullptr;
        ok("recover released factory through device",last_device->GetDirect3D(&api));expect("recovered factory canonical",api==original_factory);
        expect("parent-chain device final zero",last_device->Release()==0);
    }
    if(api)expect("factory references zero",api->Release()==0);
    DestroyWindow(window);std::printf("OWNERSHIP RESULT checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
