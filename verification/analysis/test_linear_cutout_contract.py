"""Host checks for the selected cutout capability and coverage contract.

The production header and selected MotionOutput methods are compiled unchanged
against a small scripted public-D3D9 adapter.  This does not execute a GPU or
qualify native-Windows behavior.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from verification.analysis.test_capture_bloom_lifetime import extract_function


ROOT = Path(__file__).resolve().parents[2]


PREFIX = r'''
#include <array>
#include <cstdint>
#include <cstdio>
#include "src/proxy/linear_cutout.h"

#define WINAPI
#define SUCCEEDED(value) ((value) >= 0)
#define FAILED(value) ((value) < 0)
using DWORD = std::uint32_t;
using UINT = std::uint32_t;
using ULONG = std::uint32_t;
using HRESULT = std::int32_t;
using D3DFORMAT = DWORD;
using D3DDEVTYPE = DWORD;
using D3DRESOURCETYPE = DWORD;
using D3DRENDERSTATETYPE = DWORD;
using D3DSAMPLERSTATETYPE = DWORD;
constexpr HRESULT S_OK = 0, S_FALSE = 1;
constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005u);
constexpr HRESULT E_OUTOFMEMORY = static_cast<HRESULT>(0x8007000eu);
constexpr HRESULT D3DERR_NOTAVAILABLE = static_cast<HRESULT>(0x8876086au);
constexpr DWORD D3DPMISCCAPS_INDEPENDENTWRITEMASKS = 0x00004000u;
constexpr DWORD D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS = 0x00040000u;
constexpr DWORD D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING = 0x00080000u;
constexpr DWORD D3DPCMPCAPS_GREATEREQUAL = 0x00000040u;
constexpr DWORD D3DUSAGE_RENDERTARGET = 0x00000001u;
constexpr DWORD D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING = 0x00080000u;
constexpr D3DRESOURCETYPE D3DRTYPE_TEXTURE = 3;
constexpr D3DFORMAT D3DFMT_A16B16G16R16F = 113;
constexpr D3DFORMAT D3DFMT_R32F = 114;
constexpr D3DFORMAT D3DFMT_A32B32G32R32F = 116;
constexpr DWORD D3DCMP_NEVER = 1;

constexpr D3DRENDERSTATETYPE D3DRS_ZENABLE = 7, D3DRS_FILLMODE = 8,
    D3DRS_ZWRITEENABLE = 14, D3DRS_ALPHATESTENABLE = 15,
    D3DRS_SRCBLEND = 19, D3DRS_DESTBLEND = 20, D3DRS_CULLMODE = 22,
    D3DRS_ZFUNC = 23, D3DRS_ALPHAREF = 24,
    D3DRS_ALPHAFUNC = 25, D3DRS_DITHERENABLE = 26,
    D3DRS_ALPHABLENDENABLE = 27, D3DRS_FOGENABLE = 28,
    D3DRS_STENCILENABLE = 52, D3DRS_WRAP0 = 128, D3DRS_WRAP1 = 129,
    D3DRS_WRAP2 = 130, D3DRS_WRAP3 = 131, D3DRS_WRAP4 = 132,
    D3DRS_WRAP5 = 133, D3DRS_WRAP6 = 134, D3DRS_WRAP7 = 135,
    D3DRS_COLORWRITEENABLE = 168, D3DRS_COLORWRITEENABLE1 = 190,
    D3DRS_BLENDOP = 171, D3DRS_COLORWRITEENABLE2 = 191, D3DRS_SRGBWRITEENABLE = 194,
    D3DRS_WRAP8 = 198, D3DRS_WRAP9 = 199, D3DRS_WRAP10 = 200,
    D3DRS_WRAP11 = 201, D3DRS_WRAP12 = 202, D3DRS_WRAP13 = 203,
    D3DRS_WRAP14 = 204, D3DRS_WRAP15 = 205, D3DRS_SEPARATEALPHABLENDENABLE = 206,
    D3DRS_SRCBLENDALPHA = 207, D3DRS_DESTBLENDALPHA = 208, D3DRS_BLENDOPALPHA = 209, D3DRS_BLENDFACTOR = 193;
constexpr std::size_t motion_shadow_state_count = 32;
constexpr std::size_t sampler_stage_count = 16;
constexpr unsigned failure_log_limit = 16;
constexpr D3DSAMPLERSTATETYPE D3DSAMP_MIPFILTER = 7, D3DSAMP_MIPMAPLODBIAS = 8,
    D3DSAMP_SRGBTEXTURE = 11;

struct D3DCAPS9 {
    DWORD NumSimultaneousRTs = 3;
    DWORD PrimitiveMiscCaps = D3DPMISCCAPS_INDEPENDENTWRITEMASKS
        | D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS
        | D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING;
    DWORD AlphaCmpCaps = D3DPCMPCAPS_GREATEREQUAL;
};
struct D3DDEVICE_CREATION_PARAMETERS { UINT AdapterOrdinal = 2; D3DDEVTYPE DeviceType = 1; };
struct D3DDISPLAYMODE { D3DFORMAT Format = 22; };

struct IDirect3D9 {
    HRESULT format_result[3]{S_OK,S_OK,S_OK};
    unsigned format_calls = 0, format_position = 0, releases = 0;
    bool wrong_arguments = false;
    HRESULT CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT display,
                              DWORD usage, D3DRESOURCETYPE resource, D3DFORMAT target) {
        const D3DFORMAT expected[] = {D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F,D3DFMT_R32F};
        ++format_calls;
        const unsigned call = format_position++;
        wrong_arguments = wrong_arguments || call >= 3 || adapter != 2 || type != 1 || display != 22
            || usage != (D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING)
            || resource != D3DRTYPE_TEXTURE || (call < 3 && target != expected[call]);
        const HRESULT result = call < 3 ? format_result[call] : E_FAIL;
        if (FAILED(result) || call == 2) format_position = 0;
        return result;
    }
    ULONG Release() { ++releases; return 0; }
};
struct IDirect3DDevice9 {
    IDirect3D9 factory;
    HRESULT direct_result = S_OK, creation_result = S_OK, display_result = S_OK;
    bool null_factory = false;
    unsigned direct_calls = 0, creation_calls = 0, display_calls = 0;
    DWORD sampler_value[sampler_stage_count]{};
    HRESULT sampler_results[4]{S_OK,S_OK,S_OK,S_OK};
    bool sampler_mutates[4]{};
    unsigned sampler_calls = 0;
};

int cpu_events[64]{}; unsigned cpu_event_count = 0;
struct PreserveCpuState {
    PreserveCpuState() { cpu_events[cpu_event_count++]=1; }
    ~PreserveCpuState() { cpu_events[cpu_event_count++]=3; }
};

HRESULT WINAPI fake_get_direct(IDirect3DDevice9* device, IDirect3D9** output) {
    ++device->direct_calls;
    *output = SUCCEEDED(device->direct_result) && !device->null_factory ? &device->factory : nullptr;
    return device->direct_result;
}
HRESULT WINAPI fake_get_creation(IDirect3DDevice9* device, D3DDEVICE_CREATION_PARAMETERS* output) {
    ++device->creation_calls;
    *output = D3DDEVICE_CREATION_PARAMETERS{};
    return device->creation_result;
}
HRESULT WINAPI fake_get_display(IDirect3DDevice9* device, UINT swapchain, D3DDISPLAYMODE* output) {
    ++device->display_calls;
    if (swapchain) return E_FAIL;
    *output = D3DDISPLAYMODE{};
    return device->display_result;
}
HRESULT WINAPI fake_set_sampler(IDirect3DDevice9* device, DWORD stage, D3DSAMPLERSTATETYPE,
                                DWORD value) {
    cpu_events[cpu_event_count++]=2;
    const unsigned call=device->sampler_calls++;
    const HRESULT result=call<4?device->sampler_results[call]:E_FAIL;
    if (stage<sampler_stage_count && (SUCCEEDED(result) || (call<4 && device->sampler_mutates[call])))
        device->sampler_value[stage]=value;
    return result;
}
template<class T> void release(T*& value) { if (value) { value->Release(); value = nullptr; } }

enum Slot : unsigned { GetDirect3D = 6, GetDisplayMode = 8, GetCreationParameters = 9,
    SetSamplerState = 69 };
using GetDirect3DFn = HRESULT(WINAPI*)(IDirect3DDevice9*, IDirect3D9**);
using GetDisplayModeFn = HRESULT(WINAPI*)(IDirect3DDevice9*, UINT, D3DDISPLAYMODE*);
using GetCreationFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DDEVICE_CREATION_PARAMETERS*);
using SetSamplerStateFn = HRESULT(WINAPI*)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);

struct MotionRoute {
    bool routed = false, composition = false, submit = true;
    bool cutout_candidate = false;
    bool cutout_test_known = false, cutout_color_known = false;
    bool cutout_alpha_known = false, cutout_z_known = false, cutout_zfunc_known = false, cutout_blend_known = false, cutout_source_over = false;
    DWORD cutout_test = 0, cutout_color = 0, cutout_alpha = 0, cutout_z = 0, cutout_zfunc = 0, cutout_blend = 0;
};
struct Surface { bool known = false; D3DFORMAT format = 0; };
// Mirrors src/proxy/motion_output.h's TaaInvalidateSite; this double only
// counts the calls, so the names exist for the extracted code to compile.
enum class TaaInvalidateSite : unsigned { RestoreFailed = 0, StateLost = 1, Skip = 2, Target = 3,
    Container = 4, ResolveFailed = 5, NotResolved = 6, PresentFailed = 7, Reset = 8,
    ComparisonExposure = 9, ComparisonStateFailed = 10, CompositionStateLost = 11,
    CompositionReaders = 12, CompositionExport = 13, CompositionAttach = 14, CompositionBegin = 15,
    CompositionRefused = 16, CompositionPrepare = 17, CompositionIncomplete = 18,
    CutoutMissed = 19, Count = 20 };

class MotionOutput {
public:
    enum class HdrState { Off, Active, Suspended };
    template<typename Fn> Fn native(unsigned slot) const noexcept { return reinterpret_cast<Fn>(native_[slot]); }
    void log(const char*, ...) noexcept {}
    HRESULT render_state(D3DRENDERSTATETYPE state, DWORD* value) noexcept {
        ++render_queries;
        if (state >= render_values.size() || !render_known[state]) return E_FAIL;
        *value = render_values[state]; return S_OK;
    }
    bool composition_requested() const noexcept { return composition_requested_value; }
    void invalidate_taa(TaaInvalidateSite) noexcept { ++taa_invalidations; }
    void probe_cutout_caps(bool force = false) noexcept;
    bool cutout_arm_configured() const noexcept;
    void release_mip_bias_retry_bound() noexcept;
    bool cutout_draw_state() noexcept;
    void mark_cutout_candidate(MotionRoute& route) noexcept;
    void render_state_failed(D3DRENDERSTATETYPE state) noexcept;
    void sampler_state_failed(DWORD stage,D3DSAMPLERSTATETYPE type) noexcept;
    void before_set_sampler_state(DWORD stage,D3DSAMPLERSTATETYPE type) noexcept;
    void restore_mip_bias_stage(unsigned stage,HRESULT* first) noexcept;
    void report_mip_bias_game_write_failure() noexcept;
    void set_sampler_state(DWORD stage,D3DSAMPLERSTATETYPE type,DWORD value) noexcept;

    IDirect3DDevice9* device_ = nullptr;
    void** native_ = nullptr;
    D3DCAPS9 caps_{};
    std::uint64_t id_ = 1, frame_ = 0, cutout_probe_frame_ = 0;
    bool linear_material_requested_ = true, cutout_probe_frame_known_ = false;
    bool cutout_reset_pending_ = false, hdr_enabled_ = true;
    bool enabled_ = true, composition_requested_value = true;
    bool motion_state_lost_ = false, composition_effective_ = false;
    bool composition_state_lost_ = false, composition_frame_stopped_ = false;
    HRESULT motion_state_error_ = S_OK;
    x3m::cutout::Capability cutout_caps_ = x3m::cutout::Capability::Pending;
    HRESULT cutout_cap_result_ = S_FALSE;
    std::uint32_t cutout_cap_queries_ = 0, cutout_cap_logs_ = 0;
    HdrState hdr_state_ = HdrState::Active;
    Surface hdr_target_{true,D3DFMT_A16B16G16R16F};
    DWORD mip_bias_bits_ = 0;
    unsigned logged_failures_ = 0;
    std::uint32_t sampler_restore_failed_mask_ = 0;
    bool cutout_arm_active_ = false;
    struct MipBiasGameWriteFailure {
        std::uint64_t device = 0, frame = 0;
        unsigned long index = 0, result = 0;
        bool pending = false;
    } mip_bias_game_write_failure_;
    struct {
        bool cutout_pair = false, recording = false;
        bool states_known[motion_shadow_state_count]{};
        bool composition_blend_known[4]{};
        DWORD composition_blend[4]{};
    } shadow_;
    struct {
        unsigned rs_resyncs = 0, rs_invalidations = 0, draws = 0;
        unsigned mip_bias_restores = 0, mip_bias_game_writes = 0;
        unsigned mip_bias_failures = 0, restore_failures = 0;
    } counters_;
    struct Sampler {
        DWORD saved_bias = 0, srgb = 0, mipfilter = 0;
        bool srgb_known = false, mipfilter_known = false, saved_known = false, biased = false;
    } samplers_[sampler_stage_count];
    std::uint32_t sampler_biased_mask_ = 0;
    std::uint32_t mip_bias_total_restores_ = 0, mip_bias_total_game_writes_ = 0;
    std::uint32_t mip_bias_total_failures_ = 0;
    DWORD mip_bias_game_write_stage_ = 0, mip_bias_game_write_value_ = 0;
    unsigned taa_invalidations = 0;
    std::array<DWORD,256> render_values{};
    std::array<bool,256> render_known{};
    unsigned render_queries = 0;
};

using x3m::cutout::Capability;
namespace cutout = x3m::cutout;
'''


MAIN = r'''
int failures = 0, checks = 0, scenarios = 0;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr,"FAIL %s\n",label); }
}
void scenario() { ++scenarios; }

struct Harness {
    IDirect3DDevice9 device;
    void* table[70]{};
    MotionOutput output;
    Harness() {
        table[GetDirect3D] = reinterpret_cast<void*>(fake_get_direct);
        table[GetDisplayMode] = reinterpret_cast<void*>(fake_get_display);
        table[GetCreationParameters] = reinterpret_cast<void*>(fake_get_creation);
        table[SetSamplerState] = reinterpret_cast<void*>(fake_set_sampler);
        output.device_ = &device; output.native_ = table;
    }
};

void pure_contract() {
    scenario();
    check(x3m::cutout::pair(0x53a0a641107ed76cull,0x63f96eba9eea7880ull),"default pair");
    check(x3m::cutout::pair(0x4944d81dfe531b37ull,0x5e0a10fe752b6140ull),"bump pair");
    check(!x3m::cutout::pair(0x53a0a641107ed76cull,0x5e0a10fe752b6140ull),"cross pair");
    check(!x3m::cutout::pair(0,0),"unknown pair");
    check(x3m::cutout::query_result(S_OK)==Capability::Ready,"success ready");
    check(x3m::cutout::query_result(D3DERR_NOTAVAILABLE)==Capability::Unsupported,"notavailable unsupported");
    check(x3m::cutout::query_result(E_FAIL)==Capability::Retry,"fail retry");
    check(x3m::cutout::query_result(E_OUTOFMEMORY)==Capability::Retry,"oom retry");

    scenario();
    auto state=x3m::cutout::values;
    check(x3m::cutout::state(state),"exact state");
    for (unsigned i=0;i<state.size();++i) {
        auto wrong=state; ++wrong[i];
        check(!x3m::cutout::state(wrong),"single state mismatch");
        check(shadow_states[24+i] != 0 && shadow_index(shadow_states[24+i])==24+i,"cutout shadow index");
    }
    check(shadow_index(999)==motion_shadow_state_count,"unknown state sentinel");
    check(sizeof(shadow_states)/sizeof(shadow_states[0])==32,"shadow maximum");

    scenario();
    auto missed=[](bool candidate=true,bool submitted=true,bool success=true,bool routed=false,
                   bool tk=true,DWORD test=1,bool ck=true,DWORD color=7,
                   bool ak=true,DWORD alpha=7,bool zk=true,DWORD z=1,
                   bool zfk=true,DWORD zfunc=4,bool source_over=false) {
        return x3m::cutout::missed(candidate,submitted,success,routed,tk,test,ck,color,ak,alpha,zk,z,zfk,zfunc,source_over);
    };
    check(missed(),"visible unrouted cutout missed");
    check(!missed(false),"noncandidate");
    check(!missed(true,false),"suppressed submit");
    check(!missed(true,true,false),"failed submit");
    check(!missed(true,true,true,true),"routed");
    check(!missed(true,true,true,false,true,0),"test disabled");
    check(missed(true,true,true,false,false,0),"unknown test conservative");
    check(!missed(true,true,true,false,true,1,true,0),"no rgb color");
    check(!missed(true,true,true,false,true,1,true,8),"alpha-only color");
    check(missed(true,true,true,false,true,1,false,0),"unknown color conservative");
    check(!missed(true,true,true,false,true,1,true,7,true,D3DCMP_NEVER),"alpha never");
    check(missed(true,true,true,false,true,1,true,7,false,D3DCMP_NEVER),"unknown alpha conservative");
    check(!missed(true,true,true,false,true,1,true,7,true,7,true,1,true,D3DCMP_NEVER),"depth never");
    check(missed(true,true,true,false,true,1,true,7,true,7,true,0,true,D3DCMP_NEVER),"disabled depth can color");
    check(missed(true,true,true,false,true,1,true,7,true,7,true,1,false,D3DCMP_NEVER),"unknown zfunc conservative");
    check(!missed(true,true,true,false,true,1,true,7,true,7,true,1,true,4,true),"exact source-over is native colour, not a miss");
    check(x3m::cutout::source_over(true,1,true,5,true,6),"blend on, SRCALPHA/INVSRCALPHA");
    check(!x3m::cutout::source_over(true,0,true,5,true,6),"blend off still misses");
    check(!x3m::cutout::source_over(false,1,true,5,true,6),"unknown blend conservative");
    check(!x3m::cutout::source_over(true,1,true,2,true,1),"ONE/ZERO is not source-over");
    check(!x3m::cutout::source_over(true,1,false,5,true,6)&&!x3m::cutout::source_over(true,1,true,5,false,6),"unknown factor conservative");

    scenario();
    check(!x3m::cutout::unavailable(false,false,false),"optional unavailable composition ignored");
    check(!x3m::cutout::unavailable(false,false,true),"optional ready composition ignored");
    check(x3m::cutout::unavailable(false,true,false),"required composition absent");
    check(!x3m::cutout::unavailable(false,true,true),"required composition ready");
    for (bool required:{false,true}) for (bool ready:{false,true})
        check(x3m::cutout::unavailable(true,required,ready),"miss dominates availability");
}

void capability_contract() {
    scenario();
    Harness good; good.output.probe_cutout_caps();
    check(good.output.cutout_caps_==Capability::Ready,"all required caps ready");
    check(good.output.cutout_arm_active_,"a Ready verdict refreshes the frame's arm latch");
    check(good.output.cutout_cap_queries_==1 && good.device.factory.format_calls==3,"one complete probe");
    check(good.device.direct_calls==1 && good.device.creation_calls==1 && good.device.display_calls==1,"public native queries");
    check(good.device.factory.releases==1 && !good.device.factory.wrong_arguments,"factory release and arguments");

    for (unsigned missing=0;missing<5;++missing) {
        scenario(); Harness h;
        if (missing==0) h.output.caps_.NumSimultaneousRTs=2;
        else if (missing==1) h.output.caps_.PrimitiveMiscCaps&=~D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS;
        else if (missing==2) h.output.caps_.PrimitiveMiscCaps&=~D3DPMISCCAPS_INDEPENDENTWRITEMASKS;
        else if (missing==3) h.output.caps_.PrimitiveMiscCaps&=~D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING;
        else h.output.caps_.AlphaCmpCaps&=~D3DPCMPCAPS_GREATEREQUAL;
        h.output.cutout_arm_active_=true; h.output.probe_cutout_caps();
        check(h.output.cutout_caps_==Capability::Unsupported,"missing required cap unsupported");
        check(!h.output.cutout_arm_active_,"an Unsupported verdict deactivates the frame's arm latch");
        check(h.device.direct_calls==0 && h.device.factory.format_calls==0,"numeric refusal has no adapter query");
    }

    for (unsigned format=0;format<3;++format) {
        scenario(); Harness h; h.device.factory.format_result[format]=D3DERR_NOTAVAILABLE;
        h.output.probe_cutout_caps();
        check(h.output.cutout_caps_==Capability::Unsupported,"unsupported format verdict");
        check(h.device.factory.format_calls==format+1,"unsupported format short circuit");
        h.output.frame_=1; h.output.probe_cutout_caps();
        check(h.output.cutout_cap_queries_==1,"unsupported is terminal");
    }
    for (HRESULT transient:{E_FAIL,E_OUTOFMEMORY}) for (unsigned format=0;format<3;++format) {
        scenario(); Harness h; h.device.factory.format_result[format]=transient;
        h.output.probe_cutout_caps();
        check(h.output.cutout_caps_==Capability::Retry,"transient format retry verdict");
        check(h.device.factory.format_calls==format+1,"transient format short circuit");
    }

    for (unsigned query=0;query<8;++query) {
        scenario(); Harness h;
        if (query==0) h.device.direct_result=E_FAIL;
        else if (query==1) { h.device.direct_result=E_OUTOFMEMORY; }
        else if (query==2) h.device.null_factory=true;
        else if (query==3) h.device.creation_result=E_FAIL;
        else if (query==4) h.device.display_result=E_OUTOFMEMORY;
        else if (query==5) h.device.direct_result=D3DERR_NOTAVAILABLE;
        else if (query==6) h.device.creation_result=D3DERR_NOTAVAILABLE;
        else h.device.display_result=D3DERR_NOTAVAILABLE;
        h.output.probe_cutout_caps();
        check(h.output.cutout_caps_==Capability::Retry,"transient adapter query retry verdict");
        check(h.device.factory.format_calls==0,"adapter failure precedes formats");
    }

    scenario(); Harness retry;
    retry.output.frame_=20; retry.device.factory.format_result[0]=E_FAIL;
    retry.output.probe_cutout_caps(); retry.output.probe_cutout_caps();
    check(retry.output.cutout_cap_queries_==1,"at most one failed attempt per frame");
    retry.output.frame_=21; retry.device.factory.format_result[0]=S_OK;
    retry.output.probe_cutout_caps();
    check(retry.output.cutout_cap_queries_==2 && retry.output.cutout_caps_==Capability::Ready,
          "next HDR latch retries and succeeds");
    check(retry.device.factory.format_calls==4,"retry performs fresh format sequence");

    scenario(); Harness reset;
    reset.output.cutout_reset_pending_=true; reset.output.probe_cutout_caps(true);
    check(reset.output.cutout_cap_queries_==0 && reset.output.cutout_caps_==Capability::Pending,
          "reset pending blocks forced probe");
    reset.output.cutout_reset_pending_=false; reset.output.probe_cutout_caps(true);
    check(reset.output.cutout_cap_queries_==1 && reset.output.cutout_caps_==Capability::Ready,
          "post-reset forced probe");
}

void draw_helpers() {
    scenario(); Harness h;
    for (unsigned i=0;i<x3m::cutout::values.size();++i) {
        h.output.render_known[shadow_states[24+i]]=true;
        h.output.render_values[shadow_states[24+i]]=x3m::cutout::values[i];
    }
    check(h.output.cutout_draw_state()==false,"pending caps refuse draw");
    h.output.cutout_caps_=Capability::Ready;
    check(h.output.cutout_draw_state(),"exact draw state accepted");
    check(h.device.factory.format_calls==0,"draw state never probes capabilities");
    for (unsigned i=0;i<8;++i) {
        const auto state=shadow_states[24+i]; ++h.output.render_values[state];
        check(!h.output.cutout_draw_state(),"single draw-state mismatch refused");
        --h.output.render_values[state];
    }
    h.output.cutout_reset_pending_=true; check(!h.output.cutout_draw_state(),"reset pending refuses draw");
    h.output.cutout_reset_pending_=false; h.output.hdr_enabled_=false; check(!h.output.cutout_draw_state(),"HDR off refuses draw");
    h.output.hdr_enabled_=true; h.output.hdr_state_=MotionOutput::HdrState::Off; check(!h.output.cutout_draw_state(),"HDR latch inactive");
    h.output.hdr_state_=MotionOutput::HdrState::Active; h.output.hdr_target_.known=false; check(!h.output.cutout_draw_state(),"HDR target unknown");
    h.output.hdr_target_.known=true; h.output.hdr_target_.format=D3DFMT_A32B32G32R32F; check(!h.output.cutout_draw_state(),"HDR target format mismatch");
    h.output.hdr_target_.format=D3DFMT_A16B16G16R16F; h.output.mip_bias_bits_=1; check(!h.output.cutout_draw_state(),"mip bias refused");

    scenario(); Harness candidate; MotionRoute route;
    candidate.output.cutout_caps_=Capability::Ready; candidate.output.cutout_arm_active_=candidate.output.cutout_arm_configured();
    check(candidate.output.cutout_arm_active_,"a configured arm latches active");
    candidate.output.mark_cutout_candidate(route);
    check(!route.cutout_candidate && candidate.output.render_queries==0,"nonpair is not candidate");
    candidate.output.shadow_.cutout_pair=true;
    candidate.output.render_known[D3DRS_ALPHATESTENABLE]=true;
    candidate.output.render_values[D3DRS_ALPHATESTENABLE]=0;
    candidate.output.mark_cutout_candidate(route);
    check(!route.cutout_candidate,"known test-off is not candidate");
    route={}; candidate.output.render_values[D3DRS_ALPHATESTENABLE]=1;
    candidate.output.render_known[D3DRS_COLORWRITEENABLE]=true;
    candidate.output.render_values[D3DRS_COLORWRITEENABLE]=8;
    candidate.output.mark_cutout_candidate(route);
    check(!route.cutout_candidate,"known no-RGB is not candidate");
    route={}; candidate.output.render_values[D3DRS_COLORWRITEENABLE]=7;
    candidate.output.render_known[D3DRS_ALPHABLENDENABLE]=true; candidate.output.render_values[D3DRS_ALPHABLENDENABLE]=1;
    candidate.output.mark_cutout_candidate(route);
    check(route.cutout_candidate && !route.cutout_source_over,"blend on with unknown factors stays a conservative candidate");
    route={}; candidate.output.render_known[D3DRS_SRCBLEND]=candidate.output.render_known[D3DRS_DESTBLEND]=true;
    candidate.output.render_values[D3DRS_SRCBLEND]=2; candidate.output.render_values[D3DRS_DESTBLEND]=1;
    candidate.output.mark_cutout_candidate(route);
    check(route.cutout_candidate && !route.cutout_source_over,"blend on with ONE/ZERO stays a candidate");
    route={}; candidate.output.render_values[D3DRS_SRCBLEND]=5; candidate.output.render_values[D3DRS_DESTBLEND]=6;
    candidate.output.mark_cutout_candidate(route);
    check(!route.cutout_candidate && route.cutout_source_over,"exact source-over pair is not candidate (ordinary native colour)");
    route={}; candidate.output.render_known[D3DRS_SRCBLEND]=candidate.output.render_known[D3DRS_DESTBLEND]=false;
    candidate.output.shadow_.composition_blend_known[0]=candidate.output.shadow_.composition_blend_known[1]=true;
    candidate.output.shadow_.composition_blend[0]=5; candidate.output.shadow_.composition_blend[1]=6;
    const unsigned queries_before=candidate.output.render_queries; candidate.output.mark_cutout_candidate(route);
    check(!route.cutout_candidate && route.cutout_source_over && candidate.output.render_queries==queries_before+2,"maintained composition blend shadow supplies the factors without a query");
    candidate.output.shadow_.composition_blend_known[0]=candidate.output.shadow_.composition_blend_known[1]=false;
    route={}; candidate.output.render_values[D3DRS_ALPHABLENDENABLE]=0;
    for (auto state:{D3DRS_ALPHAFUNC,D3DRS_ZENABLE,D3DRS_ZFUNC}) {
        candidate.output.render_known[state]=true; candidate.output.render_values[state]=state==D3DRS_ALPHAFUNC?7:state==D3DRS_ZENABLE?1:4;
    }
    candidate.output.mark_cutout_candidate(route);
    check(route.cutout_candidate && route.cutout_test_known && route.cutout_color_known && route.cutout_blend_known && route.cutout_blend==0 && !route.cutout_source_over
          && route.cutout_alpha_known && route.cutout_z_known && route.cutout_zfunc_known,"candidate snapshots visibility states");
    route={}; candidate.output.render_known[D3DRS_ALPHATESTENABLE]=false;
    candidate.output.mark_cutout_candidate(route);
    check(route.cutout_candidate && !route.cutout_test_known,"unknown alpha-test remains conservative candidate");
    check(candidate.device.factory.format_calls==0,"candidate marking never probes capabilities");

    // Policy: a refused/unrouted exact pair misses coverage only while the
    // frame's configured arm is active (feature, Ready caps, HDR enabled by
    // configuration, zero bias). Transient HDR state does not deactivate it.
    scenario(); {
        const auto visible_miss=[](const MotionRoute& r){
            return x3m::cutout::missed(r.cutout_candidate,true,true,false,r.cutout_test_known,r.cutout_test,
                r.cutout_color_known,r.cutout_color,r.cutout_alpha_known,r.cutout_alpha,r.cutout_z_known,r.cutout_z,
                r.cutout_zfunc_known,r.cutout_zfunc,r.cutout_source_over);
        };
        const auto arm=[&](auto&& configure){
            Harness h; h.output.shadow_.cutout_pair=true;
            h.output.render_known[D3DRS_ALPHATESTENABLE]=true; h.output.render_values[D3DRS_ALPHATESTENABLE]=1;
            h.output.render_known[D3DRS_COLORWRITEENABLE]=true; h.output.render_values[D3DRS_COLORWRITEENABLE]=7;
            h.output.cutout_caps_=Capability::Ready;
            configure(h.output,true);                                     // frame-start configuration
            h.output.cutout_arm_active_=h.output.cutout_arm_configured(); // begin_frame latch
            configure(h.output,false);                                    // mid-frame state at the draw
            MotionRoute r; h.output.mark_cutout_candidate(r);
            return std::pair<bool,bool>(h.output.cutout_arm_active_, r.cutout_candidate && visible_miss(r) && h.output.render_queries>0);
        };
        const auto inactive=[&](const char* what,auto&& configure){
            const auto [active,miss]=arm([&](MotionOutput& o,bool start){ if(start) configure(o); });
            check(!active && !miss,what);
        };
        inactive("unsupported caps: refused pair keeps history",[](MotionOutput& o){o.cutout_caps_=Capability::Unsupported;});
        inactive("retry pending: refused pair keeps history",[](MotionOutput& o){o.cutout_caps_=Capability::Retry;});
        inactive("pending caps: refused pair keeps history",[](MotionOutput& o){o.cutout_caps_=Capability::Pending;});
        inactive("feature disabled: refused pair keeps history",[](MotionOutput& o){o.linear_material_requested_=false;});
        inactive("HDR disabled by configuration: refused pair keeps history",[](MotionOutput& o){o.hdr_enabled_=false;});
        inactive("nonzero configured mip bias: refused pair keeps history",[](MotionOutput& o){o.mip_bias_bits_=1;});
        const auto miss=[&](const char* what,auto&& configure){
            const auto [active,missed]=arm([&](MotionOutput& o,bool start){ if(!start) configure(o); });
            check(active && missed,what);
        };
        miss("active arm: a draw before the frame's HDR latch misses coverage",[](MotionOutput& o){o.hdr_state_=MotionOutput::HdrState::Off;});
        miss("active arm: a draw after a mid-frame Suspend misses coverage",[](MotionOutput& o){o.hdr_state_=MotionOutput::HdrState::Suspended;});
        miss("active arm: an unknown HDR target still misses coverage",[](MotionOutput& o){o.hdr_target_.known=false;});
        miss("active arm: a non-FP16 HDR target still misses coverage",[](MotionOutput& o){o.hdr_target_.format=D3DFMT_A32B32G32R32F;});
        miss("active arm: a reset-pending draw still misses coverage",[](MotionOutput& o){o.cutout_reset_pending_=true;});
        miss("active arm: a gate-refused visible pair misses coverage",[](MotionOutput& o){o.render_known[D3DRS_ALPHAFUNC]=true; o.render_values[D3DRS_ALPHAFUNC]=5;});
        // Only a probe verdict refreshes the latch; the stored field alone does not.
        const auto [late_unsupported,late_miss]=arm([&](MotionOutput& o,bool start){ if(!start) o.cutout_caps_=Capability::Unsupported; });
        check(late_unsupported && late_miss,"the frame-start latch holds until begin_frame or a probe verdict refreshes it");
        Harness routed; routed.output.shadow_.cutout_pair=true; routed.output.cutout_caps_=Capability::Ready; routed.output.cutout_arm_active_=true;
        routed.output.render_known[D3DRS_ALPHATESTENABLE]=true; routed.output.render_values[D3DRS_ALPHATESTENABLE]=1;
        routed.output.render_known[D3DRS_COLORWRITEENABLE]=true; routed.output.render_values[D3DRS_COLORWRITEENABLE]=7;
        MotionRoute r; routed.output.mark_cutout_candidate(r);
        check(!x3m::cutout::missed(r.cutout_candidate,true,true,true,r.cutout_test_known,r.cutout_test,r.cutout_color_known,
              r.cutout_color,r.cutout_alpha_known,r.cutout_alpha,r.cutout_z_known,r.cutout_z,r.cutout_zfunc_known,r.cutout_zfunc,r.cutout_source_over),
              "active arm: a routed pair is not missed");
    }

    scenario(); Harness invalidation;
    const unsigned alpha_ref=shadow_index(D3DRS_ALPHAREF), srgb_write=shadow_index(D3DRS_SRGBWRITEENABLE);
    invalidation.output.shadow_.states_known[alpha_ref]=true;
    invalidation.output.shadow_.states_known[srgb_write]=true;
    invalidation.output.render_state_failed(D3DRS_ALPHAREF);
    invalidation.output.render_state_failed(D3DRS_SRGBWRITEENABLE);
    check(!invalidation.output.shadow_.states_known[alpha_ref]
          && !invalidation.output.shadow_.states_known[srgb_write]
          && invalidation.output.counters_.rs_invalidations==2 && invalidation.output.counters_.rs_resyncs==0,
          "failed cutout and sRGB render states invalidate shadow apart from resyncs");
    invalidation.output.shadow_.composition_blend_known[0]=true;
    invalidation.output.render_state_failed(D3DRS_SRCBLEND);
    check(!invalidation.output.shadow_.composition_blend_known[0],"failed blend state invalidates composition shadow");
    invalidation.output.samplers_[0].srgb_known=true;
    invalidation.output.sampler_state_failed(0,D3DSAMP_SRGBTEXTURE);
    check(!invalidation.output.samplers_[0].srgb_known,"failed sampler sRGB invalidates shadow");
    invalidation.output.samplers_[1].mipfilter_known=true;
    invalidation.output.sampler_state_failed(1,D3DSAMP_MIPFILTER);
    check(!invalidation.output.samplers_[1].mipfilter_known,"failed mip filter invalidates shadow");
    invalidation.output.samplers_[2].saved_known=true;
    invalidation.output.sampler_state_failed(2,D3DSAMP_MIPMAPLODBIAS);
    check(!invalidation.output.samplers_[2].saved_known,"failed mip bias invalidates saved value");
    invalidation.output.samplers_[3].srgb_known=true;
    invalidation.output.shadow_.recording=true;
    invalidation.output.sampler_state_failed(3,D3DSAMP_SRGBTEXTURE);
    check(invalidation.output.samplers_[3].srgb_known,"recording leaves sampler shadow for resync");
}

// The application's own SetSamplerState hook, in capture.cpp's order: the
// pre-restore of a held bias, the CPU boundary, the native setter and then
// exactly one of the success/failure notifications.
constexpr int CpuEnter = 1, CpuNative = 2, CpuLeave = 3, BoundaryBefore = 4, BoundaryAfter = 5;
constexpr DWORD route_bias = 0xbf000000u;   // the value the route puts on a stage
constexpr DWORD held_value = 0x11111111u;   // the application's value the route saved
constexpr DWORD app_value  = 0x22222222u;   // the value the application now writes

HRESULT hook_set_sampler_state(Harness& h, DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value) {
    h.output.before_set_sampler_state(stage, type);
    cpu_events[cpu_event_count++] = BoundaryBefore;
    const HRESULT hr = h.output.native<SetSamplerStateFn>(SetSamplerState)(&h.device, stage, type, value);
    cpu_events[cpu_event_count++] = BoundaryAfter;
    if (SUCCEEDED(hr)) h.output.set_sampler_state(stage, type, value);
    else h.output.sampler_state_failed(stage, type);
    return hr;
}
void arm(Harness& h) { h.output.mip_bias_bits_ = route_bias; cpu_event_count = 0; }
// A stage the route currently holds: the saved application value is known and
// the route's own bias is what sits on the device.
void hold_bias(Harness& h, unsigned stage, DWORD saved = held_value) {
    auto& s = h.output.samplers_[stage];
    s.saved_bias = saved; s.saved_known = true; s.biased = true;
    h.output.sampler_biased_mask_ |= 1u << stage;
    h.device.sampler_value[stage] = route_bias;
}
bool events_are(const int* expected, unsigned count) {
    if (cpu_event_count != count) return false;
    for (unsigned i = 0; i < count; ++i) if (cpu_events[i] != expected[i]) return false;
    return true;
}
bool owns_nothing(const MotionOutput& o, unsigned stage) {
    return !o.samplers_[stage].biased && !(o.sampler_biased_mask_ & (1u << stage));
}
bool quiet(const MotionOutput& o) {
    return !o.motion_state_lost_ && o.motion_state_error_ == S_OK && o.taa_invalidations == 0
        && o.counters_.restore_failures == 0 && o.counters_.mip_bias_failures == 0
        && o.mip_bias_total_failures_ == 0 && !o.composition_state_lost_ && !o.composition_frame_stopped_;
}

void sampler_transaction() {
    // A failed application setter over a held bias, both native outcomes.
    for (bool mutates : {false, true}) {
        scenario(); Harness h; arm(h); hold_bias(h, 3);
        h.device.sampler_results[1] = E_FAIL; h.device.sampler_mutates[1] = mutates;
        const HRESULT hr = hook_set_sampler_state(h, 3, D3DSAMP_MIPMAPLODBIAS, app_value);
        check(hr == E_FAIL, "failed setter forwards its HRESULT");
        check(h.device.sampler_calls == 2, "pre-restore precedes the application setter");
        check(h.device.sampler_value[3] == (mutates ? app_value : held_value),
              "the device holds the application's value, mutated or not");
        check(h.output.counters_.mip_bias_restores == 1 && h.output.mip_bias_total_restores_ == 1,
              "the pre-restore is counted once");
        check(owns_nothing(h.output, 3), "a failed application write leaves no owned obligation");
        check(!h.output.samplers_[3].saved_known, "a failed application write invalidates the saved value");
        check(h.output.counters_.mip_bias_game_writes == 0 && h.output.mip_bias_total_game_writes_ == 0,
              "a failed application write is not an accepted game write");
        check(quiet(h.output), "a successful pre-restore latches no state loss");
        const int order[] = {CpuEnter, CpuNative, CpuLeave, BoundaryBefore, CpuNative, BoundaryAfter};
        check(events_are(order, 6), "pre-restore completes before the boundary and the application native call");
    }

    // The ordinary success: the application's value becomes the saved value.
    scenario(); { Harness h; arm(h); hold_bias(h, 3);
        check(hook_set_sampler_state(h, 3, D3DSAMP_MIPMAPLODBIAS, app_value) == S_OK, "success forwards S_OK");
        check(h.device.sampler_value[3] == app_value, "the application's value stands on the device");
        check(h.output.samplers_[3].saved_known && h.output.samplers_[3].saved_bias == app_value,
              "the application's own value is what a later restore puts back");
        check(owns_nothing(h.output, 3), "an accepted application write clears the route's bias");
        check(h.output.counters_.mip_bias_restores == 1, "one pre-restore for one held stage");
        check(h.output.counters_.mip_bias_game_writes == 1 && h.output.mip_bias_total_game_writes_ == 1
              && h.output.mip_bias_game_write_stage_ == 3 && h.output.mip_bias_game_write_value_ == app_value,
              "the accepted application write is recorded for the heavy-path log");
        check(quiet(h.output), "the ordinary transaction latches no state loss");
    }

    // The owned restore itself fails: state loss and composition quarantine.
    scenario(); { Harness h; arm(h); hold_bias(h, 3); hold_bias(h, 5);
        h.output.composition_effective_ = true;
        h.device.sampler_results[0] = E_FAIL; h.device.sampler_results[1] = E_FAIL;
        check(hook_set_sampler_state(h, 3, D3DSAMP_MIPMAPLODBIAS, app_value) == E_FAIL,
              "the application setter failure is forwarded, not the restore's");
        check(h.output.motion_state_lost_ && h.output.motion_state_error_ == E_FAIL
              && h.output.taa_invalidations == 1, "a failed owned restore latches motion state loss");
        check(h.output.composition_state_lost_ && h.output.composition_frame_stopped_,
              "a failed owned restore quarantines the effective composition");
        check(h.output.counters_.mip_bias_failures == 1 && h.output.mip_bias_total_failures_ == 1
              && h.output.counters_.restore_failures == 1, "the failed restore is counted once");
        check(h.output.samplers_[3].biased && (h.output.sampler_biased_mask_ & (1u << 3)) && !h.output.samplers_[3].saved_known,
              "a failed restore plus a failed write keeps the obligation recorded and distrusts the saved value");
        check(h.device.sampler_value[3] == route_bias, "the route's bias is still on the device after both failures");
        check(h.output.sampler_restore_failed_mask_ == (1u << 3), "the failed stage is marked attempted for this frame");
        { const unsigned calls = h.device.sampler_calls, restores = h.output.counters_.mip_bias_restores; HRESULT again = S_OK;
          h.output.restore_mip_bias_stage(3, &again);
          check(SUCCEEDED(again) && h.device.sampler_calls == calls && h.output.counters_.mip_bias_restores == restores
                && h.output.samplers_[3].biased, "a restore point in the same frame does not retry, count or re-latch"); }
        check(h.output.samplers_[5].biased && (h.output.sampler_biased_mask_ & (1u << 5)),
              "the other held stage is untouched");
        check(h.output.logged_failures_ == 1, "a failed pre-restore consumes one bounded log slot");
        check(h.output.mip_bias_game_write_failure_.pending && h.output.mip_bias_game_write_failure_.result == static_cast<unsigned long>(E_FAIL)
              && h.output.mip_bias_game_write_failure_.index == h.output.counters_.draws,
              "the hook records the failure for deferred formatting instead of logging in the light root");
        h.device.sampler_results[2] = E_FAIL; h.device.sampler_results[3] = E_FAIL;
        check(hook_set_sampler_state(h, 5, D3DSAMP_MIPMAPLODBIAS, app_value) == E_FAIL, "second failing transaction");
        check(h.output.taa_invalidations == 1, "state loss is latched once");
        check(h.output.counters_.restore_failures == 2 && h.output.counters_.mip_bias_failures == 2,
              "each failed restore is counted");
        check(h.output.sampler_biased_mask_ == ((1u << 3) | (1u << 5)), "every failed restore keeps its obligation");
        check(h.output.logged_failures_ == 1 && h.output.mip_bias_game_write_failure_.pending,
              "an unreported first failure is kept; a second one before Present only counts");
        h.output.report_mip_bias_game_write_failure();
        check(!h.output.mip_bias_game_write_failure_.pending, "Present-side reporting clears the pending event");
        h.output.report_mip_bias_game_write_failure();
        check(!h.output.mip_bias_game_write_failure_.pending, "reporting twice is idempotent");
        h.device.sampler_calls = 0; h.device.sampler_results[0] = E_FAIL; h.device.sampler_results[1] = E_FAIL;
        { const unsigned failures = h.output.counters_.restore_failures;
          check(hook_set_sampler_state(h, 5, D3DSAMP_MIPMAPLODBIAS, app_value) == E_FAIL && h.device.sampler_calls == 1
                && h.output.counters_.restore_failures == failures && !h.output.mip_bias_game_write_failure_.pending,
                "a stage already attempted this frame is not retried by a later write, counted or recorded"); }
        h.output.release_mip_bias_retry_bound(); hold_bias(h, 6);
        h.device.sampler_calls = 0; h.device.sampler_results[0] = E_FAIL; h.device.sampler_results[1] = E_FAIL;
        check(hook_set_sampler_state(h, 6, D3DSAMP_MIPMAPLODBIAS, app_value) == E_FAIL, "third failing transaction, next frame");
        check(h.output.logged_failures_ == 2 && h.output.mip_bias_game_write_failure_.pending,
              "each reported pre-restore failure is logged under the shared bound");
        // After Present the bound lifts, but a distrusted saved value is never written:
        // the obligation waits for an accepted application write or Reset.
        h.output.release_mip_bias_retry_bound();
        h.device.sampler_calls = 0; h.device.sampler_results[0] = S_OK; h.device.sampler_results[1] = S_OK;
        { const unsigned restores = h.output.counters_.mip_bias_restores; HRESULT first = S_OK; h.output.restore_mip_bias_stage(3, &first);
          check(SUCCEEDED(first) && h.device.sampler_calls == 0 && h.output.counters_.mip_bias_restores == restores
                && h.output.samplers_[3].biased && h.device.sampler_value[3] == route_bias,
                "no trusted saved value: the next frame's restore writes nothing and keeps the obligation"); }
        check(hook_set_sampler_state(h, 3, D3DSAMP_MIPMAPLODBIAS, app_value) == S_OK && owns_nothing(h.output, 3)
              && h.output.samplers_[3].saved_known && h.output.samplers_[3].saved_bias == app_value,
              "an accepted application write clears the kept obligation");
    }

    // The owned restore fails without an application write (a restore point):
    // the saved value stays trusted, one attempt per frame, then a successful
    // restore in a later frame clears the obligation.
    scenario(); { Harness h; arm(h); hold_bias(h, 4);
        h.device.sampler_results[0] = E_FAIL; h.device.sampler_results[1] = E_FAIL;
        HRESULT first = S_OK; h.output.restore_mip_bias_stage(4, &first);
        check(first == E_FAIL && h.output.samplers_[4].biased && h.output.samplers_[4].saved_known && h.output.samplers_[4].saved_bias == held_value,
              "a failed restore keeps the obligation and the trusted saved value");
        first = S_OK; h.output.restore_mip_bias_stage(4, &first);
        check(SUCCEEDED(first) && h.device.sampler_calls == 1 && h.output.counters_.mip_bias_restores == 1, "no second attempt in the same frame");
        h.output.release_mip_bias_retry_bound();
        h.device.sampler_calls = 0; h.device.sampler_results[0] = S_OK;
        h.output.restore_mip_bias_stage(4, &first);
        check(SUCCEEDED(first) && owns_nothing(h.output, 4) && h.device.sampler_value[4] == held_value && h.output.counters_.mip_bias_restores == 2,
              "the next frame's restore puts the saved value back and clears the obligation");
    }

    // A failed restore whose application write is accepted still adopts the value.
    scenario(); { Harness h; arm(h); hold_bias(h, 2);
        h.device.sampler_results[0] = E_OUTOFMEMORY;
        check(hook_set_sampler_state(h, 2, D3DSAMP_MIPMAPLODBIAS, app_value) == S_OK, "accepted write after failed restore");
        check(h.output.motion_state_lost_ && h.output.motion_state_error_ == E_OUTOFMEMORY, "restore failure still latched");
        check(!h.output.composition_state_lost_, "an ineffective composition is not quarantined");
        check(h.output.samplers_[2].saved_known && h.output.samplers_[2].saved_bias == app_value,
              "the accepted application value replaces the invalidated saved value");
        check(owns_nothing(h.output, 2), "no owned bias after a failed restore");
    }

    // Another stage, another type, disabled and recording: no pre-restore.
    scenario(); { Harness h; arm(h); hold_bias(h, 0); hold_bias(h, 4);
        const int plain[] = {BoundaryBefore, CpuNative, BoundaryAfter};
        for (const auto type : {D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE}) {
            cpu_event_count = 0;
            hook_set_sampler_state(h, 0, type, 1);
            check(events_are(plain, 3), "another sampler type never restores");
        }
        cpu_event_count = 0;
        hook_set_sampler_state(h, 2, D3DSAMP_MIPMAPLODBIAS, app_value);
        check(events_are(plain, 3), "an unheld stage never restores");
        check(h.output.sampler_biased_mask_ == ((1u << 0) | (1u << 4)), "both held stages remain held");
        check(h.output.counters_.mip_bias_restores == 0, "no ordinary restores so far");
        h.output.enabled_ = false; cpu_event_count = 0;
        hook_set_sampler_state(h, 0, D3DSAMP_MIPMAPLODBIAS, app_value);
        check(events_are(plain, 3) && h.output.counters_.mip_bias_restores == 0, "disabled route never restores");
        h.output.enabled_ = true; h.output.shadow_.recording = true; cpu_event_count = 0;
        hold_bias(h, 0);
        hook_set_sampler_state(h, 0, D3DSAMP_MIPMAPLODBIAS, app_value);
        check(events_are(plain, 3) && h.output.counters_.mip_bias_restores == 0, "recording never restores");
        h.output.shadow_.recording = false; cpu_event_count = 0; h.device.sampler_calls = 0; // the device model accepts four calls
        hook_set_sampler_state(h, 0, D3DSAMP_MIPMAPLODBIAS, app_value);
        check(h.output.counters_.mip_bias_restores == 1 && quiet(h.output), "only the written held stage is restored");
        check(h.output.sampler_biased_mask_ == (1u << 4) && h.output.samplers_[4].biased,
              "the unwritten held stage keeps its bias");
    }

    // The ordinary sampler path: integer work only, one native call, no restore.
    scenario(); { Harness h; arm(h);
        unsigned natives = 0;
        for (DWORD stage = 0; stage < sampler_stage_count + 1; ++stage)
            for (const auto type : {D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE, D3DSAMP_MIPMAPLODBIAS}) {
                cpu_event_count = 0;
                h.device.sampler_calls = 0;
                hook_set_sampler_state(h, stage, type, 1);
                const int plain[] = {BoundaryBefore, CpuNative, BoundaryAfter};
                check(events_are(plain, 3) && h.device.sampler_calls == 1, "one native call, no CPU preservation");
                ++natives;
            }
        check(natives == 51 && h.output.counters_.mip_bias_restores == 0 && h.output.mip_bias_total_restores_ == 0,
              "the unbiased sampler path performs zero restores");
        check(quiet(h.output), "the unbiased sampler path latches nothing");
    }
}

int main() {
    pure_contract(); capability_contract(); draw_helpers(); sampler_transaction();
    std::printf("linear_cutout_contract scenarios=%d checks=%d failures=%d\n",scenarios,checks,failures);
    return failures ? 1 : 0;
}
'''


class LinearCutoutContractTests(unittest.TestCase):
    def test_actual_contract_and_runtime_helpers(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        states_start = source.index('constexpr D3DRENDERSTATETYPE shadow_states[')
        states_end = source.index('const char* scene_end_source_name', states_start)
        selected = source[states_start:states_end] + '\n' + '\n\n'.join(
            extract_function(source, signature) for signature in (
                'void MotionOutput::probe_cutout_caps(',
                'bool MotionOutput::cutout_arm_configured(',
                'void MotionOutput::release_mip_bias_retry_bound(',
                'bool MotionOutput::cutout_draw_state(',
                'void MotionOutput::mark_cutout_candidate(',
                'void MotionOutput::render_state_failed(',
                'void MotionOutput::sampler_state_failed(',
                'void MotionOutput::set_sampler_state(',
                'void MotionOutput::restore_mip_bias_stage(',
                'void MotionOutput::before_set_sampler_state(',
                'void MotionOutput::report_mip_bias_game_write_failure(',
            )
        )
        with tempfile.TemporaryDirectory(prefix='x3-linear-cutout-contract-') as temporary:
            directory = Path(temporary)
            source_path = directory / 'linear_cutout_contract.cpp'
            source_path.write_text(PREFIX + '\n' + selected + '\n' + MAIN)
            executable = directory / 'linear_cutout_contract'
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT), str(source_path), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stderr, '')
            self.assertRegex(run.stdout, r'^linear_cutout_contract scenarios=\d+ checks=\d+ failures=0\n$')
            print(run.stdout.strip())

    def test_failure_notifications_keep_native_boundary_and_history_union(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        for name, pre, success, failed in (
                ('set_render_state', 'before_set_render_state(state)',
                 'set_render_state(state,value)', 'render_state_failed(state)'),
                ('set_sampler_state', 'before_set_sampler_state(stage,type)',
                 'set_sampler_state(stage,type,value)', 'sampler_state_failed(stage,type)')):
            body = extract_function(capture, 'HRESULT WINAPI ' + name + '(')
            self.assertLess(body.index('LightCallBoundary cpu;'), body.index('PlainHookGuard lock;'))
            # The pre-call (which may itself issue a native restore) must run
            # before the CPU boundary hands the FPU state to the application's
            # original setter, never between the boundary and the native call.
            self.assertIn('ctx.motion_output.' + pre + ';', body)
            self.assertLess(body.index('ctx.motion_output.' + pre + ';'), body.index('cpu.before_original();'))
            self.assertLess(body.index('cpu.before_original();'), body.index('HRESULT hr='))
            self.assertLess(body.index('cpu.after_original();'), body.index('if(SUCCEEDED(hr))'))
            self.assertIn('if(SUCCEEDED(hr))ctx.motion_output.' + success + ';', body)
            self.assertIn('else ctx.motion_output.' + failed + ';', body)
            self.assertIn('return hr;', body)
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        union = source.index('if (cutout::unavailable(')
        self.assertLess(source.index('in.reactive_policy = renderer::ReactivePolicy::SupplementalMaskWithDepthSentinel;'), union)
        self.assertIn('in.reactive_policy = renderer::ReactivePolicy::Unavailable;', source[union:union+450])
        temporal = (ROOT / 'src/renderer/temporal_pass.cpp').read_text()
        self.assertIn('if(reactive_policy_!=ReactivePolicy::Unavailable)history_.completed();', temporal)
        gate = extract_function(source, 'void MotionOutput::evaluate_draw(')
        # 7f23195 (sun-lane refusal buckets) records the cutout verdict the
        # chain computed in cutout_ok; the arm and its order are unchanged.
        self.assertIn('test == 1 && color == 7 && shadow_.cutout_pair && (cutout_ok = cutout_draw_state())', gate)
        reset = extract_function(source, 'void MotionOutput::before_reset(')
        self.assertIn('cutout_caps_ = cutout::Capability::Pending', reset)
        self.assertIn('cutout_reset_pending_ = true', reset)

    def test_retry_is_only_at_hdr_latch_and_lifecycle_boundaries(self):
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        begin = extract_function(source, 'void MotionOutput::begin_redirect(')
        self.assertLess(begin.index('hdr_state_ = HdrState::Active'), begin.index('probe_cutout_caps();'))
        self.assertIn('probe_cutout_caps(true);', extract_function(source, 'void MotionOutput::attach('))
        self.assertIn('probe_cutout_caps(true);', extract_function(source, 'void MotionOutput::after_reset('))
        for signature in ('MotionRoute MotionOutput::before_draw(',
                          'void MotionOutput::evaluate_draw(',
                          'bool MotionOutput::cutout_arm_configured(',
                          'bool MotionOutput::cutout_draw_state(',
                          'void MotionOutput::mark_cutout_candidate('):
            self.assertNotIn('probe_cutout_caps(', extract_function(source, signature))


if __name__ == '__main__':
    unittest.main()
