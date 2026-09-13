// Host control-flow fixture for the chase lead marker. The Python test extracts
// selected production policy functions into the generated include below; the
// geometry core is included directly from production. Synthetic 32-bit engine
// memory replaces RPM and the native marker-hide helper.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <set>

#include "chase_lead_core.h"
#include "chase_native_timing_core.h"

namespace x3m::chase_lead {
namespace {

namespace engine_memory {
std::map<std::uint32_t, unsigned char> bytes;
std::set<std::uint32_t> unreadable;
bool read(std::uintptr_t address, void* output, unsigned size) {
    if (address > UINT32_MAX || size > UINT32_MAX - address) return false;
    auto* destination = static_cast<unsigned char*>(output);
    for (unsigned i = 0; i < size; ++i) {
        const auto at = std::uint32_t(address + i);
        if (unreadable.count(at)) return false;
        const auto found = bytes.find(at);
        if (found == bytes.end()) return false;
        destination[i] = found->second;
    }
    return true;
}
template<class T> void put(std::uint32_t address, const T& value) {
    const auto* source = reinterpret_cast<const unsigned char*>(&value);
    for (unsigned i = 0; i < sizeof value; ++i) bytes[address + i] = source[i];
}
void put_bytes(std::uint32_t address, const void* value, unsigned size) {
    const auto* source = static_cast<const unsigned char*>(value);
    for (unsigned i = 0; i < size; ++i) bytes[address + i] = source[i];
}
} // namespace engine_memory

namespace chase_transition {
struct Update { std::uint64_t generation = 0, serial = 0; std::uint32_t thread = 0; };
Update update{};
std::uint32_t cockpit = 0;
Update current_update(std::uint32_t value) { return value == cockpit ? update : Update{}; }
std::uint64_t generation(std::uint32_t value) { return value == cockpit ? update.generation : 0; }
} // namespace chase_transition

struct HostLock { std::recursive_mutex mutex; };
using SRWLOCK = HostLock;
static void AcquireSRWLockExclusive(SRWLOCK* value) { value->mutex.lock(); }
static void ReleaseSRWLockExclusive(SRWLOCK* value) { value->mutex.unlock(); }

SRWLOCK lock{};
core::Identity pose{};
struct Slot { std::uint32_t frame = 0; core::Pending ticket{}; };
Slot pending[8]{};
std::atomic<bool> enabled{true};
std::atomic<bool> hud_enabled{true};
std::atomic<bool> native_timing_enabled{false};
native_timing::State native_times{};
std::atomic<std::uint32_t> owner_thread{0};
thread_local std::uint32_t host_thread = 1;
static std::uint32_t GetCurrentThreadId() { return host_thread; }
struct LARGE_INTEGER { long long QuadPart = 0; };
std::uint64_t host_qpc = 0, frequency = 100;
static bool QueryPerformanceCounter(LARGE_INTEGER* value) { value->QuadPart = static_cast<long long>(++host_qpc); return true; }
bool timing = false;
enum Reason { Ready,Committed,Finalized,NoPose,Lifetime,Read,Scope,Identity,Projection,Marker,Hidden,Skipped,Count };
struct Sample {
    core::Identity owner{};
    core::Projection early{}, late{};
    std::int32_t point[3]{};
    std::int32_t native_x = 0, native_y = 0, x = 0, y = 0;
    unsigned reason = 0;
};
struct Counts {
    std::uint64_t reason[Count]{}, calls = 0, ticks = 0, max_ticks = 0, samples = 0, dropped = 0;
    Sample first[4]{}, last{};
} counts;
struct HudCounts { std::uint64_t reason[Count]{}, calls = 0, ticks = 0, max_ticks = 0; } hud_counts;
auto &reasons = counts.reason;
static bool installed() { return enabled.load(); }
static void native_timing_invalidate(std::uintptr_t cockpit, std::uint32_t thread) noexcept {
    native_times.revoke(std::uint32_t(cockpit), thread);
}

std::set<std::uint32_t> writable_addresses;
static bool writable(std::uintptr_t address, unsigned size) {
    if (address > UINT32_MAX || size > UINT32_MAX - address) return false;
    for (unsigned i = 0; i < size; ++i)
        if (!writable_addresses.count(std::uint32_t(address + i))) return false;
    return true;
}
unsigned hide_calls = 0;
std::uint32_t hidden_entry = 0;
bool hide_native_wrong_node = false;
static void hide_native(std::uint32_t entry) {
    ++hide_calls;
    hidden_entry = entry;
    std::uint32_t marker = 0;
    if (!engine_memory::read(entry + 4, &marker, sizeof marker) || !marker) return;
    engine_memory::put(marker + 0x1c, std::uint32_t(0)); // native node detach
    engine_memory::put(entry, std::uint32_t(0));         // native active flag clear
    if (hide_native_wrong_node) engine_memory::put(entry + 4, std::uint32_t(0x27000));
}

static void* host_memcpy(void* destination, const void* source, std::size_t size) {
    const auto address = reinterpret_cast<std::uintptr_t>(destination);
    if (address <= UINT32_MAX) {
        engine_memory::put_bytes(std::uint32_t(address), source, unsigned(size));
        return destination;
    }
    return std::memcpy(destination, source, size);
}
#include "chase_lead_under_test_inc.h"

static unsigned checks = 0, failures = 0, scenarios = 0;
static void check(bool condition, const char* label) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}

template<class T> static T get(std::uint32_t address) {
    T value{};
    check(engine_memory::read(address, &value, sizeof value), "fixture memory read");
    return value;
}
static void allow_write(std::uint32_t address, unsigned size) {
    for (unsigned i = 0; i < size; ++i) writable_addresses.insert(address + i);
}
static core::Pending* ticket_for_frame(std::uint32_t frame) {
    for (auto& slot : pending) if (slot.frame == frame) return &slot.ticket;
    return nullptr;
}
static bool pending_empty() {
    return std::all_of(std::begin(pending), std::end(pending), [](const Slot& slot){ return !slot.frame; });
}

struct World {
    static constexpr std::uint32_t cockpit = 0x10000, ship = 0x12000, camera = 0x14000;
    static constexpr std::uint32_t target = 0x16000, sector = 0x18000, hud = 0x1a000;
    static constexpr std::uint32_t registry = 0x1c000, table = 0x1d000, buckets = 0x1e000;
    static constexpr std::uint32_t link = 0x1f000, defaults = 0x20000, screen = 0x21000;
    static constexpr std::uint32_t marker = 0x22000, stack = 0x23000;
    static constexpr std::uint32_t camera_rect = 0x24000, hud_rect = 0x25000;
    static constexpr std::uint32_t texture_rows = 0x28000, textures = 0x29000;
    static constexpr std::uint32_t frame = 100;
    static constexpr std::uint32_t overlay = cockpit + 0x3ac;
    core::Identity identity{};
    core::Projection initial{};

