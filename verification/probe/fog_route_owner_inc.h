// Synthetic owner inputs only. Every fog method is compiled unchanged below.
#include "fog_sector_policy.h"
#include "fog_card_policy.h"
#include "fog_card_mask.h"
#include "fog_card_match.h"
#ifndef X3M_ROUTE_BRIDGE_BASELINE
#include "fog_prefill.h" // 4ff60c8a members
#include "../renderer/gpu_sync_timing_core.h"
#endif
#include "shadow_replay_projection.h"
#include "sun_shadow_apply_pass.h"
#include <cstdarg>
#ifndef X3M_ROUTE_BRIDGE_BASELINE
#include "fog_density_cache.h"
#endif
namespace x3 { namespace temporal { enum class AgxDecode {none,gamma22}; } }
namespace x3m {
namespace sun_light_poll { enum class Status {Ok};struct Sample {Status status=Status::Ok;std::int32_t colour[3]{256,256,256};};inline const char* status_name(Status){return "synthetic";} }
// The fog shadow-pass A/B witness reads back the last frame row and toggle row the production fragment logged.
inline char last_frame_row[2048]{},last_toggle_row[512]{},last_motes_row[512]{};inline unsigned frame_rows=0,toggle_rows=0,motes_rows=0;
inline void log(const char* format,...){char line[2048];va_list args;va_start(args,format);std::vsnprintf(line,sizeof line,format,args);va_end(args);std::puts(line);
    if(!std::strncmp(line,"volumetric_fog_frame ",21)){++frame_rows;std::snprintf(last_frame_row,sizeof last_frame_row,"%s",line);}
    else if(!std::strncmp(line,"fog_shadow_pass_toggle ",23)){++toggle_rows;std::snprintf(last_toggle_row,sizeof last_toggle_row,"%s",line);}
    else if(!std::strncmp(line,"fog_dust_motes_toggle ",22)){++motes_rows;std::snprintf(last_motes_row,sizeof last_motes_row,"%s",line);}}
template<class T>void release(T*& value){if(value)value->Release();value=nullptr;}
constexpr unsigned GetDisplayMode=8,GetRenderTarget=38,SetRenderState=57,GetStreamSourceFreq=103;
using GetDisplayModeFn=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,D3DDISPLAYMODE*);
using GetRenderTargetFn=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9**);
using GetStreamFreqFn=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT*);
using SetRenderStateFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD);
constexpr float projection_default_m22=1.00001f,projection_default_m32=-.100001f;
#ifdef X3M_ROUTE_BRIDGE_BASELINE
// The pinned baseline root (6f16dbf6, parent of the stored-density wiring 39c98242) predates the
// rename to projection_default_* (7c8e0462); its fragment reads the old names. Same values.
constexpr float ao_default_m22=projection_default_m22,ao_default_m32=projection_default_m32;
#endif
enum class TaaInvalidateSite {FogTransition,StateLost};enum class HdrState {Active,Off};
struct MotionDrawCall {bool indexed=true,user_memory=false;D3DPRIMITIVETYPE topology=D3DPT_TRIANGLELIST;unsigned primitives=2,vertex_count=4;};
struct MotionRoute {FogCardMask fog_card_mask{};HRESULT preparation_error=S_OK,submission_error=S_OK;bool submit=true;};
struct MotionOutput {
    IDirect3DDevice9* device_=nullptr;void* const* native_=nullptr;D3DCAPS9 caps_{};
    struct Shadow {bool recording=false,fog_card_pair=true;std::uintptr_t stream0=1,indices=1;
        bool declaration_stream0_only=true;unsigned stream0_stride=24,position_offset=0,position_type=16;
        std::uint64_t declaration=0x0cdf6a8c884ad955ull;}shadow_;
    struct Counts {bool filled=true,cut=false;unsigned jitter_index=0;struct Taa {bool attempted=false;}taa;}counters_;
    struct SunFrame {bool failed=false,published=true;}sun_frame_;
    bool taa_enabled_=true,taa_failed_=false,main_msaa_=false,jitter_active_=true,composition_state_lost_=false,motion_state_lost_=false;
    bool depth_enabled_=true,sun_lane_failed_=false,composition_busy_=false,scene_open_=false,cut_finished_=false;
    HRESULT motion_state_error_=S_OK;
    HdrState hdr_state_=HdrState::Active;
    struct Owner {IDirect3DSurface9* surface=nullptr;IDirect3DSurface9* target()const{return surface;}}owner_;
    Owner* hdr_=&owner_;IDirect3DSurface9* depth_surface_=nullptr;
    unsigned active_queries_=0,target_width_=64,target_height_=48;
    renderer::CameraState camera_scene_{};float jitter_[2]{};
    struct HdrConfig {x3::temporal::AgxDecode decode=x3::temporal::AgxDecode::gamma22;}hdr_config_;
    // No shaft-map publication in this bridge; the real parameter helper still
    // executes its normal no-current-map path with typed owner inputs.
    struct NoMaps {
        struct Retained {renderer::ShadowReplayBasis basis{};std::uint64_t frame=0;};
        const Retained* retained(unsigned)const{return nullptr;}
        IDirect3DTexture9* map_texture(unsigned)const{return nullptr;}
        unsigned size(unsigned)const{return 0;}
    } no_maps_;
    renderer::ShadowCascadeSet depth_cascades_{};NoMaps* depth_replay_=&no_maps_;
    float sun_apply_bias_units_=0,sun_apply_clamp_texels_=0;
    struct PointSun {const double* grid_anchor(unsigned)const{return nullptr;}}point_sun_;
    sun_light_poll::Sample point_sun_sample_{};
    bool depth_cascades_on()const{return true;}const float* cascade_sun(unsigned)const {static const float sun[4]{0,0,1,0};return sun;}
    std::unique_ptr<renderer::FogPass> fog_;renderer::FogSectorLatch fog_latch_{};FogSectorFrame fog_sector_{};FogCardPolicy fog_cards_{};
    bool fog_requested_=true,fog_enabled_=true,fog_everywhere_=false,fog_timing_=false,fog_disabled_=false,fog_attach_failed_=false,fog_sun_fallback_logged_=false;
    float fog_strength_=.02f,fog_anisotropy_=.3f;
    bool fog_cards_replace_=true,fog_card_ready_checked_=false,fog_card_ready_=false;
    const char* fog_card_refusal_=nullptr;const char* fog_card_last_refusal_=nullptr;const char* fog_card_ready_reason_=nullptr;bool fog_card_refusal_ready_=false; // run273 members (motion_output.h)
    bool fog_families_checked_=false; // added by dcf3728b (motion_output.h fog_families_checked_)
    unsigned fog_failures_=0,fog_logs_=0,fog_card_mode_=0,fog_card_logs_=0;
    std::uint64_t id_=1,frame_=0,generation_=0,fog_frame_=~std::uint64_t(0),fog_applied_frames_=0;
    std::uint64_t fog_card_states_log_frame_=0; // run278 member (motion_output.h): the refused state vector row's spacing
    std::uint64_t fog_card_logged_frame_=0,fog_transition_frame_=~std::uint64_t(0),fog_card_last_report_=~std::uint64_t(0),fog_card_observed_total_=0,fog_card_suppressed_total_=0,fog_card_refused_total_=0;
    const char* fog_last_reason_="";const char* fog_card_fault_reason_="none";
#ifndef X3M_ROUTE_BRIDGE_BASELINE
    // Stored-density range: the production members, verbatim defaults.
    bool fog_density_requested_=false,fog_density_refused_=false,fog_density_prepared_=false,fog_density_camera_valid_=false;
    bool fog_density_config_logged_=false,fog_density_ready_logged_[2]{},fog_shadow_pass_refused_logged_=false;
    // The grid pass A/B (fog-shadow-pass.md): the production members, verbatim defaults; set both as
    // configure_volumetric_fog_shadow_pass(true) does to launch with the pass.
    bool fog_shadow_pass_launch_=false;const char* fog_grid_last_march_="none";std::uint64_t fog_grid_logged_frame_=0;
    static constexpr std::uint64_t fog_grid_change_frames=60;static constexpr unsigned fog_grid_change_cap=16;unsigned fog_grid_change_logs_=0;
    int volumetric_fog_shadow_pass_toggle()noexcept;
    // The dust motes' A/B (fog-dust-motes.md): the production members, verbatim defaults; set the launch flag and the
    // config's motes as configure_volumetric_fog_dust_motes does to launch with the option.
    bool fog_dust_motes_launch_=false,fog_motes_refused_logged_=false,fog_motes_drawn_=false;long long fog_motes_epoch_qpc_=0;
    int volumetric_fog_dust_motes_toggle()noexcept;
    unsigned fog_density_logs_=0,fog_handover_logs_=0; // 4ff60c8a members (motion_output.h)
    bool fog_prefill_launch_=false;fog_prefill::Record fog_prefill_{};unsigned fog_prefill_logs_=0;bool fog_prefill_refused_logged_=false;
    fog_prefill::Decision fog_prefill_confirm(const FogSectorFrame&,const sector_background::Sample&)noexcept;
    void volumetric_fog_prefill(const fog_prefill::Result&,std::uint64_t)noexcept;
    gpu_sync_timing::Marks* gpu_sync_=nullptr;
    std::uint64_t fog_density_sample_frame_=~std::uint64_t(0),fog_density_key_=0;
    std::uint32_t fog_density_ready_sector_=0,fog_density_ready_id_=0; // run273 members (motion_output.h)
    long long fog_density_epoch_qpc_=0,fog_density_sample_qpc_=0;double fog_density_camera_[3]{};
    static constexpr unsigned fog_density_gap_ms=500;
    renderer::FogDensityConfig fog_density_config_{};
    void prepare_volumetric_fog_density(UINT,UINT)noexcept;void fog_density_epoch(const char*)noexcept;
    bool fog_density_active()const noexcept{return fog_density_requested_&&!fog_density_refused_;}
    // MotionOutput::release_resources' fog statements (the device release path).
    void release_fog(){if(fog_){taa_call([&]{fog_->detach();});fog_.reset();fog_frame_=~std::uint64_t(0);}
        fog_density_refused_=fog_density_prepared_=fog_density_camera_valid_=fog_density_config_logged_=false;}
#else
    static constexpr bool fog_density_requested_=false;
#endif
    unsigned invalidations=0;bool history_valid=false; // endpoint request witness, not a TemporalPass GPU history claim
    void invalidate_taa(TaaInvalidateSite){++invalidations;history_valid=false;}
    void invalidate_render_states(){}bool scene_bound()const{return true;}
    D3DFORMAT lane_depth_format()const{return D3DFMT_A32B32G32R32F;}
    template<class F>F native(unsigned slot)const{return reinterpret_cast<F>(native_[slot]);}
    template<class F,class...A>HRESULT direct_call(unsigned slot,A...a){return native<F>(slot)(device_,a...);}
    template<class F>void taa_call(F&& f){call_preserved(f);}
    // These getters deliberately use documented native state, not a mirror.
    long state_field(unsigned n){D3DRENDERSTATETYPE state=D3DRS_FORCE_DWORD;
        switch(n){case 0:state=D3DRS_ZENABLE;break;case 1:state=D3DRS_ZWRITEENABLE;break;case 2:state=D3DRS_ALPHATESTENABLE;break;case 3:state=D3DRS_ALPHABLENDENABLE;break;case 4:state=D3DRS_COLORWRITEENABLE;break;case 29:state=D3DRS_STENCILENABLE;break;case 30:state=D3DRS_CULLMODE;break;case 31:state=D3DRS_FILLMODE;break;}
        DWORD value=0;return SUCCEEDED(device_->GetRenderState(state,&value))?long(value):-1;}
    DWORD blend_values[4]{};bool blend_known(unsigned n){constexpr D3DRENDERSTATETYPE states[]{D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE};return SUCCEEDED(device_->GetRenderState(states[n],&blend_values[n]));}
    long composition_blend_field(unsigned n)const{return long(blend_values[n]);}
    const char* fog_frame_prerequisite()const noexcept;
    const char* fog_frame_parameters(renderer::FogFrame&,float,bool&)noexcept;
    void fog_transition_invalidate()noexcept;void fog_card_transition(unsigned)noexcept;void fault_fog_cards(const char*)noexcept;
    bool attach_volumetric_fog()noexcept;void prepare_volumetric_fog_targets(UINT,UINT)noexcept;
    void volumetric_fog_sector_sample(std::uint64_t,const sector_background::Sample&)noexcept;
    void reconcile_volumetric_fog(const renderer::FogFrame&,const renderer::FogResult&,HRESULT)noexcept;
    void complete_volumetric_fog(const char*,HRESULT,const renderer::FogResult&)noexcept;
    void volumetric_fog_begin_frame()noexcept;void prepare_fog_card(const MotionDrawCall&,MotionRoute&)noexcept;void finish_fog_card(MotionRoute&,HRESULT)noexcept;
    int volumetric_fog_toggle()noexcept;int volumetric_fog_step()noexcept;
    void disable_volumetric_fog(const char*,HRESULT)noexcept;void run_volumetric_fog()noexcept;
};
#include "fog_route_methods_inc.h"
} // namespace x3m
