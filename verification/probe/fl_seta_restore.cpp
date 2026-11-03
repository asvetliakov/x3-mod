// Native fixture of the FL SETA restore and key latch (src/engine/fl/seta_restore_fl.cpp, seta_restore_sites_fl.h, and
// the FL consumer path of src/proxy/chase_transition.cpp; docs/reverse-engineering/fl/seta.md, "Port (2026-11-03)").
// Built with -DX3M_EDITION_FL=1 -DX3M_CHASE_TRANSITION_FL_FIXTURE (this unit includes chase_transition.cpp to bind its
// base) and the production seta_restore_fl.cpp as a separate object with -DX3M_SETA_RESTORE_FL_FIXTURE, by
// verification/analysis/test_fl_seta_restore.py; run natively through WSL interop. No game launch, no Wine, nothing
// written to the game files: argv[1] X3FL.exe is mapped into a private 64 KB-aligned image with its relocations applied
// (fl_seta_image.h); argv[2] is a KC VM image (verification/probe/story_vm_image.py, built by the test from the local
// FL x3story.obj into a temporary file) whose segments are mapped at their recorded addresses, so the production story
// detection reads the real FL story.
//
// Engine stand-ins after the install (the windows are checked first): `jmp h_record` at the continuations of the factor
// hook (base+0x0ce0a6), the injection entry (base+0x0cfd06) and the three VM cancel spans; `jmp fake_wrapper` at the SETA
// wrapper base+0x0bb500 (records selector, token, key, name, kind, argc; returns the scenario's AL; can write a factor
// through the production handler from inside the call, or escape through a longjmp after the EH adapter's handler).
// The destination-Ready provider (fog_prefill_fl) is a stub the scenarios drive.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../src/proxy/chase_transition.cpp"
#include "../../src/engine/fl/fog_prefill_fl.h"
#include "../../src/engine/fl/seta_restore_sites_fl.h"
#include "../../src/proxy/seta_restore.h"
#include "fl_seta_harness.h"
#include "fl_seta_image.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace R = x3m::fl::seta_restore_fl;
namespace sr = x3m::seta_restore;
namespace CT = x3m::chase_transition;
using std::uint32_t;
namespace x3m::seta_restore {
void fixture_set_base(std::uintptr_t base);
void fixture_fault_hook();
void fixture_reset();
}
unsigned checks = 0, failures = 0;
void check(bool value, const char* label, const char* detail = "") {
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL %s %s\n", label, detail);
    }
}
std::vector<std::string> rows;
bool quiet = false;
namespace x3m {
void log(const char* format, ...) {
    char row[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(row, sizeof row, format, args);
    va_end(args);
    rows.emplace_back(row);
    if (!quiet) std::printf("LOG %s\n", row);
}
namespace telemetry {
bool enabled() {
    return false;
}
}
// The chase camera as the init-order modes need it: not wanted (the default run), or wanted and not yet initialized
// ("disabled", the loader before its chase block), or installed ("active", after it).
bool camera_wanted = false, camera_installed = false;
const char* camera_status = "disabled";
namespace chase_camera {
bool wanted() {
    return camera_wanted;
}
bool installed() {
    return camera_installed;
}
const char* status() {
    return camera_status;
}
}
namespace chase_lead {
void native_timing_invalidate(std::uintptr_t, std::uint32_t) noexcept {}
}
namespace fl {
std::uintptr_t module_base() {
    return 0; // both modules bind through their fixture seams
}
// The destination-Ready provider stand-in: admission and the walk's answer are the scenario's.
namespace fog_prefill_fl {
bool admitted_ = true;
int found_ = 0; // 0: Loading (not ready), 1: Found with node_
std::uint32_t node_ = 0;
unsigned walks_ = 0;
bool admitted() {
    return admitted_;
}
const char* status() {
    return admitted_ ? "ready" : "anchor_mismatch";
}
bool walk(x3m::fog_prefill::Result& out, std::uint32_t) {
    if (!admitted_) return false;
    ++walks_;
    out = {};
    out.status = found_ ? x3m::fog_prefill::Walk::Found : x3m::fog_prefill::Walk::Loading;
    out.node = found_ ? node_ : 0;
    return true;
}
}
}
}
namespace fp = x3m::fl::fog_prefill_fl;

// ---------------- the engine stand-ins ----------------
FlImage im;
alignas(16) unsigned char cfg[0x800], manager[0x100], game[0x800], ctx[0x500];
struct Object {
    alignas(16) unsigned char b[0x200];
    std::uint32_t& u32(unsigned at) { return *reinterpret_cast<std::uint32_t*>(b + at); }
    std::uint16_t& u16(unsigned at) { return *reinterpret_cast<std::uint16_t*>(b + at); }
    std::uint32_t va() const { return std::uint32_t(reinterpret_cast<std::uintptr_t>(b)); }
};
Object ship, ship2, sector_a, sector_b, context, desc;
// A VM task: 5-byte cells below `cells_top`; [task+0x10] capacity, +0x14 base, +0x18 signed index, +0x1c next IP.
alignas(16) unsigned char task[0x40], cells[0x100];
alignas(16) unsigned char args[8];
std::uint32_t stop_ip = 0, gate_return = 0, toggle_return = 0;
void set_factor(std::uint32_t old_factor) {
    std::memcpy(cfg + R::cfg_factor, &old_factor, 4);
}
void set_task(std::uint32_t ip, std::uint32_t ret) {
    std::memset(task, 0, sizeof task);
    std::memset(cells, 0, sizeof cells);
    unsigned char* top = cells + sizeof cells; // [task+0x14]
    const std::uint32_t base = std::uint32_t(reinterpret_cast<std::uintptr_t>(top));
    // two used cells from the top: (10, context) then (3, return)
    top[-10] = 10;
    const std::uint32_t c = context.va();
    std::memcpy(top - 9, &c, 4);
    top[-5] = 3;
    std::memcpy(top - 4, &ret, 4);
    const std::uint32_t capacity = 32, index = std::uint32_t(-2);
    std::memcpy(task + 0x10, &capacity, 4);
    std::memcpy(task + 0x14, &base, 4);
    std::memcpy(task + 0x18, &index, 4);
    std::memcpy(task + 0x1c, &ip, 4);
}
void set_args(std::uint32_t factor) {
    args[0] = 1; // int tag
    std::memcpy(args + 1, &factor, 4);
}
void set_object(Object& o, std::uint16_t cls, std::uint32_t id) {
    std::memset(o.b, 0, sizeof o.b);
    o.u16(0x48) = cls;
    o.u16(0x9c) = 0xcafe;
    o.u32(8) = id;
}
void set_player(Object& s) {
    const std::uint32_t v = s.va();
    std::memcpy(manager + R::manager_player, &v, 4);
}
// The SETA wrapper stand-in (cdecl, six words): records the call; `al` is the scenario's answer.
struct Call {
    std::uint32_t selector, token, key, kind, argc;
    char name[32];
};
std::vector<Call> calls;
unsigned fake_al = 1;
int nested_write = 0;   // 1: write a factor through the production handler from inside the call (StopFastForward's)
bool escape = false;    // run the EH adapter's handler and longjmp out of the call (a script error that unwinds)
void* escape_buffer[5];
extern "C" __attribute__((force_align_arg_pointer)) std::uint32_t __cdecl fake_wrapper(std::uint32_t selector,
                                                                                       std::uint32_t token,
                                                                                       std::uint32_t key,
                                                                                       const char* name,
                                                                                       std::uint32_t kind,
                                                                                       std::uint32_t argc) {
    Call c{selector, token, key, kind, argc, {}};
    std::snprintf(c.name, sizeof c.name, "%s", name ? name : "(null)");
    calls.push_back(c);
    if (nested_write) {
        set_args(0x10000);
        set_factor(0x40000); // the re-delivered toggle had restarted SETA; StopFastForward stops it again
        set_task(stop_ip, toggle_return);
        x3m_seta_restore_factor_write(std::uint32_t(reinterpret_cast<std::uintptr_t>(args)),
                                      std::uint32_t(reinterpret_cast<std::uintptr_t>(task)));
    }
    if (escape) {
        std::uint32_t regs[8]{};
        CT::restore_handle(CT::restore_eh_kind, regs); // the EH adapter's handler, as the unwind passes it
        __builtin_longjmp(escape_buffer, 1);
    }
    return fake_al;
}
unsigned frame = 100;
void* inject_reference = nullptr; // the six original prologue bytes + jmp h_record
bool same_cpu(const HCpu& a, const HCpu& b) {
    return a.eax == b.eax && a.ebx == b.ebx && a.ecx == b.ecx && a.edx == b.edx && a.esi == b.esi && a.edi == b.edi &&
           a.ebp == b.ebp && a.esp == b.esp && a.flags == b.flags && a.mxcsr == b.mxcsr && a.fpucw == b.fpucw &&
           a.st0 == b.st0 && ((a.fpusw >> 11) & 7) == ((b.fpusw >> 11) & 7) && !std::memcmp(a.xmm, b.xmm, 128);
}
// One input iteration: a Present (the frame counter), then the injection entry with ECX = controlled object and the
// caller's pushed processing flag (0x0787c8 pushes setne, 0x0787dc pushes 1).
void iteration(std::uint32_t controlled, std::uint32_t flag = 1, bool present = true) {
    if (present) sr::present(++frame);
    h_sentinels(frame & 0xff, (frame & 1) != 0);
    h_in.ecx = controlled;
    DWORD e = 0;
    unsigned char* top = h_stack + sizeof h_stack - 0x400;
    std::memcpy(top + 4, &flag, 4); // [ESP+4] at the entry
    if (!escape) {
        // the whole entry (consumer poll, any engine call, rows) against the six original prologue bytes alone
        const unsigned skew = (frame & 3) * 4;
        std::memcpy(top - skew + 4, &flag, 4);
        const HCpu in = h_in;
        h_run(im.at(R::inject_rva), skew, 0x5150 + frame, &e);
        const HCpu hooked = h_out;
        check(e == 0x5150 + frame, "inject_last_error");
        h_in = in;
        DWORD e2 = 0;
        h_run(reinterpret_cast<std::uintptr_t>(inject_reference), skew, 0x5150 + frame, &e2);
        check(same_cpu(hooked, h_out), "inject_cpu_preserved");
        return;
    }
    if (!__builtin_setjmp(escape_buffer)) {
        h_run(im.at(R::inject_rva), 0, 1, &e);
        check(false, "escape_returned");
    } else {
        // the longjmp left the harness frame mid-call: put the host's FPU/SSE state and the saved frame back
        asm volatile("fninit\n\tcld" ::: "memory");
        const unsigned mxcsr = 0x1f80;
        asm volatile("ldmxcsr %0" ::"m"(mxcsr) : "memory");
    }
    escape = false;
}
// A factor write through the real claimed hook: EAX = argument cells, ESI = task.
void factor_write(std::uint32_t old_factor, std::uint32_t new_factor, std::uint32_t ip, std::uint32_t ret,
                  unsigned skew = 0, bool flags_set = false) {
    set_factor(old_factor);
    set_args(new_factor);
    set_task(ip, ret);
    h_sentinels(0x42 + skew, flags_set);
    h_in.eax = std::uint32_t(reinterpret_cast<std::uintptr_t>(args));
    h_in.esi = std::uint32_t(reinterpret_cast<std::uintptr_t>(task));
    DWORD e = 0;
    h_run(im.at(R::hook_rva), skew, 0x7a7a, &e);
    check(e == 0x7a7a, "hook_last_error");
}
bool has_row(const char* prefix, std::size_t from, const char* also = nullptr) {
    for (std::size_t i = from; i < rows.size(); ++i)
        if (!rows[i].compare(0, std::strlen(prefix), prefix) && (!also || rows[i].find(also) != std::string::npos))
            return true;
    return false;
}
void show_rows(std::size_t from, const char* label) {
    for (std::size_t i = from; i < rows.size(); ++i) std::printf("  [%s] %s\n", label, rows[i].c_str());
}
bool hook_original() {
    return !std::memcmp(im.base + R::hook_rva, im.pristine.data() + R::hook_rva, R::hook_length);
}
bool inject_original() {
    return !std::memcmp(im.base + R::inject_rva, im.pristine.data() + R::inject_rva, 6);
}
bool initialize() {
    SetEnvironmentVariableW(L"X3M_SETA_RESTORE", L"1");
    return sr::initialize();
}
void reset() {
    sr::shutdown();
    sr::fixture_reset();
}
bool map_vm(const char* path, std::uint32_t& vm) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> file;
    unsigned char chunk[65536];
    for (std::size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) file.insert(file.end(), chunk, chunk + n);
    std::fclose(f);
    if (file.size() < 12 || std::memcmp(file.data(), "X3VM", 4)) return false;
    std::uint32_t count = 0;
    std::memcpy(&vm, &file[4], 4);
    std::memcpy(&count, &file[8], 4);
    std::size_t p = 12;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t at = 0, size = 0;
        std::memcpy(&at, &file[p], 4);
        std::memcpy(&size, &file[p + 4], 4);
        p += 8;
        void* m = VirtualAlloc(reinterpret_cast<void*>(std::uintptr_t(at)), size ? size : 1, MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE);
        if (m != reinterpret_cast<void*>(std::uintptr_t(at))) return false;
        std::memcpy(m, &file[p], size);
        p += size;
    }
    return true;
}
// The CPU contract of the claimed factor hook against a reference copy of the live span: every 4-byte skew, flags
// and DF set and clear, a write that neither arms nor cancels (the handler still runs its checked reads).
void test_hook_cpu(void* reference) {
    for (unsigned skew = 0; skew < 16; skew += 4)
        for (int f = 0; f < 2; ++f) {
            factor_write(0x10000, 0x20000, 0x1234, 0x5678, skew, f != 0);
            const HCpu hooked = h_out;
            h_sentinels(0x42 + skew, f != 0);
            h_in.eax = std::uint32_t(reinterpret_cast<std::uintptr_t>(args));
            h_in.esi = std::uint32_t(reinterpret_cast<std::uintptr_t>(task));
            DWORD e = 0;
            h_run(reinterpret_cast<std::uintptr_t>(reference), skew, 0x7a7a, &e);
            const HCpu& ref = h_out;
            char d[256];
            std::snprintf(d, sizeof d, "skew=%u flags=%d eax %08x/%08x ecx %08x/%08x esi %08x/%08x fl %08x/%08x esp %08x/%08x",
                          skew, f, hooked.eax, ref.eax, hooked.ecx, ref.ecx, hooked.esi, ref.esi, hooked.flags, ref.flags,
                          hooked.esp, ref.esp);
            const bool same = hooked.eax == ref.eax && hooked.ebx == ref.ebx && hooked.ecx == ref.ecx &&
                              hooked.edx == ref.edx && hooked.esi == ref.esi && hooked.edi == ref.edi &&
                              hooked.ebp == ref.ebp && hooked.esp == ref.esp && hooked.flags == ref.flags &&
                              hooked.mxcsr == ref.mxcsr && hooked.fpucw == ref.fpucw && hooked.st0 == ref.st0 &&
                              ((hooked.fpusw >> 11) & 7) == ((ref.fpusw >> 11) & 7) &&
                              !std::memcmp(hooked.xmm, ref.xmm, 128);
            check(same, "hook_cpu_preserved", d);
            check(hooked.eax == std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg)) && hooked.ecx == 0x20000,
                  "hook_replay_outputs", d);
        }
}

