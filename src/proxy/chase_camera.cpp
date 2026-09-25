#include "chase_camera.h"
#include "chase_aim_trace.h"
#include "chase_fire.h"
#include "chase_lead.h"
#include "chase_camera_math.h"
#include "chase_camera_native.h"
#include "chase_transition.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "capture.h"
#include "cpu_state.h"
#include "telemetry.h"
#include <atomic>
#include <cstring>
#include <cwchar>
#include <cstdlib>

static_assert(sizeof(void*) == 4, "Verified x86 image layout only");
namespace x3m::chase_camera {
namespace {
// The site (X3AP.exe per executable_identity.h, shipped SHA-256 fdbf3418…,
// preferred base 0x00400000), bytes
// verified against the installed executable and the Ghidra listing on
// 2026-09-13 (docs/reverse-engineering/external-camera.md, "Hook site"):
//   00420e06  83 7b 54 00        cmp dword ptr [ebx+0x54],0
//   00420e0a  0f 84 09 02 00 00  jz 0x00421019
// EBX = the cockpit object throughout FUN_004205e0. The jz's rel32 (offset 6)
// is re-based by engine_patch when the two instructions move into the tail.
constexpr uintptr_t site_va = 0x00420e06;
constexpr engine_patch::SiteSpec site_spec = {"cockpit_update_pose", site_va, {0x83, 0x7b, 0x54, 0x00, 0x0f, 0x84, 0x09, 0x02, 0x00, 0x00}, 10, 0, 6};
// Engine layout used by the handler (all from the study; offsets in bytes).
constexpr uintptr_t default_view_plane_slot = 0x00606f38; // -> struct; +0x28/+0x2c view-plane W/H (16.16)
constexpr unsigned cockpit_block = 0x200, camera_block = 0x310, scene_camera_block = 0x70;
constexpr unsigned cockpit_scene_camera = 0x08, cockpit_ref_object = 0x0c, cockpit_view_object = 0x10, cockpit_camera = 0x58, cockpit_view_rel = 0xf0;
constexpr unsigned cockpit_view_mode = 0x150, cockpit_flags = 0x1a0, cockpit_connect_mode = 0x1c0, cockpit_aim_gun = 0x1d8, cockpit_tracked = 0x1e0, cockpit_tracking_mode = 0x1e4, cockpit_sector = 0x1fc;
constexpr unsigned camera_position = 0x30, camera_basis = 0x40, camera_fov = 0x298, camera_plane_w = 0x300, camera_plane_h = 0x304;
constexpr unsigned max_cockpits_seen = 8;

engine_patch::Site site;
bool requested_ = false, initialized_ = false, scene_fix_enabled = false, timing_enabled = false;
std::atomic<const char*> state{"disabled"};
chase::Tunables tunables;
chase::State pipeline;
uint64_t qpc_frequency = 0, qpc_last = 0;
std::atomic<std::uint32_t> snap_epoch{0};
// Bolt footprint view gate: whether the last active-cockpit visit wrote the
// chase pose, and how many visits have written one (the consumer compares the
// count against its own mark taken at the previous Present).
std::atomic<bool> pose_written{false};
std::atomic<std::uint32_t> pose_writes{0};
PoseContinuity continuity;
SRWLOCK stats_lock = SRWLOCK_INIT;
Stats stats_;
uintptr_t cockpits_seen[max_cockpits_seen]; // distinct EBX values since the last report (stats_lock)
double basis_deviation_deg = 0; // exact native basis vs the derived ship basis (diagnostic)
uintptr_t last_cockpit = 0;
// chase_fov_compensate rows (report(), one per report window with a change), capped.
constexpr unsigned max_fov_rows = 32;
unsigned fov_rows = 0;
bool fov_rows_suppressed = false;
std::uint64_t fov_changes_logged = 0;

bool writable(uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof info) != sizeof info) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    if (!(info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))) return false;
    const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return address + size > address && address + size <= end;
}
double env_double(const wchar_t* name, double fallback, bool* bad) {
    wchar_t text[64]{};
    const DWORD n = GetEnvironmentVariableW(name, text, 64);
    if (n == 0) return fallback;
    if (n >= 64) { *bad = true; return fallback; }
    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    if (end == text || *end != L'\0') { *bad = true; return fallback; }
    return v;
}
// X3M_CHASE_FOV_COMPENSATE: unset = the compiled default (on); "1" on, "0" off; anything else is invalid.
bool env_on_off(const wchar_t* name, bool fallback, bool* bad) {
    wchar_t text[8]{};
    const DWORD n = GetEnvironmentVariableW(name, text, 8);
    if (n == 0) return fallback;
    if (n == 1 && (text[0] == L'0' || text[0] == L'1')) return text[0] == L'1';
    *bad = true; return fallback;
}
bool env_flag(const wchar_t* name) {
    wchar_t text[8]{};
    return GetEnvironmentVariableW(name, text, 8) == 1 && text[0] == L'1';
}
double angle_between_deg(const chase::Mat3& a, const chase::Mat3& b) {
    return chase::length(chase::log_rotation(chase::mul(chase::transpose(a), b))) * 180.0 / chase::pi;
}
void note_cockpit_seen(uintptr_t cockpit) { // caller holds stats_lock
    for (unsigned i = 0; i < stats_.cockpits_seen && i < max_cockpits_seen; ++i) if (cockpits_seen[i] == cockpit) return;
    if (stats_.cockpits_seen < max_cockpits_seen) cockpits_seen[stats_.cockpits_seen] = cockpit;
    ++stats_.cockpits_seen; // counts past the table too, so an overflow is visible
}
// A5 (X3M_CHASE_SCENE_FIX=1): the layer-0 cockpit-scene camera (cockpit+8) was
// built at 0x00420787 from this frame's vanilla +0xf0 (recomputed earlier at
// 0x00422c5c). Re-express it through this frame's R_view':
// basis' = R_view' x R_view_vanilla^T x basis, and the (shake) position through
// the same rotation. Returns the correction angle in degrees, or -1 when not applied.
double fix_scene_camera(uintptr_t cockpit_scene, const chase::Mat3& view_rel_now, const chase::Mat3& view_rel_vanilla) {
    unsigned char bytes[scene_camera_block];
    if (!cockpit_scene || (cockpit_scene & 3) || !engine_memory::read(cockpit_scene, bytes, scene_camera_block)) return -1.0;
    int32_t rows[12]; std::memcpy(rows, bytes + camera_basis, 48);
    const chase::Mat3 basis = chase::from_fixed(rows);
    if (chase::orthonormality_error(basis) > tunables.max_orthonormality_error) return -1.0;
    const chase::Mat3 correction = chase::relative_view_correction(view_rel_now, view_rel_vanilla);
    chase::Mat3 fixed = chase::mul(correction, basis);
    if (!chase::orthonormalize(fixed)) return -1.0;
    int32_t p[3]; std::memcpy(p, bytes + camera_position, 12);
    const chase::Vec3 position = chase::mul(chase::mul(chase::Vec3{double(p[0]), double(p[1]), double(p[2])}, chase::transpose(basis)), fixed);
    int32_t out_position[3];
    if (!chase::to_fixed(fixed, rows) || !chase::to_int(position, out_position) || !writable(cockpit_scene, scene_camera_block)) return -1.0;
    std::memcpy(reinterpret_cast<void*>(cockpit_scene + camera_position), out_position, 12);
    for (int r = 0; r < 3; ++r) std::memcpy(reinterpret_cast<void*>(cockpit_scene + camera_basis + 16 * r), rows + 4 * r, 12);
    return chase::length(chase::log_rotation(correction)) * 180.0 / chase::pi;
}
// The per-frame work, on the game thread inside the trampoline (full CPU
// boundary around it; XMM0-7 saved by the stub).
void publish_pose(bool written, bool snapped = false) {
    pose_written.store(written, std::memory_order_relaxed);
    if (written) pose_writes.fetch_add(1, std::memory_order_relaxed);
    if (continuity.update(written, snapped)) snap_epoch.fetch_add(1, std::memory_order_relaxed);
}
void handle(uint32_t* regs) {
    chase_lead::invalidate_pose();
    const uintptr_t cockpit = regs[4]; // EBX after pushad: EDI ESI EBP ESP EBX EDX ECX EAX
    chase::Input in; chase::Pose pose; chase::Step result;
    unsigned char cockpit_bytes[cockpit_block], camera_bytes[camera_block];
    auto u32 = [](const unsigned char* p, unsigned off) { uint32_t v; std::memcpy(&v, p + off, 4); return v; };
    auto i32 = [](const unsigned char* p, unsigned off) { int32_t v; std::memcpy(&v, p + off, 4); return v; };
    auto rows = [&](const unsigned char* p, unsigned off, int32_t* out) { std::memcpy(out, p + off, 48); };
    if ((cockpit & 3) || !engine_memory::read(cockpit, cockpit_bytes, cockpit_block)) {
        // Unreadable cockpit: which cockpit it was is unknown, so the pipeline
        // takes a gap (the next applied frame snaps) rather than trusting it.
        chase::note_gap(pipeline);
        publish_pose(false);
        chase_aim_trace::invalidate_camera(cockpit);
        chase_fire::invalidate_camera(cockpit);
        AcquireSRWLockExclusive(&stats_lock);
        ++stats_.frames; ++stats_.refused; stats_.last_verdict = 100; note_cockpit_seen(cockpit);
        ReleaseSRWLockExclusive(&stats_lock);
        return;
    }
    // A1: the registry walk (0x0041cde0) runs FUN_004205e0 for every cockpit
    // object (monitor/target-view cockpits included). Only the active control
    // cockpit, whose ref view object (+0x10) is its ref object (+0xc) - the
    // player's ship seen by its own view, the fire control's own test - owns
    // the pipeline; every other visit counts and returns before the clock,
    // the state or the frame counters are touched.
    const uintptr_t ref_object = u32(cockpit_bytes, cockpit_ref_object), view_object = u32(cockpit_bytes, cockpit_view_object);
    const bool bound = view_object != 0 && view_object == ref_object;
    // Run78 pose gap: a fresh generation's +0x10 stays 0 for tens of updates
    // while the engine renders its rear view from this cockpit. Admit that
    // unbound phase only on the registry active-control proof (complete
    // lifetime, handle row maps to this cockpit); the bounded walk runs only
    // for +0x10 == 0 visits. Fire, lead and aim contexts stay on `bound`.
    const bool active = admits_pose(ref_object, view_object, view_object == 0 && chase_transition::active_control_cockpit(cockpit));
    AcquireSRWLockExclusive(&stats_lock);
    note_cockpit_seen(cockpit);
    if (!active) ++stats_.inactive;
    else if (!bound) ++stats_.unbound;
    ReleaseSRWLockExclusive(&stats_lock);
    if (!active) { chase_fire::invalidate_camera(cockpit); return; }
    // Defence in depth behind the predicate: if the active cockpit pointer
    // still changes, a gap keeps two objects' poses and dt out of one spring.
    if (cockpit != last_cockpit) { last_cockpit = cockpit; chase::note_gap(pipeline); }
    const uintptr_t camera = u32(cockpit_bytes, cockpit_camera);
    chase::NativeAnchor anchor;
    bool ok = camera && ref_object && ((camera | ref_object) & 3) == 0;
    if (ok) ok = engine_memory::read(camera, camera_bytes, camera_block);
    if (ok) ok = chase::read_native_anchor(ref_object, &engine_memory::read, &anchor);
    uint32_t plane[2] = {0, 0};
    if (ok) {
        uintptr_t defaults = 0;
        if (engine_memory::read(default_view_plane_slot, &defaults, 4) && defaults) engine_memory::read(defaults + 0x28, plane, 8);
    }
    LARGE_INTEGER stamp{}; QueryPerformanceCounter(&stamp);
    const uint64_t now = uint64_t(stamp.QuadPart);
    const double dt = qpc_last && now > qpc_last && qpc_frequency ? double(now - qpc_last) / double(qpc_frequency) : 0.0;
    qpc_last = now;
    if (!ok) {
        chase::note_gap(pipeline);
        publish_pose(false);
        AcquireSRWLockExclusive(&stats_lock);
        ++stats_.frames; ++stats_.refused; stats_.last_verdict = 100; stats_.dt_ms = dt * 1000.0;
        ReleaseSRWLockExclusive(&stats_lock);
        chase_aim_trace::camera_context(0, 0, 0, 0, false);
        chase_fire::camera_context(0, 0, 0, 0, 0, 0, false, 0);
        return;
    }
    int32_t fixed[12];
    rows(camera_bytes, camera_basis, fixed); in.vanilla_cam = chase::from_fixed(fixed);
    rows(cockpit_bytes, cockpit_view_rel, fixed); in.view_rel = chase::from_fixed(fixed);
    in.vanilla_pos = {double(i32(camera_bytes, camera_position)), double(i32(camera_bytes, camera_position + 4)), double(i32(camera_bytes, camera_position + 8))};
    // The anchor must be the one the native camera just followed. On its
    // base-domain branch node+0xb0 is refreshed later by scene traversal;
    // treating it as this anchor makes the boom spring absorb that mismatch.
    in.ship_pos = anchor.position;
    in.view_mode = u32(cockpit_bytes, cockpit_view_mode); in.connect_mode = u32(cockpit_bytes, cockpit_connect_mode);
    in.flags_1a0 = u32(cockpit_bytes, cockpit_flags);
    in.ref_object = ref_object; in.sector = u32(cockpit_bytes, cockpit_sector);
    // A9 (unverified in game): +0x1e4 is the short the dispatcher tests for
    // INS_CockpitIsTracking (== 1) and INS_CockpitIsEnemyTracking (== 4);
    // +0x1e0 is the tracked object the fire control aims at (its +8 is the id
    // INS_CockpitGetTracking returns). A lock is either mode with a readable
    // object; the tightness scaling applies only then.
    const uint32_t tracking_mode = u32(cockpit_bytes, cockpit_tracking_mode) & 0xffffu;
    const uintptr_t tracked = u32(cockpit_bytes, cockpit_tracked);
    uint32_t tracked_id = 0;
    const bool tracked_valid = tracked && (tracked & 3) == 0 && engine_memory::read(tracked + 8, &tracked_id, 4);
    in.target_locked = (tracking_mode == 1 || tracking_mode == 4) && tracked_valid;
    // Vertical half-FOV: cam+0x298 is a binary angle (65536 = 360 deg), the
    // projection uses t = angle * pi as the half-FOV of a plane of width W and
    // height H (16.16, the camera's own or the default), so tan(half vfov) = tan(t) * H.
    // Static inference (O2): the first applied frame logs the inputs.
    const uint32_t fov298 = u32(camera_bytes, camera_fov);
    const double t = double(fov298) / 65536.0 * chase::pi;
    const int32_t w = i32(camera_bytes, camera_plane_w), h = i32(camera_bytes, camera_plane_h);
    const double H = (w > 0 && h > 0) ? h / 65536.0 : (int32_t(plane[1]) > 0 ? int32_t(plane[1]) / 65536.0 : 0.75);
    in.half_vfov_tan = (t > 0 && t < chase::pi / 2) ? std::tan(t) * H : 0.0;
    // The boom in the ship frame, for the back-view thresholds (O3; diagnostic only here).
    chase::Mat3 derived = chase::mul(chase::transpose(in.view_rel), in.vanilla_cam);
    const bool derived_ok = chase::orthonormalize(derived);
    const chase::Vec3 boom_local = derived_ok ? chase::mul(in.vanilla_pos - in.ship_pos, chase::transpose(derived)) : chase::Vec3{};

    const uint64_t rotation_clamps_before = pipeline.rotation_clamps, position_clamps_before = pipeline.position_clamps;
    result = chase::step(pipeline, in, dt, tunables, &pose);
    bool written = false, write_refused = false; double scene_fix_deg = -1.0;
    if (result.verdict == chase::Verdict::Applied) {
        int32_t position[3], basis_rows[12], rel_rows[12];
        rows(camera_bytes, camera_basis, basis_rows); rows(cockpit_bytes, cockpit_view_rel, rel_rows); // keep the 4th words
        if (chase::to_int(pose.pos, position) && chase::to_fixed(pose.basis, basis_rows) && chase::to_fixed(pose.view_rel, rel_rows) &&
            // These objects can be recreated at the same address after a load.
            // Recheck both writable spans before either write; a pointer-only
            // process-lifetime cache cannot establish current page protection.
            writable(camera, camera_block) && writable(cockpit, cockpit_block)) {
            std::memcpy(reinterpret_cast<void*>(camera + camera_position), position, 12);
            for (int r = 0; r < 3; ++r) {
                std::memcpy(reinterpret_cast<void*>(camera + camera_basis + 16 * r), basis_rows + 4 * r, 12);
                std::memcpy(reinterpret_cast<void*>(cockpit + cockpit_view_rel + 16 * r), rel_rows + 4 * r, 12);
            }
            written = true;
            if (scene_fix_enabled) scene_fix_deg = fix_scene_camera(u32(cockpit_bytes, cockpit_scene_camera), pose.view_rel, in.view_rel);
        } else {
            write_refused = true;
            chase::note_gap(pipeline);
        }
    }
    publish_pose(written, result.snapped);
    AcquireSRWLockExclusive(&stats_lock);
    ++stats_.frames;
    if (written) ++stats_.applied; else ++stats_.refused;
    if (write_refused) ++stats_.write_refused;
    if (result.target_locked) ++stats_.locked_frames;
    if (scene_fix_deg >= 0) { ++stats_.scene_fixed; stats_.scene_fix_deg = scene_fix_deg; }
    if (written && std::fabs(result.fov_factor - stats_.fov_factor) > 1e-4) {
        stats_.fov_factor = result.fov_factor; stats_.fov_half_vfov_tan = in.half_vfov_tan;
        stats_.fov_change_frame = stats_.frames; ++stats_.fov_changes;
    }
    stats_.snaps = pipeline.snaps; stats_.coalesced = pipeline.coalesced; stats_.clamps = pipeline.clamps;
    stats_.rotation_clamps += pipeline.rotation_clamps - rotation_clamps_before;
    stats_.position_clamps += pipeline.position_clamps - position_clamps_before;
    stats_.last_verdict = unsigned(result.verdict); if (result.snapped || result.coalesced) stats_.last_snap_reason = result.snap_reason;
    stats_.view_mode = in.view_mode; stats_.connect_mode = in.connect_mode; stats_.flags_1a0 = in.flags_1a0; stats_.tracking_mode = tracking_mode; stats_.target_locked = in.target_locked;
    stats_.lag_deg = result.lag_deg; stats_.pos_lag = result.pos_lag; stats_.distance = result.distance; stats_.dt_ms = dt * 1000.0;
    stats_.half_vfov_tan = in.half_vfov_tan; stats_.boom_local[0] = boom_local.x; stats_.boom_local[1] = boom_local.y; stats_.boom_local[2] = boom_local.z;
    if (written) {
        basis_deviation_deg = anchor.basis_valid && derived_ok && chase::orthonormality_error(anchor.basis) < 0.05 ? angle_between_deg(derived, anchor.basis) : -1.0;
        stats_.native_base_domain = anchor.base_domain;
        stats_.domain_delta = anchor.render_position_valid ? chase::length(anchor.position - anchor.render_position) : -1.0;
        stats_.render_basis_deviation = anchor.render_basis_valid && derived_ok && chase::orthonormality_error(anchor.render_basis) < 0.05 ? angle_between_deg(derived, anchor.render_basis) : -1.0;
        if (anchor.render_position_valid) {
            if (!stats_.window_domain_samples) stats_.domain_delta_min = stats_.domain_delta_max = stats_.domain_delta;
            else {
                stats_.domain_delta_min = std::fmin(stats_.domain_delta_min, stats_.domain_delta);
                stats_.domain_delta_max = std::fmax(stats_.domain_delta_max, stats_.domain_delta);
            }
            ++stats_.window_domain_samples;
        }
        if (!stats_.window_applied) {
            stats_.rotation_lag_min = stats_.rotation_lag_max = result.lag_deg;
            stats_.position_lag_min = stats_.position_lag_max = result.pos_lag;
        } else {
            stats_.rotation_lag_min = std::fmin(stats_.rotation_lag_min, result.lag_deg);
            stats_.rotation_lag_max = std::fmax(stats_.rotation_lag_max, result.lag_deg);
            stats_.position_lag_min = std::fmin(stats_.position_lag_min, result.pos_lag);
            stats_.position_lag_max = std::fmax(stats_.position_lag_max, result.pos_lag);
        }
        ++stats_.window_applied;
        if (anchor.base_domain) ++stats_.window_base_domain;
        if (!stats_.first.captured) {
            FirstApplied& f = stats_.first;
            f.captured = true; f.handler_frame = stats_.frames; f.half_vfov_tan = in.half_vfov_tan;
            f.boom_local[0] = boom_local.x; f.boom_local[1] = boom_local.y; f.boom_local[2] = boom_local.z;
            f.fov298 = fov298; f.plane_w = uint32_t(w); f.plane_h = uint32_t(h); f.default_plane_h = plane[1];
            f.view_mode = in.view_mode; f.connect_mode = in.connect_mode; f.flags_1a0 = in.flags_1a0; f.tracking_mode = tracking_mode; f.aim_gun = u32(cockpit_bytes, cockpit_aim_gun);
            f.ref_object = ref_object; f.view_object = view_object; f.tracked_object = tracked; f.target_locked = in.target_locked;
            f.native_base_domain = anchor.base_domain; f.domain_delta = stats_.domain_delta;
        }
    }
    const std::uint64_t handler_frame = stats_.frames;
    ReleaseSRWLockExclusive(&stats_lock);
    // Cursor fire, lead and aim keep the fire control's own predicate: an
    // unbound visit hands them an invalidated context, as a refusal would.
    const bool context_ok = written && bound;
    chase_lead::camera_context(cockpit, ref_object, camera, context_ok, context_ok ? &pose.basis : nullptr,
                               context_ok ? &pose.view_rel : nullptr);
    chase_fire::camera_context(cockpit, ref_object, camera, in.view_mode, in.connect_mode, in.flags_1a0, context_ok, now);
    chase_aim_trace::camera_context(cockpit, ref_object, camera, in.view_mode, context_ok, handler_frame);
}
// Unlike telemetry::State, this state is protected by the camera's own lock:
// the engine update runs outside capture.cpp's mutex. Only aggregate handler
// CPU times are printed on the existing report cadence, never per invocation.
void timed_handle(uint32_t* regs) {
    if (!timing_enabled) { handle(regs); return; }
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    handle(regs);
    QueryPerformanceCounter(&end);
    const uint64_t ticks = end.QuadPart > begin.QuadPart ? uint64_t(end.QuadPart - begin.QuadPart) : 0;
    AcquireSRWLockExclusive(&stats_lock);
    ++stats_.timed_calls; stats_.handler_ticks += ticks;
    if (ticks > stats_.handler_max_ticks) stats_.handler_max_ticks = ticks;
    ReleaseSRWLockExclusive(&stats_lock);
}
}
}
extern "C" {
// cdecl, called from the generated stub with the pushad block as its argument;
// the game's stack is 4-byte aligned mid-function (force_align_arg_pointer).
// Full boundary: the pipeline is SSE2 but the log formatter and CRT paths
// reached from here are x87 code (cpu_state.h).
__attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_camera_enter(uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    // PreserveCpuState saves with FNSAVE and FNINITs, so our code runs on an
    // empty x87 stack and FRSTOR returns the game's. This is a mid-function site, not a
    // call boundary, so nothing guarantees the stack is empty, and the libm
    // transcendentals the pipeline reaches (exp/acos/atan/tan) are x87 code that
    // would push onto it. Start from a clean, fully masked, round-to-nearest FPU;
    // the destructor's FRSTOR puts the game's control word, tags and registers
    // back before the relocated cmp/jz runs. (Checked: the instructions reaching
    // 0x00420e06 are an integer copy, so this is defence in depth.)
    asm volatile("fninit" ::: "memory");
    // SSE2 arithmetic must not inherit an unmasked exception, directed
    // rounding, FTZ or DAZ from the engine. PreserveCpuState restores the
    // caller's MXCSR, including sticky status, after the handler returns.
    const unsigned local_mxcsr = 0x1f80u;
    asm volatile("ldmxcsr %0" :: "m"(local_mxcsr) : "memory");
    x3m::chase_camera::timed_handle(regs);
}
}
namespace x3m::chase_camera {
namespace {
// Stub: pushfd; pushad; sub esp,0x80; movups [esp+16*i],xmm_i (i=0..7);
// lea eax,[esp+0x80]; push eax; call x3m_chase_camera_enter; add esp,4;
// movups xmm_i,[esp+16*i]; add esp,0x80; popad; popfd; jmp [next].
// XMM0-7 are saved although the site's straight-line x87 code keeps nothing
// live in them: the handler is SSE2 code injected mid-function, not at a call.
void* emit_stub(void*** next_out) {
    engine_patch::Emitter e(160);
    if (!e.ok()) return nullptr;
    void* start = e.here();
    e.byte(0x9c); e.byte(0x60);
    e.byte(0x81); e.byte(0xec); e.dword(0x80);
    for (unsigned i = 0; i < 8; ++i) { e.byte(0x0f); e.byte(0x11); e.byte(static_cast<unsigned char>(0x44 | (i << 3))); e.byte(0x24); e.byte(static_cast<unsigned char>(16 * i)); }
    e.byte(0x8d); e.byte(0x84); e.byte(0x24); e.dword(0x80);
    e.byte(0x50);
    e.byte(0xe8); e.rel32(reinterpret_cast<const void*>(&x3m_chase_camera_enter));
    e.byte(0x83); e.byte(0xc4); e.byte(0x04);
    for (unsigned i = 0; i < 8; ++i) { e.byte(0x0f); e.byte(0x10); e.byte(static_cast<unsigned char>(0x44 | (i << 3))); e.byte(0x24); e.byte(static_cast<unsigned char>(16 * i)); }
    e.byte(0x81); e.byte(0xc4); e.dword(0x80);
    e.byte(0x61); e.byte(0x9d);
    unsigned char* jmp = static_cast<unsigned char*>(e.here());
    uintptr_t next = reinterpret_cast<uintptr_t>(jmp) + 6; next = (next + 3) & ~uintptr_t(3);
    e.byte(0xff); e.byte(0x25); e.dword(uint32_t(next));
    while (e.ok() && reinterpret_cast<uintptr_t>(e.here()) < next) e.byte(0xcc); // e.ok(): an exhausted emitter reports here() == nullptr
    *next_out = reinterpret_cast<void**>(next); e.dword(0);
    return e.finish() ? start : nullptr;
}
}
bool wanted() {
    wchar_t setting[16]{};
    const DWORD n = GetEnvironmentVariableW(L"X3M_CAMERA", setting, 16);
    return n == 5 && !std::wcscmp(setting, L"chase");
}
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized_) { SetLastError(error); return site.patched_in; }
    initialized_ = true;
    requested_ = wanted();
    if (!requested_) { state = "disabled"; SetLastError(error); return false; }
    // X3M_CHASE_* (not X3M_CAMERA_*: X3M_CAMERA_CUT_DEG / X3M_CAMERA_LOG belong to the TAA camera read).
    bool bad_tunable = false;
    tunables.rot_tau = env_double(L"X3M_CHASE_ROT_TAU", tunables.rot_tau, &bad_tunable);
    tunables.pos_tau = env_double(L"X3M_CHASE_POS_TAU", tunables.pos_tau, &bad_tunable);
    tunables.pitch_down_deg = env_double(L"X3M_CHASE_PITCH_DOWN_DEG", tunables.pitch_down_deg, &bad_tunable);
    tunables.offset_y = env_double(L"X3M_CHASE_OFFSET_Y", tunables.offset_y, &bad_tunable);
    tunables.distance_scale = env_double(L"X3M_CHASE_DISTANCE_SCALE", tunables.distance_scale, &bad_tunable);
    tunables.fov_compensate = env_on_off(L"X3M_CHASE_FOV_COMPENSATE", tunables.fov_compensate, &bad_tunable);
    tunables.lag_clamp_deg = env_double(L"X3M_CHASE_LAG_CLAMP_DEG", tunables.lag_clamp_deg, &bad_tunable);
    tunables.pos_lag_clamp = env_double(L"X3M_CHASE_POS_LAG_CLAMP", tunables.pos_lag_clamp, &bad_tunable);
    tunables.combat_tightness = env_double(L"X3M_CHASE_COMBAT_TIGHTNESS", tunables.combat_tightness, &bad_tunable);
    tunables.max_dt = env_double(L"X3M_CHASE_MAX_DT", tunables.max_dt, &bad_tunable);
    scene_fix_enabled = env_flag(L"X3M_CHASE_SCENE_FIX");
    const bool tunables_ok = !bad_tunable && chase::valid(tunables);
    LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); qpc_frequency = f.QuadPart > 0 ? uint64_t(f.QuadPart) : 0;
    const char* refusal = nullptr;
    if (!tunables_ok) refusal = "invalid_tunables";
    else if (!qpc_frequency) refusal = "no_qpc";
    else if (!object_trace::executable_verified()) refusal = "executable_mismatch";
    else if (!engine_patch::install_window_open()) refusal = "late_claim";
    if (refusal) {
        state = refusal;
        log("chase_camera requested=1 installed=0 status=%s window_closed_by=%s", refusal, engine_patch::install_window_reason() ? engine_patch::install_window_reason() : "-");
        SetLastError(error); return false;
    }
    timing_enabled = telemetry::enabled();
    bool okay = engine_patch::claim(site, site_spec);
    if (okay) {
        void** next = nullptr; void* stub = emit_stub(&next);
        if (!stub || !next) { site.status = "stub_failed"; okay = false; }
        else if (!engine_patch::store_pointer(next, *site.entry)) { site.status = "chain_write_failed"; okay = false; }
        else if (!engine_patch::push_front(site, stub)) { site.status = "chain_failed"; okay = false; }
        if (!okay && site.patched_in) engine_patch::restore(site); // the plain tail would be harmless, but keep the vanilla bytes
    }
    state = okay ? "active" : site.status;
    log("chase_camera requested=1 installed=%u status=%s site=0x%08lx length=%u rel32_offset=%u atomic_write=%u arena_used=%u "
        "rot_tau=%.3f pos_tau=%.3f offset_y=%.3f pitch_down_deg=%.2f distance_scale=%.3f fov_compensate=%u lag_clamp_deg=%.2f pos_lag_clamp=%.3f combat_tightness=%.3f combat=%s max_dt=%.3f "
        "snap_coalesce_frames=%u scene_fix=%u handler_timing=%u predicate=view_object_is_ref_object lifetime=process scope=external_back_view",
        unsigned(okay), state.load(), static_cast<unsigned long>(site_va), site_spec.length, site_spec.rel32_offset, unsigned(site.atomic_write), engine_patch::arena_used(),
        tunables.rot_tau, tunables.pos_tau, tunables.offset_y, tunables.pitch_down_deg, tunables.distance_scale, unsigned(tunables.fov_compensate), tunables.lag_clamp_deg, tunables.pos_lag_clamp, tunables.combat_tightness,
        tunables.combat_tightness > 0 ? "tracking_1e4_unverified" : "off", tunables.max_dt, tunables.snap_coalesce_frames, unsigned(scene_fix_enabled), unsigned(timing_enabled));
    if (okay) chase_fire::initialize();
    SetLastError(error);
    return okay;
}
bool installed() { return site.patched_in; }
const char* status() { return state.load(); }
Stats stats() {
    AcquireSRWLockShared(&stats_lock);
    Stats s = stats_;
    ReleaseSRWLockShared(&stats_lock);
    s.requested = requested_; s.installed = site.patched_in; s.status = state.load(); s.atomic_write = unsigned(site.atomic_write);
    return s;
}
std::uint32_t snap_generation() { return snap_epoch.load(std::memory_order_relaxed); }
std::uint32_t pose_write_count() { return pose_writes.load(std::memory_order_relaxed); }
bool pose_applied_since(std::uint32_t mark) {
    return pose_gate_open(site.patched_in, pose_written.load(std::memory_order_relaxed), pose_writes.load(std::memory_order_relaxed), mark);
}
void report(std::uint64_t frame) {
    if (!site.patched_in) return;
    chase_fire::report(frame);
    Stats s;
    double deviation = 0;
    AcquireSRWLockExclusive(&stats_lock);
    s = stats_; deviation = basis_deviation_deg;
    stats_.cockpits_seen = 0; // per report window
    stats_.timed_calls = stats_.handler_ticks = stats_.handler_max_ticks = 0;
    stats_.window_applied = stats_.window_base_domain = stats_.window_domain_samples = 0;
    stats_.rotation_clamps = stats_.position_clamps = 0;
    if (stats_.first.captured && !stats_.first.logged) stats_.first.logged = true;
    ReleaseSRWLockExclusive(&stats_lock);
    s.requested = requested_; s.installed = site.patched_in; s.status = state.load(); s.atomic_write = unsigned(site.atomic_write);
    if (timing_enabled) {
        const double micros_per_tick = qpc_frequency ? 1000000.0 / double(qpc_frequency) : 0;
        log("chase_camera_timing frame=%llu calls=%llu total_us=%.3f mean_us=%.3f max_us=%.3f scope=handler_cpu window=report excludes=stub_and_cpu_save_restore",
            frame, s.timed_calls, double(s.handler_ticks) * micros_per_tick,
            s.timed_calls ? double(s.handler_ticks) * micros_per_tick / double(s.timed_calls) : 0.0,
            double(s.handler_max_ticks) * micros_per_tick);
    }
    if (s.first.captured && !s.first.logged) {
        const FirstApplied& f = s.first;
        log("chase_camera first_applied frame=%llu handler_frame=%llu half_vfov_tan=%.4f fov298=0x%lx plane_w=0x%lx plane_h=0x%lx default_plane_h=0x%lx mode=%lu connect=%lu flags_1a0=0x%lx "
            "tracking=%lu aim_gun=0x%lx tracked=0x%08lx locked=%u ref=0x%08lx view_obj=0x%08lx boom_local=%.1f,%.1f,%.1f cockpits_seen=%u native_branch=%s domain_delta=%.1f",
            frame, f.handler_frame, f.half_vfov_tan, static_cast<unsigned long>(f.fov298), static_cast<unsigned long>(f.plane_w), static_cast<unsigned long>(f.plane_h),
            static_cast<unsigned long>(f.default_plane_h), static_cast<unsigned long>(f.view_mode), static_cast<unsigned long>(f.connect_mode), static_cast<unsigned long>(f.flags_1a0),
            static_cast<unsigned long>(f.tracking_mode), static_cast<unsigned long>(f.aim_gun), static_cast<unsigned long>(f.tracked_object), unsigned(f.target_locked),
            static_cast<unsigned long>(f.ref_object), static_cast<unsigned long>(f.view_object), f.boom_local[0], f.boom_local[1], f.boom_local[2], s.cockpits_seen,
            f.native_base_domain ? "base" : "render", f.domain_delta);
    }
    if (s.fov_changes != fov_changes_logged && fov_rows < max_fov_rows) {
        ++fov_rows;
        log("chase_fov_compensate frame=%llu handler_frame=%llu factor=%.4f half_vfov_tan=%.4f enabled=%u distance_scale=%.3f boom_scale=%.4f changes=%llu row=%u/%u",
            frame, s.fov_change_frame, s.fov_factor, s.fov_half_vfov_tan, unsigned(tunables.fov_compensate), tunables.distance_scale,
            tunables.distance_scale * s.fov_factor, s.fov_changes - fov_changes_logged, fov_rows, max_fov_rows);
        fov_changes_logged = s.fov_changes;
    } else if (s.fov_changes != fov_changes_logged && !fov_rows_suppressed) {
        fov_rows_suppressed = true; // one closing row after the cap; later changes are not logged
        log("chase_fov_compensate frame=%llu suppressed=1 changes=%llu", frame, s.fov_changes - fov_changes_logged);
    }
    if (timing_enabled) log("chase_camera_window frame=%llu applied=%llu native_base=%llu native_render=%llu native_branch=%s domain_delta=%.1f domain_samples=%llu domain_delta_min=%.1f domain_delta_max=%.1f "
        "native_basis_dev_deg=%.4f render_basis_dev_deg=%.4f rotation_clamps=%llu position_clamps=%llu rotation_lag_min=%.3f rotation_lag_max=%.3f position_lag_min=%.1f position_lag_max=%.1f",
        frame, s.window_applied, s.window_base_domain, s.window_applied - s.window_base_domain, s.native_base_domain ? "base" : "render",
        s.domain_delta, s.window_domain_samples, s.window_domain_samples ? s.domain_delta_min : 0.0, s.window_domain_samples ? s.domain_delta_max : 0.0,
        deviation, s.render_basis_deviation, s.rotation_clamps, s.position_clamps,
        s.window_applied ? s.rotation_lag_min : 0.0, s.window_applied ? s.rotation_lag_max : 0.0,
        s.window_applied ? s.position_lag_min : 0.0, s.window_applied ? s.position_lag_max : 0.0);
    log("chase_camera frame=%llu status=%s frames=%llu applied=%llu refused=%llu refused_inactive=%llu admitted_unbound=%llu cockpits_seen=%u snaps=%llu coalesced=%llu clamps=%llu write_refused=%llu "
        "verdict=%lu snap_reason=%lu mode=%lu connect=%lu flags_1a0=0x%lx tracking=%lu locked=%u locked_frames=%llu lag_deg=%.3f pos_lag=%.1f distance=%.1f dt_ms=%.3f "
        "basis_dev_deg=%.3f half_vfov_tan=%.4f boom_local=%.1f,%.1f,%.1f scene_fixed=%llu scene_fix_deg=%.3f",
        frame, s.status, s.frames, s.applied, s.refused, s.inactive, s.unbound, s.cockpits_seen, s.snaps, s.coalesced, s.clamps, s.write_refused,
        static_cast<unsigned long>(s.last_verdict), static_cast<unsigned long>(s.last_snap_reason), static_cast<unsigned long>(s.view_mode), static_cast<unsigned long>(s.connect_mode),
        static_cast<unsigned long>(s.flags_1a0), static_cast<unsigned long>(s.tracking_mode), unsigned(s.target_locked), s.locked_frames, s.lag_deg, s.pos_lag, s.distance, s.dt_ms,
        deviation, s.half_vfov_tan, s.boom_local[0], s.boom_local[1], s.boom_local[2], s.scene_fixed, s.scene_fix_deg);
}
void note_last_device() {
    if (!site.patched_in) return;
    // A3: the install window closed at the first Present, so a restore here
    // could never be undone for a recreated device; the handler runs inside
    // the cockpit update, which needs no device, so the site simply stays.
    log("chase_camera_last_device kept=1 status=%s lifetime=process", state.load());
}
void shutdown() {
    chase_fire::shutdown();
    pose_written.store(false, std::memory_order_relaxed);
    if (!site.patched_in) return;
    const bool okay = engine_patch::restore(site);
    state = site.status;
    log("chase_camera_shutdown restored=%u status=%s", unsigned(okay), site.status);
}
}
