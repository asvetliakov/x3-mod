#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the cull/LOD census telemetry (docs/reverse-engineering/
// lod-selection.md, "Cull census sites"; docs/architecture/engine-frame-time.md
// 2.3): the two verified byte windows of the per-node cull and LOD pass
// 0x0047cfe0, the two six-byte trampoline sites, the stub encodings, the ring
// entry and the verdict classification. No Windows dependency so the host
// tests compile it directly.
namespace x3m::cull_census::core {
// The pass (thiscall: ECX = node, [ESP+0x28] = view, [ESP+0x2c] = flag byte
// argument, reused as the local `s` from 0x0047d24a; ret 8 at 0x0047d54f).
constexpr std::uintptr_t function_va = 0x0047cfe0, function_end_va = 0x0047d552;
// Measure site: the first instruction after both per-node measures are final
// (ESI = r*W/D, [ESP+0x2c] = s = r*640/D, EBX = [ESP+0x10] = D, EDI = node,
// EBP = [ESP+0x28] = view). Reached from 0x0047d231 (jmp) and 0x0047d24e (jne)
// and by fall-through; nothing branches into 0x0047d259..0x0047d25d.
//
// 0047d248  85 c0                    TEST EAX,EAX
// 0047d24a  89 44 24 2c              MOV  [ESP+0x2c],EAX          ; s
// 0047d24e  75 08                    JNE  0x0047d258
// 0047d250  c7 44 24 2c 01 00 00 00  MOV  dword [ESP+0x2c],1      ; s clamped to >= 1
// 0047d258  8b 87 dc 01 00 00        MOV  EAX,[EDI+0x1dc]         <- site (6 bytes, displaced)
// 0047d25e  85 c0                    TEST EAX,EAX                 ; first flag writer after the site
// 0047d260  b9 00 00 18 00           MOV  ECX,0x180000            ; first ECX writer after the site
// 0047d265  7e 10                    JLE  0x0047d277
constexpr std::uintptr_t measure_window_va = 0x0047d248, measure_site_va = 0x0047d258, measure_next_va = 0x0047d25e;
constexpr unsigned measure_window_length = 31, measure_site_offset = 16, site_length = 6;
constexpr unsigned char measure_window[measure_window_length] = {
    0x85,0xc0, 0x89,0x44,0x24,0x2c, 0x75,0x08, 0xc7,0x44,0x24,0x2c,0x01,0x00,0x00,0x00,
    0x8b,0x87,0xdc,0x01,0x00,0x00, 0x85,0xc0, 0xb9,0x00,0x00,0x18,0x00, 0x7e,0x10};
constexpr unsigned char measure_site[site_length] = {0x8b,0x87,0xdc,0x01,0x00,0x00};
// Exit site: the common exit of every node the pass evaluated (bit 2 of
// +0x12c and the LOD index +0x14c are final), before the recursion over the
// children. Reached from the eight rejection/selection branches listed in the
// verifier and by fall-through; nothing branches into 0x0047d529..0x0047d52d.
//
// 0047d519  83 f9 03                       CMP  ECX,3               ; ECX = selected LOD
// 0047d51c  7c 0a                          JL   0x0047d528
// 0047d51e  81 8f 30 01 00 00 00 00 10 00  OR   dword [EDI+0x130],0x100000
// 0047d528  8b 7f 0c                       MOV  EDI,[EDI+0xc]       <- site (3 + 3 bytes, displaced)
// 0047d52b  83 3f 00                       CMP  dword [EDI],0       ; flag writer inside the displaced span
// 0047d52e  74 18                          JE   0x0047d548          ; consumes the CMP flags after the tail's jump back
// 0047d530  8b 74 24 18                    MOV  ESI,[ESP+0x18]
constexpr std::uintptr_t exit_window_va = 0x0047d519, exit_site_va = 0x0047d528, exit_next_va = 0x0047d52e;
constexpr unsigned exit_window_length = 27, exit_site_offset = 15;
constexpr unsigned char exit_window[exit_window_length] = {
    0x83,0xf9,0x03, 0x7c,0x0a, 0x81,0x8f,0x30,0x01,0x00,0x00,0x00,0x00,0x10,0x00,
    0x8b,0x7f,0x0c, 0x83,0x3f,0x00, 0x74,0x18, 0x8b,0x74,0x24,0x18};
constexpr unsigned char exit_site[site_length] = {0x8b,0x7f,0x0c, 0x83,0x3f,0x00};
constexpr unsigned ret_pop = 8;
// Render-node fields the handlers read (every one dereferenced by the pass
// itself on the same node before the site: station-material-distance.md,
// shadow-caster-lifetime.md 3a).
constexpr unsigned parent_offset = 0x18, radius_offset = 0xa0, flags12c_offset = 0x12c, flags130_offset = 0x130, model_offset = 0x140,
                   lod_offset = 0x14c, threshold_1d8_offset = 0x1d8, threshold_1dc_offset = 0x1dc;
// +0x130 bit of a class-0 (TBullets) root node: cull_small_parts_core.h projectile_flag.
constexpr std::uint32_t projectile_flag = 0x20000000;
constexpr unsigned ring_size = 8192;
constexpr std::uint32_t no_index = 0xffffffffu;
// The model's LOD ladder (docs/architecture/merged-lod-feasibility.md 1):
// 0x0047d2fd..0x0047d30e resolve node+0x140 through 0x004863c0 into EBX and
// the frame slot [ESP+0x14]; 0x0047d321 `movsx ebp, word [ebx+0x10]` is the
// LOD count; 0x0047d433/0x0047d440 index the record-pointer array at
// model+0x0c; 0x0047d442 `fild [eax+0x34]` is record i's switch value.
// [ESP+0x10] holds D (0x0047d1c8/0x0047d1eb). The exit stub passes EBX and
// both slots; the handler keeps EBX only under exit_model_pointer(), and the
// ladder itself is read at Present through the bounded engine reader.
constexpr unsigned model_slot_offset = 0x14, d_slot_offset = 0x10;
constexpr unsigned model_records_offset = 0x0c, model_lod_count_offset = 0x10, record_threshold_offset = 0x34, ladder_cap = 8;

struct Entry {
    std::uint32_t node, model, view;
    std::int32_t s, measure, d, radius, thr_1dc, thr_1d8, limit;
    std::uint32_t flags_in, flags_out;
    std::int32_t lod;
    std::uint32_t exited;
    std::uint32_t parent;   // node+0x18 at the measure site (0 = a parentless node: a body for cull_small_parts' scope)
    std::uint32_t model_ptr; // the model pointer at the exit site (exit_model_pointer), 0 when the pass had none for this node
    std::uint32_t flags130;  // node+0x130 at the measure site (projectile_flag: the small-parts stub's exemption)
    std::uint32_t flag31;    // bit 31 of the root's +0x12c (Terran-station LOD branch bit), flag31_unknown when not resolved
};
// The row's flag31 field: bit 31 of +0x12c of the node's parentless ancestor
// (the one bit that sends a Terran TDocks/TFactories subtree to the
// fixed-distance LOD branch, lod-selection.md "Terran stations and bit 31"),
// without reading any memory the pass has not just read. The pass visits a
// node's children from the exit site onwards (depth first), so a stack of
// (node, flag) pushed at the exit site holds the ancestors of the node being
// measured: a parentless node restarts it with its own bit; any other node
// takes its parent's entry. A parent not on the stack (a pass entered below
// the root, or more than ancestor_cap levels) gives flag31_unknown ("-").
constexpr unsigned ancestor_cap = 16;
constexpr std::uint32_t flag31_unknown = 2;
struct AncestorStack {
    std::uint32_t node[ancestor_cap]{}, flag[ancestor_cap]{};
    unsigned depth = 0;
    void clear() { depth = 0; }
    // The flag for a node with this parent link and +0x12c word; drops the
    // entries above the parent (finished subtrees) only when the parent is found.
    std::uint32_t resolve(std::uint32_t parent, std::uint32_t own_flags) {
        if (!parent) { depth = 0; return own_flags >> 31; }
        for (unsigned i = depth; i > 0; --i)
            if (node[i - 1] == parent) { depth = i; return flag[i - 1]; }
        return flag31_unknown;
    }
    void push(std::uint32_t n, std::uint32_t f) {
        if (depth < ancestor_cap) { node[depth] = n; flag[depth] = f; ++depth; }
    }
};
inline const char* flag31_suffix(std::uint32_t flag) {
    return flag == 0 ? " flag31=0" : flag == 1 ? " flag31=1" : " flag31=-";
}
// At the exit site EBX is the model pointer only on the LOD path: after the
// measure site EBX is written solely at 0x0047d2f6 (0: negative model id),
// 0x0047d303 (the 0x004863c0 result, also stored to [ESP+0x14] at 0x0047d30a,
// the slot's only writer) and by the threshold loop 0x0047d436/0x0047d45f,
// which 0x0047d46e restores from the slot. A node culled at 0x0047d2e7 exits
// with EBX still D, equal to [ESP+0x10] (verify_cull_census_sites.py pins the
// writer sets). So EBX is taken when non-zero, equal to the model slot and not
// equal to D; a model pointer numerically equal to D is dropped (no ladder for
// that row), never misread. The Present-time reads are bounded regardless.
inline std::uint32_t exit_model_pointer(std::uint32_t ebx, std::uint32_t model_slot, std::uint32_t d_slot) {
    return ebx && ebx == model_slot && ebx != d_slot ? ebx : 0;
}
// The row suffix ` lods=<count> thr=<t0>,<t1>,...`: count is the signed word
// at model+0x10 or `-` when the node had no model pointer or the model header
// was unreadable; thr lists record i's +0x34 for i < min(count, ladder_cap),
// or `-` when there is none (count <= 0) or any record read failed. Writes at
// most `size` bytes including the terminator; returns false on truncation.
inline bool format_ladder(char* out, unsigned size, bool known, std::int32_t count, unsigned thresholds, const std::int32_t* thr) {
    unsigned at = 0;
    auto put = [&](char c) { if (at + 1 < size) { out[at++] = c; return true; } return false; };
    auto text = [&](const char* s) { bool ok = true; while (*s) ok = put(*s++) && ok; return ok; };
    auto number = [&](std::int32_t v) {
        char digits[12]; unsigned n = 0;
        std::uint32_t u = v < 0 ? 0u - std::uint32_t(v) : std::uint32_t(v);
        do { digits[n++] = char('0' + u % 10); u /= 10; } while (u);
        bool ok = v < 0 ? put('-') : true;
        while (n) ok = put(digits[--n]) && ok;
        return ok;
    };
    if (!size) return false;
    bool ok = text(" lods=");
    ok = (known ? number(count) : put('-')) && ok;
    ok = text(" thr=") && ok;
    if (!known || !thresholds) ok = put('-') && ok;
    for (unsigned i = 0; known && i < thresholds && i < ladder_cap; ++i) {
        if (i) ok = put(',') && ok;
        ok = number(thr[i]) && ok;
    }
    out[at] = 0;
    return ok;
}
// The body name behind a model id (docs/reverse-engineering/body-format-bob1.md
// 6): the id (node+0x140 == model+0x08) indexes the body table of the manager
// at *body_global_va; slot array at +0xbc (0x1c per slot, reallocated as ids
// are registered: never cached across frames), fixed count +0xb4 (11000),
// dynamic count +0xb8, the name a char* at slot +0x0c (null: the engine uses
// "v\%05d"). 0x0046df60 and 0x0046e400 encode the same fields
// (verification/analysis/test_body_table_exe.py pins them in the installed EXE).
constexpr std::uintptr_t body_global_va = 0x00608518;
constexpr unsigned body_fixed_count_offset = 0xb4, body_dynamic_count_offset = 0xb8, body_slots_offset = 0xbc,
                   body_slot_stride = 0x1c, body_slot_name_offset = 0x0c;
constexpr std::int32_t body_fixed_count = 11000, body_dynamic_limit = 1000000;
constexpr unsigned body_name_cap = 63, body_name_scan = 256;   // printed / scanned for the NUL (names <= 255 by the save format)
// The engine's id -> slot (0x0046df60): id < 1000 -> id; 1000..19999 -> id - 9000
// (1000..8999 invalid); >= 20000 -> fixed + id - 20000; valid when 0 <= slot < fixed + dynamic.
inline bool body_slot(std::int32_t id, std::int32_t fixed, std::int32_t dynamic, std::uint32_t* slot) {
    const std::int64_t s = id < 1000 ? std::int64_t(id) : id < 20000 ? std::int64_t(id) - 9000 : std::int64_t(fixed) + id - 20000;
    if (s < 0 || s >= std::int64_t(fixed) + dynamic) return false;
    *slot = std::uint32_t(s);
    return true;
}
// The engine's name for a slot whose +0x0c is null: "v\%05d" % id (id >= 0 here).
inline void body_default_name(std::int32_t id, char* out /* >= 13 bytes */) {
    char digits[11]; unsigned n = 0;
    std::uint32_t u = id < 0 ? 0u : std::uint32_t(id);
    do { digits[n++] = char('0' + u % 10); u /= 10; } while (u);
    unsigned at = 0;
    out[at++] = 'v'; out[at++] = '\\';
    for (unsigned pad = n; pad < 5; ++pad) out[at++] = '0';
    while (n) out[at++] = digits[--n];
    out[at] = 0;
}
// The row suffix ` body=<name>`: the name (NUL-terminated, may be null) cut at
// body_name_cap characters, every byte outside 0x21..0x7e shown as '?' so the
// field stays one space-free token; `-` when unknown or empty. `out` holds at
// least body_suffix_size bytes.
constexpr unsigned body_suffix_size = 6 + body_name_cap + 1;
inline void format_body(char* out, const char* name) {
    std::memcpy(out, " body=", 6);
    unsigned at = 6;
    for (unsigned i = 0; name && name[i] && i < body_name_cap; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        out[at++] = c > 0x20 && c < 0x7f ? char(c) : '?';
    }
    if (at == 6) out[at++] = '-';
    out[at] = 0;
}
// The engine's effective size threshold: max(node+0x1d8, parent+0x1d8) when the
// node has a parent, node+0x1d8 otherwise (0x0047d2a2..0x0047d2b9, signed).
inline std::int32_t size_limit(std::int32_t own, bool has_parent, std::int32_t parent) {
    return has_parent && parent > own ? parent : own;
}
// LOD-switch log (X3M_LOD_SWITCH_LOG=N, docs/verification/cull-census.md): the
// selected record +0x14c of every node the census recorded as kept, per view,
// compared with the same (node, view) on the previous armed frame. The table
// is open-addressed with a bounded probe; a slot whose key was not seen on the
// previous or the current frame is stale and reusable, so a node that left the
// pass (or a freed node whose address is reused) re-seeds silently instead of
// reporting a switch. A changed model id at the same address re-seeds too.
constexpr unsigned track_bits = 14, track_size = 1u << track_bits, track_probe = 16;
constexpr unsigned lod_switch_default_cap = 16, lod_switch_max_cap = 4096;
struct TrackSlot { std::uint32_t node, view, model, seen; std::int32_t lod; };   // seen 0 = never used
enum class Observed : unsigned char { seeded, same, switched, untracked };
inline std::uint32_t track_hash(std::uint32_t node, std::uint32_t view) {
    return (((node >> 4) ^ (view * 0x9e3779b1u)) * 0x85ebca6bu) >> (32 - track_bits);
}
// frame >= 1 counts the armed frames since the last clear. One lookup per kept
// entry; `from` is written only on `switched`.
inline Observed track_observe(TrackSlot* table, std::uint32_t node, std::uint32_t view, std::uint32_t model, std::int32_t lod, std::uint32_t frame, std::int32_t* from) {
    TrackSlot* reuse = nullptr;
    for (unsigned p = 0, i = track_hash(node, view); p < track_probe; ++p, i = (i + 1) & (track_size - 1)) {
        TrackSlot& t = table[i];
        if (!t.seen) { if (!reuse) reuse = &t; break; }   // slots are never emptied: the key is not further on
        if (t.node == node && t.view == view) {
            if (t.seen == frame) return Observed::same;     // a second visit in the same frame and view: first one wins
            const bool consecutive = t.seen + 1 == frame && t.model == model;
            const std::int32_t old = t.lod;
            t.model = model; t.lod = lod; t.seen = frame;
            if (!consecutive) return Observed::seeded;
            if (old == lod) return Observed::same;
            *from = old;
            return Observed::switched;
        }
        if (!reuse && t.seen + 1 < frame) reuse = &t;
    }
    if (!reuse) return Observed::untracked;
    reuse->node = node; reuse->view = view; reuse->model = model; reuse->seen = frame; reuse->lod = lod;
    return Observed::seeded;
}
// A signed decimal into out (size >= 12 holds any int32).
inline void format_int(char* out, unsigned size, std::int32_t v) {
    char digits[11]; unsigned n = 0, at = 0;
    std::uint32_t u = v < 0 ? 0u - std::uint32_t(v) : std::uint32_t(v);
    do { digits[n++] = char('0' + u % 10); u /= 10; } while (u);
    if (!size) return;
    if (v < 0 && at + 1 < size) out[at++] = '-';
    while (n && at + 1 < size) out[at++] = digits[--n];
    out[at] = 0;
}
// X3M_LOD_SWITCH_LOG: decimal digits only, 0 = off, 1..lod_switch_max_cap rows per frame.
template<class Char>
inline bool parse_lod_switch_cap(const Char* text, unsigned length, unsigned* cap) {
    if (!length || length > 5) return false;
    unsigned v = 0;
    for (unsigned i = 0; i < length; ++i) {
        if (text[i] < Char('0') || text[i] > Char('9')) return false;
        v = v * 10 + unsigned(text[i] - Char('0'));
    }
    if (v > lod_switch_max_cap) return false;
    *cap = v;
    return true;
}
enum class Verdict : unsigned char { kept = 0, culled_size, culled_min, culled_other, no_exit, culled_small };
constexpr unsigned verdict_count = 6;
inline const char* verdict_name(Verdict v) {
    static const char* const names[verdict_count] = {"kept", "culled_size", "culled_min", "culled_other", "no_exit", "culled_small"};
    return unsigned(v) < verdict_count ? names[unsigned(v)] : "?";
}
// Classification from the recorded fields, mirroring 0x0047d2a2..0x0047d2e1:
// a node whose renderable bit is clear at the exit was culled by the size
// threshold when measure < limit > 0, by the degenerate-size test when
// measure < 1 (bit 0x4000000 clear), by the small-parts stub when its `s`
// is below the frame's pixel threshold (X3M_CULL_SMALL_PARTS_PX; the stub
// runs before both engine tests, but the engine's own rules are named first
// so the census keeps showing the engine's share), otherwise by a later
// step (the env-map view's < 20 test with the bit set, or the last-LOD fade).
// small_bodies_only mirrors the stub's scope (X3M_CULL_SMALL_PARTS_SCOPE=bodies):
// it never culls a parented node, so such a node is not named culled_small.
// small_exempt_projectiles mirrors X3M_CULL_SMALL_PARTS_PROJECTILES=on: a node
// carrying projectile_flag runs the engine's own compare, so it is not either.
inline bool small_exempt(const Entry& e, std::int32_t small_threshold, bool small_exempt_projectiles) {
    return small_exempt_projectiles && small_threshold > 0 && e.s < small_threshold && (e.flags130 & projectile_flag);
}
inline Verdict classify(const Entry& e, std::int32_t small_threshold = 0, bool small_bodies_only = false, bool small_exempt_projectiles = false) {
    if (!e.exited) return Verdict::no_exit;
    if (e.flags_out & 2u) return Verdict::kept;
    if (e.limit > 0 && e.measure < e.limit) return Verdict::culled_size;
    if (e.measure < 1 && !(e.flags_in & 0x4000000u)) return Verdict::culled_min;
    if (small_threshold > 0 && e.s < small_threshold && !(small_bodies_only && e.parent) && !small_exempt(e, small_threshold, small_exempt_projectiles)) return Verdict::culled_small;
    return Verdict::culled_other;
}

// Measure stub (43 bytes), entered by the dispatcher's `jmp [entry]` with the
// site's exact register state and ESP (no return address):
//    0  80 3d abs32 00   CMP  byte [enabled],0     ; option on and a capture frame
//    7  74 1c            JE   continue              ; the dead branch otherwise
//    9  50 51 52         PUSH EAX; PUSH ECX; PUSH EDX   (dead at the site; kept anyway)
//   12  ff 74 24 34      PUSH dword [ESP+0x34]      ; view  = site [ESP+0x28]
//   16  ff 74 24 20      PUSH dword [ESP+0x20]      ; D     = site [ESP+0x10]
//   20  ff 74 24 40      PUSH dword [ESP+0x40]      ; s     = site [ESP+0x2c]
//   24  56 57            PUSH ESI; PUSH EDI         ; measure, node
//   26  e8 rel32         CALL handler(node, measure, s, d, view)   cdecl, integer only
//   31  83 c4 14         ADD  ESP,20
//   34  5a 59 58         POP  EDX; POP ECX; POP EAX
//   37  ff 25 abs32      JMP  [next]                ; the tail: displaced MOV, jump back
// EFLAGS are dead at the site (TEST EAX,EAX at 0x0047d25e writes them first);
// EBX/EBP/ESI/EDI/ESP and the caller's locals are untouched (cdecl handler).
constexpr unsigned measure_stub_length = 43, measure_stub_continue = 37;
inline void encode_measure_stub(std::uint32_t at, std::uint32_t enabled, std::uint32_t handler, std::uint32_t next_slot, unsigned char out[measure_stub_length]) {
    out[0] = 0x80; out[1] = 0x3d; std::memcpy(out + 2, &enabled, 4); out[6] = 0x00;
    out[7] = 0x74; out[8] = static_cast<unsigned char>(measure_stub_continue - 9);
    out[9] = 0x50; out[10] = 0x51; out[11] = 0x52;
    out[12] = 0xff; out[13] = 0x74; out[14] = 0x24; out[15] = 0x34;
    out[16] = 0xff; out[17] = 0x74; out[18] = 0x24; out[19] = 0x20;
    out[20] = 0xff; out[21] = 0x74; out[22] = 0x24; out[23] = 0x40;
    out[24] = 0x56; out[25] = 0x57;
    out[26] = 0xe8; const std::uint32_t rel = handler - (at + 31); std::memcpy(out + 27, &rel, 4);
    out[31] = 0x83; out[32] = 0xc4; out[33] = 0x14;
    out[34] = 0x5a; out[35] = 0x59; out[36] = 0x58;
    out[37] = 0xff; out[38] = 0x25; std::memcpy(out + 39, &next_slot, 4);
}
// Exit stub (39 bytes), same entry contract:
//    0  80 3d abs32 00   CMP  byte [enabled],0
//    7  74 18            JE   continue
//    9  50 51 52         PUSH EAX; PUSH ECX; PUSH EDX   (preserved: EAX/EDX reach the caller on a leaf's return path)
//   12  ff 74 24 1c      PUSH dword [ESP+0x1c]      ; D slot     = site [ESP+0x10]
//   16  ff 74 24 24      PUSH dword [ESP+0x24]      ; model slot = site [ESP+0x14]
//   20  53 57            PUSH EBX; PUSH EDI         ; model pointer candidate, node
//   22  e8 rel32         CALL handler(node, ebx, model_slot, d_slot)   cdecl, integer only
//   27  83 c4 10         ADD  ESP,16
//   30  5a 59 58         POP  EDX; POP ECX; POP EAX
//   33  ff 25 abs32      JMP  [next]                ; the tail: displaced MOV + CMP (flags regenerated), jump back to the JE
// The two slot reads are stack loads inside the pass's own frame (sub esp,0x14
// at 0x0047cfe0), valid on every path that reaches the site.
constexpr unsigned exit_stub_length = 39, exit_stub_continue = 33;
inline void encode_exit_stub(std::uint32_t at, std::uint32_t enabled, std::uint32_t handler, std::uint32_t next_slot, unsigned char out[exit_stub_length]) {
    out[0] = 0x80; out[1] = 0x3d; std::memcpy(out + 2, &enabled, 4); out[6] = 0x00;
    out[7] = 0x74; out[8] = static_cast<unsigned char>(exit_stub_continue - 9);
    out[9] = 0x50; out[10] = 0x51; out[11] = 0x52;
    out[12] = 0xff; out[13] = 0x74; out[14] = 0x24; out[15] = static_cast<unsigned char>(d_slot_offset + 12);
    out[16] = 0xff; out[17] = 0x74; out[18] = 0x24; out[19] = static_cast<unsigned char>(model_slot_offset + 16);
    out[20] = 0x53; out[21] = 0x57;
    out[22] = 0xe8; const std::uint32_t rel = handler - (at + 27); std::memcpy(out + 23, &rel, 4);
    out[27] = 0x83; out[28] = 0xc4; out[29] = 0x10;
    out[30] = 0x5a; out[31] = 0x59; out[32] = 0x58;
    out[33] = 0xff; out[34] = 0x25; std::memcpy(out + 35, &next_slot, 4);
}
}
