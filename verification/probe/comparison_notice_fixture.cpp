// Scripted public D3D9 API outcomes; production notice source is linked intact.
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include "../../src/proxy/comparison_notice.h"
static unsigned checks=0,failures=0,allocations=0;
void* operator new(std::size_t size){++allocations;if(void*p=std::malloc(size))return p;throw std::bad_alloc();}
void operator delete(void*p) noexcept{std::free(p);}
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line=%u %s\n",__LINE__,#x);}}while(0)
struct Device:IDirect3DDevice9 {
    IDirect3DSurface9 surfaces[6];
    IDirect3DSurface9* rt[4]{&surfaces[0],&surfaces[1],&surfaces[2],&surfaces[3]};
    IDirect3DSurface9* depth=&surfaces[4];
    D3DVIEWPORT9 viewport{7,11,640,480,.2f,.8f};
    unsigned gets=0,sets=0,clears=0,fail_get=0,fail_clear=0;
    std::array<unsigned,2> fail_set{};
    bool mutate_failure=true;
    Device(){for(auto& s:surfaces){s.desc.Width=800;s.desc.Height=600;}}
    HRESULT read(){return ++gets==fail_get?-100-HRESULT(gets):S_OK;}
    HRESULT write(){++sets;for(auto n:fail_set)if(n==sets)return -200-HRESULT(sets);return S_OK;}
    bool restored() const {
        const D3DVIEWPORT9 original{7,11,640,480,.2f,.8f};
        return rt[0]==&surfaces[0]&&rt[1]==&surfaces[1]&&rt[2]==&surfaces[2]&&rt[3]==&surfaces[3]
            &&depth==&surfaces[4]&&!std::memcmp(&viewport,&original,sizeof original);
    }
    void balanced(){for(const auto& s:surfaces)CHECK(s.refs==1);}
};
HRESULT get_rt(IDirect3DDevice9* raw,DWORD i,IDirect3DSurface9** value){auto& d=*static_cast<Device*>(raw);const auto hr=d.read();*value=d.rt[i];if(*value)(*value)->AddRef();return FAILED(hr)?hr:*value?S_OK:D3DERR_NOTFOUND;}
HRESULT get_ds(IDirect3DDevice9* raw,IDirect3DSurface9** value){auto& d=*static_cast<Device*>(raw);const auto hr=d.read();*value=d.depth;if(*value)(*value)->AddRef();return FAILED(hr)?hr:*value?S_OK:D3DERR_NOTFOUND;}
HRESULT get_vp(IDirect3DDevice9* raw,D3DVIEWPORT9* value){auto& d=*static_cast<Device*>(raw);*value=d.viewport;return d.read();}
HRESULT get_back(IDirect3DDevice9* raw,UINT,UINT,D3DBACKBUFFER_TYPE,IDirect3DSurface9** value){auto& d=*static_cast<Device*>(raw);*value=&d.surfaces[5];(*value)->AddRef();return d.read();}
HRESULT set_rt(IDirect3DDevice9* raw,DWORD i,IDirect3DSurface9* value){auto& d=*static_cast<Device*>(raw);const auto hr=d.write();if(SUCCEEDED(hr)||d.mutate_failure){d.rt[i]=value;d.viewport={0,0,800,600,0,1};}return hr;}
HRESULT set_ds(IDirect3DDevice9* raw,IDirect3DSurface9* value){auto& d=*static_cast<Device*>(raw);const auto hr=d.write();if(SUCCEEDED(hr)||d.mutate_failure)d.depth=value;return hr;}
HRESULT set_vp(IDirect3DDevice9* raw,const D3DVIEWPORT9* value){auto& d=*static_cast<Device*>(raw);const auto hr=d.write();if(SUCCEEDED(hr)||d.mutate_failure)d.viewport=*value;return hr;}
HRESULT clear(IDirect3DDevice9* raw,DWORD n,const D3DRECT* rects,DWORD flags,D3DCOLOR,float,DWORD){
    auto& d=*static_cast<Device*>(raw);++d.clears;
    CHECK(d.rt[0]==&d.surfaces[5]&&!d.rt[1]&&!d.rt[2]&&!d.rt[3]&&!d.depth);
    CHECK(flags==D3DCLEAR_TARGET&&n>0&&rects);
    for(unsigned i=0;i<n;++i)CHECK(rects[i].x1>=0&&rects[i].y1>=0&&rects[i].x2<=LONG(d.surfaces[5].desc.Width)&&rects[i].y2<=LONG(d.surfaces[5].desc.Height)&&rects[i].x1<rects[i].x2&&rects[i].y1<rects[i].y2);
    return d.clears==d.fail_clear?-300-HRESULT(d.clears):S_OK;
}
int main(){
    void* native[49]{};native[38]=reinterpret_cast<void*>(&get_rt);native[37]=reinterpret_cast<void*>(&set_rt);
    native[40]=reinterpret_cast<void*>(&get_ds);native[39]=reinterpret_cast<void*>(&set_ds);
    native[48]=reinterpret_cast<void*>(&get_vp);native[47]=reinterpret_cast<void*>(&set_vp);
    native[18]=reinterpret_cast<void*>(&get_back);native[43]=reinterpret_cast<void*>(&clear);
    x3m::ComparisonNotice notice;notice.text("EXPOSURE AUTO","BLOOM OFF REQUESTED");
    const auto before_allocations=allocations;
    for(unsigned i=0;i<100000;++i)CHECK(!notice.visible(i));
    notice.show(100);CHECK(notice.visible(100)&&notice.visible(3099)&&!notice.visible(3100));
    notice.hide();CHECK(!notice.visible(101));
    {Device d;const auto r=notice.draw(&d,native,4);CHECK(r.drawn&&r.operation==S_OK&&r.restore==S_OK);CHECK(d.gets==7&&d.sets==12&&d.clears==2);CHECK(d.restored());d.balanced();}
    for(unsigned failed=1;failed<=7;++failed){Device d;d.fail_get=failed;const auto r=notice.draw(&d,native,4);CHECK(FAILED(r.operation)&&r.restore==S_OK&&!r.drawn);CHECK(!d.sets&&!d.clears&&d.restored());d.balanced();}
    {Device d;d.surfaces[5].desc_hr=E_FAIL;const auto r=notice.draw(&d,native,4);CHECK(r.operation==E_FAIL&&!d.sets&&!d.clears);d.balanced();}
    for(bool mutates:{false,true})for(unsigned failed=1;failed<=12;++failed){
        Device d;d.fail_set[0]=failed;d.mutate_failure=mutates;const auto r=notice.draw(&d,native,4);
        if(failed<=6){CHECK(FAILED(r.operation)&&r.restore==S_OK&&!d.clears);CHECK(d.restored());}
        else{CHECK(r.operation==S_OK&&r.restore==-200-HRESULT(failed));CHECK(d.sets==12);}
        d.balanced();
    }
    {Device d;d.fail_set={7,8};const auto r=notice.draw(&d,native,4);CHECK(r.restore==-207);CHECK(d.sets==12);d.balanced();}
    for(unsigned failed=1;failed<=2;++failed){Device d;d.fail_clear=failed;const auto r=notice.draw(&d,native,4);CHECK(r.operation==-300-HRESULT(failed)&&r.restore==S_OK&&!r.drawn);CHECK(d.restored());d.balanced();}
    {Device d;d.rt[1]=d.rt[2]=d.rt[3]=nullptr;d.depth=nullptr;const auto r=notice.draw(&d,native,4);CHECK(r.drawn&&!d.rt[1]&&!d.rt[2]&&!d.rt[3]&&!d.depth);d.balanced();}
    for(unsigned size:{0u,10u,26u,40u,120u}){Device d;d.surfaces[5].desc.Width=d.surfaces[5].desc.Height=size;notice.draw(&d,native,4);CHECK(d.restored());d.balanced();}
    for(unsigned count:{0u,5u}){Device d;notice.draw(&d,native,count);CHECK(!d.gets&&!d.sets&&!d.clears);}
    CHECK(allocations==before_allocations);
    std::printf("comparison_notice checks=%u failures=%u allocations=%u\n",checks,failures,allocations-before_allocations);
    return failures?1:0;
}
