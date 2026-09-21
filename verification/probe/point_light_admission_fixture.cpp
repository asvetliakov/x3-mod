// X3 CPU fixture of the point-light root-admission patch: a synthetic emitter
// of the engine's comparison window (the exact bytes of 0x004c27a1..0x004c27bc
// with the reject target at the same +0x240 displacement), synthetic node/light
// records, and the production module (src/proxy/point_light_admission.cpp,
// compiled separately with its production flags) patching that copy.
// Checks: per-node admit unchanged, per-node reject with an admitting root,
// root reject, null parent, cycle and over-bound chains, unreadable parent
// and root (fail closed), LastError preservation, live-register/ESP-local/x87
// preservation on both paths, exact rollback, option-off untouched, late window.
// Diagnostic timings only; not game FPS. Never launches the game.
#include "../../src/proxy/point_light_admission.h"
#include "../../src/proxy/point_light_admission_core.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/engine_memory.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdarg>
// Telemetry lines are kept for the frame-line checks; the first install lines are printed for the runner.
static char frame_lines[8][512]; static unsigned frame_line_count = 0;
static char node_lines[80][512]; static unsigned node_line_count = 0;
namespace x3m { void log(const char* format, ...) {
    char text[512]; std::va_list a; va_start(a, format); std::vsnprintf(text, sizeof text, format, a); va_end(a);
    if (!std::strncmp(text, "point_light_admission_frame ", 28)) { if (frame_line_count < 8) std::strcpy(frame_lines[frame_line_count++], text); return; }
    if (!std::strncmp(text, "point_light_node ", 17)) { if (node_line_count < 80) std::strcpy(node_lines[node_line_count++], text); return; }
    static unsigned lines = 0; if (lines++ < 8) std::printf("%s\n", text);
} }
namespace x3m::object_trace { bool executable_verified() { return true; } }
namespace pla = x3m::point_light_admission;
namespace core = x3m::point_light_admission::core;

// ---- harness: a four-byte-aligned caller frame around the synthetic site ----
extern "C" {
struct Frame {
    std::uint32_t eax, ecx, edx, ebx, ebp, esi, edi, flags; // in
    std::uint32_t out[9];        // PUSHAD order edi,esi,ebp,esp,ebx,edx,ecx,eax then EFLAGS, at the site's return
    std::uint32_t entry_esp, exit_esp;
    std::uint32_t locals[24];    // the 0x60 bytes of ESP-relative locals below the return address, after the call
    std::uint32_t x87env[7];     // fnstenv after the call: status (TOP) and tag word
};
Frame* fixture_frame = nullptr; std::uint32_t fixture_site = 0;
void fixture_run(Frame* frame);
}
static_assert(offsetof(Frame, out) == 32 && offsetof(Frame, entry_esp) == 68 && offsetof(Frame, exit_esp) == 72 && offsetof(Frame, locals) == 76 && offsetof(Frame, x87env) == 172, "frame layout");
asm(".text\n.globl _fixture_run\n_fixture_run:\n"
    "pushl %ebp\n movl %esp,%ebp\n pushl %ebx\n pushl %esi\n pushl %edi\n"
    "movl 8(%ebp),%eax\n movl %eax,_fixture_frame\n movl %esp,68(%eax)\n"
    "subl $96,%esp\n movl %esp,%edi\n movl $24,%ecx\n movl $0xa5a5a5a5,%eax\n cld\n rep stosl\n"
    "fninit\n movl _fixture_frame,%edx\n pushl 28(%edx)\n popfl\n"
    "movl 0(%edx),%eax\n movl 4(%edx),%ecx\n movl 12(%edx),%ebx\n movl 20(%edx),%esi\n movl 24(%edx),%edi\n movl 16(%edx),%ebp\n movl 8(%edx),%edx\n"
    "call *_fixture_site\n"
    "pushfl\n pushal\n movl _fixture_frame,%ebx\n fnstenv 172(%ebx)\n"
    "movl %esp,%esi\n leal 32(%ebx),%edi\n movl $9,%ecx\n cld\n rep movsl\n"
    "leal 36(%esp),%esi\n leal 76(%ebx),%edi\n movl $24,%ecx\n rep movsl\n"
    "addl $132,%esp\n movl %esp,72(%ebx)\n"
    "popl %edi\n popl %esi\n popl %ebx\n popl %ebp\n ret\n");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) { ++checks; if (!okay) { ++failures; std::printf("FAIL %s\n", label); } }

