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
// 479 (scope `all`, the only scope since `bodies` was removed on 2026-09-25);
// the census armed together with the stub reports
// the flipped nodes as renderable=0 with verdict culled_small and the scope;
// rows marked as projectiles (+0x130 |= 0x20000000, the engine's class-0 marker)
// below the threshold run the vanilla compare with the exemption on (counted in
// exempt_bullet= and the census's culled_small_exempt_bullet=) and are culled
// like any node with it off; callee-saved registers, ESP, the
// empty x87 stack and LastError preserved; exact rollback; option off,
// changed bytes and the closed window refused. The small-prop draw skip's
// decision (X3M_CULL_SMALL_PROPS, cull_small_props_core.h) over a synthetic
// engine image: a prop below the threshold skipped, above it drawn, the own
// ship's and the target's props drawn, fail-closed cases, the walk budget, and
// the census's culled_prop verdict. Carrier dock-port parts
// (X3M_CULL_DOCK_PARTS_PX, docs/reverse-engineering/ship-scene-parts.md): an
// 18-node tree with model ids inside, on and one past each bound of
// [901300000, 901499999] and [909800000, 909999999] at s = 3..12 against
// thresholds 5 (4 px) and 10 (8 px): exactly the in-range nodes with
// 5 <= s < 10 flip (dock_culled=), out-of-range ids follow the 4 px rule only,
// the dock rule off at 0 and at a dock setting below the small one, a marked
// projectile exempt (culled with the exemption off), the census names
// culled_dock, registers/flags/LastError preserved, native after rollback.
// Far engine jets (X3M_ENGINE_EFFECTS=plumes, 2026-10-03): every fifth row
// carries the engine's JET flag pair (+0x130 |= 0x4000001), others bit 26 or
// bit 0 alone. Without the far block (any other engine_effects mode, the
// default) nothing is handed over; installed with it, every JET row below the
// threshold stays culled (the same 97-node / 403-draw class flips: the engine
// never submits it) and is handed to x3m_engine_far_jet: disarmed nothing is
// copied, armed exactly the pair rows the engine itself would keep are copied
// (the engine-culled ones counted, single-bit rows and v/00566 never), with the
// view's handle and context, the scene tag, and a record (far_record) with the
// expected origin, axis, size and throttle; the buffer's cap; registers, flags
// and LastError preserved across the call; the per-node cost with and without
// a far record; initialize() requests the block only for exactly `plumes` and
// leaves it out when the JET writer 0x00434708 is not the verified bytes.
// The lens-flare cull (X3M_LENS_FLARE_GAIN=0,
// src/proxy/lens_flare_cull.cpp): the second stub on the same claim, its body-name
// resolution over a synthetic body table, the flare bodies culled exactly as the
// engine's own size cull, both stubs chained in both orders (lens_section).
// Diagnostic timings only; not game FPS. Never launches the game.
#include "../../src/proxy/cull_small_parts.h"
#include "../../src/proxy/cull_small_parts_core.h"
#include "../../src/proxy/cull_census.h"
#include "../../src/proxy/cull_census_core.h"
#include "../../src/proxy/camera_state.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/proxy/cull_small_props_core.h"
#include "../../src/proxy/lens_flare_cull.h"
#include "../../src/proxy/lens_flare_cull_core.h"
#include "../../src/proxy/occlusion_engine_cull.h"
#include "../../src/proxy/occlusion_engine_core.h"
#include "../../src/proxy/engine_far_jets.h"
#include "../../src/proxy/engine_effects_core.h"
#include "run131_rows_inc.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdarg>
#include <string>
#include <vector>
static std::vector<std::string> census_frame_lines, census_entry_lines, small_lines, install_lines, lens_lines;
namespace x3m {
void log(const char* format, ...) {
    char text[512];
    std::va_list a;
    va_start(a, format);
    std::vsnprintf(text, sizeof text, format, a);
    va_end(a);
    if (!std::strncmp(text, "cull_census_frame ", 18)) {
        census_frame_lines.emplace_back(text);
        return;
    }
    if (!std::strncmp(text, "cull_census device=", 19)) {
        census_entry_lines.emplace_back(text);
        return;
    }
    if (!std::strncmp(text, "cull_small_parts_frame ", 23) || !std::strncmp(text, "cull_small_parts_value ", 23)) {
        small_lines.emplace_back(text);
    }
    if (!std::strncmp(text, "cull_small_parts requested=", 27)) install_lines.emplace_back(text);
    if (!std::strncmp(text, "lens_flare_cull", 15)) lens_lines.emplace_back(text);
    static unsigned lines = 0;
    if (lines++ < 12) std::printf("%s\n", text);
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return true;
}
}
// The camera latch seam: the fixture supplies the projection scale the
// production begin_frame() would read from the engine's buffer.
static bool fixture_camera_available = false;
static float fixture_camera_m00 = 0, fixture_camera_m11 = 0;
static bool fixture_camera_valid = false;
namespace x3m::camera_state {
bool available() {
    return fixture_camera_available;
}
const char* status() {
    return fixture_camera_available ? "fixture" : "disabled";
}
bool read(Sample* out) {
    *out = Sample{};
    if (!fixture_camera_available) {
        out->read_failure = Unavailable;
        return false;
    }
    out->state.valid = fixture_camera_valid;
    out->state.m00 = fixture_camera_m00;
    out->state.m11 = fixture_camera_m11;
    return fixture_camera_valid;
}
}
// The FOV seam: the engine's base focus the production begin_frame() falls
// back to through fov::current_focus() (registry+0x24, else the --fov value)
// when the latched projection carries no usable P[5]; the count shows the
// projection path reads nothing.
static std::uint32_t fixture_focus = 0x4000;
static unsigned fixture_focus_reads = 0;
namespace x3m::fov {
std::uint32_t current_focus() {
    ++fixture_focus_reads;
    return fixture_focus;
}
}
namespace small = x3m::cull_small_parts;
namespace lens = x3m::lens_flare_cull;
namespace lcore = x3m::lens_flare_cull::core;
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
extern "C" unsigned char synthetic_measure_window[], synthetic_small_window[], synthetic_degenerate[],
    synthetic_exit_window[];

// ---- harness: a thiscall of the synthetic pass with sentinel registers, PUSHAD/EFLAGS/ESP/x87 captured at the return
// ----
extern "C" {
struct Frame {
    std::uint32_t node, view, flag, entry_esp;
    std::uint32_t out[9]; // PUSHAD order edi,esi,ebp,esp,ebx,edx,ecx,eax then EFLAGS, at the return
    std::uint32_t exit_esp;
    std::uint32_t x87env[7];
    std::uint32_t entry_ebp;
};
Frame* fixture_frame = nullptr;
void fixture_call(Frame* frame);
}
static_assert(offsetof(Frame, out) == 16 && offsetof(Frame, exit_esp) == 52 && offsetof(Frame, x87env) == 56 &&
                  offsetof(Frame, entry_ebp) == 84,
              "frame layout");
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
static void check(bool okay, const char* label) {
    ++checks;
    if (!okay) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}

