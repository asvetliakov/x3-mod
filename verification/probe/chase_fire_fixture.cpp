// Execute the unchanged native CMP and actual relocated JZ/stub in a fixture
// PE-owned synthetic map. No game functions are called and no game is started.
#include "../../src/proxy/chase_fire.cpp"
// Exercise only the corrected screen reader; no diagnostic sites are installed.
#include "../../src/proxy/chase_aim_trace.cpp"
#include <cstdio>
namespace x3m {
void log(const char*, ...) {}
}
namespace x3m::telemetry {
bool enabled() {
    return false;
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return true;
}
}
static bool requested = true;
namespace x3m::chase_camera {
bool installed() {
    return requested;
}
bool wanted() {
    return requested;
}
}
namespace fire = x3m::chase_fire;
static unsigned checks = 0, failures = 0;
static void check(bool ok, const char* name) {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s\n", name, ok ? "PASS" : "FAIL");
}
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
void fixture_call(std::uint32_t site, std::uint32_t args, std::uint32_t locals, std::uint32_t eax, std::uint32_t ecx,
                  std::uint32_t edx);
}
// Four-byte caller stack; no 16-byte alignment promise. Save the fixture's
// state, seed hostile live state, run the real span, snapshot it, restore caller.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
    "movl %esp,_fixture_entry_esp\n"
    "pushfl\n pushal\n subl $256,%esp\n"
    "fnsave 0(%esp)\n frstor 0(%esp)\n stmxcsr 108(%esp)\n"
    "movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
    "movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
    "movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
    "fninit\n fld1\n fldpi\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n"
    "movups _fixture_xmm_seed,%xmm0\n movups _fixture_xmm_seed+16,%xmm1\n movups _fixture_xmm_seed+32,%xmm2\n movups _fixture_xmm_seed+48,%xmm3\n"
    "movups _fixture_xmm_seed+64,%xmm4\n movups _fixture_xmm_seed+80,%xmm5\n movups _fixture_xmm_seed+96,%xmm6\n movups _fixture_xmm_seed+112,%xmm7\n"
    "movl 300(%esp),%ebx\n movl 304(%esp),%ebp\n movl 308(%esp),%eax\n movl 312(%esp),%ecx\n movl 316(%esp),%edx\n"
    "movl $0x12345678,%esi\n movl $0x98765432,%edi\n pushl $0x247\n popfl\n fnsave _fixture_input_x87\n frstor _fixture_input_x87\n call *240(%esp)\n"
    "pushfl\n pushal\n movl %esp,%esi\n movl _fixture_output,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
    "movl _fixture_output,%edi\n movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
    "movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
    "fnsave 164(%edi)\n frstor 164(%edi)\n stmxcsr 272(%edi)\n addl $36,%esp\n"
    "frstor 0(%esp)\n ldmxcsr 108(%esp)\n"
    "movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n"
    "movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
    "addl $256,%esp\n popal\n popfl\n movl %esp,_fixture_exit_esp\n ret\n");
