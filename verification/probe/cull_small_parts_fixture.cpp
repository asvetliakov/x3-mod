// X3 CPU fixture of the small-parts cull trampoline: a synthetic
// re-implementation of the engine's per-node cull and LOD pass 0x0047cfe0
// whose three verified windows (the census's 0x0047d248..0x0047d266 and
// 0x0047d519..0x0047d533, this patch's 0x0047d294..0x0047d2cc) are byte-exact,
// the 1,214 census rows of run131 frame 4991 (verification/fixtures/
// run131-cull-census-rows.json, rendered by build_cull_small_parts.py) replayed
// as synthetic nodes, and the production modules (src/proxy/cull_small_parts.cpp,
// src/proxy/cull_census.cpp) patching that copy. Checks: the synthetic pass
// reproduces the engine's s, measure and verdict of every row; with the
// threshold at 0 the patched pass leaves every node and EAX/ECX/EDX/EFLAGS as
// native; at 2 px exactly the 403-draw class of kept nodes under the threshold
// flips to culled and nothing else changes, at 4 px the 458-draw class, at 8 px
// 479 (scope `all`); with scope `bodies` only the parentless subset flips (the
// rows carry no parent link: a row whose limit exceeds its own threshold
// provably has one, the rest get a synthetic parent when they carry no body
// flag 0x09000000 -- 89 nodes / 395 draws at 2 px, 120 / 450 at 4 px) and a
// parented node below the threshold reaches the engine's compare with the
// native registers and flags; the census armed together with the stub reports
// the flipped nodes as renderable=0 with verdict culled_small and the scope; callee-saved registers, ESP, the
// empty x87 stack and LastError preserved; exact rollback; option off,
// changed bytes and the closed window refused. Diagnostic timings only; not
// game FPS. Never launches the game.
#include "../../src/proxy/cull_small_parts.h"
#include "../../src/proxy/cull_small_parts_core.h"
#include "../../src/proxy/cull_census.h"
#include "../../src/proxy/cull_census_core.h"
#include "../../src/proxy/camera_state.h"
#include "../../src/proxy/engine_patch.h"
#include "run131_rows_inc.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdarg>
#include <string>
#include <vector>
static std::vector<std::string> census_frame_lines, census_entry_lines, small_lines, install_lines;
namespace x3m { void log(const char* format, ...) {
    char text[512]; std::va_list a; va_start(a, format); std::vsnprintf(text, sizeof text, format, a); va_end(a);
    if (!std::strncmp(text, "cull_census_frame ", 18)) { census_frame_lines.emplace_back(text); return; }
    if (!std::strncmp(text, "cull_census device=", 19)) { census_entry_lines.emplace_back(text); return; }
    if (!std::strncmp(text, "cull_small_parts_frame ", 23) || !std::strncmp(text, "cull_small_parts_value ", 23)) { small_lines.emplace_back(text); }
    if (!std::strncmp(text, "cull_small_parts requested=", 27)) install_lines.emplace_back(text);
    static unsigned lines = 0; if (lines++ < 12) std::printf("%s\n", text);
} }
namespace x3m::object_trace { bool executable_verified() { return true; } }
// The camera latch seam: the fixture supplies the projection scale the
// production begin_frame() would read from the engine's buffer.
static bool fixture_camera_available = false; static float fixture_camera_m00 = 0; static bool fixture_camera_valid = false;
namespace x3m::camera_state {
bool available() { return fixture_camera_available; }
const char* status() { return fixture_camera_available ? "fixture" : "disabled"; }
bool read(Sample* out) { *out = Sample{}; if (!fixture_camera_available) { out->read_failure = Unavailable; return false; } out->state.valid = fixture_camera_valid; out->state.m00 = fixture_camera_m00; return fixture_camera_valid; }
}
namespace small = x3m::cull_small_parts;
namespace score = x3m::cull_small_parts::core;
namespace census = x3m::cull_census;
namespace ccore = x3m::cull_census::core;

