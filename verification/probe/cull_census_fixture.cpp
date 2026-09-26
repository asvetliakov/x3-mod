// X3 CPU fixture of the cull-census trampolines: a synthetic re-implementation
// of the engine's per-node cull and LOD pass 0x0047cfe0 whose two verified
// windows (0x0047d248..0x0047d266 and 0x0047d519..0x0047d533) are byte-exact,
// synthetic node trees, and the production module (src/proxy/cull_census.cpp,
// compiled separately with its production flags) patching that copy.
// Checks: the patched pass leaves every node with the same renderable bit,
// LOD index and flags as the unpatched pass and returns with the same
// EAX/ECX/EDX/EFLAGS; callee-saved registers, ESP and the empty x87 stack are
// preserved; the census rows carry the engine's own s, measure, thresholds,
// limit, verdict and LOD, and the model's LOD ladder (count and record
// thresholds) only where the pass resolved a model pointer (not for nodes
// culled with EBX still D, a null model, and never a fault for a count-0 model,
// an unreadable record or a hostile EBX); early-rejected nodes count as unmeasured; the ring
// never overflows silently (overflow= on the frame row); the body name of each
// model id from a synthetic body table (fixed, 9000..19999 and dynamic ids,
// slot out of range, invalid id range, null, no-access and page-edge name
// pointers, a name across two pages, no NUL within 256 bytes, the 63-character cap, one read set per id, a moved slot
// array, no manager, a wrong fixed count); disarmed frames record nothing; LastError preserved; exact rollback; option
// off untouched; changed window bytes refused; late window refused. Diagnostic timings only; not game FPS. Never
// launches the game.
#include "../../src/proxy/cull_census.h"
#include "../../src/proxy/cull_census_core.h"
#include "../../src/proxy/engine_patch.h"
#include "../../src/proxy/engine_memory.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdarg>
#include <string>
#include <vector>
static std::vector<std::string> frame_lines, entry_lines, switch_lines;
namespace x3m {
void log(const char* format, ...) {
    char text[512];
    std::va_list a;
    va_start(a, format);
    std::vsnprintf(text, sizeof text, format, a);
    va_end(a);
    if (!std::strncmp(text, "cull_census_frame ", 18)) {
        frame_lines.emplace_back(text);
        return;
    }
    if (!std::strncmp(text, "cull_census device=", 19)) {
        entry_lines.emplace_back(text);
        return;
    }
    if (!std::strncmp(text, "lod_switch", 10)) {
        switch_lines.emplace_back(text);
        return;
    }
    static unsigned lines = 0;
    if (lines++ < 8) std::printf("%s\n", text);
}
}
namespace x3m::object_trace {
bool executable_verified() {
    return true;
}
}
namespace census = x3m::cull_census;
namespace core = x3m::cull_census::core;

// ---- the synthetic pass: thiscall(ECX = node, view, flag) with the engine's frame layout ----
// Node fields as the engine's (core.h offsets) plus a fixture-private model
// pointer at +0x1f0 standing in for the 0x004863c0 lookup of +0x140; the model
// is the engine's layout (records at +0x0c, signed word count at +0x10, record
// threshold at +0x34) and the pass carries it in EBX and [ESP+0x14] exactly as
// 0x0047d2fd..0x0047d46e do; view fields: +0x5c W, +0x270 flags,
// +0x298 scale, +0x300 view-distance setting (the engine reads those from
// globals; the copy keeps them in the view block). (a*b)/c is the engine's
// 0x00469a30 contract (0 when c == 0).
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
    cmp esi, 0x14
    jge sp_limit
    xor esi, esi
    and dword ptr [edi+0x12c], 0xfffffffd
sp_limit:
    mov ecx, dword ptr [edi+0x18]
    test ecx, ecx
    mov eax, dword ptr [edi+0x1d8]
    je sp_limit_test
    mov ecx, dword ptr [ecx+0x1d8]
    cmp ecx, eax
    jle sp_limit_test
    mov eax, ecx
sp_limit_test:
    test eax, eax
    jle sp_min_test
    cmp esi, eax
    jge sp_min_test
    and dword ptr [edi+0x12c], 0xfffffffd
    jmp sp_degenerate
sp_min_test:
    cmp esi, 1
    jge sp_lod
sp_degenerate:
    mov eax, dword ptr [edi+0x12c]
    test eax, 0x4000000
    jne sp_lod
    and eax, 0xfffffffd
    mov dword ptr [edi+0x12c], eax
    jmp sp_exit_site
sp_lod:
    mov ebx, dword ptr [edi+0x1f0]
    mov dword ptr [esp+0x14], ebx
    test ebx, ebx
    je sp_lod_select
    movsx ebp, word ptr [ebx+0x10]
    sub ebp, 1
    mov esi, ebp
    test ebp, ebp
    jle sp_lod_adjust
    mov ecx, dword ptr [esp+0x14]
    mov edx, dword ptr [ecx+0xc]
    lea ebx, [edx+ebp*4]
sp_lod_loop:
    mov eax, dword ptr [ebx]
    mov eax, dword ptr [eax+0x34]
    cmp dword ptr [esp+0x2c], eax
    jl sp_lod_store
    sub esi, 1
    sub ebx, 4
    test esi, esi
    jg sp_lod_loop
    jmp sp_lod_restore
sp_lod_store:
    mov dword ptr [edi+0x14c], esi
sp_lod_restore:
    mov ebx, dword ptr [esp+0x14]
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
sp_lod_select:
    mov ecx, dword ptr [edi+0x14c]
    test ecx, ecx
    jle sp_renderable
    movsx eax, word ptr [ebx+0x10]
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
extern "C" unsigned char synthetic_measure_window[], synthetic_exit_window[];