static std::uint32_t address(void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
static void put(std::uintptr_t p, unsigned off, std::uint32_t v) {
    std::memcpy(reinterpret_cast<void*>(p + off), &v, 4);
}

__attribute__((section(".x3map"), used)) unsigned char fixture_map[0x21f000]{};
static unsigned branch = 0;
static void branch_code(std::uintptr_t at, unsigned value) {
    // mov dword ptr [branch],value; ret -- leaves every register/flag intact.
    const unsigned char prefix[] = {0xc7, 0x05};
    std::memcpy(reinterpret_cast<void*>(at), prefix, 2);
    put(at, 2, address(&branch));
    put(at, 6, value);
    *reinterpret_cast<unsigned char*>(at + 10) = 0xc3;
}
static bool map_fixture() {
    const auto module = GetModuleHandleW(nullptr);
    if (module != reinterpret_cast<void*>(0x400000) || address(fixture_map) != 0x401000 ||
        reinterpret_cast<std::uintptr_t>(&fixture_call) < 0x620000)
        return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<void*>(fire::spec.address), &info, sizeof info) != sizeof info ||
        info.AllocationBase != module || info.State != MEM_COMMIT || info.Type != MEM_IMAGE)
        return false;
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(0x445000), 0x1000, PAGE_EXECUTE_READWRITE, &old)) return false;
    const unsigned char compare[] = {0x83, 0x3d, 0xe8, 0x7c, 0x60, 0x00, 0x00};
    std::memcpy(reinterpret_cast<void*>(0x445a3a), compare, sizeof compare);
    std::memcpy(reinterpret_cast<void*>(fire::spec.address), fire::spec.expected, fire::spec.length);
    branch_code(0x445a47, 1);
    branch_code(0x445cab, 2);
    return FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(0x445000), 0x1000) != FALSE;
}
static bool all_stack_balanced = true, all_x87_preserved = true;
static __attribute__((noinline)) void invoke(std::uint32_t* args, Snapshot& out, bool check_error = true) {
    fixture_output = &out;
    branch = 0;
    SetLastError(0x24681357);
    fixture_call(0x445a3a, address(args), address(args), 100000, 1, 500);
    const DWORD last_error = GetLastError();
    // Call-local ESP oracle: nested refusal callers can have different frames.
    const bool stack_ok = out.regs[3] == fixture_entry_esp - 296 && fixture_exit_esp == fixture_entry_esp;
    all_stack_balanced = all_stack_balanced && stack_ok;
    const bool x87_ok = !std::memcmp(fixture_input_x87, out.x87, 108);
    all_x87_preserved = all_x87_preserved && x87_ok;
    if (check_error) {
        check(last_error == 0x24681357, "branch callback preserves LastError");
        check(stack_ok, "each invocation preserves tested span and caller stack balance");
        check(x87_ok, "each invocation preserves full incoming x87 image");
    }
}
static void state_equal(const Snapshot& baseline, const Snapshot& actual, bool override) {
    bool regs = true;
    static bool stack_difference_reported = false;
    if (actual.regs[3] != baseline.regs[3] && !stack_difference_reported) {
        stack_difference_reported = true;
        std::printf(
            "CHASE FIRE STACK differing_index=3 baseline=0x%08lx actual=0x%08lx expected_from_entry=0x%08lx entry=0x%08lx exit=0x%08lx\n",
            static_cast<unsigned long>(baseline.regs[3]), static_cast<unsigned long>(actual.regs[3]),
            static_cast<unsigned long>(fixture_entry_esp - 296), static_cast<unsigned long>(fixture_entry_esp),
            static_cast<unsigned long>(fixture_exit_esp));
    }
    for (unsigned i = 0; i < 9; ++i) {
        if (i == 3) continue; // exact per-invocation ESP oracle above
        const auto expected = baseline.regs[i] & (override && i == 8 ? ~fire::zero_flag : UINT32_MAX);
        if (actual.regs[i] != expected) {
            regs = false;
            std::printf("CHASE FIRE REGISTER index=%u expected=0x%08lx actual=0x%08lx\n", i,
                        static_cast<unsigned long>(expected), static_cast<unsigned long>(actual.regs[i]));
        }
    }
    check(regs, "GPR flags unchanged except authorized ZF");
    check(!std::memcmp(baseline.xmm, actual.xmm, 128), "XMM0-7 preserved");
    // FNINIT marks registers empty but does not zero their physical payloads.
    // A prior floating-point benchmark can legitimately change those bytes;
    // compare the complete pre/post image of THIS call without masking slots.
    static bool x87_difference_reported = false;
    if (!x87_difference_reported)
        for (unsigned i = 0; i < 108; ++i)
            if (baseline.x87[i] != actual.x87[i]) {
                x87_difference_reported = true;
                std::uint16_t baseline_tag = 0, input_tag = 0, post_tag = 0;
                std::memcpy(&baseline_tag, baseline.x87 + 8, 2);
                std::memcpy(&input_tag, fixture_input_x87 + 8, 2);
                std::memcpy(&post_tag, actual.x87 + 8, 2);
                std::printf(
                    "CHASE FIRE X87 cross_call_difference_byte=%u baseline=0x%02x current_input=0x%02x current_post=0x%02x baseline_tag=0x%04x input_tag=0x%04x post_tag=0x%04x\n",
                    i, unsigned(baseline.x87[i]), unsigned(fixture_input_x87[i]), unsigned(actual.x87[i]),
                    unsigned(baseline_tag), unsigned(input_tag), unsigned(post_tag));
                break;
            }
    check(!std::memcmp(fixture_input_x87, actual.x87, 108), "full call-local x87 stack control status preserved");
    check(baseline.mxcsr == actual.mxcsr, "directed MXCSR preserved");
}
// One bounded whole-span comparison. Includes this fixture's hostile-state
// setup/snapshot transport plus the native CMP/JZ; differences include the
// actual generated stub and complete CPU boundary. No per-call logs/asserts.
static double measure_span(std::uint32_t* args, unsigned iterations, Snapshot& out) {
    fixture_output = &out;
    LARGE_INTEGER f{}, begin{}, end{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&begin);
    for (unsigned i = 0; i < iterations; ++i) fixture_call(0x445a3a, address(args), address(args), 100000, 1, 500);
    QueryPerformanceCounter(&end);
    check(f.QuadPart > 0 && begin.QuadPart > 0 && end.QuadPart > begin.QuadPart,
          "whole-span aggregate timing has valid clock");
    check(out.regs[3] == fixture_entry_esp - 296 && fixture_exit_esp == fixture_entry_esp,
          "aggregate final invocation exact stack balance");
    check(!std::memcmp(fixture_input_x87, out.x87, 108), "aggregate final invocation full x87 image preserved");
    return f.QuadPart > 0 ? double(end.QuadPart - begin.QuadPart) * 1000000.0 / double(f.QuadPart) / iterations : 0;
}
int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "complete";
    check(map_fixture(), "PE-owned actual production branch map");
    if (failures) return 2;
    for (unsigned i = 0; i < sizeof fixture_xmm_seed; ++i) fixture_xmm_seed[i] = static_cast<unsigned char>(i * 19 + 7);
    alignas(16) std::uint32_t cockpit[0x200 / 4]{}, camera[0x310 / 4]{}, ship[0x100 / 4]{}, args[8]{};
    std::uint32_t registry[8]{}, table[2]{}, bucket[1]{}, entry[3]{}, defaults[12]{}, screen[2]{};
    cockpit[0xc / 4] = cockpit[0x10 / 4] = address(ship);
    cockpit[0x58 / 4] = address(camera);
    cockpit[0x150 / 4] = 258;
    ship[8 / 4] = 0x1234;
    camera[0x288 / 4] = 0;
    camera[0x28c / 4] = 65535;
    camera[0x290 / 4] = 0;
    camera[0x294 / 4] = 65535;
    registry[0] = address(table);
    registry[4] = 99;
    table[0] = address(bucket);
    table[1] = 1;
    bucket[0] = address(entry);
    entry[1] = 99;
    entry[2] = address(cockpit);
    defaults[0] = address(screen);
    screen[1] = (720u << 16) | 1280u;
    put(0x606f38, 0, address(defaults));
    put(0x608504, 0, address(registry));
    put(0x607ce8, 0, 0);
    put(0x607cec, 0, 640);
    put(0x607cf0, 0, 360);
    put(0x607c64, 0, 0x19283746);
    // The old reader mistook defaults+4 for packed dimensions. Keep it hostile.
    defaults[1] = 0xdeadbeef;
    x3m::chase_aim_trace::Pose trace_pose;
    x3m::chase_aim_trace::pose(address(cockpit), address(ship), address(camera), trace_pose);
    check((trace_pose.valid & 1024) && trace_pose.screen[0] == 1280 && trace_pose.screen[1] == 720,
          "diagnostic screen reader follows extra pointer");
    screen[1] = 0xffff0001;
    x3m::chase_aim_trace::pose(address(cockpit), address(ship), address(camera), trace_pose);
    check(!(trace_pose.valid & 1024), "diagnostic screen reader refuses negative dimensions");
    screen[1] = (720u << 16) | 1280u;
    args[2] = address(ship);
    args[4] = 0;
    args[5] = 0x2a;
    Snapshot zero{}, nonzero{}, out{};
    invoke(args, zero);
    check(branch == 2, "unpatched inactive CMP takes original JZ");
    put(0x607ce8, 0, 1);
    invoke(args, nonzero);
    check(branch == 1, "unpatched active CMP falls through");
    put(0x607ce8, 0, 0);
    constexpr unsigned span_iterations = 10000;
    const double native_us = !std::strcmp(mode, "complete") ? measure_span(args, span_iterations, out) : 0;
    if (!std::strcmp(mode, "off")) requested = false;
    if (!std::strcmp(mode, "bad")) *reinterpret_cast<unsigned char*>(fire::spec.address) ^= 1;
    if (!std::strcmp(mode, "late")) x3m::engine_patch::close_install_window("fixture");
    const bool expected = !std::strcmp(mode, "complete");
    check(fire::initialize() == expected, "requested valid site installs or refuses");
    if (!expected) {
        check(!fire::site.patched_in, "refusal leaves no patch");
        if (!std::strcmp(mode, "off") || !std::strcmp(mode, "late")) {
            check(!std::memcmp(reinterpret_cast<void*>(fire::spec.address), fire::spec.expected, fire::spec.length),
                  "disabled or late native bytes untouched");
            invoke(args, out);
            check(branch == 2, "disabled or late original branch still executes");
            state_equal(zero, out, false);
        }
        std::printf("CHASE FIRE RESULT mode=%s checks=%u failures=%u\n", mode, checks, failures);
        return failures ? 1 : 0;
    }
    check(!fire::timing, "behavior installs with telemetry disabled");
    auto publish = [&] {
        fire::camera_context(address(cockpit), address(ship), address(camera), 258, 0, 0, true, fire::now());
    };
    auto refused = [&](const char* name) {
        invoke(args, out);
        check(branch == 2, name);
        state_equal(zero, out, false);
    };
    const double early_us = measure_span(args, span_iterations, out);
    check(branch == 2 && !fire::counts.result[0], "whole-span no-witness loop retains native branch");
    publish();
    const double eligible_us = measure_span(args, span_iterations, out);
    check(branch == 1 && fire::counts.result[0] == span_iterations, "whole-span eligible loop overrides every call");
    std::printf(
        "CHASE FIRE SPAN PERF iterations=%u native_us=%.3f hooked_no_witness_us=%.3f eligible_override_us=%.3f early_delta_us=%.3f eligible_delta_us=%.3f scope=full_span_plus_fixture_transport fps_claim=0\n",
        span_iterations, native_us, early_us, eligible_us, early_us - native_us, eligible_us - native_us);
    fire::camera_context(0, 0, 0, 0, 0, 0, false, 0);
    fire::report(299);
    check(!fire::counts.result[0], "aggregate benchmark counters drained before controls");
    refused("no applied camera witness refuses");
    publish();
    invoke(args, out);
    check(branch == 1, "applied external cursor fire overrides JZ");
    state_equal(zero, out, true);
    check(fire::counts.result[0] == 1, "effective override counter separate from native input");
    check(*reinterpret_cast<unsigned*>(0x607ce8) == 0 && *reinterpret_cast<unsigned*>(0x607cec) == 640 &&
              *reinterpret_cast<unsigned*>(0x607cf0) == 360 && *reinterpret_cast<unsigned*>(0x607c64) == 0x19283746,
          "native cursor active xy and steering unchanged");
    put(0x607ce8, 0, 1);
    invoke(args, out);
    check(branch == 1, "native active path is original fallthrough");
    state_equal(nonzero, out, false);
    put(0x607ce8, 0, 0);
    args[5] = 2;
    refused("boresight flags refuse");
    args[5] = 0x28;
    refused("nonfire flags refuse");
    args[5] = 0x2a;
    args[4] = 1;
    refused("turret group refuses");
    args[4] = 0;
    args[2] = address(ship) + 4;
    refused("other ship refuses");
    args[2] = address(ship);
    cockpit[0x150 / 4] = 1;
    refused("live internal switch refuses without new update");
    cockpit[0x150 / 4] = 257;
    refused("other external view refuses");
    cockpit[0x150 / 4] = 258;
    cockpit[0x1c0 / 4] = 1;
    refused("scripted connect refuses");
    cockpit[0x1c0 / 4] = 0;
    cockpit[0x1a0 / 4] = 4;
    refused("verbatim camera mode refuses");
    cockpit[0x1a0 / 4] = 0;
    cockpit[0x58 / 4] += 4;
    refused("live camera mismatch refuses");
    cockpit[0x58 / 4] -= 4;
    cockpit[0xc / 4] += 4;
    refused("reference identity mismatch refuses");
    cockpit[0xc / 4] -= 4;
    entry[2] += 4;
    refused("resolved cockpit mismatch refuses");
    entry[2] -= 4;
    ship[2]++;
    refused("same address new ship id refuses");
    ship[2]--;
    fire::context.qpc = fire::now() - fire::frequency * 3;
    refused("stale applied pose refuses");
    publish();
    fire::context.qpc = fire::now() + fire::frequency;
    refused("future timestamp refuses");
    publish();
    fire::context.thread++;
    refused("different thread witness refuses");
    publish();
    put(0x607cec, 0, 1280);
    refused("cursor outside screen refuses");
    put(0x607cec, 0, 640);
    camera[0x290 / 4] = 40000;
    refused("cursor outside normalized viewport refuses");
    camera[0x290 / 4] = 0;
    screen[1] = 0;
    refused("invalid screen dimensions refuse");
    screen[1] = (720u << 16) | 1280u;
    defaults[0] = 0;
    refused("unreadable screen pointer refuses");
    defaults[0] = address(screen);
    fire::camera_context(address(cockpit), address(ship), address(camera), 1, 0, 0, false, fire::now());
    refused("internal update invalidates prior chase witness");
    publish();
    fire::camera_context(address(cockpit), address(ship), address(camera), 258, 0, 0, false, fire::now());
    refused("camera write refusal invalidates witness");
    publish();
    fire::invalidate_camera(address(cockpit) + 4);
    invoke(args, out);
    check(branch == 1, "unrelated monitor retains player witness");
    fire::invalidate_camera(address(cockpit));
    refused("same unreadable or inactive cockpit invalidates witness");
    check(!fire::read(UINT32_MAX - 3, 8, &out, 4) && !fire::read(3, 0, &out, 4),
          "unaligned and overflowing reads refuse");
    // Diagnostic timing measures only the eligible handler; this fixture is
    // not a gameplay FPS benchmark. Its counter/log path also preserves ABI.
    publish();
    fire::timing = true;
    for (unsigned i = 0; i < 100; ++i) invoke(args, out, false);
    check(GetLastError() == 0x24681357, "timed callback preserves LastError");
    check(all_stack_balanced, "all timing-loop invocations preserve exact stack balance");
    check(all_x87_preserved, "all timing-loop invocations preserve full incoming x87 image");
    check(fire::counts.timed == 100 && fire::counts.ticks > 0, "bounded aggregate timing exercised");
    state_equal(zero, out, true);
    std::printf("CHASE FIRE PERF calls=%llu mean_us=%.3f max_us=%.3f scope=eligible_handler_after_first_qpc\n",
                fire::counts.timed,
                double(fire::counts.ticks) * 1000000.0 / double(fire::frequency) / double(fire::counts.timed),
                double(fire::counts.max_ticks) * 1000000.0 / double(fire::frequency));
    fire::report(300);
    check(!fire::counts.result[0] && !fire::counts.timed, "report drains bounded counters");
    fire::shutdown();
    check(!fire::site.patched_in &&
              !std::memcmp(reinterpret_cast<void*>(fire::spec.address), fire::spec.expected, fire::spec.length),
          "quiescent restore puts original branch bytes back");
    invoke(args, out);
    check(branch == 2, "restored branch returns to native policy");
    state_equal(zero, out, false);
    std::printf("CHASE FIRE RESULT mode=%s checks=%u failures=%u\n", mode, checks, failures);
    return failures ? 1 : 0;
}
