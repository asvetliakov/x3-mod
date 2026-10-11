// Execute each actual generated callback stub against a RET continuation.
// The separate site probe verifies displaced spans against the game executable.
#include "../../src/proxy/chase_transition.cpp"
#include "../../src/proxy/chase_lead.cpp"
#include <cstdarg>
#include <cstdio>
// log() must never run under the chase SRW lock (present() holds capture's
// mutex while report() takes this lock); count seam lines and violations.
static unsigned log_under_lock = 0, outcome_lines = 0, arm_refused_lines = 0;
static const char* last_hud_refresh = nullptr; // the outcome row's hud_refresh value (a static name)
namespace x3m {
void log(const char* format, ...) {
    if (TryAcquireSRWLockExclusive(&x3m::chase_transition::lock))
        ReleaseSRWLockExclusive(&x3m::chase_transition::lock);
    else
        ++log_under_lock;
    if (!std::strncmp(format, "chase_view_restore result=", 26)) {
        ++outcome_lines;
        // result=%s cancel=%s attempts=%u monitor=0x%08lx monitor_class=0x%lx hud_refresh=%s
        va_list ap;
        va_start(ap, format);
        (void)va_arg(ap, const char*);
        (void)va_arg(ap, const char*);
        (void)va_arg(ap, unsigned);
        (void)va_arg(ap, unsigned long);
        (void)va_arg(ap, unsigned long);
        last_hud_refresh = va_arg(ap, const char*);
        va_end(ap);
    }
    if (!std::strncmp(format, "chase_view_restore_arm_refused ", 31)) ++arm_refused_lines;
}
}
namespace x3m::telemetry {
bool enabled() {
    return true;
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return true;
}
}
namespace x3m::chase_camera {
bool installed() {
    return true;
}
bool wanted() {
    return true;
}
}
namespace transition = x3m::chase_transition;
extern "C" {
struct Snapshot {
    std::uint32_t regs[9];
    unsigned char xmm[128], x87[108];
    std::uint32_t mxcsr;
};
Snapshot* fixture_output = nullptr;
std::uint32_t fixture_entry_esp = 0, fixture_exit_esp = 0;
alignas(16) unsigned char fixture_input_x87[108];
alignas(16) unsigned char fixture_xmm_seed[128];
std::uint16_t fixture_cw = 0x077f;
std::uint32_t fixture_mxcsr = 0x3f80;
std::uint32_t fixture_flags = 0x647, fixture_normal_cpu = 0;
std::uint32_t fixture_esi = 0x12345678, fixture_edi = 0x98765432;
void fixture_call(std::uint32_t site, std::uint32_t args, std::uint32_t locals, std::uint32_t eax, std::uint32_t ecx,
                  std::uint32_t edx);
// Controllable interpreter frame: every GPR and twelve caller stack words are
// supplied, the site is called (return address at [ESP]), and the resulting
// GPRs/flags are captured in PUSHAD order (edi..eax, eflags) as in the stubs.
struct FrameCall {
    std::uint32_t eax, ecx, edx, ebx, ebp, esi, edi;
    std::uint32_t stack[12];
    std::uint32_t out[9];
    std::uint32_t flags;
};
FrameCall* fixture_frame_current = nullptr;
std::uint32_t fixture_frame_target = 0;
void fixture_call_frame(void* target, FrameCall* frame);
}
asm(".text\n.globl _fixture_call_frame\n_fixture_call_frame:\n"
    "pushl %ebp\n movl %esp,%ebp\n pushl %ebx\n pushl %esi\n pushl %edi\n"
    "movl 12(%ebp),%edx\n movl %edx,_fixture_frame_current\n movl 8(%ebp),%eax\n movl %eax,_fixture_frame_target\n"
    "movl $11,%ecx\n1:\n pushl 28(%edx,%ecx,4)\n decl %ecx\n jns 1b\n"
    "pushl 112(%edx)\n popfl\n movl 0(%edx),%eax\n movl 4(%edx),%ecx\n movl 12(%edx),%ebx\n movl 20(%edx),%esi\n movl 24(%edx),%edi\n movl 16(%edx),%ebp\n movl 8(%edx),%edx\n"
    "call *_fixture_frame_target\n"
    "pushfl\n pushal\n movl %esp,%esi\n movl _fixture_frame_current,%edi\n addl $76,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
    "addl $84,%esp\n popl %edi\n popl %esi\n popl %ebx\n popl %ebp\n ret\n");
// Four-byte caller stack; no 16-byte alignment promise. Save the fixture's
// state, seed hostile live state, run the real span, snapshot it, restore caller.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
    "movl %esp,_fixture_entry_esp\n"
    "pushfl\n pushal\n subl $256,%esp\n"
    "fnsave 0(%esp)\n frstor 0(%esp)\n stmxcsr 108(%esp)\n"
    "movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
    "movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
    "movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
    "fninit\n cmpl $0,_fixture_normal_cpu\n jne 1f\n fld1\n fldpi\n1:\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n"
    "movups _fixture_xmm_seed,%xmm0\n movups _fixture_xmm_seed+16,%xmm1\n movups _fixture_xmm_seed+32,%xmm2\n movups _fixture_xmm_seed+48,%xmm3\n"
    "movups _fixture_xmm_seed+64,%xmm4\n movups _fixture_xmm_seed+80,%xmm5\n movups _fixture_xmm_seed+96,%xmm6\n movups _fixture_xmm_seed+112,%xmm7\n"
    "movl 300(%esp),%ebx\n movl 304(%esp),%ebp\n movl 308(%esp),%eax\n movl 312(%esp),%ecx\n movl 316(%esp),%edx\n"
    "movl _fixture_esi,%esi\n movl _fixture_edi,%edi\n pushl _fixture_flags\n popfl\n fnsave _fixture_input_x87\n frstor _fixture_input_x87\n call *240(%esp)\n"
    "pushfl\n pushal\n movl %esp,%esi\n movl _fixture_output,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
    "movl _fixture_output,%edi\n movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
    "movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
    "fnsave 164(%edi)\n frstor 164(%edi)\n stmxcsr 272(%edi)\n addl $36,%esp\n"
    "frstor 0(%esp)\n ldmxcsr 108(%esp)\n"
    "movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n"
    "movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
    "addl $256,%esp\n popal\n popfl\n movl %esp,_fixture_exit_esp\n ret\n");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) {
    ++checks;
    if (!okay) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}
