// FL edition of the chase lead marker and central HUD (src/proxy/chase_lead.h; the AP module is
// src/proxy/chase_lead.cpp). Contract: docs/reverse-engineering/fl/chase-lead.md, "Verified 2026-11-02"; sites and
// windows: src/engine/fl/chase_lead_sites_fl.h. The policy is the AP module's (shared portable core chase_lead_core.h):
// the predictive marker (gate, publish, final FOV) and the optional central instruments with the true-forward anchor.
// FL differences: base + RVA addresses admitted by twelve identity windows, overlay O = cockpit + 0x3bc and its fields,
// publication in ECX/EDX, the solver point at saved ESP + 0xe4, the ECX hide helper, and the update binding below.
// The five AP timing sites are telemetry only and are not ported (one refusal row).
//
// Update binding (the pose seam, not chase_transition's FL lifetime/update service, which the view-restore port added
// in the same build; taking the generation/serial from it as AP does is a follow-up): the chase camera's pose seam (updater RVA
// 0x0227ad) calls invalidate_pose() then, for an applied bound pose, camera_context() on every visit. That seam
// dominates the overlay call (0x0229c2) and the final-FOV seam (0x022cf5) in the updater's complete CFG (verifier), so
// every gate reached in an updater invocation was preceded by that invocation's pose visit. The published pose is the
// update token: {generation = serial = a per-publication counter, thread}, valid for that cockpit on that thread until
// the next pose visit or the final-FOV seam, which closes it. EBX (the cockpit) is the live `this` of the enclosing
// updater invocation throughout. One path leaves a token open: 0x022a0d `je 0x022fa9` (cockpit+0xc == 0 after the HUD
// calls) skips the final-FOV seam, so that pose stays open until the next pose-seam visit invalidates it. A gate cannot
// reach it first (the next overlay call is dominated by that visit); a leftover ticket fails at scope()/owned_marker()
// (the token no longer matches), so nothing is written from it, and its anchor slot is reclaimed at that visit.
#include "../../proxy/chase_lead.h"
#include "../../proxy/config.h"
#include "../../proxy/chase_lead_core.h"
#include "../../proxy/chase_camera.h"
#include "../../proxy/engine_patch.h"
#include "../../proxy/engine_memory.h"
#include "../../proxy/cpu_state.h"
#include "../../proxy/capture.h"
#include "../../proxy/telemetry.h"
#include "executable_identity_fl.h"
#include "chase_lead_sites_fl.h"
#include <atomic>
#include <cstring>
#include <cwchar>

