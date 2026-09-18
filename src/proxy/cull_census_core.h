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
constexpr unsigned parent_offset = 0x18, radius_offset = 0xa0, flags12c_offset = 0x12c, model_offset = 0x140,
                   lod_offset = 0x14c, threshold_1d8_offset = 0x1d8, threshold_1dc_offset = 0x1dc;
constexpr unsigned ring_size = 8192;
constexpr std::uint32_t no_index = 0xffffffffu;

struct Entry {
    std::uint32_t node, model, view;
    std::int32_t s, measure, d, radius, thr_1dc, thr_1d8, limit;
    std::uint32_t flags_in, flags_out;
    std::int32_t lod;
    std::uint32_t exited;
};
// The engine's effective size threshold: max(node+0x1d8, parent+0x1d8) when the
// node has a parent, node+0x1d8 otherwise (0x0047d2a2..0x0047d2b9, signed).
inline std::int32_t size_limit(std::int32_t own, bool has_parent, std::int32_t parent) {
    return has_parent && parent > own ? parent : own;
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
inline Verdict classify(const Entry& e, std::int32_t small_threshold = 0) {
    if (!e.exited) return Verdict::no_exit;
    if (e.flags_out & 2u) return Verdict::kept;
    if (e.limit > 0 && e.measure < e.limit) return Verdict::culled_size;
    if (e.measure < 1 && !(e.flags_in & 0x4000000u)) return Verdict::culled_min;
    if (small_threshold > 0 && e.s < small_threshold) return Verdict::culled_small;
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
// Exit stub (30 bytes), same entry contract:
//    0  80 3d abs32 00   CMP  byte [enabled],0
//    7  74 0f            JE   continue
//    9  50 51 52         PUSH EAX; PUSH ECX; PUSH EDX   (preserved: EAX/EDX reach the caller on a leaf's return path)
//   12  57               PUSH EDI                   ; node
//   13  e8 rel32         CALL handler(node)
//   18  83 c4 04         ADD  ESP,4
//   21  5a 59 58         POP  EDX; POP ECX; POP EAX
//   24  ff 25 abs32      JMP  [next]                ; the tail: displaced MOV + CMP (flags regenerated), jump back to the JE
constexpr unsigned exit_stub_length = 30, exit_stub_continue = 24;
inline void encode_exit_stub(std::uint32_t at, std::uint32_t enabled, std::uint32_t handler, std::uint32_t next_slot, unsigned char out[exit_stub_length]) {
    out[0] = 0x80; out[1] = 0x3d; std::memcpy(out + 2, &enabled, 4); out[6] = 0x00;
    out[7] = 0x74; out[8] = static_cast<unsigned char>(exit_stub_continue - 9);
    out[9] = 0x50; out[10] = 0x51; out[11] = 0x52;
    out[12] = 0x57;
    out[13] = 0xe8; const std::uint32_t rel = handler - (at + 18); std::memcpy(out + 14, &rel, 4);
    out[18] = 0x83; out[19] = 0xc4; out[20] = 0x04;
    out[21] = 0x5a; out[22] = 0x59; out[23] = 0x58;
    out[24] = 0xff; out[25] = 0x25; std::memcpy(out + 26, &next_slot, 4);
}
}