    World() { reset(); }
    void reset() {
        engine_memory::bytes.clear(); engine_memory::unreadable.clear(); writable_addresses.clear();
        for (auto& slot : pending) slot = {};
        pose = {}; std::fill(std::begin(reasons), std::end(reasons), 0);
        hide_calls = 0; hidden_entry = 0; hide_native_wrong_node = false;
        enabled = true; hud_enabled = true; native_timing_enabled = false;
        owner_thread = host_thread = 1; counts = {}; hud_counts = {}; native_times = {}; host_qpc = 0;
        chase_transition::cockpit = cockpit;
        chase_transition::update = {7, 11, 3};

        const std::uint32_t registry_address = registry;
        engine_memory::put(0x608504, registry_address);
        engine_memory::put(registry + 0, std::uint32_t(table));
        engine_memory::put(registry + 0x10, std::uint32_t(5));
        const std::uint32_t bucket[2] = {buckets, 8};
        engine_memory::put_bytes(table, bucket, sizeof bucket);
        engine_memory::put(buckets + 4 * 5, std::uint32_t(link));
        const std::uint32_t row[3] = {0, 5, cockpit};
        engine_memory::put_bytes(link, row, sizeof row);

        const std::uint32_t refs[2] = {ship, ship};
        engine_memory::put_bytes(cockpit + 0xc, refs, sizeof refs);
        engine_memory::put(cockpit + 0x150, std::uint32_t(258));
        engine_memory::put(cockpit + 0x1c0, std::uint32_t(0));
        engine_memory::put(cockpit + 0x1a0, std::uint32_t(0));
        engine_memory::put(cockpit + 0x1d8, std::uint32_t(0));
        engine_memory::put(cockpit + 0x1e0, std::uint32_t(target));
        engine_memory::put(cockpit + 0x1e4, std::uint16_t(2));
        engine_memory::put(cockpit + 0x54, std::uint32_t(sector));
        engine_memory::put(cockpit + 0x58, std::uint32_t(camera));
        engine_memory::put(cockpit + 4, std::uint32_t(0x31000));
        engine_memory::put(cockpit + 8, std::uint32_t(hud));
        engine_memory::put(cockpit + 0x234, std::uint32_t(1));
        engine_memory::put(cockpit + 0x2a0, std::uint32_t(1));
        engine_memory::put(camera + 0x278, std::uint32_t(camera_rect));
        engine_memory::put(hud + 0x278, std::uint32_t(hud_rect));
        engine_memory::put(ship + 8, std::uint32_t(101));
        engine_memory::put(target + 8, std::uint32_t(202));
        engine_memory::put(ship + 0x54, std::uint32_t(sector));
        engine_memory::put(target + 0x54, std::uint32_t(sector));
        engine_memory::put(ship + 0x48, std::uint16_t(7));
        engine_memory::put(sector + 0x48, std::uint16_t(1));
        engine_memory::put(overlay + 0x320, std::uint32_t(1));
        engine_memory::put(overlay + 0x324, std::uint32_t(cockpit));
        for (const unsigned offset : {0x118u, 0x140u, 0x12cu})
            engine_memory::put(overlay + offset, std::uint32_t(0));
        engine_memory::put(0x31000 + 0x18, std::uint32_t(1));

        engine_memory::put(0x608dac, std::int16_t(16));
        engine_memory::put(0x6069b0, std::int16_t(16));
        engine_memory::put(0x6069b4, std::int16_t(0));
        engine_memory::put(0x608db0, std::uint32_t(texture_rows));
        engine_memory::put(texture_rows + 15 * 0x3c + 0xc, std::int16_t(1));
        engine_memory::put(0x6069ac, std::uint32_t(textures));
        engine_memory::put(textures + 15 * 0x10 + 8, std::uint32_t(0x2a000));

        initial.position[0] = initial.position[1] = initial.position[2] = 0;
        const std::int32_t basis[12] = {65536,0,0,0, 0,65536,0,0, 0,0,65536,0};
        std::memcpy(initial.basis, basis, sizeof basis);
        initial.fov = 0x2000;
        initial.plane[0] = initial.plane[1] = 65536;
        initial.viewport[0] = initial.viewport[2] = 0;
        initial.viewport[1] = initial.viewport[3] = 65536;
        initial.screen[0] = 1280; initial.screen[1] = 720;
        put_projection(initial);
        engine_memory::put_bytes(hud + 0x288, initial.viewport, sizeof initial.viewport);
        engine_memory::put_bytes(hud + 0x300, initial.plane, sizeof initial.plane);
        const std::uint32_t rectangle[6] = {0, 0, 1280, 720, 0, 0};
        engine_memory::put_bytes(camera_rect, rectangle, sizeof rectangle);
        engine_memory::put_bytes(hud_rect, rectangle, sizeof rectangle);
        engine_memory::put(0x606f38, std::uint32_t(defaults));
        engine_memory::put(defaults + 0, std::uint32_t(screen));
        const std::int32_t fallback[2] = {65536, 65536};
        engine_memory::put_bytes(defaults + 0x28, fallback, sizeof fallback);
        const std::int16_t dimensions[2] = {1280, 720};
        engine_memory::put_bytes(screen + 4, dimensions, sizeof dimensions);

        const std::uint32_t entry[2] = {1, marker};
        engine_memory::put_bytes(overlay + 0x3c, entry, sizeof entry);
        engine_memory::put(overlay + 0x40, std::uint32_t(marker));
        engine_memory::put(marker + 0x1c, std::uint32_t(0x31000));
        engine_memory::put(marker + 0x130, std::uint32_t(0x200));
        const std::int32_t native_xy[2] = {77, 88};
        engine_memory::put_bytes(marker + 0x30, native_xy, sizeof native_xy);
        engine_memory::put_bytes(overlay + 0x340, native_xy, sizeof native_xy);
        allow_write(marker + 0x30, 8); allow_write(overlay + 0x340, 8); allow_write(overlay + 0x3c, 8);

        const std::int32_t point[3] = {240, 80, 1600};
        engine_memory::put_bytes(stack + 4 + 0xc0, point, sizeof point);
        identity = {7, 11, 3, cockpit, ship, 101, 0, 0, camera, 0, 0, 0, 0};
        pose = identity;
    }
    void put_projection(const core::Projection& value) {
        engine_memory::put_bytes(camera + 0x30, value.position, sizeof value.position);
        engine_memory::put_bytes(camera + 0x40, value.basis, sizeof value.basis);
        engine_memory::put(camera + 0x298, value.fov);
        engine_memory::put_bytes(camera + 0x288, value.viewport, sizeof value.viewport);
        engine_memory::put_bytes(camera + 0x300, value.plane, sizeof value.plane);
    }
    std::array<std::uint32_t, 9> gate_regs(bool replacement = true) const {
        std::array<std::uint32_t, 9> regs{};
        regs[1] = cockpit; regs[2] = frame; regs[4] = overlay; regs[8] = replacement ? 0x40 : 0;
        return regs;
    }
    std::array<std::uint32_t, 9> publish_regs() const {
        std::array<std::uint32_t, 9> regs{};
        regs[0] = 88; regs[1] = 77; regs[2] = frame; regs[3] = stack; regs[4] = overlay;
        return regs;
    }
    std::array<std::uint32_t, 9> final_regs() const {
        std::array<std::uint32_t, 9> regs{}; regs[4] = cockpit; return regs;
    }
    std::array<std::uint32_t, 9> central_regs(std::uint32_t flags = 0x40) const {
        std::array<std::uint32_t, 9> regs{};
        regs[4] = overlay; regs[7] = cockpit; regs[8] = flags; return regs;
    }
};

static void geometry_and_late_fov() {
    ++scenarios; World world;
    auto gate_state = world.gate_regs(); gate(gate_state.data());
    auto* ticket = ticket_for_frame(World::frame);
    check(!(gate_state[8] & 0x40) && ticket && ticket->admitted, "valid chase scope replaces native gate");
    auto publication = world.publish_regs(); publish(publication.data());
    ticket = ticket_for_frame(World::frame);
    check(ticket && ticket->published && reasons[Committed] == 1, "publication commits current target point");
    const std::int32_t early_x = std::int32_t(publication[1]);
    core::Projection late = world.initial; late.position[0] = 80; late.fov = 0x1800;
    world.put_projection(late);
    core::Pixel expected{}; const std::int32_t point[3] = {240, 80, 1600};
    check(core::project(late, point, expected), "late parallax/FOV geometry remains finite");
    auto final_state = world.final_regs(); final_fov(final_state.data());
    check(reasons[Finalized] == 1 && expected.x != early_x,
          "late camera position and FOV change recomputes lead");
    check(get<std::int32_t>(World::marker + 0x30) == expected.x
              && get<std::int32_t>(World::overlay + 0x340) == expected.x,
          "final callback writes marker and cached overlay coordinates");
}

static void native_passthrough() {
    ++scenarios; World world;
    auto regs = world.gate_regs(false); gate(regs.data());
    check(regs[8] == 0 && pending_empty(), "native bit-clear path passes through unchanged");
    check(std::all_of(std::begin(reasons), std::end(reasons), [](auto value){ return value == 0; }),
          "native pass-through emits no replacement reason");
}

static void publication_rejection(Reason expected, bool new_target, bool new_thread, bool read_failure) {
    ++scenarios; World world;
    auto gate_state = world.gate_regs(); gate(gate_state.data());
    auto publication = world.publish_regs();
    if (new_target) {
        constexpr std::uint32_t replacement = 0x26000;
        engine_memory::put(World::cockpit + 0x1e0, replacement);
        engine_memory::put(replacement + 8, std::uint32_t(303));
        engine_memory::put(replacement + 0x54, std::uint32_t(World::sector));
    }
    if (new_thread) ++chase_transition::update.thread;
    if (read_failure) engine_memory::unreadable.insert(World::cockpit + 0x150);
    publish(publication.data());
    check(reasons[expected] == 1 && pending_empty(), "publication rejects changed/read-failed scope");
    check(hide_calls == (read_failure || new_target || new_thread ? 1u : 0u),
          "owned marker is hidden on invalid current publication");
    check(publication[0] == UINT32_MAX && publication[1] == UINT32_MAX,
          "owned invalid publication suppresses stale native coordinates");
}

static void nested_serial_and_skipped_publication() {
    ++scenarios; World world;
    auto first = world.gate_regs(); gate(first.data());
    const auto first_serial = ticket_for_frame(World::frame)->owner.serial;
    pose.serial = ++chase_transition::update.serial;
    auto nested = world.gate_regs(); gate(nested.data());
    check(ticket_for_frame(World::frame) && ticket_for_frame(World::frame)->owner.serial != first_serial,
          "nested serial gate atomically replaces the old ticket");
    auto skipped = world.publish_regs(); skipped[4] = 0xdead;
    publish(skipped.data());
    check(pending_empty() && reasons[Committed] == 0 && hide_calls == 0,
          "wrong-overlay publication consumes ticket without writing or hiding");
    auto final_state = world.final_regs(); final_fov(final_state.data());
    check(reasons[Finalized] == 0, "final callback cannot reuse skipped old point");
}

static void final_skip_and_no_old_point() {
    ++scenarios; World world;
    auto gate_state = world.gate_regs(); gate(gate_state.data());
    auto publication = world.publish_regs(); publish(publication.data());
    auto final_state = world.final_regs(); final_fov(final_state.data());
    check(reasons[Skipped] == 1 && pending_empty(), "unchanged projection skips and consumes publication");
    final_fov(final_state.data());
    check(reasons[Skipped] == 1 && reasons[Finalized] == 0, "second final callback has no old point");
}

static void ownership_and_scene_guards(bool wrong_marker, bool wrong_scene) {
    ++scenarios; World world;
    auto gate_state = world.gate_regs(); gate(gate_state.data());
    auto publication = world.publish_regs();
    if (wrong_marker) engine_memory::put(World::overlay + 0x3c + 4, std::uint32_t(0x27000));
    if (wrong_scene) engine_memory::put(World::cockpit + 4, std::uint32_t(0x32000));
    ++chase_transition::update.serial; // invalidate ticket after successful admission
    publish(publication.data());
    check(hide_calls == 0, "foreign marker or scene is never hidden");
    check(publication[0] == 88 && publication[1] == 77,
          "foreign ownership leaves native coordinates untouched");
}

static void gate_read_failure() {
    ++scenarios; World world;
    engine_memory::unreadable.insert(World::cockpit + 0x150);
    auto regs = world.gate_regs(); gate(regs.data());
    check(reasons[Read] == 1 && pending_empty(), "bounded read failure rejects gate");
    check(regs[8] & 0x40, "read failure preserves native gate path");
}

static void owner_thread_callbacks() {
    ++scenarios; World world;
    pose = {}; owner_thread = 0; host_thread = 1;
    camera_context(World::cockpit, World::ship, World::camera, true);
    check(owner_thread == 1 && pose.serial == chase_transition::update.serial,
          "first valid camera context binds callback owner thread");
    const auto serial = pose.serial;
    host_thread = 2; ++chase_transition::update.serial;
    camera_context(World::cockpit, World::ship, World::camera, true);
    check(pose.serial == serial, "foreign camera callback cannot replace owner pose");
    auto refused = world.gate_regs(); handle(0, refused.data());
    check((refused[8] & 0x40) && pending_empty() && reasons[Ready] == 0,
          "foreign hook thread returns before gate policy");
    invalidate_pose();
    check(pose.serial == serial, "foreign pose invalidation is ignored");
    host_thread = 1; invalidate_pose();
    check(!pose.serial, "owner-thread invalidation clears current pose");
}

static void admitted_ticket_survives_pose_invalidation() {
    ++scenarios; World world;
    owner_thread = host_thread = 1;
    auto gate_state = world.gate_regs(); gate(gate_state.data());
    check(ticket_for_frame(World::frame) != nullptr, "parent frame ticket admitted before invalidation");
    invalidate_pose();
    check(ticket_for_frame(World::frame) != nullptr && !pose.serial,
          "pose invalidation preserves admitted parent ticket");
    ++chase_transition::update.serial;
    auto publication = world.publish_regs(); publish(publication.data());
    check(reasons[Lifetime] == 1 && hide_calls == 1 && pending_empty(),
          "nested serial invalidates parent publication and hides owned marker");
}

enum class ProjectionFault { InvalidFov, BehindCamera, OutputOverflow };
static void projection_failure_hides(ProjectionFault fault) {
    ++scenarios; World world;
    auto gate_state = world.gate_regs(); gate(gate_state.data());
    if (fault == ProjectionFault::InvalidFov)
        engine_memory::put(World::camera + 0x298, std::int32_t(0x100));
    else {
        const std::int32_t point[3] = {
            fault == ProjectionFault::OutputOverflow ? INT32_MAX : 10,
            0,
            fault == ProjectionFault::BehindCamera ? -1 : 1,
        };
        engine_memory::put_bytes(World::stack + 4 + 0xc0, point, sizeof point);
    }
    auto publication = world.publish_regs(); publish(publication.data());
    check(reasons[Projection] == 1 && pending_empty(),
          "invalid FOV, behind point, or overflow refuses publication");
    check(hide_calls == 1 && hidden_entry == World::overlay + 0x3c,
          "projection failure invokes native hide only for owned overlay");
    check(get<std::uint32_t>(World::overlay + 0x3c) == 0
              && get<std::uint32_t>(World::marker + 0x1c) == 0,
          "native hide stub detaches marker scene link and active flag");
    check(get<std::int32_t>(World::overlay + 0x340) == -1
              && publication[0] == UINT32_MAX && publication[1] == UINT32_MAX,
          "actual hide callback invalidates owned cached/native coordinates");
}

static void hide_write_guards(bool nonwritable, bool wrong_node) {
    ++scenarios; World world;
    core::Identity owner{};
    check(scope(pose, owner) == Ready, "hide guard fixture owner scope");
    if (nonwritable) writable_addresses.erase(World::marker + 0x30);
    hide_native_wrong_node = wrong_node;
    hide(owner);
    check(hide_calls == (wrong_node ? 1u : 0u),
          "nonwritable pair blocks native hide while wrong post-detach node blocks only cache write");
    check(get<std::int32_t>(World::overlay + 0x340) == 77,
          "failed ownership/write revalidation performs no cached coordinate write");
    check(get<std::uint32_t>(World::overlay + 0x3c) == (wrong_node ? 0u : 1u),
          "only validated native hide clears active entry");
}

static void inactive_entry_is_not_reshown() {
    ++scenarios; World world;
    core::Identity owner{}; check(scope(pose, owner) == Ready, "inactive fixture owner scope");
    hide(owner);
    check(hide_calls == 1 && get<std::int32_t>(World::overlay + 0x340) == -1,
          "first owned hide invalidates marker");
    hide(owner);
    check(hide_calls == 1 && get<std::uint32_t>(World::overlay + 0x3c) == 0
              && get<std::int32_t>(World::overlay + 0x340) == -1,
          "inactive entry cannot be detached or rewritten again");
}

static void slot_capacity() {
    ++scenarios; World world;
    for (std::uint32_t index = 0; index < 8; ++index) {
        auto regs = world.gate_regs(); regs[2] = 200 + index; gate(regs.data());
        check(!(regs[8] & 0x40), "available frame slot admits replacement");
    }
    auto overflow = world.gate_regs(); overflow[2] = 208; gate(overflow.data());
    const unsigned occupied = unsigned(std::count_if(std::begin(pending), std::end(pending),
        [](const Slot& slot){ return slot.frame != 0; }));
    check((overflow[8] & 0x40) && occupied == 8,
          "ninth concurrent frame preserves native gate at bounded capacity");
    check(reasons[Ready] == 8 && reasons[Lifetime] == 1,
          "slot overflow records one bounded lifetime refusal");
}

static void joint_fallback_planes() {
    ++scenarios; World world;
    core::Projection altered = world.initial;
    altered.plane[0] = 0; altered.plane[1] = 123;
    world.put_projection(altered);
    const std::int32_t hud_planes[2] = {456, -1};
    const std::int32_t fallback[2] = {70000, 80000};
    engine_memory::put_bytes(World::hud + 0x300, hud_planes, sizeof hud_planes);
    engine_memory::put_bytes(World::defaults + 0x28, fallback, sizeof fallback);
    core::Projection actual{};
    check(projection(World::cockpit, World::camera, actual),
          "camera and HUD jointly accept the same fallback plane pair");
    check(actual.plane[0] == fallback[0] && actual.plane[1] == fallback[1],
          "one invalid component replaces the whole effective plane pair");
    auto gate_state = world.gate_regs(); gate(gate_state.data());
    check(!(gate_state[8] & 0x40) && ticket_for_frame(World::frame),
          "joint fallback plane remains eligible at gate");
}

static void nested_frames_preserve_parent() {
    ++scenarios; World world;
    auto parent = world.gate_regs(); gate(parent.data());
    pose.serial = ++chase_transition::update.serial;
    auto inner = world.gate_regs(); inner[2] = World::frame + 1; gate(inner.data());
    auto inner_publication = world.publish_regs(); inner_publication[2] = World::frame + 1;
    publish(inner_publication.data());
    auto final_state = world.final_regs(); final_fov(final_state.data());
    check(reasons[Committed] == 1 && reasons[Skipped] == 1,
          "inner serial frame publishes and finalizes independently");
    check(ticket_for_frame(World::frame) && ticket_for_frame(World::frame)->owner.serial == 11,
          "inner final callback leaves invalidated parent frame ticket intact");
    auto parent_publication = world.publish_regs(); publish(parent_publication.data());
    check(reasons[Lifetime] == 1 && hide_calls == 1 && pending_empty(),
          "parent publication later consumes its own ticket and safely hides");
}

static void fractional_pixels_truncate_toward_zero() {
    ++scenarios; World world;
    const std::int32_t point[3] = {1, 1, 1000};
    core::Pixel pixel{};
    check(core::project(world.initial, point, pixel),
          "fractional signed projection remains finite");
    // The unconverted coordinates are approximately +1.545 and -0.869.
    // Rounding would produce (+2,-1); native signed IDIV produces (+1,0).
    check(pixel.x == 1 && pixel.y == 0,
          "positive and negative fractional pixels truncate toward zero");
}

static void rectangle_origin_overflow_refuses() {
    ++scenarios; World world;
    const std::uint32_t rectangle[6] = {UINT32_MAX, 0, 1280, 720, 0, 0};
    engine_memory::put_bytes(World::camera_rect, rectangle, sizeof rectangle);
    engine_memory::put_bytes(World::hud_rect, rectangle, sizeof rectangle);
    core::Projection actual{};
    check(!projection(World::cockpit, World::camera, actual),
          "wrapped rectangle origin plus positive width is rejected");
    auto gate_state = world.gate_regs(); gate(gate_state.data());
    check((gate_state[8] & 0x40) && reasons[Projection] == 1 && pending_empty(),
          "invalid effective rectangle preserves native gate");
}

static void cockpit_overlay_overflow_refuses() {
    ++scenarios; World world;
    constexpr std::uint32_t near_end = 0xfffffc80u;
    engine_memory::put(near_end + 0x1e4, std::uint16_t(2));
    std::uint16_t tracking = 0;
    check(field(near_end, 0x1e4, tracking) && tracking == 2,
          "highest current cockpit field remains bounded and readable");
    pose.cockpit = near_end;
    auto gate_state = world.gate_regs();
    gate_state[1] = near_end;
    gate_state[4] = std::uint32_t(near_end + 0x3acu);
    gate(gate_state.data());
    check((gate_state[8] & 0x40) && reasons[NoPose] == 1 && pending_empty(),
          "cockpit whose derived overlay wraps is rejected before admission");
}

static void marker_destination_overflow_refuses() {
    ++scenarios; World world;
    core::Identity owner{};
    check(scope(pose, owner) == Ready, "marker overflow fixture owner scope");
    constexpr std::uint32_t marker = 0xffffffe0u;
    const std::uint32_t entry[2] = {1, marker};
    engine_memory::put_bytes(World::overlay + 0x3c, entry, sizeof entry);
    engine_memory::put(marker + 0x1c, std::uint32_t(0x31000));
    allow_write(0x10, 8); // would admit the destination if marker+0x30 wrapped first
    check(!writable_at(marker, 0x30, 8),
          "marker destination offset rejects before wrapped writable address");
    std::uint32_t observed = 0;
    check(!owned_marker(owner, observed) && hide_calls == 0,
          "near-end marker cannot acquire ownership or invoke native hide");
}

static void sample_retention_counts() {
    ++scenarios; World world;
    timing = true;
    Sample sample{};
    for (unsigned index = 0; index < 4; ++index) { sample.x = std::int32_t(index); note(Committed, &sample); }
    check(counts.samples == 4 && counts.dropped == 0,
          "four samples are retained without omission");
    sample.x = 4; note(Committed, &sample);
    check(counts.samples == 5 && counts.dropped == 0 && counts.last.x == 4,
          "fifth sample is retained as the exact last sample");
    sample.x = 5; note(Committed, &sample);
    check(counts.samples == 6 && counts.dropped == 1 && counts.last.x == 5,
          "sixth sample increments omitted count while updating last");
    timing = false;
}

static void central_without_target_or_solver() {
    ++scenarios; World world; host_thread = 3;
    engine_memory::put(World::cockpit + 0x1e0, std::uint32_t(0));
    engine_memory::put(World::cockpit + 0x1e4, std::uint16_t(0));
    auto regs = world.central_regs(0x840);
    central_hud(regs.data());
    check(regs[8] == 0x800 && hud_counts.reason[Ready] == 1,
          "central HUD admits without target, tracking, or lead-solver success");
    check(hide_calls == 0, "central HUD admission invokes no marker hide helper");
}

enum class HudGuard {
    Disabled, NativePath, WrongCockpit, WrongOverlay, WrongThread, Generation, Serial,
    InactiveCockpit, Refs, Mode, Connect, AppliedFlags, Gun, Camera, ShipId, ShipType, OwnerType, Scene,
    Draw2d, Display, Bodies, Back, ReadFailure
};
static void central_guard(HudGuard guard) {
    ++scenarios; World world; host_thread = 3;
    auto regs = world.central_regs(0x840);
    Reason expected = Scope;
    switch (guard) {
    case HudGuard::Disabled: hud_enabled = false; break;
    case HudGuard::NativePath: regs[8] &= ~0x40u; break;
    case HudGuard::WrongCockpit: ++regs[7]; expected = NoPose; break;
    case HudGuard::WrongOverlay: ++regs[4]; expected = NoPose; break;
    case HudGuard::WrongThread: host_thread = 2; expected = Lifetime; break;
    case HudGuard::Generation: ++chase_transition::update.generation; expected = Lifetime; break;
    case HudGuard::Serial: ++chase_transition::update.serial; expected = Lifetime; break;
    case HudGuard::InactiveCockpit: engine_memory::put(World::link + 8, std::uint32_t(0)); expected = Identity; break;
    case HudGuard::Refs: engine_memory::put(World::cockpit + 0xc, std::uint32_t(0)); expected = Identity; break;
    case HudGuard::Mode: engine_memory::put(World::cockpit + 0x150, std::uint32_t(257)); break;
    case HudGuard::Connect: engine_memory::put(World::cockpit + 0x1c0, std::uint32_t(1)); break;
    case HudGuard::AppliedFlags: engine_memory::put(World::cockpit + 0x1a0, std::uint32_t(4)); break;
    case HudGuard::Gun: engine_memory::put(World::cockpit + 0x1d8, std::uint32_t(1)); break;
    case HudGuard::Camera: engine_memory::put(World::cockpit + 0x58, std::uint32_t(0)); expected = Identity; break;
    case HudGuard::ShipId: engine_memory::put(World::ship + 8, std::uint32_t(999)); expected = Identity; break;
    case HudGuard::ShipType: engine_memory::put(World::ship + 0x48, std::uint16_t(6)); break;
    case HudGuard::OwnerType: engine_memory::put(World::sector + 0x48, std::uint16_t(2)); break;
    case HudGuard::Scene: engine_memory::put(0x31000 + 0x18, std::uint32_t(0)); break;
    case HudGuard::Draw2d: engine_memory::put(World::cockpit + 0x234, std::uint32_t(0)); break;
    case HudGuard::Display: engine_memory::put(World::cockpit + 0x2a0, std::uint32_t(0)); break;
    case HudGuard::Bodies: engine_memory::put(World::overlay + 0x320, std::uint32_t(0)); break;
    case HudGuard::Back: engine_memory::put(World::overlay + 0x324, std::uint32_t(0)); expected = Identity; break;
    case HudGuard::ReadFailure: engine_memory::unreadable.insert(World::cockpit + 0x234); expected = Read; break;
    }
    central_hud(regs.data());
    check(regs[8] == (guard == HudGuard::NativePath ? 0x800u : 0x840u),
          "central HUD leaves saved flags untouched outside admission");
    if (guard == HudGuard::Disabled || guard == HudGuard::NativePath)
        check(std::all_of(std::begin(hud_counts.reason), std::end(hud_counts.reason),
                          [](auto value){ return value == 0; }),
              "disabled/native central path bypasses admission accounting");
    else
        check(hud_counts.reason[expected] == 1, "central HUD records exact failed guard class");
    check(hide_calls == 0, "central guard failure never invokes hide helper");
}

static void central_native_cleanup_flags() {
    for (const unsigned offset : {0x118u, 0x140u, 0x12cu}) {
        ++scenarios; World world; host_thread = 3;
        engine_memory::put(World::overlay + offset, std::uint32_t(1));
        auto first = world.central_regs(0x840); central_hud(first.data());
        check(first[8] == 0x840 && hud_counts.reason[Marker] == 1 && hide_calls == 0,
              "active extra entry preserves native cleanup branch without helper call");
        engine_memory::put(World::overlay + offset, std::uint32_t(0)); // native branch cleanup
        auto next = world.central_regs(0x840); central_hud(next.data());
        check(next[8] == 0x800 && hud_counts.reason[Ready] == 1,
              "next update admits after native cleanup clears extra entry");
    }
}

enum class TextureGuard { CachedSurface, SignedTotal, RequiredMetadata, MissingRead };
static void central_texture_guard(TextureGuard guard) {
    ++scenarios; World world; host_thread = 3;
    switch (guard) {
    case TextureGuard::CachedSurface:
        engine_memory::put(World::textures + 15 * 0x10 + 8, std::uint32_t(0)); break;
    case TextureGuard::SignedTotal:
        engine_memory::put(0x6069b0, std::int16_t(-1));
        engine_memory::put(0x6069b4, std::int16_t(16)); break;
    case TextureGuard::RequiredMetadata:
        engine_memory::put(0x608dac, std::int16_t(16));
        engine_memory::put(World::texture_rows + 15 * 0x3c + 0xc, std::int16_t(0)); break;
    case TextureGuard::MissingRead: engine_memory::unreadable.insert(0x6069b0); break;
    }
    auto regs = world.central_regs(0x840); central_hud(regs.data());
    check(regs[8] == 0x840 && hud_counts.reason[Projection] == 1,
          "malformed or unavailable texture-15 state preserves native branch");
}

static void native_end_precedes_hud_refusal() {
    ++scenarios; World world;
    host_thread = owner_thread = 3; native_timing_enabled = true;
    auto begin = world.gate_regs(); native_hook(0, begin.data());
    engine_memory::put(World::overlay + 0x118, std::uint32_t(1));
    auto central = world.central_regs(0x840); central[2] = World::frame;
    handle(3, central.data());
    check(native_times.window.metrics[0].calls == 1,
          "common native lead span ends before central HUD policy");
    check(central[8] == 0x840 && hud_counts.reason[Marker] == 1,
          "central HUD refusal follows native timing end without changing flags");
    bool central_started = false;
    for (const auto& slot : native_times.slots)
        central_started |= slot.starts[2].stamp != 0;
    check(central_started, "same common hook begins native central span despite HUD refusal");
}

static unsigned native_open_starts() {
    unsigned count = 0;
    for (const auto& slot : native_times.slots)
        for (const auto& start : slot.starts) count += start.stamp != 0;
    return count;
}

static void native_hook_complete_sequence() {
    ++scenarios; World world;
    host_thread = owner_thread = 3; native_timing_enabled = true;
    auto lead = world.gate_regs(); native_hook(0, lead.data());
    auto central = world.central_regs(); central[2] = World::frame; native_hook(3, central.data());
    auto solver = world.central_regs(); solver[2] = World::frame;
    native_hook(4, solver.data()); solver[7] = 0; native_hook(5, solver.data());
    auto distance = world.gate_regs(); native_hook(6, distance.data()); native_hook(7, distance.data());
    native_hook(8, central.data());
    bool one_each = true;
    for (const auto& metric : native_times.window.metrics) one_each &= metric.calls == 1;
    check(one_each, "native hook indices map one completed span to all four phases");
    check(native_times.window.metrics[1].failures == 1
              && native_times.window.metrics[0].failures == 0
              && native_times.window.metrics[2].failures == 0
              && native_times.window.metrics[3].failures == 0,
          "saved false solver EAX is the only phase failure");
    check(native_times.window.metrics[0].ticks == 1
              && native_times.window.metrics[1].ticks == 1
              && native_times.window.metrics[2].ticks == 5
              && native_times.window.metrics[3].ticks == 1,
          "hook boundaries record the exact scripted phase durations");
    check(native_open_starts() == 0 && native_times.window.unmatched == 0,
          "complete native hook sequence closes every start");
}

static void native_hook_context_failure_revokes() {
    ++scenarios; World world;
    host_thread = owner_thread = 3; native_timing_enabled = true;
    auto solver = world.central_regs(); solver[2] = World::frame;
    native_hook(4, solver.data());
    check(native_open_starts() == 1, "solver begin creates one native timing span");
    engine_memory::put(World::link + 8, std::uint32_t(0));
    native_hook(5, solver.data());
    check(native_open_starts() == 0 && native_times.window.abandoned == 1,
          "invalid end context revokes the open thread span");
}

} // namespace
} // namespace x3m::chase_lead