// ---- synthetic records ----
struct alignas(16) Node {
    unsigned char bytes[0x240];
};
struct alignas(16) View {
    unsigned char bytes[0x400];
};
alignas(16) static std::uint32_t sentinel[4] = {0, 0, 0, 0};
static std::uint32_t addr(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
static void put(void* base, unsigned off, std::uint32_t v) {
    std::memcpy(static_cast<unsigned char*>(base) + off, &v, 4);
}
static std::uint32_t get(const void* base, unsigned off) {
    std::uint32_t v;
    std::memcpy(&v, static_cast<const unsigned char*>(base) + off, 4);
    return v;
}
constexpr unsigned ladder_offset = 0x1f0, ladder_count_offset = 0x1fc, d_offset = 0xf0, first_child_offset = 0xc,
                   view_w = 0x5c, view_flags = 0x270, view_scale = 0x298, view_distance = 0x300;
static void node_set(Node& n, Node* parent, std::int32_t radius, std::int32_t d, std::uint32_t flags,
                     std::int32_t thr_1d8, std::int32_t thr_1dc, std::uint32_t model, unsigned lods = 1,
                     std::int32_t t1 = 0, std::int32_t t2 = 0, std::int32_t t3 = 0) {
    std::memset(n.bytes, 0, sizeof n.bytes);
    put(n.bytes, 0, addr(sentinel));
    put(n.bytes, first_child_offset, addr(sentinel));
    put(n.bytes, ccore::parent_offset, parent ? addr(parent) : 0);
    put(n.bytes, ccore::radius_offset, std::uint32_t(radius));
    put(n.bytes, d_offset, std::uint32_t(d));
    put(n.bytes, ccore::flags12c_offset, flags);
    put(n.bytes, ccore::threshold_1d8_offset, std::uint32_t(thr_1d8));
    put(n.bytes, ccore::threshold_1dc_offset, std::uint32_t(thr_1dc));
    put(n.bytes, ccore::model_offset, model);
    put(n.bytes, ladder_count_offset, lods);
    put(n.bytes, ladder_offset, std::uint32_t(t1));
    put(n.bytes, ladder_offset + 4, std::uint32_t(t2));
    put(n.bytes, ladder_offset + 8, std::uint32_t(t3));
}
// Links children[0..n) as the traversal children of parent (sibling chain at +0, first child at +0xc); +0x18 is left as
// set.
static void link_traversal(Node& parent, Node* const* children, unsigned count) {
    put(parent.bytes, first_child_offset, count ? addr(children[0]) : addr(sentinel));
    for (unsigned i = 0; i < count; ++i)
        put(children[i]->bytes, 0, i + 1 < count ? addr(children[i + 1]) : addr(sentinel));
}
static void view_set(View& v, std::int32_t w, std::uint32_t flags, std::int32_t scale, std::int32_t distance) {
    std::memset(v.bytes, 0, sizeof v.bytes);
    put(v.bytes, view_w, std::uint32_t(w));
    put(v.bytes, view_flags, flags);
    put(v.bytes, view_scale, std::uint32_t(scale));
    put(v.bytes, view_distance, std::uint32_t(distance));
}
struct Result {
    std::uint32_t edi, esi, ebp, ebx, edx, ecx, eax, flags;
    bool preserved, x87_empty;
};
static Result run(Node& root, View& view, std::uint32_t flag = 0) {
    Frame f{};
    f.node = addr(&root);
    f.view = addr(&view);
    f.flag = flag;
    fixture_call(&f);
    Result r{};
    r.edi = f.out[0];
    r.esi = f.out[1];
    r.ebp = f.out[2];
    r.ebx = f.out[4];
    r.edx = f.out[5];
    r.ecx = f.out[6];
    r.eax = f.out[7];
    r.flags = f.out[8] & 0x8d5;
    r.preserved = r.edi == 0xd1d1d1d1u && r.esi == 0x5e5e5e5eu && r.ebx == 0x0b0b0b0bu && r.ebp == f.entry_ebp &&
                  f.exit_esp == f.entry_esp;
    r.x87_empty = ((f.x87env[1] >> 11) & 7) == 0 && (f.x87env[2] & 0xffff) == 0xffff;
    if (!r.preserved || !r.x87_empty)
        std::printf("DETAIL edi=%08lx esi=%08lx ebx=%08lx ebp=%08lx/%08lx esp=%08lx/%08lx status=%04lx tag=%04lx\n",
                    (unsigned long)r.edi, (unsigned long)r.esi, (unsigned long)r.ebx, (unsigned long)r.ebp,
                    (unsigned long)f.entry_ebp, (unsigned long)f.exit_esp, (unsigned long)f.entry_esp,
                    (unsigned long)(f.x87env[1] & 0xffff), (unsigned long)(f.x87env[2] & 0xffff));
    return r;
}
static bool same_outputs(const Result& a, const Result& b) {
    return a.eax == b.eax && a.ecx == b.ecx && a.edx == b.edx && a.flags == b.flags;
}
// The engine's verdict from a node's final flags and the row's fields (cull_census classify without the stub).
static int verdict_of(const Node& n, const RowData& r) {
    if (get(n.bytes, ccore::flags12c_offset) & 2u) return 0;
    if (r.limit > 0 && r.measure < r.limit) return 1;
    if (r.measure < 1 && !(r.flags_in & 0x4000000u)) return 2;
    return 3;
}
struct Row {
    unsigned long device, frame, view, node, model, flags_in, flags_out;
    long s, measure, d, radius, thr_1dc, thr_1d8, limit, lod;
    char verdict[24];
};
static bool parse_row(const std::string& line, Row& r) {
    return std::sscanf(
               line.c_str(),
               "cull_census device=%lu frame=%lu view=%lx node=%lx model=%lx s=%ld measure=%ld d=%ld radius=%ld thr_1dc=%ld thr_1d8=%ld limit=%ld flags_in=%lx flags_out=%lx lod=%ld verdict=%23s",
               &r.device, &r.frame, &r.view, &r.node, &r.model, &r.s, &r.measure, &r.d, &r.radius, &r.thr_1dc,
               &r.thr_1d8, &r.limit, &r.flags_in, &r.flags_out, &r.lod, r.verdict) == 16;
}

static const unsigned char* small_site() {
    return synthetic_small_window + score::site_offset;
}
static const unsigned char* small_cull() {
    return synthetic_small_window + score::cull_offset;
}
static bool small_window_original() {
    return !std::memcmp(synthetic_small_window, score::window, score::window_length);
}
static bool census_windows_original() {
    return !std::memcmp(synthetic_measure_window, ccore::measure_window, ccore::measure_window_length) &&
           !std::memcmp(synthetic_exit_window, ccore::exit_window, ccore::exit_window_length);
}

// ---- the replay tree: a hidden root (early exit, unmeasured) with every census row as a child ----
static Node* replay = nullptr;  // [0] root, [1..kRowCount] the rows
static Node* parents = nullptr; // per row: the parent record carrying +0x1d8 = limit when the recorded limit exceeds
                                // the own threshold (proven parent), or +0x1d8 = the own threshold (limit unchanged)
                                // for a row without a body flag (synthetic parent: the rows carry no +0x18)
static Node* replay_native = nullptr;
static void replay_build(View& view) {
    view_set(view, std::int32_t(kRowsMeasureReference), 0, std::int32_t(kRowsViewScale), 2);
    node_set(replay[0], nullptr, 1, 100000, 0x1000, 0, 0, 0xffff); // renderable bit clear: exits before the measure
                                                                   // site
    std::vector<Node*> kids;
    kids.reserve(kRowCount);
    for (unsigned i = 0; i < kRowCount; ++i) {
        const RowData& r = kRows[i];
        Node& n = replay[1 + i];
        node_set(n, nullptr, r.radius, r.d, r.flags_in, r.thr_1d8, r.thr_1dc, 0x10000 + i);
        if (r.limit > r.thr_1d8 || !(r.flags_in & kRowsBodyFlagsMask)) {
            std::memset(parents[i].bytes, 0, sizeof parents[i].bytes);
            put(parents[i].bytes, ccore::threshold_1d8_offset,
                std::uint32_t(r.limit > r.thr_1d8 ? r.limit : r.thr_1d8));
            put(n.bytes, ccore::parent_offset, addr(&parents[i]));
        }
        kids.push_back(&n);
    }
    link_traversal(replay[0], kids.data(), kRowCount);
}
static void replay_reset() {
    std::memcpy(replay, replay_native, sizeof(Node) * (kRowCount + 1));
}
// Runs the replay and compares every row node with the native reference: nodes
// whose bytes differ only by a cleared renderable bit are "flipped"; any other
// difference is counted. Returns the flipped count, their draws and the others.
struct Flip {
    unsigned flipped, draws, other_changes;
};
static Flip replay_compare(const Node* reference) {
    Flip f{};
    for (unsigned i = 0; i < kRowCount; ++i) {
        const Node& now = replay[1 + i];
        const Node& ref = reference[1 + i];
        if (!std::memcmp(now.bytes, ref.bytes, sizeof(Node))) continue;
        Node expect = ref;
        put(expect.bytes, ccore::flags12c_offset, get(ref.bytes, ccore::flags12c_offset) & ~2u);
        if ((get(ref.bytes, ccore::flags12c_offset) & 2u) && !std::memcmp(now.bytes, expect.bytes, sizeof(Node))) {
            ++f.flipped;
            f.draws += unsigned(kRows[i].draws);
        } else
            ++f.other_changes;
    }
    return f;
}
// Rows the stub sends down the cull path: every measured node with s below the threshold, whether or not the engine's
// own tests would have culled it.
static unsigned rows_below(std::int32_t threshold) {
    unsigned n = 0;
    for (unsigned i = 0; i < kRowCount; ++i)
        if (kRows[i].s < threshold) ++n;
    return n;
}
// Whether the flipped set is exactly {kept rows with s < threshold}.
static bool replay_flipped_exactly(const Node* reference, std::int32_t threshold) {
    for (unsigned i = 0; i < kRowCount; ++i) {
        const bool kept_native = (get(reference[1 + i].bytes, ccore::flags12c_offset) & 2u) != 0;
        const bool kept_now = (get(replay[1 + i].bytes, ccore::flags12c_offset) & 2u) != 0;
        const bool expect_flip = kept_native && kRows[i].s < threshold;
        if (kept_now != (kept_native && !expect_flip)) return false;
    }
    return true;
}

// Projectile marking: every third row carries the engine's class-0 marker in +0x130 (the synthetic pass never
// touches that bit, so the native result of a marked tree is replay_native with the same bit set).
static bool row_marked(unsigned i) {
    return i % 3 == 0;
}
static void mark_rows(Node* tree) {
    for (unsigned i = 0; i < kRowCount; ++i)
        if (row_marked(i))
            put(tree[1 + i].bytes, score::flags130_offset,
                get(tree[1 + i].bytes, score::flags130_offset) | score::projectile_flag);
}
static void unmark_rows(Node* tree) {
    for (unsigned i = 0; i < kRowCount; ++i)
        if (row_marked(i))
            put(tree[1 + i].bytes, score::flags130_offset,
                get(tree[1 + i].bytes, score::flags130_offset) & ~score::projectile_flag);
}
static unsigned rows_below_marked(std::int32_t threshold) {
    unsigned n = 0;
    for (unsigned i = 0; i < kRowCount; ++i)
        if (kRows[i].s < threshold && row_marked(i)) ++n;
    return n;
}
// Whether exactly the unmarked kept rows below the threshold flipped (the marked ones keep the engine's verdict).
static bool replay_flipped_exactly_unmarked(const Node* reference, std::int32_t threshold) {
    for (unsigned i = 0; i < kRowCount; ++i) {
        const bool kept_native = (get(reference[1 + i].bytes, ccore::flags12c_offset) & 2u) != 0;
        const bool kept_now = (get(replay[1 + i].bytes, ccore::flags12c_offset) & 2u) != 0;
        if (kept_now != (kept_native && !(kRows[i].s < threshold && !row_marked(i)))) return false;
    }
    return true;
}
static unsigned kept_marked_below(const Node* reference, std::int32_t threshold, unsigned* draws) {
    unsigned n = 0;
    *draws = 0;
    for (unsigned i = 0; i < kRowCount; ++i)
        if (row_marked(i) && kRows[i].s < threshold && (get(reference[1 + i].bytes, ccore::flags12c_offset) & 2u)) {
            ++n;
            *draws += unsigned(kRows[i].draws);
        }
    return n;
}

// Far engine jets: rows i % 5 == 1 carry the JET flag pair, i % 5 == 2 bit 26 alone (SMALLJET's), i % 5 == 3 bit 0
// alone (the synthetic pass, like the engine's entry mask ~0x180000, never touches these bits).
static std::uint32_t jet_bits(unsigned i) {
    return i % 5 == 1 ? score::jet_flags : i % 5 == 2 ? score::jet_flag_high : i % 5 == 3 ? score::jet_flag_low : 0u;
}
static bool row_jet(unsigned i) {
    return i % 5 == 1;
}
static void set_jet_bits(Node* tree, bool on) {
    for (unsigned i = 0; i < kRowCount; ++i) {
        const std::uint32_t b = jet_bits(i), f = get(tree[1 + i].bytes, score::flags130_offset);
        if (b) put(tree[1 + i].bytes, score::flags130_offset, on ? f | b : f & ~b);
    }
}
static unsigned rows_below_jet(std::int32_t threshold) {
    unsigned n = 0;
    for (unsigned i = 0; i < kRowCount; ++i)
        if (kRows[i].s < threshold && row_jet(i)) ++n;
    return n;
}
// Pair rows below the threshold that the engine keeps natively (the handler's copies) and their first index.
static unsigned kept_jet_below(const Node* reference, std::int32_t threshold, int* first = nullptr) {
    unsigned n = 0;
    if (first) *first = -1;
    for (unsigned i = 0; i < kRowCount; ++i)
        if (row_jet(i) && kRows[i].s < threshold && (get(reference[1 + i].bytes, ccore::flags12c_offset) & 2u)) {
            if (first && *first < 0) *first = int(i);
            ++n;
        }
    return n;
}

// ---- the 12-node bench tree of the census fixture ----
static Node R, A, B, C, E, F, G, H, I, J, gA1, gA2;
static Node* const all[] = {&R, &A, &B, &C, &E, &F, &G, &H, &I, &J, &gA1, &gA2};
constexpr unsigned all_count = sizeof all / sizeof all[0];
static Node initial[all_count];
static void reset_tree() {
    for (unsigned i = 0; i < all_count; ++i) *all[i] = initial[i];
}
static void link_parented(Node& parent, Node* const* children, unsigned count) {
    link_traversal(parent, children, count);
    for (unsigned i = 0; i < count; ++i) put(children[i]->bytes, ccore::parent_offset, addr(&parent));
}
// ---- the carrier dock-port tree (X3M_CULL_DOCK_PARTS_PX): a hidden root and 18 children at D = 64000 in a W = 1280,
// F = 0x4000 view, so s = radius / 100 and measure = 2 s (no engine limit, one LOD record: kept natively). At 4 px and
// 8 px with m00 0.8 at 1280 (0.8 px per s) the thresholds are 5 and 10. ----
struct DockCase {
    std::uint32_t model;
    std::int32_t s;
    bool marked;
    char expect; // 's' culled by the small rule, 'd' by the dock rule, 'e' exempt projectile (kept), 'k' kept
};
static const DockCase dock_cases[] = {
    {901300003u, 3, false, 's'},  {901300003u, 7, false, 'd'},  {901300003u, 12, false, 'k'}, {901400003u, 9, false, 'd'},
    {909900005u, 5, false, 'd'},  {901300000u, 7, false, 'd'},  {901499999u, 7, false, 'd'},  {909800000u, 7, false, 'd'},
    {909999999u, 7, false, 'd'},  {901299999u, 7, false, 'k'},  {901500000u, 7, false, 'k'},  {909799999u, 7, false, 'k'},
    {910000000u, 7, false, 'k'},  {901299999u, 3, false, 's'},  {910000000u, 4, false, 's'},  {901300003u, 10, false, 'k'},
    {901300003u, 7, true, 'e'},   {0x5001u, 7, false, 'k'}};
constexpr unsigned dock_count = sizeof dock_cases / sizeof dock_cases[0];
static Node dock_tree[dock_count + 1], dock_initial[dock_count + 1], dock_native[dock_count + 1];
static View dock_view;
static Result dock_native_result{};
static void dock_build() {
    view_set(dock_view, 1280, 0, 0x4000, 2);
    node_set(dock_tree[0], nullptr, 1, 100000, 0x1000, 0, 0, 0xffff); // renderable bit clear: exits before the site
    Node* kids[dock_count];
    for (unsigned i = 0; i < dock_count; ++i) {
        node_set(dock_tree[1 + i], nullptr, dock_cases[i].s * 100, 64000, 0x1002, 0, 0, dock_cases[i].model);
        if (dock_cases[i].marked) put(dock_tree[1 + i].bytes, score::flags130_offset, score::projectile_flag);
        kids[i] = &dock_tree[1 + i];
    }
    link_traversal(dock_tree[0], kids, dock_count);
    std::memcpy(dock_initial, dock_tree, sizeof dock_tree);
}
static Result dock_run() {
    std::memcpy(dock_tree, dock_initial, sizeof dock_tree);
    return run(dock_tree[0], dock_view);
}
// Whether exactly the nodes whose class is in `flip` lost the renderable bit and nothing else changed.
static bool dock_flipped_exactly(const char* flip) {
    for (unsigned i = 0; i < dock_count; ++i) {
        const Node& now = dock_tree[1 + i];
        Node expect = dock_native[1 + i];
        if (std::strchr(flip, dock_cases[i].expect))
            put(expect.bytes, ccore::flags12c_offset, get(expect.bytes, ccore::flags12c_offset) & ~2u);
        if (std::memcmp(now.bytes, expect.bytes, sizeof(Node))) return false;
    }
    return !std::memcmp(dock_tree[0].bytes, dock_native[0].bytes, sizeof(Node));
}
static unsigned dock_class_count(char c) {
    unsigned n = 0;
    for (unsigned i = 0; i < dock_count; ++i) n += dock_cases[i].expect == c;
    return n;
}

static double bench_us(Node& root, View& view, unsigned loops = 20000) {
    LARGE_INTEGER f{}, s{}, e{};
    QueryPerformanceFrequency(&f);
    auto once = [&] {
        reset_tree();
        run(root, view);
    };
    for (unsigned i = 0; i < 256; ++i) once();
    QueryPerformanceCounter(&s);
    for (unsigned i = 0; i < loops; ++i) once();
    QueryPerformanceCounter(&e);
    return double(e.QuadPart - s.QuadPart) * 1e6 / double(f.QuadPart) / double(loops);
}
static float bits_to_float(std::uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// ---- small props (X3M_CULL_SMALL_PROPS, src/proxy/cull_small_props_core.h) ----
// The draw path's decision over a synthetic engine image read through the production engine_memory::read: a body
// table (ids 5 ships\props\..., 6 a hull, 7 the prop prefix in upper case with '/', 8 a null name, 9 a name shorter
// than the prefix), the cockpit registry (own ship and target, object_capture.h), three ships with prop children and
// one mesh part whose AABB is the unit cube (x65536). Clip rows: x, y, z through, w = z + d; at 5120x1440 and d = 2000
// the box spans 2.56 px (radius 1.28, below 4 px), at d = 200 25.7 px (radius 12.9, above).
namespace pcore = x3m::cull_small_props::core;
struct alignas(16) Blob {
    unsigned char bytes[0x200];
};
static bool engine_read(std::uintptr_t p, void* out, std::size_t n) {
    return x3m::engine_memory::read(p, out, n);
}
static void props_section() {
    static Blob mgr, registry, table, buckets, link, cockpit, own_obj, target_obj, desc, part;
    static Blob own_root, own_prop, target_root, target_prop, far_root, far_prop, far_hull, far_dummy, far_null,
        far_short, far_nobounds, far_norows;
    static Blob many[70];
    static std::uint32_t body_global_var = 0, cockpit_slot_var = 0;
    alignas(16) static unsigned char slots[16 * 0x1c];
    static const char n_prop[] = "ships\\props\\split_m1turretB_base", n_hull[] = "ships\\split\\split_m7_cobra\\hull",
                      n_dummy[] = "SHIPS/Props/weapondummy", n_short[] = "ships\\pr";
    put(slots, 5 * 0x1c + 0x0c, addr(n_prop));
    put(slots, 6 * 0x1c + 0x0c, addr(n_hull));
    put(slots, 7 * 0x1c + 0x0c, addr(n_dummy));
    put(slots, 8 * 0x1c + 0x0c, 0);
    put(slots, 9 * 0x1c + 0x0c, addr(n_short));
    put(mgr.bytes, 0xb4, 11000);
    put(mgr.bytes, 0xb8, 16);
    put(mgr.bytes, 0xbc, addr(slots));
    body_global_var = addr(&mgr);
    cockpit_slot_var = addr(&registry);
    put(registry.bytes, 0, addr(&table));
    put(registry.bytes, 0x10, 3);
    put(table.bytes, 0, addr(&buckets));
    put(table.bytes, 4, 4);
    put(buckets.bytes, 12, addr(&link));
    put(link.bytes, 4, 3);
    put(link.bytes, 8, addr(&cockpit));
    put(cockpit.bytes, 0xc, addr(&own_obj));
    put(cockpit.bytes, 0x58, 0x1234);
    put(cockpit.bytes, 0x1e0, addr(&target_obj));
    put(own_obj.bytes, 0x70, addr(&own_root));
    put(target_obj.bytes, 8, 77);
    put(target_obj.bytes, 0x70, addr(&target_root));
    const auto node = [](Blob& n, Blob* parent, std::uint32_t handle, std::uint32_t model) {
        put(n.bytes, 0x18, parent ? addr(parent) : 0);
        put(n.bytes, 0x28, handle);
        put(n.bytes, 0x140, model);
    };
    node(own_root, nullptr, 0x111, 6);
    node(own_prop, &own_root, 0x112, 5);
    node(target_root, nullptr, 0x222, 6);
    node(target_prop, &target_root, 0x223, 5);
    node(far_root, nullptr, 0x333, 6);
    node(far_prop, &far_root, 0x334, 5);
    node(far_hull, &far_root, 0x335, 6);
    node(far_dummy, &far_root, 0x336, 7);
    node(far_null, &far_root, 0x337, 8);
    node(far_short, &far_root, 0x338, 9);
    node(far_nobounds, &far_root, 0x339, 5);
    node(far_norows, &far_root, 0x33a, 5);
    for (unsigned i = 0; i < 70; ++i) node(many[i], &far_root, 0x400 + i, 5);
    put(desc.bytes, 0, addr(&part));
    for (unsigned i = 0; i < 3; ++i) {
        put(part.bytes, 0x40 + 4 * i, 0);
        put(part.bytes, 0x50 + 4 * i, 9 * 65536); // the engine's part box: 9x the drawn range (the run376 gap)
    }
    // A descriptor on a released page: its part cannot be read.
    void* page = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    const std::uint32_t bad_desc = addr(page);
    check(page && VirtualFree(page, 0, MEM_RELEASE), "props: released descriptor page");
    const float far_rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 2000};
    const float near_rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 200};
    const float behind_rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0.5f};
    const unsigned W = 5120, H = 1440;
    pcore::Addresses a;
    a.body_global = addr(&body_global_var);
    a.cockpit_slot = addr(&cockpit_slot_var);
    static pcore::Culler c;
    c.px = 4.f;
    x3m::engine_memory::next_frame();
    using V = pcore::Verdict;
    // The draw's vertex extent (production: the shadow-replay extent cache): the unit cube; null = not known.
    const pcore::Box unit{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
    const auto eval = [&](Blob& n, const pcore::Box* extent, const float* rows, bool* first = nullptr) {
        return c.evaluate(engine_read, a, addr(&n), [extent]() { return extent; }, rows, W, H, first);
    };
    // Regression (run376, Run 105 A): the engine's mesh-part box behind the descriptor projects above the threshold
    // (11.6 px) while the drawn range is 1.28 px; the decision must follow the drawn range.
    {
        pcore::Box engine{};
        float engine_px = 0.f;
        check(pcore::Culler::engine_part_box(engine_read, addr(&desc), engine) &&
                  pcore::screen_radius(far_rows, engine, W, H, &engine_px) && engine_px > 11.f && engine_px < 12.f &&
                  !pcore::Culler::engine_part_box(engine_read, bad_desc, engine),
              "props: the engine part box reads 11.6 px (above 4) and an unreadable descriptor none");
        std::printf("PROPS engine_part_px=%.3f\n", double(engine_px));
    }
    {
        pcore::Box box{};
        std::int32_t raw[7] = {0, 0, 0, 0, 65536, 65536, 65536};
        float far_r = 0, near_r = 0;
        check(pcore::part_box(raw, box) && pcore::screen_radius(far_rows, box, W, H, &far_r) && far_r > 1.27f &&
                  far_r < 1.29f,
              "props: the far unit box measures 1.28 px");
        check(pcore::screen_radius(near_rows, box, W, H, &near_r) && near_r > 12.8f && near_r < 12.9f,
              "props: the near unit box measures 12.9 px");
        std::printf("PROPS radius_far_px=%.3f radius_near_px=%.3f threshold_px=4\n", double(far_r), double(near_r));
    }
    c.begin_frame(1);
    bool first = false, again = true;
    const V below = eval(far_prop, &unit, far_rows, &first);
    const V memo = eval(far_prop, &unit, far_rows, &again);
    check(below == V::culled && first && memo == V::culled && !again,
          "props: a far prop whose drawn range is 1.28 px is skipped although its engine part box is 11.6 px; its "
          "second draw in the frame shares the verdict");
    check(eval(far_hull, &unit, far_rows) == V::not_prop, "props: a hull body below the threshold is drawn");
    check(eval(far_dummy, &unit, far_rows) == V::culled, "props: SHIPS/Props/ matches the prefix");
    check(eval(far_null, &unit, far_rows) == V::not_prop && eval(far_short, &unit, far_rows) == V::not_prop,
          "props: a null name (the engine's v\\%05d) and a name shorter than the prefix are not props");
    check(eval(own_prop, &unit, far_rows) == V::exempt_own, "props: a prop on the player's ship is drawn");
    check(eval(target_prop, &unit, far_rows) == V::exempt_target, "props: a prop on the current target is drawn");
    check(eval(far_nobounds, nullptr, far_rows) == V::no_bounds, "props: a draw whose extent is not known is drawn");
    check(eval(far_norows, &unit, nullptr) == V::unbounded, "props: a draw without clip rows is drawn");
    check(c.window.draws == 7 && c.window.culled == 3 && c.window.kept == 4 && c.window.nodes_culled == 2 &&
              c.window.exempt_own == 1 && c.window.exempt_target == 1 && c.window.no_bounds == 1 &&
              c.window.unbounded == 1,
          "props: window counts (7 prop draws, 3 skipped over 2 nodes, 4 drawn)");
    c.begin_frame(2);
    check(eval(far_prop, &unit, near_rows) == V::kept_size, "props: the same prop above the threshold is drawn");
    c.begin_frame(3);
    check(eval(far_prop, &unit, behind_rows) == V::unbounded,
          "props: a box reaching behind the eye plane has no size and is drawn");
    // The own ship / target unknown (registry unreadable): fail closed, nothing is skipped.
    put(registry.bytes, 0, 0);
    c.begin_frame(4);
    check(eval(far_prop, &unit, far_rows) == V::unresolved, "props: unresolved own ship: drawn");
    put(registry.bytes, 0, addr(&table));
    // No target: the former target's prop is an ordinary prop again (the ancestry cache follows the roots).
    put(cockpit.bytes, 0x1e0, 0);
    c.begin_frame(5);
    const V former = eval(target_prop, &unit, far_rows), own_v = eval(own_prop, &unit, far_rows),
            far_v = eval(far_prop, &unit, far_rows);
    check(former == V::culled && own_v == V::exempt_own && far_v == V::culled,
          "props: without a target its former prop is skipped, the own ship's still drawn");
    put(cockpit.bytes, 0x1e0, addr(&target_obj));
    c.begin_frame(6);
    check(eval(target_prop, &unit, far_rows) == V::exempt_target, "props: the target back: drawn again");
    // Walk budget: 70 new prop nodes in one frame: 64 walks, the rest drawn this frame and walked the next.
    c.begin_frame(7);
    unsigned culled = 0, deferred = 0;
    for (auto& n : many) {
        const V v = eval(n, &unit, far_rows);
        culled += v == V::culled;
        deferred += v == V::deferred;
    }
    c.begin_frame(8);
    unsigned culled_next = 0;
    for (auto& n : many) culled_next += eval(n, &unit, far_rows) == V::culled;
    check(culled == 64 && deferred == 6 && culled_next == 70,
          "props: 64 walks per frame, 6 deferred (drawn) and skipped on the next frame");
    std::printf("PROPS budget culled=%u deferred=%u next=%u walks=%u resolves=%u\n", culled, deferred, culled_next,
                c.window.walks, c.window.resolves);
    // Per-draw cost (harness-inclusive, Wine/FEX, not game FPS).
    LARGE_INTEGER f{}, s{}, e{};
    QueryPerformanceFrequency(&f);
    const unsigned loops = 200000;
    const auto ns = [&](auto&& body) {
        for (unsigned i = 0; i < 1000; ++i) body(i);
        QueryPerformanceCounter(&s);
        for (unsigned i = 0; i < loops; ++i) body(i);
        QueryPerformanceCounter(&e);
        return double(e.QuadPart - s.QuadPart) * 1e9 / double(f.QuadPart) / double(loops);
    };
    c.begin_frame(9);
    const double memo_hit = ns([&](unsigned) { eval(far_hull, &unit, far_rows); });
    std::uint32_t frame = 10;
    const double not_prop_first = ns([&](unsigned) {
        c.begin_frame(frame++);
        eval(far_hull, &unit, far_rows);
    });
    const double prop_first = ns([&](unsigned) {
        c.begin_frame(frame++);
        eval(far_prop, &unit, far_rows);
    });
    const double prop_pair = ns([&](unsigned i) {
        if (!(i & 1)) c.begin_frame(frame++);
        eval((i & 1) ? far_dummy : far_prop, &unit, far_rows);
    });
    std::printf("CULL SMALL PROPS BENCH memo_hit_ns=%.1f not_prop_first_ns=%.1f prop_culled_first_in_frame_ns=%.1f prop_culled_pair_mean_ns=%.1f harness=fixture_included game_fps=unmeasured\n",
                memo_hit, not_prop_first, prop_first, prop_pair);
}

