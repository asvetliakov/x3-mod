// Synthetic executable-address map and actual production stubs. Raw game
// content is limited to the four reviewed short hook spans. Never launches X3.
#include "../../src/proxy/chase_aim_trace.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
namespace x3m {
void log(const char*, ...) {}
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
namespace aim = x3m::chase_aim_trace;
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
alignas(16) unsigned char fixture_xmm_seed[128];
std::uint16_t fixture_cw = 0x077f;
std::uint32_t fixture_mxcsr = 0x3f80;
void fixture_call(std::uint32_t site, std::uint32_t args, std::uint32_t locals, std::uint32_t eax, std::uint32_t ecx,
                  std::uint32_t edx);
}
// Four-byte caller stack; no 16-byte alignment promise. Save the fixture's
// state, seed hostile live state, run the real span, snapshot it, restore caller.
asm(".text\n.globl _fixture_call\n_fixture_call:\n"
    "pushfl\n pushal\n subl $256,%esp\n"
    "fnsave 0(%esp)\n frstor 0(%esp)\n stmxcsr 108(%esp)\n"
    "movups %xmm0,112(%esp)\n movups %xmm1,128(%esp)\n movups %xmm2,144(%esp)\n movups %xmm3,160(%esp)\n"
    "movups %xmm4,176(%esp)\n movups %xmm5,192(%esp)\n movups %xmm6,208(%esp)\n movups %xmm7,224(%esp)\n"
    "movl 296(%esp),%eax\n movl %eax,240(%esp)\n"
    "fninit\n fld1\n fldpi\n fldcw _fixture_cw\n ldmxcsr _fixture_mxcsr\n"
    "movups _fixture_xmm_seed,%xmm0\n movups _fixture_xmm_seed+16,%xmm1\n movups _fixture_xmm_seed+32,%xmm2\n movups _fixture_xmm_seed+48,%xmm3\n"
    "movups _fixture_xmm_seed+64,%xmm4\n movups _fixture_xmm_seed+80,%xmm5\n movups _fixture_xmm_seed+96,%xmm6\n movups _fixture_xmm_seed+112,%xmm7\n"
    "movl 300(%esp),%ebx\n movl 304(%esp),%ebp\n movl 308(%esp),%eax\n movl 312(%esp),%ecx\n movl 316(%esp),%edx\n"
    "movl $0x12345678,%esi\n movl $0x98765432,%edi\n pushl $0x247\n popfl\n call *240(%esp)\n"
    "pushfl\n pushal\n movl %esp,%esi\n movl _fixture_output,%edi\n movl $9,%ecx\n cld\n rep movsl\n"
    "movl _fixture_output,%edi\n movups %xmm0,36(%edi)\n movups %xmm1,52(%edi)\n movups %xmm2,68(%edi)\n movups %xmm3,84(%edi)\n"
    "movups %xmm4,100(%edi)\n movups %xmm5,116(%edi)\n movups %xmm6,132(%edi)\n movups %xmm7,148(%edi)\n"
    "fnsave 164(%edi)\n frstor 164(%edi)\n stmxcsr 272(%edi)\n addl $36,%esp\n"
    "frstor 0(%esp)\n ldmxcsr 108(%esp)\n"
    "movups 112(%esp),%xmm0\n movups 128(%esp),%xmm1\n movups 144(%esp),%xmm2\n movups 160(%esp),%xmm3\n"
    "movups 176(%esp),%xmm4\n movups 192(%esp),%xmm5\n movups 208(%esp),%xmm6\n movups 224(%esp),%xmm7\n"
    "addl $256,%esp\n popal\n popfl\n ret\n");