// ---- the synthetic pass: thiscall(ECX = node, view, flag) with the engine's frame layout ----
// As cull_census_fixture.cpp; the effective-limit window 0x0047d294..0x0047d2cc
// is emitted as the engine's exact bytes (gas would pick the other encodings
// of `cmp esi,eax` / `mov eax,ecx`), with the `eb 05` at its end landing on
// sp_degenerate after the five bytes of `cmp esi,1; jge sp_lod`. View fields:
// +0x5c W (the engine's [0x608518]+0x5c, 640 in run131), +0x270 flags, +0x298
// scale, +0x300 view-distance setting. (a*b)/c is the 0x00469a30 contract.
extern "C" std::int32_t synthetic_ratio(std::int32_t a, std::int32_t b, std::int32_t c) {
    if (!c) return 0;
    return std::int32_t((std::int64_t(a) * std::int64_t(b)) / std::int64_t(c));
}
extern "C" void synthetic_pass();
asm(R"(
    .intel_syntax noprefix
    .text
    .globl _synthetic_pass
_synthetic_pass:
    sub esp, 0x14
    mov al, byte ptr [esp+0x1c]
    push ebx
    push ebp
    push esi
    push edi
    mov edi, ecx
    and dword ptr [edi+0x130], 0xffe7ffff
    xor edx, edx
    mov dword ptr [edi+0x14c], edx
    mov byte ptr [esp+0x18], al
    mov ebp, dword ptr [esp+0x28]
    mov ebx, dword ptr [edi+0x12c]
    test ebx, 0x100000
    jz sp_latch_done
    and ebx, 0xfffffffd
    mov dword ptr [edi+0x12c], ebx
sp_latch_done:
    test bl, 2
    jz sp_exit_site
    mov ebx, dword ptr [edi+0xf0]
    mov dword ptr [esp+0x10], ebx
    mov eax, ebx
    imul dword ptr [ebp+0x298]
    mov ecx, 0x4000
    idiv ecx
    mov ebx, eax
    mov dword ptr [esp+0x10], ebx
    mov eax, dword ptr [ebp+0x5c]
    cmp ebx, eax
    jl sp_measure_far
    test dword ptr [edi+0x12c], 0x400
    jnz sp_measure_far
    mov ecx, dword ptr [edi+0xa0]
    push ebx
    push eax
    push ecx
    call _synthetic_ratio
    add esp, 12
    mov esi, eax
    jmp sp_metric
sp_measure_far:
    mov esi, 0x7000000
sp_metric:
    cmp ebx, 0x280
    jge sp_metric_ratio
    mov dword ptr [esp+0x2c], 0x7000000
    jmp sp_measure_site
sp_metric_ratio:
    mov edx, dword ptr [edi+0xa0]
    push ebx
    push 0x280
    push edx
    call _synthetic_ratio
    add esp, 12
    .globl _synthetic_measure_window
_synthetic_measure_window:
    test eax, eax
    mov dword ptr [esp+0x2c], eax
    jne sp_measure_site
    mov dword ptr [esp+0x2c], 1
sp_measure_site:
    mov eax, dword ptr [edi+0x1dc]
    test eax, eax
    mov ecx, 0x180000
    jle sp_size_flag
    cmp esi, eax
    jge sp_size_flag
    or dword ptr [edi+0x130], 0x100000
    jmp sp_envmap
sp_size_flag:
    cmp esi, 0x14
    jge sp_envmap
    or dword ptr [edi+0x130], ecx
sp_envmap:
    test dword ptr [ebp+0x270], 0x1000000
    je sp_limit
    or dword ptr [edi+0x130], ecx
    .globl _synthetic_small_window
_synthetic_small_window:
    .byte 0x83,0xfe,0x14, 0x7d,0x09, 0x33,0xf6, 0x83,0xa7,0x2c,0x01,0x00,0x00,0xfd
sp_limit:
    .byte 0x8b,0x4f,0x18, 0x85,0xc9, 0x8b,0x87,0xd8,0x01,0x00,0x00, 0x74,0x0c, 0x8b,0x89,0xd8,0x01,0x00,0x00
    .byte 0x3b,0xc8, 0x7e,0x02, 0x8b,0xc1, 0x85,0xc0, 0x7e,0x0d, 0x3b,0xf0, 0x7d,0x09
    .byte 0x83,0xa7,0x2c,0x01,0x00,0x00,0xfd, 0xeb,0x05
sp_min_test:
    cmp esi, 1
    jge sp_lod
    .globl _synthetic_degenerate
_synthetic_degenerate:
sp_degenerate:
    mov eax, dword ptr [edi+0x12c]
    test eax, 0x4000000
    jne sp_lod
    and eax, 0xfffffffd
    mov dword ptr [edi+0x12c], eax
    jmp sp_exit_site
sp_lod:
    mov ebp, dword ptr [edi+0x1fc]
    sub ebp, 1
    mov esi, ebp
    test ebp, ebp
    jle sp_lod_adjust
sp_lod_loop:
    mov eax, dword ptr [edi+0x1ec+esi*4]
    cmp dword ptr [esp+0x2c], eax
    jl sp_lod_store
    sub esi, 1
    test esi, esi
    jg sp_lod_loop
    jmp sp_lod_adjust
sp_lod_store:
    mov dword ptr [edi+0x14c], esi
sp_lod_adjust:
    mov edx, dword ptr [esp+0x28]
    test dword ptr [edx+0x270], 0x1000000
    je sp_lod_setting
    add dword ptr [edi+0x14c], 1
    jmp sp_lod_force
sp_lod_setting:
    cmp dword ptr [edx+0x300], 3
    jl sp_lod_force
    add dword ptr [edi+0x14c], -1
sp_lod_force:
    cmp dword ptr [edx+0x300], 3
    jle sp_lod_clamp
    mov dword ptr [edi+0x14c], 0
sp_lod_clamp:
    mov eax, dword ptr [edi+0x14c]
    cmp eax, ebp
    jl sp_lod_floor
    mov eax, ebp
sp_lod_floor:
    xor edx, edx
    test eax, eax
    setle dl
    sub edx, 1
    and eax, edx
    mov dword ptr [edi+0x14c], eax
    mov ecx, dword ptr [edi+0x14c]
    test ecx, ecx
    jle sp_renderable
    mov eax, dword ptr [edi+0x1fc]
    sub eax, 1
    cmp ecx, eax
    jne sp_renderable
    mov eax, dword ptr [edi+0x12c]
    test eax, 0x8000
    je sp_renderable
    and eax, 0xfffffffd
    mov dword ptr [edi+0x12c], eax
    jmp sp_exit_window
sp_renderable:
    mov edx, dword ptr [edi+0x12c]
    and edx, 0xfffffeff
    or edx, 2
    mov dword ptr [edi+0x12c], edx
    .globl _synthetic_exit_window
_synthetic_exit_window:
sp_exit_window:
    cmp ecx, 3
    jl sp_exit_site
    or dword ptr [edi+0x130], 0x100000
sp_exit_site:
    mov edi, dword ptr [edi+0xc]
    cmp dword ptr [edi], 0
    je sp_epilogue
    mov esi, dword ptr [esp+0x18]
    mov ebx, dword ptr [esp+0x28]
sp_child_loop:
    push esi
    push ebx
    mov ecx, edi
    call _synthetic_pass
    mov edi, dword ptr [edi]
    cmp dword ptr [edi], 0
    jne sp_child_loop
sp_epilogue:
    pop edi
    pop esi
    pop ebp
    pop ebx
    add esp, 0x14
    ret 8
    .att_syntax
)");
extern "C" unsigned char synthetic_measure_window[], synthetic_small_window[], synthetic_degenerate[], synthetic_exit_window[];

// ---- harness: a thiscall of the synthetic pass with sentinel registers, PUSHAD/EFLAGS/ESP/x87 captured at the return ----
extern "C" {
struct Frame {
    std::uint32_t node, view, flag, entry_esp;
    std::uint32_t out[9];                        // PUSHAD order edi,esi,ebp,esp,ebx,edx,ecx,eax then EFLAGS, at the return
    std::uint32_t exit_esp;
    std::uint32_t x87env[7];
    std::uint32_t entry_ebp;
};
Frame* fixture_frame = nullptr;
void fixture_call(Frame* frame);
}
static_assert(offsetof(Frame, out) == 16 && offsetof(Frame, exit_esp) == 52 && offsetof(Frame, x87env) == 56 && offsetof(Frame, entry_ebp) == 84, "frame layout");
asm(R"(
    .intel_syntax noprefix
    .text
    .globl _fixture_call
_fixture_call:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, dword ptr [ebp+8]
    mov dword ptr [_fixture_frame], eax
    mov dword ptr [eax+12], esp
    mov dword ptr [eax+84], ebp
    fninit
    push dword ptr [eax+8]
    push dword ptr [eax+4]
    mov ecx, dword ptr [eax]
    mov ebx, 0x0b0b0b0b
    mov esi, 0x5e5e5e5e
    mov edi, 0xd1d1d1d1
    mov edx, 0xdddddddd
    mov eax, 0xaaaaaaaa
    call _synthetic_pass
    pushfd
    pushad
    mov ebx, dword ptr [_fixture_frame]
    fnstenv [ebx+56]
    mov esi, esp
    lea edi, [ebx+16]
    mov ecx, 9
    cld
    rep movsd
    add esp, 36
    mov dword ptr [ebx+52], esp
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
    .att_syntax
)");