// ---- the lens-flare cull (X3M_LENS_FLARE_GAIN=0, src/proxy/lens_flare_cull.cpp): the second stub on the same claim ----
// A synthetic body table (the census fixture's layout) resolves the 42 names: fixed slots with null names (the
// engine's default `v\NNNNN`), slot 753 with its literal name, slot 754 renamed (its name is then a dynamic
// registration in upper case), `v\01006` / `v\01016` as dynamic literal names, a dynamic name longer than any of
// ours, two names absent. A six-node tree: two flare bodies (a fixed and a dynamic id), a plain node, a node
// without a model and a small plain node for the small-parts stub, so the two stubs are exercised together in both
// chain orders.
static Node L, LF, LD, LN, LM, LS;
static Node* const lens_all[] = {&L, &LF, &LD, &LN, &LM, &LS};
constexpr unsigned lens_count = sizeof lens_all / sizeof lens_all[0];
static Node lens_initial[lens_count];
static std::uint32_t zero_global_for_lens = 0;
static void lens_reset() {
    for (unsigned i = 0; i < lens_count; ++i) *lens_all[i] = lens_initial[i];
}
static bool lens_same_but(const Node* got, const Node* want, unsigned except_offset) {
    for (unsigned i = 0; i < sizeof got->bytes; i += 4)
        if (i != except_offset && std::memcmp(got->bytes + i, want->bytes + i, 4)) return false;
    return true;
}
static bool lens_all_native(const Node* native) {
    for (unsigned i = 0; i < lens_count; ++i)
        if (std::memcmp(lens_all[i]->bytes, native[i].bytes, sizeof(Node))) return false;
    return true;
}
static const std::string& lens_last(const char* prefix) {
    static const std::string none;
    for (unsigned i = unsigned(lens_lines.size()); i > 0; --i)
        if (!std::strncmp(lens_lines[i - 1].c_str(), prefix, std::strlen(prefix))) return lens_lines[i - 1];
    return none;
}
static const lcore::Bitmap* lens_map() {
    return reinterpret_cast<const lcore::Bitmap*>(x3m_lens_flare_cull_bitmap);
}
static void lens_section(std::uintptr_t site, std::uintptr_t cull, View& view) {
    const unsigned checks_before = checks, failures_before = failures;
    // ---- core rules: the name forms, the case fold, the bitmap and the slot inverse ----
    {
        std::int32_t id = 0;
        check(lcore::default_name_id("v\\00752", &id) && id == 752 && lcore::default_name_id("v\\11000", &id) &&
                  id == 11000 && lcore::default_name_id("v\\01006", &id) && id == 1006 &&
                  !lcore::default_name_id("v\\0752", &id) && !lcore::default_name_id("v\\007520", &id) &&
                  !lcore::default_name_id("V\\00752", &id) && !lcore::default_name_id("v/00752", &id) &&
                  !lcore::default_name_id("", &id) && !lcore::default_name_id(nullptr, &id),
              "lens: default-form names parse, others do not");
        check(lcore::name_equal("v\\01006", "V\\01006") && !lcore::name_equal("v\\01006", "v/01006") &&
                  !lcore::name_equal("v\\01006", "v\\010060") && lcore::name_equal("", ""),
              "lens: the engine's case fold, separators distinct");
        check(lcore::slot_id(752, 11000) == 752 && lcore::slot_id(2000, 11000) == 11000 &&
                  lcore::slot_id(11000, 11000) == 20000 && lcore::slot_id(11003, 11000) == 20003,
              "lens: slot -> id is the inverse of the engine's id -> slot");
        static lcore::Bitmap map;
        map.clear();
        check(map.set(0) && map.set(752) && map.set(0x7fff) && !map.set(0x8000) && !map.set(-1) && map.test(752) &&
                  !map.test(753) && map.test(0) && map.test(0x7fff) && !map.test(0x8000) && !map.test(-1),
              "lens: bitmap set/test inside the span, refused outside");
        unsigned char stub[lcore::stub_length];
        lcore::encode_stub(0x10000000u, 0x20000000u, 0x20001000u, 0x20000004u, std::uint32_t(cull), 0x10000054u, stub);
        check(stub[0] == 0x83 && stub[1] == 0x3d && stub[7] == 0x74 && stub[8] == lcore::stub_continue - 9 &&
                  stub[9] == 0x8b && stub[10] == 0x87 && stub[11] == 0x40 && stub[12] == 0x01 && stub[15] == 0x3d &&
                  stub[16] == 0x00 && stub[17] == 0x80 && stub[20] == 0x73 && stub[21] == lcore::stub_continue - 22 &&
                  stub[27] == 0x8b && stub[28] == 0x0c && stub[29] == 0x8d && stub[34] == 0x0f && stub[35] == 0xa3 &&
                  stub[36] == 0xc1 && stub[37] == 0x73 && stub[38] == lcore::stub_continue - 39 &&
                  !std::memcmp(stub + lcore::stub_replay, score::window + score::site_offset, 25) &&
                  stub[64] == 0xff && stub[65] == 0x05 && stub[70] == 0xe9 && stub[75] == 0xff && stub[76] == 0x25,
              "lens: stub layout (flag test, model load, span compare, bitmap word, bit test, replayed span, count, "
              "cull jump, continue)");
    }

    // ---- the synthetic body table ----
    constexpr unsigned dynamic = 6, slots = ccore::body_fixed_count + dynamic;
    static unsigned char manager[0xc0];
    unsigned char* table = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, (slots + 1) * ccore::body_slot_stride, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(table != nullptr, "lens: synthetic slots");
    static const char literal_753[] = "v\\00753", renamed_754[] = "effects\\ray", dyn_1006[] = "v\\01006",
                      dyn_1016[] = "v\\01016", dyn_other[] = "ships\\x", dyn_754_upper[] = "V\\00754",
                      dyn_long[] = "v\\01019abcdefgh", dyn_1011[] = "v\\01011";
    auto name_at = [&](unsigned slot, const char* name) {
        put(table + slot * ccore::body_slot_stride, ccore::body_slot_name_offset, name ? addr(name) : 0u);
    };
    name_at(753, literal_753);
    name_at(754, renamed_754);
    name_at(ccore::body_fixed_count + 0, dyn_1006);
    name_at(ccore::body_fixed_count + 1, dyn_1016);
    name_at(ccore::body_fixed_count + 2, dyn_other);
    name_at(ccore::body_fixed_count + 3, dyn_754_upper);
    name_at(ccore::body_fixed_count + 4, dyn_long);
    name_at(ccore::body_fixed_count + 5, nullptr); // a dynamic slot without a name: `v\20005`, no match
    std::memset(manager, 0, sizeof manager);
    put(manager, ccore::body_fixed_count_offset, ccore::body_fixed_count);
    put(manager, ccore::body_dynamic_count_offset, dynamic);
    put(manager, ccore::body_slots_offset, addr(table));
    static std::uint32_t manager_global = 0;
    manager_global = addr(manager);
    {
        const lcore::Table t = lcore::read_table(&engine_read, addr(&manager_global));
        check(t.valid && t.fixed == ccore::body_fixed_count && t.dynamic == int(dynamic) && t.slots == addr(table),
              "lens: the table header through the production reader");
        static lcore::Bitmap map;
        map.clear();
        bool found[lcore::body_name_count] = {};
        const lcore::Resolution r = lcore::resolve(&engine_read, t, &map, found, 0, slots);
        check(r.resolved == 40 && r.mapped == 40 && r.scanned == 5,
              "lens: 37 names by id, 3 by scan (the dynamic literals and the renamed fixed slot), 2 absent");
        check(map.test(752) && map.test(753) && !map.test(754) && map.test(20003) && map.test(20000) &&
                  map.test(20001) && !map.test(20002) && !map.test(20004) && !map.test(20005) && map.test(11000) &&
                  map.test(11011) && map.test(61) && !map.test(1006) && !map.test(1011) && !map.test(1019),
              "lens: the bitmap holds the resolved ids only");
        const lcore::Resolution again = lcore::resolve(&engine_read, t, &map, found, 0, slots);
        check(again.resolved == 0 && again.mapped == 0, "lens: a second pass over found names resolves nothing new");
        check(!lcore::read_table(&engine_read, addr(&zero_global_for_lens)).valid,
              "lens: body system not up: invalid table");
        put(manager, ccore::body_fixed_count_offset, 10999);
        check(!lcore::read_table(&engine_read, addr(&manager_global)).valid, "lens: a wrong fixed count: invalid");
        put(manager, ccore::body_fixed_count_offset, ccore::body_fixed_count);
    }

    // ---- the tree and its native references ----
    node_set(L, nullptr, 20000, 100000, 0x1002, 0, 0, 0x5000, 4, 100, 50, 25);
    node_set(LF, &L, 1000, 30000, 0x1002, 0, 0, 752);   // a fixed flare body: s = 21
    node_set(LD, &L, 1000, 30000, 0x1002, 0, 0, 20000); // `v\01006`, a dynamic flare body
    node_set(LN, &L, 3000, 100000, 0x1002, 0, 0, 0x5001);
    node_set(LM, &L, 3000, 100000, 0x1002, 0, 0, 0xffffffffu); // no model
    node_set(LS, &L, 800, 100000, 0x1002, 0, 0, 0x5002);       // s = 5: the small-parts stub's at threshold 10
    Node* children[] = {&LF, &LD, &LS, &LN, &LM}; // the last child is kept on every path: the pass's return state
                                                   // (EAX/ECX) follows the last node it evaluated
    link_parented(L, children, 5);
    for (unsigned i = 0; i < lens_count; ++i) lens_initial[i] = *lens_all[i];
    static Node native[lens_count], culled_reference[lens_count];
    lens_reset();
    const Result native_result = run(L, view);
    for (unsigned i = 0; i < lens_count; ++i) native[i] = *lens_all[i];
    check(native_result.preserved && native_result.x87_empty && (get(LF.bytes, 0x12c) & 2) &&
              (get(LD.bytes, 0x12c) & 2) && (get(LN.bytes, 0x12c) & 2) && (get(LS.bytes, 0x12c) & 2),
          "lens: native tree keeps the flare bodies, the plain node and the small node");
    // The engine's own size cull of the two flare bodies (a limit above their measure): the reference the stub must
    // reproduce byte for byte except the limit word itself.
    lens_reset();
    put(LF.bytes, ccore::threshold_1d8_offset, 0x7fffffffu);
    put(LD.bytes, ccore::threshold_1d8_offset, 0x7fffffffu);
    run(L, view);
    for (unsigned i = 0; i < lens_count; ++i) culled_reference[i] = *lens_all[i];
    check(!(get(LF.bytes, 0x12c) & 2) && !(get(LD.bytes, 0x12c) & 2), "lens: the engine's own size cull reference");

    // ---- initialize() without the engine: off at G > 0, refused at G = 0 (the lens block bytes are not here) ----
    check(!lens::initialize(false) && !std::strcmp(lens::state(), "gain") &&
              lens_last("lens_flare_cull status=").find("lens_flare_cull status=off reason=gain bodies=0") == 0,
          "lens: initialize at G > 0: off, nothing patched");
    check(!lens::initialize(true) && !std::strcmp(lens::state(), "lens_bytes_mismatch") && !lens::installed() &&
              lens_last("lens_flare_cull status=").find("lens_flare_cull status=refused reason=lens_bytes_mismatch") == 0 &&
              small_window_original(),
          "lens: initialize at G = 0 without the lens block bytes: refused, site untouched");

    // ---- install alone at G > 0: the claim is made, the set resolves, the flag stays 0 ----
    lens::set_body_table_global(addr(&manager_global));
    check(lens::install_at(site, cull, false) && !std::strcmp(lens::state(), "ok") && lens::installed() &&
              small::site_claimed() && !small::stub_address() && small_site()[0] == 0xe9,
          "lens: install_at alone claims the shared site (the small-parts stub not installed)");
    check(!lens::install_at(site, cull, true) && !std::strcmp(lens::state(), "already_installed"),
          "lens: second install refused");
    {
        const std::uint32_t at = std::uint32_t(lens::stub_address()), slot = (at + lcore::stub_length + 3) & ~3u;
        unsigned char want[lcore::stub_length];
        lcore::encode_stub(at, addr(const_cast<std::uint32_t*>(&x3m_lens_flare_cull_enabled)),
                           addr(x3m_lens_flare_cull_bitmap), addr(const_cast<std::uint32_t*>(&x3m_lens_flare_cull_culled)),
                           std::uint32_t(cull), slot, want);
        check(at != 0 && !std::memcmp(reinterpret_cast<const void*>(at), want, lcore::stub_length) &&
                  *reinterpret_cast<void**>(slot) != nullptr,
              "lens: stub bytes as encoded, continuation slot points at the tail");
    }
    lens_lines.clear();
    lens::begin_frame(12);
    check(lens::stats().bodies == 40 && lens::stats().mapped == 40 && !lens::stats().enabled &&
              x3m_lens_flare_cull_enabled == 0 &&
              lens_last("lens_flare_cull_bodies").find("lens_flare_cull_bodies bodies=40 mapped=40 scanned=5 fixed=11000 dynamic=6 enabled=0") == 0,
          "lens: begin_frame resolves the set, the flag stays 0 at G > 0");
    lens_reset();
    Result r = run(L, view);
    check(r.preserved && r.x87_empty && same_outputs(r, native_result) && lens_all_native(native) &&
              x3m_lens_flare_cull_culled == 0,
          "lens: G > 0: every node and EAX/ECX/EDX/EFLAGS as native, nothing counted");
    check(lens::shutdown() && !lens::installed() && lens::shutdown(), "lens: shutdown disarms; a second is a no-op");
    check(small::shutdown() && small_window_original(), "lens: the owner's shutdown restores the shared site");
    lens_reset();
    r = run(L, view);
    check(same_outputs(r, native_result) && lens_all_native(native), "lens: after the restore the native pass is back");

    // ---- install at G = 0: the flare bodies take the engine's cull path, the rest is native ----
    check(lens::install_at(site, cull, true) && !std::strcmp(lens::state(), "ok"), "lens: install_at at G = 0");
    lens::begin_frame(12);
    check(lens::stats().enabled && x3m_lens_flare_cull_enabled == 1 && lens::stats().mapped == 40,
          "lens: G = 0 and a mapped set: the flag is 1");
    lens_reset();
    SetLastError(0x5153);
    r = run(L, view);
    check(GetLastError() == 0x5153, "lens: LastError preserved across the armed pass");
    check(r.preserved && r.x87_empty && same_outputs(r, native_result),
          "lens: armed: callee-saved registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
    check(!(get(LF.bytes, 0x12c) & 2) && !(get(LD.bytes, 0x12c) & 2) &&
              lens_same_but(&LF, &culled_reference[1], ccore::threshold_1d8_offset) &&
              lens_same_but(&LD, &culled_reference[2], ccore::threshold_1d8_offset),
          "lens: the fixed and the dynamic flare body end exactly as the engine's own size cull leaves them");
    check(!std::memcmp(LN.bytes, native[3].bytes, sizeof(Node)) && !std::memcmp(LM.bytes, native[4].bytes, sizeof(Node)) &&
              !std::memcmp(LS.bytes, native[5].bytes, sizeof(Node)) && !std::memcmp(L.bytes, native[0].bytes, sizeof(Node)),
          "lens: the plain node, the node without a model, the small node and the root untouched");
    check(x3m_lens_flare_cull_culled == 2 && lens::stats().culled == 2, "lens: two nodes counted");
    lens_lines.clear();
    lens::report(300);
    check(lens_last("lens_flare_cull culled=") == "lens_flare_cull culled=2 total=2 enabled=1 bodies=40 mapped=40 frame=300",
          "lens: the 300-frame row");
    lens::report(600);
    check(lens_last("lens_flare_cull culled=") == "lens_flare_cull culled=0 total=2 enabled=1 bodies=40 mapped=40 frame=600",
          "lens: the next window counts from zero");
    // A grown table: the new dynamic slot carries a missing name and is found without a rescan of the old ones.
    name_at(ccore::body_fixed_count + 6, dyn_1011);
    put(manager, ccore::body_dynamic_count_offset, dynamic + 1);
    lens_lines.clear();
    lens::begin_frame(12);
    check(lens::stats().bodies == 41 && lens::stats().mapped == 41 && lens::stats().scanned == 6 &&
              lens::stats().dynamic == dynamic + 1 && lens_map()->test(20006) &&
              lens_last("lens_flare_cull_bodies").find("lens_flare_cull_bodies bodies=41 mapped=41 scanned=6 fixed=11000 dynamic=7 enabled=1") == 0,
          "lens: a grown table: the new name found in the new slot alone, one row");
    lens_lines.clear();
    lens::begin_frame(12);
    check(lens::stats().bodies == 41 && lens::stats().scanned == 7 && lens_lines.empty(),
          "lens: an unchanged table: the newest slot re-read, no row");
    // A moved table (a game load re-binds the ids): everything is resolved again from the new array.
    unsigned char* moved = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, (slots + 1) * ccore::body_slot_stride, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(moved != nullptr, "lens: moved slots");
    std::memcpy(moved, table, (slots + 1) * ccore::body_slot_stride);
    put(moved + (ccore::body_fixed_count + 0) * ccore::body_slot_stride, ccore::body_slot_name_offset, addr(dyn_other));
    put(moved + (ccore::body_fixed_count + 2) * ccore::body_slot_stride, ccore::body_slot_name_offset, addr(dyn_1006));
    put(manager, ccore::body_slots_offset, addr(moved));
    lens::begin_frame(12);
    check(lens::stats().bodies == 41 && lens::stats().mapped == 41 && lens::stats().scanned == 6 &&
              !lens_map()->test(20000) && lens_map()->test(20002) && lens_map()->test(20006),
          "lens: a moved table restarts the resolution: `v\\01006` now id 20002, 20000 cleared");
    // A shrunk table (bulk free): restart too; the flag follows the mapped count.
    put(manager, ccore::body_dynamic_count_offset, 0);
    lens::begin_frame(12);
    check(lens::stats().bodies == 37 && lens::stats().mapped == 37 && lens::stats().enabled && !lens_map()->test(20002),
          "lens: a shrunk table: the fixed names alone, still armed");
    put(manager, ccore::body_slots_offset, addr(table));
    put(manager, ccore::body_dynamic_count_offset, dynamic + 1);
    lens::begin_frame(12);
    check(lens::stats().bodies == 41 && lens::stats().mapped == 41 && lens_map()->test(20000),
          "lens: back to the first array: 41 again");
    // A refill in place (the same array and count after a game load, the ids re-bound): the mapped dynamic slots are
    // re-read every frame, the mismatch restarts the resolution.
    check(lens::stats().restarts == 3 && lens_map()->test(20000) && !lens_map()->test(20002), "lens: three restarts so far");
    name_at(ccore::body_fixed_count + 0, dyn_other);
    name_at(ccore::body_fixed_count + 2, dyn_1006);
    lens::begin_frame(13);
    check(lens::stats().restarts == 4 && lens::stats().bodies == 41 && !lens_map()->test(20000) &&
              lens_map()->test(20002) && lens_map()->test(20006),
          "lens: a refill in place re-binds `v\\01006` 20000 -> 20002 within one frame");
    lens::begin_frame(14);
    check(lens::stats().restarts == 4 && lens_map()->test(20002), "lens: a stable refill: no further restart");
    name_at(ccore::body_fixed_count + 0, dyn_1006);
    name_at(ccore::body_fixed_count + 2, dyn_other);
    lens::begin_frame(15);
    check(lens::stats().restarts == 5 && lens_map()->test(20000) && !lens_map()->test(20002), "lens: and back");
    // No body system: the last set stands (the reads fail, nothing is cleared).
    lens::set_body_table_global(addr(&zero_global_for_lens));
    lens::begin_frame(12);
    check(lens::stats().mapped == 41 && lens::stats().enabled, "lens: body system unreadable: the set stands");
    lens::set_body_table_global(addr(&manager_global));
    lens::begin_frame(12);
    check(lens::stats().bodies == 41 && lens::stats().enabled, "lens: readable again: resolved from the same array");

    // ---- both stubs on the chain: the small-parts stub pushed in front (it runs first) ----
    check(small::install_at(site, cull, true) && !std::strcmp(small::state(), "ok") && small::site_claimed(),
          "lens: the small-parts stub joins the live claim");
    x3m_cull_small_parts_threshold = 10;
    x3m_cull_small_parts_upper = 10; // the stub's first compare (dock rule off)
    x3m_cull_small_parts_culled = 0;
    lens_reset();
    r = run(L, view);
    check(r.preserved && r.x87_empty && same_outputs(r, native_result) && !(get(LF.bytes, 0x12c) & 2) &&
              !(get(LD.bytes, 0x12c) & 2) && !(get(LS.bytes, 0x12c) & 2) && (get(LN.bytes, 0x12c) & 2) &&
              lens_same_but(&LF, &culled_reference[1], ccore::threshold_1d8_offset) &&
              x3m_cull_small_parts_culled == 1 && x3m_lens_flare_cull_culled == 4,
          "lens: small-parts first: the small node by its stub, the flare bodies by this one, the plain node kept");
    check(small::shutdown() && small_window_original() && !std::strcmp(small::state(), "restored") && !small::site_claimed(),
          "lens: the owner's shutdown restores the site with both stubs chained");
    lens::begin_frame(12);
    check(x3m_lens_flare_cull_enabled == 0 && lens::installed(), "lens: with the site restored the flag drops");
    lens_reset();
    r = run(L, view);
    check(same_outputs(r, native_result) && lens_all_native(native), "lens: native again");
    check(lens::shutdown(), "lens: disarm after the owner's restore");
    // ---- the production order: the small-parts stub first, this one pushed in front ----
    check(small::install_at(site, cull, true) && lens::install_at(site, cull, true) && small::site_claimed(),
          "lens: production order: small-parts claims, the lens stub chains in front");
    lens::begin_frame(12);
    x3m_cull_small_parts_threshold = 10;
    x3m_cull_small_parts_upper = 10; // the stub's first compare (dock rule off)
    x3m_cull_small_parts_culled = 0;
    lens_reset();
    SetLastError(0x5154);
    r = run(L, view);
    check(GetLastError() == 0x5154 && r.preserved && r.x87_empty && same_outputs(r, native_result) &&
              !(get(LF.bytes, 0x12c) & 2) && !(get(LD.bytes, 0x12c) & 2) && !(get(LS.bytes, 0x12c) & 2) &&
              (get(LN.bytes, 0x12c) & 2) && lens_same_but(&LD, &culled_reference[2], ccore::threshold_1d8_offset) &&
              x3m_cull_small_parts_culled == 1 && x3m_lens_flare_cull_enabled == 1 && x3m_lens_flare_cull_culled == 2,
          "lens: lens stub first: the same verdicts, LastError preserved");
    x3m_cull_small_parts_threshold = 0;
    x3m_cull_small_parts_upper = 0;
    check(lens::shutdown() && small::shutdown() && small_window_original(), "lens: both down, bytes exact");
    lens_reset();
    r = run(L, view);
    check(same_outputs(r, native_result) && lens_all_native(native), "lens: native after both");
    lens::set_body_table_global(addr(&zero_global_for_lens));
    // ---- cost of one restart over a Mayhem-sized table (11000 fixed + 2200 dynamic slots, every dynamic slot with a
    // short literal name so each is read and compared) and of the per-frame re-read of five mapped dynamic slots ----
    {
        constexpr unsigned big_dynamic = 2200, big_slots = ccore::body_fixed_count + big_dynamic;
        unsigned char* big = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, big_slots * ccore::body_slot_stride, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        char* names = static_cast<char*>(VirtualAlloc(nullptr, big_dynamic * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        check(big && names, "lens: bench table");
        for (unsigned i = 0; i < big_dynamic; ++i) {
            std::memcpy(names + i * 8, "ships\\x", 8);
            put(big + (ccore::body_fixed_count + i) * ccore::body_slot_stride, ccore::body_slot_name_offset, addr(names + i * 8));
        }
        std::memcpy(names + 100 * 8, "v\\01006", 8);
        std::memcpy(names + 900 * 8, "v\\01011", 8);
        std::memcpy(names + 1500 * 8, "v\\01016", 8);
        std::memcpy(names + 2100 * 8, "v\\01019", 8);
        std::memcpy(names + 2199 * 8, "V\\00754", 8);
        put(big + 754 * ccore::body_slot_stride, ccore::body_slot_name_offset, addr(names + 1 * 8));
        static unsigned char big_manager[0xc0];
        std::memset(big_manager, 0, sizeof big_manager);
        put(big_manager, ccore::body_fixed_count_offset, ccore::body_fixed_count);
        put(big_manager, ccore::body_dynamic_count_offset, big_dynamic);
        put(big_manager, ccore::body_slots_offset, addr(big));
        static std::uint32_t big_global = 0;
        big_global = addr(big_manager);
        const lcore::Table t = lcore::read_table(&engine_read, addr(&big_global));
        static lcore::Bitmap map;
        static lcore::Mappings mappings;
        bool found[lcore::body_name_count];
        lcore::Resolution r;
        LARGE_INTEGER f{}, s{}, e{};
        QueryPerformanceFrequency(&f);
        auto restart = [&] {
            map.clear();
            mappings.clear();
            std::memset(found, 0, sizeof found);
            x3m::engine_memory::next_frame();
            r = lcore::resolve(&engine_read, t, &map, found, 0, big_slots, &mappings);
        };
        for (unsigned i = 0; i < 4; ++i) restart();
        constexpr unsigned loops = 20;
        QueryPerformanceCounter(&s);
        for (unsigned i = 0; i < loops; ++i) restart();
        QueryPerformanceCounter(&e);
        const double restart_us = double(e.QuadPart - s.QuadPart) * 1e6 / double(f.QuadPart) / loops;
        check(t.valid && r.resolved == 42 && r.mapped == 42 && r.scanned == big_dynamic + 1 && mappings.count == 5,
              "lens: bench table resolves all 42 (37 by id, 5 in dynamic slots), every dynamic slot scanned");
        constexpr unsigned hold_loops = 20000;
        QueryPerformanceCounter(&s);
        bool held = true;
        for (unsigned i = 0; i < hold_loops; ++i) held = lcore::mappings_hold(&engine_read, t, mappings) && held;
        QueryPerformanceCounter(&e);
        const double hold_us = double(e.QuadPart - s.QuadPart) * 1e6 / double(f.QuadPart) / hold_loops;
        check(held, "lens: the five mappings hold");
        std::printf("LENS BENCH restart_scan_us=%.1f slots=%u dynamic_named=%u mappings_hold_us=%.3f mappings=%u harness=engine_memory_read game_fps=unmeasured\n",
                    restart_us, big_slots, big_dynamic, hold_us, unsigned(mappings.count));
    }
    std::printf("LENS FLARE CULL checks=%u failures=%u\n", checks - checks_before, failures - failures_before);
}

// ---- engine-side occlusion skip (X3M_OCCLUSION_CULL=engine, src/proxy/occlusion_engine_cull.cpp): the third stub
// executed on the synthetic pass, alone and chained with the lens-flare and small-parts stubs in both orders ----
// A root with seven children: XH listed in the table (three draws, every one a proxy skip), XM listed under another
// model id, XP listed at a camera-space position outside its window (the ledger recorded +0xf0 shifted by 1000 units
// against the 100000 / 256 = 390 window), XN drawn (never listed), XF a flare body (the lens stub), XS a small node
// (the small-parts stub at threshold 10) and XL last and kept, so the pass's return state (EAX/ECX/EDX/EFLAGS) follows a
// node that took the tail. Checks: both exits of the stub (the replay into the tail: every kept node byte-identical to
// the native pass and the pass's outputs identical, so the tail's TEST fed the JE at the engine's 0x0047d2ad as native;
// the jump to the cull: XH ends exactly as the engine's own size cull leaves it), callee-saved registers, ESP and the
// x87 stack as native, LastError preserved, the six counters (visits, skipped parts/draws, the three guards), the stamp
// guard through the stamp word, a non-sector view (another View object) not skipped and not counted, the disarmed
// stub, take() clearing and disarming, the chain in the production order (engine stub first) and reversed (last), the
// owner's restore, and refusals (initialize without the engine's bytes emits nothing: the arena is unchanged).
namespace oe = x3m::occlusion_cull::engine;
namespace eng = x3m::occlusion_engine_cull;
static Node X, XH, XM, XP, XN, XF, XS, XL;
static Node* const eng_all[] = {&X, &XH, &XM, &XP, &XN, &XF, &XS, &XL};
constexpr unsigned eng_count = sizeof eng_all / sizeof eng_all[0];
static Node eng_initial[eng_count];
static void eng_reset() {
    for (unsigned i = 0; i < eng_count; ++i) *eng_all[i] = eng_initial[i];
}
static bool eng_all_native(const Node* native) {
    for (unsigned i = 0; i < eng_count; ++i)
        if (std::memcmp(eng_all[i]->bytes, native[i].bytes, sizeof(Node))) return false;
    return true;
}
static bool eng_counters(std::uint32_t visits, std::uint32_t parts, std::uint32_t draws, std::uint32_t model,
                         std::uint32_t stamp, std::uint32_t position) {
    return x3m_occlusion_engine_visits == visits && x3m_occlusion_engine_skipped_parts == parts &&
           x3m_occlusion_engine_skipped_draws == draws && x3m_occlusion_engine_rejected_model == model &&
           x3m_occlusion_engine_rejected_stamp == stamp && x3m_occlusion_engine_rejected_position == position;
}
static void engine_section(std::uintptr_t site, std::uintptr_t cull, View& view) {
    const unsigned checks_before = checks, failures_before = failures;
    node_set(X, nullptr, 20000, 100000, 0x1002, 0, 0, 0x5000, 4, 100, 50, 25);
    node_set(XH, &X, 3000, 100000, 0x1002, 0, 0, 0x6001);
    node_set(XM, &X, 3000, 100000, 0x1002, 0, 0, 0x6002);
    node_set(XP, &X, 3000, 100000, 0x1002, 0, 0, 0x6003);
    node_set(XN, &X, 3000, 100000, 0x1002, 0, 0, 0x6004);
    node_set(XF, &X, 1000, 30000, 0x1002, 0, 0, 752);    // a fixed flare body: the lens stub's
    node_set(XS, &X, 800, 100000, 0x1002, 0, 0, 0x6005);  // s = 5: the small-parts stub's at threshold 10
    node_set(XL, &X, 3000, 100000, 0x1002, 0, 0, 0x6006); // last: kept on every path
    Node* children[] = {&XH, &XM, &XP, &XN, &XF, &XS, &XL};
    link_parented(X, children, 7);
    for (unsigned i = 0; i < eng_count; ++i) eng_initial[i] = *eng_all[i];
    static Node native[eng_count], culled_reference[eng_count];
    eng_reset();
    const Result native_result = run(X, view);
    for (unsigned i = 0; i < eng_count; ++i) native[i] = *eng_all[i];
    check(native_result.preserved && native_result.x87_empty && (get(XH.bytes, 0x12c) & 2) && (get(XF.bytes, 0x12c) & 2) &&
              (get(XS.bytes, 0x12c) & 2) && (get(XL.bytes, 0x12c) & 2),
          "engine: native tree keeps every node");
    // The engine's own size cull of XH, XF and XS (a limit above their measure): the reference every stub must
    // reproduce byte for byte except the limit word itself.
    eng_reset();
    put(XH.bytes, ccore::threshold_1d8_offset, 0x7fffffffu);
    put(XF.bytes, ccore::threshold_1d8_offset, 0x7fffffffu);
    put(XS.bytes, ccore::threshold_1d8_offset, 0x7fffffffu);
    run(X, view);
    for (unsigned i = 0; i < eng_count; ++i) culled_reference[i] = *eng_all[i];
    check(!(get(XH.bytes, 0x12c) & 2) && !(get(XF.bytes, 0x12c) & 2) && !(get(XS.bytes, 0x12c) & 2),
          "engine: the engine's own size cull reference");
    auto culled_as_engine = [&](const Node& n, unsigned i) {
        return !(get(n.bytes, 0x12c) & 2) && lens_same_but(&n, &culled_reference[i], ccore::threshold_1d8_offset);
    };

    // ---- initialize(): off at the modes on/off; refused without the engine's bytes, nothing emitted ----
    const unsigned arena_before = x3m::engine_patch::arena_used();
    check(!eng::initialize(false) && !std::strcmp(eng::state(), "mode") && !eng::installed(),
          "engine: initialize with the mode on/off: nothing patched");
    check(!eng::initialize(true) && !std::strcmp(eng::state(), "bytes_mismatch") && !eng::installed() &&
              small_window_original() && x3m::engine_patch::arena_used() == arena_before,
          "engine: initialize without the engine's window bytes: refused before any stub is emitted (arena unchanged)");

    // ---- install alone: the claim is made by this module; the bytes are the encoder's ----
    const bool installed_alone = eng::install_at(site, cull);
    if (!installed_alone)
        std::printf("DETAIL engine install state=%s arena_used=%u of %u\n", eng::state(), x3m::engine_patch::arena_used(),
                    x3m::engine_patch::arena_capacity());
    check(installed_alone && !std::strcmp(eng::state(), "ok") && eng::installed() && small::site_claimed() &&
              !small::stub_address() && !lens::installed() && small_site()[0] == 0xe9,
          "engine: install_at alone claims the shared site");
    check(!eng::install_at(site, cull) && !std::strcmp(eng::state(), "already_installed"), "engine: second install refused");
    {
        const std::uint32_t at = std::uint32_t(eng::stub_address()), slot = (at + oe::stub_length + 3) & ~3u;
        unsigned char want[oe::stub_length];
        const oe::StubWords words{addr(const_cast<std::uint32_t*>(&x3m_occlusion_engine_armed)),
                                  addr(const_cast<std::uint32_t*>(&x3m_occlusion_engine_view)),
                                  addr(const_cast<std::uint32_t*>(&x3m_occlusion_engine_stamp)),
                                  addr(eng::table().entries),
                                  addr(const_cast<std::uint32_t*>(&x3m_occlusion_engine_visits)),
                                  addr(const_cast<std::uint32_t*>(&x3m_occlusion_engine_skipped_parts)),
                                  addr(const_cast<std::uint32_t*>(&x3m_occlusion_engine_skipped_draws)),
                                  addr(const_cast<std::uint32_t*>(&x3m_occlusion_engine_rejected_model)),
                                  addr(const_cast<std::uint32_t*>(&x3m_occlusion_engine_rejected_stamp)),
                                  addr(const_cast<std::uint32_t*>(&x3m_occlusion_engine_rejected_position))};
        oe::encode_stub(at, words, std::uint32_t(cull), slot, want);
        check(at != 0 && !std::memcmp(reinterpret_cast<const void*>(at), want, oe::stub_length) &&
                  *reinterpret_cast<void**>(slot) != nullptr,
              "engine: stub bytes as encoded, continuation slot points at the tail");
    }
    // Disarmed (nothing published): the pass is native, nothing counted.
    eng_reset();
    Result r = run(X, view);
    check(r.preserved && r.x87_empty && same_outputs(r, native_result) && eng_all_native(native) && eng_counters(0, 0, 0, 0, 0, 0),
          "engine: installed but disarmed: every node and EAX/ECX/EDX/EFLAGS as native, nothing counted");

    // ---- the previous frame's ledger: XH three skipped draws; XM skipped under model 0x7002 (the node carries
    // 0x6002); XP skipped at +0xf0 - 1000 (outside the 390-unit window); XN drawn ----
    static oe::Ledger ledger;
    auto shifted_read = [&](std::uintptr_t p, void* out, std::size_t n) {
        std::memcpy(out, reinterpret_cast<const void*>(p), n);
        if (p == addr(&XP) + oe::position_offset) {
            std::int32_t v;
            std::memcpy(&v, out, 4);
            v -= 1000;
            std::memcpy(out, &v, 4);
        }
        return true;
    };
    ledger.begin(41, addr(&view));
    for (unsigned i = 0; i < 3; ++i) ledger.skipped(ledger.draw(addr(&XH)), 0x6001, shifted_read);
    ledger.skipped(ledger.draw(addr(&XM)), 0x7002, shifted_read);
    ledger.skipped(ledger.draw(addr(&XP)), 0x6003, shifted_read);
    ledger.draw(addr(&XN));
    oe::PublishStats st{};
    unsigned published = eng::publish(ledger, 42, 1, &st);
    check(published == 3 && st.candidates == 4 && st.withheld == 0 && x3m_occlusion_engine_armed == 1 &&
              x3m_occlusion_engine_view == addr(&view) && x3m_occlusion_engine_stamp == 42 && eng::table().find(addr(&XH)) &&
              eng::table().find(addr(&XH))->draws == 3 && !eng::table().find(addr(&XN)),
          "engine: publish lists the three fully skipped nodes, arms the stub with the view and the stamp");
    // ---- armed, the sector view: XH takes the cull exit, XM and XP the guards, XN misses; the rest native ----
    eng_reset();
    SetLastError(0x5155);
    r = run(X, view);
    check(GetLastError() == 0x5155, "engine: LastError preserved across the armed pass");
    check(r.preserved && r.x87_empty && same_outputs(r, native_result),
          "engine: armed: callee-saved registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native (the tail's TEST fed the JE)");
    if (!culled_as_engine(XH, 1)) {
        std::int32_t pos[3];
        std::memcpy(pos, XH.bytes + oe::position_offset, 12);
        const oe::Entry* e = eng::table().find(addr(&XH));
        std::printf("DETAIL engine armed run: visits=%lu parts=%lu draws=%lu rejected=%lu,%lu,%lu xh_flags=%08lx xh_pos=%ld,%ld,%ld "
                    "entry=%s lo=%ld hi=%ld mirror=%u view_word=%08lx view=%08lx stamp_word=%lu\n",
                    (unsigned long)x3m_occlusion_engine_visits, (unsigned long)x3m_occlusion_engine_skipped_parts,
                    (unsigned long)x3m_occlusion_engine_skipped_draws, (unsigned long)x3m_occlusion_engine_rejected_model,
                    (unsigned long)x3m_occlusion_engine_rejected_stamp, (unsigned long)x3m_occlusion_engine_rejected_position,
                    (unsigned long)get(XH.bytes, 0x12c), (long)pos[0], (long)pos[1], (long)pos[2], e ? "yes" : "no",
                    e ? (long)e->lo_x : 0L, e ? (long)e->hi_x : 0L,
                    unsigned(eng::table().lookup(addr(&XH), 0x6001, pos, x3m_occlusion_engine_stamp)),
                    (unsigned long)x3m_occlusion_engine_view, (unsigned long)addr(&view), (unsigned long)x3m_occlusion_engine_stamp);
    }
    check(culled_as_engine(XH, 1), "engine: the listed node ends exactly as the engine's own size cull leaves it (cull exit)");
    check(!std::memcmp(XM.bytes, native[2].bytes, sizeof(Node)) && !std::memcmp(XP.bytes, native[3].bytes, sizeof(Node)) &&
              !std::memcmp(XN.bytes, native[4].bytes, sizeof(Node)) && !std::memcmp(XF.bytes, native[5].bytes, sizeof(Node)) &&
              !std::memcmp(XS.bytes, native[6].bytes, sizeof(Node)) && !std::memcmp(XL.bytes, native[7].bytes, sizeof(Node)) &&
              !std::memcmp(X.bytes, native[0].bytes, sizeof(Node)),
          "engine: the model-guarded, position-guarded, unlisted and other nodes take the tail untouched (replay exit)");
    check(eng_counters(8, 1, 3, 1, 0, 1),
          "engine: counters: 8 visits (root and seven children), 1 part / 3 draws skipped, model 1, stamp 0, position 1");
    // ---- the stamp guard: the stamp word of another frame ----
    x3m_occlusion_engine_stamp = 43;
    eng_reset();
    r = run(X, view);
    check(same_outputs(r, native_result) && eng_all_native(native) && eng_counters(16, 1, 3, 2, 2, 1),
          "engine: a stale stamp: nothing skipped (XH and XP rejected by stamp, XM by model first)");
    x3m_occlusion_engine_stamp = 42;
    // ---- a non-sector view: another View object with the same contents ----
    {
        View other;
        std::memcpy(other.bytes, view.bytes, sizeof other.bytes);
        eng_reset();
        r = run(X, other);
        check(same_outputs(r, native_result) && eng_all_native(native) && eng_counters(16, 1, 3, 2, 2, 1),
              "engine: another view: nothing skipped, no visit counted");
    }
    // ---- take(): the counters since the previous take, then cleared and disarmed ----
    {
        const eng::Counters c = eng::take();
        eng_reset();
        r = run(X, view);
        check(c.visits == 16 && c.skipped_parts == 1 && c.skipped_draws == 3 && c.rejected_model == 2 && c.rejected_stamp == 2 &&
                  c.rejected_position == 1 && x3m_occlusion_engine_armed == 0 && eng_all_native(native) && eng_counters(0, 0, 0, 0, 0, 0),
              "engine: take() returns the counters, clears them and disarms: the next pass is native");
    }
    check(eng::shutdown() && !eng::installed() && eng::shutdown(), "engine: shutdown disarms; a second is a no-op");
    check(small::shutdown() && small_window_original(), "engine: the owner's shutdown restores the site");
    eng_reset();
    r = run(X, view);
    check(same_outputs(r, native_result) && eng_all_native(native), "engine: after the restore the native pass is back");

    // ---- the lens stub's body set: fixed slots without names resolve by id (752 among them) ----
    constexpr unsigned slots = ccore::body_fixed_count;
    static unsigned char manager[0xc0];
    unsigned char* table = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, (slots + 1) * ccore::body_slot_stride, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(table != nullptr, "engine: synthetic body slots for the lens stub");
    std::memset(manager, 0, sizeof manager);
    put(manager, ccore::body_fixed_count_offset, ccore::body_fixed_count);
    put(manager, ccore::body_dynamic_count_offset, 0);
    put(manager, ccore::body_slots_offset, addr(table));
    static std::uint32_t manager_global = 0;
    manager_global = addr(manager);
    lens::set_body_table_global(addr(&manager_global));
    // visits: with this stub first every node reaching the site is counted (8); last, the nodes the lens and small-parts
    // stubs culled before it (XF, XS) never reach it (6).
    auto chained_run = [&](const char* order, std::uint32_t visits) {
        lens::begin_frame(12);
        x3m_cull_small_parts_threshold = 10;
        x3m_cull_small_parts_upper = 10;
        x3m_cull_small_parts_culled = 0;
        x3m_lens_flare_cull_culled = 0;
        eng::publish(ledger, 42, 1, &st);
        eng_reset();
        SetLastError(0x5156);
        r = run(X, view);
        char label[160];
        std::snprintf(label, sizeof label, "engine: %s: XH by this stub, XF by the lens stub, XS by the small-parts stub, the rest kept", order);
        const bool ok = GetLastError() == 0x5156 && r.preserved && r.x87_empty && same_outputs(r, native_result) &&
                        culled_as_engine(XH, 1) && culled_as_engine(XF, 5) && culled_as_engine(XS, 6) &&
                        !std::memcmp(XM.bytes, native[2].bytes, sizeof(Node)) && !std::memcmp(XP.bytes, native[3].bytes, sizeof(Node)) &&
                        !std::memcmp(XN.bytes, native[4].bytes, sizeof(Node)) && !std::memcmp(XL.bytes, native[7].bytes, sizeof(Node)) &&
                        x3m_lens_flare_cull_enabled == 1 && x3m_lens_flare_cull_culled == 1 && x3m_cull_small_parts_culled == 1 &&
                        eng_counters(visits, 1, 3, 1, 0, 1);
        if (!ok)
            std::printf("DETAIL engine chained (%s): preserved=%d outputs=%d xh=%d xf=%d xs=%d lens_enabled=%lu lens_culled=%lu "
                        "small_culled=%lu visits=%lu parts=%lu draws=%lu rejected=%lu,%lu,%lu\n",
                        order, int(r.preserved), int(same_outputs(r, native_result)), int(culled_as_engine(XH, 1)),
                        int(culled_as_engine(XF, 5)), int(culled_as_engine(XS, 6)), (unsigned long)x3m_lens_flare_cull_enabled,
                        (unsigned long)x3m_lens_flare_cull_culled, (unsigned long)x3m_cull_small_parts_culled,
                        (unsigned long)x3m_occlusion_engine_visits, (unsigned long)x3m_occlusion_engine_skipped_parts,
                        (unsigned long)x3m_occlusion_engine_skipped_draws, (unsigned long)x3m_occlusion_engine_rejected_model,
                        (unsigned long)x3m_occlusion_engine_rejected_stamp, (unsigned long)x3m_occlusion_engine_rejected_position);
        check(ok, label);
        eng::take();
        x3m_cull_small_parts_threshold = 0;
        x3m_cull_small_parts_upper = 0;
    };
    // ---- the production order: small-parts claims, the lens stub chains in front, this stub last pushed (runs first) ----
    check(small::install_at(site, cull, true) && lens::install_at(site, cull, true) && eng::install_at(site, cull) &&
              small::site_claimed() && !std::strcmp(eng::state(), "ok"),
          "engine: production order: small-parts, lens, engine (the engine stub runs first)");
    chained_run("engine stub first", 8);
    check(eng::shutdown() && lens::shutdown() && small::shutdown() && small_window_original(), "engine: all three down, bytes exact");
    // ---- reversed: this stub claims, the other two chain in front of it (it runs last, from the lens stub's continue) ----
    check(eng::install_at(site, cull) && small::install_at(site, cull, true) && lens::install_at(site, cull, true) &&
              small::site_claimed(),
          "engine: reversed order: engine claims, small-parts and lens chain in front (the engine stub runs last)");
    chained_run("engine stub last", 6);
    check(eng::shutdown() && lens::shutdown() && small::shutdown() && small_window_original(), "engine: all three down again");
    eng_reset();
    r = run(X, view);
    check(same_outputs(r, native_result) && eng_all_native(native) && x3m_occlusion_engine_armed == 0, "engine: native after both orders");
    lens::set_body_table_global(addr(&zero_global_for_lens));
    std::printf("ENGINE SKIP checks=%u failures=%u\n", checks - checks_before, failures - failures_before);
}

// ---- far engine jets (X3M_ENGINE_EFFECTS=plumes): culled JET nodes handed to x3m_engine_far_jet ----
namespace fj = x3m::engine_far_jets;
namespace fcore = x3m::engine_far_jets::core;
namespace ee = x3m::engine_effects::core;
// The fixture as one requesting device (engine_far_jets.h request: the production device count).
static bool far_counted = false;
static void far_arm(bool on) {
    fj::request(&far_counted, on, &far_counted);
}
struct alignas(16) Context {
    unsigned char bytes[0x40];
};
static Context far_context;
// A tree of 64 children below the 4 px threshold (5) at D = 64000 in a 1280 / 0x4000 view (s = radius / 100 = 3, measure
// 6: the engine keeps them), JET or not: the stub's per-node cost with and without a far record (the copy is armed).
static Node far_tree[65], far_tree_initial[65];
static double far_bench_us(bool jets, View& v, unsigned loops) {
    node_set(far_tree[0], nullptr, 1, 100000, 0x1000, 0, 0, 0xffff);
    Node* kids[64];
    for (unsigned i = 0; i < 64; ++i) {
        node_set(far_tree[1 + i], nullptr, 300, 64000, 0x1002, 0, 0, 0x20000 + i);
        if (jets) {
            put(far_tree[1 + i].bytes, score::flags130_offset, score::jet_flags);
            put(far_tree[1 + i].bytes, fcore::scale70_offset, 93922);
            put(far_tree[1 + i].bytes, fcore::scale80_offset, 0x10000);
            put(far_tree[1 + i].bytes, fcore::scale88_offset, 0x20000);
            put(far_tree[1 + i].bytes, fcore::basis_x_offset, 0x10000);
            put(far_tree[1 + i].bytes, fcore::basis_z_offset + 8, 0x10000);
        }
        kids[i] = &far_tree[1 + i];
    }
    link_traversal(far_tree[0], kids, 64);
    std::memcpy(far_tree_initial, far_tree, sizeof far_tree);
    LARGE_INTEGER f{}, s{}, e{};
    QueryPerformanceFrequency(&f);
    auto once = [&] {
        std::memcpy(far_tree, far_tree_initial, sizeof far_tree);
        fj::begin_frame();
        run(far_tree[0], v);
    };
    for (unsigned i = 0; i < 64; ++i) once();
    QueryPerformanceCounter(&s);
    for (unsigned i = 0; i < loops; ++i) once();
    QueryPerformanceCounter(&e);
    return double(e.QuadPart - s.QuadPart) * 1e6 / double(f.QuadPart) / double(loops);
}
static void far_section(std::uintptr_t site, std::uintptr_t cull, View& view, const Node* replay_initial) {
    // The native outputs at this function's stack depth: the synthetic pass's final `add esp,0x14` sets PF from the
    // stack address, so the baseline is taken here, unpatched, before the re-install.
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    const Result native = run(replay[0], view);
    check(!std::memcmp(replay, replay_native, sizeof(Node) * (kRowCount + 1)), "far jets: the unpatched baseline is native");
    check(small::install_at(site, cull, true, true) && !std::strcmp(small::state(), "ok") && small::projectiles_exempt() &&
              small::far_jets(),
          "far jets: re-install with the far block");
    {
        const std::uint32_t at = std::uint32_t(small::stub_address()), slot = (at + score::stub_length + 3) & ~3u;
        unsigned char want[score::stub_length];
        score::encode_stub(at, addr(const_cast<std::int32_t*>(&x3m_cull_small_parts_threshold)),
                           addr(const_cast<std::int32_t*>(&x3m_cull_small_parts_upper)),
                           addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_culled)),
                           addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_exempt)),
                           addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_dock_culled)), std::uint32_t(cull), slot,
                           want, true, addr(reinterpret_cast<const void*>(&x3m_engine_far_jet)), true);
        std::int32_t rel = 0;
        std::memcpy(&rel, want + score::stub_far_call + 1, 4);
        check(!std::memcmp(reinterpret_cast<const void*>(at), want, score::stub_length) && want[score::stub_far] == 0xf7 &&
                  want[score::stub_far_call] == 0xe8 &&
                  at + score::stub_far_call + 5 + std::uint32_t(rel) == addr(reinterpret_cast<const void*>(&x3m_engine_far_jet)),
              "far jets: stub bytes as encoded (both bit tests live, the call reaches the handler)");
    }
    // The view's handle and context (the context's +0x2c: the view's context scale, read outside the pass).
    std::memset(far_context.bytes, 0, sizeof far_context.bytes);
    const float scale = 0.01f;
    std::memcpy(far_context.bytes + fcore::context_scale_offset, &scale, 4);
    put(view.bytes, fcore::view_handle_offset, 0xc0de0028u);
    put(view.bytes, fcore::view_context_offset, addr(&far_context));
    small::after_reset(kRowsWidth);
    check(small::set_px(2.0) && small::set_dock_px(0.0), "far jets: 2 px, dock rule off");
    small::begin_frame();
    // The record row: the first pair row the engine keeps below the threshold carries the node fields of a flight-A jet.
    int j0 = -1;
    const unsigned jet_kept = kept_jet_below(replay_native, 3, &j0);
    auto prepare = [&]() {
        std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
        set_jet_bits(replay, true);
        if (j0 >= 0) {
            unsigned char* n = replay[1 + j0].bytes;
            put(n, fcore::handle_offset, 0x51a7e000u);
            put(n, fcore::scale70_offset, 93922);
            put(n, fcore::scale80_offset, 0x10000);
            put(n, fcore::scale88_offset, 0x20000);
            const std::int32_t position[3] = {-12384560, -931560, -3175560};
            const std::int32_t bx[3] = {0, 0x10000, 0}, bz[3] = {46341, 0, -46341}; // model z = (1, 0, -1) / sqrt 2
            for (unsigned k = 0; k < 3; ++k) {
                put(n, fcore::position_offset + 4 * k, std::uint32_t(position[k]));
                put(n, fcore::basis_x_offset + 4 * k, std::uint32_t(bx[k]));
                put(n, fcore::basis_z_offset + 4 * k, std::uint32_t(bz[k]));
            }
        }
    };
    // Disarmed (no device requested the stage): the handler is called for every pair row below the threshold and copies
    // nothing; the class flips exactly as without the block.
    {
        far_arm(false);
        fj::begin_frame();
        prepare();
        SetLastError(0x5155);
        const Result r = run(replay[0], view);
        set_jet_bits(replay, false);
        const auto st = fj::stats();
        check(GetLastError() == 0x5155 && r.preserved && r.x87_empty && same_outputs(r, native),
              "far jets disarmed: LastError, registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
        check(fj::count() == 0 && st.disarmed == rows_below_jet(3) && rows_below_jet(3) > 0 && st.written == 0,
              "far jets disarmed: one call per pair row below the threshold, nothing copied");
    }
    // Armed: exactly the pair rows the engine keeps are copied; the engine-culled pair rows counted; single-bit rows
    // never; every node below the threshold still culled (the engine never submits a far jet).
    {
        far_arm(true);
        fj::begin_frame();
        small::begin_frame(); // the stub's per-frame counts start again
        prepare();
        SetLastError(0x5156);
        const Result r = run(replay[0], view);
        check(GetLastError() == 0x5156, "far jets armed: LastError preserved across the pass and the handler calls");
        check(r.preserved && r.x87_empty && same_outputs(r, native),
              "far jets armed: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
        const auto st = fj::stats();
        check(fj::count() == jet_kept && st.written == jet_kept && jet_kept > 0 && st.engine == rows_below_jet(3) - jet_kept &&
                  st.steering == 0 && st.overflow == 0 && st.disarmed == 0 && x3m_cull_small_parts_culled == rows_below(3),
              "far jets armed: the kept pair rows copied, the engine-culled pair rows counted, nothing else");
        bool rows_ok = true, tags_ok = true;
        const fcore::Raw* e = fj::entries();
        for (unsigned k = 0; k < fj::count(); ++k) {
            const unsigned i = unsigned((e[k].node - addr(&replay[1])) / sizeof(Node));
            rows_ok = rows_ok && i < kRowCount && row_jet(i) && kRows[i].s < 3 &&
                      (get(replay_native[1 + i].bytes, ccore::flags12c_offset) & 2u) && e[k].model == 0x10000 + i;
            tags_ok = tags_ok && e[k].view_handle == 0xc0de0028u && e[k].context == addr(&far_context) &&
                      e[k].parent == get(replay[1 + i].bytes, ccore::parent_offset);
        }
        check(rows_ok, "far jets armed: every copy is a pair row below the threshold the engine keeps (no single-bit row)");
        check(tags_ok, "far jets armed: the view's handle and context and the parent with every copy");
        set_jet_bits(replay, false);
        Node* rj = j0 >= 0 ? &replay[1 + j0] : nullptr;
        if (rj) { // the record row's extra fields out again before the flip compare
            Node& n = *rj;
            const Node& ref = replay_native[1 + j0];
            for (unsigned off : {fcore::handle_offset, fcore::scale70_offset, fcore::scale80_offset, fcore::scale88_offset})
                put(n.bytes, off, get(ref.bytes, off));
            for (unsigned k = 0; k < 3; ++k)
                for (unsigned off : {fcore::position_offset, fcore::basis_x_offset, fcore::basis_z_offset})
                    put(n.bytes, off + 4 * k, get(ref.bytes, off + 4 * k));
        }
        const Flip f = replay_compare(replay_native);
        check(f.other_changes == 0 && replay_flipped_exactly(replay_native, 3) && f.flipped == 97 && f.draws == 403,
              "far jets armed: the same 97-node / 403-draw class flips (a far jet stays culled: no draw)");
        // The record of the flight-A jet: origin = +0xb0 x 0.01, axis = -(model z), size = 939.22, s 1, z 2.
        const fcore::Raw* raw = nullptr;
        for (unsigned k = 0; k < fj::count(); ++k)
            if (rj && e[k].node == addr(rj)) raw = &e[k];
        ee::Record rec{};
        const ee::FarVerdict v = raw ? ee::far_record(*raw, scale, -1, nullptr, 4999, &rec) : ee::FarVerdict::invalid;
        const float h = 0.70710678f;
        const bool record_ok = v == ee::FarVerdict::record && std::fabs(rec.origin[0] + 123845.6f) < 0.01f &&
                               std::fabs(rec.origin[1] + 9315.6f) < 0.01f && std::fabs(rec.origin[2] + 31755.6f) < 0.01f &&
                               std::fabs(rec.axis[0] + h) < 1e-4f && std::fabs(rec.axis[1]) < 1e-6f && std::fabs(rec.axis[2] - h) < 1e-4f &&
                               std::fabs(rec.size - 939.22f) < 0.01f && rec.s == 1.f && rec.z == 2.f && std::fabs(rec.ratio - 2.f) < 1e-3f &&
                               rec.node_handle == 0x51a7e000u && rec.model == 0x10000u + unsigned(j0) && rec.body == -1 &&
                               (rec.flags & ee::flag_far) && (rec.flags & ee::flag_unknown_body) && !(rec.flags & ee::flag_steering) &&
                               rec.serial == 0 && rec.frame == 4999;
        std::printf("FAR record origin=%.3f,%.3f,%.3f axis=%.5f,%.5f,%.5f size=%.3f s=%.3f z=%.3f ratio=%.4f flags=%04x copies=%u engine=%u\n",
                    double(rec.origin[0]), double(rec.origin[1]), double(rec.origin[2]), double(rec.axis[0]), double(rec.axis[1]),
                    double(rec.axis[2]), double(rec.size), double(rec.s), double(rec.z), double(rec.ratio), unsigned(rec.flags),
                    fj::count(), st.engine);
        check(record_ok, "far jets: the record of a culled jet carries the expected origin, axis, size, throttle and identity");
        // A context scale outside (0, 1) or a zero basis is no record; v/00566 or a SMALLJET table entry is steering.
        ee::Record none{};
        check(raw && ee::far_record(*raw, 0.f, -1, nullptr, 0, &none) == ee::FarVerdict::invalid &&
                  ee::far_record(*raw, 1.5f, -1, nullptr, 0, &none) == ee::FarVerdict::invalid,
              "far jets: an unusable context scale is no record");
        if (raw) {
            fcore::Raw zero = *raw;
            zero.basis_z[0] = zero.basis_z[1] = zero.basis_z[2] = 0;
            ee::Body smalljet{};
            smalljet.lists = ee::list_smalljet;
            check(ee::far_record(zero, scale, -1, nullptr, 0, &none) == ee::FarVerdict::invalid &&
                      ee::far_record(*raw, scale, 3, &smalljet, 0, &none) == ee::FarVerdict::steering,
                  "far jets: a zero basis is no record; a SMALLJET table entry is steering");
        }
        small::present(7, 5000, true);
        check(!small_lines.empty() && small_lines.back().find(" far_jets=on") != std::string::npos,
              "far jets: the frame row says far_jets=on");
    }
    // v/00566 (the RCS body) is never copied.
    {
        fj::begin_frame();
        prepare();
        if (j0 >= 0) put(replay[1 + j0].bytes, ccore::model_offset, fcore::steering_model);
        run(replay[0], view);
        const auto st = fj::stats();
        check(st.steering == 1 && fj::count() == jet_kept - 1, "far jets: v/00566 skipped (steering=1)");
    }
    // The device count: a second device's request and withdrawal leave the far block armed for the fixture's own;
    // the device that claimed the resolve alone empties the buffer until it withdraws.
    {
        bool other = false;
        fj::request(&other, true, &other);
        fj::request(&other, true, &other); // idempotent per device
        fj::claim(&other);
        const bool owner_only = fj::clears(&other) && !fj::clears(&far_counted);
        fj::request(&other, false, &other);
        const bool still = x3m_engine_far_armed == 1 && fj::clears(&far_counted);
        far_arm(false);
        const bool last = x3m_engine_far_armed == 0;
        far_arm(true);
        check(owner_only && still && last && x3m_engine_far_armed == 1,
              "far jets: armed while one device requests, the resolve's owner empties the buffer, its withdrawal frees it");
    }
    // The buffer's cap: a direct call on a kept JET node beyond 1,024 copies counts overflow.
    {
        fj::begin_frame();
        Node lone{};
        node_set(lone, nullptr, 300, 64000, 0x1002, 0, 0, 0x30000);
        put(lone.bytes, score::flags130_offset, score::jet_flags);
        for (unsigned k = 0; k < fcore::capacity + 3; ++k) x3m_engine_far_jet(addr(&lone), 6, addr(&view));
        x3m_engine_far_jet(addr(&lone), 0, addr(&view)); // measure 0 without 0x4000000: the engine's own degenerate cull
        const auto st = fj::stats();
        check(fj::count() == fcore::capacity && st.written == fcore::capacity && st.overflow == 3 && st.engine == 1,
              "far jets: 1,024 copies a frame, the rest counted overflow; the engine's degenerate cull mirrored");
        fj::begin_frame();
        check(fj::count() == 0 && fj::stats().overflow == 0, "far jets: begin_frame empties the buffer and the counts");
    }
    // The cost per node below the threshold with and without a far record (armed, the copy included).
    {
        View bv;
        view_set(bv, 1280, 0, 0x4000, 2);
        put(bv.bytes, fcore::view_handle_offset, 0xc0de0029u);
        put(bv.bytes, fcore::view_context_offset, addr(&far_context));
        small::publish(0.8f, 1280, 0x4000, true); // 4 px at m00 0.8 / 1280: threshold 5
        check(small::set_px(4.0), "far bench: 4 px");
        small::publish(0.8f, 1280, 0x4000, true);
        const double plain = far_bench_us(false, bv, 20000), jets = far_bench_us(true, bv, 20000);
        const auto st = fj::stats();
        check(st.written == 64 && fj::count() == 64, "far bench: every child copied in the JET tree");
        std::printf("FAR BENCH nodes=64 culled_plain_us=%.4f culled_far_us=%.4f per_far_jet_ns=%.1f harness=fixture_call_included game_fps=unmeasured\n",
                    plain, jets, (jets - plain) * 1000. / 64.);
        far_arm(false);
        fj::begin_frame();
        small::present(7, 5001, false);
    }
    check(small::shutdown() && small_window_original(), "far jets: restore, rollback bytes exact");
    {
        std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
        set_jet_bits(replay, true);
        far_arm(true);
        fj::begin_frame();
        run(replay[0], view);
        set_jet_bits(replay, false);
        check(!std::memcmp(replay, replay_native, sizeof(Node) * (kRowCount + 1)) && fj::count() == 0 &&
                  fj::stats().disarmed == 0,
              "far jets: after rollback every node as native, the handler never called");
        far_arm(false);
    }
}

int main() {
    DWORD old = 0;
    check(VirtualProtect(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(synthetic_measure_window) &
                                                 ~std::uintptr_t(0xfff)),
                         0x2000, PAGE_EXECUTE_READWRITE, &old) != FALSE,
          "synthetic code writable");
    check(small_window_original() && census_windows_original(),
          "synthetic pass carries the three engine windows byte-exact");
    check(synthetic_degenerate == synthetic_small_window + score::window_length + 5,
          "the window's trailing jmp lands on the degenerate test after five bytes");
    if (!small_window_original() || !census_windows_original()) {
        std::printf("CULL SMALL PARTS CPU checks=%u failures=%u\n", checks, failures);
        return 1;
    }
    const float m00 = bits_to_float(kRowsM00Bits);

    // ---- core rules ----
    check(score::threshold_for(2.0, m00, kRowsWidth) == 3 && score::threshold_for(4.0, m00, kRowsWidth) == 6 &&
              score::threshold_for(8.0, m00, kRowsWidth) == 11,
          "threshold rule at m00 3f4ccccc / 1280: 2 px -> 3, 4 px -> 6, 8 px -> 11");
    check(score::threshold_for(2.0, 0.8f, 1280) == 3 && score::threshold_for(2.0, 0.8f, 1920) == 2 &&
              score::threshold_for(1.0, 1.0f, 1280) == 1,
          "threshold rule: exact 0.8 and other widths");
    check(score::threshold_for(0.0, m00, 1280) == 0 && score::threshold_for(2.0, 0.0f, 1280) == 0 &&
              score::threshold_for(2.0, m00, 0) == 0 && score::threshold_for(65.0, m00, 1280) == 0,
          "threshold rule: unusable inputs give 0 (vanilla)");
    for (unsigned i = 0; i < kRowsExpectedCount; ++i)
        check(score::threshold_for(kRowsExpected[i].px, m00, kRowsWidth) == kRowsExpected[i].threshold_s,
              "threshold rule matches the tracked expectation");
    double px = 0;
    check(score::parse_px("2", &px) && px == 2.0 && score::parse_px("2.5", &px) && px == 2.5 &&
              !score::parse_px("2,5", &px) && !score::parse_px("", &px) && !score::parse_px("x", &px),
          "px parser");

    // ---- replay tree and the native reference ----
    replay = static_cast<Node*>(
        VirtualAlloc(nullptr, sizeof(Node) * (kRowCount + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    parents = static_cast<Node*>(
        VirtualAlloc(nullptr, sizeof(Node) * kRowCount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    replay_native = static_cast<Node*>(
        VirtualAlloc(nullptr, sizeof(Node) * (kRowCount + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(replay && parents && replay_native, "replay pools");
    View view;
    replay_build(view);
    Node* replay_initial = static_cast<Node*>(
        VirtualAlloc(nullptr, sizeof(Node) * (kRowCount + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memcpy(replay_initial, replay, sizeof(Node) * (kRowCount + 1));
    const Result native = run(replay[0], view);
    check(native.preserved && native.x87_empty, "native replay: callee-saved registers, ESP and empty x87 stack");
    std::memcpy(replay_native, replay, sizeof(Node) * (kRowCount + 1));
    {
        unsigned mismatched = 0, kept = 0, draws_kept = 0;
        for (unsigned i = 0; i < kRowCount; ++i) {
            if (verdict_of(replay[1 + i], kRows[i]) != kRows[i].verdict) ++mismatched;
            if (kRows[i].verdict == 0) {
                ++kept;
                draws_kept += unsigned(kRows[i].draws);
            }
        }
        check(mismatched == 0, "native replay: the synthetic pass reproduces the engine's verdict of every row");
        std::printf("REPLAY rows=%u native_mismatches=%u kept=%u draws_joined=%u\n", kRowCount, mismatched, kept,
                    draws_kept);
    }

    // ---- the dock-port tree's native reference (nothing installed yet) ----
    dock_build();
    dock_native_result = dock_run();
    std::memcpy(dock_native, dock_tree, sizeof dock_tree);
    {
        unsigned kept = 0;
        for (unsigned i = 0; i < dock_count; ++i) {
            const std::int32_t s = std::int32_t(get(dock_tree[1 + i].bytes, 0xa0)) / 100;
            if ((get(dock_tree[1 + i].bytes, ccore::flags12c_offset) & 2u) && s == dock_cases[i].s) ++kept;
        }
        check(dock_native_result.preserved && dock_native_result.x87_empty && kept == dock_count,
              "dock tree, native: every node kept, callee-saved registers, ESP and x87 as native");
    }

    // ---- option off / invalid / engine site absent / changed bytes ----
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", nullptr);
    check(!small::initialize() && !std::strcmp(small::state(), "disabled") && small_window_original(),
          "unset variable: disabled, site untouched");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", L"0");
    check(!small::initialize() && !std::strcmp(small::state(), "disabled"), "X3M_CULL_SMALL_PARTS_PX=0: disabled");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", L"abc");
    check(!small::initialize() && !std::strcmp(small::state(), "invalid_px"), "non-numeric setting: invalid_px");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", L"65");
    check(!small::initialize() && !std::strcmp(small::state(), "invalid_px"), "65 px: invalid_px (band)");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PX", L"2");
    check(!small::initialize() && !std::strcmp(small::state(), "bytes_mismatch") && small::stub_address() == 0,
          "engine site absent in this process: bytes_mismatch, nothing patched");
    check(install_lines.back().find(" scope=all") != std::string::npos,
          "the install line says scope=all (the only scope)");
    check(
        install_lines.back().find(" projectiles=marker_mismatch") != std::string::npos,
        "unset projectiles: on by default, turned off when the engine's marker instructions are absent (this process)");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PROJECTILES", L"missiles");
    check(!small::initialize() && !std::strcmp(small::state(), "invalid_projectiles") && small::stub_address() == 0 &&
              install_lines.back().find(" projectiles=invalid") != std::string::npos,
          "unknown projectiles value: invalid_projectiles, nothing patched");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PROJECTILES", L"off");
    check(!small::initialize() && !std::strcmp(small::state(), "bytes_mismatch") &&
              install_lines.back().find(" projectiles=off") != std::string::npos,
          "projectiles off: parsed and logged, the marker not consulted");
    SetEnvironmentVariableW(L"X3M_CULL_SMALL_PARTS_PROJECTILES", nullptr);
    // Far engine jets: requested only by X3M_ENGINE_EFFECTS exactly `plumes`; the JET writer's bytes are absent in this
    // process, so even then the block is left out (writer_mismatch).
    SetEnvironmentVariableW(L"X3M_ENGINE_EFFECTS", nullptr);
    check(!small::initialize() && install_lines.back().find(" far_jets=off") != std::string::npos,
          "far jets: X3M_ENGINE_EFFECTS unset: far_jets=off");
    for (const wchar_t* mode : {L"native", L"off", L"Plumes", L"plumes2"}) {
        SetEnvironmentVariableW(L"X3M_ENGINE_EFFECTS", mode);
        check(!small::initialize() && !std::strcmp(small::state(), "bytes_mismatch") &&
                  install_lines.back().find(" far_jets=off") != std::string::npos,
              "far jets: native, off and refused words: far_jets=off");
    }
    SetEnvironmentVariableW(L"X3M_ENGINE_EFFECTS", L"plumes");
    check(!small::initialize() && !std::strcmp(small::state(), "bytes_mismatch") &&
              install_lines.back().find(" far_jets=writer_mismatch") != std::string::npos,
          "far jets: plumes requests the block; the JET writer absent in this process leaves it out (writer_mismatch)");
    SetEnvironmentVariableW(L"X3M_ENGINE_EFFECTS", nullptr);
    const std::uintptr_t site = addr(small_site()), cull = addr(small_cull());
    synthetic_small_window[2] ^= 1;
    check(!small::install_at(site, cull, true) && !std::strcmp(small::state(), "bytes_mismatch") &&
              small_window_original() == false,
          "changed window byte: bytes_mismatch");
    synthetic_small_window[2] ^= 1;
    check(small_window_original(), "window byte restored");
    check(!small::install_at(0, cull, true) && !std::strcmp(small::state(), "invalid_site"), "null site refused");
    check(!small::install_at(site, cull + 1, true) && !std::strcmp(small::state(), "invalid_site"),
          "cull target not at window offset 47 refused");

    // ---- install ----
    check(small::install_at(site, cull, true) && !std::strcmp(small::state(), "ok") && small::projectiles_exempt(),
          "install_at synthetic site (projectiles exempt)");
    if (!small::stub_address()) {
        std::printf("FAIL install state=%s\nCULL SMALL PARTS CPU checks=%u failures=%u\n", small::state(), checks,
                    failures + 1);
        return 1;
    }
    check(small_site()[0] == 0xe9 && !std::memcmp(synthetic_small_window, score::window, score::site_offset) &&
              !std::memcmp(synthetic_small_window + score::site_offset + 5, score::window + score::site_offset + 5,
                           score::window_length - score::site_offset - 5),
          "site is jmp dispatcher; every other window byte untouched");
    {
        const std::uint32_t at = std::uint32_t(small::stub_address()), slot = (at + score::stub_length + 3) & ~3u;
        unsigned char want[score::stub_length];
        score::encode_stub(at, addr(const_cast<std::int32_t*>(&x3m_cull_small_parts_threshold)),
                           addr(const_cast<std::int32_t*>(&x3m_cull_small_parts_upper)),
                           addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_culled)),
                           addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_exempt)),
                           addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_dock_culled)), std::uint32_t(cull), slot,
                           want, true, addr(reinterpret_cast<const void*>(&x3m_engine_far_jet)), false);
        check(!std::memcmp(reinterpret_cast<const void*>(at), want, score::stub_length) && !small::far_jets() &&
                  want[score::stub_far] == 0xeb && score::stub_far + 2 + want[score::stub_far + 1] == score::stub_count,
              "stub bytes as encoded (projectiles on, far jets off: jmp over the far block)");
        check(*reinterpret_cast<void**>(slot) != nullptr, "continuation slot points at the tail");
    }
    check(!small::install_at(site, cull, true) && !std::strcmp(small::state(), "already_installed"),
          "second install refused");
    check(x3m_cull_small_parts_threshold == 0 && x3m_cull_small_parts_upper == 0 && small::stats().threshold == 0 &&
              small::stats().dock_threshold == 0 && small::requested_dock_px() == 0.0,
          "installed with the threshold at 0, the dock rule unset (off)");
    check(small::requested_px() == 2.0, "requested px carried from the setting");

    // ---- patched, threshold 0: identical ----
    replay_reset();
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    Result patched = run(replay[0], view);
    check(patched.preserved && patched.x87_empty && same_outputs(patched, native),
          "patched, threshold 0: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
    check(!std::memcmp(replay, replay_native, sizeof(Node) * (kRowCount + 1)),
          "patched, threshold 0: every node as native");
    check(x3m_cull_small_parts_culled == 0, "patched, threshold 0: nothing counted");

    // ---- begin_frame through the camera seam: no camera -> 0, invalid -> 0, valid -> the threshold ----
    small::set_backbuffer_width(kRowsWidth);
    fixture_camera_available = false;
    small::begin_frame();
    check(x3m_cull_small_parts_threshold == 0, "begin_frame without the camera latch: vanilla frame");
    fixture_camera_available = true;
    fixture_camera_valid = false;
    fixture_camera_m00 = m00;
    small::begin_frame();
    check(x3m_cull_small_parts_threshold == 0, "begin_frame with an invalid projection: vanilla frame");
    fixture_camera_valid = true;
    small::begin_frame();
    check(x3m_cull_small_parts_threshold == 3 && small::stats().threshold == 3 && small::stats().width == kRowsWidth,
          "begin_frame with the run131 projection at 1280: threshold 3");
    check(small_lines.size() == 1 &&
              small_lines[0].find("cull_small_parts_value px=2 m00=0.799999952 width=1280 threshold=3") == 0 &&
              small_lines[0].find(" source=registry fallback=no_scene") != std::string::npos,
          "one cull_small_parts_value line on the first valid frame (registry: nothing latched since install)");
    small::after_reset(1920);
    check(x3m_cull_small_parts_threshold == 0 && small::stats().width == 1920, "after_reset: disarmed, new width");
    small::begin_frame();
    check(x3m_cull_small_parts_threshold == 2 && small_lines.size() == 2,
          "begin_frame after the reset: threshold 2 at 1920, a second value line");
    small::after_reset(kRowsWidth);
    small::begin_frame();
    check(x3m_cull_small_parts_threshold == 3 && small_lines.size() == 3,
          "back to 1280: threshold 3, a third value line");
    // ---- the engine's base FOV through the seam: F = 0x3470 (--fov default, 90 deg horizontal on 16:9) ----
    // The engine's s = r*640/D' with D' = D*F/0x4000, so px per s carries F/0x4000 = 0.8193: every threshold rises.
    check(score::threshold_for(2.0, m00, kRowsWidth, 0x3470) == 4 &&
              score::threshold_for(4.0, m00, kRowsWidth, 0x3470) == 7 &&
              score::threshold_for(8.0, m00, kRowsWidth, 0x3470) == 13,
          "threshold rule at F 0x3470: 2 px -> 4, 4 px -> 7, 8 px -> 13");
    check(score::threshold_for(2.0, m00, kRowsWidth, 0x4000) == 3 &&
              score::threshold_for(2.0, m00, kRowsWidth, 0x105) == 0 &&
              score::threshold_for(2.0, m00, kRowsWidth, 0x8001) == 0,
          "threshold rule: F 0x4000 is the default, an implausible F gives 0 (vanilla)");
    fixture_focus = 0x3470;
    small::begin_frame();
    check(x3m_cull_small_parts_threshold == 4 && small::stats().focus == 0x3470 && small_lines.size() == 4 &&
              small_lines[3].find("cull_small_parts_value px=2 m00=0.799999952 width=1280 threshold=4 focus=0x3470") ==
                  0,
          "begin_frame at F 0x3470: threshold 4, a value line naming the focus");
    {
        std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
        const Result focused = run(replay[0], view);
        const Flip f = replay_compare(replay_native);
        check(focused.preserved && focused.x87_empty && f.other_changes == 0 &&
                  replay_flipped_exactly(replay_native, 4),
              "2 px at F 0x3470: only kept nodes with s < 4 change, and every one of them");
        std::printf("REPLAY px=2 focus=0x3470 threshold=4 flipped=%u draws=%u other_changes=%u culled_count=%lu\n",
                    f.flipped, f.draws, f.other_changes, (unsigned long)x3m_cull_small_parts_culled);
    }
    fixture_focus = 0x4000;
    small::begin_frame();
    check(x3m_cull_small_parts_threshold == 3 && small::stats().focus == 0x4000 && small_lines.size() == 5 &&
              small_lines[4].find("cull_small_parts_value px=2 m00=0.799999952 width=1280 threshold=3 focus=0x4000") ==
                  0,
          "back to F 0x4000: threshold 3 again");
    // ---- the view's focus from the latched projection (zoom included), preferred over the registry base ----
    {
        const float m11_4000 = 1.33333337f; // cot(45 deg) / 0.75
        const float m11_3470 = float(1.0 / std::tan(0x3470 * 3.14159265358979323846 / 65536.0) / 0.75);
        const float m11_zoom = float(1.0 / std::tan(0x1a38 * 3.14159265358979323846 / 65536.0) / 0.75); // 0x3470 base,
                                                                                                        // zoom x2:
                                                                                                        // +0x298 =
                                                                                                        // 0x1a38
        check(score::focus_from_projection(m00, m11_4000) == 0x4000 &&
                  score::focus_from_projection(0.5f, m11_3470) == 0x3470 &&
                  score::focus_from_projection(0.375f, 1.33333337f) == 0x4000 &&
                  score::focus_from_projection(m00, m11_zoom) == 0x1a38,
              "focus from the projection: 0x4000, 0x3470 (5120x1440), 0x1a38 (zoom x2)");
        check(score::focus_from_projection(1.0f, 1.25f) == 0x4000 && score::focus_from_projection(0.0f, 1.3f) == 0 &&
                  score::focus_from_projection(0.8f, 0.0f) == 0 && score::focus_from_projection(0.8f, 500.0f) == 0,
              "focus from the projection: 5:4 plane (W = 1), unusable terms and F below 0x106 give 0");
        // ---- whose projection: run309 at 5120x1440 with --fov 0x3470. The live buffer at Present (begin_frame) holds
        // the HUD view (P[0] 0.375, F 0x4000); the motion route's scene Clear latches the scene view (P[0] 0.5,
        // F 0x3470). 8 px tells the three readings apart: scene 5, HUD at 0x4000 6, HUD P[0] at 0x3470 7. ----
        check(score::threshold_for(8.0, 0.5f, 5120, 0x3470) == 5 &&
                  score::threshold_for(8.0, 0.375f, 5120, 0x4000) == 6 &&
                  score::threshold_for(8.0, 0.375f, 5120, 0x3470) == 7,
              "8 px at 5120: scene 5, HUD 6, HUD P[0] with the scene F 7");
        const unsigned reads = fixture_focus_reads;
        check(small::set_px(8.0), "8 px for the projection-source cases");
        small::after_reset(5120);
        fixture_camera_m00 = 0.375f;
        fixture_camera_m11 = m11_4000;
        fixture_focus = 0x3470;
        small::begin_frame();
        check(
            x3m_cull_small_parts_threshold == 5 && small::stats().focus == 0x3470 && !small::stats().scene &&
                std::fabs(small::stats().m00 - 0.5f) < 1e-4f && fixture_focus_reads == reads + 1 &&
                small_lines.back().find(" threshold=5 focus=0x3470 source=registry fallback=reset") !=
                    std::string::npos &&
                small::stats().fallback == 2,
            "no scene latch: the registry F 0x3470 on the HUD projection's aspect (P[0] 0.5), 8 px -> 5, one registry read");
        small::note_scene_projection(0.5f, m11_3470);
        fixture_focus = 0x4000;
        small::begin_frame();
        check(
            x3m_cull_small_parts_threshold == 5 && small::stats().focus == 0x3470 && small::stats().scene &&
                small::stats().m00 == 0.5f && fixture_focus_reads == reads + 1 &&
                small_lines.back().find("m00=0.5 width=5120 threshold=5 focus=0x3470 source=scene fallback=none") !=
                    std::string::npos &&
                small::stats().fallback == 0,
            "HUD live, scene latched after it: the scene's P[0] 0.5 and F 0x3470 (5, not 6 or 7), no registry read, a value row naming the source");
        for (unsigned i = 0; i < score::scene_max_age; ++i) small::begin_frame();
        check(small::stats().scene && x3m_cull_small_parts_threshold == 5 && fixture_focus_reads == reads + 1,
              "the scene latch holds for scene_max_age frames without a scene Clear");
        small::begin_frame();
        check(!small::stats().scene && small::stats().focus == 0x4000 && x3m_cull_small_parts_threshold == 6 &&
                  fixture_focus_reads == reads + 2 && small::stats().fallback == 3 &&
                  small_lines.back().find(" source=registry fallback=aged") != std::string::npos,
              "aged out: the registry (0x4000 now) on the live HUD projection, 6");
        const double cot_zoom = 1.0 / std::tan(0x1a38 * 3.14159265358979323846 / 65536.0); // 0x3470 base, zoom x2:
                                                                                           // +0x298 = 0x1a38
        const float zoom00 = float(cot_zoom / (0.75 * 5120.0 / 1440.0)), zoom11 = float(cot_zoom / 0.75);
        small::note_scene_projection(zoom00, zoom11);
        small::begin_frame();
        std::printf("REPLAY scene zoom=2 px=8 width=5120 threshold=%ld focus=0x%lx value_row=%s\n",
                    static_cast<long>(x3m_cull_small_parts_threshold), static_cast<unsigned long>(small::stats().focus),
                    small_lines.back().c_str());
        check(small::stats().scene && small::stats().focus == 0x1a38 &&
                  x3m_cull_small_parts_threshold == score::threshold_for(8.0, zoom00, 5120, 0x1a38) &&
                  fixture_focus_reads == reads + 2 &&
                  small_lines.back().find(" focus=0x1a38 source=scene") != std::string::npos,
              "scene view at zoom x2: F 0x1a38 from its P[5] (zoom included), no registry read");
        small::after_reset(5120);
        check(x3m_cull_small_parts_threshold == 0, "after_reset: disarmed");
        small::begin_frame();
        check(!small::stats().scene && fixture_focus_reads == reads + 3,
              "after_reset drops the scene latch: the registry until the next scene Clear");
        small::note_scene_projection(0.8f, 0.0f);
        small::begin_frame();
        check(!small::stats().scene && fixture_focus_reads == reads + 4,
              "a scene projection without a usable P[5] is not latched");
        small::note_scene_projection(0.5f, m11_3470);
        fixture_camera_available = false;
        small::begin_frame();
        check(x3m_cull_small_parts_threshold == 0 && fixture_focus_reads == reads + 4,
              "a scene latch without a valid live projection: vanilla frame, nothing read");
        fixture_camera_available = true;
        fixture_camera_valid = false;
        small::begin_frame();
        check(x3m_cull_small_parts_threshold == 0, "a scene latch with an invalid live projection: vanilla frame");
        fixture_camera_valid = true;
        check(small::set_px(2.0), "back to 2 px");
        small::after_reset(kRowsWidth);
        fixture_camera_m00 = m00;
        fixture_camera_m11 = 0;
        fixture_focus = 0x4000;
        small::begin_frame();
        check(x3m_cull_small_parts_threshold == 3 && small::stats().focus == 0x4000 && !small::stats().scene &&
                  fixture_focus_reads == reads + 5,
              "back to the run131 frame: registry 0x4000, threshold 3");
    }
    small_lines.resize(3); // the rows below count from the three value lines above

    // ---- 2 px: exactly the 403-draw class flips ----
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    SetLastError(0x5150);
    patched = run(replay[0], view);
    check(GetLastError() == 0x5150, "LastError preserved across the armed pass");
    check(patched.preserved && patched.x87_empty && same_outputs(patched, native),
          "2 px: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
    {
        const Flip f = replay_compare(replay_native);
        check(f.other_changes == 0 && replay_flipped_exactly(replay_native, 3),
              "2 px: only kept nodes with s < 3 change, and every one of them");
        check(f.flipped == kRowsExpected[0].nodes && f.draws == 403 && f.draws == kRowsExpected[0].draws,
              "2 px: the flipped class is 97 nodes / 403 draws");
        check(
            x3m_cull_small_parts_culled == rows_below(3) && rows_below(3) == 1147 && rows_below(3) > f.flipped,
            "2 px: the stub's count is every node below the threshold (1147: the 97 flipped plus the 1050 the engine culls itself)");
        std::printf("REPLAY px=2 threshold=3 flipped=%u draws=%u other_changes=%u culled_count=%lu\n", f.flipped,
                    f.draws, f.other_changes, (unsigned long)x3m_cull_small_parts_culled);
    }
    small::present(7, 4991, true);
    check(
        small_lines.size() == 4 &&
            small_lines[3].find(
                "cull_small_parts_frame device=7 frame=4991 px=2 threshold=3 culled=1147 m00=0.799999952 width=1280 scope=all projectiles=on exempt_bullet=0") ==
                0,
        "capture frame: one cull_small_parts_frame row (no marked row: exempt_bullet=0)");
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
        check(patched.preserved && same_outputs(patched, native) && f.other_changes == 0 &&
                  replay_flipped_exactly(replay_native, kRowsExpected[k].threshold_s),
              "px class: only the class below the threshold flips");
        check(f.flipped == kRowsExpected[k].nodes && f.draws == kRowsExpected[k].draws &&
                  x3m_cull_small_parts_culled == rows_below(kRowsExpected[k].threshold_s),
              "px class: node and draw counts as tracked, the stub's count every node below the threshold");
        std::printf("REPLAY px=%g threshold=%ld flipped=%u draws=%u other_changes=%u\n", kRowsExpected[k].px,
                    (long)x3m_cull_small_parts_threshold, f.flipped, f.draws, f.other_changes);
        small::present(7, 5000 + k, false);
    }
    check(!small::set_px(0.0) && !small::set_px(65.0), "set_px outside the band refused");

    // ---- projectiles: marked rows below the threshold keep the engine's verdict ----
    check(small::set_px(2.0), "projectiles: back to 2 px");
    small::begin_frame();
    {
        unsigned marked_draws = 0;
        const unsigned marked_kept = kept_marked_below(replay_native, 3, &marked_draws);
        std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
        mark_rows(replay);
        SetLastError(0x5152);
        patched = run(replay[0], view);
        check(GetLastError() == 0x5152, "projectiles: LastError preserved across the armed pass");
        check(patched.preserved && patched.x87_empty && same_outputs(patched, native),
              "projectiles: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
        check(
            x3m_cull_small_parts_exempt == rows_below_marked(3) &&
                x3m_cull_small_parts_culled + x3m_cull_small_parts_exempt == rows_below(3) && rows_below_marked(3) > 0,
            "projectiles: every marked node below the threshold counted exempt, the rest culled (together every node below it)");
        unmark_rows(replay);
        const Flip f = replay_compare(replay_native);
        check(f.other_changes == 0 && replay_flipped_exactly_unmarked(replay_native, 3),
              "projectiles: only unmarked kept nodes below the threshold flip; marked ones keep the engine's verdict");
        check(marked_kept > 0 && f.flipped == kRowsExpected[0].nodes - marked_kept &&
                  f.draws == kRowsExpected[0].draws - marked_draws,
              "projectiles: the flipped class shrinks by exactly the marked kept rows");
        std::printf(
            "REPLAY projectiles=on px=2 threshold=3 marked_below=%u marked_kept=%u flipped=%u draws=%u exempt_count=%lu culled_count=%lu\n",
            rows_below_marked(3), marked_kept, f.flipped, f.draws, (unsigned long)x3m_cull_small_parts_exempt,
            (unsigned long)x3m_cull_small_parts_culled);
        small::present(7, 4995, true);
        char want_row[160];
        std::snprintf(want_row, sizeof want_row, "scope=all projectiles=on exempt_bullet=%u", rows_below_marked(3));
        check(!small_lines.empty() && small_lines.back().find(want_row) != std::string::npos &&
                  x3m_cull_small_parts_exempt == 0,
              "projectiles: the frame row carries exempt_bullet=, present clears it");
    }

    // ---- far jets off (engine_effects native or off, the default): JET rows are culled, nothing is handed over ----
    small::begin_frame();
    {
        far_arm(true);
        x3m::engine_far_jets::begin_frame();
        std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
        set_jet_bits(replay, true);
        SetLastError(0x5153);
        patched = run(replay[0], view);
        check(GetLastError() == 0x5153, "far jets off: LastError preserved across the armed pass");
        check(patched.preserved && patched.x87_empty && same_outputs(patched, native),
              "far jets off: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
        set_jet_bits(replay, false);
        const Flip f = replay_compare(replay_native);
        check(f.other_changes == 0 && replay_flipped_exactly(replay_native, 3) && f.flipped == 97 && f.draws == 403,
              "far jets off: the full 97-node / 403-draw class flips, JET flags or not");
        const auto st = x3m::engine_far_jets::stats();
        check(x3m::engine_far_jets::count() == 0 && st.written == 0 && st.disarmed == 0 && st.engine == 0 &&
                  x3m_cull_small_parts_culled == rows_below(3),
              "far jets off: the handler is never called (nothing copied, nothing counted), even armed");
        small::present(7, 4998, true);
        check(!small_lines.empty() && small_lines.back().find(" far_jets=off") != std::string::npos,
              "far jets off: the frame row says far_jets=off");
        far_arm(false);
    }

    // ---- the census and the stub armed together ----
    check(census::install_at(addr(synthetic_measure_window + ccore::measure_site_offset),
                             addr(synthetic_exit_window + ccore::exit_site_offset)) &&
              !std::strcmp(census::state(), "ok"),
          "census installed beside the stub (disjoint claims)");
    check(small::set_px(2.0), "back to 2 px");
    census::begin_frame(true);
    small::begin_frame();
    check(x3m_cull_small_parts_threshold == 3 && census::stats().armed, "both armed");
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    patched = run(replay[0], view);
    check(patched.preserved && patched.x87_empty && same_outputs(patched, native),
          "both armed: registers, ESP, x87 and outputs as native");
    {
        const Flip f = replay_compare(replay_native);
        check(f.other_changes == 0 && f.flipped == 97 && f.draws == 403, "both armed: the same 403-draw class flips");
        const census::Stats s = census::stats();
        check(s.entries == kRowCount && s.exited == kRowCount && s.unmeasured == 1 && s.overflow == 0,
              "both armed: every row measured and exited, the root unmeasured");
        census::present(7, 4991, true);
        small::present(7, 4991, true);
        std::vector<Row> rows;
        for (const std::string& line : census_entry_lines) {
            Row r{};
            if (parse_row(line, r)) rows.push_back(r);
        }
        check(rows.size() == kRowCount, "both armed: one census row per replayed node");
        unsigned fidelity = 0, small_verdicts = 0, kept = 0, size = 0, min = 0, other = 0, renderable_small = 0;
        for (unsigned i = 0; i < rows.size() && i < kRowCount; ++i) {
            const Row& r = rows[i];
            const RowData& d = kRows[i];
            if (r.node == addr(&replay[1 + i]) && r.s == d.s && r.measure == d.measure && r.d == d.d &&
                r.radius == d.radius && r.limit == d.limit && r.thr_1d8 == d.thr_1d8 && r.thr_1dc == d.thr_1dc &&
                r.flags_in == d.flags_in)
                ++fidelity;
            if (!std::strcmp(r.verdict, "culled_small")) {
                ++small_verdicts;
                if (r.flags_out & 2) ++renderable_small;
            } else if (!std::strcmp(r.verdict, "kept"))
                ++kept;
            else if (!std::strcmp(r.verdict, "culled_size"))
                ++size;
            else if (!std::strcmp(r.verdict, "culled_min"))
                ++min;
            else
                ++other;
        }
        check(
            fidelity == kRowCount,
            "both armed: the census rows carry the engine's own s, measure, D, radius, thresholds and limit of every row");
        check(small_verdicts == 97 && renderable_small == 0,
              "both armed: 97 rows culled_small, every one with the renderable bit clear");
        {
            unsigned scoped = 0;
            for (const std::string& line : census_entry_lines)
                if (line.find("verdict=culled_small scope=all") != std::string::npos) ++scoped;
            check(scoped == 97, "both armed: every culled_small row carries scope=all");
        }
        check(kept == 164 - 97 && size == 624 && min == 426 && other == 0,
              "both armed: kept 67, culled_size 624, culled_min 426 (the engine's share unchanged)");
        std::printf("CENSUS rows=%u fidelity=%u kept=%u culled_size=%u culled_min=%u culled_small=%u other=%u\n",
                    (unsigned)rows.size(), fidelity, kept, size, min, small_verdicts, other);
        census_frame_lines.clear();
        census_entry_lines.clear();
    }
    // the census beside the stub on a marked tree: marked rows below the threshold are not culled_small, the frame row
    // counts them
    small::begin_frame();
    census::begin_frame(true);
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    mark_rows(replay);
    run(replay[0], view);
    census::present(7, 4996, true);
    small::present(7, 4996, true);
    {
        unsigned marked_draws = 0;
        const unsigned marked_kept = kept_marked_below(replay_native, 3, &marked_draws);
        unsigned small_verdicts = 0;
        for (const std::string& line : census_entry_lines)
            if (line.find("verdict=culled_small") != std::string::npos) ++small_verdicts;
        char want_row[64];
        std::snprintf(want_row, sizeof want_row, " culled_small_exempt_bullet=%u", rows_below_marked(3));
        check(census_entry_lines.size() == kRowCount && small_verdicts == 97 - marked_kept,
              "census on a marked tree: culled_small only for the unmarked flipped rows");
        check(census_frame_lines.size() == 1 && census_frame_lines[0].find(want_row) != std::string::npos,
              "census on a marked tree: the frame row counts every marked row below the threshold");
        std::printf("CENSUS projectiles=on culled_small=%u marked_kept=%u frame=%s\n", small_verdicts, marked_kept,
                    census_frame_lines.empty() ? "-" : census_frame_lines[0].c_str());
        census_frame_lines.clear();
        census_entry_lines.clear();
    }
    // the census alone (stub disarmed) reports the engine's verdicts again
    small::after_reset(kRowsWidth);
    census::begin_frame(true);
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    run(replay[0], view);
    check(!std::memcmp(replay, replay_native, sizeof(Node) * (kRowCount + 1)),
          "stub disarmed beside the armed census: every node as native");
    // X3M_CULL_SMALL_PROPS: the draw path reports skipped prop nodes; a kept row of such a node reads culled_prop,
    // an engine-culled row keeps its own verdict.
    std::uint32_t prop_kept = 0, prop_culled = 0;
    for (unsigned i = 0; i < kRowCount && (!prop_kept || !prop_culled); ++i) {
        const bool kept = (get(replay_native[1 + i].bytes, ccore::flags12c_offset) & 2u) != 0;
        if (kept && !prop_kept) prop_kept = addr(&replay[1 + i]);
        if (!kept && !prop_culled) prop_culled = addr(&replay[1 + i]);
    }
    census::note_culled_prop(prop_kept);
    census::note_culled_prop(prop_culled);
    census::note_culled_prop(0);
    census::present(7, 4993, true);
    {
        unsigned small_verdicts = 0, prop_verdicts = 0;
        char kept_node[32], culled_node[32];
        std::snprintf(kept_node, sizeof kept_node, " node=%08lx ", (unsigned long)prop_kept);
        std::snprintf(culled_node, sizeof culled_node, " node=%08lx ", (unsigned long)prop_culled);
        bool kept_named = false, culled_unchanged = false;
        for (const std::string& line : census_entry_lines) {
            if (line.find("verdict=culled_small") != std::string::npos) ++small_verdicts;
            if (line.find("verdict=culled_prop") != std::string::npos) ++prop_verdicts;
            if (line.find(kept_node) != std::string::npos) kept_named = line.find("verdict=culled_prop") != std::string::npos;
            if (line.find(culled_node) != std::string::npos)
                culled_unchanged = line.find("verdict=culled_prop") == std::string::npos &&
                                   line.find("verdict=kept") == std::string::npos;
        }
        check(small_verdicts == 0 && census_entry_lines.size() == kRowCount, "stub disarmed: no culled_small rows");
        check(prop_kept && prop_culled && prop_verdicts == 1 && kept_named && culled_unchanged,
              "culled_prop: exactly the reported kept node's row; an engine-culled node keeps its verdict");
        check(census_frame_lines.size() == 1 &&
                  census_frame_lines[0].find(" culled_prop_nodes=2 culled_prop_overflow=0") != std::string::npos,
              "culled_prop: the frame row counts the reported nodes");
        std::printf("CENSUS culled_prop rows=%u kept_named=%u culled_unchanged=%u\n", prop_verdicts, kept_named ? 1u : 0u,
                    culled_unchanged ? 1u : 0u);
        census_frame_lines.clear();
        census_entry_lines.clear();
    }
    // A report outside a captured frame is ignored.
    census::begin_frame(false);
    census::note_culled_prop(prop_kept);
    census::begin_frame(true);
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    run(replay[0], view);
    census::present(7, 4994, true);
    {
        unsigned prop_verdicts = 0;
        for (const std::string& line : census_entry_lines)
            if (line.find("verdict=culled_prop") != std::string::npos) ++prop_verdicts;
        check(prop_verdicts == 0 && census_frame_lines.size() == 1 &&
                  census_frame_lines[0].find(" culled_prop_nodes=0 ") != std::string::npos,
              "culled_prop: a report on an uncaptured frame never reaches the next captured frame");
        census_frame_lines.clear();
        census_entry_lines.clear();
    }
    // ---- carrier dock-port parts: 4 px -> threshold 5, 8 px -> dock threshold 10 (m00 0.8 at 1280, F 0x4000) ----
    {
        check(small::set_px(4.0) && small::set_dock_px(8.0) && !small::set_dock_px(65.0) && !small::set_dock_px(-1.0) &&
                  small::requested_dock_px() == 8.0,
              "dock: 4 px and dock 8 px set; 65 and -1 refused");
        const std::size_t value_rows = small_lines.size();
        small::publish(0.8f, 1280, 0x4000, true);
        check(x3m_cull_small_parts_threshold == 5 && x3m_cull_small_parts_upper == 10 && small::stats().dock_threshold == 10 &&
                  small_lines.size() == value_rows + 1 &&
                  small_lines.back().find(" threshold=5 ") != std::string::npos &&
                  small_lines.back().find(" dock_px=8 dock_threshold=10") != std::string::npos,
              "dock: thresholds 5 and 10 from the same conversion; the value row names dock_px and dock_threshold");
        SetLastError(0x5160);
        const Result armed = dock_run();
        check(GetLastError() == 0x5160, "dock: LastError preserved across the armed pass");
        check(armed.preserved && armed.x87_empty && same_outputs(armed, dock_native_result),
              "dock: callee-saved registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
        check(dock_flipped_exactly("sd"),
              "dock: exactly the in-range ids with 5 <= s < 10 and every id below 5 flip; bounds, neighbours, s = 10 and the marked node as native");
        check(x3m_cull_small_parts_culled == dock_class_count('s') && x3m_cull_small_parts_dock_culled == dock_class_count('d') &&
                  x3m_cull_small_parts_exempt == dock_class_count('e') && dock_class_count('d') == 7 && dock_class_count('s') == 3,
              "dock: culled=3 (4 px rule), dock_culled=7, exempt=1 (the marked dock-port node)");
        std::printf("DOCK px=4 dock_px=8 threshold=%ld upper=%ld culled=%lu dock_culled=%lu exempt=%lu flipped_exact=%u\n",
                    (long)x3m_cull_small_parts_threshold, (long)x3m_cull_small_parts_upper,
                    (unsigned long)x3m_cull_small_parts_culled, (unsigned long)x3m_cull_small_parts_dock_culled,
                    (unsigned long)x3m_cull_small_parts_exempt, dock_flipped_exactly("sd") ? 1u : 0u);
        small::present(7, 6000, true);
        check(!small_lines.empty() && small_lines.back().find("cull_small_parts_frame device=7 frame=6000 px=4 threshold=5 culled=3 ") == 0 &&
                  small_lines.back().find(" dock_px=8 dock_threshold=10 dock_culled=7") != std::string::npos &&
                  x3m_cull_small_parts_dock_culled == 0,
              "dock: the frame row carries dock_px, dock_threshold and dock_culled; present clears the count");
        // the census beside it names the dock rule's rows culled_dock
        census::begin_frame(true);
        small::publish(0.8f, 1280, 0x4000, true);
        dock_run();
        census::present(7, 6001, true);
        {
            unsigned dock_rows = 0, small_rows = 0, kept_rows = 0;
            for (const std::string& line : census_entry_lines) {
                if (line.find("verdict=culled_dock") != std::string::npos) ++dock_rows;
                if (line.find("verdict=culled_small") != std::string::npos) ++small_rows;
                if (line.find("verdict=kept") != std::string::npos) ++kept_rows;
            }
            check(census_entry_lines.size() == dock_count && dock_rows == 7 && small_rows == 3 && kept_rows == dock_count - 10,
                  "dock: census rows culled_dock 7, culled_small 3, kept 8");
            std::printf("CENSUS dock rows=%u culled_dock=%u culled_small=%u kept=%u\n", (unsigned)census_entry_lines.size(),
                        dock_rows, small_rows, kept_rows);
            census_frame_lines.clear();
            census_entry_lines.clear();
        }
        small::present(7, 6001, false);
        // the dock rule off at 0: in-range ids follow the 4 px rule only
        check(small::set_dock_px(0.0), "dock: 0 accepted (off)");
        small::publish(0.8f, 1280, 0x4000, true);
        check(x3m_cull_small_parts_upper == 5 && small::stats().dock_threshold == 0 &&
                  small_lines.back().find(" dock_px=0 dock_threshold=0") != std::string::npos,
              "dock off: upper equals the threshold, the value row says dock_threshold=0");
        SetLastError(0x5161);
        const Result off = dock_run();
        check(GetLastError() == 0x5161 && off.preserved && off.x87_empty && same_outputs(off, dock_native_result) &&
                  dock_flipped_exactly("s") && x3m_cull_small_parts_dock_culled == 0 &&
                  x3m_cull_small_parts_culled == dock_class_count('s') && x3m_cull_small_parts_exempt == 0,
              "dock off: only the three nodes below 5 flip, nothing counted by the dock rule");
        small::present(7, 6002, false);
        // a dock setting below the small one adds nothing (dock threshold 3 < 5)
        check(small::set_dock_px(2.0), "dock: 2 px");
        small::publish(0.8f, 1280, 0x4000, true);
        dock_run();
        check(x3m_cull_small_parts_upper == 5 && dock_flipped_exactly("s") && x3m_cull_small_parts_dock_culled == 0,
              "dock below the small setting: the 4 px rule alone");
        small::present(7, 6003, false);
        // a vanilla frame (Reset) disarms both
        check(small::set_dock_px(8.0), "dock: back to 8 px");
        small::after_reset(1280);
        check(x3m_cull_small_parts_threshold == 0 && x3m_cull_small_parts_upper == 0, "dock: after_reset disarms both words");
        dock_run();
        check(!std::memcmp(dock_tree, dock_native, sizeof dock_tree) && x3m_cull_small_parts_dock_culled == 0,
              "dock: disarmed, every node as native");
        small::present(7, 6004, false);
    }
    check(census::shutdown(), "census restored");
    check(census_windows_original(), "census sites back exactly");

    // ---- the 12-node bench tree (census fixture layout), W = 1280 ----
    View bench_view;
    view_set(bench_view, 1280, 0, 0x4000, 2);
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
    Node* r_children[] = {&A, &B, &C, &E, &F, &G, &H, &I, &J};
    link_parented(R, r_children, 9);
    Node* a_children[] = {&gA1, &gA2};
    link_parented(A, a_children, 2);
    for (unsigned i = 0; i < all_count; ++i) initial[i] = *all[i];
    // threshold 20 in s units: E (s 19), F (19) and J (5) are kept natively and flip; C fades either way; A/B/gA2 are
    // the engine's own culls
    x3m_cull_small_parts_threshold = 20;
    x3m_cull_small_parts_upper = 20;
    reset_tree();
    run(R, bench_view);
    check(!(get(E.bytes, 0x12c) & 2) && !(get(F.bytes, 0x12c) & 2) && !(get(J.bytes, 0x12c) & 2) &&
              (get(R.bytes, 0x12c) & 2) && (get(I.bytes, 0x12c) & 2) && (get(gA1.bytes, 0x12c) & 2) &&
              x3m_cull_small_parts_culled >= 3,
          "bench tree at threshold 20: E, F, J culled; R, I (saturated), gA1 kept");
    const double armed_us = bench_us(R, bench_view);
    // The dock rule armed (upper 40): nodes with 20 <= s < 40 also run the id read and the two range compares (the bench
    // ids are body-table ids, never dock ports, so the verdicts are those of threshold 20).
    x3m_cull_small_parts_upper = 40;
    reset_tree();
    run(R, bench_view);
    check(!(get(E.bytes, 0x12c) & 2) && !(get(F.bytes, 0x12c) & 2) && !(get(J.bytes, 0x12c) & 2) &&
              (get(R.bytes, 0x12c) & 2) && (get(I.bytes, 0x12c) & 2) && (get(gA1.bytes, 0x12c) & 2) &&
              x3m_cull_small_parts_dock_culled == 0,
          "bench tree with the dock rule armed (upper 40): the same verdicts, nothing dock-culled");
    const double armed_dock_us = bench_us(R, bench_view);
    x3m_cull_small_parts_threshold = 0;
    x3m_cull_small_parts_upper = 0;
    const double disarmed_us = bench_us(R, bench_view);

    // ---- rollback ----
    check(small::shutdown() && !std::strcmp(small::state(), "restored"), "shutdown restores");
    check(small_window_original(), "rollback bytes exact");
    std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
    const Result restored = run(replay[0], view);
    check(!std::memcmp(replay, replay_native, sizeof(Node) * (kRowCount + 1)) && same_outputs(restored, native) &&
              restored.preserved,
          "after rollback the native pass is back");
    std::memcpy(dock_tree, dock_initial, sizeof dock_tree);
    const Result dock_restored = dock_run();
    check(!std::memcmp(dock_tree, dock_native, sizeof dock_tree) && same_outputs(dock_restored, dock_native_result) &&
              dock_restored.preserved,
          "after rollback the dock tree is native");
    check(small::shutdown(), "second shutdown is a no-op");
    check(small::stats().threshold == 0, "not live: threshold 0");
    const double native_us = bench_us(R, bench_view);
    std::printf(
        "CULL SMALL PARTS BENCH native_pass_us=%.4f patched_disarmed_us=%.4f patched_armed_us=%.4f patched_armed_dock_us=%.4f nodes_per_pass=12 culled_per_armed_pass=7 harness=fixture_call_included game_fps=unmeasured\n",
        native_us, disarmed_us, armed_us, armed_dock_us);
    // ---- re-install, restore, then the closed window refuses ----
    // ---- projectiles off: marked nodes are culled like any node ----
    check(small::install_at(site, cull, false) && !std::strcmp(small::state(), "ok") && !small::projectiles_exempt(),
          "re-install with projectiles off");
    {
        const std::uint32_t at = std::uint32_t(small::stub_address()), slot = (at + score::stub_length + 3) & ~3u;
        unsigned char want[score::stub_length];
        score::encode_stub(at, addr(const_cast<std::int32_t*>(&x3m_cull_small_parts_threshold)),
                           addr(const_cast<std::int32_t*>(&x3m_cull_small_parts_upper)),
                           addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_culled)),
                           addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_exempt)),
                           addr(const_cast<std::uint32_t*>(&x3m_cull_small_parts_dock_culled)), std::uint32_t(cull), slot,
                           want, false, addr(reinterpret_cast<const void*>(&x3m_engine_far_jet)), false);
        check(!std::memcmp(reinterpret_cast<const void*>(at), want, score::stub_length) &&
                  want[score::stub_dock_projectile] == 0xeb &&
                  score::stub_dock_projectile + 2 + want[score::stub_dock_projectile + 1] == score::stub_dock_count &&
                  want[score::stub_projectile] == 0xeb &&
                  score::stub_projectile + 2 + want[score::stub_projectile + 1] == score::stub_far,
              "projectiles off: stub bytes as encoded (jmp over both marker tests)");
        small::after_reset(kRowsWidth);
        check(small::set_px(2.0), "projectiles off: 2 px");
        small::begin_frame();
        std::memcpy(replay, replay_initial, sizeof(Node) * (kRowCount + 1));
        mark_rows(replay);
        const Result off = run(replay[0], view);
        unmark_rows(replay);
        const Flip f = replay_compare(replay_native);
        check(off.preserved && off.x87_empty && same_outputs(off, native) && f.other_changes == 0 &&
                  replay_flipped_exactly(replay_native, 3) && f.flipped == 97 && f.draws == 403,
              "projectiles off: the full 97-node / 403-draw class flips, marked or not");
        check(x3m_cull_small_parts_exempt == 0 && x3m_cull_small_parts_culled == rows_below(3),
              "projectiles off: nothing exempt, every node below the threshold counted culled");
        small::present(7, 4997, true);
        check(!small_lines.empty() &&
                  small_lines.back().find("scope=all projectiles=off exempt_bullet=0") != std::string::npos,
              "projectiles off: the frame row says so");
        check(small::set_px(4.0) && small::set_dock_px(8.0), "projectiles off: 4 px, dock 8 px");
        small::publish(0.8f, 1280, 0x4000, true);
        const Result dock_off = dock_run();
        check(dock_off.preserved && dock_off.x87_empty && same_outputs(dock_off, dock_native_result) &&
                  dock_flipped_exactly("sde") && x3m_cull_small_parts_exempt == 0 &&
                  x3m_cull_small_parts_dock_culled == dock_class_count('d') + dock_class_count('e'),
              "projectiles off: the marked dock-port node is dock-culled like the others (dock_culled=8)");
        small::present(7, 6005, false);
    }
    check(small::shutdown() && small_window_original(), "projectiles off: restore, rollback bytes exact");
    far_section(site, cull, view, replay_initial);
    lens_section(site, cull, bench_view);
    engine_section(site, cull, bench_view);
    x3m::engine_patch::close_install_window("fixture");
    check(!small::install_at(site, cull, true) && !std::strcmp(small::state(), "late_claim") && small_window_original(),
          "closed install window: late_claim, site untouched");
    check(!eng::install_at(site, cull) && !std::strcmp(eng::state(), "late_claim") && small_window_original(),
          "engine: closed install window: late_claim, site untouched");
    check(!lens::install_at(site, cull, true) && !std::strcmp(lens::state(), "late_claim") && small_window_original(),
          "lens: closed install window: late_claim, site untouched");
    props_section();
    std::printf("CULL SMALL PARTS CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
