#pragma once
// One authored family atlas, fixed 24-step half-resolution march and compatible
// full-resolution composite/repair. Device methods use the supplied native table;
// resource methods use documented COM interfaces. Caller serializes all methods.
#include <cstdint>
#include <vector>
#include <d3d9.h>
#include "fog_pass_math.h"
#include "fog_volume_math.h"
#include "fog_field_assets.h"
namespace x3m::renderer {
struct FogCaps {
    bool enabled=false;
    const char* reason="detached";
    HRESULT formats=S_FALSE,programs=S_FALSE;
    unsigned largest_program_slots=0;
};
// Retained only for source compatibility with the previous integration. Spatial
// fog does not consume cascades or a mean-sky history.
struct FogCascadeInput { IDirect3DTexture9* map=nullptr; float rows[12]{}; float bias=0; bool valid=false; };
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
    unsigned count=0; FogCascadeInput cascades[fog_cascade_max]{};
    FogParams params{};
    fog_field::Profile profile=fog_field::Profile::None;
    std::uint32_t recipe_id=0;
    std::uint64_t field_generation=0;
    // Positive provenance; D3D9 cannot query scene/recording/query ownership.
    bool main_target=false,linear_depth_current=false,caller_scene_known=false;
    bool caller_scene_open=true,caller_stateblock_recording=false,caller_queries_idle=false;
};
enum class FogStage : unsigned {
    None,Validate,Targets,Block,Capture,Normalize,Scene,March,Copy,SkyLevel,SkyReduce,Composite,EndScene,Restore,
    Field,CloseScene,ReopenScene,RecoverScene
};
struct FogResult {
    HRESULT operation=S_FALSE,restore=S_FALSE,scene_recovery=S_FALSE;
    FogStage failed=FogStage::None;
    bool applied=false; // successful composite draw; not a rollback guarantee
    bool scene_known=false,scene_open=false,scene_write_started=false;
    bool caller_state_restored=false,route_poisoned=false;
    bool sky_updated=false; unsigned cascades_bound=0; // legacy diagnostics, always zero
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
    HRESULT execute(const FogFrame&,FogResult*) noexcept;
    void before_reset() noexcept; void after_reset(HRESULT) noexcept; void detach() noexcept;
    bool resources_ready(UINT w,UINT h) const noexcept {
        return caps_.enabled&&!reset_pending_&&active_profile_!=fog_field::Profile::None&&atlas_&&block_&&
            w&&h&&w==width_&&h==height_&&lit_surface_&&scratch_surface_;
    }
    bool resources_ready(UINT w,UINT h,fog_field::Profile p,std::uint32_t recipe,std::uint64_t generation) const noexcept {
        return resources_ready(w,h)&&p==active_profile_&&recipe==field_recipe_&&generation==field_generation_;
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
#endif
private:
    struct SavedState;
    template<class Fn> Fn call(unsigned slot) const noexcept { ++calls_; return reinterpret_cast<Fn>(vtable_[slot]); }
    HRESULT normalize() noexcept;
    HRESULT quad(UINT,UINT) noexcept;
    HRESULT bind_target(IDirect3DSurface9*,UINT,UINT,IDirect3DPixelShader9*) noexcept;
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
};
} // namespace x3m::renderer