// ---- synthetic site: the engine window at +0, reject target at +0x254 ----
static unsigned char* code = nullptr;
static std::uint32_t site_va() { return std::uint32_t(reinterpret_cast<std::uintptr_t>(code) + core::site_offset); }
static void build_site() {
    code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    std::memset(code, 0xcc, 0x1000);
    std::memcpy(code, core::expected_window, core::window_length);
    // admit continuation after `mov esi,[eax+0x16c]`: mov ecx,1; ret
    static const unsigned char admit_tail[] = {0xb9, 0x01, 0x00, 0x00, 0x00, 0xc3};
    std::memcpy(code + core::window_length, admit_tail, sizeof admit_tail);
    // reject target: the engine's first instruction (mov eax,[esp+0x5c]); the engine's following
    // mov edx,[0x00608518] would read game memory, so the copy continues with mov ecx,2; ret
    unsigned char* reject = code + core::site_offset + core::site_length + core::site_rel32;
    std::memcpy(reject, core::expected_reject_prefix, core::reject_prefix_length);
    static const unsigned char reject_tail[] = {0xb9, 0x02, 0x00, 0x00, 0x00, 0xc3};
    std::memcpy(reject + core::reject_prefix_length, reject_tail, sizeof reject_tail);
    FlushInstructionCache(GetCurrentProcess(), code, 0x1000);
}
static bool site_bytes_are(const unsigned char* expected, unsigned n) { return !std::memcmp(code + core::site_offset, expected, n); }
static bool window_untouched_around_site() {
    return !std::memcmp(code, core::expected_window, core::site_offset) && !std::memcmp(code + core::site_offset + core::site_length, core::expected_window + core::site_offset + core::site_length, core::window_length - core::site_offset - core::site_length);
}

// ---- synthetic records ----
struct alignas(16) Node { unsigned char bytes[0x200]; };
static Node pool[16];
static std::uint32_t frame_block[8];
static std::uint32_t addr(const void* p) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(p)); }
static void put(Node& n, unsigned off, std::uint32_t v) { std::memcpy(n.bytes + off, &v, 4); }
static void node_set(Node& n, std::uint32_t parent, std::int32_t scale, std::int32_t x, std::int32_t y, std::int32_t z) {
    std::memset(n.bytes, 0, sizeof n.bytes); put(n, core::parent_offset, parent); put(n, core::scale_offset, std::uint32_t(scale));
    put(n, core::position_offset, std::uint32_t(x)); put(n, core::position_offset + 4, std::uint32_t(y)); put(n, core::position_offset + 8, std::uint32_t(z));
}
static void light_set(Node& l, std::int32_t range, std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t record) {
    node_set(l, 0, 0, x, y, z); put(l, core::range_offset, std::uint32_t(range)); put(l, 0x16c, record);
}
constexpr std::uint32_t k_ebx = 0x0b0b0b0b, k_edi = 0x0d0d0d0d, k_ecx = 0xcccccccc, k_edx = 0xdddddddd, k_record = 0x5ec0d0ad;
enum Path : std::uint32_t { admit = 1, reject = 2 };
struct Result { std::uint32_t path, eax, esi; bool preserved, x87_empty; };
// Every scenario is a new frame unless it tests the memo: the records are
// rewritten between runs, so a same-frame verdict would be stale by design.
static Result run(std::int32_t distance, Node& node, Node& light, std::uint32_t flags = 0x246, bool new_frame = true) {
    if (new_frame) pla::next_frame();
    Frame f{}; std::memset(&f, 0, sizeof f);
    frame_block[3] = addr(&node); // [EBP+0xc]
    f.eax = std::uint32_t(distance); f.ecx = k_ecx; f.edx = k_edx; f.ebx = k_ebx; f.ebp = addr(frame_block); f.esi = addr(&light); f.edi = k_edi; f.flags = flags;
    fixture_site = addr(code); fixture_run(&f);
    Result r{}; r.path = f.out[6]; r.eax = f.out[7]; r.esi = f.out[1];
    bool locals_ok = true; for (std::uint32_t w : f.locals) locals_ok = locals_ok && w == 0xa5a5a5a5u;
    // PUSHAD stores the ESP value before PUSHAD, i.e. after the PUSHFD word: entry_esp - 96 (locals) - 4.
    r.preserved = f.out[0] == k_edi && f.out[2] == addr(frame_block) && f.out[4] == k_ebx && f.out[3] == f.entry_esp - 100 && f.exit_esp == f.entry_esp && locals_ok;
    r.x87_empty = ((f.x87env[1] >> 11) & 7) == 0 && (f.x87env[2] & 0xffff) == 0xffff; // TOP = 0, all tags empty
    if (!r.preserved || !r.x87_empty)
        std::printf("DETAIL edi=%08lx ebp=%08lx ebx=%08lx esp=%08lx entry_esp=%08lx exit_esp=%08lx locals_ok=%u status=%04lx tag=%04lx\n",
                    (unsigned long)f.out[0], (unsigned long)f.out[2], (unsigned long)f.out[4], (unsigned long)f.out[3], (unsigned long)f.entry_esp, (unsigned long)f.exit_esp,
                    locals_ok ? 1u : 0u, (unsigned long)(f.x87env[1] & 0xffff), (unsigned long)(f.x87env[2] & 0xffff));
    return r;
}
static void check_path(const Result& r, Path expected, Node& light, const char* label) {
    check(r.path == expected, label);
    check(r.preserved && r.x87_empty, "live registers, ESP locals and empty x87 stack preserved");
    if (expected == admit) check(r.eax == addr(&light) && r.esi == k_record, "admit path: eax=light, esi=[light+0x16c]");
    else check(r.eax == 0xa5a5a5a5u && r.esi == addr(&light), "reject path: eax=[esp+0x5c], esi=light");
}
static bool same(const Result& a, const Result& b) { return a.path == b.path && a.eax == b.eax && a.esi == b.esi && a.preserved == b.preserved && a.x87_empty == b.x87_empty; }