static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) { ++checks; if (!okay) { ++failures; std::printf("FAIL %s\n", label); } }

// ---- synthetic records ----
struct alignas(16) Node { unsigned char bytes[0x240]; };
struct alignas(16) View { unsigned char bytes[0x400]; };
alignas(16) static std::uint32_t sentinel[4] = {0, 0, 0, 0};
static std::uint32_t addr(const void* p) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(p)); }
static void put(void* base, unsigned off, std::uint32_t v) { std::memcpy(static_cast<unsigned char*>(base) + off, &v, 4); }
static std::uint32_t get(const void* base, unsigned off) { std::uint32_t v; std::memcpy(&v, static_cast<const unsigned char*>(base) + off, 4); return v; }
constexpr unsigned ladder_offset = 0x1f0, ladder_count_offset = 0x1fc, d_offset = 0xf0, first_child_offset = 0xc, view_w = 0x5c, view_flags = 0x270, view_scale = 0x298, view_distance = 0x300;
static void node_set(Node& n, Node* parent, std::int32_t radius, std::int32_t d, std::uint32_t flags, std::int32_t thr_1d8, std::int32_t thr_1dc, std::uint32_t model,
                     unsigned lods = 1, std::int32_t t1 = 0, std::int32_t t2 = 0, std::int32_t t3 = 0) {
    std::memset(n.bytes, 0, sizeof n.bytes);
    put(n.bytes, 0, addr(sentinel)); put(n.bytes, first_child_offset, addr(sentinel));
    put(n.bytes, ccore::parent_offset, parent ? addr(parent) : 0); put(n.bytes, ccore::radius_offset, std::uint32_t(radius)); put(n.bytes, d_offset, std::uint32_t(d));
    put(n.bytes, ccore::flags12c_offset, flags); put(n.bytes, ccore::threshold_1d8_offset, std::uint32_t(thr_1d8)); put(n.bytes, ccore::threshold_1dc_offset, std::uint32_t(thr_1dc));
    put(n.bytes, ccore::model_offset, model); put(n.bytes, ladder_count_offset, lods);
    put(n.bytes, ladder_offset, std::uint32_t(t1)); put(n.bytes, ladder_offset + 4, std::uint32_t(t2)); put(n.bytes, ladder_offset + 8, std::uint32_t(t3));
}
// Links children[0..n) as the traversal children of parent (sibling chain at +0, first child at +0xc); +0x18 is left as set.
static void link_traversal(Node& parent, Node* const* children, unsigned count) {
    put(parent.bytes, first_child_offset, count ? addr(children[0]) : addr(sentinel));
    for (unsigned i = 0; i < count; ++i) put(children[i]->bytes, 0, i + 1 < count ? addr(children[i + 1]) : addr(sentinel));
}
static void view_set(View& v, std::int32_t w, std::uint32_t flags, std::int32_t scale, std::int32_t distance) {
    std::memset(v.bytes, 0, sizeof v.bytes); put(v.bytes, view_w, std::uint32_t(w)); put(v.bytes, view_flags, flags); put(v.bytes, view_scale, std::uint32_t(scale)); put(v.bytes, view_distance, std::uint32_t(distance));
}
struct Result { std::uint32_t edi, esi, ebp, ebx, edx, ecx, eax, flags; bool preserved, x87_empty; };
static Result run(Node& root, View& view, std::uint32_t flag = 0) {
    Frame f{}; f.node = addr(&root); f.view = addr(&view); f.flag = flag;
    fixture_call(&f);
    Result r{}; r.edi = f.out[0]; r.esi = f.out[1]; r.ebp = f.out[2]; r.ebx = f.out[4]; r.edx = f.out[5]; r.ecx = f.out[6]; r.eax = f.out[7]; r.flags = f.out[8] & 0x8d5;
    r.preserved = r.edi == 0xd1d1d1d1u && r.esi == 0x5e5e5e5eu && r.ebx == 0x0b0b0b0bu && r.ebp == f.entry_ebp && f.exit_esp == f.entry_esp;
    r.x87_empty = ((f.x87env[1] >> 11) & 7) == 0 && (f.x87env[2] & 0xffff) == 0xffff;
    if (!r.preserved || !r.x87_empty)
        std::printf("DETAIL edi=%08lx esi=%08lx ebx=%08lx ebp=%08lx/%08lx esp=%08lx/%08lx status=%04lx tag=%04lx\n", (unsigned long)r.edi, (unsigned long)r.esi, (unsigned long)r.ebx,
                    (unsigned long)r.ebp, (unsigned long)f.entry_ebp, (unsigned long)f.exit_esp, (unsigned long)f.entry_esp, (unsigned long)(f.x87env[1] & 0xffff), (unsigned long)(f.x87env[2] & 0xffff));
    return r;
}
static bool same_outputs(const Result& a, const Result& b) { return a.eax == b.eax && a.ecx == b.ecx && a.edx == b.edx && a.flags == b.flags; }
// The engine's verdict from a node's final flags and the row's fields (cull_census classify without the stub).
static int verdict_of(const Node& n, const RowData& r) {
    if (get(n.bytes, ccore::flags12c_offset) & 2u) return 0;
    if (r.limit > 0 && r.measure < r.limit) return 1;
    if (r.measure < 1 && !(r.flags_in & 0x4000000u)) return 2;
    return 3;
}
struct Row { unsigned long device, frame, view, node, model, flags_in, flags_out; long s, measure, d, radius, thr_1dc, thr_1d8, limit, lod; char verdict[24]; };
static bool parse_row(const std::string& line, Row& r) {
    return std::sscanf(line.c_str(), "cull_census device=%lu frame=%lu view=%lx node=%lx model=%lx s=%ld measure=%ld d=%ld radius=%ld thr_1dc=%ld thr_1d8=%ld limit=%ld flags_in=%lx flags_out=%lx lod=%ld verdict=%23s",
                       &r.device, &r.frame, &r.view, &r.node, &r.model, &r.s, &r.measure, &r.d, &r.radius, &r.thr_1dc, &r.thr_1d8, &r.limit, &r.flags_in, &r.flags_out, &r.lod, r.verdict) == 16;
}