// The chase family's install rows with every 8-digit address masked (the base differs per process): what the host test
// compares across the init-order modes.
void print_chase_rows() {
    for (const std::string& r : rows) {
        if (r.compare(0, 17, "chase_transition ") && r.compare(0, 19, "chase_view_restore ") &&
            r.compare(0, 22, "chase_transition_site ") && r.compare(0, 24, "chase_view_restore_site "))
            continue;
        std::string m = r;
        for (std::size_t i = 0; (i = m.find("0x", i)) != std::string::npos; i += 2)
            if (i + 10 <= m.size() && std::strspn(m.c_str() + i + 2, "0123456789abcdef") == 8) m.replace(i + 2, 8, "XXXXXXXX");
        std::printf("CHASE_ROW %s\n", m.c_str());
    }
}
// Init-order modes (one process each: chase_transition::initialize runs once per process). chase_only: the chase family
// alone (the reference rows). chase_first: the loader's order -- the chase transition admits its windows and, with the
// view restore requested, claims the shared entry; the SETA consumer then attaches to the live sites. seta_first: an
// out-of-order SETA initialize while the chase camera is wanted but not yet initialized is refused before any write
// (chase_pending), the chase transition then installs exactly as alone, and a SETA initialize after it attaches.
int order_mode(const char* mode) {
    SetEnvironmentVariableW(L"X3M_CHASE_VIEW_RESTORE", L"1");
    x3m::camera_wanted = true;
    const bool seta_first = !std::strcmp(mode, "seta_first");
    if (seta_first) {
        const std::size_t mark = rows.size();
        check(!initialize() && !std::strcmp(sr::state(), "chase_pending") && hook_original() && inject_original(),
              "seta_first_refused_before_any_write", sr::state());
        check(has_row("seta_restore refused reason=chase_pending edition=fl", mark), "chase_pending_row");
        sr::fixture_reset();
    }
    x3m::camera_installed = true;
    x3m::camera_status = "active";
    std::uint32_t failed = 0;
    const char* plan = x3m::fl::chase_transition_fl::plan(
        reinterpret_cast<std::uintptr_t>(im.base),
        [](std::uintptr_t a, void* out, unsigned n) {
            return x3m::engine_patch::read_code(a, static_cast<unsigned char*>(out), n);
        },
        &failed);
    std::printf("CHASE_PLAN_BEFORE_CHASE plan=%s window=%u\n", plan ? plan : "none", failed);
    check(!plan, "chase_plan_admitted", plan ? plan : "");
    CT::initialize();
    check(CT::installed() && CT::restore_installed(), "chase_transition_and_restore_installed");
    if (std::strcmp(mode, "chase_only")) {
        const std::size_t mark = rows.size();
        check(initialize() && !std::strcmp(sr::state(), "ok"), "seta_attaches_after_chase", sr::state());
        check(has_row("seta_restore installed=1 edition=fl", mark, "inject=ok"), "seta_install_row");
        check(CT::inject_chase && CT::inject_consumer.load() != nullptr, "both_consumers_on_the_shared_sites");
        // one entry through the shared site with both consumers attached (the chase ticket idle, the SETA debug filter on)
        check(!std::strcmp(sr::key_latch_enable(true), "ok"), "order_key_latch_enable");
        im.jump(R::inject_rva + 6, reinterpret_cast<void*>(&h_record));
        iteration(ship.va());
        im.restore(R::inject_rva + 6, 5);
    }
    print_chase_rows();
    std::printf("ORDER mode=%s\n", mode);
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    std::uint32_t vm = 0;
    if (argc < 3 || !map_fl_image(argv[1], im) || !map_vm(argv[2], vm)) {
        std::printf("RESULT FAIL cannot map the image or the VM\n");
        return 2;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(im.base);
    std::printf("IMAGE base=0x%08lx relocated=%u vm=0x%08x\n", static_cast<unsigned long>(base), im.relocated, vm);
    check(im.timestamp == 0x618416c8 && im.size == 0x00391000, "fl_identity");
    // runtime slots (not windows): cfg, VM, manager, game root, input context
    im.u32(R::cfg_slot_rva) = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
    im.u32(R::vm_slot_rva) = vm;
    im.u32(R::manager_slot_rva) = std::uint32_t(reinterpret_cast<std::uintptr_t>(manager));
    im.u32(R::game_root_rva) = std::uint32_t(reinterpret_cast<std::uintptr_t>(game));
    im.u32(R::input_ctx_slot_rva) = std::uint32_t(reinterpret_cast<std::uintptr_t>(ctx));
    im.pristine.assign(im.base, im.base + im.size);
    CT::fixture_base_ = base;
    sr::fixture_set_base(base);
    set_object(sector_a, 1, 100);
    set_object(sector_b, 1, 200);
    set_object(ship, 7, 0x1111);
    ship.u32(0x54) = sector_a.va();
    ship.u32(0x40) = 0x80000; // autopilot
    ship.u32(0x94) = 0xfffebb1e;
    ship.u32(0x10) = 500;
    set_player(ship);
    desc.u32(0) = 150;
    context.u32(0) = std::uint32_t(-5);
    context.u32(8) = desc.va();
    void* hook_reference = h_reference(im.base + R::hook_rva, R::hook_length);
    inject_reference = h_reference(im.base + R::inject_rva, 6);
    if (argc > 3) return order_mode(argv[3]);

    // ---- refusals before any write ----
    quiet = true;
    fp::admitted_ = false;
    check(!initialize() && !std::strcmp(sr::state(), "ready_provider_refused") && hook_original() && inject_original(),
          "ready_provider_refused");
    reset();
    fp::admitted_ = true;
    unsigned flips = 0, refused = 0;
    for (unsigned w = 0; w < R::window_count; ++w) {
        const auto& win = R::windows[w];
        auto in_fixup = [&win](unsigned at) {
            for (unsigned f = 0; f < win.fixups; ++f)
                if (at >= win.fixup[f].offset && at < win.fixup[f].offset + 4) return true;
            return false;
        };
        unsigned at = win.length / 2;
        for (unsigned k = 0; k < win.length && in_fixup(at); ++k) at = (at + 1) % win.length;
        const char* want = in_fixup(at) ? "fixup_mismatch" : "window_mismatch";
        im.base[win.rva + at] ^= 0x5a;
        ++flips;
        const bool ok = initialize();
        const bool named = !std::strcmp(sr::state(), want);
        im.restore(win.rva + at, 1);
        const bool clean = !ok && named && hook_original() && inject_original();
        refused += clean;
        if (!clean) std::printf("FLIP_MISSED window=%s state=%s\n", win.name, sr::state());
        reset();
        if (win.fixups) {
            im.u32(win.rva + win.fixup[0].offset) += 4;
            ++flips;
            const bool ok2 = initialize();
            const bool named2 = !std::strcmp(sr::state(), "fixup_mismatch");
            im.restore(win.rva + win.fixup[0].offset, 4);
            const bool clean2 = !ok2 && named2 && hook_original() && inject_original();
            refused += clean2;
            if (!clean2) std::printf("FLIP_MISSED window=%s fixup state=%s\n", win.name, sr::state());
            reset();
        }
    }
    std::printf("FLIPS windows=%u flips=%u refused=%u\n", R::window_count, flips, refused);
    check(flips == refused, "every_flip_refused_before_any_write");
    // a chase_transition identity window that differs: the consumer's admission refuses after the hook was written,
    // and the hook is rolled back
    const auto& chase_inject = x3m::fl::chase_transition_fl::windows[x3m::fl::chase_transition_fl::window_inject];
    im.base[chase_inject.rva + 1] ^= 0x5a;
    check(!initialize() && !std::strcmp(sr::state(), "window_mismatch") && hook_original(), "attach_refusal_rolls_back_hook",
          sr::state());
    im.restore(chase_inject.rva + 1, 1);
    reset();
    sr::fixture_fault_hook();
    check(!initialize() && !std::strcmp(sr::state(), "readback_failed") && hook_original() && inject_original(),
          "hook_readback_fault_rolled_back", sr::state());
    reset();
    quiet = false;

    // ---- the install ----
    std::size_t mark = rows.size();
    check(initialize() && !std::strcmp(sr::state(), "ok"), "install");
    check(has_row("seta_restore installed=1 edition=fl", mark, "write=plain") &&
              has_row("seta_restore installed=1", mark, "inject=ok") &&
              has_row("seta_restore installed=1", mark, "call_rva=0x0bb500 selector=32 key=0x96 method=StartFastForward") &&
              has_row("seta_restore installed=1", mark, "ready=ready"),
          "install_row");
    check(!hook_original() && !inject_original(), "hook_and_inject_claimed");
    // stand-ins at the continuations and the wrapper
    im.jump(R::hook_next_rva, reinterpret_cast<void*>(&h_record));
    im.jump(R::inject_rva + 6, reinterpret_cast<void*>(&h_record));
    im.jump(0x0ba6b5, reinterpret_cast<void*>(&h_record)); // VM clear continuation
    im.jump(0x0bcb15, reinterpret_cast<void*>(&h_record)); // VM load continuation
    im.jump(0x14e80f, reinterpret_cast<void*>(&h_record)); // EH adapter continuation
    im.jump(R::script_call_rva, reinterpret_cast<void*>(&fake_wrapper));
    test_hook_cpu(hook_reference);

    // ---- story detection through the first stop-shaped write (the real FL story in the VM image) ----
    // StopFastForward's IP and StartGateWarp's return as detected: read back from the factor row of a debug latch.
    check(!std::strcmp(sr::key_latch_enable(true), "ok"), "key_latch_enable");
    mark = rows.size();
    factor_write(0x40000, 0x10000, 0x18d39, 0x199d6); // the clean FL story's stop IP / gate return (seta.md section 2)
    check(has_row("seta_factor edition=fl", mark, "ip_name=StopFastForward") &&
              has_row("seta_factor edition=fl", mark, "ret_name=gate"),
          "story_detected_fl");
    stop_ip = 0x18d39;
    gate_return = 0x199d6;
    toggle_return = 0x18d59;
    check(has_row("seta_restore armed edition=fl", mark), "gate_armed");
    // ---- Ready / defer / due / call ----
    fp::found_ = 0;
    mark = rows.size();
    const unsigned walks0 = fp::walks_;
    for (int i = 0; i < 3; ++i) iteration(ship.va());
    check(calls.empty() && fp::walks_ == walks0 + 3, "ready_defers_one_walk_per_frame");
    iteration(ship.va(), 1, false); // no Present: no second walk this frame
    check(fp::walks_ == walks0 + 3, "one_walk_per_presented_frame");
    fp::found_ = 1;
    fp::node_ = sector_b.va(); // Ready, but the ship is still parented to the departure sector: not due
    iteration(ship.va());
    check(calls.empty(), "ready_other_sector_not_due");
    ship.u32(0x54) = sector_b.va();
    iteration(ship.va()); // due now
    iteration(ship.va()); // frame + 1
    check(calls.empty(), "two_presented_frames");
    iteration(sector_a.va(), 0); // frame + 2, but the engine controls another object (caller 0x0787c8, flag 0): wait
    check(calls.empty(), "remote_view_waits");
    iteration(ship.va(), 0); // the controlled ship: StartFastForward
    check(calls.size() == 1 && calls[0].selector == 32 && calls[0].token == 0 && calls[0].key == 0x96 &&
              !std::strcmp(calls[0].name, "StartFastForward") && calls[0].kind == 1 && calls[0].argc == 0,
          "start_call_selector32", calls.empty() ? "none" : calls[0].name);
    // accepted dispatch without an observed factor write (the stand-in writes none): the AP policy's restored row
    check(has_row("seta_restore restored edition=fl", mark, "result=1"), "accepted_without_factor_restored");
    if (failures) show_rows(mark, "ready");

    // ---- missing method: AL 0 -> restore_failed ----
    calls.clear();
    ship.u32(0x54) = sector_a.va();
    sector_a.u32(8) = 101; // a new transit (one arm per departure sector)
    factor_write(0x40000, 0x10000, stop_ip, gate_return);
    ship.u32(0x54) = sector_b.va();
    fake_al = 0;
    mark = rows.size();
    for (int i = 0; i < 4; ++i) iteration(ship.va());
    check(calls.size() == 1 && has_row("seta_restore restore_failed edition=fl", mark, "result=0"), "missing_method_refused");
    fake_al = 1;

    // ---- cancels: factor write, VM clear, VM load, EH epoch, ship change ----
    struct CancelCase {
        const char* name;
        std::uint32_t site; // 0: a factor write; 1: ship change
    } cancels[] = {{"factor_write", 0}, {"vm_clear", 0x0ba6b0}, {"vm_load", 0x0bcb10}, {"epoch", 0x14e80a}, {"ship_change", 1}};
    std::uint32_t transit = 300;
    for (const auto& c : cancels) {
        ship.u32(0x54) = sector_a.va();
        sector_a.u32(8) = ++transit;
        fp::found_ = 0;
        factor_write(0x40000, 0x10000, stop_ip, gate_return);
        mark = rows.size();
        if (c.site == 0) {
            factor_write(0x10000, 0x30000, 0x1000, 0x2000);
        } else if (c.site == 1) {
            set_object(ship2, 7, 0x2222);
            ship2.u32(0x94) = 0x12345678; // another script id: no re-bind
            ship2.u32(0x54) = sector_b.va();
            set_player(ship2);
            iteration(ship.va());
            set_player(ship);
        } else {
            h_sentinels(7, true);
            DWORD e = 0;
            h_run(im.at(c.site), 4, 0x31, &e);
            check(e == 0x31, "cancel_site_last_error");
            iteration(ship.va());
        }
        char want[64];
        std::snprintf(want, sizeof want, "reason=%s ", c.name);
        check(has_row("seta_restore cancelled edition=fl", mark, want), "cancel", c.name);
    }
    // ---- player rebind: one change to a live ship with the arm's script id in a live sector other than the departure ----
    ship.u32(0x54) = sector_a.va();
    sector_a.u32(8) = ++transit;
    fp::found_ = 0;
    calls.clear();
    factor_write(0x40000, 0x10000, stop_ip, gate_return);
    set_object(ship2, 7, 0x2222);
    ship2.u32(0x94) = 0xfffebb1e;
    ship2.u32(0x54) = 0; // the new ship's sector not live yet: rebind_pending (defers)
    set_player(ship2);
    mark = rows.size();
    iteration(ship.va());
    check(has_row("seta_restore rebind_pending edition=fl", mark), "rebind_pending");
    ship2.u32(0x54) = sector_b.va();
    iteration(ship2.va());
    check(has_row("seta_restore rebound edition=fl", mark, "ship_check=rebound"), "rebound");
    fp::found_ = 1;
    fp::node_ = sector_b.va();
    for (int i = 0; i < 4; ++i) iteration(ship2.va());
    check(calls.size() == 1 && has_row("seta_restore restored edition=fl", mark), "rebound_ticket_restores");
    set_player(ship);
    if (failures) show_rows(mark, "rebind");

    // ---- the key latch: press (held 3 entries), release, 8-entry tail; a re-delivered toggle inside the call ----
    fp::found_ = 0;
    calls.clear();
    unsigned char& counter = im.base[R::seta_counter_rva];
    counter = 0;
    mark = rows.size();
    factor_write(0x40000, 0x10000, stop_ip, toggle_return); // the key's own stop through ToggleFastForward
    check(has_row("seta_key_latch armed edition=fl", mark), "latch_armed");
    counter = 1;
    nested_write = 1; // the first call's StopFastForward writes 1.0 over a restarted SETA: a fire
    iteration(ship.va());
    nested_write = 0;
    iteration(ship.va());
    iteration(ship.va());
    counter = 0;
    for (int i = 0; i < 8; ++i) iteration(ship.va());
    unsigned stops = 0;
    for (const Call& c : calls) stops += !std::strcmp(c.name, "StopFastForward") && c.selector == 32 && c.token == 0;
    char d[96];
    std::snprintf(d, sizeof d, "calls=%u stops=%u", unsigned(calls.size()), stops);
    check(calls.size() == 11 && stops == 11, "latch_held_plus_tail_calls", d);
    check(has_row("seta_key_latch fire edition=fl", mark), "latch_nested_fire");
    check(has_row("seta_key_latch released edition=fl reason=released", mark, "held=3 zeros=8 calls=11"), "latch_tail_release");
    iteration(ship.va());
    check(calls.size() == 11, "latch_idle_after_release");
    if (failures) show_rows(mark, "latch");
    // ramp guard: a stop right after a run's first ramp step (old 1.0 -> new > 1.0) is that press's re-delivery: not armed
    mark = rows.size();
    factor_write(0x10000, 0x18000, 0x16a43, 0x1000); // Vbi's first ramp step
    factor_write(0x40000, 0x10000, stop_ip, toggle_return);
    check(!has_row("seta_key_latch armed", mark), "ramp_guard_not_armed");
    for (int i = 0; i < 6; ++i) iteration(ship.va()); // past the guard
    // a new press after a release inside the window, and a VM clear release
    factor_write(0x40000, 0x10000, stop_ip, toggle_return);
    check(has_row("seta_key_latch armed edition=fl", mark), "latch_armed_after_guard");
    counter = 1;
    iteration(ship.va());
    counter = 0;
    iteration(ship.va());
    iteration(ship.va());
    counter = 1;
    iteration(ship.va()); // a rise after zeros: a new press, released
    check(has_row("seta_key_latch released edition=fl reason=new_press", mark), "latch_new_press");
    counter = 0;
    factor_write(0x40000, 0x10000, stop_ip, toggle_return);
    {
        h_sentinels(9, false);
        DWORD e = 0;
        h_run(im.at(0x0ba6b0), 0, 1, &e);
    }
    check(has_row("seta_key_latch released edition=fl reason=vm", mark), "latch_vm_release");
    // escaped dispatch: the latch's call unwinds (the EH adapter's handler, then a longjmp past the consumer); the next
    // entry finds the call unreported and turns the latch off for the session
    sr::key_latch_enable(true);
    for (int i = 0; i < 6; ++i) iteration(ship.va());
    mark = rows.size();
    factor_write(0x40000, 0x10000, stop_ip, toggle_return);
    counter = 1;
    const std::size_t before_escape = calls.size();
    escape = true;
    iteration(ship.va());
    check(calls.size() == before_escape + 1, "escape_call_made");
    iteration(ship.va());
    check(has_row("seta_key_latch refused edition=fl reason=call_unwound", mark), "escaped_dispatch_cancels_latch");
    iteration(ship.va());
    check(calls.size() == before_escape + 1, "latch_off_after_unwind");
    // an escaped gate-ticket call: cancel call_unwound
    counter = 0;
    ship.u32(0x54) = sector_a.va();
    sector_a.u32(8) = ++transit;
    fp::found_ = 1;
    fp::node_ = sector_b.va();
    factor_write(0x40000, 0x10000, stop_ip, gate_return);
    ship.u32(0x54) = sector_b.va();
    mark = rows.size();
    iteration(ship.va());
    iteration(ship.va());
    escape = true;
    iteration(ship.va());
    iteration(ship.va());
    check(has_row("seta_restore cancelled edition=fl reason=call_unwound", mark) ||
              has_row("seta_restore cancelled edition=fl reason=epoch", mark),
          "escaped_ticket_call_cancelled");
    if (failures) show_rows(mark, "escape");
    // AL 0 for the latch's StopFastForward: the latch turns off (call_unresolved)
    sr::key_latch_enable(true);
    for (int i = 0; i < 6; ++i) iteration(ship.va());
    mark = rows.size();
    factor_write(0x40000, 0x10000, stop_ip, toggle_return);
    counter = 1;
    fake_al = 0;
    iteration(ship.va());
    fake_al = 1;
    check(has_row("seta_key_latch refused edition=fl reason=call_unresolved", mark), "latch_call_unresolved_off");
    counter = 0;

    // ---- late window ----
    for (std::uint32_t rva : {R::hook_next_rva, R::inject_rva + 6, 0x0ba6b5u, 0x0bcb15u, 0x14e80fu, R::script_call_rva})
        im.restore(rva, 5);
    reset();
    check(hook_original() && inject_original(), "shutdown_restores");
    x3m::engine_patch::close_install_window("fixture_present");
    check(!initialize() && !std::strcmp(sr::state(), "late_claim") && hook_original() && inject_original(),
          "late_claim_refused");
    std::printf("CALLS total_harness_runs=%u\n", h_calls);
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
