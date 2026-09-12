#include "chase_camera.h"
#include "chase_camera_math.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "capture.h"
#include "cpu_state.h"
#include <atomic>
#include <cstring>
#include <cwchar>
#include <cstdlib>

static_assert(sizeof(void*) == 4, "Verified x86 image layout only");
namespace x3m::chase_camera {
namespace {
// The site (X3AP.exe SHA-256 fdbf3418…, preferred base 0x00400000), bytes
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
constexpr unsigned cockpit_block = 0x200, camera_block = 0x310, node_block = 0x40;
constexpr unsigned cockpit_ref_object = 0x0c, cockpit_camera = 0x58, cockpit_view_rel = 0xf0, cockpit_view_mode = 0x150, cockpit_connect_mode = 0x1c0, cockpit_sector = 0x1fc;
constexpr unsigned object_node = 0x70, node_position = 0xb0, node_basis = 0xc0;
constexpr unsigned camera_position = 0x30, camera_basis = 0x40, camera_fov = 0x298, camera_plane_w = 0x300, camera_plane_h = 0x304;

engine_patch::Site site;
bool requested_ = false, initialized_ = false;
std::atomic<const char*> state{"disabled"};
chase::Tunables tunables;
chase::State pipeline;
uint64_t qpc_frequency = 0, qpc_last = 0;
std::atomic<bool> snap_pending{false};
SRWLOCK stats_lock = SRWLOCK_INIT;
Stats stats_;
double basis_deviation_deg = 0; // node basis vs the derived ship basis (diagnostic)
struct WritableCache { uintptr_t pointer = 0; bool valid = false; };
WritableCache writable_camera, writable_cockpit;

bool writable(uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof info) != sizeof info) return false;
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    if (!(info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))) return false;
    const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return address + size > address && address + size <= end;
}
bool writable_cached(WritableCache& cache, uintptr_t pointer, size_t size) {
    if (cache.pointer != pointer || !cache.valid) { cache.pointer = pointer; cache.valid = writable(pointer, size); }
    return cache.valid;
}
double env_double(const wchar_t* name, double fallback, bool* bad) {
    wchar_t text[64]{};
    const DWORD n = GetEnvironmentVariableW(name, text, 64);
    if (n == 0 || n >= 64) return fallback;
    wchar_t* end = nullptr;
    const double v = std::wcstod(text, &end);
    if (end == text || *end != L'\0') { *bad = true; return fallback; }
    return v;
}
double angle_between_deg(const chase::Mat3& a, const chase::Mat3& b) {
    return chase::length(chase::log_rotation(chase::mul(chase::transpose(a), b))) * 180.0 / chase::pi;
}
// The per-frame work, on the game thread inside the trampoline (full CPU
// boundary around it; XMM0-7 saved by the stub).
void handle(uint32_t* regs) {
    const uintptr_t cockpit = regs[4]; // EBX after pushad: EDI ESI EBP ESP EBX EDX ECX EAX
    chase::Input in; chase::Pose pose; chase::Step result;
    unsigned char cockpit_bytes[cockpit_block], camera_bytes[camera_block], node_bytes[node_block];
    auto u32 = [](const unsigned char* p, unsigned off) { uint32_t v; std::memcpy(&v, p + off, 4); return v; };
    auto i32 = [](const unsigned char* p, unsigned off) { int32_t v; std::memcpy(&v, p + off, 4); return v; };
    auto rows = [&](const unsigned char* p, unsigned off, int32_t* out) { std::memcpy(out, p + off, 48); };
    // One pipeline state for one cockpit: the registry walk (0x0041cde0) calls
    // FUN_004205e0 for every cockpit object, so a second cockpit would otherwise
    // interleave its poses and its dt into the same springs. A change of the
    // cockpit pointer is a gap, so the next frame of either snaps instead.
    static uintptr_t last_cockpit = 0;
    if (cockpit != last_cockpit) { last_cockpit = cockpit; chase::note_gap(pipeline); }
    bool ok = (cockpit & 3) == 0 && engine_memory::read(cockpit, cockpit_bytes, cockpit_block);
    uintptr_t camera = 0, ref_object = 0, node = 0;
    if (ok) { camera = u32(cockpit_bytes, cockpit_camera); ref_object = u32(cockpit_bytes, cockpit_ref_object); ok = camera && ref_object && ((camera | ref_object) & 3) == 0; }
    if (ok) ok = engine_memory::read(camera, camera_bytes, camera_block);
    if (ok) ok = engine_memory::read(ref_object + object_node, &node, 4) && node && (node & 3) == 0;
    if (ok) ok = engine_memory::read(node + node_position, node_bytes, node_block);
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
        AcquireSRWLockExclusive(&stats_lock);
        ++stats_.frames; ++stats_.refused; stats_.last_verdict = 100; stats_.dt_ms = dt * 1000.0;
        ReleaseSRWLockExclusive(&stats_lock);
        return;
    }
    int32_t fixed[12];
    rows(camera_bytes, camera_basis, fixed); in.vanilla_cam = chase::from_fixed(fixed);
    rows(cockpit_bytes, cockpit_view_rel, fixed); in.view_rel = chase::from_fixed(fixed);
    in.vanilla_pos = {double(i32(camera_bytes, camera_position)), double(i32(camera_bytes, camera_position + 4)), double(i32(camera_bytes, camera_position + 8))};
    in.ship_pos = {double(i32(node_bytes, 0)), double(i32(node_bytes, 4)), double(i32(node_bytes, 8))};
    rows(node_bytes, node_basis - node_position, fixed); const chase::Mat3 node_basis_matrix = chase::from_fixed(fixed);
    in.view_mode = u32(cockpit_bytes, cockpit_view_mode); in.connect_mode = u32(cockpit_bytes, cockpit_connect_mode);
    in.ref_object = ref_object; in.sector = u32(cockpit_bytes, cockpit_sector);
    // Vertical half-FOV: cam+0x298 is a binary angle (65536 = 360 deg), the
    // projection uses t = angle * pi as the half-FOV of a plane of width W and
    // height H (16.16, the camera's own or the default), so tan(half vfov) = tan(t) * H.
    const double t = double(u32(camera_bytes, camera_fov)) / 65536.0 * chase::pi;
    const int32_t w = i32(camera_bytes, camera_plane_w), h = i32(camera_bytes, camera_plane_h);
    const double H = (w > 0 && h > 0) ? h / 65536.0 : (int32_t(plane[1]) > 0 ? int32_t(plane[1]) / 65536.0 : 0.75);
    in.half_vfov_tan = (t > 0 && t < chase::pi / 2) ? std::tan(t) * H : 0.0;

    result = chase::step(pipeline, in, dt, tunables, &pose);
    bool written = false, write_refused = false;
    if (result.verdict == chase::Verdict::Applied) {
        int32_t position[3], basis_rows[12], rel_rows[12];
        rows(camera_bytes, camera_basis, basis_rows); rows(cockpit_bytes, cockpit_view_rel, rel_rows); // keep the 4th words
        if (chase::to_int(pose.pos, position) && chase::to_fixed(pose.basis, basis_rows) && chase::to_fixed(pose.view_rel, rel_rows) &&
            writable_cached(writable_camera, camera, camera_block) && writable_cached(writable_cockpit, cockpit, cockpit_block)) {
            std::memcpy(reinterpret_cast<void*>(camera + camera_position), position, 12);
            for (int r = 0; r < 3; ++r) {
                std::memcpy(reinterpret_cast<void*>(camera + camera_basis + 16 * r), basis_rows + 4 * r, 12);
                std::memcpy(reinterpret_cast<void*>(cockpit + cockpit_view_rel + 16 * r), rel_rows + 4 * r, 12);
            }
            written = true;
        } else {
            write_refused = true;
            chase::note_gap(pipeline);
        }
        if (result.snapped) snap_pending.store(true, std::memory_order_relaxed);
    }
    AcquireSRWLockExclusive(&stats_lock);
    ++stats_.frames;
    if (written) ++stats_.applied; else ++stats_.refused;
    if (write_refused) ++stats_.write_refused;
    stats_.snaps = pipeline.snaps; stats_.clamps = pipeline.clamps;
    stats_.last_verdict = unsigned(result.verdict); if (result.snapped) stats_.last_snap_reason = result.snap_reason;
    stats_.view_mode = in.view_mode; stats_.connect_mode = in.connect_mode;
    stats_.lag_deg = result.lag_deg; stats_.pos_lag = result.pos_lag; stats_.distance = result.distance; stats_.dt_ms = dt * 1000.0;
    if (result.verdict == chase::Verdict::Applied) {
        chase::Mat3 derived = chase::mul(chase::transpose(in.view_rel), in.vanilla_cam);
        basis_deviation_deg = chase::orthonormalize(derived) && chase::orthonormality_error(node_basis_matrix) < 0.05 ? angle_between_deg(derived, node_basis_matrix) : -1.0;
    }
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
    // PreserveCpuState saves with FNSAVE and immediately FRSTORs, so the x87
    // stack our code inherits is the game's. This is a mid-function site, not a
    // call boundary, so nothing guarantees the stack is empty, and the libm
    // transcendentals the pipeline reaches (exp/acos/atan/tan) are x87 code that
    // would push onto it. Start from a clean, fully masked, round-to-nearest FPU;
    // the destructor's FRSTOR puts the game's control word, tags and registers
    // back before the relocated cmp/jz runs. (Checked: the instructions reaching
    // 0x00420e06 are an integer copy, so this is defence in depth.)
    asm volatile("fninit" ::: "memory");
    x3m::chase_camera::handle(regs);
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
    bool bad_tunable = false;
    tunables.rot_tau = env_double(L"X3M_CAMERA_ROT_TAU", tunables.rot_tau, &bad_tunable);
    tunables.pos_tau = env_double(L"X3M_CAMERA_POS_TAU", tunables.pos_tau, &bad_tunable);
    tunables.offset_y = env_double(L"X3M_CAMERA_OFFSET_Y", tunables.offset_y, &bad_tunable);
    tunables.distance_scale = env_double(L"X3M_CAMERA_DISTANCE_SCALE", tunables.distance_scale, &bad_tunable);
    tunables.lag_clamp_deg = env_double(L"X3M_CAMERA_LAG_CLAMP_DEG", tunables.lag_clamp_deg, &bad_tunable);
    tunables.pos_lag_clamp = env_double(L"X3M_CAMERA_POS_LAG_CLAMP", tunables.pos_lag_clamp, &bad_tunable);
    tunables.combat_tightness = env_double(L"X3M_CAMERA_COMBAT_TIGHTNESS", tunables.combat_tightness, &bad_tunable);
    tunables.max_dt = env_double(L"X3M_CAMERA_MAX_DT", tunables.max_dt, &bad_tunable);
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
    engine_memory::configure();
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
        "rot_tau=%.3f pos_tau=%.3f offset_y=%.3f distance_scale=%.3f lag_clamp_deg=%.2f pos_lag_clamp=%.3f combat_tightness=%.3f combat=inactive max_dt=%.3f scope=external_back_view",
        unsigned(okay), state.load(), static_cast<unsigned long>(site_va), site_spec.length, site_spec.rel32_offset, unsigned(site.atomic_write), engine_patch::arena_used(),
        tunables.rot_tau, tunables.pos_tau, tunables.offset_y, tunables.distance_scale, tunables.lag_clamp_deg, tunables.pos_lag_clamp, tunables.combat_tightness, tunables.max_dt);
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
bool take_snap() { return snap_pending.exchange(false, std::memory_order_relaxed); }
void report(std::uint64_t frame) {
    if (!site.patched_in) return;
    const Stats s = stats();
    AcquireSRWLockShared(&stats_lock); const double deviation = basis_deviation_deg; ReleaseSRWLockShared(&stats_lock);
    log("chase_camera frame=%llu status=%s frames=%llu applied=%llu refused=%llu snaps=%llu clamps=%llu write_refused=%llu verdict=%lu snap_reason=%lu mode=%lu connect=%lu lag_deg=%.3f pos_lag=%.1f distance=%.1f dt_ms=%.3f basis_dev_deg=%.3f",
        frame, s.status, s.frames, s.applied, s.refused, s.snaps, s.clamps, s.write_refused, static_cast<unsigned long>(s.last_verdict), static_cast<unsigned long>(s.last_snap_reason),
        static_cast<unsigned long>(s.view_mode), static_cast<unsigned long>(s.connect_mode), s.lag_deg, s.pos_lag, s.distance, s.dt_ms, deviation);
}
void shutdown() {
    if (!site.patched_in) return;
    const bool okay = engine_patch::restore(site);
    state = site.status;
    log("chase_camera_shutdown restored=%u status=%s", unsigned(okay), site.status);
}
}
