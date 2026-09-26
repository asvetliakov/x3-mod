#include "chase_fire.h"
#include "chase_camera.h"
#include "engine_patch.h"
#include "engine_memory.h"
#include "object_trace.h"
#include "cpu_state.h"
#include "capture.h"
#include "telemetry.h"
#include <atomic>
#include <cstring>

static_assert(sizeof(void*) == 4, "Verified x86 game ABI only");
namespace x3m::chase_fire {
namespace {
// Original CMP [607ce8],0 is left in place. Only this following JZ observes
// the intentionally changed ZF; the subsequent TEST EAX,EAX overwrites flags.
// Full-function decode: no incoming direct branch into these six bytes.
constexpr engine_patch::SiteSpec spec = {
    "chase_cursor_admission", 0x00445a41, {0x0f, 0x84, 0x64, 0x02, 0x00, 0x00}, 6, 0, 2};
constexpr std::uint32_t zero_flag = 0x40;
engine_patch::Site site;
std::atomic<bool> enabled{false};
std::atomic<std::uint32_t> player{0};
bool initialized = false, timing = false;
const char* status = "disabled";
std::uint64_t frequency = 0;
SRWLOCK lock = SRWLOCK_INIT;
struct Context {
    std::uintptr_t cockpit = 0, ship = 0, camera = 0;
    std::uint32_t ship_id = 0, thread = 0;
    std::uint64_t qpc = 0;
};
Context context;
enum class Result : unsigned { Override, Stale, Identity, View, Cursor, Read, Count };
struct Counts {
    std::uint64_t result[unsigned(Result::Count)]{};
    std::uint64_t timed = 0, ticks = 0, max_ticks = 0;
};
Counts counts;

bool read(std::uintptr_t base, unsigned offset, void* out, unsigned size) {
    if (!base || (base & 3) || offset > UINT32_MAX - base) return false;
    const auto at = base + offset;
    return size <= UINT32_MAX - at && engine_memory::read(at, out, size);
}
template <class T> bool field(std::uintptr_t base, unsigned offset, T& out) {
    return read(base, offset, &out, sizeof out);
}
std::uint64_t now() {
    LARGE_INTEGER stamp{};
    return QueryPerformanceCounter(&stamp) && stamp.QuadPart > 0 ? std::uint64_t(stamp.QuadPart) : 0;
}
// Same bounded registry walk as the native resolver 41cd20. Do not call the
// engine from this mid-function boundary. Only identity is needed here; native
// code still performs its own resolver and aim-index/gun-group checks below.
bool active_cockpit(std::uintptr_t& cockpit) {
    std::uint32_t registry = 0, table = 0, handle = 0, bucket[2]{}, link = 0;
    if (!field(0x00608504, 0, registry) || !field(registry, 0, table) || !field(registry, 0x10, handle) ||
        !field(table, 0, bucket))
        return false;
    const auto n = bucket[1];
    if (!n || n > 65536 || (n & (n - 1)) || !field(bucket[0], 4 * ((n - 1) & handle), link)) return false;
    for (unsigned i = 0; link && i < 32; ++i) {
        std::uint32_t row[3]{};
        if (!field(link, 0, row)) return false;
        if (row[1] == handle) {
            cockpit = row[2];
            return cockpit && !(cockpit & 3);
        }
        link = row[0];
    }
    return false;
}
Result decide(const Context& witness, std::uint32_t ship, std::uint64_t stamp) {
    if (!witness.qpc || !stamp || stamp < witness.qpc || stamp - witness.qpc > frequency * 2 ||
        witness.thread != GetCurrentThreadId())
        return Result::Stale;
    std::uintptr_t cockpit = 0;
    if (!active_cockpit(cockpit)) return Result::Read;
    if (cockpit != witness.cockpit || ship != witness.ship) return Result::Identity;
    std::uint32_t refs[2]{}, camera = 0, id = 0, mode = 0, connect = 0, flags = 0;
    if (!field(cockpit, 0xc, refs) || !field(cockpit, 0x58, camera) || !field(ship, 8, id) ||
        !field(cockpit, 0x150, mode) || !field(cockpit, 0x1c0, connect) || !field(cockpit, 0x1a0, flags))
        return Result::Read;
    if (refs[0] != ship || refs[1] != ship || camera != witness.camera || id != witness.ship_id)
        return Result::Identity;
    if (mode != 258 || connect != 0 || (flags & 4)) return Result::View;
    // Native unprojection at 489780 uses normalized 16.16 viewport bounds in
    // y0,y1,x0,x1 order, and signed screen shorts at **606f38 +4/+6. The default
    // view-plane block itself is NOT the screen structure (extra dereference).
    std::int32_t cursor[3]{}, viewport[4]{};
    std::uint32_t defaults = 0, screen = 0;
    std::int16_t dimensions[2]{};
    if (!field(0x00607ce8, 0, cursor) || !field(camera, 0x288, viewport) || !field(0x00606f38, 0, defaults) ||
        !field(defaults, 0, screen) || !field(screen, 4, dimensions))
        return Result::Read;
    if (cursor[0] != 0 || dimensions[0] <= 0 || dimensions[1] <= 0 || viewport[0] < 0 || viewport[0] >= viewport[1] ||
        viewport[1] > 65536 || viewport[2] < 0 || viewport[2] >= viewport[3] || viewport[3] > 65536 || cursor[1] < 0 ||
        cursor[1] >= dimensions[0] || cursor[2] < 0 || cursor[2] >= dimensions[1])
        return Result::Cursor;
    // The signed-16 screen bound and x/y checks above keep these products
    // below INT32_MAX; native-style integer division needs no wide CRT helper.
    const auto x = cursor[1] * 65536 / dimensions[0];
    const auto y = cursor[2] * 65536 / dimensions[1];
    if (x < viewport[2] || x > viewport[3] || y < viewport[0] || y > viewport[1]) return Result::Cursor;
    return Result::Override;
}
void handle(std::uint32_t* regs) {
    // Almost all native/AI calls leave before any context lock, QPC or registry
    // walk. The original flags and global remain untouched on every refusal.
    if (!enabled.load(std::memory_order_acquire) || !(regs[8] & zero_flag)) return;
    const auto expected_ship = player.load(std::memory_order_relaxed);
    if (!expected_ship) return;
    std::uint32_t args[4]{}; // EBX+8: ship,target,gun-group,fire flags
    if (!field(regs[4], 8, args) || args[0] != expected_ship || args[2] != 0 || (args[3] & 0x22) != 0x22) return;
    const auto stamp = now();
    AcquireSRWLockExclusive(&lock);
    const Result result = decide(context, args[0], stamp);
    if (result == Result::Override) regs[8] &= ~zero_flag;
    ++counts.result[unsigned(result)];
    if (timing) {
        const auto end = now(), ticks = end > stamp ? end - stamp : 0;
        ++counts.timed;
        counts.ticks += ticks;
        if (ticks > counts.max_ticks) counts.max_ticks = ticks;
    }
    ReleaseSRWLockExclusive(&lock);
}
}
}
extern "C" __attribute__((force_align_arg_pointer)) void __cdecl x3m_chase_fire_enter(std::uint32_t* regs) {
    x3m::PreserveCpuState cpu;
    asm volatile("fninit" ::: "memory");
    const unsigned mxcsr = 0x1f80;
    asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    x3m::chase_fire::handle(regs);
}
namespace x3m::chase_fire {
namespace {
void* emit(void*** next_out) {
    engine_patch::Emitter e(176);
    if (!e.ok()) return nullptr;
    void* start = e.here();
    e.byte(0x9c);
    e.byte(0x60);
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
    e.byte(0xe8);
    e.rel32(reinterpret_cast<const void*>(&x3m_chase_fire_enter));
    e.byte(0x83);
    e.byte(0xc4);
    e.byte(4);
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
    const auto next = (reinterpret_cast<std::uintptr_t>(e.here()) + 6 + 3) & ~std::uintptr_t(3);
    e.byte(0xff);
    e.byte(0x25);
    e.dword(std::uint32_t(next));
    while (e.ok() && reinterpret_cast<std::uintptr_t>(e.here()) < next) e.byte(0xcc);
    *next_out = reinterpret_cast<void**>(next);
    e.dword(0);
    return e.finish() ? start : nullptr;
}
}
bool initialize() {
    const DWORD error = GetLastError();
    if (initialized) {
        SetLastError(error);
        return enabled.load();
    }
    initialized = true;
    if (!chase_camera::installed() || !chase_camera::wanted()) {
        SetLastError(error);
        return false;
    }
    LARGE_INTEGER f{};
    bool okay = object_trace::executable_verified() && engine_patch::install_window_open() &&
                QueryPerformanceFrequency(&f) && f.QuadPart > 0;
    status = okay ? "installing" : "prerequisite_failed";
    frequency = okay ? std::uint64_t(f.QuadPart) : 0;
    timing = telemetry::enabled();
    if (okay) {
        okay = engine_patch::claim(site, spec);
        status = site.status;
    }
    if (okay) {
        void** next = nullptr;
        void* stub = emit(&next);
        if (!stub || !next) {
            okay = false;
            status = "stub_failed";
        } else if (!engine_patch::store_pointer(next, *site.entry)) {
            okay = false;
            status = "chain_pointer_failed";
        } else if (!engine_patch::push_front(site, stub)) {
            okay = false;
            status = "chain_failed";
        }
    }
    if (!okay && site.patched_in && !engine_patch::restore(site)) status = "rollback_failed_disabled";
    if (okay) status = "active";
    enabled.store(okay, std::memory_order_release);
    log("chase_fire installed=%u status=%s site=0x%08lx scope=applied_chase_258_player_main_cursor lifetime=process telemetry_required=0 native_input_writes=0",
        unsigned(okay), status, static_cast<unsigned long>(spec.address));
    SetLastError(error);
    return okay;
}
bool installed() {
    return enabled.load(std::memory_order_acquire);
}
void invalidate_camera(std::uintptr_t cockpit) {
    if (!enabled.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&lock);
    if (context.cockpit == cockpit) {
        context = {};
        player.store(0, std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&lock);
}
void camera_context(std::uintptr_t cockpit, std::uintptr_t ship, std::uintptr_t camera, std::uint32_t mode,
                    std::uint32_t connect, std::uint32_t flags, bool applied, std::uint64_t stamp) {
    if (!enabled.load(std::memory_order_acquire)) return;
    Context next;
    if (applied && mode == 258 && connect == 0 && !(flags & 4) && stamp && cockpit && ship && camera &&
        !((cockpit | ship | camera) & 3) && field(ship, 8, next.ship_id)) {
        next.cockpit = cockpit;
        next.ship = ship;
        next.camera = camera;
        next.qpc = stamp;
        next.thread = GetCurrentThreadId();
    }
    AcquireSRWLockExclusive(&lock);
    context = next;
    player.store(std::uint32_t(next.ship), std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&lock);
}
void report(std::uint64_t frame) {
    if (!enabled.load(std::memory_order_acquire)) return;
    Counts c;
    AcquireSRWLockExclusive(&lock);
    c = counts;
    counts = {};
    ReleaseSRWLockExclusive(&lock);
    // Native gate3 in chase_aim is intentionally retained. This is an effective
    // branch override count, not proof that later native gun checks admitted a ray.
    const double micros = frequency ? 1000000.0 / double(frequency) : 0;
    if (timing)
        log("chase_fire_window frame=%llu native_inactive_override=%llu refused_stale=%llu refused_identity=%llu refused_view=%llu refused_cursor=%llu refused_read=%llu "
            "timed_calls=%llu total_us=%.3f max_us=%.3f timing_scope=eligible_handler_after_first_qpc excludes=stub_cpu_boundary_first_qpc_early_filter native_input_writes=0 downstream_constraints=native",
            frame, c.result[0], c.result[1], c.result[2], c.result[3], c.result[4], c.result[5], c.timed,
            double(c.ticks) * micros, double(c.max_ticks) * micros);
}
void shutdown() {
    enabled.store(false, std::memory_order_release);
    player.store(0, std::memory_order_relaxed);
    if (site.patched_in) {
        const bool restored = engine_patch::restore(site);
        log("chase_fire_shutdown restored=%u status=%s", unsigned(restored), site.status);
    }
}
}