// Fixed, paired measurements of the complete emitted no-op callback path.
// Both paths use the same expensive register/x87 snapshot harness. Differences
// are diagnostic estimates, may be negative under noise, and are not game FPS.
static inline double as_double(std::uint64_t v) {
    return double(std::uint32_t(v >> 32)) * 4294967296.0 + double(std::uint32_t(v));
}
static bool benchmark_batch(void* target, unsigned loops, std::uint64_t& ticks) {
    std::uint32_t args[16]{};
    Snapshot out{};
    fixture_output = &out;
    const auto entry = std::uint32_t(reinterpret_cast<std::uintptr_t>(target));
    const auto arguments = std::uint32_t(reinterpret_cast<std::uintptr_t>(args));
    LARGE_INTEGER start{}, end{};
    if (!QueryPerformanceCounter(&start) || start.QuadPart <= 0) return false;
    for (unsigned i = 0; i < loops; ++i) fixture_call(entry, arguments, arguments, 0, 0, 0);
    if (!QueryPerformanceCounter(&end) || end.QuadPart < start.QuadPart) return false;
    ticks = std::uint64_t(end.QuadPart - start.QuadPart);
    return true;
}
static void benchmark(void* continuation, void* const* stubs, void* seam_idle) {
    constexpr unsigned loops = 4096, trials = 3;
    LARGE_INTEGER f{};
    const bool clock_ok = QueryPerformanceFrequency(&f) && f.QuadPart > 0;
    check(clock_ok, "benchmark QPC frequency");
    if (!clock_ok) return;
    // All callbacks disabled: the benchmark isolates wrapper/guard cost. Live
    // handler and native-function timings are separately recorded during gameplay.
    transition::enabled.store(false);
    x3m::chase_lead::enabled.store(false);
    x3m::chase_lead::hud_enabled.store(false);
    x3m::chase_lead::native_timing_enabled.store(false);
    transition::restore_enabled.store(false);
    transition::restore_inject_filter.store(0); // kind 18: the injection stub's idle prefilter path
    fixture_normal_cpu = 1;
    fixture_cw = 0x037f;
    fixture_mxcsr = 0x1f80;
    fixture_flags = 0x247;
    const double scale = 1e6 / as_double(std::uint64_t(f.QuadPart)) / double(loops);
    for (unsigned kind = 0; kind < 19; ++kind) {
        void* const target = kind < 18 ? stubs[kind] : seam_idle;
        std::uint64_t ignored = 0;
        bool valid = benchmark_batch(continuation, 16, ignored) && benchmark_batch(target, 16, ignored);
        std::uint64_t base[trials]{}, hook[trials]{};
        for (unsigned trial = 0; trial < trials && valid; ++trial) {
            // Alternate order so a single warm-up/order bias is not always charged to
            // the callback; warm-up calls themselves are excluded from the measurements.
            if (trial & 1)
                valid = benchmark_batch(target, loops, hook[trial]) &&
                        benchmark_batch(continuation, loops, base[trial]);
            else
                valid = benchmark_batch(continuation, loops, base[trial]) &&
                        benchmark_batch(target, loops, hook[trial]);
        }
        check(valid, "bounded paired benchmark QPC samples");
        if (!valid) continue;
        double sum_base = 0, sum_hook = 0, best_delta = 0;
        for (unsigned trial = 0; trial < trials; ++trial) {
            const double b = as_double(base[trial]) * scale, h = as_double(hook[trial]) * scale;
            sum_base += b;
            sum_hook += h;
            if (!trial || h - b < best_delta) best_delta = h - b;
        }
        std::printf(
            "CHASE STUB BENCH kind=%u trials=%u loops=%u baseline_mean_us=%.6f full_stub_mean_us=%.6f delta_mean_us=%.6f delta_best_trial_us=%.6f scope=%s cpu=normal harness=same_fixture_call native_work=excluded game_fps=unmeasured\n",
            kind, trials, loops, sum_base / double(trials), sum_hook / double(trials),
            (sum_hook - sum_base) / double(trials), best_delta,
            kind < 18 ? "full_stub_disabled_callback" : "inject_idle_prefilter_skip");
    }
}