static double bench_us(std::int32_t distance, Node& node, Node& light, bool new_frame = true) {
    LARGE_INTEGER f{}, s{}, e{}; QueryPerformanceFrequency(&f);
    constexpr unsigned loops = 20000;
    for (unsigned i = 0; i < 256; ++i) run(distance, node, light, 0x246, new_frame);
    QueryPerformanceCounter(&s); for (unsigned i = 0; i < loops; ++i) run(distance, node, light, 0x246, new_frame); QueryPerformanceCounter(&e);
    return double(e.QuadPart - s.QuadPart) * 1e6 / double(f.QuadPart) / double(loops);
}

int main() {
    build_site();
    Node& light = pool[0]; Node& node = pool[1]; Node& root = pool[2]; Node& mid = pool[3];
    light_set(light, 1000, 0, 0, 0, k_record);
    // run-22 geometry: a clamp node of base scale 200 at 1232 units from the headlight (rejected natively), its root
    // (the station body, scale 19536) at 3461 units; the 16 km outpost root (scale 500) at 81192.
    node_set(root, 0, 19536, 3461, 0, 0);
    node_set(node, addr(&root), 200, 1232, 0, 0);
    const Result native_admit = run(900, node, light), native_reject = run(1232, node, light);
    check_path(native_admit, admit, light, "native: per-node admit");
    check_path(native_reject, reject, light, "native: per-node reject");

    // option off: nothing patched, nothing written
    SetEnvironmentVariableW(L"X3M_POINT_LIGHT_ROOT_ADMISSION", nullptr);
    check(!pla::initialize() && !std::strcmp(pla::state(), "disabled") && site_bytes_are(core::expected_site, core::site_length), "unset variable: disabled, site untouched");
    SetEnvironmentVariableW(L"X3M_POINT_LIGHT_ROOT_ADMISSION", L"0");
    check(!pla::initialize() && !std::strcmp(pla::state(), "disabled"), "X3M_POINT_LIGHT_ROOT_ADMISSION=0: disabled");
    SetEnvironmentVariableW(L"X3M_POINT_LIGHT_ROOT_ADMISSION", L"1");
    check(!pla::initialize() && !std::strcmp(pla::state(), "bytes_mismatch") && pla::detour_address() == 0, "engine site absent in this process: bytes_mismatch, nothing patched");
    // corrupted synthetic window refused
    code[core::site_offset + 1] ^= 1;
    check(!pla::install_at(site_va()) && !std::strcmp(pla::state(), "bytes_mismatch"), "changed site byte: bytes_mismatch");
    code[core::site_offset + 1] ^= 1;
    unsigned char* reject_at = code + core::site_offset + core::site_length + core::site_rel32;
    reject_at[2] ^= 1;
    check(!pla::install_at(site_va()) && !std::strcmp(pla::state(), "bytes_mismatch") && site_bytes_are(core::expected_site, core::site_length), "changed reject-target byte: bytes_mismatch, site untouched");
    reject_at[2] ^= 1;

    // install
    check(pla::install_at(site_va()) && !std::strcmp(pla::state(), "ok"), "install_at synthetic site");
    if (!pla::detour_address()) { std::printf("FAIL install state=%s\nPOINT LIGHT ADMISSION CPU checks=%u failures=%u\n", pla::state(), checks, failures + 1); return 1; }
    unsigned char patched[core::site_length]; core::encode_site_patch(site_va(), std::uint32_t(pla::detour_address()), patched);
    check(site_bytes_are(patched, core::site_length) && patched[0] == 0xe9 && patched[5] == 0x90 && window_untouched_around_site(), "site bytes are jmp detour; nop and the rest of the window is untouched");
    check(!std::strcmp(pla::write_path(), "plain") || !std::strcmp(pla::write_path(), "atomic"), "write path reported");
    unsigned char detour_expected[core::detour_length]; core::encode_detour(std::uint32_t(pla::detour_address()), addr(reinterpret_cast<const void*>(&x3m_point_light_root_admits)), site_va() + 6, site_va() + 6 + core::site_rel32, addr(const_cast<std::uint32_t*>(&x3m_point_light_fast_admit)), detour_expected);
    check(!std::memcmp(reinterpret_cast<const void*>(pla::detour_address()), detour_expected, core::detour_length), "detour bytes as encoded");
    check(!pla::install_at(site_va()) && !std::strcmp(pla::state(), "already_installed"), "second install refused");

    // per-node admit unchanged (fast path), every flag pattern the TEST can leave behind
    check(same(run(900, node, light), native_admit), "patched: per-node admit identical to native");
    check_path(run(1200, node, light, 0x2c6), admit, light, "patched: distance == range + scale admits (ZF=1)");
    check_path(run(1199, node, light, 0x2c7), admit, light, "patched: distance below range + scale admits (SF=1)");
    // per-node reject, root admits (run-22 clamp: 3461 <= 1000 + 19536)
    check_path(run(1232, node, light), admit, light, "patched: rejected node admitted through its root");
    check_path(run(0x7fffffff, node, light), admit, light, "patched: far node with an admitting root");
    // root rejects: the outpost (81192 > 1000 + 500)
    node_set(root, 0, 500, 81192, 0, 0);
    check_path(run(81192, node, light), reject, light, "patched: root out of range rejects");
    node_set(root, 0, 500, 0, 1500, 0);
    check_path(run(81192, node, light), admit, light, "patched: root exactly at range + scale admits");
    node_set(root, 0, 500, 0, 1501, 0);
    check_path(run(81192, node, light), reject, light, "patched: root one unit beyond rejects");
    node_set(root, 0, -2000, 0, 0, 0);
    check_path(run(81192, node, light), reject, light, "patched: negative reach rejects");
    node_set(root, 0, 100, std::int32_t(0x80000000), 0x7fffffff, std::int32_t(0x80000000));
    check_path(run(81192, node, light), reject, light, "patched: extreme coordinates reject without fault");
    node_set(root, 0, 19536, 3461, 0, 0);
    // null parent: the node is its own root
    node_set(node, 0, 200, 1232, 0, 0);
    check_path(run(1232, node, light), reject, light, "patched: root node rejected natively stays rejected");
    // chain depth: 7 ancestors resolve, 8 do not
    {
        Node* chain[9] = {&pool[4], &pool[5], &pool[6], &pool[7], &pool[8], &pool[9], &pool[10], &pool[11], &pool[12]};
        for (unsigned depth = 1; depth <= 8; ++depth) {
            node_set(*chain[depth], 0, 19536, 3461, 0, 0);                               // the root
            for (unsigned i = depth; i-- > 1;) node_set(*chain[i], addr(chain[i + 1]), 200, 5000, 0, 0);
            node_set(node, addr(chain[1]), 200, 1232, 0, 0);
            const Result r = run(1232, node, light);
            check_path(r, depth <= core::max_hops - 1 ? admit : reject, light, depth <= core::max_hops - 1 ? "patched: chain within the hop bound admits" : "patched: chain beyond the hop bound rejects");
        }
    }
    // cycles
    node_set(mid, addr(&node), 200, 5000, 0, 0); node_set(node, addr(&mid), 200, 1232, 0, 0);
    check_path(run(1232, node, light), reject, light, "patched: two-node cycle rejects");
    node_set(node, addr(&node), 200, 1232, 0, 0);
    check_path(run(1232, node, light), reject, light, "patched: self-parent rejects");
    node_set(mid, addr(&mid), 200, 5000, 0, 0); node_set(node, addr(&mid), 200, 1232, 0, 0);
    check_path(run(1232, node, light), reject, light, "patched: parent's self-loop rejects");
    // unreadable parents: reserved (uncommitted) page, kernel-range address, and a root whose fields cross into it
    void* reserved = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
    check(reserved != nullptr, "reserved page");
    node_set(node, addr(reserved), 200, 1232, 0, 0);
    x3m::engine_memory::next_frame();
    check_path(run(1232, node, light), reject, light, "patched: parent on an uncommitted page rejects");
    SetLastError(0x5150);
    node_set(node, 0xfffff000u, 200, 1232, 0, 0);
    check_path(run(1232, node, light), reject, light, "patched: parent in the kernel range rejects");
    check(GetLastError() == 0x5150, "LastError preserved across a failed validation");
    {
        void* page = VirtualAlloc(nullptr, 0x2000, MEM_RESERVE, PAGE_NOACCESS);
        void* committed = VirtualAlloc(page, 0x1000, MEM_COMMIT, PAGE_READWRITE);
        check(committed == page, "half-committed pair");
        auto* edge = static_cast<unsigned char*>(page) + 0x1000 - 0x40; // +0x18 readable, +0x70 and +0xb0 are on the uncommitted page
        std::memset(edge, 0, 0x40);
        node_set(node, addr(edge), 200, 1232, 0, 0);
        x3m::engine_memory::next_frame();
        check_path(run(1232, node, light), reject, light, "patched: root with unreadable scale/position rejects");
    }
    node_set(node, addr(&root), 200, 1232, 0, 0);
    x3m::engine_memory::next_frame();
    check_path(run(1232, node, light), admit, light, "patched: readable chain admits again after the unreadable cases");
    const pla::Stats s = pla::stats();
    std::printf("POINT LIGHT ADMISSION OUTCOMES admitted=%lu node_is_root=%lu chain_unreadable=%lu chain_too_deep=%lu chain_cycle=%lu root_unreadable=%lu light_unreadable=%lu reach_negative=%lu root_rejected=%lu\n",
                (unsigned long)s.outcomes[0], (unsigned long)s.outcomes[1], (unsigned long)s.outcomes[2], (unsigned long)s.outcomes[3], (unsigned long)s.outcomes[4], (unsigned long)s.outcomes[5], (unsigned long)s.outcomes[6], (unsigned long)s.outcomes[7], (unsigned long)s.outcomes[8]);
    check(s.outcomes[0] == 11 && s.outcomes[1] == 1 && s.outcomes[2] == 2 && s.outcomes[3] == 1 && s.outcomes[4] == 3 && s.outcomes[5] == 1 && s.outcomes[6] == 0 && s.outcomes[7] == 1 && s.outcomes[8] == 3, "outcome counters");

    check(s.walks == 23 && s.memo_hits == 0, "every scenario so far walked once (new frame each)");
    // memo: the second call for the same (node, light) in the same frame answers from the memo without a walk;
    // a different node or light walks; a new frame walks again; a memoised verdict reflects the root state at walk time
    {
        Node& other = pool[13]; node_set(other, addr(&root), 200, 1232, 0, 0);
        const pla::Stats before = pla::stats();
        check_path(run(1232, node, light, 0x246, false), admit, light, "memo: same frame, same (node, light) admits");
        check_path(run(1300, node, light, 0x246, false), admit, light, "memo: same frame, different distance, same verdict");
        pla::Stats now = pla::stats();
        check(now.walks == before.walks && now.memo_hits == before.memo_hits + 2, "memo: two hits, no walk");
        check_path(run(1232, other, light, 0x246, false), admit, light, "memo: another node in the same frame walks");
        now = pla::stats(); check(now.walks == before.walks + 1 && now.memo_hits == before.memo_hits + 2, "memo: keyed by node");
        Node& light2 = pool[14]; light_set(light2, 1000, 0, 0, 0, k_record);
        check_path(run(1232, node, light2, 0x246, false), admit, light2, "memo: another light in the same frame walks");
        now = pla::stats(); check(now.walks == before.walks + 2, "memo: keyed by light");
        node_set(root, 0, 500, 81192, 0, 0); // the root moves out of range: the same frame still answers the memoised admit
        check_path(run(1232, node, light, 0x246, false), admit, light, "memo: verdict held for the frame");
        check_path(run(1232, node, light), reject, light, "memo: a new frame re-walks and sees the moved root");
        now = pla::stats(); check(now.walks == before.walks + 3 && now.memo_hits == before.memo_hits + 3, "memo: new frame walked");
        check_path(run(1232, node, light, 0x246, false), reject, light, "memo: rejection memoised too");
        node_set(root, 0, 19536, 3461, 0, 0);
        check_path(run(1232, node, light), admit, light, "memo: next frame admits again");
        now = pla::stats(); check(now.walks == before.walks + 4 && now.memo_hits == before.memo_hits + 4, "memo: counters");
        check(now.frame > before.frame, "memo: frame serial advanced");
    }
    // frame telemetry: counters since the last present(), the frame line's sums, the capture-frame samples, the reset
    {
        pla::present(7, 41, false); // drains everything above; not a capture frame: no node lines
        check(frame_line_count == 1 && node_line_count == 0, "present logs one frame line, no samples on a plain frame");
        frame_line_count = 0;
        pla::Stats z = pla::stats();
        check(z.tests == 0 && z.fast_admit == 0 && z.walks == 0 && z.memo_hits == 0 && z.outcomes[0] == 0 && z.outcomes[8] == 0 && z.samples == 0, "present resets the counters");
        pla::begin_frame(true); // capture frame: sample the walked nodes
        Node& other = pool[13]; node_set(other, addr(&root), 200, 1232, 0, 0);
        Node& lonely = pool[15]; node_set(lonely, 0, 300, 5000, 0, 0);
        check_path(run(900, node, light, 0x246, false), admit, light, "telemetry: fast admit 1");
        check_path(run(1200, node, light, 0x246, false), admit, light, "telemetry: fast admit 2");
        check_path(run(-7, other, light, 0x246, false), admit, light, "telemetry: fast admit 3");
        check_path(run(1232, node, light, 0x246, false), admit, light, "telemetry: walk 1 (root admits)");
        check_path(run(1300, other, light, 0x246, false), admit, light, "telemetry: walk 2 (root admits, other node)");
        check_path(run(5000, lonely, light, 0x246, false), reject, light, "telemetry: walk 3 (node is root)");
        check_path(run(1232, node, light, 0x246, false), admit, light, "telemetry: memo hit");
        z = pla::stats();
        check(z.tests == 7 && z.fast_admit == 3 && z.reject == 4 && z.walks == 3 && z.memo_hits == 1 && z.samples == 3, "telemetry: live counters");
        pla::present(7, 42, true);
        check(frame_line_count == 1 && node_line_count == 3, "capture frame: one frame line and three node lines");
        unsigned long device = 0, frame = 0, tests = 0, fast = 0, rej = 0, walks = 0, hits = 0, ra = 0, rr = 0, cu = 0, cd = 0, cc = 0, nr = 0, ru = 0, lu = 0, rn = 0, smp = 0;
        const int fields = std::sscanf(frame_lines[0], "point_light_admission_frame device=%lu frame=%lu tests=%lu fast_admit=%lu reject=%lu walks=%lu memo_hits=%lu root_admit=%lu root_reject=%lu chain_unreadable=%lu chain_too_deep=%lu chain_cycle=%lu node_is_root=%lu root_unreadable=%lu light_unreadable=%lu reach_negative=%lu samples=%lu",
                                       &device, &frame, &tests, &fast, &rej, &walks, &hits, &ra, &rr, &cu, &cd, &cc, &nr, &ru, &lu, &rn, &smp);
        check(fields == 17 && device == 7 && frame == 42, "frame line parses");
        check(tests == fast + rej && rej == walks + hits && walks == ra + rr + cu + cd + cc + nr + ru + lu + rn, "frame line sums");
        check(tests == 7 && fast == 3 && rej == 4 && walks == 3 && hits == 1 && ra == 2 && nr == 1 && rr == 0 && smp == 3, "frame line values");
        unsigned long n_node = 0, n_root = 0; unsigned depth = 0; long dist = 0, reach = 0, rdist = 0, rreach = 0, nscale = 0, rscale = 0; char verdict[32] = {};
        const int nf = std::sscanf(node_lines[0], "point_light_node device=%lu frame=%lu node=%lx root=%lx depth=%u dist=%ld reach=%ld root_dist=%ld root_reach=%ld verdict=%31s node_scale=%ld root_scale=%ld",
                                   &device, &frame, &n_node, &n_root, &depth, &dist, &reach, &rdist, &rreach, verdict, &nscale, &rscale);
        check(nf == 12 && n_node == addr(&node) && n_root == addr(&root) && depth == 1 && dist == 1232 && reach == 1200 && rdist == 3461 && rreach == 20536 && !std::strcmp(verdict, "root_admit") && nscale == 200 && rscale == 19536, "node line 1: the walked clamp with its root");
        const int nf3 = std::sscanf(node_lines[2], "point_light_node device=%lu frame=%lu node=%lx root=%lx depth=%u dist=%ld reach=%ld root_dist=%ld root_reach=%ld verdict=%31s node_scale=%ld root_scale=%ld",
                                    &device, &frame, &n_node, &n_root, &depth, &dist, &reach, &rdist, &rreach, verdict, &nscale, &rscale);
        check(nf3 == 12 && n_node == addr(&lonely) && n_root == 0 && depth == 0 && dist == 5000 && reach == 1300 && rdist == 0 && rreach == 0 && !std::strcmp(verdict, "node_is_root") && nscale == 300 && rscale == 0, "node line 3: a root node reports no root");
        frame_line_count = node_line_count = 0;
        // the sample is bounded: 70 distinct walked nodes on a capture frame keep 64
        pla::begin_frame(true);
        static Node many[70];
        for (unsigned i = 0; i < 70; ++i) { node_set(many[i], addr(&root), 200, 1232, 0, 0); run(1232, many[i], light, 0x246, false); }
        z = pla::stats(); check(z.walks == 70 && z.samples == 64, "sample bounded at 64 of 70 walks");
        pla::present(7, 43, true);
        check(frame_line_count == 1 && node_line_count == 64, "64 node lines");
        frame_line_count = node_line_count = 0;
        pla::begin_frame(true); pla::present(7, 44, false); // capture flagged then not captured at present: nothing logged for nodes
        check(node_line_count == 0, "no node lines without captured=true");
        frame_line_count = node_line_count = 0;
    }
    // cost: the same harness around the patched and the native site (diagnostic, not game FPS)
    const double patched_admit = bench_us(900, node, light), patched_root = bench_us(1232, node, light), patched_memo = bench_us(1232, node, light, false);
    {
        const pla::Stats after = pla::stats();
        check(after.memo_hits >= 20000, "memo bench answered from the memo");
    }
    frame_line_count = node_line_count = 0;
    // rollback
    check(pla::shutdown() && !std::strcmp(pla::state(), "restored"), "shutdown restores");
    check(site_bytes_are(core::expected_site, core::site_length) && !std::memcmp(code, core::expected_window, core::window_length), "rollback bytes exact");
    check(same(run(1232, node, light), native_reject) && same(run(900, node, light), native_admit), "after rollback the native decisions are back");
    check(pla::shutdown(), "second shutdown is a no-op");
    const double native_admit_us = bench_us(900, node, light), native_reject_us = bench_us(1232, node, light);
    std::printf("POINT LIGHT ADMISSION BENCH native_admit_us=%.4f native_reject_us=%.4f patched_admit_us=%.4f patched_root_walk_us=%.4f patched_memo_hit_us=%.4f harness=fixture_run_included game_fps=unmeasured\n",
                native_admit_us, native_reject_us, patched_admit, patched_root, patched_memo);
    // re-install, restore, then the closed window refuses
    check(pla::install_at(site_va()) && pla::shutdown() && site_bytes_are(core::expected_site, core::site_length), "re-install and restore");
    x3m::engine_patch::close_install_window("fixture");
    check(!pla::install_at(site_va()) && !std::strcmp(pla::state(), "late_claim") && site_bytes_are(core::expected_site, core::site_length), "closed install window: late_claim, site untouched");
    pla::present(7, 45, true);
    check(frame_line_count == 0 && node_line_count == 0, "present logs nothing while the patch is not live");
    std::printf("POINT LIGHT ADMISSION CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