// ---- harness: a thiscall of the synthetic pass with sentinel registers, PUSHAD/EFLAGS/ESP/x87 captured at the return
// ----
extern "C" {
struct Frame {
    std::uint32_t node, view, flag, entry_esp; // in
    std::uint32_t out[9]; // PUSHAD order edi,esi,ebp,esp,ebx,edx,ecx,eax then EFLAGS, at the return
    std::uint32_t exit_esp;
    std::uint32_t x87env[7]; // fnstenv after the return: status (TOP) and tag word
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
constexpr std::uint32_t model_pointer_offset = 0x1f0, d_offset = 0xf0, first_child_offset = 0xc, view_w = 0x5c,
                        view_flags = 0x270, view_scale = 0x298, view_distance = 0x300;
// Synthetic models in the engine's layout: +0x0c -> record pointers, +0x10 signed word count,
// record +0x34 the switch value. Record 0 carries t0 = 1000 (the pass never reads it; the census does).
struct alignas(16) Record {
    unsigned char bytes[0x40];
};
struct alignas(16) Model {
    unsigned char bytes[0x20];
    std::uint32_t pointers[4];
    Record records[4];
    std::int32_t lods, t1, t2, t3;
};
constexpr std::int32_t t0 = 1000;
static Model models[16];
static unsigned model_count = 0;
static Model* model_for(unsigned lods, std::int32_t t1, std::int32_t t2, std::int32_t t3) {
    for (unsigned i = 0; i < model_count; ++i)
        if (models[i].lods == std::int32_t(lods) && models[i].t1 == t1 && models[i].t2 == t2 && models[i].t3 == t3)
            return &models[i];
    if (model_count == sizeof models / sizeof models[0]) return nullptr;
    Model& m = models[model_count++];
    std::memset(&m, 0, sizeof m);
    m.lods = std::int32_t(lods);
    m.t1 = t1;
    m.t2 = t2;
    m.t3 = t3;
    put(m.bytes, core::model_records_offset, addr(m.pointers));
    const std::uint16_t count = std::uint16_t(lods);
    std::memcpy(m.bytes + core::model_lod_count_offset, &count, 2);
    const std::int32_t thr[4] = {t0, t1, t2, t3};
    for (unsigned i = 0; i < 4; ++i) {
        m.pointers[i] = addr(&m.records[i]);
        put(m.records[i].bytes, core::record_threshold_offset, std::uint32_t(thr[i]));
    }
    return &m;
}
static void node_set(Node& n, Node* parent, std::int32_t radius, std::int32_t d, std::uint32_t flags,
                     std::int32_t thr_1d8, std::int32_t thr_1dc, std::uint32_t model, unsigned lods = 1,
                     std::int32_t t1 = 0, std::int32_t t2 = 0, std::int32_t t3 = 0) {
    std::memset(n.bytes, 0, sizeof n.bytes);
    put(n.bytes, 0, addr(sentinel));
    put(n.bytes, first_child_offset, addr(sentinel));
    put(n.bytes, core::parent_offset, parent ? addr(parent) : 0);
    put(n.bytes, core::radius_offset, std::uint32_t(radius));
    put(n.bytes, d_offset, std::uint32_t(d));
    put(n.bytes, core::flags12c_offset, flags);
    put(n.bytes, core::threshold_1d8_offset, std::uint32_t(thr_1d8));
    put(n.bytes, core::threshold_1dc_offset, std::uint32_t(thr_1dc));
    put(n.bytes, core::model_offset, model);
    put(n.bytes, model_pointer_offset, addr(model_for(lods, t1, t2, t3)));
}
// Links children[0..n) as the child list of parent (each child's +0 = the next, the last -> sentinel).
static void link(Node& parent, Node* const* children, unsigned count) {
    put(parent.bytes, first_child_offset, count ? addr(children[0]) : addr(sentinel));
    for (unsigned i = 0; i < count; ++i) {
        put(children[i]->bytes, 0, i + 1 < count ? addr(children[i + 1]) : addr(sentinel));
        put(children[i]->bytes, core::parent_offset, addr(&parent));
    }
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
    r.flags = f.out[8] & 0x8d5; // CF PF AF ZF SF OF
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

// Expected measures from the synthetic node and view, the engine's arithmetic.
struct Expected {
    std::int32_t s, measure, d, limit;
};
static Expected expected(const Node& n, const View& v, const Node* parent) {
    Expected e{};
    const std::int32_t d0 = std::int32_t(get(n.bytes, d_offset)), scale = std::int32_t(get(v.bytes, view_scale)),
                       w = std::int32_t(get(v.bytes, view_w)), r = std::int32_t(get(n.bytes, core::radius_offset));
    e.d = std::int32_t((std::int64_t(d0) * scale) / 0x4000);
    e.measure = (e.d < w || (get(n.bytes, core::flags12c_offset) & 0x400)) ? 0x7000000 : synthetic_ratio(r, w, e.d);
    e.s = e.d < 0x280 ? 0x7000000 : synthetic_ratio(r, 0x280, e.d);
    if (!e.s) e.s = 1;
    e.limit = core::size_limit(std::int32_t(get(n.bytes, core::threshold_1d8_offset)), parent != nullptr,
                               parent ? std::int32_t(get(parent->bytes, core::threshold_1d8_offset)) : 0);
    return e;
}
struct Row {
    unsigned long device, frame, view, node, model, flags_in, flags_out;
    long s, measure, d, radius, thr_1dc, thr_1d8, limit, lod;
    char verdict[24], lods[16], thr[128];
};
static bool parse_row(const std::string& line, Row& r) {
    const char* ladder = std::strstr(line.c_str(), " lods=");
    return std::sscanf(
               line.c_str(),
               "cull_census device=%lu frame=%lu view=%lx node=%lx model=%lx s=%ld measure=%ld d=%ld radius=%ld thr_1dc=%ld thr_1d8=%ld limit=%ld flags_in=%lx flags_out=%lx lod=%ld verdict=%23s",
               &r.device, &r.frame, &r.view, &r.node, &r.model, &r.s, &r.measure, &r.d, &r.radius, &r.thr_1dc,
               &r.thr_1d8, &r.limit, &r.flags_in, &r.flags_out, &r.lod, r.verdict) == 16 &&
           ladder && std::sscanf(ladder, " lods=%15s thr=%127s", r.lods, r.thr) == 2;
}
// Rows end in " flag31=<v>" (bit 31 of the root's +0x12c): true when every line carries exactly v there.
static bool every_flag31(const std::vector<std::string>& lines, const char* v) {
    if (lines.empty()) return false;
    for (const std::string& line : lines) {
        const char* at = std::strstr(line.c_str(), " flag31=");
        if (!at || std::strcmp(at + 8, v)) return false;
    }
    return true;
}
static bool ladder_is(const Row* r, const char* lods, const char* thr) {
    return r && !std::strcmp(r->lods, lods) && !std::strcmp(r->thr, thr);
}
static const Row* row_of(const std::vector<Row>& rows, const Node& n) {
    for (const Row& r : rows)
        if (r.node == addr(&n)) return &r;
    return nullptr;
}
static bool row_matches(const Row* r, const Node& n, const Node& reference, const Expected& e, const char* verdict) {
    if (!r) return false;
    const bool kept = (get(reference.bytes, core::flags12c_offset) & 2) != 0;
    return r->s == e.s && r->measure == e.measure && r->d == e.d && r->limit == e.limit &&
           r->radius == std::int32_t(get(n.bytes, core::radius_offset)) &&
           r->thr_1d8 == std::int32_t(get(n.bytes, core::threshold_1d8_offset)) &&
           r->thr_1dc == std::int32_t(get(n.bytes, core::threshold_1dc_offset)) &&
           r->model == get(n.bytes, core::model_offset) &&
           r->lod == std::int32_t(get(reference.bytes, core::lod_offset)) &&
           r->flags_out == get(reference.bytes, core::flags12c_offset) && ((r->flags_out & 2) != 0) == kept &&
           !std::strcmp(r->verdict, verdict) && (std::strcmp(verdict, "kept") == 0) == kept;
}

static const unsigned char* site_a() {
    return synthetic_measure_window + core::measure_site_offset;
}
static const unsigned char* site_b() {
    return synthetic_exit_window + core::exit_site_offset;
}
static bool sites_original() {
    return !std::memcmp(site_a(), core::measure_site, core::site_length) &&
           !std::memcmp(site_b(), core::exit_site, core::site_length);
}
static bool windows_original() {
    return !std::memcmp(synthetic_measure_window, core::measure_window, core::measure_window_length) &&
           !std::memcmp(synthetic_exit_window, core::exit_window, core::exit_window_length);
}
// The tree of the main scenario, restored to its initial bytes before every pass.
static Node R, A, B, C, E, F, G, H, I, J, gA1, gA2;
static Node* const all[] = {&R, &A, &B, &C, &E, &F, &G, &H, &I, &J, &gA1, &gA2};
constexpr unsigned all_count = sizeof all / sizeof all[0];
static Node initial[all_count];
static void reset_tree() {
    for (unsigned i = 0; i < all_count; ++i) *all[i] = initial[i];
}
// One pass per iteration over the restored tree, armed (begin_frame(true) each
// time, so every node records) or not. Harness and restore included; diagnostic.
static double bench_us(Node& root, View& view, bool arm, unsigned loops = 20000) {
    LARGE_INTEGER f{}, s{}, e{};
    QueryPerformanceFrequency(&f);
    auto once = [&] {
        reset_tree();
        if (arm) census::begin_frame(true);
        run(root, view);
    };
    for (unsigned i = 0; i < 256; ++i) once();
    QueryPerformanceCounter(&s);
    for (unsigned i = 0; i < loops; ++i) once();
    QueryPerformanceCounter(&e);
    return double(e.QuadPart - s.QuadPart) * 1e6 / double(f.QuadPart) / double(loops);
}

static std::uint32_t no_body_manager = 0; // the body-table global the main scenario sees: body system not up, every row
                                          // body=-
int main() {
    census::set_body_table_global(addr(&no_body_manager));
    // The synthetic code lives in .text: make the two windows writable for the patch (the engine's pages are handled by
    // engine_patch).
    DWORD old = 0;
    check(VirtualProtect(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(synthetic_measure_window) &
                                                 ~std::uintptr_t(0xfff)),
                         0x2000, PAGE_EXECUTE_READWRITE, &old) != FALSE,
          "synthetic code writable");
    check(windows_original(), "synthetic pass carries the two engine windows byte-exact");
    if (!windows_original()) {
        std::printf("CULL CENSUS CPU checks=%u failures=%u\n", checks, failures);
        return 1;
    }

    // ---- the tree: R with children A..J, A with two grandchildren ----
    View view;
    view_set(view, 1280, 0, 0x4000, 2);
    node_set(R, nullptr, 20000, 100000, 0x1002, 0, 0, 0x5000, 4, 100, 50, 25); // measure 256, s 128, lod 0, kept
    node_set(A, &R, 100, 100000, 0x1002, 4, 0, 0x5001); // measure 1, s 1 (clamped), limit 4 -> culled_size
    node_set(B, &R, 50, 100000, 0x1002, 0, 0, 0x5002);  // measure 0, s 1, limit 0 -> culled_min
    node_set(C, &R, 3000, 100000, 0x9002, 0, 40, 0x5003, 3, 100, 50);    // measure 38 < 1dc 40 (flag), s 19 -> lod 2 ==
                                                                         // last with 0x8000 -> culled_other
    node_set(E, &R, 3000, 100000, 0x1002, 0, 40, 0x5004, 3, 100, 50);    // as C without the fade flag -> kept, lod 2
    node_set(F, &R, 3000, 100000, 0x1002, 0, 0, 0x5005, 4, 100, 50, 25); // s 19 < 25 -> lod 3 -> kept, +0x130 |=
                                                                         // 0x100000
    node_set(G, &R, 3000, 100000, 0x1000, 0, 0, 0x5006);     // renderable bit clear at entry -> early exit, unmeasured
    node_set(H, &R, 3000, 100000, 0x101002, 0, 0, 0x5007);   // hide latch -> bit cleared, early exit, unmeasured
    node_set(I, &R, 300, 500, 0x1002, 0, 0, 0x5008, 2, 100); // D < 640: s = measure = 0x7000000 -> kept, lod 0
    node_set(J, &R, 800, 100000, 0x1002, 0, 0, 0x500b); // measure 10, s 5 -> kept; zeroed (< 20) in the env-map view ->
                                                        // culled_other there
    node_set(gA1, &A, 5000, 100000, 0x1002, 2, 0, 0x5009); // measure 64, own 2 < parent 4 -> limit 4 -> kept
    node_set(gA2, &A, 470, 100000, 0x1002, 8, 0, 0x500a);  // measure 6, own 8 > parent 4 -> limit 8 -> culled_size
    put(gA1.bytes, model_pointer_offset, 0);               // no model (0x0047d2f6 / 0x0047d30e): EBX 0, no ladder
    put(J.bytes, model_pointer_offset, addr(model_for(0, 0, 0, 0))); // LOD count 0: ebp -1, loop skipped, row "lods=0
                                                                     // thr=-"
    // I's model: two records, record 0 on a no-access page. The pass reads only record 1
    // (s saturated, not below 100); the census's bounded read of record 0 fails -> "lods=2 thr=-".
    auto* noaccess = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS));
    check(noaccess != nullptr, "no-access page");
    static Model unreadable;
    unreadable = *model_for(2, 100, 0, 0);
    unreadable.lods = -1;
    put(unreadable.bytes, core::model_records_offset, addr(unreadable.pointers));
    unreadable.pointers[0] = addr(noaccess);
    unreadable.pointers[1] = addr(&unreadable.records[1]);
    put(I.bytes, model_pointer_offset, addr(&unreadable));
    Node* r_children[] = {&A, &B, &C, &E, &F, &G, &H, &I, &J};
    link(R, r_children, 9);
    Node* a_children[] = {&gA1, &gA2};
    link(A, a_children, 2);
    static Node reference[all_count];
    for (unsigned i = 0; i < all_count; ++i) initial[i] = *all[i];

    // ---- native reference ----
    const Result native = run(R, view);
    check(native.preserved && native.x87_empty, "native: callee-saved registers, ESP and empty x87 stack");
    for (unsigned i = 0; i < all_count; ++i) reference[i] = *all[i];
    const unsigned iR = 0, iA = 1, iB = 2, iC = 3, iE = 4, iF = 5, iG = 6, iH = 7, iI = 8, iJ = 9, igA1 = 10, igA2 = 11;
    check((get(reference[iR].bytes, 0x12c) & 2) && get(reference[iR].bytes, 0x14c) == 0, "native: R kept at LOD 0");
    check(!(get(reference[iA].bytes, 0x12c) & 2) && !(get(reference[iB].bytes, 0x12c) & 2) &&
              !(get(reference[iC].bytes, 0x12c) & 2) && !(get(reference[igA2].bytes, 0x12c) & 2),
          "native: A, B, C, gA2 culled");
    check((get(reference[iE].bytes, 0x12c) & 2) && get(reference[iE].bytes, 0x14c) == 2 &&
              (get(reference[iF].bytes, 0x12c) & 2) && get(reference[iF].bytes, 0x14c) == 3 &&
              (get(reference[iF].bytes, 0x130) & 0x100000),
          "native: E LOD 2, F LOD 3 with the far flag");
    check((get(reference[iC].bytes, 0x130) & 0x100000) && get(reference[iC].bytes, 0x14c) == 2,
          "native: C detail flag and LOD 2 before the fade");
    check(!(get(reference[iG].bytes, 0x12c) & 2) && !(get(reference[iH].bytes, 0x12c) & 2) &&
              (get(reference[iI].bytes, 0x12c) & 2) && (get(reference[iJ].bytes, 0x12c) & 2) &&
              (get(reference[igA1].bytes, 0x12c) & 2),
          "native: G, H early exits, I, J and gA1 kept");

    // ---- option off / engine site absent / changed bytes ----
    SetEnvironmentVariableW(L"X3M_CULL_CENSUS", nullptr);
    check(!census::initialize() && !std::strcmp(census::state(), "disabled") && sites_original(),
          "unset variable: disabled, sites untouched");
    SetEnvironmentVariableW(L"X3M_CULL_CENSUS", L"0");
    check(!census::initialize() && !std::strcmp(census::state(), "disabled"), "X3M_CULL_CENSUS=0: disabled");
    SetEnvironmentVariableW(L"X3M_CULL_CENSUS", L"1");
    check(!census::initialize() && !std::strcmp(census::state(), "bytes_mismatch") &&
              census::measure_stub_address() == 0,
          "engine sites absent in this process: bytes_mismatch, nothing patched");
    synthetic_measure_window[3] ^= 1;
    check(!census::install_at(addr(site_a()), addr(site_b())) && !std::strcmp(census::state(), "bytes_mismatch") &&
              sites_original(),
          "changed measure-window byte: bytes_mismatch");
    synthetic_measure_window[3] ^= 1;
    synthetic_exit_window[1] ^= 1;
    check(!census::install_at(addr(site_a()), addr(site_b())) && !std::strcmp(census::state(), "bytes_mismatch") &&
              sites_original(),
          "changed exit-window byte: bytes_mismatch, measure site untouched");
    synthetic_exit_window[1] ^= 1;
    check(!census::install_at(0, addr(site_b())) && !std::strcmp(census::state(), "invalid_site"), "null site refused");

    // ---- install ----
    check(census::install_at(addr(site_a()), addr(site_b())) && !std::strcmp(census::state(), "ok"),
          "install_at synthetic sites");
    if (!census::measure_stub_address()) {
        std::printf("FAIL install state=%s\nCULL CENSUS CPU checks=%u failures=%u\n", census::state(), checks,
                    failures + 1);
        return 1;
    }
    check(site_a()[0] == 0xe9 && site_a()[5] == core::measure_site[5] && site_b()[0] == 0xe9 &&
              site_b()[5] == core::exit_site[5],
          "both sites are jmp dispatcher with the sixth byte untouched");
    check(!std::memcmp(synthetic_measure_window, core::measure_window, core::measure_site_offset) &&
              !std::memcmp(synthetic_exit_window, core::exit_window, core::exit_site_offset),
          "window bytes before the sites untouched");
    {
        const std::uint32_t at = std::uint32_t(census::measure_stub_address()),
                            slot = (at + core::measure_stub_length + 3) & ~3u;
        unsigned char want[core::measure_stub_length];
        core::encode_measure_stub(at, addr(const_cast<unsigned char*>(&x3m_cull_census_enabled)),
                                  addr(reinterpret_cast<const void*>(&x3m_cull_census_measure)), slot, want);
        check(!std::memcmp(reinterpret_cast<const void*>(at), want, core::measure_stub_length),
              "measure stub bytes as encoded");
        const std::uint32_t bt = std::uint32_t(census::exit_stub_address()),
                            bslot = (bt + core::exit_stub_length + 3) & ~3u;
        unsigned char bwant[core::exit_stub_length];
        core::encode_exit_stub(bt, addr(const_cast<unsigned char*>(&x3m_cull_census_enabled)),
                               addr(reinterpret_cast<const void*>(&x3m_cull_census_exit)), bslot, bwant);
        check(!std::memcmp(reinterpret_cast<const void*>(bt), bwant, core::exit_stub_length),
              "exit stub bytes as encoded");
        check(*reinterpret_cast<void**>(slot) != nullptr && *reinterpret_cast<void**>(bslot) != nullptr,
              "continuation slots point at the tails");
    }
    check(!census::install_at(addr(site_a()), addr(site_b())) && !std::strcmp(census::state(), "already_installed"),
          "second install refused");
    check(x3m_cull_census_enabled == 0 && !census::stats().armed, "installed disarmed");

    // ---- patched, disarmed: identical, nothing recorded ----
    reset_tree();
    census::begin_frame(false);
    Result patched = run(R, view);
    check(patched.preserved && patched.x87_empty && same_outputs(patched, native),
          "patched disarmed: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
    bool identical = true;
    for (unsigned i = 0; i < all_count; ++i)
        identical = identical && !std::memcmp(all[i]->bytes, reference[i].bytes, sizeof(Node));
    check(identical, "patched disarmed: every node as native");
    check(census::stats().entries == 0 && census::stats().unmeasured == 0,
          "patched disarmed: nothing recorded (dead branch)");
    const std::uint64_t epoch_plain = x3m::engine_memory::stats().frame;
    census::present(7, 40, false);
    check(x3m::engine_memory::stats().frame == epoch_plain,
          "present of a plain frame leaves the engine-memory epoch alone");
    check(frame_lines.empty() && entry_lines.empty(), "present of a plain frame logs nothing");

    // ---- patched, armed: identical nodes and outputs, the census rows ----
    reset_tree();
    census::begin_frame(true);
    check(census::stats().armed, "armed for a capture frame");
    SetLastError(0x5150);
    patched = run(R, view);
    check(GetLastError() == 0x5150, "LastError preserved across the armed pass");
    check(patched.preserved && patched.x87_empty && same_outputs(patched, native),
          "patched armed: registers, ESP, x87 and EAX/ECX/EDX/EFLAGS as native");
    identical = true;
    for (unsigned i = 0; i < all_count; ++i)
        identical = identical && !std::memcmp(all[i]->bytes, reference[i].bytes, sizeof(Node));
    check(identical, "patched armed: every node as native");
    census::Stats s = census::stats();
    check(s.entries == 10 && s.exited == 10 && s.unmeasured == 2 && s.overflow == 0,
          "armed: 10 measured and exited, 2 early exits unmeasured, no overflow");
    census::present(7, 41, true);
    check(frame_lines.size() == 1 && entry_lines.size() == 10, "capture frame: one frame row and ten entry rows");
    check(!census::stats().armed && census::stats().entries == 0, "present disarms and clears");
    {
        unsigned long device = 0, frame = 0, entries = 0, overflow = 0, unmeasured = 0, exited = 0;
        unsigned ring = 0;
        const int fields =
            frame_lines.empty()
                ? 0
                : std::sscanf(
                      frame_lines[0].c_str(),
                      "cull_census_frame device=%lu frame=%lu entries=%lu overflow=%lu unmeasured=%lu exited=%lu ring=%u",
                      &device, &frame, &entries, &overflow, &unmeasured, &exited, &ring);
        check(fields == 7 && device == 7 && frame == 41 && entries == 10 && overflow == 0 && unmeasured == 2 &&
                  exited == 10 && ring == core::ring_size,
              "frame row fields");
        std::vector<Row> rows;
        for (const std::string& line : entry_lines) {
            Row r{};
            if (parse_row(line, r)) rows.push_back(r);
        }
        check(rows.size() == 10, "every entry row parses");
        const unsigned long order[10] = {addr(&R), addr(&A), addr(&gA1), addr(&gA2), addr(&B),
                                         addr(&C), addr(&E), addr(&F),   addr(&I),   addr(&J)};
        bool ordered = rows.size() == 10;
        for (unsigned i = 0; ordered && i < 10; ++i) ordered = rows[i].node == order[i];
        check(ordered, "rows in traversal order (parent before children, siblings in list order)");
        check(rows.size() == 10 && rows[0].view == addr(&view) && rows[0].device == 7 && rows[0].frame == 41,
              "row carries the view, device and frame");
        check(row_matches(row_of(rows, R), R, reference[iR], expected(R, view, nullptr), "kept"),
              "R row: s 128, measure 256, kept, LOD 0");
        check(row_matches(row_of(rows, A), A, reference[iA], expected(A, view, &R), "culled_size"),
              "A row: measure 1 below limit 4, culled_size");
        check(row_matches(row_of(rows, B), B, reference[iB], expected(B, view, &R), "culled_min"),
              "B row: measure 0, culled_min");
        check(row_matches(row_of(rows, C), C, reference[iC], expected(C, view, &R), "culled_other"),
              "C row: last-LOD fade, culled_other, LOD 2");
        check(row_matches(row_of(rows, E), E, reference[iE], expected(E, view, &R), "kept"), "E row: kept, LOD 2");
        check(row_matches(row_of(rows, F), F, reference[iF], expected(F, view, &R), "kept"), "F row: kept, LOD 3");
        check(row_matches(row_of(rows, I), I, reference[iI], expected(I, view, &R), "kept"),
              "I row: D below 640 saturates s and measure");
        check(row_matches(row_of(rows, J), J, reference[iJ], expected(J, view, &R), "kept"),
              "J row: measure 10, kept, LOD 0");
        check(row_matches(row_of(rows, gA1), gA1, reference[igA1], expected(gA1, view, &A), "kept"),
              "gA1 row: parent threshold 4 is the limit, kept");
        check(row_matches(row_of(rows, gA2), gA2, reference[igA2], expected(gA2, view, &A), "culled_size"),
              "gA2 row: own threshold 8 is the limit, culled_size");
        const Row* ri = row_of(rows, I);
        check(ri && ri->s == 0x7000000 && ri->measure == 0x7000000, "I row saturated values");
        const Row* ra = row_of(rows, A);
        check(ra && ra->s == 1 && ra->measure == 1 && ra->limit == 4 && ra->thr_1d8 == 4, "A row values");
        const Row* rf = row_of(rows, F);
        check(rf && rf->s == 19 && rf->measure == 38 && rf->lod == 3 && rf->d == 100000 && rf->radius == 3000 &&
                  rf->model == 0x5005,
              "F row values");
        check(row_of(rows, G) == nullptr && row_of(rows, H) == nullptr, "early exits have no row");
        // The LOD ladder: record 0's value (never read by the pass) first, then the switch values the loop compares s
        // with.
        check(ladder_is(row_of(rows, C), "3", "1000,100,50") && ladder_is(row_of(rows, E), "3", "1000,100,50"),
              "C and E rows: the three-record ladder (C culled by the fade on the LOD path)");
        check(ladder_is(row_of(rows, R), "4", "1000,100,50,25") && ladder_is(row_of(rows, F), "4", "1000,100,50,25"),
              "R and F rows: the four-record ladder");
        check(ladder_is(row_of(rows, A), "-", "-") && ladder_is(row_of(rows, B), "-", "-") &&
                  ladder_is(row_of(rows, gA2), "-", "-"),
              "nodes culled at 0x0047d2e7 (EBX still D): no ladder");
        check(ladder_is(row_of(rows, gA1), "-", "-") && row_of(rows, gA1) &&
                  !std::strcmp(row_of(rows, gA1)->verdict, "kept"),
              "null model (EBX 0): no ladder, kept");
        check(ladder_is(row_of(rows, J), "0", "-"), "count-0 model: count, no thresholds");
        check(ladder_is(row_of(rows, I), "2", "-"), "unreadable record 0: count, no thresholds, no fault");
        check(every_flag31(entry_lines, "0"),
              "flag31: every row, grandchildren gA1/gA2 included, resolves to the root's clear bit 31 (no '-')");
    }
    frame_lines.clear();
    entry_lines.clear();

    // ---- root bit 31 set (Terran station root): every row of the subtree carries flag31=1 ----
    {
        reset_tree();
        put(R.bytes, core::flags12c_offset, get(R.bytes, core::flags12c_offset) | 0x80000000u);
        census::begin_frame(true);
        const Result pat = run(R, view);
        census::present(7, 43, true);
        check(pat.preserved && pat.x87_empty && entry_lines.size() == 10 && every_flag31(entry_lines, "1"),
              "flag31: root bit 31 set -> all ten rows flag31=1 through two levels, registers and x87 preserved");
        check(!(get(gA1.bytes, core::flags12c_offset) & 0x80000000u),
              "flag31 comes from the root, not the child's own word");
        frame_lines.clear();
        entry_lines.clear();
        reset_tree();
    }

    // ---- env-map view: the < 20 zeroing path, culled_other ----
    {
        View env;
        view_set(env, 1280, 0x1000000, 0x4000, 2);
        reset_tree();
        const Result nat = run(R, env);
        static Node env_reference[all_count];
        for (unsigned i = 0; i < all_count; ++i) env_reference[i] = *all[i];
        reset_tree();
        census::begin_frame(true);
        const Result pat = run(R, env);
        bool same = pat.preserved && pat.x87_empty && same_outputs(pat, nat);
        for (unsigned i = 0; i < all_count; ++i)
            same = same && !std::memcmp(all[i]->bytes, env_reference[i].bytes, sizeof(Node));
        check(same, "env-map view: patched pass identical to native");
        census::present(7, 42, true);
        std::vector<Row> rows;
        for (const std::string& line : entry_lines) {
            Row r{};
            if (parse_row(line, r)) rows.push_back(r);
        }
        const Row* rj = row_of(rows, J);
        const Row* rr = row_of(rows, R);
        check(rj && !std::strcmp(rj->verdict, "culled_other") && !(rj->flags_out & 2) && rj->measure == 10 &&
                  rj->limit == 0,
              "env-map view: J culled_other (measure 10 zeroed below 20, not the size limit)");
        check(rr && !std::strcmp(rr->verdict, "kept") && rr->lod == 1, "env-map view: R kept with the +1 LOD step");
        check(ladder_is(rr, "4", "1000,100,50,25") && ladder_is(rj, "-", "-"),
              "env-map view: R keeps its ladder; J, zeroed and culled with EBX still D, has none");
        frame_lines.clear();
        entry_lines.clear();
    }

    // ---- view-distance 4 forces LOD 0; 3 steps one finer ----
    {
        View wide;
        view_set(wide, 1280, 0, 0x4000, 4);
        reset_tree();
        run(R, wide);
        static Node far_reference[all_count];
        for (unsigned i = 0; i < all_count; ++i) far_reference[i] = *all[i];
        reset_tree();
        census::begin_frame(true);
        run(R, wide);
        bool same = true;
        for (unsigned i = 0; i < all_count; ++i)
            same = same && !std::memcmp(all[i]->bytes, far_reference[i].bytes, sizeof(Node));
        check(same, "view distance 4: patched pass identical to native");
        census::present(7, 43, true);
        std::vector<Row> rows;
        for (const std::string& line : entry_lines) {
            Row r{};
            if (parse_row(line, r)) rows.push_back(r);
        }
        const Row* rf = row_of(rows, F);
        const Row* rc = row_of(rows, C);
        check(rf && rf->lod == 0 && !std::strcmp(rf->verdict, "kept") && rc && rc->lod == 0 &&
                  !std::strcmp(rc->verdict, "kept"),
              "view distance 4: every LOD forced to 0, C no longer fades");
        frame_lines.clear();
        entry_lines.clear();
    }

    // ---- hostile exit arguments: a register that passes the guard but points at no-access memory, and EBX == D ----
    {
        static Node lone;
        node_set(lone, nullptr, 3000, 100000, 0x1002, 0, 0, 0x5100, 3, 100, 50);
        census::begin_frame(true);
        SetLastError(0x6160);
        x3m_cull_census_measure(addr(&lone), 38, 19, 100000, addr(&view));
        x3m_cull_census_exit(addr(&lone), addr(noaccess), addr(noaccess), 100000);
        x3m_cull_census_measure(addr(&lone), 38, 19, 100000, addr(&view));
        x3m_cull_census_exit(addr(&lone), 100000, 100000, 100000);
        x3m_cull_census_measure(addr(&lone), 38, 19, 100000, addr(&view));
        x3m_cull_census_exit(addr(&lone), 0x00000010u, 0x00000010u, 100000); // the null page
        const std::uint64_t epoch = x3m::engine_memory::stats().frame;
        census::present(7, 48, true);
        check(x3m::engine_memory::stats().frame == epoch + 1,
              "captured present advances the engine-memory epoch once before the ladder reads");
        check(GetLastError() == 0x6160, "hostile exit arguments: LastError preserved across the Present-time reads");
        std::vector<Row> rows;
        for (const std::string& line : entry_lines) {
            Row r{};
            if (parse_row(line, r)) rows.push_back(r);
        }
        check(
            rows.size() == 3 && ladder_is(&rows[0], "-", "-") && ladder_is(&rows[1], "-", "-") &&
                ladder_is(&rows[2], "-", "-"),
            "hostile exit arguments: no-access model, EBX equal to D and a null-page model all give lods=- thr=-, no fault");
        frame_lines.clear();
        entry_lines.clear();
    }

    // ---- body names: a synthetic body manager (0x0046df60's id -> slot map, the name char* at slot +0x0c) ----
    {
        constexpr unsigned dynamic = 4, slots = core::body_fixed_count + dynamic; // valid slots 0..11003
        static unsigned char manager[0xc0];
        auto* table = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, slots * core::body_slot_stride, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        auto* edge = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)); // a name ending at a page end
        DWORD was = 0;
        check(table && edge && VirtualProtect(edge + 0x1000, 0x1000, PAGE_NOACCESS, &was) != FALSE,
              "body table: synthetic slots and an edge page");
        if (!table || !edge) {
            std::printf("CULL CENSUS CPU checks=%u failures=%u\n", checks, failures);
            return 1;
        }
        auto name_at = [&](unsigned slot, const void* name) {
            put(table + slot * core::body_slot_stride, core::body_slot_name_offset, addr(name));
        };
        static char m5[] = "ships\\argon\\argon_m5", dock[] = "stations\\docks\\argon_dock_center";
        static char longest[101];
        std::memset(longest, 'y', 100);
        std::memcpy(edge + 0x1000 - 4, "abc", 4);
        name_at(42, m5);                                        // id 42 (< 1000): slot 42
        name_at(core::body_fixed_count + 1, dock);              // id 20001: slot 11001
        name_at(core::body_fixed_count + 2, noaccess);          // id 20002: the name pointer on a no-access page
        name_at(core::body_fixed_count + 3, edge + 0x1000 - 4); // id 20003: "abc" ending just before a no-access page
        name_at(44, longest);                                   // id 44: 100 characters, printed 63
        // id 45: 256 bytes without a NUL (the NUL at byte 256 is past the scan): refused
        static char unterminated[257];
        std::memset(unterminated, 'z', 256);
        name_at(45, unterminated);
        // id 46: a name continuing from one readable page into the next (two page-bounded chunks)
        auto* span = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        check(span != nullptr, "body table: a two-page readable span");
        if (!span) {
            std::printf("CULL CENSUS CPU checks=%u failures=%u\n", checks, failures);
            return 1;
        }
        std::memcpy(span + 0x1000 - 5, "abcdefghij", 11);
        name_at(46, span + 0x1000 - 5);
        // id 10500 (9000..19999): slot 1500
        static char fixed[] = "ships\\teladi\\teladi_m6";
        name_at(10500 - 9000, fixed);
        // id 43: slot 43 has no name (the engine's "v\%05d"); id 20005: slot 11005 beyond the 11004 slots; id 5000:
        // 1000..8999 has no slot
        put(manager, core::body_fixed_count_offset, core::body_fixed_count);
        put(manager, core::body_dynamic_count_offset, dynamic);
        put(manager, core::body_slots_offset, addr(table));
        static std::uint32_t manager_global = 0;
        manager_global = addr(manager);
        census::set_body_table_global(addr(&manager_global));
        static Node named[12];
        auto frame_of = [&](const std::uint32_t* ids, unsigned count, unsigned long long frame) {
            census::begin_frame(true);
            for (unsigned i = 0; i < count; ++i) {
                node_set(named[i], nullptr, 3000, 100000, 0x1002, 0, 0, ids[i]);
                x3m_cull_census_measure(addr(&named[i]), 38, 19, 100000, addr(&view));
                x3m_cull_census_exit(addr(&named[i]), 100000, 100000, 100000); // EBX == D: no ladder read, only the
                                                                               // body read
            }
            census::present(7, frame, true);
            std::vector<std::string> bodies;
            for (const std::string& line : entry_lines) {
                const char* b = std::strstr(line.c_str(), " body=");
                char text[128] = "?";
                if (b) std::sscanf(b, " body=%127s", text);
                bodies.emplace_back(text);
            }
            frame_lines.clear();
            entry_lines.clear();
            return bodies;
        };
        static const std::uint32_t ids[12] = {42, 20001, 20005, 20002, 43, 5000, 20003, 44, 42, 45, 46, 10500};
        SetLastError(0x6170);
        const std::vector<std::string> bodies = frame_of(ids, 12, 49);
        check(GetLastError() == 0x6170, "body names: LastError preserved across the Present-time reads");
        check(bodies.size() == 12, "body names: twelve rows");
        if (bodies.size() == 12) {
            check(bodies[0] == "ships\\argon\\argon_m5" && bodies[8] == bodies[0],
                  "body names: id 42 (fixed, below 1000) named, and again on a second row");
            check(bodies[1] == "stations\\docks\\argon_dock_center", "body names: id 20001 (dynamic) named");
            check(bodies[2] == "-", "body names: id 20005, slot out of range: body=-");
            check(bodies[3] == "-", "body names: id 20002, name pointer on a no-access page: body=-, no fault");
            check(bodies[4] == "v\\00043", "body names: id 43, null name pointer: the engine's v\\%05d");
            check(bodies[5] == "-", "body names: id 5000 (1000..8999, no slot): body=-");
            check(bodies[6] == "abc", "body names: a name ending just before a no-access page reads");
            check(bodies[7] == std::string(63, 'y'), "body names: a 100-character name capped at 63");
            check(bodies[9] == "-", "body names: id 45, no NUL within 256 bytes: body=-");
            check(bodies[10] == "abcdefghij", "body names: id 46, a name continuing into a second readable page");
            check(bodies[11] == "ships\\teladi\\teladi_m6",
                  "body names: id 10500 (9000..19999) maps to slot id - 9000");
        }
        // One read set per distinct id per captured frame: three rows of id 42 cost the same reads as one.
        static const std::uint32_t same[3] = {42, 42, 42};
        std::uint64_t before = x3m::engine_memory::stats().reads;
        frame_of(same, 1, 50);
        const std::uint64_t one = x3m::engine_memory::stats().reads - before;
        before = x3m::engine_memory::stats().reads;
        frame_of(same, 3, 51);
        const std::uint64_t three = x3m::engine_memory::stats().reads - before;
        check(one > 0 && three == one, "body names: one read set per distinct id per captured frame");
        // The slot array moves between frames (0x0046e400 reallocates it): the next captured frame reads the new one.
        auto* moved = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, slots * core::body_slot_stride, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        static char renamed[] = "ships\\boron\\boron_m5";
        if (moved) {
            std::memcpy(moved, table, slots * core::body_slot_stride);
            put(moved + 42 * core::body_slot_stride, core::body_slot_name_offset, addr(renamed));
            put(manager, core::body_slots_offset, addr(moved));
        }
        const std::vector<std::string> after = frame_of(same, 1, 52);
        check(moved && after.size() == 1 && after[0] == "ships\\boron\\boron_m5",
              "body names: a reallocated slot array is re-read on the next captured frame");
        // The body system not up (manager pointer 0), then a fixed count other than 11000: every row body=-.
        manager_global = 0;
        const std::vector<std::string> down = frame_of(ids, 2, 53);
        manager_global = addr(manager);
        put(manager, core::body_fixed_count_offset, core::body_fixed_count + 1);
        const std::vector<std::string> wrong = frame_of(ids, 2, 54);
        check(down.size() == 2 && down[0] == "-" && down[1] == "-" && wrong.size() == 2 && wrong[0] == "-" &&
                  wrong[1] == "-",
              "body names: no manager or a fixed count other than 11000 gives body=-");
        census::set_body_table_global(addr(&no_body_manager));
    }

    // ---- overflow: 8,200 measured nodes keep 8,192 rows and count the rest ----
    {
        constexpr unsigned many = core::ring_size + 8;
        auto* pool = static_cast<Node*>(
            VirtualAlloc(nullptr, sizeof(Node) * (many + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        check(pool != nullptr, "overflow pool");
        Node& root = pool[0];
        node_set(root, nullptr, 20000, 100000, 0x1002, 0, 0, 0x6000);
        std::vector<Node*> kids;
        kids.reserve(many);
        for (unsigned i = 1; i <= many; ++i) {
            node_set(pool[i], &root, 1000, 100000, 0x1002, 0, 0, 0x6000 + i);
            kids.push_back(&pool[i]);
        }
        link(root, kids.data(), many);
        census::begin_frame(true);
        const Result pat = run(root, view);
        check(pat.preserved && pat.x87_empty, "overflow: registers preserved through 8,201 nodes");
        s = census::stats();
        check(s.entries == core::ring_size && s.overflow == many + 1 - core::ring_size && s.unmeasured == 0 &&
                  s.exited == core::ring_size,
              "overflow: ring full, 9 overflow, every kept row exited");
        bool all_kept = true;
        for (unsigned i = 0; i <= many; ++i) all_kept = all_kept && (get(pool[i].bytes, 0x12c) & 2);
        check(all_kept, "overflow: the pass itself unaffected (every node kept)");
        census::present(7, 44, true);
        unsigned long device = 0, frame = 0, entries = 0, overflow = 0, unmeasured = 0, exited = 0;
        unsigned ring = 0;
        const int fields =
            frame_lines.empty()
                ? 0
                : std::sscanf(
                      frame_lines[0].c_str(),
                      "cull_census_frame device=%lu frame=%lu entries=%lu overflow=%lu unmeasured=%lu exited=%lu ring=%u",
                      &device, &frame, &entries, &overflow, &unmeasured, &exited, &ring);
        check(fields == 7 && entries == core::ring_size && overflow == 9 && entry_lines.size() == core::ring_size,
              "overflow: frame row overflow=9, 8192 entry rows");
        Row last{};
        check(!entry_lines.empty() && parse_row(entry_lines.back(), last) &&
                  last.node == addr(&pool[core::ring_size - 1]) && !std::strcmp(last.verdict, "kept"),
              "overflow: the last row is the 8,192nd node, exited");
        frame_lines.clear();
        entry_lines.clear();
        // a following frame starts clean
        census::begin_frame(true);
        reset_tree();
        run(R, view);
        check(census::stats().entries == 10 && census::stats().overflow == 0, "frame after the overflow starts clean");
        census::present(7, 45, true);
        frame_lines.clear();
        entry_lines.clear();
    }

    // ---- LOD-switch log: one row per changed record, the per-frame cap, Reset re-seeds ----
    {
        frame_lines.clear();
        entry_lines.clear();
        switch_lines.clear();
        check(!census::set_lod_switch_log(core::lod_switch_max_cap + 1) && census::set_lod_switch_log(16),
              "lod switch: cap above the maximum refused, 16 accepted");
        // Frame 1 seeds, frame 2 is identical: no rows, and plain frames log no census rows.
        census::begin_frame(false);
        check(census::stats().armed, "lod switch: a plain frame is armed while the log is on");
        reset_tree();
        run(R, view);
        census::present(7, 60, false);
        census::begin_frame(false);
        reset_tree();
        run(R, view);
        census::present(7, 61, false);
        check(switch_lines.empty() && frame_lines.empty() && entry_lines.empty(),
              "lod switch: seed and unchanged frame log nothing, no census rows on plain frames");
        // Frame 3: F's D halves (s 19 -> 38): record 3 -> 2, exactly one row with from/to/s/D and T_pad, then the frame
        // row.
        census::begin_frame(false);
        reset_tree();
        put(F.bytes, d_offset, 50000);
        const Result pat = run(R, view);
        census::present(7, 62, false);
        check(pat.preserved && pat.x87_empty && get(F.bytes, core::lod_offset) == 2,
              "lod switch: F now selects record 2, registers and x87 preserved");
        unsigned long long frame = 0;
        unsigned long node = 0, v = 0;
        long from = -1, to = -1, sv = -1, dv = -1;
        char body[80] = {}, pad[16] = {}, f31[4] = {};
        const int fields =
            switch_lines.empty()
                ? 0
                : std::sscanf(
                      switch_lines[0].c_str(),
                      "lod_switch frame=%llu node=%lx body=%79s from=%ld to=%ld s=%ld D=%ld T_pad=%15s flag31=%3s view=%lx",
                      &frame, &node, body, &from, &to, &sv, &dv, pad, f31, &v);
        check(switch_lines.size() == 2 && fields == 10,
              "lod switch: one change -> exactly one lod_switch row and one frame row");
        check(
            frame == 62 && node == addr(&F) && from == 3 && to == 2 && sv == 38 && dv == 50000 &&
                !std::strcmp(pad, "25") && !std::strcmp(f31, "0") && v == addr(&view) && !std::strcmp(body, "-"),
            "lod switch: row carries frame, node, from 3, to 2, s 38, D, T_pad 25 (last record), flag31 and the view");
        unsigned long long ff = 0;
        unsigned sw = 0, nodes = 0;
        check(switch_lines.size() == 2 &&
                  std::sscanf(switch_lines[1].c_str(), "lod_switch_frame frame=%llu switches=%u nodes=%u", &ff, &sw,
                              &nodes) == 3 &&
                  ff == 62 && sw == 1 && nodes == 6,
              "lod switch: frame row switches=1 nodes=6 (the kept nodes R, E, F, I, J, gA1)");
        switch_lines.clear();
        // Frame 4: F back (3), E moves (2 -> 1) with cap 1: one row, one overflow row dropped=1, frame row switches=2.
        check(census::set_lod_switch_log(1), "lod switch: cap 1");
        census::begin_frame(false);
        reset_tree();
        run(R, view);
        census::present(7, 63, false); // re-seed after set_lod_switch_log's clear
        census::begin_frame(false);
        reset_tree();
        put(F.bytes, d_offset, 50000);
        run(R, view);
        census::present(7, 64, false);
        switch_lines.clear();
        census::begin_frame(false);
        reset_tree();
        put(E.bytes, d_offset, 30000);
        run(R, view);
        census::present(7, 65, false);
        unsigned long dropped = 0;
        unsigned cap = 0;
        check(switch_lines.size() == 3 && !std::strncmp(switch_lines[0].c_str(), "lod_switch frame=65 ", 20) &&
                  std::sscanf(switch_lines[1].c_str(), "lod_switch_overflow frame=%llu dropped=%lu cap=%u", &ff,
                              &dropped, &cap) == 3 &&
                  ff == 65 && dropped == 1 && cap == 1 &&
                  std::sscanf(switch_lines[2].c_str(), "lod_switch_frame frame=%llu switches=%u nodes=%u", &ff, &sw,
                              &nodes) == 3 &&
                  sw == 2 && nodes == 6,
              "lod switch: cap 1 -> one row, lod_switch_overflow dropped=1 cap=1, frame row switches=2");
        switch_lines.clear();
        // A Reset re-seeds: the change across it reports nothing; a captured frame logs the census rows and switch rows
        // together.
        census::reset();
        check(!census::stats().armed, "lod switch: reset disarms");
        census::begin_frame(false);
        reset_tree();
        run(R, view);
        census::present(7, 66, false);
        check(switch_lines.empty(), "lod switch: the first frame after a Reset seeds, no row");
        census::begin_frame(true);
        reset_tree();
        put(F.bytes, d_offset, 50000);
        run(R, view);
        census::present(7, 67, true);
        check(entry_lines.size() == 10 && switch_lines.size() == 2 &&
                  !std::strncmp(switch_lines[0].c_str(), "lod_switch frame=67 ", 20),
              "lod switch: captured frame keeps its ten census rows and adds the switch row");
        frame_lines.clear();
        entry_lines.clear();
        switch_lines.clear();
        // Cost of the Present-time compare per tracked node (diagnostic, not game FPS): 4,096 kept
        // children, pass + present with the log on minus the same with it off (ring filled either way).
        constexpr unsigned kids_count = 4096;
        auto* pool = static_cast<Node*>(
            VirtualAlloc(nullptr, sizeof(Node) * (kids_count + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        check(pool != nullptr, "lod switch: bench pool");
        if (pool) {
            node_set(pool[0], nullptr, 20000, 100000, 0x1002, 0, 0, 0x7000);
            std::vector<Node*> kids;
            kids.reserve(kids_count);
            for (unsigned i = 1; i <= kids_count; ++i) {
                node_set(pool[i], &pool[0], 1000, 100000, 0x1002, 0, 0, 0x7000 + i);
                kids.push_back(&pool[i]);
            }
            link(pool[0], kids.data(), kids_count);
            unsigned long long bench_frame = 100;
            auto frame_us = [&](bool on, unsigned loops) {
                census::set_lod_switch_log(on ? 16 : 0);
                LARGE_INTEGER f{}, a{}, b{};
                QueryPerformanceFrequency(&f);
                for (unsigned i = 0; i < 8; ++i) {
                    census::begin_frame(true);
                    run(pool[0], view);
                    census::present(7, bench_frame++, false);
                }
                QueryPerformanceCounter(&a);
                for (unsigned i = 0; i < loops; ++i) {
                    census::begin_frame(true);
                    run(pool[0], view);
                    census::present(7, bench_frame++, false);
                }
                QueryPerformanceCounter(&b);
                return double(b.QuadPart - a.QuadPart) * 1e6 / double(f.QuadPart) / double(loops);
            };
            double off_us = 0, on_us = 0;
            for (unsigned r = 0; r < 3; ++r) {
                off_us += frame_us(false, 200);
                on_us += frame_us(true, 200);
            }
            off_us /= 3;
            on_us /= 3;
            check(switch_lines.empty(), "lod switch: a static 4,097-node scene logs no switch");
            std::printf(
                "LOD SWITCH BENCH tracked_nodes=%u frame_off_us=%.3f frame_on_us=%.3f per_node_ns=%.2f harness=pass_and_present_included game_fps=unmeasured\n",
                kids_count + 1, off_us, on_us, (on_us - off_us) * 1000.0 / double(kids_count + 1));
        }
        census::set_lod_switch_log(0);
        census::begin_frame(false);
        check(!census::stats().armed, "lod switch off: plain frames disarmed again");
        switch_lines.clear();
    }

    // ---- cost: the same harness around the patched-armed, patched-disarmed and native pass (diagnostic, not game FPS)
    // ----
    const double armed_us = bench_us(R, view, true);
    census::present(7, 46, false);
    census::begin_frame(false);
    const double disarmed_us = bench_us(R, view, false);

    // ---- rollback ----
    check(census::shutdown() && !std::strcmp(census::state(), "restored"), "shutdown restores");
    check(sites_original() && windows_original(), "rollback bytes exact");
    reset_tree();
    const Result restored = run(R, view);
    identical = true;
    for (unsigned i = 0; i < all_count; ++i)
        identical = identical && !std::memcmp(all[i]->bytes, reference[i].bytes, sizeof(Node));
    check(identical && same_outputs(restored, native) && restored.preserved, "after rollback the native pass is back");
    census::begin_frame(true);
    reset_tree();
    run(R, view);
    check(census::stats().entries == 0, "not live: nothing recorded");
    census::present(7, 47, true);
    check(frame_lines.empty() && entry_lines.empty(), "present logs nothing while the patch is not live");
    check(census::shutdown(), "second shutdown is a no-op");
    const double native_us = bench_us(R, view, false);
    std::printf(
        "CULL CENSUS BENCH native_pass_us=%.4f patched_disarmed_us=%.4f patched_armed_us=%.4f nodes_per_pass=12 measured_per_pass=10 harness=fixture_call_included game_fps=unmeasured\n",
        native_us, disarmed_us, armed_us);
    // ---- re-install, restore, then the closed window refuses ----
    check(census::install_at(addr(site_a()), addr(site_b())) && census::shutdown() && sites_original(),
          "re-install and restore");
    x3m::engine_patch::close_install_window("fixture");
    check(!census::install_at(addr(site_a()), addr(site_b())) && !std::strcmp(census::state(), "late_claim") &&
              sites_original(),
          "closed install window: late_claim, sites untouched");
    std::printf("CULL CENSUS CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