// ---------------- X3M_CHASE_VIEW_RESTORE scenarios ----------------
// The engine path (docs/architecture/chase-view-restore-engine-state.md,
// section 4) through the actual emitted stubs: the injection site's one-word
// prefilter, the engine call made from the site-I handler under the engine's
// own x87/MXCSR image (a mock 0x0049f570 records its seven arguments and the
// CPU state it was entered with), the kind-6 capture of the monitor object
// from the dispatcher frame, the ticket outcomes, and install/rollback of the
// four spans on fixture-owned copies.
namespace restore_fixture {
using namespace x3m::chase_transition;
using namespace x3m::chase_transition::detail;
namespace engine_patch = x3m::engine_patch;
constexpr std::uint32_t native_id = 0x8dbu, monitor_id = 0xffff6eaeu, controller_id = 0xffff6eafu,
                        sector_a = 0x265e8e08u, sector_b = 0x80e89c18u, ship_script = 0xfffefa78u,
                        pause_none = 0;
alignas(16) unsigned char world[0x4000];
std::uint32_t W(unsigned off) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(world + off));
}
template <class T> void put(std::uint32_t at, T v) {
    std::memcpy(reinterpret_cast<void*>(at), &v, sizeof v);
}
template <class T> T get(std::uint32_t at) {
    T v;
    std::memcpy(&v, reinterpret_cast<void*>(at), sizeof v);
    return v;
}
// Offsets inside world.
enum : unsigned {
    o_roots = 0, // [0] native registry root, [4] cockpit registry root, [8] game root
    o_game = 0x100,              // +0x418 controller id, +0x4a0 pause bits
    o_ship = 0x700,              // +8 native id, +0x40 flags, +0x94 ship-script id
    o_ship2 = 0x800,             // a second ship (ship change)
    o_native_registry = 0x900,
    o_native_table = 0x920,
    o_native_bucket = 0x940,
    o_native_row = 0x960,
    o_native_row2 = 0x980,
    o_cockpit_registry = 0xa00,
    o_cockpit_table = 0xa20,
    o_cockpit_bucket = 0xa40,
    o_cockpit_row = 0xa60,
    o_cockpit = 0x1000,  // the armed one
    o_cockpit2 = 0x1400, // the fresh one
    o_cockpit3 = 0x1800, // a side monitor's
    o_dispatcher_frame = 0x2000,
    o_task = 0x2100,
    o_context = 0x2200,
    o_descriptor = 0x2300,
    o_input_frame = 0x2400,
    o_manager_context = 0x2500, // the monitor manager's static record: [0] key, [8] class descriptor
    o_manager_descriptor = 0x2540,
    o_task_stack_base = 0x3000 // task cells grow down from here (5-byte cells)
};
constexpr std::uint32_t manager_key = 0x25du;
unsigned checks = 0, failures = 0;
void check(bool okay, const char* label) {
    ++checks;
    if (!okay) {
        ++failures;
        std::printf("FAIL restore %s\n", label);
    }
}
void build_world() {
    std::memset(world, 0, sizeof world);
    put(W(o_roots), W(o_native_registry));
    put(W(o_roots + 4), W(o_cockpit_registry));
    put(W(o_roots + 8), W(o_game));
    put(W(o_game + 0x418), controller_id);
    put(W(o_game + 0x4a0), pause_none);
    put(W(o_ship + 8), native_id);
    put(W(o_ship + 0x94), ship_script);
    put(W(o_ship2 + 8), native_id + 1);
    put(W(o_ship2 + 0x94), 0xffff1766u);
    put(W(o_native_registry + 0x14), W(o_native_table));
    put(W(o_native_table), W(o_native_bucket));
    put(W(o_native_table + 4), 4u);
    put(W(o_native_bucket) + 4 * (native_id & 3), W(o_native_row));
    put(W(o_native_row + 4), native_id);
    put(W(o_native_row + 8), W(o_ship));
    put(W(o_native_bucket) + 4 * ((native_id + 1) & 3), W(o_native_row2));
    put(W(o_native_row2 + 4), native_id + 1);
    put(W(o_native_row2 + 8), W(o_ship2));
    put(W(o_cockpit_registry), W(o_cockpit_table));
    put(W(o_cockpit_registry + 0x10), 7u);
    put(W(o_cockpit_table), W(o_cockpit_bucket));
    put(W(o_cockpit_table + 4), 4u);
    put(W(o_cockpit_bucket) + 4 * (7 & 3), W(o_cockpit_row));
    put(W(o_cockpit_row + 4), 7u);
    put(W(o_cockpit_row + 8), W(o_cockpit));
    for (unsigned c : {o_cockpit, o_cockpit2, o_cockpit3}) {
        put(W(c + 0xc), W(o_ship));
        put(W(c + 0x10), W(o_ship));
        put(W(c + 0x150), c == o_cockpit ? restore_rear_mode : restore_internal_mode);
        put(W(c + 0x1fc), c == o_cockpit ? sector_a : sector_b);
    }
    // Dispatcher frame at the kind-6 store: [ebp+4] = 0x4a3909, [ebp+0xc] = task, task+0x3c = context,
    // context = monitor id, context+8 = class descriptor, desc = class id.
    put(W(o_dispatcher_frame + 4), 0x4a3909u);
    put(W(o_dispatcher_frame + 0xc), W(o_task));
    put(W(o_task + 0x3c), W(o_context));
    put(W(o_context), monitor_id);
    put(W(o_context + 8), W(o_descriptor));
    put(W(o_descriptor), 0x25eu);
}
void control(unsigned cockpit) {
    put(W(o_cockpit_row + 8), W(cockpit));
}
// The mock engine wrapper (variadic like the engine's: argc dwords follow argc):
// records the arguments and the CPU state of the last call and of each of the
// first four, optionally performs the script's store (the kind-6 observer
// through the real handler, as the engine's SelectMode -> INS_CockpitSetViewMode
// does synchronously inside the call), and returns the configured AL
// (refresh_result for UpdateVisibility).
struct MockCall {
    unsigned calls = 0;
    std::uint32_t args[7]{};
    const char* name = nullptr;
    std::uint16_t cw = 0;
    std::uint32_t mxcsr = 0;
    std::uint32_t store_mode = restore_rear_mode; // 0: store nothing
    unsigned store_cockpit = o_cockpit2;
    std::uint32_t result = 1, refresh_result = 1;
    std::uint32_t last_error_seen = 0;
    std::uint32_t log_args[4][7]{};
    const char* log_names[4]{};
    std::uint16_t log_cw[4]{};
    std::uint32_t log_mxcsr[4]{}, log_error[4]{};
};
MockCall mock;
extern "C" std::uint32_t __cdecl mock_script_call(std::uint32_t a, std::uint32_t b, std::uint32_t c, const char* name,
                                                   std::uint32_t e, std::uint32_t f, ...) {
    std::uint32_t g = 0;
    if (f) {
        va_list ap;
        va_start(ap, f);
        g = va_arg(ap, std::uint32_t);
        va_end(ap);
    }
    const unsigned slot = mock.calls++;
    const std::uint32_t v[7] = {a, b, c, 0, e, f, g};
    std::memcpy(mock.args, v, sizeof v);
    mock.name = name;
    asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(mock.cw), "=m"(mock.mxcsr)::"memory");
    mock.last_error_seen = GetLastError();
    if (slot < 4) {
        std::memcpy(mock.log_args[slot], v, sizeof v);
        mock.log_names[slot] = name;
        mock.log_cw[slot] = mock.cw;
        mock.log_mxcsr[slot] = mock.mxcsr;
        mock.log_error[slot] = mock.last_error_seen;
    }
    const bool refresh = name && !std::strcmp(name, "UpdateVisibility");
    if (mock.store_mode) {
        std::uint32_t regs[9]{};
        regs[7] = W(mock.store_cockpit);
        regs[6] = mock.store_mode;
        regs[2] = W(o_dispatcher_frame);
        handle(6, regs);
    }
    SetLastError(0x5a5a0001u); // the engine may leave any LastError; the stub boundary restores the caller's
    return refresh ? mock.refresh_result : mock.result;
}
void* stubs[restore_site_count]{};
void* continuation = nullptr;
std::uint32_t thread() {
    return GetCurrentThreadId();
}
void reset_state() {
    restore = {};
    restore.retries = restore_default_retries;
    restore_filter_publish();
    restore_epoch.store(1);
    state = {};
    restore_enabled.store(true);
    mock = {};
    build_world();
}
void construct(unsigned cockpit, std::uint32_t caller = restore_alloc_caller) {
    std::uint32_t frame[4]{};
    frame[0] = caller;
    frame[1] = W(cockpit);
    std::uint32_t regs[9]{};
    regs[3] = std::uint32_t(reinterpret_cast<std::uintptr_t>(frame)) - 4;
    handle(0, regs);
    std::uint32_t complete[9]{};
    complete[7] = W(cockpit);
    handle(1, complete);
}
void update(unsigned cockpit = o_cockpit2) {
    std::uint32_t frame[4]{};
    frame[1] = W(cockpit);
    std::uint32_t regs[9]{};
    regs[3] = std::uint32_t(reinterpret_cast<std::uintptr_t>(frame)) - 4;
    handle(3, regs);
}
void destroy(unsigned cockpit, std::uint32_t caller = restore_free_caller) {
    std::uint32_t frame[4]{};
    frame[0] = caller;
    frame[1] = W(cockpit);
    std::uint32_t regs[9]{};
    regs[3] = std::uint32_t(reinterpret_cast<std::uintptr_t>(frame)) - 4;
    regs[2] = W(o_dispatcher_frame);
    handle(2, regs);
}
void store(unsigned cockpit, std::uint32_t mode, std::uint32_t ebp = W(o_dispatcher_frame)) {
    std::uint32_t regs[9]{};
    regs[7] = W(cockpit);
    regs[6] = mode;
    regs[2] = ebp;
    handle(6, regs);
}
void observe(unsigned cockpit) {
    AppliedFrame f;
    f.cockpit = W(cockpit);
    f.mode = get<std::uint32_t>(W(cockpit + 0x150));
    f.connect = get<std::uint32_t>(W(cockpit + 0x1c0));
    f.flags = get<std::uint32_t>(W(cockpit + 0x1a0));
    f.ref = get<std::uint32_t>(W(cockpit + 0xc));
    f.view = get<std::uint32_t>(W(cockpit + 0x10));
    f.sector = get<std::uint32_t>(W(cockpit + 0x1fc));
    f.sector54 = get<std::uint32_t>(W(cockpit + 0x54));
    restore_observe_frame(f);
}
// Site I through the actual stub: [ESP] return, [ESP+4] the engine's controlled object, [ESP+8] flag.
FrameCall input_entry(std::uint32_t controlled = W(o_ship)) {
    FrameCall f{};
    f.flags = 0x202;
    f.eax = 0x11;
    f.ecx = 0x22;
    f.edx = 0x33;
    f.ebx = 0x44;
    f.esi = 0x55;
    f.edi = 0x66;
    f.ebp = 0x77;
    f.stack[0] = 0x404368;
    f.stack[1] = controlled;
    f.stack[2] = 1;
    return f;
}
void run_input(FrameCall& f) {
    fixture_call_frame(stubs[restore_inject_kind], &f);
}
// Arm on the old cockpit, free it through the free command, construct the fresh one, let the script store
// mode 1 into it (the monitor capture), then two applied frames: due.
void arm_free_construct(bool capture_monitor = true) {
    reset_state();
    construct(o_cockpit, 0x41f83bu); // the armed cockpit's own constructor caller is irrelevant
    observe(o_cockpit);
    check(restore.phase == phase_armed && restore.arm_ship_id == ship_script && restore.arm_controller == controller_id &&
              restore.arm_sector == sector_a,
          "applied rear frame arms from engine state");
    destroy(o_cockpit);
    check(restore.phase == phase_pending, "free command transfers to pending");
    construct(o_cockpit2);
    if (capture_monitor) store(o_cockpit2, restore_internal_mode);
    check(restore.monitor == (capture_monitor ? monitor_id : 0) && restore.monitor_class == (capture_monitor ? 0x25eu : 0),
          "StartMonitor's store captures the monitor object and class from the dispatcher frame");
    control(o_cockpit2);
}
void due() {
    observe(o_cockpit2);
    check(restore.phase == phase_pending && restore_inject_filter.load() == 0, "first fresh frame is not due");
    observe(o_cockpit2);
    check(restore.phase == phase_due && restore_inject_filter.load() == 1, "second frame is due and publishes the prefilter");
}
void scenarios() {
    // 1. Acceptance: one call with the captured object, the script's 258 store inside the call, restored.
    arm_free_construct();
    due();
    {
        const auto rows = outcome_lines;
        // Hostile engine state for the call: a non-default x87 control word and MXCSR (the handler's own
        // clean state must not leak into the callee, and the engine's image must come back afterwards).
        const std::uint16_t engine_cw = 0x077f;
        const std::uint32_t engine_mxcsr = 0x3f80;
        std::uint16_t cw_after = 0;
        std::uint32_t mxcsr_after = 0;
        asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(engine_cw), "m"(engine_mxcsr) : "memory");
        FrameCall f = input_entry();
        SetLastError(0x2468ace0);
        run_input(f);
        asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(cw_after), "=m"(mxcsr_after)::"memory");
        const std::uint16_t default_cw = 0x037f;
        const std::uint32_t default_mxcsr = 0x1f80;
        asm volatile("fninit\n\tfldcw %0\n\tldmxcsr %1" ::"m"(default_cw), "m"(default_mxcsr) : "memory");
        check(GetLastError() == 0x2468ace0, "LastError restored across the engine call");
        check(cw_after == engine_cw && mxcsr_after == engine_mxcsr, "the engine's x87 CW and MXCSR image is back after the call");
        check(mock.calls == 1 && mock.args[0] == 0 && mock.args[1] == 0 && mock.args[2] == monitor_id &&
                  mock.name && !std::strcmp(mock.name, "SelectMode") && mock.args[4] == 1 && mock.args[5] == 1 &&
                  mock.args[6] == restore_rear_mode,
              "the engine call is SelectMode(258) on the captured monitor object (object and lookup key)");
        check(mock.cw == engine_cw && mock.mxcsr == engine_mxcsr, "the engine call runs under the engine's incoming x87 CW and MXCSR");
        check(restore.phase == phase_idle && restore.results[result_restored] == 1 && restore.attempts == 0 &&
                  restore_inject_filter.load() == 0,
              "258 stored on the bound cockpit inside the call is restored; prefilter back to idle");
        check(outcome_lines == rows + 1, "one chase_view_restore row per ticket outcome, emitted after the lock");
        check(f.out[7] == 0x11 && f.out[6] == 0x22 && f.out[5] == 0x33 && f.out[4] == 0x44 && f.out[1] == 0x55 &&
                  f.out[0] == 0x66 && f.out[2] == 0x77 && (f.out[8] & 0x8d5) == (0x202 & 0x8d5),
              "site I stub returns the input routine's registers and flags untouched after the call");
        FrameCall g = input_entry();
        run_input(g);
        check(mock.calls == 1, "consumed ticket never calls again");
    }
    // 2. Ship change in the same sector, and a different sector with the same ship, both restore.
    arm_free_construct();
    put(W(o_cockpit2 + 0xc), W(o_ship2));
    put(W(o_cockpit2 + 0x10), W(o_ship2));
    put(W(o_cockpit2 + 0x1fc), sector_a);
    due();
    {
        FrameCall f = input_entry(W(o_ship2));
        run_input(f);
        check(mock.calls == 1 && restore.results[result_restored] == 1, "same sector, different ship restores");
    }
    arm_free_construct();
    due();
    {
        FrameCall f = input_entry();
        run_input(f);
        check(mock.calls == 1 && restore.results[result_restored] == 1, "different sector, same ship restores");
    }
    // 3. Injection skips: another controlled object or the pause bit; the ticket stays due.
    arm_free_construct();
    due();
    {
        FrameCall f = input_entry(W(o_ship2));
        run_input(f);
        check(mock.calls == 0 && restore.phase == phase_due && restore.injections_skipped == 1,
              "another controlled object at the entry skips the iteration");
        put(W(o_game + 0x4a0), 1u);
        FrameCall g = input_entry();
        run_input(g);
        check(mock.calls == 0 && restore.phase == phase_due && restore.injections_skipped == 2, "pause bit skips");
        put(W(o_game + 0x4a0), pause_none);
        FrameCall h = input_entry();
        run_input(h);
        check(mock.calls == 1 && restore.results[result_restored] == 1, "the next iteration calls");
    }
    // 4. No monitor captured: cancel no_monitor, no call. 5. call_refused. 6. no_effect retries. 7. other_mode.
    arm_free_construct(false);
    due();
    {
        FrameCall f = input_entry();
        run_input(f);
        check(mock.calls == 0 && restore.phase == phase_idle && restore.cancels[cancel_no_monitor] == 1,
              "no captured monitor cancels with no_monitor and makes no call");
    }
    arm_free_construct();
    due();
    mock.result = 0;
    mock.store_mode = 0;
    {
        FrameCall f = input_entry();
        run_input(f);
        check(mock.calls == 1 && restore.phase == phase_idle && restore.cancels[cancel_call_refused] == 1 &&
                  restore.results[result_call_refused] == 1,
              "a wrapper returning AL 0 (object or method not found) cancels call_refused");
    }
    arm_free_construct();
    restore.retries = 1;
    due();
    mock.store_mode = 0;
    {
        FrameCall f = input_entry();
        run_input(f);
        check(mock.calls == 1 && restore.phase == phase_pending && restore.results[result_no_effect] == 1,
              "no store inside the call is no_effect and schedules a retry");
        for (unsigned i = 0; i < 59; ++i) {
            update();
            observe(o_cockpit2);
        }
        check(restore.phase == phase_pending, "frames inside the 60-update spacing do not reach due");
        update();
        due();
        FrameCall g = input_entry();
        run_input(g);
        check(mock.calls == 2 && restore.phase == phase_idle && restore.cancels[cancel_retries] == 1,
              "the attempt cap cancels with retries");
    }
    arm_free_construct();
    due();
    mock.store_mode = 3;
    {
        FrameCall f = input_entry();
        run_input(f);
        check(mock.calls == 1 && restore.phase == phase_idle && restore.results[result_other_mode] == 1 &&
                  restore.cancels[cancel_other_mode] == 1,
              "another mode stored by the script ends the ticket");
    }
    // 8. Cancels: deserializer, VM clear/load (stubs 1/2), EH adapter (stub 3) at the next frame, load-chain
    //    constructor, bound cockpit destroyed, a second destruction while pending, expiry, mode left.
    arm_free_construct();
    {
        std::uint32_t regs[9]{};
        regs[2] = W(o_cockpit2);
        regs[5] = 1;
        handle(7, regs);
        check(restore.phase == phase_idle && restore.cancels[cancel_load] == 1, "kind7 deserialization cancels");
    }
    for (unsigned kind = 1; kind <= 2; ++kind) {
        arm_free_construct();
        const auto epoch = restore_epoch.load();
        FrameCall f{};
        f.stack[0] = W(o_game);
        f.esi = W(o_game);
        f.flags = 0x202;
        fixture_call_frame(stubs[kind], &f);
        check(restore.phase == phase_idle && restore_epoch.load() == epoch + 1 && restore.cancels[cancel_epoch] >= 1,
              "VM clear/load bumps the epoch and cancels");
    }
    arm_free_construct();
    {
        const auto epoch = restore_epoch.load();
        const auto bumps = restore_eh_bumps.load();
        FrameCall f{};
        f.flags = 0x202;
        fixture_call_frame(stubs[restore_eh_kind], &f);
        check(restore.phase == phase_pending && restore_epoch.load() == epoch + 1 && restore_eh_bumps.load() == bumps + 1,
              "eh adapter bumps the epoch without touching locked state");
        observe(o_cockpit2);
        check(restore.phase == phase_idle && restore.cancels[cancel_epoch] >= 1, "the next frame observes the stale epoch");
    }
    arm_free_construct();
    construct(o_cockpit3, restore_load_constructor_caller); // return address 0x0041f840
    check(restore.phase == phase_idle && restore.cancels[cancel_lifetime] == 1, "load-chain constructor cancels");
    arm_free_construct();
    due();
    destroy(o_cockpit2, 0x41ccfbu);
    check(restore.phase == phase_idle && restore.cancels[cancel_lifetime] == 2, "bound cockpit destruction cancels");
    reset_state();
    construct(o_cockpit, 0x41f83bu);
    observe(o_cockpit);
    destroy(o_cockpit);
    destroy(o_cockpit3, 0x41ccfbu);
    check(restore.phase == phase_idle && restore.cancels[cancel_second_destruction] == 1,
          "a second destruction while pending (nothing bound) cancels");
    arm_free_construct();
    for (unsigned i = 0; i <= restore_pending_update_expiry; ++i) update();
    check(restore.phase == phase_idle && restore.cancels[cancel_expiry] == 1, "bounded update expiry cancels pending");
    arm_free_construct();
    put(W(o_cockpit2 + 0x150), restore_rear_mode);
    observe(o_cockpit2);
    check(restore.phase == phase_idle && restore.cancels[cancel_mode_left] == 1,
          "a fresh cockpit already at 258 cancels mode_left");
    // 9. Arm refusals and the armed cockpit leaving 258; a side cockpit at 258 without control leaves the arm.
    reset_state();
    construct(o_cockpit, 0x41f83bu);
    put(W(o_cockpit + 0x1c0), 2u);
    observe(o_cockpit);
    check(restore.phase == phase_idle && restore.arm_refusal_reasons[arm_connect] == 1 && arm_refused_lines >= 1,
          "connect refuses arming and samples once");
    put(W(o_cockpit + 0x1c0), 0u);
    put(W(o_cockpit + 0x10), 0u);
    observe(o_cockpit);
    check(restore.arm_refusal_reasons[arm_view_not_ready] == 1, "view 0 refuses arming");
    put(W(o_cockpit + 0x10), W(o_ship));
    put(W(o_cockpit_row + 8), 0u);
    observe(o_cockpit);
    check(restore.arm_refusal_reasons[arm_no_handle] == 1, "no active handle refuses arming");
    put(W(o_cockpit_row + 8), W(o_cockpit));
    put(W(o_ship + 0x40), restore_ship_delete_flag);
    observe(o_cockpit);
    check(restore.arm_refusal_reasons[arm_ship_dead] == 1, "deferred-delete ship refuses arming");
    put(W(o_ship + 0x40), 0u);
    observe(o_cockpit);
    observe(o_cockpit);
    check(restore.phase == phase_armed && restore.arms == 1, "rear frames re-arm each frame and count one arm");
    construct(o_cockpit3);
    put(W(o_cockpit3 + 0x150), restore_rear_mode);
    observe(o_cockpit3);
    check(restore.phase == phase_armed && restore.arm_cockpit == W(o_cockpit), "a side cockpit at 258 leaves the arm");
    put(W(o_cockpit + 0x150), 1u);
    observe(o_cockpit);
    check(restore.phase == phase_idle && restore.cancels[cancel_mode_left] == 1, "the armed cockpit at mode 1 disarms");
    // 10. An unreadable dispatcher frame captures nothing; a store into the armed (old) cockpit is not a capture.
    reset_state();
    construct(o_cockpit, 0x41f83bu);
    observe(o_cockpit);
    destroy(o_cockpit);
    construct(o_cockpit2);
    store(o_cockpit2, restore_internal_mode, W(o_input_frame));
    check(restore.monitor == 0, "a frame without the interpreter's return address captures nothing");
    put(W(o_dispatcher_frame + 4), 0x4a3908u);
    store(o_cockpit2, restore_internal_mode);
    check(restore.monitor == 0, "a wrong return address captures nothing");
    put(W(o_dispatcher_frame + 4), 0x4a3909u);
    store(o_cockpit2, restore_internal_mode);
    check(restore.monitor == monitor_id, "the validated frame captures the monitor");
    // 11. HUD refresh: the task stack carries the ship-change chain (bottom pair, RestartAllMonitors' record
    // with the manager's static record, SelectMode's cells); the restored ticket makes the second call
    // UpdateVisibility() on the manager key, argument 0 = 0, argc 0, under the engine's x87/MXCSR image.
    for (const std::uint32_t refresh_al : {1u, 0u}) {
        reset_state();
        mock.refresh_result = refresh_al;
        put(W(o_manager_context), manager_key);
        put(W(o_manager_context + 8), W(o_manager_descriptor));
        put(W(o_manager_descriptor), 0x25du);
        unsigned used = 0;
        auto push = [&](unsigned char tag, std::uint32_t v) {
            const std::uint32_t at = W(o_task_stack_base) - 5 * ++used;
            put(at, tag);
            put(at + 1, v);
        };
        push(3, 0), push(10, 0);                     // 0x0049f430's bottom pair
        push(3, 0x1234), push(10, W(o_manager_context)); // RestartAllMonitors -0x85-> SelectMode
        for (unsigned i = 0; i < 12; ++i) push(1, i);    // SelectMode's argument, locals, the native's argc
        put(W(o_task + 0x14), W(o_task_stack_base));
        put(W(o_task + 0x10), 64u);
        put(W(o_task + 0x18), std::uint32_t(-std::int32_t(used)));
        construct(o_cockpit, 0x41f83bu);
        observe(o_cockpit);
        destroy(o_cockpit);
        construct(o_cockpit2);
        store(o_cockpit2, restore_internal_mode);
        check(restore.monitor == monitor_id && restore.manager == manager_key && restore.manager_class == 0x25du &&
                  restore.manager_scan == scan_found,
              "the store captures the manager from the task's return records");
        control(o_cockpit2);
        due();
        const auto rows = outcome_lines;
        const std::uint16_t engine_cw = 0x077f;
        const std::uint32_t engine_mxcsr = 0x3f80;
        std::uint16_t cw_after = 0;
        std::uint32_t mxcsr_after = 0;
        asm volatile("fldcw %0\n\tldmxcsr %1" ::"m"(engine_cw), "m"(engine_mxcsr) : "memory");
        FrameCall f = input_entry();
        SetLastError(0x2468ace0);
        run_input(f);
        asm volatile("fnstcw %0\n\tstmxcsr %1" : "=m"(cw_after), "=m"(mxcsr_after)::"memory");
        const std::uint16_t default_cw = 0x037f;
        const std::uint32_t default_mxcsr = 0x1f80;
        asm volatile("fninit\n\tfldcw %0\n\tldmxcsr %1" ::"m"(default_cw), "m"(default_mxcsr) : "memory");
        check(mock.calls == 2 && mock.log_names[0] && !std::strcmp(mock.log_names[0], "SelectMode") &&
                  mock.log_args[0][0] == 0 && mock.log_args[0][2] == monitor_id && mock.log_args[0][5] == 1,
              "first call SelectMode(258) on the monitor");
        check(mock.log_names[1] && !std::strcmp(mock.log_names[1], "UpdateVisibility") && mock.log_args[1][0] == 0 &&
                  mock.log_args[1][1] == 0 && mock.log_args[1][2] == manager_key && mock.log_args[1][4] == 1 &&
                  mock.log_args[1][5] == 0,
              "second call UpdateVisibility() on the manager key, argument 0 = 0, argc 0");
        check(mock.log_cw[1] == engine_cw && mock.log_mxcsr[1] == engine_mxcsr && mock.log_error[1] == 0x2468ace0,
              "the second call runs under the engine's incoming x87 CW, MXCSR and LastError");
        check(GetLastError() == 0x2468ace0 && cw_after == engine_cw && mxcsr_after == engine_mxcsr,
              "LastError and the engine's x87/MXCSR image are back after both calls");
        check(f.out[7] == 0x11 && f.out[6] == 0x22 && f.out[5] == 0x33 && f.out[4] == 0x44 && f.out[1] == 0x55 &&
                  f.out[0] == 0x66 && f.out[2] == 0x77 && (f.out[8] & 0x8d5) == (0x202 & 0x8d5),
              "site I stub returns the registers and flags untouched after both calls");
        check(outcome_lines == rows + 1 && last_hud_refresh &&
                  !std::strcmp(last_hud_refresh, refresh_al ? "ok" : "refused") &&
                  restore.hud_refreshes[refresh_al ? hud_ok : hud_refused] == 1 && !restore.held.wanted,
              refresh_al ? "one outcome row with hud_refresh=ok" : "one outcome row with hud_refresh=refused");
    }
    // 12. Skipped: an empty task stack (no caller frame) -> one call, hud_refresh=skipped.
    arm_free_construct();
    check(restore.manager == 0 && restore.manager_scan == scan_bottom, "no caller frame: no manager (bottom)");
    due();
    {
        const auto rows = outcome_lines;
        FrameCall f = input_entry();
        run_input(f);
        check(mock.calls == 1 && outcome_lines == rows + 1 && last_hud_refresh &&
                  !std::strcmp(last_hud_refresh, "skipped"),
              "without a manager one call and hud_refresh=skipped");
    }
    reset_state();
}
// Real install/rollback on fixture-owned copies of the four spans.
struct Synthetic {
    unsigned char* fn[restore_site_count]{};
    unsigned char* memory = nullptr;
};
bool build_synthetic(Synthetic& s) {
    s.memory = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!s.memory) return false;
    // Tails undo what the displaced prologues did to the stack so their register effects stay observable:
    // inject (push ebp; mov ebp,esp; and esp,-16): restore esp from ebp, pop ebp; vm_clear (sub esp,8; push ebp;
    // mov ebp,[esp+0x10]): pop ebp, add esp,8; vm_load (push -1; push imm): add esp,8; eh: mov eax only.
    const unsigned char tails[restore_site_count][8] = {{0x8b, 0xe5, 0x5d, 0xc3},
                                                        {0x5d, 0x83, 0xc4, 0x08, 0xc3},
                                                        {0x83, 0xc4, 0x08, 0xc3},
                                                        {0xc3}};
    const unsigned tail_len[restore_site_count] = {4, 5, 4, 1};
    for (unsigned i = 0; i < restore_site_count; ++i) {
        s.fn[i] = s.memory + 0x40 * i;
        std::memcpy(s.fn[i], restore_specs[i].expected, restore_specs[i].length);
        std::memcpy(s.fn[i] + restore_specs[i].length, tails[i], tail_len[i]);
    }
    return FlushInstructionCache(GetCurrentProcess(), s.memory, 0x1000) != FALSE;
}
FrameCall native_frame(unsigned kind) {
    FrameCall f{};
    f.flags = 0x246;
    f.eax = 0x30;
    f.ebx = 0x31;
    f.ebp = 0x32;
    f.ecx = 0x33;
    f.edx = 0x34;
    f.esi = 0x35;
    f.edi = 0x36;
    for (unsigned i = 0; i < 12; ++i) f.stack[i] = 0x100 + i;
    (void)kind;
    return f;
}
void install_rollback() {
    Synthetic s;
    check(build_synthetic(s), "synthetic span functions");
    if (!s.memory) return;
    engine_patch::SiteSpec local[restore_site_count];
    for (unsigned i = 0; i < restore_site_count; ++i) {
        local[i] = restore_specs[i];
        local[i].address = reinterpret_cast<std::uintptr_t>(s.fn[i]);
    }
    FrameCall baseline[restore_site_count];
    for (unsigned i = 0; i < restore_site_count; ++i) {
        baseline[i] = native_frame(i);
        fixture_call_frame(s.fn[i], &baseline[i]);
    }
    // inject: ebp restored by the tail; vm_clear: ebp = [esp+0x10] after sub 8 and push = the first caller word;
    // vm_load: registers untouched; eh: eax = 0x56e5f4.
    check(baseline[0].out[2] == 0x32 && baseline[1].out[2] == 0x100 && baseline[2].out[7] == 0x30 &&
              baseline[3].out[7] == 0x56e5f4,
          "synthetic spans execute their displaced semantics");
    // Partial install: a corrupted third span rolls the first two back.
    s.fn[2][1] ^= 1;
    check(!restore_sites_install(local), "corrupted span refuses the install");
    bool clean = true;
    for (unsigned i = 0; i < restore_site_count; ++i)
        clean = clean && !restore_sites[i].patched_in &&
                !std::memcmp(s.fn[i], i == 2 ? restore_specs[i].expected : local[i].expected,
                             i == 2 ? 1 : local[i].length);
    check(clean && !std::strcmp(restore_sites[2].status, "bytes_mismatch"),
          "partial install rolls earlier sites back to original bytes");
    s.fn[2][1] ^= 1;
    for (auto& site : restore_sites) site = {};
    restore = {};
    restore_enabled.store(true);
    restore_filter_publish();
    check(restore_sites_install(local), "all four synthetic sites install");
    bool patched = true;
    for (unsigned i = 0; i < restore_site_count; ++i)
        patched = patched && restore_sites[i].patched_in && s.fn[i][0] == 0xe9 &&
                  !std::strcmp(restore_sites[i].status, "active");
    check(patched, "four sites active with jmp at entry");
    const auto inject_calls = restore_timing[restore_inject_kind].calls;
    restore_inject_filter.store(1); // force the injection stub's full path once (nothing due: the handler returns)
    for (unsigned i = 0; i < restore_site_count; ++i) {
        FrameCall f = native_frame(i);
        SetLastError(0x2468ace0);
        fixture_call_frame(s.fn[i], &f);
        const bool same_regs = !std::memcmp(f.out, baseline[i].out, 8 * 4) &&
                               (f.out[8] & 0x8d5) == (baseline[i].out[8] & 0x8d5);
        check(same_regs && GetLastError() == 0x2468ace0,
              "patched span preserves registers, flags, LastError and displaced semantics");
    }
    check(restore_timing[restore_inject_kind].calls == inject_calls + 1 && restore_inject_filter.load() == 0 &&
              mock.calls == 0,
          "injection handler ran once through the patched span, found nothing due and republished idle");
    check(restore_sites_restore(), "restore returns success");
    bool restored = true;
    for (unsigned i = 0; i < restore_site_count; ++i)
        restored = restored && !restore_sites[i].patched_in &&
                   !std::memcmp(s.fn[i], local[i].expected, local[i].length) &&
                   !std::strcmp(restore_sites[i].status, "restored");
    check(restored, "rollback restores every original byte");
    for (unsigned i = 0; i < restore_site_count; ++i) {
        FrameCall f = native_frame(i);
        fixture_call_frame(s.fn[i], &f);
        check(!std::memcmp(f.out, baseline[i].out, 8 * 4), "restored span behaves as baseline");
    }
    for (auto& site : restore_sites) site = {};
    restore = {};
    restore_filter_publish();
}
void late_window() {
    Synthetic s;
    if (!build_synthetic(s)) return;
    engine_patch::SiteSpec local[restore_site_count];
    for (unsigned i = 0; i < restore_site_count; ++i) {
        local[i] = restore_specs[i];
        local[i].address = reinterpret_cast<std::uintptr_t>(s.fn[i]);
    }
    engine_patch::close_install_window("fixture first present");
    check(!restore_sites_install(local) && !std::strcmp(restore_sites[0].status, "late_claim") &&
              s.fn[0][0] == restore_specs[0].expected[0],
          "late window refuses without touching bytes");
}
}
int main(int argc, char** argv) {
    const bool cpu_only = argc == 2 && !std::strcmp(argv[1], "--cpu-only");
    if (argc > 1 && !cpu_only) {
        std::fprintf(stderr, "usage: chase_transition_cpu_fixture.exe [--cpu-only]\n");
        return 2;
    }
    for (unsigned i = 0; i < sizeof fixture_xmm_seed; ++i) fixture_xmm_seed[i] = static_cast<unsigned char>(i * 37 + 9);
    x3m::engine_patch::Emitter tail(8);
    void* continuation = tail.here();
    tail.byte(0xc3);
    if (!tail.finish()) return 2;
    // Option-off path first: production initialize() with X3M_CHASE_VIEW_RESTORE unset claims none of the sites.
    SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE", nullptr);
    SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE_RETRIES", nullptr);
    transition::initialize();
    {
        bool none = !transition::restore_installed();
        for (const auto& site : transition::restore_sites) none = none && !site.claimed && !site.patched_in;
        check(none && !transition::restore_wanted() && transition::restore_inject_filter.load() == 0,
              "option off: no restore site claimed, prefilter idle");
    }
    SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE", L"1");
    check(transition::restore_wanted(), "option parses X3M_CHASE_VIEW_RESTORE=1");
    {
        unsigned r = 99;
        check(transition::restore_retries(r) && r == transition::detail::restore_default_retries, "retries default");
        SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE_RETRIES", L"5");
        check(transition::restore_retries(r) && r == 5, "retries 5 parses");
        SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE_RETRIES", L"6");
        check(!transition::restore_retries(r), "retries 6 refuses (fail closed)");
        SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE_RETRIES", L"x");
        check(!transition::restore_retries(r), "retries x refuses");
        SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE_RETRIES", nullptr);
    }
    transition::enabled.store(true);
    transition::diagnostic.store(true);
    LARGE_INTEGER timing_frequency{};
    transition::timing = QueryPerformanceFrequency(&timing_frequency) && timing_frequency.QuadPart > 0;
    transition::frequency = transition::timing ? std::uint64_t(timing_frequency.QuadPart) : 0;
    check(transition::timing, "transition handler timing enabled for CPU preservation checks");
    x3m::chase_lead::enabled.store(false);
    x3m::chase_lead::hud_enabled.store(false);
    x3m::chase_lead::native_timing_enabled.store(false);
    void* stubs[18]{};
    for (unsigned kind = 0; kind < 18; ++kind) {
        void** next = nullptr;
        void* stub = kind < 9 ? transition::emit(kind, &next) : x3m::chase_lead::emit(kind - 9, &next);
        stubs[kind] = stub;
        check(stub && next, "emit succeeds");
        if (!stub || !next) return 2;
        check(x3m::engine_patch::store_pointer(next, continuation), "continuation stored");
        std::uint32_t arguments[16]{};
        Snapshot baseline{}, hooked{};
        auto invoke = [&](void* target, Snapshot& out) {
            fixture_output = &out;
            SetLastError(0x13572468);
            fixture_call(std::uint32_t(reinterpret_cast<std::uintptr_t>(target)),
                         std::uint32_t(reinterpret_cast<std::uintptr_t>(arguments)),
                         std::uint32_t(reinterpret_cast<std::uintptr_t>(arguments)), 0, 0, 0);
            check(GetLastError() == 0x13572468, "LastError preserved");
            check(fixture_entry_esp == fixture_exit_esp, "four-byte caller stack balanced");
            check(!std::memcmp(fixture_input_x87, out.x87, 108), "incoming live x87 image preserved");
        };
        invoke(continuation, baseline);
        invoke(stub, hooked);
        for (unsigned r = 0; r < 9; ++r)
            if (r != 3) check(baseline.regs[r] == hooked.regs[r], "GPR/flags preserved");
        check(!std::memcmp(baseline.xmm, hooked.xmm, 128), "XMM0-7 preserved");
        check(baseline.mxcsr == hooked.mxcsr, "MXCSR preserved");
    }
    for (unsigned kind = 0; kind < 9; ++kind)
        check(transition::handler_timing[kind].calls == 1, "each transition handler records one timed CPU-check call");
    // Restore stubs: the same hostile-state preservation harness; the injection stub on its idle-skip path and
    // its full path (nothing due).
    restore_fixture::build_world();
    transition::native_registry_root = restore_fixture::W(0);
    transition::cockpit_registry_root = restore_fixture::W(4);
    transition::game_root = restore_fixture::W(8);
    transition::script_call_address = reinterpret_cast<std::uintptr_t>(&restore_fixture::mock_script_call);
    transition::restore_enabled.store(true);
    for (unsigned kind = 0; kind < transition::restore_site_count; ++kind) {
        void** next = nullptr;
        void* stub = transition::emit_restore(kind, &next);
        restore_fixture::stubs[kind] = stub;
        check(stub && next, "restore emit succeeds");
        if (!stub || !next) return 2;
        check(x3m::engine_patch::store_pointer(next, continuation), "restore continuation stored");
        for (unsigned mode = 0; mode < 2; ++mode) {
            std::uint32_t arguments[16]{};
            Snapshot baseline{}, hooked{};
            auto invoke = [&](void* target, Snapshot& out) {
                fixture_output = &out;
                SetLastError(0x13572468);
                transition::restore_inject_filter.store(mode);
                fixture_call(std::uint32_t(reinterpret_cast<std::uintptr_t>(target)),
                             std::uint32_t(reinterpret_cast<std::uintptr_t>(arguments)),
                             std::uint32_t(reinterpret_cast<std::uintptr_t>(arguments)), 0, 0, 0);
                check(GetLastError() == 0x13572468, "restore LastError preserved");
                check(fixture_entry_esp == fixture_exit_esp, "restore four-byte caller stack balanced");
                check(!std::memcmp(fixture_input_x87, out.x87, 108), "restore incoming live x87 image preserved");
            };
            const auto calls = transition::restore_timing[transition::restore_inject_kind].calls;
            invoke(continuation, baseline);
            invoke(stub, hooked);
            for (unsigned r = 0; r < 9; ++r)
                if (r != 3) check(baseline.regs[r] == hooked.regs[r], "restore GPR/flags preserved");
            check(!std::memcmp(baseline.xmm, hooked.xmm, 128), "restore XMM0-7 preserved");
            check(baseline.mxcsr == hooked.mxcsr, "restore MXCSR preserved");
            if (kind == transition::restore_inject_kind)
                check(transition::restore_timing[kind].calls == calls + (mode ? 1 : 0),
                      mode ? "published prefilter reaches the injection handler" : "idle prefilter skips the handler");
        }
    }
    transition::restore_inject_filter.store(0);
    restore_fixture::install_rollback();
    restore_fixture::scenarios();
    restore_fixture::late_window();
    check(log_under_lock == 0, "no log call under the chase SRW lock");
    check(outcome_lines > 0, "ticket outcomes emit the chase_view_restore row after the lock is released");
    checks += restore_fixture::checks;
    failures += restore_fixture::failures;
    if (!cpu_only) benchmark(continuation, stubs, restore_fixture::stubs[transition::restore_inject_kind]);
    std::printf("CHASE TRANSITION LEAD CPU stubs=18 restore_stubs=%u checks=%u failures=%u\n",
                transition::restore_site_count, checks, failures);
    return failures ? 1 : 0;
}