static const unsigned char* small_site() { return synthetic_small_window + score::site_offset; }
static const unsigned char* small_cull() { return synthetic_small_window + score::cull_offset; }
static bool small_window_original() { return !std::memcmp(synthetic_small_window, score::window, score::window_length); }
static bool census_windows_original() { return !std::memcmp(synthetic_measure_window, ccore::measure_window, ccore::measure_window_length) && !std::memcmp(synthetic_exit_window, ccore::exit_window, ccore::exit_window_length); }

// ---- the replay tree: a hidden root (early exit, unmeasured) with every census row as a child ----
static Node* replay = nullptr;      // [0] root, [1..kRowCount] the rows
static Node* parents = nullptr;     // per row: the parent record carrying +0x1d8 = limit when the recorded limit exceeds the own threshold (proven parent),
                                    // or +0x1d8 = the own threshold (limit unchanged) for a row without a body flag (synthetic parent: the rows carry no +0x18)
static Node* replay_native = nullptr;
static void replay_build(View& view) {
    view_set(view, std::int32_t(kRowsMeasureReference), 0, std::int32_t(kRowsViewScale), 2);
    node_set(replay[0], nullptr, 1, 100000, 0x1000, 0, 0, 0xffff);   // renderable bit clear: exits before the measure site
    std::vector<Node*> kids; kids.reserve(kRowCount);
    for (unsigned i = 0; i < kRowCount; ++i) {
        const RowData& r = kRows[i];
        Node& n = replay[1 + i];
        node_set(n, nullptr, r.radius, r.d, r.flags_in, r.thr_1d8, r.thr_1dc, 0x10000 + i);
        if (r.limit > r.thr_1d8 || !(r.flags_in & kRowsBodyFlagsMask)) { std::memset(parents[i].bytes, 0, sizeof parents[i].bytes); put(parents[i].bytes, ccore::threshold_1d8_offset, std::uint32_t(r.limit > r.thr_1d8 ? r.limit : r.thr_1d8)); put(n.bytes, ccore::parent_offset, addr(&parents[i])); }
        kids.push_back(&n);
    }
    link_traversal(replay[0], kids.data(), kRowCount);
}
static void replay_reset() { std::memcpy(replay, replay_native, sizeof(Node) * (kRowCount + 1)); }
// Runs the replay and compares every row node with the native reference: nodes
// whose bytes differ only by a cleared renderable bit are "flipped"; any other
// difference is counted. Returns the flipped count, their draws and the others.
struct Flip { unsigned flipped, draws, other_changes; };
static Flip replay_compare(const Node* reference) {
    Flip f{};
    for (unsigned i = 0; i < kRowCount; ++i) {
        const Node& now = replay[1 + i]; const Node& ref = reference[1 + i];
        if (!std::memcmp(now.bytes, ref.bytes, sizeof(Node))) continue;
        Node expect = ref; put(expect.bytes, ccore::flags12c_offset, get(ref.bytes, ccore::flags12c_offset) & ~2u);
        if ((get(ref.bytes, ccore::flags12c_offset) & 2u) && !std::memcmp(now.bytes, expect.bytes, sizeof(Node))) { ++f.flipped; f.draws += unsigned(kRows[i].draws); }
        else ++f.other_changes;
    }
    return f;
}
// Rows the stub sends down the cull path: every measured node with s below the threshold, whether or not the engine's own tests would have culled it.
static bool row_parentless(const Node* reference, unsigned i) { return get(reference[1 + i].bytes, ccore::parent_offset) == 0; }
static unsigned rows_below(std::int32_t threshold, bool bodies_only = false) { unsigned n = 0; for (unsigned i = 0; i < kRowCount; ++i) if (kRows[i].s < threshold && !(bodies_only && !row_parentless(replay_native, i))) ++n; return n; }
// Whether the flipped set is exactly {kept rows with s < threshold} (scope bodies: the parentless ones of them).
static bool replay_flipped_exactly(const Node* reference, std::int32_t threshold, bool bodies_only = false) {
    for (unsigned i = 0; i < kRowCount; ++i) {
        const bool kept_native = (get(reference[1 + i].bytes, ccore::flags12c_offset) & 2u) != 0;
        const bool kept_now = (get(replay[1 + i].bytes, ccore::flags12c_offset) & 2u) != 0;
        const bool expect_flip = kept_native && kRows[i].s < threshold && !(bodies_only && !row_parentless(reference, i));
        if (kept_now != (kept_native && !expect_flip)) return false;
    }
    return true;
}

