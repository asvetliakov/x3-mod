// Actual locally supplied shader pair, original geometry/textures; no game assets copied.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/material_motion.h"
#include "../../src/renderer/rigid_replay_program.h"
#include "../../src/renderer/rigid_motion_pixel_program.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
using namespace x3m::renderer;
using Words=std::vector<std::uint32_t>;
unsigned checks=0,numerics=0,colors=0,depth_cases=0,configurations=0;
void api(HRESULT h){if(FAILED(h)){std::printf("API FAIL %08lx\n",h);throw std::runtime_error("native API");}}
void require(bool b,const char* label){++checks;std::printf("CHECK %s %s\n",label,b?"PASS":"FAIL");if(!b)throw std::runtime_error(label);}
template<class T>struct Com{T* p=nullptr;Com()=default;Com(const Com&)=delete;~Com(){if(p)p->Release();}T*operator->()const{return p;}};
template<class T>T symbol(HMODULE m,const char* name){auto raw=GetProcAddress(m,name);T fn=nullptr;std::memcpy(&fn,&raw,sizeof fn);if(!fn)throw std::runtime_error(name);return fn;}
Words load(const char* name){std::ifstream f(name,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("local shader missing");auto size=f.tellg();if(size<=0||size%4||size>65536)throw std::runtime_error("shader size");Words w(std::size_t(size)/4);f.seekg(0);f.read(reinterpret_cast<char*>(w.data()),size);if(!f)throw std::runtime_error("shader read");return w;}
const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
unsigned short half(float x){ // Original fixture values are exact half-representable.
    unsigned b;std::memcpy(&b,&x,4);unsigned e=(b>>23)&255;return static_cast<unsigned short>((b>>16&0x8000)|(e?((e-112)<<10)|(b>>13&1023):0));
}
struct Pixel{float value[4];};
struct Configuration{bool packed=false,perspective=false,valid=true;unsigned lights=0;float translation=0,jx=0,jy=0,material_scale=1;};
struct Gpu {
    IDirect3DDevice9* d;UINT w,h;D3DFORMAT format;
    Com<IDirect3DVertexShader9> vs[2];Com<IDirect3DPixelShader9> ps[2];
    Com<IDirect3DVertexShader9> replay_vs;Com<IDirect3DPixelShader9> replay_ps;
    Com<IDirect3DVertexDeclaration9> declaration[2];Com<IDirect3DVertexBuffer9> vb[2];
    Com<IDirect3DTexture9> textures[3],motion;Com<IDirect3DCubeTexture9> cube;
    Com<IDirect3DSurface9> color,depth,motion_surface,back;
    Gpu(IDirect3DDevice9* device,const Words& v,const Words& p,const MaterialMotionVariant& changed,UINT width,UINT height,D3DFORMAT f):d(device),w(width),h(height),format(f){
        api(d->GetRenderTarget(0,&back.p));api(d->CreateRenderTarget(w,h,f,D3DMULTISAMPLE_NONE,0,FALSE,&color.p,nullptr));
        api(d->CreateDepthStencilSurface(w,h,D3DFMT_D24X8,D3DMULTISAMPLE_NONE,0,FALSE,&depth.p,nullptr));
        api(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&motion.p,nullptr));api(motion->GetSurfaceLevel(0,&motion_surface.p));
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&vs[0].p));api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(changed.vertex.data()),&vs[1].p));
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&ps[0].p));api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(changed.pixel.data()),&ps[1].p));
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(rigid_replay_program().data()),&replay_vs.p));api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(rigid_motion_pixel_program()),&replay_ps.p));
        const float positions[3][3]={{-1,1,.5f},{3,1,.5f},{-1,-3,.5f}};
        for(UINT packed=0;packed<2;++packed){const UINT stride=packed?24:32;
            const D3DVERTEXELEMENT9 elements[]={{0,0,BYTE(packed?D3DDECLTYPE_FLOAT16_4:D3DDECLTYPE_FLOAT3),D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
                {0,WORD(packed?8:12),BYTE(packed?D3DDECLTYPE_FLOAT16_4:D3DDECLTYPE_FLOAT2),D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
                {0,WORD(packed?16:20),BYTE(packed?D3DDECLTYPE_FLOAT16_4:D3DDECLTYPE_FLOAT3),D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},D3DDECL_END()};
            api(d->CreateVertexDeclaration(elements,&declaration[packed].p));api(d->CreateVertexBuffer(stride*3,0,0,D3DPOOL_MANAGED,&vb[packed].p,nullptr));void* dst=nullptr;api(vb[packed]->Lock(0,0,&dst,0));
            for(UINT i=0;i<3;++i){if(packed){unsigned short data[12]={half(positions[i][0]),half(positions[i][1]),half(.5f),half(7),half(float(i&1)),half(float(i>>1)),0,half(7),0,0,half(1),half(7)};std::memcpy(static_cast<char*>(dst)+i*stride,data,stride);}
                else{float data[8]={positions[i][0],positions[i][1],.5f,float(i&1),float(i>>1),0,0,1};std::memcpy(static_cast<char*>(dst)+i*stride,data,stride);}}
            api(vb[packed]->Unlock());}
        const DWORD texels[3][4]={{0x99704020,0xcc208050,0xaa508020,0xee403080},{0x80302010,0x90401020,0xa0205030,0xb0402060},{0x20100804,0x30201008,0x40201018,0x50182010}};
        for(UINT i=0;i<3;++i){api(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&textures[i].p,nullptr));D3DLOCKED_RECT lock{};api(textures[i]->LockRect(0,&lock,nullptr,0));for(UINT y=0;y<2;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,&texels[i][y*2],8);api(textures[i]->UnlockRect(0));}
        api(d->CreateCubeTexture(2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cube.p,nullptr));
        for(UINT face=0;face<6;++face){D3DLOCKED_RECT lock{};api(cube->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0));for(UINT y=0;y<2;++y)for(UINT x=0;x<2;++x){DWORD value=0xff102030+face*0x00050301;std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&value,4);}api(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}
    }
    ~Gpu(){for(UINT i=0;i<4;++i)d->SetTexture(i,nullptr);d->SetRenderTarget(1,nullptr);d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,back.p);d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetVertexDeclaration(nullptr);d->SetStreamSource(0,nullptr,0,0);}
    void state(const Configuration& c,bool variant){
        api(d->SetRenderTarget(1,nullptr));api(d->SetDepthStencilSurface(nullptr));api(d->SetRenderTarget(0,color.p));api(d->SetRenderTarget(1,variant?motion_surface.p:nullptr));api(d->SetDepthStencilSurface(depth.p));D3DVIEWPORT9 vp{0,0,w,h,0,1};api(d->SetViewport(&vp));
        for(auto s:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_DITHERENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_LIGHTING})api(d->SetRenderState(s,FALSE));
        for(auto s:{D3DRS_WRAP0,D3DRS_WRAP1,D3DRS_WRAP2,D3DRS_WRAP3,D3DRS_WRAP4})api(d->SetRenderState(s,0));
        api(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));api(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));api(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));api(d->SetRenderState(D3DRS_ZENABLE,TRUE));api(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));api(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        api(d->SetVertexShader(vs[variant].p));api(d->SetPixelShader(ps[variant].p));api(d->SetVertexDeclaration(declaration[c.packed].p));api(d->SetStreamSource(0,vb[c.packed].p,0,c.packed?24:32));
        for(UINT i=0;i<4;++i){api(d->SetTexture(i,i==3?static_cast<IDirect3DBaseTexture9*>(cube.p):textures[i].p));for(auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(d->SetSamplerState(i,s,D3DTEXF_POINT));api(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE));api(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));api(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));api(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));}
        float values[256][4]{};std::memcpy(values[24],identity,sizeof identity);values[24][3]=c.translation;
        if(c.perspective){values[27][0]=.125f;values[26][0]=.0625f;values[26][2]=0;values[26][3]=.5f;}
        for(UINT base:{28u,31u,34u})for(UINT i=0;i<3;++i)values[base+i][i]=1;
        values[36][3]=4;values[37][0]=1;values[38][1]=1;values[39][0]=.625f;values[40][0]=.2f;values[40][1]=.1f;values[40][2]=.05f;values[41][0]=1;
        for(UINT i=0;i<8;++i){values[3*i][0]=float(i%3)-1;values[3*i][1]=1;values[3*i][2]=3;values[3*i+1][0]=.04f;values[3*i+1][1]=.025f;values[3*i+1][2]=.015f;values[3*i+2][0]=1;values[3*i+2][1]=.05f;}
        std::memcpy(values[252],identity,sizeof identity);if(c.perspective)values[255][0]=.0625f;
        api(d->SetVertexShaderConstantF(0,values[0],256));int count[4]={int(c.lights),0,1,0};api(d->SetVertexShaderConstantI(0,count,1));BOOL fog=FALSE;api(d->SetVertexShaderConstantB(0,&fog,1));
        float pixel[218][4]{};pixel[0][0]=pixel[1][1]=pixel[2][2]=c.material_scale;pixel[3][0]=.25f;pixel[4][2]=1;pixel[5][0]=.3f;pixel[5][1]=.2f;pixel[5][2]=.1f;pixel[6][0]=.5f;pixel[6][2]=.5f;pixel[7][0]=.1f;pixel[7][1]=.15f;pixel[7][2]=.2f;
        pixel[216][0]=1.f/w;pixel[216][1]=1.f/h;pixel[216][2]=c.jx/w;pixel[216][3]=c.jy/h;pixel[217][0]=c.valid?1.f:0.f;api(d->SetPixelShaderConstantF(0,pixel[0],218));
    }
    void draw(){api(d->BeginScene());api(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1));api(d->EndScene());}
    std::vector<Pixel> read(IDirect3DSurface9* target,D3DFORMAT f){Com<IDirect3DSurface9> sys;api(d->CreateOffscreenPlainSurface(w,h,f,D3DPOOL_SYSTEMMEM,&sys.p,nullptr));api(d->GetRenderTargetData(target,sys.p));D3DLOCKED_RECT lock{};api(sys->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<Pixel> result(std::size_t(w)*h);
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto& p=result[std::size_t(y)*w+x];auto* ptr=static_cast<char*>(lock.pBits)+y*lock.Pitch+x*(f==D3DFMT_A8R8G8B8?4:16);if(f==D3DFMT_A8R8G8B8){DWORD value;std::memcpy(&value,ptr,4);p={{float(value>>16&255)/255,float(value>>8&255)/255,float(value&255)/255,float(value>>24)/255}};}else std::memcpy(&p,ptr,16);}
        api(sys->UnlockRect());return result;}
    std::vector<Pixel> render(const Configuration& c,bool variant){state(c,variant);api(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));draw();return read(color.p,format);}
    void compare(const std::vector<Pixel>& a,const std::vector<Pixel>& b,const char* label){unsigned mismatch=0,covered=0;float maximum=0;for(std::size_t i=0;i<a.size();++i){covered+=a[i].value[3]!=0;for(unsigned k=0;k<4;++k){++colors;float error=std::fabs(a[i].value[k]-b[i].value[k]);maximum=std::max(maximum,error);if(!std::isfinite(a[i].value[k])||!std::isfinite(b[i].value[k])||error!=0)++mismatch;}}
        std::printf("COLOR %s width=%u height=%u format=%u components=%zu covered=%u mismatches=%u maximum=%.9g\n",label,w,h,unsigned(format),a.size()*4,covered,mismatch,maximum);require(!mismatch&&covered>0,"all original color channels unchanged and nonempty");}
    void replay(const Configuration& c,bool scene_open=false){
        api(d->SetRenderTarget(1,nullptr));api(d->SetRenderTarget(0,motion_surface.p));api(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        api(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));api(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        api(d->SetVertexShader(replay_vs.p));api(d->SetPixelShader(replay_ps.p));float rows[32]{};std::memcpy(rows,identity,sizeof identity);std::memcpy(rows+16,identity,sizeof identity);rows[3]=c.translation;
        if(c.perspective){rows[12]=.125f;rows[8]=.0625f;rows[10]=0;rows[11]=.5f;rows[28]=.0625f;}
        api(d->SetVertexShaderConstantF(0,rows,8));float uv[4]={1.f/w,1.f/h,c.jx/w,c.jy/h},mode[4]={c.valid?1.f:0.f,0,0,0};api(d->SetPixelShaderConstantF(0,uv,1));api(d->SetPixelShaderConstantF(1,mode,1));if(scene_open)api(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1));else draw();
    }
    void sensitivity(){Configuration c;auto base=render(c,false);c.lights=8;auto lit=render(c,false);require(std::memcmp(base.data(),lit.data(),base.size()*sizeof(Pixel))!=0,"native point-light count changes original material");compare(lit,render(c,true),"light_sensitivity");c.lights=0;c.material_scale=.5f;auto tint=render(c,false);require(std::memcmp(base.data(),tint.data(),base.size()*sizeof(Pixel))!=0,"native material constant changes original material");compare(tint,render(c,true),"material_sensitivity");}
    void test(const Configuration& c){++configurations;std::printf("CONFIG id=%u width=%u height=%u format=%u packed=%u perspective=%u lights=%u valid=%u translation=%.6f jitter=%.6f,%.6f\n",configurations,w,h,unsigned(format),c.packed,c.perspective,c.lights,c.valid,c.translation,c.jx,c.jy);auto original=render(c,false);auto transformed=render(c,true);compare(original,transformed,"same_draw");auto output=read(motion_surface.p,D3DFMT_A32B32G32R32F);
        render(c,false);replay(c);auto reference=read(motion_surface.p,D3DFMT_A32B32G32R32F);unsigned reference_mismatches=0;float reference_max=0;for(std::size_t i=0;i<output.size();++i)for(unsigned k=0;k<4;++k){float error=std::fabs(output[i].value[k]-reference[i].value[k]);reference_max=std::max(reference_max,error);reference_mismatches+=!std::isfinite(error)||error>2e-6;}std::printf("REFERENCE config=%u components=%zu mismatches=%u max=%.9g\n",configurations,output.size()*4,reference_mismatches,reference_max);require(!reference_mismatches,"same-draw output matches independent authored replay");
        for(UINT y:{h/4,h/2,3*h/4})for(UINT x:{w/4,w/2,3*w/4}){double nx=2.*x/w-1,ny=1-2.*y/h;double object_x=c.perspective?(nx-c.translation)/(1-.125*nx):nx-c.translation;double current_w=1+(c.perspective?.125*object_x:0);double object_y=ny*current_w;double previous_w=1+(c.perspective?.0625*object_x:0);double expected[4]={.5*object_x/previous_w+.5+.5/w-c.jx/w,-.5*object_y/previous_w+.5+.5/h-c.jy/h,.5/previous_w,1};if(!c.valid){expected[0]=expected[1]=expected[2]=0;expected[3]=-1;}
            require(original[std::size_t(y)*w+x].value[3]>0,"motion sample is covered original geometry");for(UINT k=0;k<4;++k){float actual=output[std::size_t(y)*w+x].value[k];double error=std::fabs(actual-expected[k]);++numerics;double pixel_error=error*(k==0?w:k==1?h:1);bool okay=std::isfinite(actual)&&(k<2?pixel_error<=.005:error<=2e-6);std::printf("SAMPLE config=%u x=%u y=%u channel=%u actual=%.9g expected=%.9g error=%.9g pixel_error=%.9g %s\n",configurations,x,y,k,actual,expected[k],error,pixel_error,okay?"PASS":"FAIL");if(!okay)throw std::runtime_error("motion numeric");}}
        for(unsigned reverse=0;reverse<2;++reverse){render(c,reverse);state(c,!reverse);api(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));api(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_EQUAL));api(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));draw();compare(original,read(color.p,format),"bilateral_depth_equal");++depth_cases;}
        // Changed depth must fail EQUAL instead of satisfying equality vacuously.
        float row[4]={0,0,0,.8f};api(d->SetVertexShaderConstantF(26,row,1));api(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));draw();auto negative=read(color.p,format);require(std::all_of(negative.begin(),negative.end(),[](const Pixel& p){return p.value[3]==0;}),"changed depth rejects all tested fragments");
    }
    void wait(IDirect3DQuery9* event){BOOL done=FALSE;auto limit=GetTickCount()+5000;for(;;){HRESULT hr=event->GetData(&done,sizeof done,D3DGETDATA_FLUSH);if(hr==S_OK&&done)return;if(FAILED(hr)||LONG(GetTickCount()-limit)>=0)throw std::runtime_error("event completion");Sleep(0);}}
    void timing(){Com<IDirect3DQuery9> event;api(d->CreateQuery(D3DQUERYTYPE_EVENT,&event.p));Configuration c;c.packed=true;c.perspective=true;c.lights=8;LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
        auto submit=[&](unsigned mode){api(d->SetRenderTarget(1,mode==1?motion_surface.p:nullptr));api(d->SetVertexShader(vs[mode==1].p));api(d->SetPixelShader(ps[mode==1].p));api(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));api(d->BeginScene());api(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1));if(mode==2)replay(c,true);api(d->EndScene());};
        for(unsigned i=0;i<6;++i){state(c,false);submit(i%3);api(event->Issue(D3DISSUE_END));wait(event.p);}
        for(unsigned i=0;i<18;++i){unsigned mode=(i/3)%2?2-i%3:i%3;state(c,false);api(event->Issue(D3DISSUE_END));wait(event.p);LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);submit(mode);api(event->Issue(D3DISSUE_END));wait(event.p);QueryPerformanceCounter(&end);std::printf("TIMING width=%u height=%u format=%u iteration=%u mode=%u completed_ms=%.6f timed_readback=0 gpu_timestamp=0 scene_pairs=1 draws=%u\n",w,h,unsigned(format),i,mode,1000.*(end.QuadPart-begin.QuadPart)/frequency.QuadPart,mode==2?2:1);}

    }
};
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);int exit=1;WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="X3MaterialMotion";RegisterClassA(&cls);HWND window=CreateWindowA(cls.lpszClassName,"Detached same-draw material motion",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);HMODULE runtime=LoadLibraryA("d3d9.dll");
    try{if(argc!=3||!window||!runtime)throw std::runtime_error("shader pair paths required");auto v=load(argv[1]),p=load(argv[2]);MaterialMotionVariant variant;require(material_motion_variant(v.data(),v.size(),p.data(),p.size(),variant)==MaterialMotionResult::Applied,"exact local shader pair transformed");
        auto create=symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime,"Direct3DCreate9");Com<IDirect3D9> factory;factory.p=create(D3D_SDK_VERSION);if(!factory.p)throw std::runtime_error("factory");D3DCAPS9 caps{};api(factory->GetDeviceCaps(0,D3DDEVTYPE_HAL,&caps));std::printf("CAPS mrt=%lu misc=%08lx vs=%08lx ps=%08lx max_vs_const=%lu vs_slots=%lu ps_slots=%lu\n",caps.NumSimultaneousRTs,caps.PrimitiveMiscCaps,caps.VertexShaderVersion,caps.PixelShaderVersion,caps.MaxVertexShaderConst,caps.MaxVertexShader30InstructionSlots,caps.MaxPixelShader30InstructionSlots);
        require(caps.NumSimultaneousRTs>=2&&caps.MaxVertexShaderConst>=256,"MRT and high constant capacity");D3DDISPLAYMODE display{};api(factory->GetAdapterDisplayMode(0,&display));std::printf("ADAPTER_FORMAT %u\n",unsigned(display.Format));bool mixed=(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)!=0;
        for(auto f:{D3DFMT_A8R8G8B8,D3DFMT_A32B32G32R32F,D3DFMT_G16R16F})std::printf("FORMAT value=%u rt=%08lx postblend=%08lx depth_match=%08lx\n",unsigned(f),factory->CheckDeviceFormat(0,D3DDEVTYPE_HAL,display.Format,D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE,f),factory->CheckDeviceFormat(0,D3DDEVTYPE_HAL,display.Format,D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_TEXTURE,f),factory->CheckDepthStencilMatch(0,D3DDEVTYPE_HAL,display.Format,f,D3DFMT_D24X8));
        for(UINT pure=0;pure<2;++pure){std::printf("DEVICE pure=%u mixed=%u\n",pure,mixed);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;Com<IDirect3DDevice9> d;api(factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|(pure?D3DCREATE_PUREDEVICE:0),&pp,&d.p));
            for(const char* name:{"d3d9.dll","wined3d.dll"}){char path[MAX_PATH]{};HMODULE m=GetModuleHandleA(name);if(!m||!GetModuleFileNameA(m,path,MAX_PATH))throw std::runtime_error("native module identity");std::printf("MODULE name=%s path=%s\n",name,path);}
            for(auto type:{D3DQUERYTYPE_TIMESTAMP,D3DQUERYTYPE_TIMESTAMPFREQ,D3DQUERYTYPE_TIMESTAMPDISJOINT}){Com<IDirect3DQuery9> q;std::printf("QUERY type=%u result=%08lx\n",unsigned(type),d->CreateQuery(type,&q.p));}
            for(UINT generation=0;generation<2;++generation){for(auto f:{D3DFMT_A32B32G32R32F,D3DFMT_A8R8G8B8}){if(f==D3DFMT_A8R8G8B8&&!mixed){std::puts("MIXED unsupported_cap");continue;}Gpu gpu(d.p,v,p,variant,32,32,f);gpu.sensitivity();
                    for(bool packed:{false,true}){Configuration c;c.packed=packed;gpu.test(c);c.translation=.25f;c.lights=1;gpu.test(c);c.perspective=true;c.lights=8;gpu.test(c);c.jx=.25f;c.jy=-.375f;gpu.test(c);c.valid=false;gpu.test(c);}}
                if(!generation){api(d->Reset(&pp));std::puts("RESET PASS");}}
            if(!pure){for(auto size:{std::pair<UINT,UINT>{1280,768},{5120,1440}}){Gpu gpu(d.p,v,p,variant,size.first,size.second,mixed?D3DFMT_A8R8G8B8:D3DFMT_A32B32G32R32F);Configuration c;c.packed=true;c.perspective=true;c.translation=.25f;c.lights=8;c.jx=.25f;c.jy=-.375f;gpu.test(c);gpu.timing();}}
        }
        std::printf("RESULT PASS checks=%u numerical=%u color_components=%u depth_cases=%u configurations=%u devices=2\n",checks,numerics,colors,depth_cases,configurations);exit=0;
    }catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());}if(runtime)FreeLibrary(runtime);if(window)DestroyWindow(window);UnregisterClassA(cls.lpszClassName,cls.hInstance);return exit;
}
