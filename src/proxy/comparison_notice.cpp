#include "comparison_notice.h"
#include <algorithm>
#include <cstring>

namespace x3m {
namespace {
// Authored 5x7 glyphs, one low-five-bit row each. Only the notice's ASCII set.
const unsigned char* glyph(char c) noexcept {
    static constexpr unsigned char letters[][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    static constexpr unsigned char digits[][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    static constexpr unsigned char blank[7]{}, dot[7]{0,0,0,0,0,12,12},
        plus[7]{0,4,4,31,4,4,0}, minus[7]{0,0,0,31,0,0,0}, slash[7]{1,2,2,4,8,8,16};
    if (c >= 'A' && c <= 'Z') return letters[c-'A'];
    if (c >= '0' && c <= '9') return digits[c-'0'];
    if (c == '.') return dot;
    if (c == '+') return plus;
    if (c == '-') return minus;
    if (c == '/') return slash;
    return blank;
}
void first_failure(HRESULT& first, HRESULT value) noexcept { if (SUCCEEDED(first) && FAILED(value)) first = value; }
template<class T> void drop(T*& value) noexcept { T* old=value; value=nullptr; if(old)old->Release(); }
}

void ComparisonNotice::text(const char* first, const char* second) noexcept {
    char next[2][columns+1]{};
    std::strncpy(next[0], first, columns); std::strncpy(next[1], second, columns);
    if (!std::memcmp(next, lines_, sizeof next)) return;
    std::memcpy(lines_, next, sizeof next); count_=0; width_=0;
    for (unsigned line=0; line<2; ++line) {
        const unsigned length=unsigned(std::strlen(lines_[line]));
        width_=std::max(width_, length*12+16);
        for(unsigned col=0; col<length; ++col) {
            const auto* rows=glyph(lines_[line][col]);
            for(unsigned y=0;y<7;++y) for(unsigned x=0;x<5;++x) if(rows[y]&(16u>>x)) {
                const LONG left=24+LONG(col*12+x*2), top=24+LONG(line*20+y*2);
                pixels_[count_++]={left,top,left+2,top+2};
            }
        }
    }
}

ComparisonNoticeResult ComparisonNotice::draw(IDirect3DDevice9* device, void* const* native,
                                               unsigned target_count) noexcept {
    ComparisonNoticeResult result{};
    if (!device || !native || !count_ || !target_count || target_count>4) return result;
    using GetRT=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9**);
    using SetRT=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*);
    using GetDS=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9**);
    using SetDS=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*);
    using GetVP=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DVIEWPORT9*);
    using SetVP=HRESULT(WINAPI*)(IDirect3DDevice9*,const D3DVIEWPORT9*);
    using Backbuffer=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DBACKBUFFER_TYPE,IDirect3DSurface9**);
    using Clear=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD);
    const auto get_rt=reinterpret_cast<GetRT>(native[38]); const auto set_rt=reinterpret_cast<SetRT>(native[37]);
    const auto get_ds=reinterpret_cast<GetDS>(native[40]); const auto set_ds=reinterpret_cast<SetDS>(native[39]);
    const auto get_vp=reinterpret_cast<GetVP>(native[48]); const auto set_vp=reinterpret_cast<SetVP>(native[47]);
    IDirect3DSurface9* rt[4]{}, *depth=nullptr, *back=nullptr;
    D3DVIEWPORT9 viewport{}; D3DSURFACE_DESC desc{};
    unsigned attempted_rt=0; bool attempted_depth=false, attempted_viewport=false;
    auto release=[&] { for(auto& surface:rt)drop(surface); drop(depth); drop(back); };
    HRESULT hr=S_OK;
    // All reads and references precede the first mutation. NOTFOUND is only
    // a known null for optional bindings, never a blanket failed-read fallback.
    for(unsigned i=0;i<target_count;++i) {
        hr=get_rt(device,i,&rt[i]);
        if ((FAILED(hr) && !(i && hr==D3DERR_NOTFOUND && !rt[i])) || (!i && !rt[i])) {
            result.operation=FAILED(hr)?hr:E_FAIL; release(); return result;
        }
    }
    hr=get_ds(device,&depth);
    if (FAILED(hr) && !(hr==D3DERR_NOTFOUND && !depth)) {result.operation=hr;release();return result;}
    if (FAILED(hr=get_vp(device,&viewport)) ||
        FAILED(hr=reinterpret_cast<Backbuffer>(native[18])(device,0,0,D3DBACKBUFFER_TYPE_MONO,&back)) || !back ||
        FAILED(hr=back->GetDesc(&desc))) { result.operation=FAILED(hr)?hr:E_FAIL;release();return result; }
    if (!desc.Width || !desc.Height) {result.operation=E_FAIL;release();return result;}
    unsigned clipped_count=0;
    for(unsigned i=0;i<count_;++i) {
        auto rect=pixels_[i];
        rect.x2=std::min(rect.x2,LONG(desc.Width)); rect.y2=std::min(rect.y2,LONG(desc.Height));
        if(rect.x1<rect.x2 && rect.y1<rect.y2)clipped_[clipped_count++]=rect;
    }
    D3DRECT panel{16,16,std::min(LONG(16+width_),LONG(desc.Width)),std::min(LONG(64),LONG(desc.Height))};
    if(!clipped_count || panel.x2<=panel.x1 || panel.y2<=panel.y1) {release();return result;}
    attempted_depth=true; hr=set_ds(device,nullptr);
    // A failed setter can still mutate. Mark every attempt before the call;
    // any RT attempt can reset the viewport, including a reported failure.
    for(unsigned i=1;i<target_count && SUCCEEDED(hr);++i) {
        attempted_rt|=1u<<i; attempted_viewport=true; hr=set_rt(device,i,nullptr);
    }
    if(SUCCEEDED(hr)) { attempted_rt|=1; attempted_viewport=true; hr=set_rt(device,0,back); }
    const D3DVIEWPORT9 full{0,0,desc.Width,desc.Height,0.f,1.f};
    if(SUCCEEDED(hr)) {attempted_viewport=true;hr=set_vp(device,&full);}
    const auto clear=reinterpret_cast<Clear>(native[43]);
    if(SUCCEEDED(hr))hr=clear(device,1,&panel,D3DCLEAR_TARGET,0xff101820u,1.f,0);
    if(SUCCEEDED(hr))hr=clear(device,clipped_count,clipped_,D3DCLEAR_TARGET,0xffe8f0f8u,1.f,0);
    result.operation=hr; result.drawn=SUCCEEDED(hr);
    // Restore RT0 first, optional MRTs next, compatible original depth, then
    // viewport last (SetRenderTarget itself changes the viewport).
    for(unsigned i=0;i<target_count;++i) if(attempted_rt&(1u<<i)) first_failure(result.restore,set_rt(device,i,rt[i]));
    if(attempted_depth)first_failure(result.restore,set_ds(device,depth));
    if(attempted_viewport)first_failure(result.restore,set_vp(device,&viewport));
    release(); return result;
}
} // namespace x3m
