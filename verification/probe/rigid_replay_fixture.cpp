// Original synthetic inputs only. GPU readback here is verification, never production.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/rigid_replay_program.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::renderer;
template<class T> struct Com { T* p=nullptr; ~Com(){if(p)p->Release();} T* operator->()const{return p;} Com()=default; Com(const Com&)=delete; Com& operator=(const Com&)=delete; };
void api(HRESULT hr,const char* label){if(FAILED(hr)){std::printf("API FAIL %s %08lx\n",label,hr);throw std::runtime_error(label);}}
unsigned checks=0; void require(bool v,const char* s){++checks;if(!v)throw std::runtime_error(s);}
template<class T>T symbol(HMODULE m,const char* s){auto p=GetProcAddress(m,s);T v=nullptr;std::memcpy(&v,&p,sizeof v);if(!v)throw std::runtime_error(s);return v;}
using Assembler=decltype(&D3DXAssembleShader);
std::vector<DWORD> assemble(Assembler a,const std::string& text){Com<ID3DXBuffer> code,error;HRESULT hr=a(text.c_str(),UINT(text.size()),nullptr,nullptr,0,&code.p,&error.p);if(error.p)std::printf("ASSEMBLER %s\n",static_cast<char*>(error->GetBufferPointer()));api(hr,"assemble");auto p=static_cast<DWORD*>(code->GetBufferPointer());return {p,p+code->GetBufferSize()/4};}
std::vector<DWORD> strip_comments(const std::vector<DWORD>& p){std::vector<DWORD> out{p.at(0)};for(size_t i=1;i<p.size();){DWORD op=p[i]&0xffff;size_t n=op==0xfffe?1+((p[i]>>16)&0x7fff):op==0xffff?1:1+((p[i]>>24)&15);require(i+n<=p.size(),"truncated reference instruction");if(op!=0xfffe)out.insert(out.end(),p.begin()+i,p.begin()+i+n);i+=n;}return out;}
// Separate textual assembly pins readable operand order, masks, local constants.
std::string reference(bool instrumented){
    std::string s="vs_3_0\ndef c20, 1, 0, 0, 0\ndcl_position v0\n";
    if(instrumented)s+="dcl_position1 v1\ndcl_position o0\ndcl_texcoord o1\ndcl_texcoord1 o2\n";
    else s+="dcl_position o0\ndcl_texcoord o1\n";
    s+="mad r3, v0.xyzx, c20.xxxy, c20.yyyx\n";
    for(unsigned group=0;group<2;++group)for(unsigned lane=0;lane<4;++lane){char line[96];std::snprintf(line,sizeof line,"dp4 o%u.%c, r3, c%u\n",group?1:instrumented?2:0,"xyzw"[lane],24+4*group+lane);s+=line;}
    if(instrumented)s+="mov o0, v1\n";
    return s;
}
void structural(Assembler assembler){
    const auto& p=rigid_replay_program();
    std::string exact="vs_3_0\ndef c8, 1, 0, 0, 0\ndcl_position v0\ndcl_position o0\ndcl_texcoord o1\nmad r0, v0.xyzx, c8.xxxy, c8.yyyx\n";
    for(unsigned i=0;i<8;++i){char line[80];std::snprintf(line,sizeof line,"dp4 o%u.%c, r0, c%u\n",i/4,"xyzw"[i%4],i);exact+=line;}
    auto a=strip_comments(assemble(assembler,exact));require(a.size()==p.size()&&std::equal(a.begin(),a.end(),p.begin()),"independent assembler token equality");
    // Decode using D3D9 SDK masks, independently of production token helpers.
    require(p[0]==D3DVS_VERSION(3,0)&&p.back()==D3DSIO_END,"version/end");
    unsigned mad=0,dot=0,definitions=0,declarations=0;
    for(size_t i=1;i+1<p.size();){DWORD op=p[i]&D3DSI_OPCODE_MASK;size_t n=(p[i]&D3DSI_INSTLENGTH_MASK)>>D3DSI_INSTLENGTH_SHIFT;require(i+n<p.size(),"instruction bounds");
        if(op==D3DSIO_DEF){require(n==5&&p[i+1]==0xa00f0008&&p[i+2]==0x3f800000&&p[i+3]==0&&p[i+4]==0&&p[i+5]==0,"literal bits");++definitions;}
        else if(op==D3DSIO_DCL){require(n==2,"declaration length");++declarations;}
        else if(op==D3DSIO_MAD){require(n==4&&p[i+1]==0x800f0000&&p[i+2]==0x90240000&&p[i+3]==0xa0400008&&p[i+4]==0xa0150008,"constructor modifiers and swizzles");++mad;}
        else if(op==D3DSIO_DP4){require(n==3&&p[i+2]==0x80e40000&&p[i+3]==(0xa0e40000|dot),"temp before row");require((p[i+1]&D3DSP_WRITEMASK_ALL)==(1u<<(16+dot%4))&&(p[i+1]&D3DSP_REGNUM_MASK)==dot/4,"output lane order");++dot;}
        else require(false,"unexpected instruction");
        i+=n+1;
    }
    require(mad==1&&dot==8&&definitions==1&&declarations==3,"instruction inventory");
    require(find_rigid_replay_profile(p.data(),p.size())==nullptr,"original synthetic not archive qualification");
    require(find_rigid_replay_profile(nullptr,0)==nullptr,"unknown lookup");
}
std::vector<DWORD> instrument(){
    const auto& original=rigid_replay_program();std::vector<DWORD> p{original.begin(),original.end()-1};
    // Only route computed current POSITION to TEXCOORD1, add an independent
    // raster-position stream, and append MOV. MAD/DP4 arithmetic tokens unchanged.
    for(size_t i=1;i<p.size();){DWORD op=p[i]&0xffff;size_t n=(p[i]>>24)&15;
        if(op==D3DSIO_DCL&&p[i+2]==0xe00f0000){p[i+1]=0x80010005;p[i+2]=0xe00f0002;}
        if(op==D3DSIO_DP4&&(p[i+1]&0x7ff)==0)p[i+1]|=2;
        i+=n+1;
    }
    const DWORD declarations[]={0x0200001f,0x80010000,0x900f0001,0x0200001f,0x80000000,0xe00f0000};
    p.insert(p.begin()+16,std::begin(declarations),std::end(declarations));
    p.insert(p.end(),{0x02000001,0xe00f0000,0x90e40001,0x0000ffff});return p;
}
using Pixel=std::array<DWORD,4>;
struct Fixture{
    IDirect3DDevice9* d;UINT width,height;
    Com<IDirect3DSurface9> targets[3],readback,depth;
    Com<IDirect3DVertexShader9> generated,baseline,sweepGenerated,sweepBaseline;
    Com<IDirect3DPixelShader9> sweepPS,solidPS;
    Com<IDirect3DVertexDeclaration9> halfDecl,floatDecl,halfSweep,floatSweep;
    Fixture(IDirect3DDevice9* device,Assembler a,UINT w,UINT h):d(device),width(w),height(h){
        for(auto& t:targets)api(d->CreateRenderTarget(w,h,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&t.p,nullptr),"target");
        api(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&readback.p,nullptr),"readback");
        api(d->CreateDepthStencilSurface(w,h,D3DFMT_D24X8,D3DMULTISAMPLE_NONE,0,FALSE,&depth.p,nullptr),"depth");
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(rigid_replay_program().data()),&generated.p),"production generated VS");
        auto r=assemble(a,reference(false));api(d->CreateVertexShader(r.data(),&baseline.p),"reference VS");
        r=instrument();api(d->CreateVertexShader(r.data(),&sweepGenerated.p),"instrumented production VS");
        r=assemble(a,reference(true));api(d->CreateVertexShader(r.data(),&sweepBaseline.p),"instrumented reference VS");
        r=assemble(a,"ps_3_0\ndef c0, 1, 1, 1, 1\ndcl_texcoord v0\ndcl_texcoord1 v1\nmov oC0, v1\nmov oC1, v0\nmov oC2, c0\n");api(d->CreatePixelShader(r.data(),&sweepPS.p),"arithmetic PS");
        r=assemble(a,"ps_3_0\nmov oC0, c0\n");api(d->CreatePixelShader(r.data(),&solidPS.p),"solid PS");
        D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        api(d->CreateVertexDeclaration(elements,&floatDecl.p),"FLOAT3 conversion");elements[0].Type=D3DDECLTYPE_FLOAT16_4;api(d->CreateVertexDeclaration(elements,&halfDecl.p),"FLOAT16_4 conversion");
        D3DVERTEXELEMENT9 sweep[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,1},D3DDECL_END()};
        api(d->CreateVertexDeclaration(sweep,&floatSweep.p),"float sweep declaration");sweep[0].Type=D3DDECLTYPE_FLOAT16_4;sweep[1].Offset=8;api(d->CreateVertexDeclaration(sweep,&halfSweep.p),"half sweep declaration");
        for(auto s:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_LIGHTING,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_FOGENABLE,FALSE},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_COLORWRITEENABLE1,15},{D3DRS_COLORWRITEENABLE2,15},{D3DRS_POINTSIZE,0x3f800000},{D3DRS_POINTSPRITEENABLE,FALSE},{D3DRS_POINTSCALEENABLE,FALSE}})api(d->SetRenderState(s.first,s.second),"normalize state");
    }
    ~Fixture(){d->SetRenderTarget(2,nullptr);d->SetRenderTarget(1,nullptr);d->SetDepthStencilSurface(nullptr);d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetVertexDeclaration(nullptr);}
    void constants(const float* rows){api(d->SetVertexShaderConstantF(0,rows,8),"candidate rows");api(d->SetVertexShaderConstantF(24,rows,8),"reference rows");const float hostile[]={19,23,29,31};api(d->SetVertexShaderConstantF(8,hostile,1),"overwrite local c8");api(d->SetVertexShaderConstantF(20,hostile,1),"overwrite reference c20");}
    void setup(bool sweep){api(d->SetDepthStencilSurface(sweep?nullptr:depth.p),"depth binding");api(d->SetRenderTarget(0,targets[0].p),"RT0");api(d->SetRenderTarget(1,sweep?targets[1].p:nullptr),"RT1");api(d->SetRenderTarget(2,sweep?targets[2].p:nullptr),"RT2 coverage marker");D3DVIEWPORT9 viewport{0,0,width,height,0,1};api(d->SetViewport(&viewport),"viewport");api(d->SetPixelShader(sweep?sweepPS.p:solidPS.p),"PS");api(d->SetRenderState(D3DRS_ZENABLE,!sweep),"depth enable");}
    std::vector<Pixel> read(unsigned target){api(d->GetRenderTargetData(targets[target].p,readback.p),"test-only GPU readback");D3DLOCKED_RECT r{};api(readback->LockRect(&r,nullptr,D3DLOCK_READONLY),"read lock");std::vector<Pixel> out(width*height);for(UINT y=0;y<height;++y)std::memcpy(out.data()+y*width,static_cast<char*>(r.pBits)+y*r.Pitch,width*sizeof(Pixel));api(readback->UnlockRect(),"read unlock");return out;}
    std::array<std::vector<Pixel>,2> sweep(bool candidate,bool half,const void* vertices,UINT n){setup(true);api(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff123456,1,0),"sweep clear");api(d->SetVertexDeclaration(half?halfSweep.p:floatSweep.p),"sweep decl");api(d->SetVertexShader(candidate?sweepGenerated.p:sweepBaseline.p),"sweep VS");api(d->BeginScene(),"begin sweep");api(d->DrawPrimitiveUP(D3DPT_POINTLIST,n,vertices,half?24:28),"native conversion/arithmetic sweep");api(d->EndScene(),"end sweep");auto marker=read(2);for(UINT i=0;i<n;++i)require(marker[i]==Pixel{0x3f800000,0x3f800000,0x3f800000,0x3f800000},"every arithmetic point writes coverage marker");return {read(0),read(1)};}
    std::vector<Pixel> raster(bool candidate,bool half,const void* vertices,bool equality=false){setup(false);api(d->SetRenderState(D3DRS_ZWRITEENABLE,!equality),"depth writes");api(d->SetRenderState(D3DRS_ZFUNC,equality?D3DCMP_EQUAL:D3DCMP_ALWAYS),"depth function");api(d->Clear(0,nullptr,D3DCLEAR_TARGET|(equality?0:D3DCLEAR_ZBUFFER),0,1,0),"raster clear");const float value[]={1,.5f,.25f,1};api(d->SetPixelShaderConstantF(0,value,1),"raster color");api(d->SetVertexDeclaration(half?halfDecl.p:floatDecl.p),"raster decl");api(d->SetVertexShader(candidate?generated.p:baseline.p),"raster VS");api(d->BeginScene(),"begin raster");api(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,half?8:12),"actual raster/depth");api(d->EndScene(),"end raster");return read(0);}
};
bool nanbits(DWORD p){return (p&0x7f800000)==0x7f800000&&(p&0x7fffff);}
struct Counts{unsigned long long components=0,nans=0,mismatch=0,raster=0,covered=0,empty=0;};
void compare(const std::array<std::vector<Pixel>,2>& a,const std::array<std::vector<Pixel>,2>& b,UINT n,Counts& count){for(unsigned target=0;target<2;++target)for(UINT i=0;i<n;++i)for(unsigned lane=0;lane<4;++lane){++count.components;DWORD x=a[target][i][lane],y=b[target][i][lane];if(nanbits(x)&&nanbits(y)){++count.nans;continue;}if(x!=y){if(count.mismatch<8)std::printf("ARITHMETIC MISMATCH target=%u index=%u lane=%u reference=%08lx generated=%08lx\n",target,i,lane,x,y);++count.mismatch;}}}
const float mixed[32]={.75f,.125f,-.0625f,.125f,-.125f,.625f,.0625f,-.0625f,.03125f,-.015625f,.5f,.25f,.03125f,.015625f,.0625f,1, .5f,.25f,.125f,-.125f,-.25f,.75f,.125f,.125f,.0625f,.03125f,.5f,.125f,.015625f,-.03125f,.125f,1};
const DWORD edges[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x3eaaaaab,0xbeaaaaab,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7f800001,0xff800001,0x7fc00000,0xffc00000,0x7fffffff,0xffffffff};
void run_sweeps(Fixture& f,Counts& count){f.constants(mixed);
    struct HalfVertex{unsigned short value[4];float clip[4];};static_assert(sizeof(HalfVertex)==24);
    std::vector<HalfVertex> v(65536);for(unsigned lane=0;lane<3;++lane){for(unsigned i=0;i<65536;++i){v[i]={{0x3400,0x3800,0x3a00,static_cast<unsigned short>(i^0xaaaa)},{float(i%256)/128-1,1-float(i/256)/128,.5f,1}};v[i].value[lane]=static_cast<unsigned short>(i);}auto ref=f.sweep(false,true,v.data(),UINT(v.size()));auto replay=f.sweep(true,true,v.data(),UINT(v.size()));compare(ref,replay,65536,count);
        float input[]={.25f,.5f,.75f,1};input[lane]=.25f;
        for(unsigned target=0;target<2;++target)for(unsigned output=0;output<4;++output){const float* row=mixed+16*target+4*output;float expected=input[0]*row[0]+input[1]*row[1]+input[2]*row[2]+row[3];DWORD bits;std::memcpy(&bits,&expected,4);require(replay[target][0x3400][output]==bits,"independent finite native-half arithmetic anchor");}
        for(unsigned encoding:{0x0000u,0x8000u,0x0001u,0x7c00u,0x7e00u}){auto p=ref[0][encoding];std::printf("HALF_OBSERVATION lane=%u bits=%04x current=%08lx,%08lx,%08lx,%08lx\n",lane,encoding,p[0],p[1],p[2],p[3]);}
        std::printf("HALF_SWEEP lane=%u encodings=65536 cumulative_mismatches=%llu\n",lane,count.mismatch);}
    struct FloatVertex{DWORD value[3];float clip[4];};static_assert(sizeof(FloatVertex)==28);
    std::vector<FloatVertex> floats;for(unsigned lane=0;lane<3;++lane)for(DWORD bits:edges){unsigned i=unsigned(floats.size());FloatVertex vtx{{0x3e800000,0x3f000000,0x3f400000},{float(i)/128-1,1,.5f,1}};vtx.value[lane]=bits;floats.push_back(vtx);}
    compare(f.sweep(false,false,floats.data(),UINT(floats.size())),f.sweep(true,false,floats.data(),UINT(floats.size())),UINT(floats.size()),count);
    std::printf("FLOAT_SWEEP vertices=%u cumulative_mismatches=%llu\n",unsigned(floats.size()),count.mismatch);
    // Positive finite anchors prove point coverage and both output streams; clear
    // values cannot satisfy these nontrivial matrix results.
    const auto finite=f.sweep(true,false,floats.data(),UINT(floats.size()));float expected=.125f+.125f*.5f-.0625f*.75f;DWORD bits;std::memcpy(&bits,&expected,4);require(finite[0][0][0]==bits,"finite arithmetic anchor");
}
void run_raster(Fixture& f,Counts& count){f.constants(mixed);
    const unsigned short halves[]={0,0x8000,1,0x8001,0x3ff,0x83ff,0x400,0x8400,0x3555,0xb555,0x3c00,0xbc00,0x7bff,0xfbff,0x7c00,0xfc00,0x7c01,0xfc01,0x7e00,0xfe00,0x7fff,0xffff};
    auto one=[&](bool half,const void* data){auto ref=f.raster(false,half,data);auto equal=f.raster(true,half,data,true);auto generated=f.raster(true,half,data);auto reverse=f.raster(false,half,data,true);++count.raster;unsigned coverage=0;for(auto p:ref)coverage+=p[0]==0x3f800000;if(coverage)++count.covered;else ++count.empty;if(ref!=equal||ref!=generated||generated!=reverse){++count.mismatch;std::printf("RASTER MISMATCH case=%llu half=%d coverage=%u\n",count.raster,half,coverage);}};
    float ordinary[3][3]={{-1,-1,.333251953125f},{1,-1,.333251953125f},{0,1,.333251953125f}};
    one(false,ordinary);unsigned short ordinaryHalf[3][4]={{0xbc00,0xbc00,0x3555,0x4700},{0x3c00,0xbc00,0x3555,0x7e00},{0,0x3c00,0x3555,0x7c00}};one(true,ordinaryHalf);
    for(unsigned lane=0;lane<3;++lane){for(DWORD bits:edges){float values[3][3];std::memcpy(values,ordinary,sizeof values);std::memcpy(&values[0][lane],&bits,4);one(false,values);}for(unsigned short bits:halves){unsigned short values[3][4];std::memcpy(values,ordinaryHalf,sizeof values);values[0][lane]=bits;one(true,values);}}
    require(count.covered>20&&count.empty>0,"raster nonempty and clipped controls");
    // Deliberate current Z-row perturbation must fail native EQUAL, proving the
    // parity test actually depends on the preserved depth instead of coverage only.
    auto ref=f.raster(false,false,ordinary);float altered[32];std::copy(mixed,mixed+32,altered);altered[11]+=.125f;api(f.d->SetVertexShaderConstantF(0,altered,8),"negative depth control");auto wrong=f.raster(true,false,ordinary,true);require(ref!=wrong,"depth mismatch detector distinguishes wrong rows");f.constants(mixed);
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);int result=1;WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="X3OriginalReplay";RegisterClassA(&wc);HWND window=CreateWindowA(wc.lpszClassName,"Original replay parity",WS_OVERLAPPEDWINDOW,90,90,128,128,nullptr,nullptr,wc.hInstance,nullptr);
    try{if(argc!=2||!window)throw std::runtime_error("D3DX path required");HMODULE runtime=LoadLibraryA("d3d9.dll"),dx=LoadLibraryA(argv[1]);if(!runtime||!dx)throw std::runtime_error("runtime library");auto assembler=symbol<Assembler>(dx,"D3DXAssembleShader");structural(assembler);auto create=symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime,"Direct3DCreate9");Com<IDirect3D9> api9;api9.p=create(D3D_SDK_VERSION);require(api9.p!=nullptr,"Create9");D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=256;pp.BackBufferHeight=256;pp.BackBufferFormat=D3DFMT_A8R8G8B8;Com<IDirect3DDevice9> d;api(api9->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_PUREDEVICE,&pp,&d.p),"CreateDevice");Counts count;{Fixture f(d.p,assembler,256,256);run_sweeps(f,count);} {Fixture f(d.p,assembler,32,32);run_raster(f,count);}require(count.mismatch==0,"unknown: observed replay mismatch");std::printf("RESULT PASS checks=%u arithmetic_components=%llu nan_pairs=%llu raster_cases=%llu covered=%llu empty=%llu mismatches=%llu finite_gate_retained=1\n",checks,count.components,count.nans,count.raster,count.covered,count.empty,count.mismatch);result=0;
    }catch(const std::exception& e){std::printf("RESULT FAIL %s\n",e.what());}if(window)DestroyWindow(window);UnregisterClassA(wc.lpszClassName,wc.hInstance);return result;}
