#pragma once
// One authored family atlas, fixed 24-step half-resolution march and compatible
// full-resolution composite/repair. Device methods use the supplied native table;
// resource methods use documented COM interfaces. Caller serializes all methods.
#include <cstdint>
#include <vector>
#include <d3d9.h>
#include "fog_pass_math.h"
#include "fog_look_math.h"
#include "fog_volume_math.h"
#include "fog_field_assets.h"
namespace x3m::fog { class DensityCache; }
namespace x3m::renderer {
struct FogCaps {
    bool enabled=false;
    const char* reason="detached";
    HRESULT formats=S_FALSE,programs=S_FALSE;
    unsigned largest_program_slots=0;
};
// Stored-density path (docs/architecture/fog-density-runtime-integration.md). Unreachable
// unless `enabled`: no worker thread, allocation, program or device call exists before the
// first enabled prepare_density. A capability refusal leaves the legacy path untouched.
struct FogDensityConfig {
    bool enabled=false;
    std::uint64_t sector_key=0; std::uint32_t recipe=0; // cache identity with world_offset
    double world_offset[3]{};   // per-sector field translation O_s, render units
    float sigma=0;              // family extinction before strength and readiness
    float chroma[3]{1,1,1};     // family mean chroma
    unsigned upload_budget_bytes=0; // per prepare_density; 0 selects 8 tiles (1,065,024 B)
    FogLookTuning look{};       // look presets L1-L3; read once at init by the caller
};
struct FogDensityStatus {
    bool available=false; const char* reason="off";
    bool looks=false; const char* look_reason="off"; // L1-L3 programs exist; otherwise every frame draws L0
    float ready_fine=0,ready_far=0;          // 90-frame ramps: lambda weight, density weight
    unsigned upload_bytes=0,upload_rects=0;  // last prepare_density
    std::uint64_t upload_bytes_total=0,upload_rects_total=0,nodes_generated=0,worker_busy_us=0,missed_locks=0;
};
// Borrowed only during execute. Rows map current view coordinates to the exact
// retained replay basis; frame stamps prohibit the surface lane's older far map.
struct FogCascadeInput {
    IDirect3DTexture9* map=nullptr; float rows[12]{}; float bias=0; bool valid=false;
    std::uint64_t frame=~std::uint64_t(0);
};
constexpr unsigned fog_cascade_max=3;
struct FogParams {
    // Already corrected exactly once for raster jitter and quad pixel centres.
    float m00=0,m11=0,m20=0,m21=0,m22=0,m32=0;
    FogWorldBasis world{};
    float density_scale=0; // 0 is off; integration maps S/.02, maximum 5x.
    float anisotropy=fog_anisotropy_default;
    float sun_radiance[3]{fog_volume_pi,fog_volume_pi,fog_volume_pi};
    float decode_exponent=2.2f; // supported scene encodings: 1 or 2.2
    // Legacy integration fields, ignored by the spatial pass.
    float tau_max=0,radius=fog_radius_default,margin=1,sun_view[3]{};
    unsigned jitter_index=0; bool update_sky=true; float sky_blend=.25f;
};
// Shared admission/execute validation; caller supplies the ordinary full CPU boundary.
bool fog_valid_params(const FogParams&) noexcept;
struct FogFrame {
    IDirect3DTexture9* depth_share=nullptr; // current RGBA32F RT2: .r class, .b positive view z
    IDirect3DSurface9* target=nullptr; // owning non-MSAA FP16 scene
    UINT width=0,height=0;
    std::uint64_t frame=0;
    unsigned count=0; FogCascadeInput cascades[fog_cascade_max]{};
    FogParams params{};
    fog_field::Profile profile=fog_field::Profile::None;
    std::uint32_t recipe_id=0;
    std::uint64_t field_generation=0;
    // Positive provenance; D3D9 cannot query scene/recording/query ownership.
    bool main_target=false,linear_depth_current=false,caller_scene_known=false;
    bool caller_scene_open=true,caller_stateblock_recording=false,caller_queries_idle=false;
    // Stored-density transaction (march, composite, repair). Requires a successful
    // prepare_density in this frame. camera_world is this frame's double camera; the proxy
    // posts the previous frame's camera to prepare_density (the camera is read after the owner
    // latch), which is safe because execute re-checks residency for camera_world itself.
    bool density=false; double camera_world[3]{};
    // Stored-density look preset 0..3 and the TAA jitter sequence index (L3 sample offset). Constants and a
    // prebuilt program only; a look whose programs are unavailable draws L0 (FogResult::look).
    unsigned look=0,look_phase=0;
};
enum class FogStage : unsigned {
    None,Validate,Targets,Block,Capture,Normalize,Scene,March,Copy,SkyLevel,SkyReduce,Composite,EndScene,Restore,
    Field,CloseScene,ReopenScene,RecoverScene,Repair
};
struct FogResult {
    HRESULT operation=S_FALSE,restore=S_FALSE,scene_recovery=S_FALSE;
    FogStage failed=FogStage::None;
    bool applied=false; // successful composite draw; not a rollback guarantee
    bool scene_known=false,scene_open=false,scene_write_started=false;
    bool caller_state_restored=false,route_poisoned=false;
    bool sky_updated=false; unsigned cascades_bound=0; // actual current maps admitted
    unsigned look=0; // preset actually drawn
    unsigned device_calls=0; // native methods + block Capture/Apply; excludes Releases/resource validation
    IDirect3DTexture9* lit=nullptr; // borrowed FP16 (S.rgb,T), invalidated by resize/Reset/detach
    UINT half_width=0,half_height=0;
};
class FogPass {
public:
    FogPass()=default; ~FogPass();
    FogPass(const FogPass&)=delete; FogPass& operator=(const FogPass&)=delete;
    HRESULT attach(IDirect3DDevice9*,void* const* native,const D3DCAPS9&,D3DFORMAT adapter_format) noexcept;
    const FogCaps& caps() const noexcept { return caps_; }
    // Preparation must run outside any draw/suppression bracket. One decoded CPU
    // cache slot; publication happens only after upload. None disarms immediately.
    HRESULT prepare_field(void* module,fog_field::Profile) noexcept;
    HRESULT prepare_targets(UINT width,UINT height) noexcept;
    HRESULT prepare(UINT width,UINT height) noexcept { return prepare_targets(width,height); }
    // Outside any draw/suppression bracket, once per frame while the density path is wanted:
    // starts the worker on first use, posts the camera, uploads at most the byte budget and 64
    // rectangles of committed tile regions (SYSTEMMEM -> DEFAULT UpdateSurface), advances the ramps.
    // Never waits for the worker. S_FALSE when disabled, D3DERR_NOTAVAILABLE when refused.
    HRESULT prepare_density(const FogDensityConfig&,const double camera_world[3],std::uint64_t frame) noexcept;
    void invalidate_density() noexcept; // load or sector change without a key change
    // This frame's prepare_density succeeded and a ray from `camera` may be drawn: far ramp
    // above zero and the far need box resident. Pure CPU, no lock, no device call.
    bool density_drawable(const double camera[3]) const noexcept;
    // Extinction and mean chroma of the prepared family: tracked offline constants
    // (fog_family_chroma_inc.h, sum rgb / sum density of the pinned packet). False without a
    // decoded field or for a profile the table does not know.
    bool field_family(float chroma[3],float* sigma) const noexcept;
    const FogDensityStatus& density_status() const noexcept { return density_status_; }
    // Caller contract for the worker's lifetime:
    //  - MotionOutput::release_resources (the device release path, never under the loader lock)
    //    calls detach(), which joins the worker and releases every density resource. If the join
    //    has to give up (mutex unavailable for 250 ms) the cache is leaked whole, never freed;
    //  - the proxy's DllMain DLL_PROCESS_DETACH, and nothing else, calls abandon_density_worker()
    //    (x3m::abandon_fog_density_workers) before the CRT destroys the static device map: the OS
    //    has already ended the worker, possibly inside its condition variable, so this never
    //    joins, detaches, notifies or locks, and leaks the cache with its thread handle;
    //  - the first worker start pins the module (DensityCache::start), so a dynamic FreeLibrary
    //    cannot unmap the worker's code.
    void abandon_density_worker() noexcept;
    HRESULT execute(const FogFrame&,FogResult*) noexcept;
    // detach joins the worker: call it from the device's release path, not under the loader lock.
    void before_reset() noexcept; void after_reset(HRESULT) noexcept; void detach() noexcept;
    bool resources_ready(UINT w,UINT h) const noexcept {
        return caps_.enabled&&!reset_pending_&&active_profile_!=fog_field::Profile::None&&atlas_&&block_&&
            w&&h&&w==width_&&h==height_&&lit_surface_&&scratch_surface_;
    }
    bool resources_ready(UINT w,UINT h,fog_field::Profile p,std::uint32_t recipe,std::uint64_t generation) const noexcept {
        return resources_ready(w,h)&&p==active_profile_&&recipe==field_recipe_&&generation==field_generation_;
    }
    bool density_ready(UINT w,UINT h) const noexcept {
        return caps_.enabled&&!reset_pending_&&density_&&density_march_&&density_composite_&&density_repair_&&density_atlas_surface_[0]&&density_atlas_surface_[1]&&
            block_&&w&&h&&w==width_&&h==height_&&lit_surface_&&scratch_surface_;
    }
    fog_field::Profile field_profile() const noexcept { return active_profile_; }
    std::uint32_t field_recipe() const noexcept { return active_profile_==fog_field::Profile::None?0:field_recipe_; }
    std::uint64_t field_generation() const noexcept { return active_profile_==fog_field::Profile::None?0:field_generation_; }
    bool reset_pending() const noexcept { return reset_pending_; }
    unsigned references() const noexcept;
    unsigned allocations() const noexcept { return allocations_; }
    bool sky_seeded() const noexcept { return false; }
#ifdef X3M_FOG_PASS_FIXTURE
    IDirect3DTexture9* fixture_sky() const noexcept { return nullptr; }
    IDirect3DTexture9* fixture_sky_level() const noexcept { return nullptr; }
    IDirect3DSurface9* fixture_st() const noexcept { return lit_surface_; }
    std::size_t fixture_cpu_bytes() const noexcept { return atlas_bytes_.size()*sizeof(std::uint16_t); }
    IDirect3DTexture9* fixture_density_atlas(unsigned level) const noexcept { return density_atlas_[level]; }
    const fog::DensityCache* fixture_density_cache() const noexcept { return density_; }
#endif
private:
    struct SavedState;
    template<class Fn> Fn call(unsigned slot) const noexcept { ++calls_; return reinterpret_cast<Fn>(vtable_[slot]); }
    HRESULT normalize(bool density) noexcept;
    HRESULT density_resources() noexcept;
    HRESULT density_uploads(unsigned budget) noexcept;
    void release_density_default() noexcept;
    HRESULT quad(UINT,UINT) noexcept;
    HRESULT bind_target(IDirect3DSurface9*,UINT,UINT,IDirect3DPixelShader9*,UINT samplers=7) noexcept;
    void release_targets() noexcept;
    void disarm_field() noexcept { active_profile_=fog_field::Profile::None; }
    IDirect3DDevice9* device_=nullptr; void* const* vtable_=nullptr; // borrowed
    FogCaps caps_{};
    IDirect3DStateBlock9* block_=nullptr;
    IDirect3DPixelShader9 *march_=nullptr,*composite_=nullptr;
    IDirect3DVertexShader9* quad_vs_=nullptr;
    IDirect3DVertexDeclaration9* quad_declaration_=nullptr;
    IDirect3DTexture9 *atlas_=nullptr,*lit_=nullptr,*scratch_=nullptr;
    IDirect3DSurface9 *lit_surface_=nullptr,*scratch_surface_=nullptr;
    std::vector<std::uint16_t> atlas_bytes_;
    fog_field::Profile cached_profile_=fog_field::Profile::None,active_profile_=fog_field::Profile::None;
    std::uint32_t field_recipe_=0; std::uint64_t field_generation_=0;
    float base_sigma_=0;
    UINT width_=0,height_=0,half_width_=0,half_height_=0,render_targets_=0,streams_=0,max_width_=0,max_height_=0;
    unsigned allocations_=0; mutable unsigned calls_=0;
    bool reset_pending_=false;
    // Stored-density path; everything below stays null/zero until an enabled prepare_density.
    fog::DensityCache* density_=nullptr;
    IDirect3DPixelShader9 *density_march_=nullptr,*density_composite_=nullptr,*density_repair_=nullptr;
    // Look variants, created with the base programs and never on the draw path: [0] FOG_LOOK 1 (L1), [1] FOG_LOOK 2 (L2, L3).
    IDirect3DPixelShader9 *look_march_[2]{},*look_repair_[2]{},*look_composite_=nullptr;
    IDirect3DTexture9 *density_staging_[2]{},*density_atlas_[2]{};
    IDirect3DSurface9 *density_staging_surface_[2]{},*density_atlas_surface_[2]{};
    FogDensityConfig density_config_{}; FogDensityStatus density_status_{};
    unsigned ps30_slots_=0; bool density_refused_=false;
};
} // namespace x3m::renderer