static std::uint32_t address(void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
static void put(std::uintptr_t p, unsigned off, std::uint32_t v) {
    std::memcpy(reinterpret_cast<void*>(p + off), &v, 4);
}
static __attribute__((noinline)) void invoke(unsigned phase, std::uint32_t* args, std::uint32_t* local, Snapshot& out,
                                             std::uint32_t eax = 100000, std::uint32_t ecx = 1,
                                             std::uint32_t edx = 500) {
    fixture_output = &out;
    SetLastError(0x24681357);
    fixture_call(std::uint32_t(aim::specs[phase].address), address(args), address(local), eax, ecx, edx);
    check(GetLastError() == 0x24681357, "injected span preserves LastError");
}
// The PE loader reserves this synthetic section before the process heap and
// mappings take the low game addresses. The fixture's actual .text is linked
// above 0x620000; no real fixture code/data can overlap these test fragments.
// This is fixture-only ownership, not a production address indirection.
__attribute__((section(".x3map"), used)) unsigned char fixture_map[0x21f000]{};
static void map_inventory(std::uintptr_t begin, std::uintptr_t end, const char* reason) {
    unsigned rows = 0;
    while (begin < end && rows++ < 16) {
        MEMORY_BASIC_INFORMATION info{};
        SetLastError(0);
        const auto bytes = VirtualQuery(reinterpret_cast<void*>(begin), &info, sizeof info);
        const DWORD error = GetLastError();
        std::printf(
            "CHASE AIM MAP reason=%s query=0x%08lx bytes=%lu error=%lu base=%p size=%lu state=0x%lx protect=0x%lx type=0x%lx allocation=%p\n",
            reason, static_cast<unsigned long>(begin), static_cast<unsigned long>(bytes),
            static_cast<unsigned long>(error), info.BaseAddress, static_cast<unsigned long>(info.RegionSize),
            static_cast<unsigned long>(info.State), static_cast<unsigned long>(info.Protect),
            static_cast<unsigned long>(info.Type), info.AllocationBase);
        if (!bytes || !info.RegionSize) break;
        const auto next = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= begin) break;
        begin = next;
    }
    if (begin < end)
        std::printf("CHASE AIM MAP reason=%s truncated=1 next=0x%08lx\n", reason, static_cast<unsigned long>(begin));
}
static bool map_fixture() {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto module = GetModuleHandleW(nullptr);
    const auto page = system.dwPageSize;
    std::printf("CHASE AIM MAP module=%p stack=%p page=%lu granularity=%lu synthetic=%p bytes=%lu real_code=%p\n",
                module, &system, static_cast<unsigned long>(page),
                static_cast<unsigned long>(system.dwAllocationGranularity), fixture_map,
                static_cast<unsigned long>(sizeof fixture_map), reinterpret_cast<void*>(&fixture_call));
    if (module != reinterpret_cast<void*>(0x00400000) || address(fixture_map) != 0x00401000 ||
        reinterpret_cast<std::uintptr_t>(&fixture_call) < 0x00620000 || !page || (page & (page - 1))) {
        map_inventory(0x00400000, 0x00620000, "section_layout_failure");
        return false;
    }
    for (const auto& spec : aim::specs) {
        MEMORY_BASIC_INFORMATION info{};
        SetLastError(0);
        const auto queried = VirtualQuery(reinterpret_cast<void*>(spec.address), &info, sizeof info);
        const DWORD query_error = GetLastError();
        if (!queried || info.AllocationBase != module || info.Type != MEM_IMAGE || info.State != MEM_COMMIT) {
            std::printf("CHASE AIM MAP site=0x%08lx query_error=%lu owned_image=0\n",
                        static_cast<unsigned long>(spec.address), static_cast<unsigned long>(query_error));
            map_inventory(spec.address, spec.address + spec.length, "site_ownership_failure");
            return false;
        }
        DWORD old = 0;
        SetLastError(0);
        const bool protected_ok = VirtualProtect(reinterpret_cast<void*>(spec.address), spec.length + 1,
                                                 PAGE_EXECUTE_READWRITE, &old) != FALSE;
        const DWORD protect_error = GetLastError();
        if (!protected_ok) {
            std::printf("CHASE AIM MAP site=0x%08lx protect_error=%lu\n", static_cast<unsigned long>(spec.address),
                        static_cast<unsigned long>(protect_error));
            return false;
        }
    }
    std::printf(
        "CHASE AIM MAP strategy=owned_pe_section actual_site_addresses=1 section_begin=0x00401000 section_end=0x00620000\n");
    return true;
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "complete";
    for (unsigned i = 0; i < sizeof fixture_xmm_seed; ++i) fixture_xmm_seed[i] = static_cast<unsigned char>(i * 19 + 7);
    const bool mapped = map_fixture();
    check(mapped, "synthetic fixed image map");
    if (!mapped) return 2;
    for (const auto& spec : aim::specs) {
        std::memcpy(reinterpret_cast<void*>(spec.address), spec.expected, spec.length);
        *reinterpret_cast<unsigned char*>(spec.address + spec.length) = 0xc3;
    }
    alignas(16) std::uint32_t cockpit[0x200 / 4]{}, camera[0x310 / 4]{}, ship[0x100 / 4]{}, node[0x100 / 4]{};
    alignas(16) std::uint32_t types[0xdb8 / 4]{}, registry[8]{}, table[2]{}, bucket[1]{}, entry[3]{};
    alignas(16) std::uint32_t args[8]{}, locals[0x400 / 4]{};
    auto* local = locals + 0x300 / 4;
    cockpit[0x10 / 4] = address(ship);
    cockpit[0x58 / 4] = address(camera);
    cockpit[0x150 / 4] = 258;
    cockpit[0x1d8 / 4] = 0;
    ship[8 / 4] = 0x1234;
    ship[0x70 / 4] = address(node);
    ship[0x48 / 4] = 7;
    types[0xd4 / 4] = 1;
    types[0xdc / 4] = 0;
    registry[0] = address(table);
    registry[4] = 99;
    table[0] = address(bucket);
    table[1] = 1;
    bucket[0] = address(entry);
    entry[1] = 99;
    entry[2] = address(cockpit);
    put(0x00608504, 0, address(registry));
    put(0x00606fd4, 0, address(types));
    put(0x00607ce8, 0, 1);
    put(0x00607cec, 0, 500);
    put(0x00607cf0, 0, 300);
    put(0x00587b88, 0, 56756);
    put(0x00587b8c, 0, 5461);
    put(0x00587b90, 0, 37837);
    args[2] = address(ship);
    args[3] = 0;
    args[4] = 0;
    args[5] = 0x22;
    args[6] = 0xffffffff;
    put(address(local) - 0x30, 0, 20000);
    put(address(local) - 0x30, 4, 0);
    put(address(local) - 0x30, 8, 100000);
    put(address(local) - 0x14, 0, 100000);
    put(address(local) - 0x50, 0, 1000);
    put(address(local) - 0x50, 4, 2000);
    put(address(local) - 0x50, 8, 65000);
    put(address(local) - 0x74, 0, 1);
    Snapshot baseline[4]{}, hooked[4]{};
    for (unsigned phase = 0; phase < 4; ++phase) invoke(phase, args, local, baseline[phase]);
    // Preserve baseline writer globals for the same seeded state below.
    if (!std::strncmp(mode, "bad", 3) && mode[3] >= '0' && mode[3] <= '3')
        *reinterpret_cast<unsigned char*>(aim::specs[mode[3] - '0'].address) ^= 1;
    if (!std::strcmp(mode, "late")) x3m::engine_patch::close_install_window("fixture");
    const bool expected = !std::strcmp(mode, "complete");
    check(aim::initialize() == expected, "complete group or failure refusal");
    if (!expected) {
        for (const auto& site : aim::sites) check(!site.patched_in, "partial group rollback");
        const unsigned changed = mode[0] == 'b' ? unsigned(mode[3] - '0') : 0;
        for (unsigned i = 0; i < changed; ++i)
            check(!std::memcmp(reinterpret_cast<void*>(aim::specs[i].address), aim::specs[i].expected,
                               aim::specs[i].length),
                  "previously patched site bytes restored");
        std::printf("CHASE AIM RESULT mode=%s checks=%u failures=%u\n", mode, checks, failures);
        return failures ? 1 : 0;
    }
    aim::camera_context(address(cockpit), address(ship), address(camera), 258, true);
    for (unsigned phase = 0; phase < 4; ++phase) {
        invoke(phase, args, local, hooked[phase]);
        // ESP records the call-frame address, shared by both invocations.
        check(!std::memcmp(baseline[phase].regs, hooked[phase].regs, sizeof baseline[phase].regs),
              "real stub preserves native GPR and flags outcome");
        check(!std::memcmp(baseline[phase].xmm, hooked[phase].xmm, 128), "real stub preserves live XMM0-7");
        check(!std::memcmp(baseline[phase].x87, hooked[phase].x87, 108),
              "real stub preserves live x87 stack control status");
        check(baseline[phase].mxcsr == hooked[phase].mxcsr, "real stub preserves directed MXCSR");
    }
    check(aim::counts.entries == 1 && aim::counts.rays == 1 && aim::counts.finals == 1,
          "phase counts from actual installed stubs");
    check(aim::used == 1 && aim::events[0].admitted && aim::events[0].finals == 1, "entry ray final correlation");
    check(aim::events[0].gate == 0 && aim::events[0].cursor_marker == 1, "native admission observation");
    check(aim::counts.writer_updates == 1 && aim::last_cursor_write.active == 1 && aim::last_cursor_write.x == 500 &&
              aim::last_cursor_write.y == 100000,
          "writer register inputs observed");
    Snapshot scratch{};
    invoke(2, args, local, scratch);
    check(aim::events[0].finals == 2, "multiple barrels retain call correlation");
    args[5] = 2;
    invoke(0, args, local, scratch);
    invoke(2, args, local, scratch);
    check(aim::events[1].gate == 2 && !aim::events[1].admitted, "cursor flag off has separate event");
    args[5] = 0x22;
    put(0x00607ce8, 0, 0);
    invoke(0, args, local, scratch);
    check(aim::events[2].gate == 3, "inactive cursor gate captured");
    put(0x00607ce8, 0, 1);
    const auto age_slot = aim::used;
    aim::context.qpc = aim::now() - std::uint64_t(aim::frequency.QuadPart) * 3;
    invoke(0, args, local, scratch);
    check(aim::events[age_slot].gate == 9, "stale pose age is explicitly unknown diagnostic context");
    aim::camera_context(address(cockpit), address(ship), address(camera), 258, true);
    const auto camera_slot = aim::used;
    const auto camera_saved = cockpit[0x58 / 4];
    cockpit[0x58 / 4] = address(camera) + 4;
    invoke(0, args, local, scratch);
    check(aim::events[camera_slot].gate == 9, "resolved camera mismatch cannot claim coherent pose");
    cockpit[0x58 / 4] = camera_saved;
    const auto cockpit_slot = aim::used;
    const auto context_cockpit = aim::context.cockpit;
    aim::context.cockpit += 4;
    invoke(0, args, local, scratch);
    check(aim::events[cockpit_slot].gate == 9, "resolved cockpit mismatch cannot claim coherent pose");
    aim::context.cockpit = context_cockpit;
    while (aim::used < aim::capacity) invoke(0, args, local, scratch);
    const auto prior = aim::events[aim::capacity - 1].finals;
    invoke(0, args, local, scratch);
    invoke(1, args, local, scratch);
    invoke(2, args, local, scratch);
    check(aim::counts.dropped == 1 && aim::counts.omitted_followups == 2, "full sample window counts omitted phases");
    check(aim::events[aim::capacity - 1].finals == prior, "unsampled reused frame cannot append to old event");
    const auto seq = aim::context.sequence;
    aim::invalidate_camera(address(cockpit) + 4);
    check(aim::context.sequence == seq && aim::player.load() == address(ship),
          "unreadable unrelated cockpit leaves active context");
    aim::invalidate_camera(address(cockpit));
    check(!aim::player.load() && aim::context.sequence == seq + 1, "current unreadable context invalidates filter");
    aim::camera_context(address(cockpit), address(ship), address(camera), 1, false);
    check(aim::context.mode == 1 && !aim::context.applied && aim::player.load() == address(ship),
          "internal pass through retained for A B");
    aim::report(300);
    check(!aim::used && !aim::counts.entries, "report drains bounded samples and counters");
    invoke(0, args, local, scratch);
    const auto first_serial = aim::events[0].serial;
    invoke(0, args, local, scratch);
    invoke(2, args, local, scratch);
    check(aim::events[1].serial > first_serial && aim::events[1].finals == 1 && !aim::events[0].finals,
          "sampled reused EBP belongs only to new serial");
    const auto orphan_before = aim::counts.orphans;
    invoke(2, args, local + 1, scratch);
    check(aim::counts.orphans == orphan_before + 1, "out of order frame produces orphan without stale append");
    check(aim::pending[0].slot >= 0, "report reset test starts with valid pending sample");
    aim::report(301);
    invoke(2, args, local, scratch);
    check(aim::counts.omitted_followups == 1 && !aim::used, "report invalidates previously valid pending sample");
    check(!aim::read(UINT32_MAX - 3, 8, &scratch, 4) && !aim::read(3, 0, &scratch, 4),
          "unaligned and wrapping diagnostic reads refused");
    for (auto& site : aim::sites) check(x3m::engine_patch::restore(site), "fixture quiescent site restore");
    std::printf("CHASE AIM RESULT mode=%s checks=%u failures=%u\n", mode, checks, failures);
    return failures ? 1 : 0;
}
