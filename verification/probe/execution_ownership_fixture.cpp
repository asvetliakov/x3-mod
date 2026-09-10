// Original hidden native D3D9 device; fault injection only replaces per-instance
// native method slots. No application DLL, game process or install is touched.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace x3m::ownership;
namespace {
unsigned checks=0,failures=0,retired=0;
void check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL %s\n",name);}}
void require(HRESULT hr,const char* name){if(FAILED(hr)){std::printf("API_FAIL %s %08lx\n",name,hr);throw std::runtime_error(name);}}
ExecutionView view(IDirect3DDevice9* d){ExecutionView v;require(get_execution_view(d,&v),"execution view");return v;}
const GUID marker_id={0xbe659151,0x44a1,0x43bf,{0x88,0x27,0x01,0x53,0x62,0xa7,0x4e,0x1d}};
struct Marker final:IUnknown{
 ULONG refs=1;
 HRESULT WINAPI QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
 ULONG WINAPI AddRef()override{return ++refs;}
 ULONG WINAPI Release()override{auto n=--refs;if(!n){++retired;delete this;}return n;}
};
struct Device {
 IDirect3DDevice9* app=nullptr;IDirect3DDevice9* native=nullptr;D3DPRESENT_PARAMETERS pp{};
 Device(IDirect3D9* factory,HWND window,bool pure=false){
  pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
  pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24X8;
  require(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|(pure?D3DCREATE_PUREDEVICE:0),&pp,&app),"CreateDevice");native=borrowed_native_device(app);
 }
 ~Device(){if(app)app->Release();}
 void reset(){require(app->Reset(&pp),"Reset");}
 void history(){IDirect3DTexture9* t=nullptr;require(native->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&t,nullptr),"history");auto m=new Marker;
  require(t->SetPrivateData(marker_id,m,sizeof(IUnknown*),D3DSPD_IUNKNOWN),"history marker");m->Release();require(retain_renderer_resource(app,t),"retain history");}
};
struct Patch {
 void* object;void** saved;std::array<void*,119> table{};
 Patch(void* target,unsigned count,unsigned slot,void* replacement):object(target),saved(*reinterpret_cast<void***>(target)){
  std::memcpy(table.data(),saved,count*sizeof(void*));table[slot]=replacement;*reinterpret_cast<void***>(target)=table.data();}
 ~Patch(){*reinterpret_cast<void***>(object)=saved;}
};
HRESULT injected=E_FAIL;
HRESULT WINAPI noarg(IDirect3DDevice9*){return injected;}
HRESULT WINAPI gettexture(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9** out){if(out)*out=nullptr;return injected;}
HRESULT WINAPI getdepth(IDirect3DDevice9*,IDirect3DSurface9** out){if(out)*out=nullptr;return injected;}
HRESULT WINAPI container(IDirect3DSurface9*,REFIID,void** out){if(out)*out=nullptr;return injected;}
HRESULT WINAPI private_data(IDirect3DVertexBuffer9*,REFGUID,const void*,DWORD,DWORD){return injected;}
HRESULT WINAPI process(IDirect3DDevice9*,UINT,UINT,UINT,IDirect3DVertexBuffer9*,IDirect3DVertexDeclaration9*,DWORD){return injected;}
void basics(IDirect3D9* factory,HWND window,bool pure){
 Device d(factory,window,pure);auto v=view(d.app);check(v.known&&!v.scene_open&&!v.stateblock_recording&&v.queries_idle,"pristine known idle");
 ExecutionView invalid;check(get_execution_view(d.native,&invalid)==E_INVALIDARG&&!invalid.known,"native view rejected");check(get_execution_view(d.app,nullptr)==E_POINTER,"null output rejected");
 check(invalidate_execution_state(d.native)==E_INVALIDARG,"native invalidation rejected");
 require(d.app->BeginScene(),"BeginScene");check(view(d.app).scene_open,"scene opened");
 require(d.app->BeginStateBlock(),"BeginStateBlock");check(view(d.app).stateblock_recording,"recording observed");
 IDirect3DStateBlock9* state=nullptr;require(d.app->EndStateBlock(&state),"EndStateBlock");check(!view(d.app).stateblock_recording,"recording ended");
 require(state->Capture(),"state capture");require(state->Apply(),"state apply");state->Release();check(view(d.app).known&&view(d.app).scene_open,"stateblock preserves scene");
 require(d.app->CreateQuery(D3DQUERYTYPE_EVENT,nullptr),"query support probe");check(view(d.app).known,"support probe not query object");
 IDirect3DQuery9* q=nullptr;require(d.app->CreateQuery(D3DQUERYTYPE_OCCLUSION,&q),"occlusion query");check(view(d.app).queries_idle,"created occlusion idle");
 require(q->Issue(D3DISSUE_BEGIN),"query begin");check(view(d.app).known&&!view(d.app).queries_idle&&view(d.app).active_queries==1,"active interval blocks replay");
 require(d.app->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),"query enclosed clear");
 require(q->Issue(D3DISSUE_END),"query end");check(view(d.app).queries_idle,"ended interval idle without GetData");q->Release();
 require(d.app->EndScene(),"EndScene");check(!view(d.app).scene_open,"scene closed");
 auto generation=view(d.app).generation;d.reset();check(view(d.app).known&&view(d.app).queries_idle&&view(d.app).generation>generation,"Reset fresh scene generation");
 {injected=E_FAIL;Patch p(d.native,119,41,reinterpret_cast<void*>(&noarg));check(d.app->BeginScene()==E_FAIL,"ordinary Begin HRESULT preserved");}
 check(!view(d.app).known,"failed Begin unknown");d.reset();check(view(d.app).known,"Reset recovers transition failure");
 require(invalidate_execution_state(d.app),"explicit invalidation");check(!view(d.app).known,"native failure permanent invalidation");d.reset();check(!view(d.app).known,"invalidation survives Reset");
 std::printf("CASE basic pure=%u PASS\n",pure);
}
void unsupported(IDirect3D9* factory,HWND window){
 Device d(factory,window);IDirect3DQuery9* q=nullptr;require(d.app->CreateQuery(D3DQUERYTYPE_EVENT,&q),"real event query");
 check(!view(d.app).known&&view(d.app).reason==ExecutionReason::UnsupportedQuery,"event creation unavailable");q->Release();d.reset();check(!view(d.app).known,"query taint survives release Reset");
}
void lost_case(IDirect3D9* factory,HWND window,unsigned kind,HRESULT hr){
 Device d(factory,window);d.history();unsigned old=retired;
 require(d.app->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.5f,0),"source clear");require(copy_auto_depth(d.app),"depth snapshot");
 CopyDepthView before{},after{};require(get_copy_depth_view(d.app,&before),"initial copy view");check(before.copy_valid,"copy initially valid");
 injected=hr;HRESULT result=E_UNEXPECTED;
 if(kind==0){Patch p(d.native,119,64,reinterpret_cast<void*>(&gettexture));IDirect3DBaseTexture9* t=nullptr;result=d.app->GetTexture(0,&t);check(!t,"base output null");}
 if(kind==1){Patch p(d.native,119,40,reinterpret_cast<void*>(&getdepth));IDirect3DSurface9* t=nullptr;result=d.app->GetDepthStencilSurface(&t);check(!t,"typed output null");}
 if(kind==2){
  IDirect3DTexture9 *texture=nullptr,*native_texture=nullptr;IDirect3DSurface9 *surface=nullptr,*native_surface=nullptr;
  require(d.app->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr),"container texture");
  require(texture->GetSurfaceLevel(0,&surface),"wrapped texture surface");
  require(d.app->SetTexture(0,texture),"bind container texture");require(d.native->GetTexture(0,reinterpret_cast<IDirect3DBaseTexture9**>(&native_texture)),"native texture");
  require(native_texture->GetSurfaceLevel(0,&native_surface),"native surface");
  {Patch p(native_surface,17,11,reinterpret_cast<void*>(&container));void* out=nullptr;result=surface->GetContainer(IID_IDirect3DTexture9,&out);check(!out,"container output null");}
  native_surface->Release();native_texture->Release();surface->Release();texture->Release();
 }
 if(kind==3||kind==4){IDirect3DVertexBuffer9* vb=nullptr;require(d.app->CreateVertexBuffer(64,0,0,D3DPOOL_MANAGED,&vb,nullptr),"fault VB");
  auto nv=borrowed_native_buffer_for_lock_contract(vb);check(nv!=nullptr,"native VB endpoint");
  if(kind==3){Patch p(nv,14,4,reinterpret_cast<void*>(&private_data));DWORD bytes=1;result=vb->SetPrivateData(marker_id,&bytes,4,0);}
  else {Patch p(d.native,119,85,reinterpret_cast<void*>(&process));result=d.app->ProcessVertices(0,0,1,vb,nullptr,0);}
  vb->Release();
 }
 check(result==hr,"injected native HRESULT unchanged");require(get_copy_depth_view(d.app,&after),"post fault copy view");
 if(hr==E_FAIL){check(view(d.app).known,"ordinary getter failure preserves execution");check(after.available&&after.copy_valid,"ordinary error preserves snapshot");check(retired==old,"ordinary error preserves renderer resource");}
 else{check(!view(d.app).known,"getter loss invalidates execution");check(!after.available&&!after.texture&&!after.copy_valid,"getter loss retires snapshot");check(retired==old+1,"getter loss retires renderer resource");}
 std::printf("CASE loss method=%u hr=%08lx PASS\n",kind,hr);
}
}
int main(){try{
 auto module=LoadLibraryA("d3d9.dll");if(!module)throw std::runtime_error("d3d9");
 IDirect3D9*(WINAPI*create)(UINT)=nullptr;auto proc=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&proc,sizeof(create));
 if(!create)throw std::runtime_error("Direct3DCreate9");
 IDirect3D9* factory=nullptr;Options options;options.track_execution_state=true;options.capture_auto_depth=true;
 require(wrap_factory(create(D3D_SDK_VERSION),&factory,options),"wrap factory");
 HWND window=CreateWindowExA(0,"STATIC","Original execution observer fixture",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
 if(!window)throw std::runtime_error("window");
 {
  IDirect3D9* disabled=nullptr;require(wrap_factory(create(D3D_SDK_VERSION),&disabled),"default disabled factory");
  {Device d(disabled,window);check(!view(d.app).requested&&!view(d.app).known&&!view(d.app).queries_idle,"default option unavailable");
   require(d.app->BeginScene(),"disabled BeginScene");require(d.app->EndScene(),"disabled EndScene");d.reset();check(!view(d.app).known,"disabled stays unknown after calls and Reset");}
  disabled->Release();
 }
 basics(factory,window,false);basics(factory,window,true);unsupported(factory,window);
 for(unsigned kind=0;kind<5;++kind)for(auto hr:{E_FAIL,D3DERR_DEVICELOST,D3DERR_DEVICENOTRESET})lost_case(factory,window,kind,hr);
 factory->Release();DestroyWindow(window);FreeLibrary(module);
 std::printf("RESULT %s checks=%u failures=%u\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
 }catch(const std::exception& e){std::printf("EXCEPTION %s\nRESULT FAIL checks=%u failures=%u\n",e.what(),checks,failures+1);return 2;}}
