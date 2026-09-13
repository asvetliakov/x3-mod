#include "chase_lead.h"
#include "chase_lead_core.h"
#include "chase_transition.h"
#include "chase_camera.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include "telemetry.h"
#include <atomic>
#include <cstring>

static_assert(sizeof(void *) == 4, "Verified x86 engine ABI");
namespace x3m::chase_lead {
namespace {
constexpr engine_patch::SiteSpec specs[] = {
    {"chase_lead_gate", 0x0042a6fe, {0x0f, 0x84, 0xb8, 0x03, 0x00, 0x00}, 6, 0, 2},
    {"chase_lead_publish", 0x0042aaae, {0x89, 0xb3, 0x40, 0x03, 0x00, 0x00}, 6, 0, 0},
    {"chase_lead_final_fov", 0x004213dd, {0x39, 0xb3, 0x30, 0x02, 0x00, 0x00}, 6, 0, 0}};
// Whole helper, including its one fixed relative call to native node detach.
constexpr unsigned char hide_bytes[] = {0x51, 0x83, 0x3e, 0x00, 0x74, 0x10, 0x8b, 0x4e, 0x04, 0x85,
                                        0xc9, 0x74, 0x09, 0x8b, 0x41, 0x1c, 0x50, 0xe8, 0xfa, 0x3b,
                                        0x06, 0x00, 0xc7, 0x06, 0,    0,    0,    0,    0x59, 0xc3};
engine_patch::Site sites[3];
std::atomic<bool> enabled{false};
bool initialized = false;
SRWLOCK lock = SRWLOCK_INIT;
core::Identity pose{};
struct Slot {
    std::uint32_t frame = 0;
    core::Pending ticket{};
};
Slot pending[8]{};
std::atomic<std::uint32_t> owner_thread{0};
std::uint64_t frequency = 0;
bool timing = false;
inline double as_double(std::uint64_t v) {
    return double(std::uint32_t(v >> 32)) * 4294967296.0 + double(std::uint32_t(v));
}
enum Reason {
    Ready,
    Committed,
    Finalized,
    NoPose,
    Lifetime,
    Read,
    Scope,
    Identity,
    Projection,
    Marker,
    Hidden,
    Skipped,
    Count
};
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

bool read(std::uintptr_t base, unsigned offset, void *out, unsigned size) {
    return base && !(base & 3) && offset <= UINT32_MAX - base && size <= UINT32_MAX - base - offset &&
           engine_memory::read(base + offset, out, size);
}
template <class T> bool field(std::uintptr_t base, unsigned offset, T &out) {
    return read(base, offset, &out, sizeof out);
}
bool writable(std::uintptr_t at, unsigned size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!at || size > UINT32_MAX - at ||
        VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof info) != sizeof info)
        return false;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) &&
           at + size <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
}
bool writable_at(std::uint32_t base, unsigned offset, unsigned size) {
    return base && offset <= UINT32_MAX - base && size <= UINT32_MAX - base - offset &&
           writable(base + offset, size);
}
bool active(std::uint32_t cockpit) {
    std::uint32_t registry = 0, table = 0, handle = 0, bucket[2]{}, link = 0;
    if (!field(0x608504, 0, registry) || !field(registry, 0, table) || !field(registry, 0x10, handle) ||
        !field(table, 0, bucket))
        return false;
    const auto n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !field(bucket[0], 4 * ((n - 1) & handle), link))
        return false;
    for (unsigned i = 0; link && i < 32; ++i) {
        std::uint32_t row[3]{};
        if (!field(link, 0, row))
            return false;
        if (row[1] == handle)
            return row[2] == cockpit;
        link = row[0];
    }
    return false;
}
Reason scope(const core::Identity &base, core::Identity &out) {
    // Validate the complete inline overlay before deriving any of its addresses.
    if (base.cockpit > UINT32_MAX - (0x3ac + 0x348))
        return Read;
    const auto update = chase_transition::current_update(base.cockpit);
    if (!update.generation || update.generation != base.generation || update.serial != base.serial ||
        update.thread != base.thread)
        return Lifetime;
    if (!active(base.cockpit))
        return Identity;
    std::uint32_t refs[2]{}, mode = 0, connect = 0, flags = 0, target = 0, sector = 0, camera = 0, scene = 0,
                             gun = 0, owners[2]{};
    std::uint16_t types[2]{}, tracking = 0;
    if (!field(base.cockpit, 0xc, refs) || !field(base.cockpit, 0x150, mode) ||
        !field(base.cockpit, 0x1c0, connect) || !field(base.cockpit, 0x1a0, flags) ||
        !field(base.cockpit, 0x1d8, gun) || !field(base.cockpit, 0x1e0, target) ||
        !field(base.cockpit, 0x1e4, tracking) || !field(base.cockpit, 0x54, sector) ||
        !field(base.cockpit, 0x58, camera) || !field(base.cockpit, 4, scene))
        return Read;
    if (refs[0] != base.ship || refs[1] != base.ship || camera != base.camera)
        return Identity;
    if (mode != 258 || connect || flags & 4 || gun || tracking < 2 || tracking > 3 || !target || !sector || !scene)
        return Scope;
    out = base;
    out.target = target;
    out.sector = sector;
    out.scene = scene;
    out.overlay = base.cockpit + 0x3ac;
    std::uint32_t bodies = 0;
    if (!field(base.ship, 8, out.ship_id) || !field(target, 8, out.target_id) ||
        !field(base.ship, 0x54, owners[0]) || !field(target, 0x54, owners[1]) ||
        !field(base.ship, 0x48, types[0]) || !field(sector, 0x48, types[1]) || !field(out.overlay, 0x320, bodies))
        return Read;
    if (out.ship_id != base.ship_id || owners[0] != sector || owners[1] != sector || types[0] != 7 ||
        types[1] != 1 || !bodies)
        return Scope;
    return Ready;
}
bool projection(std::uint32_t cockpit, std::uint32_t camera, core::Projection &p) {
    std::uint32_t hud = 0, defaults = 0, screen = 0, viewptr[2]{}, rect[2][6]{};
    std::int32_t vp[4]{}, planes[2]{}, fallback[2]{};
    std::int16_t dims[2]{};
    if (!field(cockpit, 8, hud) || !field(camera, 0x30, p.position) || !field(camera, 0x40, p.basis) ||
        !field(camera, 0x298, p.fov) || !field(camera, 0x278, viewptr[0]) || !field(hud, 0x278, viewptr[1]) ||
        !field(viewptr[0], 0, rect[0]) || !field(viewptr[1], 0, rect[1]) || !field(camera, 0x288, p.viewport) ||
        !field(camera, 0x300, p.plane) || !field(hud, 0x288, vp) || !field(hud, 0x300, planes) ||
        !field(0x606f38, 0, defaults) || !field(defaults, 0, screen) || !field(defaults, 0x28, fallback) ||
        !field(screen, 4, dims))
        return false;
    if (std::memcmp(vp, p.viewport, sizeof vp) || std::memcmp(rect[0], rect[1], 16))
        return false;
    if (p.plane[0] <= 0 || p.plane[1] <= 0)
        std::memcpy(p.plane, fallback, sizeof fallback);
    if (planes[0] <= 0 || planes[1] <= 0)
        std::memcpy(planes, fallback, sizeof fallback);
    for (unsigned i = 0; i < 2; ++i) {
        if (planes[i] != p.plane[i])
            return false;
        p.screen[i] = dims[i];
    }
    if (!core::valid(p))
        return false;
    const auto w = std::uint32_t((p.screen[0] * (p.viewport[3] - p.viewport[2]) + 32768) / 65536);
    const auto h = std::uint32_t((p.screen[1] * (p.viewport[1] - p.viewport[0]) + 32768) / 65536);
    return rect[0][2] == w && rect[0][3] == h && w <= std::uint32_t(p.screen[0]) &&
           h <= std::uint32_t(p.screen[1]) && rect[0][0] <= std::uint32_t(p.screen[0]) - w &&
           rect[0][1] <= std::uint32_t(p.screen[1]) - h;
}
// Ownership validation is deliberately separate from target validity: a target
// switch can invalidate the point while the owning cockpit marker is still ours.
bool owned_marker(const core::Identity &owner, std::uint32_t &marker, bool require_active = true) {
    if (chase_transition::generation(owner.cockpit) != owner.generation)
        return false;
    std::uint32_t scene = 0, entry[2]{}, context = 0, flags = 0;
    if (!field(owner.cockpit, 4, scene) || scene != owner.scene || !field(owner.overlay, 0x3c, entry) ||
        !entry[1] || (require_active && !entry[0]) || !field(entry[1], 0x1c, context) || context != scene ||
        !field(entry[1], 0x130, flags) || !(flags & 0x200) || (flags & 0xf000) ||
        (owner.marker && entry[1] != owner.marker))
        return false;
    marker = entry[1];
    return writable_at(marker, 0x30, 8) && writable_at(owner.overlay, 0x340, 8) && writable_at(owner.overlay, 0x3c, 8);
}
__attribute__((noinline)) void hide_native(std::uint32_t entry) {
    // Native helper takes ESI=overlay entry; no stack args, plain RET. Its
    // complete bytes are verified at install. Do not hold our lock across it.
    asm volatile("pushl %%esi\n\tmovl %0,%%esi\n\tmovl $0x426280,%%eax\n\tcall *%%eax\n\tpopl %%esi" ::"r"(entry)
                 : "eax", "ecx", "edx", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "memory",
                   "cc");
}
void hide(const core::Identity &owner) {
    std::uint32_t marker = 0;
    if (!owned_marker(owner, marker))
        return;
    hide_native(owner.overlay + 0x3c);
    if (chase_transition::generation(owner.cockpit) != owner.generation)
        return;
    std::uint32_t scene = 0, node = 0;
    if (!field(owner.cockpit, 4, scene) || scene != owner.scene || !field(owner.overlay, 0x40, node) ||
        node != marker || !writable_at(owner.overlay, 0x340, 8))
        return;
    const std::int32_t invalid[2] = {-1, -1};
    std::memcpy(reinterpret_cast<void *>(owner.overlay + 0x340), invalid, 8);
}
void note(Reason r, const Sample *sample = nullptr) {
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
void gate(std::uint32_t *regs) {
    core::Identity base;
    AcquireSRWLockExclusive(&lock);
    base = pose;
    for (auto &slot : pending)
        if (slot.frame == regs[2])
            slot = {};
    ReleaseSRWLockExclusive(&lock);
    if (!(regs[8] & 0x40))
        return; // native bit-set path is untouched
    if (!base.serial || base.cockpit > UINT32_MAX - (0x3ac + 0x348) ||
        regs[1] != base.cockpit || regs[4] != base.cockpit + 0x3ac) {
        note(NoPose);
        return;
    }
    core::Identity owner;
    auto r = scope(base, owner);
    core::Projection p;
    if (r == Ready && !projection(owner.cockpit, owner.camera, p))
        r = Projection;
    if (r != Ready) {
        note(r);
        return;
    }
    bool stored = false;
    AcquireSRWLockExclusive(&lock);
    for (auto &slot : pending)
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
void publish(std::uint32_t *regs) {
    core::Pending ticket;
    AcquireSRWLockExclusive(&lock);
    for (auto &slot : pending)
        if (slot.frame == regs[2]) {
            ticket = slot.ticket;
            slot = {};
            break;
        }
    ReleaseSRWLockExclusive(&lock);
    if (!ticket.admitted || regs[4] != ticket.owner.overlay)
        return;
    core::Identity current;
    auto r = scope(ticket.owner, current);
    if (r == Ready && !ticket.matches(current))
        r = Identity;
    std::uint32_t marker = 0;
    const bool owned = owned_marker(ticket.owner, marker);
    core::Pixel pixel;
    Sample sample;
    sample.owner = ticket.owner;
    sample.native_x = regs[1];
    sample.native_y = regs[0];
    // pushfd precedes pushad; its saved ESP is original game ESP minus four.
    if (r == Ready && (!owned || !field(regs[3], 0xc4, ticket.point)))
        r = owned ? Read : Marker;
    if (r == Ready && (!projection(current.cockpit, current.camera, ticket.projection) ||
                       !core::project(ticket.projection, ticket.point, pixel)))
        r = Projection;
    if (r != Ready) {
        if (owned) {
            hide(ticket.owner);
            regs[1] = regs[0] = UINT32_MAX;
        }
        note(r);
        return;
    }
    const std::int32_t xy[2] = {pixel.x, pixel.y};
    std::memcpy(reinterpret_cast<void *>(marker + 0x30), xy, 8);
    regs[1] = std::uint32_t(pixel.x);
    regs[0] = std::uint32_t(pixel.y);
    ticket.owner.marker = marker;
    ticket.published = true;
    AcquireSRWLockExclusive(&lock);
    for (auto &slot : pending)
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
void final_fov(std::uint32_t *regs) {
    core::Pending ticket;
    const auto update = chase_transition::current_update(regs[4]);
    AcquireSRWLockExclusive(&lock);
    for (auto &slot : pending)
        if (slot.ticket.owner.cockpit == regs[4] &&
            (!update.serial || slot.ticket.owner.serial == update.serial)) {
            if (slot.ticket.published)
                ticket = slot.ticket;
            slot = {};
        }
    ReleaseSRWLockExclusive(&lock);
    if (!ticket.published)
        return;
    core::Identity current;
    auto r = scope(ticket.owner, current);
    if (r == Ready && !ticket.matches(current))
        r = Identity;
    std::uint32_t marker = 0;
    if (r == Ready && !owned_marker(ticket.owner, marker))
        r = Marker;
    core::Projection p;
    core::Pixel pixel;
    if (r == Ready && !projection(current.cockpit, current.camera, p))
        r = Projection;
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
    std::memcpy(reinterpret_cast<void *>(marker + 0x30), xy, 8);
    std::memcpy(reinterpret_cast<void *>(ticket.owner.overlay + 0x340), xy, 8);
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
void handle(unsigned index, std::uint32_t *regs) {
    if (!enabled.load(std::memory_order_acquire) ||
        owner_thread.load(std::memory_order_relaxed) != GetCurrentThreadId())
        return;
    LARGE_INTEGER start{}, end{};
    if (timing)
        QueryPerformanceCounter(&start);
    if (index == 0)
        gate(regs);
    else if (index == 1)
        publish(regs);
    else
        final_fov(regs);
    if (timing) {
        QueryPerformanceCounter(&end);
        const auto ticks = end.QuadPart > start.QuadPart ? std::uint64_t(end.QuadPart - start.QuadPart) : 0;
        AcquireSRWLockExclusive(&lock);
        ++counts.calls;
        counts.ticks += ticks;
        if (ticks > counts.max_ticks)
            counts.max_ticks = ticks;
        ReleaseSRWLockExclusive(&lock);
    }
}
} // namespace
} // namespace x3m::chase_lead
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_lead_enter(unsigned index,
                                                                                      std::uint32_t *regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_lead::handle(index, regs);
}
namespace x3m::chase_lead {
namespace {
void *emit(unsigned index, void ***next_out) {
    engine_patch::Emitter e(192);
    if (!e.ok())
        return nullptr;
    void *start = e.here();
    e.byte(0x9c);
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
    e.rel32(reinterpret_cast<void *>(&x3m_chase_lead_enter));
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
    const auto next = (reinterpret_cast<std::uintptr_t>(e.here()) + 9) & ~std::uintptr_t(3);
    e.byte(0xff);
    e.byte(0x25);
    e.dword(std::uint32_t(next));
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < next)
        e.byte(0xcc);
    *next_out = reinterpret_cast<void **>(next);
    e.dword(0);
    return e.finish() ? start : nullptr;
}
} // namespace
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized) {
        SetLastError(error);
        return installed();
    }
    initialized = true;
    bool ok = chase_camera::installed() && chase_transition::installed() && object_trace::executable_verified() &&
              engine_patch::install_window_open() && []() {
                  unsigned char actual[sizeof hide_bytes]{};
                  return engine_patch::read_code(0x426280, actual, sizeof actual) &&
                         !std::memcmp(actual, hide_bytes, sizeof actual);
              }();
    const char *status = ok ? "installing" : "prerequisite_failed";
    timing = telemetry::enabled();
    LARGE_INTEGER f{};
    if (QueryPerformanceFrequency(&f) && f.QuadPart > 0)
        frequency = f.QuadPart;
    for (unsigned i = 0; ok && i < 3; ++i) {
        ok = engine_patch::claim(sites[i], specs[i]);
        status = sites[i].status;
        if (ok) {
            void **next = nullptr;
            auto stub = emit(i, &next);
            ok = stub && next && engine_patch::store_pointer(next, *sites[i].entry) &&
                 engine_patch::push_front(sites[i], stub);
            if (!ok)
                status = "stub_chain_failed";
        }
    }
    if (!ok)
        for (auto &site : sites)
            if (site.patched_in && !engine_patch::restore(site))
                status = "rollback_failed_disabled";
    enabled.store(ok, std::memory_order_release);
    log("chase_lead installed=%u status=%s scope=applied_main_chase_same_sector native_solver=retained "
        "telemetry=%u",
        unsigned(ok), ok ? "active" : status, unsigned(timing));
    SetLastError(error);
    return ok;
}
bool installed() {
    return enabled.load(std::memory_order_acquire);
}
void invalidate_pose() {
    if (!installed() || owner_thread.load(std::memory_order_relaxed) != GetCurrentThreadId())
        return;
    AcquireSRWLockExclusive(&lock);
    pose = {};
    ReleaseSRWLockExclusive(&lock);
}
void camera_context(std::uintptr_t cockpit, std::uintptr_t ship, std::uintptr_t camera, bool applied) {
    if (!installed() || !applied)
        return;
    const auto update = chase_transition::current_update(cockpit);
    core::Identity next;
    if (!update.serial)
        return;
    std::uint32_t expected = 0;
    owner_thread.compare_exchange_strong(expected, GetCurrentThreadId());
    if (owner_thread.load() != GetCurrentThreadId())
        return;
    if (update.generation && update.serial && field(ship, 8, next.ship_id)) {
        next.cockpit = cockpit;
        next.ship = ship;
        next.camera = camera;
        next.generation = update.generation;
        next.serial = update.serial;
        next.thread = update.thread;
    }
    AcquireSRWLockExclusive(&lock);
    pose = next;
    ReleaseSRWLockExclusive(&lock);
}
void report(std::uint64_t frame) {
    if (!installed() || !timing)
        return;
    Counts c;
    AcquireSRWLockExclusive(&lock);
    c = counts;
    counts = {};
    ReleaseSRWLockExclusive(&lock);
    const double us = frequency ? 1e6 / as_double(frequency) : 0;
    log("chase_lead_window frame=%llu ready=%llu committed=%llu final_fov=%llu no_pose=%llu lifetime=%llu "
        "read=%llu scope=%llu identity=%llu projection=%llu marker=%llu unchanged=%llu calls=%llu total_us=%.3f "
        "max_us=%.3f samples=%llu omitted=%llu",
        frame, c.reason[Ready], c.reason[Committed], c.reason[Finalized], c.reason[NoPose], c.reason[Lifetime],
        c.reason[Read], c.reason[Scope], c.reason[Identity], c.reason[Projection], c.reason[Marker],
        c.reason[Skipped], c.calls, as_double(c.ticks) * us, as_double(c.max_ticks) * us, c.samples, c.dropped);
    const unsigned shown = unsigned(c.samples < 4 ? c.samples : 4);
    for (unsigned i = 0; i < shown + (c.samples > 4 ? 1u : 0u); ++i) {
        const auto &s = i < shown ? c.first[i] : c.last;
        log("chase_lead_sample frame=%llu phase=%u generation=%llu update=%llu thread=%u cockpit=0x%08x ship=%u "
            "target=%u sector=0x%08x marker=0x%08x point=%ld,%ld,%ld early_fov=%ld final_fov=%ld native=%ld,%ld "
            "projected=%ld,%ld "
            "camera_pos=%ld,%ld,%ld basis=%ld,%ld,%ld/%ld,%ld,%ld/%ld,%ld,%ld viewport=%ld,%ld,%ld,%ld "
            "plane=%ld,%ld screen=%ld,%ld",
            frame, s.reason, s.owner.generation, s.owner.serial, s.owner.thread, s.owner.cockpit, s.owner.ship_id,
            s.owner.target_id, s.owner.sector, s.owner.marker, long(s.point[0]), long(s.point[1]),
            long(s.point[2]), long(s.early.fov), long(s.late.fov), long(s.native_x), long(s.native_y), long(s.x),
            long(s.y), long(s.early.position[0]), long(s.early.position[1]), long(s.early.position[2]),
            long(s.early.basis[0]), long(s.early.basis[1]), long(s.early.basis[2]), long(s.early.basis[4]),
            long(s.early.basis[5]), long(s.early.basis[6]), long(s.early.basis[8]), long(s.early.basis[9]),
            long(s.early.basis[10]), long(s.early.viewport[0]), long(s.early.viewport[1]),
            long(s.early.viewport[2]), long(s.early.viewport[3]), long(s.early.plane[0]), long(s.early.plane[1]),
            long(s.early.screen[0]), long(s.early.screen[1]));
    }
}
void shutdown() {
    enabled.store(false, std::memory_order_release);
    for (auto &site : sites)
        if (site.patched_in)
            engine_patch::restore(site);
}
} // namespace x3m::chase_lead