// ---- the 12-node bench tree of the census fixture ----
static Node R, A, B, C, E, F, G, H, I, J, gA1, gA2;
static Node* const all[] = {&R, &A, &B, &C, &E, &F, &G, &H, &I, &J, &gA1, &gA2};
constexpr unsigned all_count = sizeof all / sizeof all[0];
static Node initial[all_count];
static void reset_tree() { for (unsigned i = 0; i < all_count; ++i) *all[i] = initial[i]; }
static void link_parented(Node& parent, Node* const* children, unsigned count) {
    link_traversal(parent, children, count);
    for (unsigned i = 0; i < count; ++i) put(children[i]->bytes, ccore::parent_offset, addr(&parent));
}
static double bench_us(Node& root, View& view, unsigned loops = 20000) {
    LARGE_INTEGER f{}, s{}, e{}; QueryPerformanceFrequency(&f);
    auto once = [&] { reset_tree(); run(root, view); };
    for (unsigned i = 0; i < 256; ++i) once();
    QueryPerformanceCounter(&s); for (unsigned i = 0; i < loops; ++i) once(); QueryPerformanceCounter(&e);
    return double(e.QuadPart - s.QuadPart) * 1e6 / double(f.QuadPart) / double(loops);
}
static float bits_to_float(std::uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

int main() {
    DWORD old = 0;
    check(VirtualProtect(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(synthetic_measure_window) & ~std::uintptr_t(0xfff)), 0x2000, PAGE_EXECUTE_READWRITE, &old) != FALSE, "synthetic code writable");
    check(small_window_original() && census_windows_original(), "synthetic pass carries the three engine windows byte-exact");
    check(synthetic_degenerate == synthetic_small_window + score::window_length + 5, "the window's trailing jmp lands on the degenerate test after five bytes");
    if (!small_window_original() || !census_windows_original()) { std::printf("CULL SMALL PARTS CPU checks=%u failures=%u\n", checks, failures); return 1; }
    const float m00 = bits_to_float(kRowsM00Bits);

    // ---- core rules ----
    check(score::threshold_for(2.0, m00, kRowsWidth) == 3 && score::threshold_for(4.0, m00, kRowsWidth) == 6 && score::threshold_for(8.0, m00, kRowsWidth) == 11, "threshold rule at m00 3f4ccccc / 1280: 2 px -> 3, 4 px -> 6, 8 px -> 11");
    check(score::threshold_for(2.0, 0.8f, 1280) == 3 && score::threshold_for(2.0, 0.8f, 1920) == 2 && score::threshold_for(1.0, 1.0f, 1280) == 1, "threshold rule: exact 0.8 and other widths");
    check(score::threshold_for(0.0, m00, 1280) == 0 && score::threshold_for(2.0, 0.0f, 1280) == 0 && score::threshold_for(2.0, m00, 0) == 0 && score::threshold_for(65.0, m00, 1280) == 0, "threshold rule: unusable inputs give 0 (vanilla)");
    for (unsigned i = 0; i < kRowsExpectedCount; ++i) check(score::threshold_for(kRowsExpected[i].px, m00, kRowsWidth) == kRowsExpected[i].threshold_s, "threshold rule matches the tracked expectation");
    double px = 0;
    check(score::parse_px("2", &px) && px == 2.0 && score::parse_px("2.5", &px) && px == 2.5 && !score::parse_px("2,5", &px) && !score::parse_px("", &px) && !score::parse_px("x", &px), "px parser");

    // ---- replay tree and the native reference ----
    replay = static_cast<Node*>(VirtualAlloc(nullptr, sizeof(Node) * (kRowCount + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    parents = static_cast<Node*>(VirtualAlloc(nullptr, sizeof(Node) * kRowCount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    replay_native = static_cast<Node*>(VirtualAlloc(nullptr, sizeof(Node) * (kRowCount + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(replay && parents && replay_native, "replay pools");
    View view; replay_build(view);
    Node* replay_initial = static_cast<Node*>(VirtualAlloc(nullptr, sizeof(Node) * (kRowCount + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memcpy(replay_initial, replay, sizeof(Node) * (kRowCount + 1));
    const Result native = run(replay[0], view);
    check(native.preserved && native.x87_empty, "native replay: callee-saved registers, ESP and empty x87 stack");
    std::memcpy(replay_native, replay, sizeof(Node) * (kRowCount + 1));
    {
        unsigned mismatched = 0, kept = 0, draws_kept = 0;
        for (unsigned i = 0; i < kRowCount; ++i) { if (verdict_of(replay[1 + i], kRows[i]) != kRows[i].verdict) ++mismatched; if (kRows[i].verdict == 0) { ++kept; draws_kept += unsigned(kRows[i].draws); } }
        check(mismatched == 0, "native replay: the synthetic pass reproduces the engine's verdict of every row");
        std::printf("REPLAY rows=%u native_mismatches=%u kept=%u draws_joined=%u\n", kRowCount, mismatched, kept, draws_kept);
    }

    // ---- option off / invalid / engine site absent / changed bytes ----
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", nullptr);
    check(!small::initialize() && !std::strcmp(small::state(), "disabled") && small_window_original(), "unset variable: disabled, site untouched");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", L"0");
    check(!small::initialize() && !std::strcmp(small::state(), "disabled"), "X3M_CULL_SMALL_PARTS_PX=0: disabled");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", L"abc");
    check(!small::initialize() && !std::strcmp(small::state(), "invalid_px"), "non-numeric setting: invalid_px");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", L"65");
    check(!small::initialize() && !std::strcmp(small::state(), "invalid_px"), "65 px: invalid_px (band)");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", L"2");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_SCOPE", L"parts");
    check(!small::initialize() && !std::strcmp(small::state(), "invalid_scope") && small::stub_address() == 0, "unknown scope: invalid_scope, nothing patched");
    check(!install_lines.empty() && install_lines.back().find(" scope=invalid") != std::string::npos, "unknown scope: the install line says scope=invalid");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_SCOPE", L"all");
    check(!small::initialize() && !std::strcmp(small::state(), "bytes_mismatch") && install_lines.back().find(" scope=all") != std::string::npos, "scope all: parsed and logged");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_SCOPE", nullptr);
    { score::Scope sc = score::Scope::all; check(score::parse_scope("", &sc) && sc == score::Scope::bodies && score::parse_scope("all", &sc) && sc == score::Scope::all && score::parse_scope("bodies", &sc) && sc == score::Scope::bodies && !score::parse_scope("ALL", &sc) && !score::parse_scope("body", &sc), "scope parser: bodies (default), all, nothing else"); }
    check(!small::initialize() && !std::strcmp(small::state(), "bytes_mismatch") && small::stub_address() == 0, "engine site absent in this process: bytes_mismatch, nothing patched");
    check(install_lines.back().find(" scope=bodies") != std::string::npos, "unset scope: the install line says scope=bodies (default)");
    const std::uintptr_t site = addr(small_site()), cull = addr(small_cull());
    synthetic_small_window[2] ^= 1;
    check(!small::install_at(site, cull, false) && !std::strcmp(small::state(), "bytes_mismatch") && small_window_original() == false, "changed window byte: bytes_mismatch");
    synthetic_small_window[2] ^= 1;
    check(small_window_original(), "window byte restored");
    check(!small::install_at(0, cull, false) && !std::strcmp(small::state(), "invalid_site"), "null site refused");
    check(!small::install_at(site, cull + 1, false) && !std::strcmp(small::state(), "invalid_site"), "cull target not at window offset 47 refused");

    // ---- install ----
    check(small::install_at(site, cull, false) && !std::strcmp(small::state(), "ok"), "install_at synthetic site");
    if (!small::stub_address()) { std::printf("FAIL install state=%s\nCULL SMALL PARTS CPU checks=%u failures=%u\n", small::state(), checks, failures + 1); return 1; }
    check(small_site()[0] == 0xe9 && !std::memcmp(synthetic_small_window, score::window, score::site_offset) && !std::memcmp(synthetic_small_window + score::site_offset + 5, score::window + score::site_offset + 5, score::window_length - score::site_offset - 5), "site is jmp dispatcher; every other window byte untouched");
    {
        const std::uint32_t at = std::uint32_t(small::stub_address()), slot = (at + score::stub_length + 3) & ~3u;
        unsigned char want[score::stub_length];
        score::encode_stub(at, addr(const_cast<std::int32_t*>(&x3m_cull_small_parts_threshold)), addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_culled)), std::uint32_t(cull), slot, want, score::Scope::all);
        check(!std::memcmp(reinterpret_cast<const void*>(at), want, score::stub_length) && !std::strcmp(small::scope(), "all"), "stub bytes as encoded (scope all)");
        check(*reinterpret_cast<void**>(slot) != nullptr, "continuation slot points at the tail");
    }
    check(!small::install_at(site, cull, false) && !std::strcmp(small::state(), "already_installed"), "second install refused");
    check(x3m_cull_small_parts_threshold == 0 && small::stats().threshold == 0, "installed with the threshold at 0");
    check(small::requested_px() == 2.0, "requested px carried from the setting");

    // ---- patched, threshold 0: identical ----
    replay_reset(); std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    Result patched = run(replay[0], view);
    check(patched.preserved && patched.x87_empty && same_outputs(patched, native), "patched, threshold 0: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
    check(!std::memcmp(replay, replay_native, sizeof(Node) * (kRowCount + 1)), "patched, threshold 0: every node as native");
    check(x3m_cull_small_parts_culled == 0, "patched, threshold 0: nothing counted");

    // ---- begin_frame through the camera seam: no camera -> 0, invalid -> 0, valid -> the threshold ----
    small::set_backbuffer_width(kRowsWidth);
    fixture_camera_available = false; small::begin_frame();
    check(x3m_cull_small_parts_threshold == 0, "begin_frame without the camera latch: vanilla frame");
    fixture_camera_available = true; fixture_camera_valid = false; fixture_camera_m00 = m00; small::begin_frame();
    check(x3m_cull_small_parts_threshold == 0, "begin_frame with an invalid projection: vanilla frame");
    fixture_camera_valid = true; small::begin_frame();
    check(x3m_cull_small_parts_threshold == 3 && small::stats().threshold == 3 && small::stats().width == kRowsWidth, "begin_frame with the run131 projection at 1280: threshold 3");
    check(small_lines.size() == 1 && small_lines[0].find("cull_small_parts_value px=2 m00=0.799999952 width=1280 threshold=3") == 0, "one cull_small_parts_value line on the first valid frame");
    small::after_reset(1920);
    check(x3m_cull_small_parts_threshold == 0 && small::stats().width == 1920, "after_reset: disarmed, new width");
    small::begin_frame();
    check(x3m_cull_small_parts_threshold == 2 && small_lines.size() == 2, "begin_frame after the reset: threshold 2 at 1920, a second value line");
    small::after_reset(kRowsWidth); small::begin_frame();
    check(x3m_cull_small_parts_threshold == 3 && small_lines.size() == 3, "back to 1280: threshold 3, a third value line");

    // ---- 2 px: exactly the 403-draw class flips ----
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    SetLastError(0x5150);
    patched = run(replay[0], view);
    check(GetLastError() == 0x5150, "LastError preserved across the armed pass");
    check(patched.preserved && patched.x87_empty && same_outputs(patched, native), "2 px: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
    {
        const Flip f = replay_compare(replay_native);
        check(f.other_changes == 0 && replay_flipped_exactly(replay_native, 3), "2 px: only kept nodes with s < 3 change, and every one of them");
        check(f.flipped == kRowsExpected[0].nodes && f.draws == 403 && f.draws == kRowsExpected[0].draws, "2 px: the flipped class is 97 nodes / 403 draws");
        check(x3m_cull_small_parts_culled == rows_below(3) && rows_below(3) == 1147 && rows_below(3) > f.flipped, "2 px: the stub's count is every node below the threshold (1147: the 97 flipped plus the 1050 the engine culls itself)");
        std::printf("REPLAY px=2 threshold=3 flipped=%u draws=%u other_changes=%u culled_count=%lu\n", f.flipped, f.draws, f.other_changes, (unsigned long)x3m_cull_small_parts_culled);
    }
    small::present(7, 4991, true);
    check(small_lines.size() == 4 && small_lines[3].find("cull_small_parts_frame device=7 frame=4991 px=2 threshold=3 culled=1147 m00=0.799999952 width=1280") == 0, "capture frame: one cull_small_parts_frame row");
    check(x3m_cull_small_parts_culled == 0, "present clears the count");
    small::present(7, 4992, false);
    check(small_lines.size() == 4, "plain frame: no row");

    // ---- 4 px and 8 px ----
    for (unsigned k = 1; k < kRowsExpectedCount; ++k) {
        check(small::set_px(kRowsExpected[k].px), "set_px in band");
        small::begin_frame();
        check(x3m_cull_small_parts_threshold == kRowsExpected[k].threshold_s, "threshold for the px class");
        std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
        patched = run(replay[0], view);
        const Flip f = replay_compare(replay_native);
        check(patched.preserved && same_outputs(patched, native) && f.other_changes == 0 && replay_flipped_exactly(replay_native, kRowsExpected[k].threshold_s), "px class: only the class below the threshold flips");
        check(f.flipped == kRowsExpected[k].nodes && f.draws == kRowsExpected[k].draws && x3m_cull_small_parts_culled == rows_below(kRowsExpected[k].threshold_s), "px class: node and draw counts as tracked, the stub's count every node below the threshold");
        std::printf("REPLAY px=%g threshold=%ld flipped=%u draws=%u other_changes=%u\n", kRowsExpected[k].px, (long)x3m_cull_small_parts_threshold, f.flipped, f.draws, f.other_changes);
        small::present(7, 5000 + k, false);
    }
    check(!small::set_px(0.0) && !small::set_px(65.0), "set_px outside the band refused");

    // ---- the census and the stub armed together ----
    check(census::install_at(addr(synthetic_measure_window + ccore::measure_site_offset), addr(synthetic_exit_window + ccore::exit_site_offset)) && !std::strcmp(census::state(), "ok"), "census installed beside the stub (disjoint claims)");
    check(small::set_px(2.0), "back to 2 px");
    census::begin_frame(true); small::begin_frame();
    check(x3m_cull_small_parts_threshold == 3 && census::stats().armed, "both armed");
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    patched = run(replay[0], view);
    check(patched.preserved && patched.x87_empty && same_outputs(patched, native), "both armed: registers, ESP, x87 and outputs as native");
    {
        const Flip f = replay_compare(replay_native);
        check(f.other_changes == 0 && f.flipped == 97 && f.draws == 403, "both armed: the same 403-draw class flips");
        const census::Stats s = census::stats();
        check(s.entries == kRowCount && s.exited == kRowCount && s.unmeasured == 1 && s.overflow == 0, "both armed: every row measured and exited, the root unmeasured");
        census::present(7, 4991, true); small::present(7, 4991, true);
        std::vector<Row> rows; for (const std::string& line : census_entry_lines) { Row r{}; if (parse_row(line, r)) rows.push_back(r); }
        check(rows.size() == kRowCount, "both armed: one census row per replayed node");
        unsigned fidelity = 0, small_verdicts = 0, kept = 0, size = 0, min = 0, other = 0, renderable_small = 0;
        for (unsigned i = 0; i < rows.size() && i < kRowCount; ++i) {
            const Row& r = rows[i]; const RowData& d = kRows[i];
            if (r.node == addr(&replay[1 + i]) && r.s == d.s && r.measure == d.measure && r.d == d.d && r.radius == d.radius && r.limit == d.limit && r.thr_1d8 == d.thr_1d8 && r.thr_1dc == d.thr_1dc && r.flags_in == d.flags_in) ++fidelity;
            if (!std::strcmp(r.verdict, "culled_small")) { ++small_verdicts; if (r.flags_out & 2) ++renderable_small; }
            else if (!std::strcmp(r.verdict, "kept")) ++kept;
            else if (!std::strcmp(r.verdict, "culled_size")) ++size;
            else if (!std::strcmp(r.verdict, "culled_min")) ++min;
            else ++other;
        }
        check(fidelity == kRowCount, "both armed: the census rows carry the engine's own s, measure, D, radius, thresholds and limit of every row");
        check(small_verdicts == 97 && renderable_small == 0, "both armed: 97 rows culled_small, every one with the renderable bit clear");
        { unsigned scoped = 0; for (const std::string& line : census_entry_lines) if (line.find("verdict=culled_small scope=all") != std::string::npos) ++scoped; check(scoped == 97, "both armed: every culled_small row carries scope=all"); }
        check(kept == 164 - 97 && size == 624 && min == 426 && other == 0, "both armed: kept 67, culled_size 624, culled_min 426 (the engine's share unchanged)");
        std::printf("CENSUS rows=%u fidelity=%u kept=%u culled_size=%u culled_min=%u culled_small=%u other=%u\n", (unsigned)rows.size(), fidelity, kept, size, min, small_verdicts, other);
        census_frame_lines.clear(); census_entry_lines.clear();
    }
    // the census alone (stub disarmed) reports the engine's verdicts again
    small::after_reset(kRowsWidth);
    census::begin_frame(true);
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    run(replay[0], view);
    check(!std::memcmp(replay, replay_native, sizeof(Node) * (kRowCount + 1)), "stub disarmed beside the armed census: every node as native");
    census::present(7, 4993, true);
    {
        unsigned small_verdicts = 0; for (const std::string& line : census_entry_lines) if (line.find("verdict=culled_small") != std::string::npos) ++small_verdicts;
        check(small_verdicts == 0 && census_entry_lines.size() == kRowCount, "stub disarmed: no culled_small rows");
        census_frame_lines.clear(); census_entry_lines.clear();
    }
    check(census::shutdown(), "census restored");
    check(census_windows_original(), "census sites back exactly");

    // ---- the 12-node bench tree (census fixture layout), W = 1280 ----
    View bench_view; view_set(bench_view, 1280, 0, 0x4000, 2);
    node_set(R, nullptr, 20000, 100000, 0x1002, 0, 0, 0x5000, 4, 100, 50, 25);
    node_set(A, &R, 100, 100000, 0x1002, 4, 0, 0x5001);
    node_set(B, &R, 50, 100000, 0x1002, 0, 0, 0x5002);
    node_set(C, &R, 3000, 100000, 0x9002, 0, 40, 0x5003, 3, 100, 50);
    node_set(E, &R, 3000, 100000, 0x1002, 0, 40, 0x5004, 3, 100, 50);
    node_set(F, &R, 3000, 100000, 0x1002, 0, 0, 0x5005, 4, 100, 50, 25);
    node_set(G, &R, 3000, 100000, 0x1000, 0, 0, 0x5006);
    node_set(H, &R, 3000, 100000, 0x101002, 0, 0, 0x5007);
    node_set(I, &R, 300, 500, 0x1002, 0, 0, 0x5008, 2, 100);
    node_set(J, &R, 800, 100000, 0x1002, 0, 0, 0x500b);
    node_set(gA1, &A, 5000, 100000, 0x1002, 2, 0, 0x5009);
    node_set(gA2, &A, 470, 100000, 0x1002, 8, 0, 0x500a);
    Node* r_children[] = {&A, &B, &C, &E, &F, &G, &H, &I, &J}; link_parented(R, r_children, 9);
    Node* a_children[] = {&gA1, &gA2}; link_parented(A, a_children, 2);
    for (unsigned i = 0; i < all_count; ++i) initial[i] = *all[i];
    // threshold 20 in s units: E (s 19), F (19) and J (5) are kept natively and flip; C fades either way; A/B/gA2 are the engine's own culls
    x3m_cull_small_parts_threshold = 20;
    reset_tree(); run(R, bench_view);
    check(!(get(E.bytes, 0x12c) & 2) && !(get(F.bytes, 0x12c) & 2) && !(get(J.bytes, 0x12c) & 2) && (get(R.bytes, 0x12c) & 2) && (get(I.bytes, 0x12c) & 2) && (get(gA1.bytes, 0x12c) & 2) && x3m_cull_small_parts_culled >= 3, "bench tree at threshold 20: E, F, J culled; R, I (saturated), gA1 kept");
    const double armed_us = bench_us(R, bench_view);
    x3m_cull_small_parts_threshold = 0;
    const double disarmed_us = bench_us(R, bench_view);

    // ---- rollback ----
    check(small::shutdown() && !std::strcmp(small::state(), "restored"), "shutdown restores");
    check(small_window_original(), "rollback bytes exact");
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    const Result restored = run(replay[0], view);
    check(!std::memcmp(replay, replay_native, sizeof(Node) * (kRowCount + 1)) && same_outputs(restored, native) && restored.preserved, "after rollback the native pass is back");
    check(small::shutdown(), "second shutdown is a no-op");
    check(small::stats().threshold == 0, "not live: threshold 0");
    const double native_us = bench_us(R, bench_view);
    std::printf("CULL SMALL PARTS BENCH native_pass_us=%.4f patched_disarmed_us=%.4f patched_armed_us=%.4f nodes_per_pass=12 culled_per_armed_pass=7 harness=fixture_call_included game_fps=unmeasured\n",
                native_us, disarmed_us, armed_us);
    // ---- re-install, restore, then the closed window refuses ----
    // ---- scope bodies: only parentless nodes are culled ----
    check(small::install_at(site, cull, true) && !std::strcmp(small::state(), "ok") && !std::strcmp(small::scope(), "bodies"), "re-install with scope bodies");
    {
        const std::uint32_t at = std::uint32_t(small::stub_address()), slot = (at + score::stub_length + 3) & ~3u;
        unsigned char want[score::stub_length], all_stub[score::stub_length];
        score::encode_stub(at, addr(const_cast<std::int32_t*>(&x3m_cull_small_parts_threshold)), addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_culled)), std::uint32_t(cull), slot, want, score::Scope::bodies);
        score::encode_stub(at, addr(const_cast<std::int32_t*>(&x3m_cull_small_parts_threshold)), addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_culled)), std::uint32_t(cull), slot, all_stub, score::Scope::all);
        check(!std::memcmp(reinterpret_cast<const void*>(at), want, score::stub_length), "bodies stub bytes as encoded");
        check(!std::memcmp(want, all_stub, score::stub_scope_branch) && !std::memcmp(want + score::stub_cull, all_stub + score::stub_cull, score::stub_length - score::stub_cull) && want[27] == 0x75 && want[28] == score::stub_continue - 29 && want[35] == 0xeb && want[36] == score::stub_cull - 37,
              "bodies stub differs from the all stub only in bytes 27..46: jne continue, mov eax,[edi+0x1d8], jmp cull");
    }
    small::after_reset(kRowsWidth);
    for (unsigned k = 0; k < kRowsExpectedCount; ++k) {
        check(small::set_px(kRowsExpected[k].px), "bodies: set_px in band");
        small::begin_frame();
        const std::int32_t threshold = kRowsExpected[k].threshold_s;
        check(x3m_cull_small_parts_threshold == threshold, "bodies: threshold for the px class");
        std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
        SetLastError(0x5151);
        patched = run(replay[0], view);
        check(GetLastError() == 0x5151, "bodies: LastError preserved across the armed pass");
        const Flip f = replay_compare(replay_native);
        check(patched.preserved && patched.x87_empty && same_outputs(patched, native) && f.other_changes == 0 && replay_flipped_exactly(replay_native, threshold, true), "bodies: only parentless kept nodes below the threshold flip, and every one of them");
        check(f.flipped == kRowsExpected[k].bodies_nodes && f.draws == kRowsExpected[k].bodies_draws && f.flipped < kRowsExpected[k].nodes, "bodies: node and draw counts as tracked, fewer than scope all");
        check(x3m_cull_small_parts_culled == rows_below(threshold, true) && rows_below(threshold, true) < rows_below(threshold), "bodies: the stub's count is every parentless node below the threshold");
        std::printf("REPLAY scope=bodies px=%g threshold=%ld flipped=%u draws=%u other_changes=%u culled_count=%lu parented_below=%u\n", kRowsExpected[k].px, (long)threshold, f.flipped, f.draws, f.other_changes,
                    (unsigned long)x3m_cull_small_parts_culled, rows_below(threshold) - rows_below(threshold, true));
        small::present(7, 6000 + k, false);
    }
    check(kRowsExpected[0].bodies_nodes == 89 && kRowsExpected[0].bodies_draws == 395 && kRowsExpected[1].bodies_nodes == 120 && kRowsExpected[1].bodies_draws == 450, "bodies: 89 nodes / 395 draws at 2 px, 120 / 450 at 4 px");
    // bodies beside the census: the rows name the scope, a parented node below the threshold keeps the engine's verdict
    check(census::install_at(addr(synthetic_measure_window + ccore::measure_site_offset), addr(synthetic_exit_window + ccore::exit_site_offset)), "census re-installed beside the bodies stub");
    check(small::set_px(2.0), "bodies: back to 2 px");
    census::begin_frame(true); small::begin_frame();
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    run(replay[0], view);
    census::present(7, 4994, true); small::present(7, 4994, true);
    {
        unsigned small_verdicts = 0, scoped = 0, scoped_all = 0, kept = 0;
        for (const std::string& line : census_entry_lines) {
            if (line.find("verdict=culled_small") != std::string::npos) { ++small_verdicts; if (line.find("verdict=culled_small scope=bodies") != std::string::npos) ++scoped; }
            if (line.find(" scope=all") != std::string::npos) ++scoped_all;
            if (line.find("verdict=kept") != std::string::npos) ++kept;
        }
        check(census_entry_lines.size() == kRowCount && small_verdicts == 89 && scoped == 89 && scoped_all == 0 && kept == 164 - 89, "bodies beside the census: 89 rows `culled_small scope=bodies`, the 8 parented ones kept");
        check(!small_lines.empty() && small_lines.back().find(" scope=bodies") != std::string::npos, "bodies: the frame row carries the scope");
        census_frame_lines.clear(); census_entry_lines.clear();
    }
    check(census::shutdown() && census_windows_original(), "census restored again");
    // the bench tree: every node below threshold 20 is parented, so the bodies stub leaves the tree native
    {
        reset_tree(); run(R, bench_view);
        Node native_tree[all_count]; for (unsigned i = 0; i < all_count; ++i) native_tree[i] = *all[i];
        x3m_cull_small_parts_threshold = 20; x3m_cull_small_parts_culled = 0;
        reset_tree(); const Result bodies_bench = run(R, bench_view);
        x3m_cull_small_parts_threshold = 0; reset_tree(); const Result native_bench_patched = run(R, bench_view);
        bool same = true; x3m_cull_small_parts_threshold = 20; reset_tree(); run(R, bench_view);
        for (unsigned i = 0; i < all_count; ++i) if (std::memcmp(all[i]->bytes, native_tree[i].bytes, sizeof(Node))) same = false;
        x3m_cull_small_parts_threshold = 0;
        check(same && (get(E.bytes, 0x12c) & 2) && (get(F.bytes, 0x12c) & 2) && (get(J.bytes, 0x12c) & 2), "bodies, bench tree at threshold 20: parented E, F, J stay; every node as native");
        check(bodies_bench.preserved && bodies_bench.x87_empty && same_outputs(bodies_bench, native_bench_patched), "bodies, bench tree: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as the disarmed pass");
    }
    check(small::shutdown() && small_window_original(), "bodies: restore, rollback bytes exact");
    x3m::engine_patch::close_install_window("fixture");
    check(!small::install_at(site, cull, false) && !std::strcmp(small::state(), "late_claim") && small_window_original(), "closed install window: late_claim, site untouched");
    std::printf("CULL SMALL PARTS CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