static_assert(sizeof(void*) == 4, "Verified x86 engine ABI");
namespace x3m::chase_lead {
namespace {
namespace S = x3m::fl::chase_lead_fl;
constexpr unsigned O = S::overlay, extent = S::overlay + S::overlay_extent;
constexpr const S::Seam* seams[4] = {&S::gate, &S::publish, &S::final_fov, &S::hud_gate};
std::uintptr_t base_ = 0;
#ifdef X3M_CHASE_LEAD_FL_FIXTURE
std::uintptr_t fixture_base_ = 0;
unsigned fixture_fault_site_ = 4, fixture_fault_offset_[2] = {6, 6};
#endif
engine_patch::SiteSpec specs[4]{};
engine_patch::Site sites[3];
engine_patch::Site hud_site;
std::atomic<bool> hud_enabled{false};
std::atomic<bool> enabled{false};
bool initialized = false;
SRWLOCK lock = SRWLOCK_INIT;
core::Identity pose{}; // also the update token (see the header comment)
std::uint64_t pose_counter = 0;
std::int32_t pose_forward[3]{};
bool pose_forward_valid = false;
struct Slot {
    std::uint32_t frame = 0;
    core::Pending ticket{};
};
Slot pending[8]{};
struct HudAnchorSlot {
    core::Identity owner{};
    std::int32_t forward[3]{};
};
HudAnchorSlot hud_anchor_pending[8]{};
bool hud_anchor_forward = false;
std::atomic<std::uint32_t> owner_thread{0};
std::uint64_t frequency = 0;
bool timing = false;
const char* lead_rollback = "none";
const char* hud_rollback = "none";
const char* refused_window = "-";
inline double as_double(std::uint64_t v) {
    return double(std::uint32_t(v >> 32)) * 4294967296.0 + double(std::uint32_t(v));
}
enum Reason { Ready, Committed, Finalized, NoPose, Lifetime, Read, Scope, Identity, Projection, Marker, Hidden, Skipped, Count };
struct Sample {
    core::Identity owner{};
    core::Projection early{}, late{};
    std::int32_t point[3]{}, native_x = 0, native_y = 0, x = 0, y = 0;
    unsigned reason = 0;
};
struct Counts {
    std::uint64_t reason[Count]{}, calls = 0, ticks = 0, max_ticks = 0, samples = 0, dropped = 0;
    Sample first[4]{}, last{};
} counts;
struct HudCounts {
    std::uint64_t reason[Count]{}, calls = 0, ticks = 0, max_ticks = 0;
} hud_counts;
struct HudAnchorCounts {
    std::uint64_t applied = 0, refused = 0;
    std::int32_t last_x = 0, last_y = 0;
} hud_anchor_counts;

struct Update {
    std::uint64_t generation = 0, serial = 0;
    std::uint32_t thread = 0;
};
// The open update token of `cockpit` on this thread, or zeros.
Update current_update(std::uint32_t cockpit) {
    Update u;
    const auto thread = GetCurrentThreadId();
    AcquireSRWLockExclusive(&lock);
    if (pose.serial && pose.cockpit == cockpit && pose.thread == thread) u = {pose.generation, pose.serial, pose.thread};
    ReleaseSRWLockExclusive(&lock);
    return u;
}
std::uint64_t generation(std::uint32_t cockpit) {
    return current_update(cockpit).generation;
}
bool read(std::uintptr_t base, unsigned offset, void* out, unsigned size) {
    return base && !(base & 3) && offset <= UINT32_MAX - base && size <= UINT32_MAX - base - offset &&
           engine_memory::read(base + offset, out, size);
}
template <class T> bool field(std::uintptr_t base, unsigned offset, T& out) {
    return read(base, offset, &out, sizeof out);
}
// Image globals (base + RVA); the RVAs are relocated dwords of the admitted windows.
template <class T> bool global(std::uint32_t rva, T& out) {
    return base_ && engine_memory::read(base_ + rva, &out, sizeof out);
}
bool writable(std::uintptr_t at, unsigned size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!at || size > UINT32_MAX - at || VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof info) != sizeof info)
        return false;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) &&
           at + size <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
}
bool writable_at(std::uint32_t base, unsigned offset, unsigned size) {
    return base && offset <= UINT32_MAX - base && size <= UINT32_MAX - base - offset && writable(base + offset, size);
}
// The registry's active control cockpit (resolver 0x01e420's walk, bounded): a nonzero active handle whose row maps to
// this cockpit.
bool active(std::uint32_t cockpit) {
    std::uint32_t registry = 0, table = 0, handle = 0, bucket[2]{}, link = 0;
    if (!global(S::registry_slot_rva, registry) || !field(registry, 0, table) || !field(registry, 0x10, handle) ||
        !handle || !field(table, 0, bucket))
        return false;
    const auto n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !field(bucket[0], 4 * ((n - 1) & handle), link)) return false;
    for (unsigned i = 0; link && i < 32; ++i) {
        std::uint32_t row[3]{};
        if (!field(link, 0, row)) return false;
        if (row[1] == handle) return row[2] == cockpit;
        link = row[0];
    }
    return false;
}
Reason scope(const core::Identity& base, core::Identity& out) {
    if (base.cockpit > UINT32_MAX - extent) return Read;
    const auto update = current_update(base.cockpit);
    if (!update.generation || update.generation != base.generation || update.serial != base.serial ||
        update.thread != base.thread)
        return Lifetime;
    if (!active(base.cockpit)) return Identity;
    std::uint32_t refs[2]{}, mode = 0, connect = 0, flags = 0, target = 0, sector = 0, camera = 0, scene = 0, gun = 0,
                             owners[2]{};
    std::uint16_t types[2]{}, tracking = 0;
    if (!field(base.cockpit, 0xc, refs) || !field(base.cockpit, 0x150, mode) || !field(base.cockpit, 0x1c0, connect) ||
        !field(base.cockpit, 0x1a0, flags) || !field(base.cockpit, 0x1d8, gun) || !field(base.cockpit, 0x1e0, target) ||
        !field(base.cockpit, 0x1e4, tracking) || !field(base.cockpit, 0x54, sector) ||
        !field(base.cockpit, 0x58, camera) || !field(base.cockpit, 4, scene))
        return Read;
    if (refs[0] != base.ship || refs[1] != base.ship || camera != base.camera) return Identity;
    if (mode != 258 || connect || flags & 4 || gun || tracking < 2 || tracking > 3 || !target || !sector || !scene)
        return Scope;
    out = base;
    out.target = target;
    out.sector = sector;
    out.scene = scene;
    out.overlay = base.cockpit + O;
    std::uint32_t bodies = 0;
    if (!field(base.ship, 8, out.ship_id) || !field(target, 8, out.target_id) || !field(base.ship, 0x54, owners[0]) ||
        !field(target, 0x54, owners[1]) || !field(base.ship, 0x48, types[0]) || !field(sector, 0x48, types[1]) ||
        !field(out.overlay, S::overlay_bodies, bodies))
        return Read;
    if (out.ship_id != base.ship_id || owners[0] != sector || owners[1] != sector || types[0] != 7 || types[1] != 1 ||
        !bodies)
        return Scope;
    return Ready;
}
bool projection(std::uint32_t cockpit, std::uint32_t camera, core::Projection& p) {
    std::uint32_t hud = 0, defaults = 0, screen = 0, viewptr[2]{}, rect[2][6]{};
    std::int32_t vp[4]{}, planes[2]{}, fallback[2]{};
    std::int16_t dims[2]{};
    if (!field(cockpit, 8, hud) || !field(camera, 0x30, p.position) || !field(camera, 0x40, p.basis) ||
        !field(camera, 0x298, p.fov) || !field(camera, 0x278, viewptr[0]) || !field(hud, 0x278, viewptr[1]) ||
        !field(viewptr[0], 0, rect[0]) || !field(viewptr[1], 0, rect[1]) || !field(camera, 0x288, p.viewport) ||
        !field(camera, 0x300, p.plane) || !field(hud, 0x288, vp) || !field(hud, 0x300, planes) ||
        !global(S::defaults_slot_rva, defaults) || !field(defaults, 0, screen) || !field(defaults, 0x28, fallback) ||
        !field(screen, 4, dims))
        return false;
    if (std::memcmp(vp, p.viewport, sizeof vp) || std::memcmp(rect[0], rect[1], 16)) return false;
    if (p.plane[0] <= 0 || p.plane[1] <= 0) std::memcpy(p.plane, fallback, sizeof fallback);
    if (planes[0] <= 0 || planes[1] <= 0) std::memcpy(planes, fallback, sizeof fallback);
    for (unsigned i = 0; i < 2; ++i) {
        if (planes[i] != p.plane[i]) return false;
        p.screen[i] = dims[i];
    }
    if (!core::valid(p)) return false;
    const auto w = std::uint32_t((p.screen[0] * (p.viewport[3] - p.viewport[2]) + 32768) / 65536);
    const auto h = std::uint32_t((p.screen[1] * (p.viewport[1] - p.viewport[0]) + 32768) / 65536);
    return rect[0][2] == w && rect[0][3] == h && w <= std::uint32_t(p.screen[0]) && h <= std::uint32_t(p.screen[1]) &&
           rect[0][0] <= std::uint32_t(p.screen[0]) - w && rect[0][1] <= std::uint32_t(p.screen[1]) - h;
}
bool owned_marker(const core::Identity& owner, std::uint32_t& marker, bool require_active = true) {
    if (generation(owner.cockpit) != owner.generation) return false;
    std::uint32_t scene = 0, entry[2]{}, context = 0, flags = 0;
    if (!field(owner.cockpit, 4, scene) || scene != owner.scene || !field(owner.overlay, S::lead_entry, entry) ||
        !entry[1] || (require_active && !entry[0]) || !field(entry[1], 0x1c, context) || context != scene ||
        !field(entry[1], 0x130, flags) || !(flags & 0x200) || (flags & 0xf000) ||
        (owner.marker && entry[1] != owner.marker))
        return false;
    marker = entry[1];
    return writable_at(marker, 0x30, 8) && writable_at(owner.overlay, S::overlay_cached, 8) &&
           writable_at(owner.overlay, S::lead_entry, 8);
}
__attribute__((noinline)) void hide_native(std::uint32_t entry) {
    // Native helper base + 0x027c20: ECX = overlay entry, no stack arguments, plain RET; it saves ESI itself and calls
    // the detacher (thiscall, callee-cleaned). Its complete 31 bytes are an admitted window. No lock held across it.
    const std::uint32_t target = std::uint32_t(base_ + S::hide_rva);
    asm volatile("call *%1" : "+c"(entry) : "r"(target)
                 : "eax", "edx", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "memory", "cc");
}
void hide(const core::Identity& owner) {
    std::uint32_t marker = 0;
    if (!owned_marker(owner, marker)) return;
    hide_native(owner.overlay + S::lead_entry);
    if (generation(owner.cockpit) != owner.generation) return;
    std::uint32_t scene = 0, node = 0;
    if (!field(owner.cockpit, 4, scene) || scene != owner.scene || !field(owner.overlay, S::lead_entry + 4, node) ||
        node != marker || !writable_at(owner.overlay, S::overlay_cached, 8))
        return;
    const std::int32_t invalid[2] = {-1, -1};
    std::memcpy(reinterpret_cast<void*>(owner.overlay + S::overlay_cached), invalid, 8);
}
void note(Reason r, const Sample* sample = nullptr) {
    AcquireSRWLockExclusive(&lock);
    ++counts.reason[r];
    if (timing && sample) {
        if (counts.samples < 4)
            counts.first[counts.samples] = *sample;
        else if (counts.samples >= 5)
            ++counts.dropped;
        counts.last = *sample;
        ++counts.samples;
    }
    ReleaseSRWLockExclusive(&lock);
}
// regs: the pushad block (EDI ESI EBP ESP EBX EDX ECX EAX), then EFLAGS.
void gate(std::uint32_t* regs) {
    core::Identity base;
    AcquireSRWLockExclusive(&lock);
    base = pose;
    for (auto& slot : pending)
        if (slot.frame == regs[2]) slot = {};
    ReleaseSRWLockExclusive(&lock);
    if (!(regs[8] & 0x40)) return; // native bit-set path is untouched
    if (!base.serial || base.cockpit > UINT32_MAX - extent || regs[1] != base.cockpit || regs[4] != base.cockpit + O) {
        note(NoPose);
        return;
    }
    core::Identity owner;
    auto r = scope(base, owner);
    core::Projection p;
    if (r == Ready && !projection(owner.cockpit, owner.camera, p)) r = Projection;
    if (r != Ready) {
        note(r);
        return;
    }
    bool stored = false;
    AcquireSRWLockExclusive(&lock);
    for (auto& slot : pending)
        if (!slot.frame) {
            slot.frame = regs[2];
            slot.ticket.admit(owner);
            stored = true;
            break;
        }
    ReleaseSRWLockExclusive(&lock);
    if (stored) {
        regs[8] &= ~0x40u;
        note(Ready);
    } else
        note(Lifetime);
}
void publish(std::uint32_t* regs) {
    core::Pending ticket;
    AcquireSRWLockExclusive(&lock);
    for (auto& slot : pending)
        if (slot.frame == regs[2]) {
            ticket = slot.ticket;
            slot = {};
            break;
        }
    ReleaseSRWLockExclusive(&lock);
    if (!ticket.admitted || regs[4] != ticket.owner.overlay) return;
    core::Identity current;
    auto r = scope(ticket.owner, current);
    if (r == Ready && !ticket.matches(current)) r = Identity;
    std::uint32_t marker = 0;
    const bool owned = owned_marker(ticket.owner, marker);
    core::Pixel pixel;
    Sample sample;
    sample.owner = ticket.owner;
    sample.native_x = regs[6];
    sample.native_y = regs[5];
    // pushfd precedes pushad: the saved ESP is the overlay's local base P minus four; the solver's point is P+0xe0.
    if (r == Ready && (!owned || !field(regs[3], S::solver_point_slot + 4, ticket.point))) r = owned ? Read : Marker;
    if (r == Ready && (!projection(current.cockpit, current.camera, ticket.projection) ||
                       !core::project(ticket.projection, ticket.point, pixel)))
        r = Projection;
    if (r != Ready) {
        if (owned) {
            hide(ticket.owner);
            regs[6] = regs[5] = UINT32_MAX;
        }
        note(r);
        return;
    }
    const std::int32_t xy[2] = {pixel.x, pixel.y};
    std::memcpy(reinterpret_cast<void*>(marker + 0x30), xy, 8);
    regs[6] = std::uint32_t(pixel.x);
    regs[5] = std::uint32_t(pixel.y);
    ticket.owner.marker = marker;
    ticket.published = true;
    AcquireSRWLockExclusive(&lock);
    for (auto& slot : pending)
        if (!slot.frame) {
            slot.frame = regs[2];
            slot.ticket = ticket;
            break;
        }
    ReleaseSRWLockExclusive(&lock);
    sample.owner = ticket.owner;
    sample.early = ticket.projection;
    std::memcpy(sample.point, ticket.point, 12);
    sample.x = pixel.x;
    sample.y = pixel.y;
    sample.reason = Committed;
    note(Committed, &sample);
}
void finalize_hud_anchor(std::uint32_t* regs);
void final_fov(std::uint32_t* regs) {
    finalize_hud_anchor(regs);
    core::Pending ticket;
    const auto update = current_update(regs[4]);
    AcquireSRWLockExclusive(&lock);
    for (auto& slot : pending)
        if (slot.ticket.owner.cockpit == regs[4] && (!update.serial || slot.ticket.owner.serial == update.serial)) {
            if (slot.ticket.published) ticket = slot.ticket;
            slot = {};
        }
    ReleaseSRWLockExclusive(&lock);
    if (!ticket.published) return;
    core::Identity current;
    auto r = scope(ticket.owner, current);
    if (r == Ready && !ticket.matches(current)) r = Identity;
    std::uint32_t marker = 0;
    if (r == Ready && !owned_marker(ticket.owner, marker)) r = Marker;
    core::Projection p;
    core::Pixel pixel;
    if (r == Ready && !projection(current.cockpit, current.camera, p)) r = Projection;
    if (r != Ready) {
        hide(ticket.owner);
        note(r);
        return;
    }
    if (core::same(p, ticket.projection)) {
        note(Skipped);
        return;
    }
    if (!core::project(p, ticket.point, pixel)) {
        hide(ticket.owner);
        note(Projection);
        return;
    }
    const std::int32_t xy[2] = {pixel.x, pixel.y};
    std::memcpy(reinterpret_cast<void*>(marker + 0x30), xy, 8);
    std::memcpy(reinterpret_cast<void*>(ticket.owner.overlay + S::overlay_cached), xy, 8);
    Sample s;
    s.owner = ticket.owner;
    s.early = ticket.projection;
    s.late = p;
    std::memcpy(s.point, ticket.point, 12);
    s.x = pixel.x;
    s.y = pixel.y;
    s.reason = Finalized;
    note(Finalized, &s);
}
// The final-FOV seam ends this updater invocation's use of the pose: close the token so nothing later in the
// invocation (or a later invocation that would not pass the pose seam) can reuse it.
void close_update(std::uint32_t cockpit) {
    const auto thread = GetCurrentThreadId();
    AcquireSRWLockExclusive(&lock);
    if (pose.serial && pose.cockpit == cockpit && pose.thread == thread) {
        pose = {};
        pose_forward_valid = false;
        std::memset(pose_forward, 0, sizeof pose_forward);
    }
    ReleaseSRWLockExclusive(&lock);
}
// Texture 15 is refreshed by the native distance/speed producer later in this update (base + 0x0266f0 through the
// query base + 0x1123a0). Its lazy loader is never called from here: a missing cached texture leaves native hiding.
bool hud_texture_available() {
    std::int16_t first_count = 0, a = 0, b = 0, metadata = 0;
    std::uint32_t rows = 0, textures = 0, surface = 0;
    if (!global(S::texture_meta_count_rva, first_count) || !global(S::texture_count_a_rva, a) ||
        !global(S::texture_count_b_rva, b))
        return false;
    if (15 < first_count &&
        (!global(S::texture_meta_rows_rva, rows) || !field(rows, 15 * 0x3c + 0xc, metadata) || !metadata))
        return false;
    return 15 < int(a) + int(b) && global(S::texture_rows_rva, textures) && field(textures, 15 * 0x10 + 8, surface) &&
           surface;
}
Reason hud_scope(const core::Identity& base, core::Identity& out) {
    if (!base.serial || !base.generation || base.cockpit > UINT32_MAX - extent) return Lifetime;
    const auto update = current_update(base.cockpit);
    if (base.thread != GetCurrentThreadId() || update.generation != base.generation || update.serial != base.serial ||
        update.thread != base.thread)
        return Lifetime;
    if (!active(base.cockpit)) return Identity;
    std::uint32_t refs[2]{}, mode = 0, connect = 0, flags = 0, gun = 0, camera = 0, scene = 0, draw2d = 0, display = 0,
                             scene_flags = 0, owner = 0, bodies = 0, back = 0;
    std::uint16_t ship_type = 0, owner_type = 0;
    out = base;
    out.overlay = base.cockpit + O;
    if (!field(base.cockpit, 0xc, refs) || !field(base.cockpit, 0x150, mode) || !field(base.cockpit, 0x1c0, connect) ||
        !field(base.cockpit, 0x1a0, flags) || !field(base.cockpit, 0x1d8, gun) || !field(base.cockpit, 0x58, camera) ||
        !field(base.cockpit, 4, scene) || !field(base.cockpit, S::cockpit_draw2d, draw2d) ||
        !field(base.cockpit, S::cockpit_display, display) || !field(base.ship, 8, out.ship_id) ||
        !field(base.ship, 0x48, ship_type) || !field(base.ship, 0x54, owner) || !field(owner, 0x48, owner_type) ||
        !field(scene, 0x18, scene_flags) || !field(out.overlay, S::overlay_bodies, bodies) ||
        !field(out.overlay, S::overlay_cockpit, back))
        return Read;
    if (refs[0] != base.ship || refs[1] != base.ship || camera != base.camera || out.ship_id != base.ship_id ||
        back != base.cockpit)
        return Identity;
    if (mode != 258 || connect || (flags & 4) || gun || ship_type != 7 || owner_type != 1 || !draw2d || !display ||
        !(scene_flags & 1) || !bodies)
        return Scope;
    out.scene = scene;
    out.sector = owner;
    // The native rejected branch hides eight entries, the admitted path updates five: with any of the other three
    // active, leave the native branch to clean up; admission can follow on a later update. No detacher call from here.
    for (const unsigned offset : S::extra_entries) {
        std::uint32_t entry_active = 0;
        if (!field(out.overlay, offset, entry_active)) return Read;
        if (entry_active) return Marker;
    }
    return hud_texture_available() ? Ready : Projection;
}
bool owned_hud_nodes(const core::Identity& owner, std::uint32_t nodes[5], unsigned& active_count) {
    if (generation(owner.cockpit) != owner.generation) return false;
    std::uint32_t scene = 0;
    if (!field(owner.cockpit, 4, scene) || scene != owner.scene) return false;
    active_count = 0;
    for (unsigned i = 0; i < 5; ++i) {
        std::uint32_t entry[2]{}, node_scene = 0, flags = 0;
        if (!field(owner.overlay, S::hud_entries[i], entry)) return false;
        if (!entry[0]) continue;
        if (!entry[1] || !field(entry[1], 0x1c, node_scene) || node_scene != scene || !field(entry[1], 0x130, flags) ||
            !(flags & 0x200) || (flags & 0xf000) || !writable_at(entry[1], 0x30, 8))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (nodes[j] == entry[1]) return false;
        nodes[i] = entry[1];
        ++active_count;
    }
    return active_count && generation(owner.cockpit) == owner.generation && field(owner.cockpit, 4, scene) &&
           scene == owner.scene;
}
void reclaim_hud_anchor_slots() {
    HudAnchorSlot snapshot[8]{};
    bool stale[8]{};
    AcquireSRWLockExclusive(&lock);
    std::memcpy(snapshot, hud_anchor_pending, sizeof snapshot);
    ReleaseSRWLockExclusive(&lock);
    for (unsigned i = 0; i < 8; ++i)
        if (snapshot[i].owner.serial) {
            const auto update = current_update(snapshot[i].owner.cockpit);
            stale[i] = update.generation != snapshot[i].owner.generation || update.serial != snapshot[i].owner.serial ||
                       update.thread != snapshot[i].owner.thread;
        }
    AcquireSRWLockExclusive(&lock);
    for (unsigned i = 0; i < 8; ++i)
        if (stale[i] && core::same(hud_anchor_pending[i].owner, snapshot[i].owner)) hud_anchor_pending[i] = {};
    ReleaseSRWLockExclusive(&lock);
}
void finalize_hud_anchor(std::uint32_t* regs) {
    if (!hud_anchor_forward) return;
    const auto update = current_update(regs[4]);
    HudAnchorSlot ticket;
    AcquireSRWLockExclusive(&lock);
    for (auto& slot : hud_anchor_pending)
        if (slot.owner.cockpit == regs[4] && (!update.serial || slot.owner.serial == update.serial)) {
            if (slot.owner.serial == update.serial) ticket = slot;
            slot = {};
        }
    ReleaseSRWLockExclusive(&lock);
    if (!ticket.owner.serial) return;
    core::Identity current;
    auto r = hud_scope(ticket.owner, current);
    if (r == Ready && !core::same(ticket.owner, current)) r = Identity;
    core::Projection p;
    core::Pixel pixel;
    if (r == Ready &&
        (!projection(current.cockpit, current.camera, p) || !core::project_direction(p, ticket.forward, pixel)))
        r = Projection;
    std::uint32_t nodes[5]{};
    unsigned active_count = 0;
    if (r == Ready && !owned_hud_nodes(ticket.owner, nodes, active_count)) r = Marker;
    std::int32_t positions[5][2]{};
    if (r == Ready) {
        constexpr std::int32_t native[5][2] = {{0, 9}, {-70, -10}, {70, -10}, {0, 40}, {1, -25}};
        for (unsigned i = 0; i < 5; ++i) {
            if (!nodes[i]) continue;
            const std::int64_t x = std::int64_t(native[i][0]) + pixel.x;
            const std::int64_t y = std::int64_t(native[i][1]) + pixel.y;
            if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {
                r = Projection;
                break;
            }
            positions[i][0] = std::int32_t(x);
            positions[i][1] = std::int32_t(y);
        }
    }
    if (r == Ready)
        for (unsigned i = 0; i < 5; ++i)
            if (nodes[i]) std::memcpy(reinterpret_cast<void*>(nodes[i] + 0x30), positions[i], sizeof positions[i]);
    AcquireSRWLockExclusive(&lock);
    if (r == Ready) {
        ++hud_anchor_counts.applied;
        hud_anchor_counts.last_x = pixel.x;
        hud_anchor_counts.last_y = pixel.y;
    } else
        ++hud_anchor_counts.refused;
    ReleaseSRWLockExclusive(&lock);
}
void central_hud(std::uint32_t* regs) {
    if (!hud_enabled.load(std::memory_order_acquire) || !(regs[8] & 0x40)) return;
    if (hud_anchor_forward) reclaim_hud_anchor_slots();
    core::Identity base;
    std::int32_t forward[3]{};
    bool forward_valid = false;
    AcquireSRWLockExclusive(&lock);
    base = pose;
    std::memcpy(forward, pose_forward, sizeof forward);
    forward_valid = pose_forward_valid;
    ReleaseSRWLockExclusive(&lock);
    core::Identity owner;
    Reason r = NoPose;
    if (base.serial && regs[7] == base.cockpit && base.cockpit <= UINT32_MAX - extent && regs[4] == base.cockpit + O)
        r = hud_scope(base, owner);
    if (r == Ready) regs[8] &= ~0x40u;
    AcquireSRWLockExclusive(&lock);
    ++hud_counts.reason[r];
    if (r == Ready && hud_anchor_forward) {
        bool stored = false;
        for (auto& slot : hud_anchor_pending)
            if (slot.owner.cockpit == owner.cockpit && slot.owner.serial == owner.serial) slot = {};
        if (forward_valid)
            for (auto& slot : hud_anchor_pending)
                if (!slot.owner.serial) {
                    slot.owner = owner;
                    std::memcpy(slot.forward, forward, sizeof forward);
                    stored = true;
                    break;
                }
        if (!stored) ++hud_anchor_counts.refused;
    }
    ReleaseSRWLockExclusive(&lock);
}
void handle(unsigned index, std::uint32_t* regs) {
    if (!enabled.load(std::memory_order_acquire) || index > 3 ||
        owner_thread.load(std::memory_order_relaxed) != GetCurrentThreadId())
        return;
    LARGE_INTEGER start{}, end{};
    if (timing) QueryPerformanceCounter(&start);
    if (index == 0)
        gate(regs);
    else if (index == 1)
        publish(regs);
    else if (index == 2) {
        final_fov(regs);
        close_update(regs[4]);
    } else
        central_hud(regs);
    if (timing) {
        QueryPerformanceCounter(&end);
        const auto ticks = end.QuadPart > start.QuadPart ? std::uint64_t(end.QuadPart - start.QuadPart) : 0;
        AcquireSRWLockExclusive(&lock);
        if (index == 3) {
            ++hud_counts.calls;
            hud_counts.ticks += ticks;
            if (ticks > hud_counts.max_ticks) hud_counts.max_ticks = ticks;
        } else {
            ++counts.calls;
            counts.ticks += ticks;
            if (ticks > counts.max_ticks) counts.max_ticks = ticks;
        }
        ReleaseSRWLockExclusive(&lock);
    }
}
} // namespace
} // namespace x3m::chase_lead
// cdecl from the generated stub: index, then the pushad block. The game's stack is 4-byte aligned mid-function
// (force_align_arg_pointer). Full CPU boundary: SSE2/libm work under a clean masked x87 and MXCSR 0x1f80; the native
// x87 image, MXCSR and LastError come back before the displaced instruction replays.
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_lead_fl_enter(unsigned index,
                                                                                         std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_lead::handle(index, regs);
}
namespace x3m::chase_lead {
namespace {
// Stub (entry after its aligned continuation slot):
//   pushfd; cmp byte [flag],0; jne main; popfd; jmp [next]          <- disabled: one byte test, flags/stack restored
//   main: pushad; cld; sub esp,0x80; movups [esp+16*i],xmm_i; lea eax,[esp+0x80]; push eax; push index; call enter;
//   add esp,8; movups xmm_i,[esp+16*i]; add esp,0x80; popad; popfd; jmp [next]
// `flag` is `enabled` for the marker seams and `hud_enabled` for the HUD gate (the handler re-tests both). The pushad
// block and the saved EFLAGS above it are laid out as in the AP module's stub.
void* emit(unsigned index, void*** next_out) {
    engine_patch::Emitter e(200);
    if (!e.ok()) return nullptr;
    while (e.ok() && (reinterpret_cast<std::uintptr_t>(e.here()) & 3)) e.byte(0xcc);
    auto* slot = static_cast<void**>(e.here());
    e.dword(0);
    void* start = e.here();
    const std::atomic<bool>& flag = index == 3 ? hud_enabled : enabled;
    static_assert(sizeof(std::atomic<bool>) == 1, "one-byte flag");
    e.byte(0x9c);
    e.byte(0x80);
    e.byte(0x3d);
    e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(&flag)));
    e.byte(0x00);
    e.byte(0x75);
    e.byte(0x07);
    e.byte(0x9d);
    e.byte(0xff);
    e.byte(0x25);
    e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(slot)));
    e.byte(0x60);
    e.byte(0xfc);
    e.byte(0x81);
    e.byte(0xec);
    e.dword(0x80);
    for (unsigned i = 0; i < 8; ++i) {
        e.byte(0x0f);
        e.byte(0x11);
        e.byte(static_cast<unsigned char>(0x44 | (i << 3)));
        e.byte(0x24);
        e.byte(static_cast<unsigned char>(i * 16));
    }
    e.byte(0x8d);
    e.byte(0x84);
    e.byte(0x24);
    e.dword(0x80);
    e.byte(0x50);
    e.byte(0x68);
    e.dword(index);
    e.byte(0xe8);
    e.rel32(reinterpret_cast<void*>(&x3m_chase_lead_fl_enter));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(8);
    for (unsigned i = 0; i < 8; ++i) {
        e.byte(0x0f);
        e.byte(0x10);
        e.byte(static_cast<unsigned char>(0x44 | (i << 3)));
        e.byte(0x24);
        e.byte(static_cast<unsigned char>(i * 16));
    }
    e.byte(0x81);
    e.byte(0xc4);
    e.dword(0x80);
    e.byte(0x61);
    e.byte(0x9d);
    e.byte(0xff);
    e.byte(0x25);
    e.dword(std::uint32_t(reinterpret_cast<std::uintptr_t>(slot)));
    const bool complete = e.ok();
    *next_out = slot;
    return e.finish() && complete ? start : nullptr;
}
std::uintptr_t fl_base() {
#ifdef X3M_CHASE_LEAD_FL_FIXTURE
    if (fixture_base_) return fixture_base_;
#endif
    return x3m::fl::module_base();
}
bool read_code(std::uintptr_t address, void* out, unsigned size) {
    return engine_patch::read_code(address, static_cast<unsigned char*>(out), size);
}
void bind_spec(unsigned i) {
    const S::Seam& s = *seams[i];
    specs[i] = engine_patch::SiteSpec{};
    specs[i].name = s.name;
    specs[i].address = base_ + s.rva;
    std::memcpy(specs[i].expected, s.bytes, 6);
    specs[i].length = 6;
    specs[i].ret_pop = 0;
    specs[i].rel32_offset = s.rel32_offset;
}
// The FL read-back rollback (the engine_effects_fl / chase_camera FL convention): a site that does not read back as
// this module's jump is not owned, so engine_patch::restore() would refuse it. Inside the install window the original
// first five bytes go back over whatever is there; restored only when all six bytes then read back original.
bool force_restore(engine_patch::Site& site) {
    auto* code = reinterpret_cast<void*>(site.spec.address);
    DWORD writable = 0, unused = 0;
    if (VirtualProtect(code, 6, PAGE_EXECUTE_READWRITE, &writable)) {
        engine_patch::write_code(site.spec.address, site.original, 5);
        FlushInstructionCache(GetCurrentProcess(), code, 6);
        VirtualProtect(code, 6, site.protection, &unused);
    }
    unsigned char back[6]{};
    if (!engine_patch::read_code(site.spec.address, back, 6) || std::memcmp(back, site.original, 6)) return false;
    site.patched_in = false;
    return true;
}
// Claim, read back, chain one stub. On failure: the status names the step; *rollback says what happened to this
// site's bytes ("restored", "failed", or "none" when nothing was written).
bool install_site(engine_patch::Site& site, unsigned index, const char*& status, const char*& rollback) {
    rollback = "none";
    if (!engine_patch::claim(site, specs[index])) {
        status = site.status;
        if (site.patched_in) rollback = "failed";
        else if (!std::strcmp(site.status, "patch_rolled_back")) rollback = "restored";
        return false;
    }
#ifdef X3M_CHASE_LEAD_FL_FIXTURE
    if (fixture_fault_site_ == index) { // a foreign write between the claim and the read-back
        for (const unsigned offset : fixture_fault_offset_)
            if (offset < 6) *reinterpret_cast<unsigned char*>(site.spec.address + offset) ^= 0xff;
        fixture_fault_site_ = 4;
    }
#endif
    unsigned char now[5]{};
    if (!engine_patch::read_code(site.spec.address, now, 5) || std::memcmp(now, site.patched, 5)) {
        status = "readback_mismatch";
        rollback = force_restore(site) ? "restored" : "failed";
        return false;
    }
    void** next = nullptr;
    void* stub = emit(index, &next);
    const char* failure = !stub || !next                                       ? "stub_failed"
                          : !engine_patch::store_pointer(next, *site.entry)    ? "chain_write_failed"
                          : !engine_patch::push_front(site, stub)              ? "chain_failed"
                                                                               : nullptr;
    if (failure) {
        status = failure;
        rollback = engine_patch::restore(site) ? "restored" : "failed";
        return false;
    }
    status = "active";
    return true;
}
} // namespace
#ifdef X3M_CHASE_LEAD_FL_FIXTURE
void fixture_base(std::uintptr_t base) {
    fixture_base_ = base;
}
void fixture_reset() {
    initialized = false;
    for (auto& site : sites) site = engine_patch::Site{};
    hud_site = engine_patch::Site{};
    enabled.store(false);
    hud_enabled.store(false);
    owner_thread.store(0);
    AcquireSRWLockExclusive(&lock);
    pose = {};
    pose_forward_valid = false;
    for (auto& slot : pending) slot = {};
    for (auto& slot : hud_anchor_pending) slot = {};
    counts = {};
    hud_counts = {};
    hud_anchor_counts = {};
    ReleaseSRWLockExclusive(&lock);
    base_ = 0;
    lead_rollback = hud_rollback = "none";
    refused_window = "-";
}
void fixture_fault_readback(unsigned site, unsigned offset, unsigned second) {
    fixture_fault_site_ = site;
    fixture_fault_offset_[0] = offset;
    fixture_fault_offset_[1] = second;
}
FixtureCounts fixture_counts() {
    FixtureCounts f;
    AcquireSRWLockExclusive(&lock);
    for (unsigned i = 0; i < Count && i < 12; ++i) {
        f.lead[i] = counts.reason[i];
        f.hud[i] = hud_counts.reason[i];
    }
    f.anchor_applied = hud_anchor_counts.applied;
    f.anchor_refused = hud_anchor_counts.refused;
    f.anchor_x = hud_anchor_counts.last_x;
    f.anchor_y = hud_anchor_counts.last_y;
    f.pose_open = pose.serial != 0;
    ReleaseSRWLockExclusive(&lock);
    return f;
}
bool hud_installed() {
    return hud_enabled.load();
}
void fixture_enable(bool lead, bool hud) {
    enabled.store(lead);
    hud_enabled.store(hud);
}
#endif
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized) {
        SetLastError(error);
        return installed();
    }
    initialized = true;
    wchar_t anchor[16]{};
    // A zero return is "unset" only with ERROR_ENVVAR_NOT_FOUND; an empty value also returns 0 and keeps centre.
    SetLastError(ERROR_SUCCESS);
    const DWORD anchor_length = x3m::config::get(L"X3M_CHASE_HUD_ANCHOR", anchor, 16);
    const bool anchor_present = anchor_length != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
    hud_anchor_forward = core::anchor_forward_setting(anchor_present, anchor, anchor_length);
    timing = telemetry::enabled();
    LARGE_INTEGER f{};
    if (QueryPerformanceFrequency(&f) && f.QuadPart > 0) frequency = f.QuadPart;
    const std::uintptr_t base = fl_base();
    const char* status = nullptr;
    if (!chase_camera::installed())
        status = "chase_camera_unavailable";
    else if (!base)
        status = "executable_mismatch";
    else if (!engine_patch::install_window_open())
        status = "late_claim";
    else if ((status = S::plan(base, read_code)) != nullptr && std::strcmp(status, "invalid_base")) {
        const char* w = S::refused_window(base, read_code);
        refused_window = w ? w : "-";
    }
    bool ok = status == nullptr;
    if (ok) {
        base_ = base;
        for (unsigned i = 0; i < 4; ++i) bind_spec(i);
    }
    // The marker's three sites: all or none. The failing site reports its own rollback (install_site); the sites
    // before it are ours and go back through engine_patch::restore in reverse order. A site that cannot be restored
    // stays registered with its stub chained but inert: enabled stays false, so the handler returns at once.
    unsigned claimed = 0;
    const char* failed_rollback = "none";
    for (; ok && claimed < 3; ++claimed)
        if (!install_site(sites[claimed], claimed, status, failed_rollback)) ok = false;
    if (!ok && base_) {
        bool restored = std::strcmp(failed_rollback, "failed") != 0;
        const unsigned owned = claimed ? claimed - 1 : 0; // claimed counts the failing site too
        for (unsigned i = owned; i-- > 0;)
            if (!engine_patch::restore(sites[i])) restored = false;
        lead_rollback = !owned && !std::strcmp(failed_rollback, "none") ? "none" : restored ? "restored" : "failed";
    }
    enabled.store(ok, std::memory_order_release);
    // Optional independent transaction: the central instruments never disable the marker if their one site fails.
    const char* hud_status = ok ? nullptr : "lead_unavailable";
    bool hud_ok = ok && install_site(hud_site, 3, hud_status, hud_rollback);
    hud_enabled.store(hud_ok, std::memory_order_release);
    log("chase_native_timing installed=0 status=not_ported sites=0 edition=fl reason=telemetry_only");
    log("chase_central_hud installed=%u status=%s cleanup=native_when_extra_active hud_anchor=%s edition=fl rva=0x%06lx "
        "rollback=%s",
        unsigned(hud_ok), hud_ok ? "active" : hud_status, hud_anchor_forward ? "forward" : "centre",
        static_cast<unsigned long>(S::hud_gate.rva), hud_rollback);
    log("chase_lead installed=%u status=%s scope=applied_main_chase_same_sector native_solver=retained telemetry=%u "
        "edition=fl base=0x%08lx rva=0x%06lx,0x%06lx,0x%06lx window=%s rollback=%s update=pose_seam",
        unsigned(ok), ok ? "active" : status, unsigned(timing), static_cast<unsigned long>(base),
        static_cast<unsigned long>(S::gate.rva), static_cast<unsigned long>(S::publish.rva),
        static_cast<unsigned long>(S::final_fov.rva), refused_window, lead_rollback);
    SetLastError(error);
    return ok;
}
void native_timing_invalidate(std::uintptr_t, std::uint32_t) noexcept {} // no FL timing sites
bool installed() {
    return enabled.load(std::memory_order_acquire);
}
void invalidate_pose() {
    if (!installed() || owner_thread.load(std::memory_order_relaxed) != GetCurrentThreadId()) return;
    AcquireSRWLockExclusive(&lock);
    pose = {};
    pose_forward_valid = false;
    std::memset(pose_forward, 0, sizeof pose_forward);
    ReleaseSRWLockExclusive(&lock);
    if (hud_anchor_forward) reclaim_hud_anchor_slots();
}
void camera_context(std::uintptr_t cockpit, std::uintptr_t ship, std::uintptr_t camera, bool applied,
                    const chase::Mat3* camera_basis, const chase::Mat3* view_rel) {
    if (!installed() || !applied) return;
    std::uint32_t expected = 0;
    owner_thread.compare_exchange_strong(expected, GetCurrentThreadId());
    if (owner_thread.load() != GetCurrentThreadId()) return;
    core::Identity next;
    std::int32_t forward[3]{};
    bool forward_valid = false;
    if (cockpit && field(ship, 8, next.ship_id)) {
        next.cockpit = std::uint32_t(cockpit);
        next.ship = std::uint32_t(ship);
        next.camera = std::uint32_t(camera);
        next.thread = GetCurrentThreadId();
        if (hud_anchor_forward && camera_basis && view_rel) {
            auto ship_basis = chase::mul(chase::transpose(*view_rel), *camera_basis);
            std::int32_t rows[12]{};
            if (chase::orthonormalize(ship_basis) && chase::to_fixed(ship_basis, rows)) {
                forward[0] = rows[8];
                forward[1] = rows[9];
                forward[2] = rows[10];
                forward_valid = true;
            }
        }
    }
    AcquireSRWLockExclusive(&lock);
    if (next.cockpit) next.generation = next.serial = ++pose_counter;
    pose = next;
    std::memcpy(pose_forward, forward, sizeof forward);
    pose_forward_valid = forward_valid;
    ReleaseSRWLockExclusive(&lock);
}
void report(std::uint64_t frame) {
    if (!installed() || !timing) return;
    Counts c;
    HudCounts h;
    HudAnchorCounts a;
    AcquireSRWLockExclusive(&lock);
    c = counts;
    h = hud_counts;
    a = hud_anchor_counts;
    counts = {};
    hud_counts = {};
    hud_anchor_counts = {};
    ReleaseSRWLockExclusive(&lock);
    const double us = frequency ? 1e6 / as_double(frequency) : 0;
    log("chase_central_hud_window frame=%llu active=%u admitted=%llu no_pose=%llu lifetime=%llu read=%llu "
        "scope=%llu identity=%llu cleanup_pending=%llu texture_unavailable=%llu calls=%llu total_us=%.3f max_us=%.3f "
        "hud_anchor=%s anchor_applied=%llu anchor_refused=%llu anchor_last_px=%ld,%ld edition=fl",
        frame, unsigned(hud_enabled.load(std::memory_order_relaxed)), h.reason[Ready], h.reason[NoPose],
        h.reason[Lifetime], h.reason[Read], h.reason[Scope], h.reason[Identity], h.reason[Marker], h.reason[Projection],
        h.calls, as_double(h.ticks) * us, as_double(h.max_ticks) * us, hud_anchor_forward ? "forward" : "centre",
        a.applied, a.refused, long(a.last_x), long(a.last_y));
    log("chase_lead_window frame=%llu ready=%llu committed=%llu final_fov=%llu no_pose=%llu lifetime=%llu "
        "read=%llu scope=%llu identity=%llu projection=%llu marker=%llu unchanged=%llu calls=%llu total_us=%.3f "
        "max_us=%.3f samples=%llu omitted=%llu edition=fl",
        frame, c.reason[Ready], c.reason[Committed], c.reason[Finalized], c.reason[NoPose], c.reason[Lifetime],
        c.reason[Read], c.reason[Scope], c.reason[Identity], c.reason[Projection], c.reason[Marker], c.reason[Skipped],
        c.calls, as_double(c.ticks) * us, as_double(c.max_ticks) * us, c.samples, c.dropped);
    const unsigned shown = unsigned(c.samples < 4 ? c.samples : 4);
    for (unsigned i = 0; i < shown + (c.samples > 4 ? 1u : 0u); ++i) {
        const auto& s = i < shown ? c.first[i] : c.last;
        log("chase_lead_sample frame=%llu phase=%u generation=%llu update=%llu thread=%u cockpit=0x%08x ship=%u "
            "target=%u sector=0x%08x marker=0x%08x point=%ld,%ld,%ld early_fov=%ld final_fov=%ld native=%ld,%ld "
            "projected=%ld,%ld camera_pos=%ld,%ld,%ld viewport=%ld,%ld,%ld,%ld plane=%ld,%ld screen=%ld,%ld",
            frame, s.reason, s.owner.generation, s.owner.serial, s.owner.thread, s.owner.cockpit, s.owner.ship_id,
            s.owner.target_id, s.owner.sector, s.owner.marker, long(s.point[0]), long(s.point[1]), long(s.point[2]),
            long(s.early.fov), long(s.late.fov), long(s.native_x), long(s.native_y), long(s.x), long(s.y),
            long(s.early.position[0]), long(s.early.position[1]), long(s.early.position[2]), long(s.early.viewport[0]),
            long(s.early.viewport[1]), long(s.early.viewport[2]), long(s.early.viewport[3]), long(s.early.plane[0]),
            long(s.early.plane[1]), long(s.early.screen[0]), long(s.early.screen[1]));
    }
}
void shutdown() {
    hud_enabled.store(false, std::memory_order_release);
    if (hud_site.patched_in) engine_patch::restore(hud_site);
    enabled.store(false, std::memory_order_release);
    for (unsigned i = 3; i-- > 0;)
        if (sites[i].patched_in) engine_patch::restore(sites[i]);
}
} // namespace x3m::chase_lead