int main() {
    using namespace x3m::chase_lead;
    geometry_and_late_fov();
    native_passthrough();
    publication_rejection(Lifetime, false, true, false);
    publication_rejection(Identity, true, false, false);
    publication_rejection(Read, false, false, true);
    nested_serial_and_skipped_publication();
    final_skip_and_no_old_point();
    ownership_and_scene_guards(true, false);
    ownership_and_scene_guards(false, true);
    gate_read_failure();
    owner_thread_callbacks();
    admitted_ticket_survives_pose_invalidation();
    projection_failure_hides(ProjectionFault::InvalidFov);
    projection_failure_hides(ProjectionFault::BehindCamera);
    projection_failure_hides(ProjectionFault::OutputOverflow);
    hide_write_guards(true, false);
    hide_write_guards(false, true);
    inactive_entry_is_not_reshown();
    slot_capacity();
    joint_fallback_planes();
    nested_frames_preserve_parent();
    fractional_pixels_truncate_toward_zero();
    rectangle_origin_overflow_refuses();
    cockpit_overlay_overflow_refuses();
    marker_destination_overflow_refuses();
    sample_retention_counts();
    central_without_target_or_solver();
    for (auto guard : {HudGuard::Disabled, HudGuard::NativePath, HudGuard::WrongCockpit,
                       HudGuard::WrongOverlay, HudGuard::WrongThread, HudGuard::Generation,
                       HudGuard::Serial, HudGuard::InactiveCockpit, HudGuard::Refs, HudGuard::Mode,
                       HudGuard::Connect, HudGuard::AppliedFlags, HudGuard::Gun, HudGuard::Camera,
                       HudGuard::ShipId, HudGuard::ShipType,
                       HudGuard::OwnerType, HudGuard::Scene, HudGuard::Draw2d, HudGuard::Display,
                       HudGuard::Bodies, HudGuard::Back, HudGuard::ReadFailure}) central_guard(guard);
    central_native_cleanup_flags();
    for (auto guard : {TextureGuard::CachedSurface, TextureGuard::SignedTotal,
                       TextureGuard::RequiredMetadata, TextureGuard::MissingRead})
        central_texture_guard(guard);
    native_end_precedes_hud_refusal();
    native_hook_complete_sequence();
    native_hook_context_failure_revokes();
    std::printf("chase_lead_host scenarios=%u checks=%u failures=%u\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
