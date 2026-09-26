#pragma once
// One authored family atlas, fixed 24-step half-resolution march and compatible
// full-resolution composite/repair. Device methods use the supplied native table;
// resource methods use documented COM interfaces. Caller serializes all methods.
#include <cstdint>
#include <vector>
#include <d3d9.h>
#include "fog_pass_math.h"
#include "fog_look_math.h"
#include "fog_mote_math.h"
#include "fog_volume_math.h"
#include "fog_field_assets.h"
#include "gpu_sync_timing_core.h"
#include "../fog/fog_handover.h"
namespace x3m::fog {
class DensityCache;
}
namespace x3m::renderer {
struct FogCaps {
    bool enabled = false;
    const char* reason = "detached";
    HRESULT formats = S_FALSE, programs = S_FALSE;
    unsigned largest_program_slots = 0;
};
// Stored-density path (docs/architecture/fog-density-runtime-integration.md). Unreachable
// unless `enabled`: no worker thread, allocation, program or device call exists before the
// first enabled prepare_density. A capability refusal leaves the legacy path untouched.
struct FogDensityConfig {
    bool enabled = false;
    std::uint64_t sector_key = 0;
    std::uint32_t recipe = 0;         // cache identity with world_offset
    double world_offset[3]{};         // per-sector field translation O_s, render units
    float sigma = 0;                  // family extinction before strength and readiness
    float chroma[3]{1, 1, 1};         // family mean chroma
    unsigned upload_budget_bytes = 0; // per prepare_density; 0 selects 8 tiles (1,065,024 B)
    FogLookTuning look{};             // the single look's tuning; read once at init by the caller
    // X3M_FOG_DUST_MOTES (docs/architecture/fog-dust-motes.md): motes.count > 0 is the launch option; dust_motes is its
    // on/off (a fixture seam; the Ctrl+Alt+F11 key was removed 2026-09-26), latched here. The mote programs and the
    // static DEFAULT VB/IB are created at prepare_density while it is on, never on a draw path; count 0 creates,
    // queries and draws nothing.
    FogMoteTuning motes{};
    bool dust_motes = false;
    // Cold-start hand-over (docs/architecture/fog-handover.md, "Implementation"; X3M_FOG_HANDOVER_STEP /
    // X3M_FOG_HANDOVER_COLDFILL, launcher default on): the far readiness steps to 1 when the far need box is
    // resident, and the far level fills its need box first and goes up in one whole-atlas latch. Off: legacy.
    bool handover_step = false, handover_coldfill = false;
    // X3M_FOG_MARCH_SCALE (docs/architecture/fog-gpu-cost.md, step C; launcher --fog-march-scale, default 4 since Run
    // 77 C2): the look's march spacing in full pixels, 4 (the default: a quarter-resolution march target, composite and
    // repair reading their samples 4 px apart) or 2 (the half-resolution march, the opt-out). prepare_density refuses
    // any other value; the march/repair/composite of the requested spacing and the quarter target are created there,
    // never on a draw path. 4 falls back to 2 (FogDensityStatus::march_scale_refused) when its programs or its target
    // could not be built. The look marches 40 far bins (fog_far_bins; the 24-bin variant and the visibility-grid shadow
    // pass were removed on 2026-09-25).
    unsigned march_scale = fog_march_scale_default;
};
struct FogDensityStatus {
    bool available = false;
    const char* reason = "off";
    float ready_fine = 0, ready_far = 0;         // 90-frame ramps: lambda weight, density weight
    unsigned upload_bytes = 0, upload_rects = 0; // last prepare_density
    std::uint64_t upload_bytes_total = 0, upload_rects_total = 0, nodes_generated = 0, worker_busy_us = 0,
                  missed_locks = 0;
    // The mote stage could not be built or drawn (capability, program, buffer, a failed draw): the fog keeps drawing
    // without it; sticky until detach. The proxy logs it once (fog_dust_motes_refused).
    const char* motes_refused = nullptr;
    // FogDensityConfig::march_scale 4 was asked but 2 draws: "program" (the set could
    // not be built; the working set keeps drawing, sticky) or "target" (the quarter target could not be created,
    // sticky). Null when the requested spacing draws. The proxy logs it once (fog_march_scale_refused).
    const char* march_scale_refused = nullptr;
    // The last completed cold start, due in the successful prepare_density of the frame it completed (logged once).
    fog::HandoverReport handover{};
};
// Borrowed only during execute. Rows map current view coordinates to the exact
// retained replay basis; frame stamps prohibit the surface lane's older far map.
struct FogCascadeInput {
    IDirect3DTexture9* map = nullptr;
    float rows[12]{};
    float bias = 0;
    bool valid = false;
    std::uint64_t frame = ~std::uint64_t(0);
};
constexpr unsigned fog_cascade_max = 3;
// The mote stage's per-frame report (fog-dust-motes.md section 4), written by execute only on a density frame that
// takes the stage (FogDensityConfig::dust_motes after prepare_density); `frame` is the FogFrame::frame it describes.
struct FogMoteReport {
    std::uint64_t frame = ~std::uint64_t(0);
    bool drawn = false;  // the indexed draw succeeded (FogResult::motes)
    bool streak = false; // previous basis valid: consecutive fog frame, |delta| <= R, rotation under 30 degrees
    unsigned count = 0, calls = 0; // N; device calls issued by the stage
    float shift_px = 0;          // |camera delta| / R x (H/2 x m11): the perpendicular displacement at the wrap radius
    const char* shadow = "none"; // sun visibility the motes read: in_march, none (no cascade)
};
struct FogParams {
    // Already corrected exactly once for raster jitter and quad pixel centres.
    float m00 = 0, m11 = 0, m20 = 0, m21 = 0, m22 = 0, m32 = 0;
    FogWorldBasis world{};
    float density_scale = 0; // 0 is off; integration maps S/.02, maximum 5x.
    float anisotropy = fog_anisotropy_default;
    float sun_radiance[3]{fog_volume_pi, fog_volume_pi, fog_volume_pi};
    float decode_exponent = 2.2f; // supported scene encodings: 1 or 2.2
    // Legacy integration fields, ignored by the spatial pass.
    float tau_max = 0, radius = fog_radius_default, margin = 1, sun_view[3]{};
    unsigned jitter_index = 0;
    bool update_sky = true;
    float sky_blend = .25f;
};
// Shared admission/execute validation; caller supplies the ordinary full CPU boundary.
bool fog_valid_params(const FogParams&) noexcept;
struct FogFrame {
    IDirect3DTexture9* depth_share = nullptr; // current RGBA32F RT2: .r class, .b positive view z
    IDirect3DSurface9* target = nullptr;      // owning non-MSAA FP16 scene
    UINT width = 0, height = 0;
    std::uint64_t frame = 0;
    unsigned count = 0;
    FogCascadeInput cascades[fog_cascade_max]{};
    FogParams params{};
    fog_field::Profile profile = fog_field::Profile::None;
    std::uint32_t recipe_id = 0;
    std::uint64_t field_generation = 0;
    // Positive provenance; D3D9 cannot query scene/recording/query ownership.
    bool main_target = false, linear_depth_current = false, caller_scene_known = false;
    bool caller_scene_open = true, caller_stateblock_recording = false, caller_queries_idle = false;
    // Stored-density transaction (march, composite, repair). Requires a successful
    // prepare_density in this frame. camera_world is this frame's double camera; the proxy
    // posts the previous frame's camera to prepare_density (the camera is read after the owner
    // latch), which is safe because execute re-checks residency for camera_world itself.
    bool density = false;
    double camera_world[3]{};
    // The TAA jitter sequence index of the look's shadow-shaft lookup offset; constants only.
    unsigned look_phase = 0;
    bool look_resolved = false; // look_resolved: TAA averages look_phase (shaft lookup offset on)
    double mote_seconds = 0; // the dust motes' drift clock in seconds (any epoch); read only when the mote stage draws
    bool mote_cut = false; // the caller's camera cut this frame (view switch, roll-only cut): the motes draw no streak
};
enum class FogStage : unsigned {
    None,
    Validate,
    Targets,
    Block,
    Capture,
    Normalize,
    Scene,
    March,
    Copy,
    SkyLevel,
    SkyReduce,
    Composite,
    EndScene,
    Restore,
    Field,
    CloseScene,
    ReopenScene,
    RecoverScene,
    Repair,
    Visibility,
    Motes,
    Census // Visibility: unused since the grid pass went (2026-09-25); values unchanged
};
struct FogResult {
    HRESULT operation = S_FALSE, restore = S_FALSE, scene_recovery = S_FALSE;
    FogStage failed = FogStage::None;
    bool applied = false; // successful composite draw; not a rollback guarantee
    bool scene_known = false, scene_open = false, scene_write_started = false;
    bool caller_state_restored = false, route_poisoned = false;
    bool sky_updated = false;
    unsigned cascades_bound = 0;          // actual current maps admitted
    bool motes = false;                   // the dust motes were drawn this frame (after the repair)
    unsigned device_calls = 0;            // native methods + block Capture/Apply; excludes Releases/resource validation
    IDirect3DTexture9* lit = nullptr;     // borrowed FP16 (S.rgb,T), invalidated by resize/Reset/detach
    UINT half_width = 0, half_height = 0; // lit's extent: the march target (half resolution, or quarter at march_scale
                                          // 4)
};
class FogPass {
public:
    FogPass() = default;
    ~FogPass();
    FogPass(const FogPass&) = delete;
    FogPass& operator=(const FogPass&) = delete;
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, D3DFORMAT adapter_format) noexcept;
    const FogCaps& caps() const noexcept { return caps_; }
    // Preparation must run outside any draw/suppression bracket. One decoded CPU
    // cache slot; publication happens only after upload. None disarms immediately.
    // A file family (fog_field::is_file_profile) whose packet cannot be read or decoded returns
    // field_row_disabled: the row is disabled, the next sector scan leaves native cards, and the
    // caller must not fault the session (a compiled resource failure still does).
    static constexpr HRESULT field_row_disabled = static_cast<HRESULT>(0x80040f4dL);
    HRESULT prepare_field(void* module, fog_field::Profile) noexcept;
    HRESULT prepare_targets(UINT width, UINT height) noexcept;
    HRESULT prepare(UINT width, UINT height) noexcept { return prepare_targets(width, height); }
    // Outside any draw/suppression bracket, once per frame while the density path is wanted:
    // starts the worker on first use, posts the camera, uploads at most the byte budget and 64
    // rectangles of committed tile regions (SYSTEMMEM -> DEFAULT UpdateSurface), advances the ramps.
    // Never waits for the worker. S_FALSE when disabled, D3DERR_NOTAVAILABLE when refused.
    HRESULT prepare_density(const FogDensityConfig&, const double camera_world[3], std::uint64_t frame) noexcept;
    void invalidate_density() noexcept; // load or sector change without a key change
    // R3 (fog-handover.md, "R3 implementation"): start the far fill of an identity found during a transit stall,
    // with the caller's hand-over switches (the proxy's, so a prefill before the first stored frame steps and
    // cold-fills too). No device call, never waits. When no stored frame has run yet the worker is created here, inside
    // the caller's resource-creation hook: two 4,260,096 B caches (zeroed), the job and scratch arrays, a thread and
    // the module pin, at most once per stall (a failed start sets prefill_refused() until the next prepare_density or
    // release_density_worker, and later polls allocate nothing). False: the path is refused, a Reset is pending, the
    // worker could not start, or the camera was not posted (a missed lock; retried at the next poll). The resident
    // identity is re-centred as a cold start.
    bool prefill_density(std::uint64_t sector_key, std::uint32_t recipe, const double world_offset[3],
                         const double camera[3], bool handover_step, bool handover_coldfill) noexcept;
    bool prefill_refused() const noexcept { return prefill_refused_; }
    // A worker a prefill started is not kept until device release when the attach is refused (8.5 MB): joins it.
    void release_density_worker() noexcept;
    // This frame's prepare_density succeeded and a ray from `camera` may be drawn: far ramp
    // above zero and the far need box resident. Pure CPU, no lock, no device call.
    bool density_drawable(const double camera[3]) const noexcept;
    // Extinction and mean chroma of the prepared family: tracked offline constants
    // (fog_family_chroma_inc.h, sum rgb / sum density of the pinned packet), or the file row's
    // chroma for a file family (same definition, computed by tools/analysis/fog_families.py).
    // False without a decoded field or for a profile neither table knows.
    bool field_family(float chroma[3], float* sigma) const noexcept;
    const FogDensityStatus& density_status() const noexcept { return density_status_; }
    unsigned density_march_scale() const noexcept {
        return density_march_scale_;
    } // march spacing of the created programs (0: none)
    const FogMoteReport& mote_report() const noexcept { return mote_report_; }
    bool motes_refused() const noexcept { return motes_refused_; } // sticky until detach; the fog draws without motes
    bool motes_variant() const noexcept {
        return density_config_.dust_motes;
    } // the mote toggle the last prepare_density latched
    // --gpu-sync-timing (engine-frame-time.md, "GPU sync timing"): the boundary pairs inside execute (fog_march,
    // fog_composite, fog_repair around the three quads, motes around the mote draw), the repair-pixel census
    // bracket (fog-gpu-cost.md, step A) and the needs-repair census quad at the drawn march spacing (step C: its
    // program is created at the next prepare_density while marks are configured; drawn only when
    // Marks::needs_wanted()). Null (the default): one branch per boundary, no device call, no census program.
    void configure_sync_timing(gpu_sync_timing::Marks* marks) noexcept { sync_marks_ = marks; }
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
    HRESULT execute(const FogFrame&, FogResult*) noexcept;
    // detach joins the worker: call it from the device's release path, not under the loader lock.
    void before_reset() noexcept;
    void after_reset(HRESULT) noexcept;
    void detach() noexcept;
    bool resources_ready(UINT w, UINT h) const noexcept {
        return caps_.enabled && !reset_pending_ && active_profile_ != fog_field::Profile::None && atlas_ && block_ &&
               w && h && w == width_ && h == height_ && lit_surface_ && scratch_surface_;
    }
    bool resources_ready(UINT w, UINT h, fog_field::Profile p, std::uint32_t recipe,
                         std::uint64_t generation) const noexcept {
        return resources_ready(w, h) && p == active_profile_ && recipe == field_recipe_ &&
               generation == field_generation_;
    }
    bool density_ready(UINT w, UINT h) const noexcept {
        return caps_.enabled && !reset_pending_ && density_ && density_march_ && density_composite_ &&
               density_repair_ && density_atlas_surface_[0] && density_atlas_surface_[1] && block_ && w && h &&
               w == width_ && h == height_ && lit_surface_ && scratch_surface_ &&
               (density_march_scale_ != fog_march_scale_quarter || quarter_surface_);
    }
    fog_field::Profile field_profile() const noexcept { return active_profile_; }
    std::uint32_t field_recipe() const noexcept {
        return active_profile_ == fog_field::Profile::None ? 0 : field_recipe_;
    }
    std::uint64_t field_generation() const noexcept {
        return active_profile_ == fog_field::Profile::None ? 0 : field_generation_;
    }
    bool reset_pending() const noexcept { return reset_pending_; }
    unsigned references() const noexcept;
    unsigned allocations() const noexcept { return allocations_; }
    bool sky_seeded() const noexcept { return false; }
#ifdef X3M_FOG_PASS_FIXTURE
    IDirect3DTexture9* fixture_sky() const noexcept { return nullptr; }
    IDirect3DTexture9* fixture_sky_level() const noexcept { return nullptr; }
    IDirect3DSurface9* fixture_st() const noexcept {
        return density_march_scale_ == fog_march_scale_quarter && quarter_surface_ ? quarter_surface_ : lit_surface_;
    }
    std::size_t fixture_cpu_bytes() const noexcept { return atlas_bytes_.size() * sizeof(std::uint16_t); }
    IDirect3DTexture9* fixture_density_atlas(unsigned level) const noexcept { return density_atlas_[level]; }
    IDirect3DVertexBuffer9* fixture_mote_vertices() const noexcept { return mote_vb_; }
    const fog::DensityCache* fixture_density_cache() const noexcept { return density_; }
#endif
private:
    gpu_sync_timing::Marks* sync_marks_ = nullptr;
    struct SavedState;
    template <class Fn> Fn call(unsigned slot) const noexcept {
        ++calls_;
        return reinterpret_cast<Fn>(vtable_[slot]);
    }
    HRESULT normalize(bool density) noexcept;
    HRESULT density_resources() noexcept;
    HRESULT density_uploads(unsigned budget) noexcept;
    void release_density_default() noexcept;
    void release_motes() noexcept;
    void release_quarter() noexcept;
    const char* mote_capabilities() noexcept;
    HRESULT mote_buffers() noexcept;
    bool mote_constants(const FogFrame&, float ready_far, float rows[fog_mote_vs_rows][4], FogMoteReport&,
                        double rotation[9]) const noexcept;
    HRESULT quad(UINT, UINT) noexcept;
    HRESULT bind_target(IDirect3DSurface9*, UINT, UINT, IDirect3DPixelShader9*, UINT samplers = 7) noexcept;
    void release_targets() noexcept;
    void disarm_field() noexcept { active_profile_ = fog_field::Profile::None; }
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr; // borrowed
    FogCaps caps_{};
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DPixelShader9 *march_ = nullptr, *composite_ = nullptr;
    IDirect3DVertexShader9* quad_vs_ = nullptr;
    IDirect3DVertexDeclaration9* quad_declaration_ = nullptr;
    IDirect3DTexture9 *atlas_ = nullptr, *lit_ = nullptr, *scratch_ = nullptr;
    IDirect3DSurface9 *lit_surface_ = nullptr, *scratch_surface_ = nullptr;
    std::vector<std::uint16_t> atlas_bytes_;
    fog_field::Profile cached_profile_ = fog_field::Profile::None, active_profile_ = fog_field::Profile::None;
    std::uint32_t cached_packet_ = 0; // file families: the decoded packet's own id (0: compiled or none)
    std::uint32_t field_recipe_ = 0;
    std::uint64_t field_generation_ = 0;
    float base_sigma_ = 0;
    UINT width_ = 0, height_ = 0, half_width_ = 0, half_height_ = 0, render_targets_ = 0, streams_ = 0, max_width_ = 0,
         max_height_ = 0;
    unsigned allocations_ = 0;
    mutable unsigned calls_ = 0;
    bool reset_pending_ = false;
    // Stored-density path; everything below stays null/zero until an enabled prepare_density.
    fog::DensityCache* density_ = nullptr;
    // The single look's programs (FOG_LOOK); created once, never on a draw path.
    IDirect3DPixelShader9 *density_march_ = nullptr, *density_composite_ = nullptr, *density_repair_ = nullptr;
    // The march spacing of the three programs above (FogDensityConfig::march_scale; 0: not created), its sticky
    // refusals (the reason of an unbuildable spacing: "program" or "target"), and the quarter-resolution march target,
    // which exists only while the quarter programs draw (sized from the targets, released with them and re-created at
    // prepare_density).
    unsigned density_march_scale_ = 0;
    unsigned march_scale_unbuildable_ = 0;
    const char* march_scale_unbuildable_reason_ = nullptr;
    IDirect3DTexture9* quarter_ = nullptr;
    IDirect3DSurface9* quarter_surface_ = nullptr;
    UINT quarter_width_ = 0, quarter_height_ = 0;
    // --gpu-sync-timing only: the needs-repair census program at spacing density_needs_scale_ (fog-gpu-cost.md step C);
    // a failed creation (not a lost device) leaves the census without it until detach.
    IDirect3DPixelShader9* density_needs_ = nullptr;
    unsigned density_needs_scale_ = 0;
    bool needs_refused_ = false;
    IDirect3DTexture9 *density_staging_[2]{}, *density_atlas_[2]{};
    IDirect3DSurface9 *density_staging_surface_[2]{}, *density_atlas_surface_[2]{};
    FogDensityConfig density_config_{};
    FogDensityStatus density_status_{};
    unsigned ps30_slots_ = 0;
    bool density_refused_ = false, prefill_refused_ = false;
    // Dust motes (FogDensityConfig::motes): one vs_3_0 program, its declaration, the pixel program (created once,
    // surviving Reset) and the static DEFAULT VB/IB (released with the targets, re-created by the next
    // prepare_density). The previous drawn fog frame's basis feeds the streak; Reset and any gap drop it.
    IDirect3DVertexShader9* mote_vs_ = nullptr;
    IDirect3DVertexDeclaration9* mote_declaration_ = nullptr;
    IDirect3DPixelShader9* mote_ps_ = nullptr;
    IDirect3DVertexBuffer9* mote_vb_ = nullptr;
    IDirect3DIndexBuffer9* mote_ib_ = nullptr;
    unsigned mote_built_count_ = 0;
    std::uint32_t mote_built_seed_ = 0;
    bool mote_caps_ = false, motes_refused_ = false;
    DWORD mote_max_index_ = 0, mote_max_primitives_ = 0;
    D3DFORMAT adapter_format_ = D3DFMT_UNKNOWN;
    bool mote_previous_valid_ = false;
    std::uint64_t mote_previous_frame_ = 0;
    double mote_previous_camera_[3]{}, mote_previous_seconds_ = 0;
    double mote_previous_rotation_[9]{};
    FogMoteReport mote_report_{};
};
} // namespace x3m::renderer
