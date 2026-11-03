#pragma once
#include "../../proxy/collide_box_cull_core.h"
#include "collide_windows_fl.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

// FL edition of the collision box-cull sites (the AP twin is src/proxy/collide_box_cull_core.h, whose integer box test
// box_reject(), wrapped_difference(), the 2^30 axis cap, the Counters shape and the 300-frame Window are shared
// unchanged). Verified facts: docs/reverse-engineering/fl/collision-box.md (static contract, conservative bound, "Local
// FP admission decision (2026-10-29)", "Port (2026-11-03)"; verifier verification/probe/verify_collide_box_cull_sites_fl.py).
// X3FL.exe is relocatable: every value below is an RVA added to x3m::fl::module_base(), or a field offset. The nine
// windows (collide_windows_fl_inc.h, box table) carry no HIGHLOW, so their bytes are base independent.
//
// P1, the all-pairs loop 0x066de0 (EDI = object A, EBX = object B, ESP the loop frame):
// 06703d  8b 97 a4 00 00 00 / 03 93 a4 00 00 00 / 0f b7 47 48 / 89 54 24 34    r = A+0xa4 + B+0xa4 (32-bit), [ESP+0x34]
// 0671b1  66 83 f8 07 / 74 0a / 66 83 f9 07 / 0f 85 7e 00 00 00   class words; neither 7 -> 0x06723f; a 7 runs the
//         loose box 3r + 250000 first and falls through 0x067239 into the same site
// 06723f  8b 4f 70 8b 53 70      MOV ECX,[EDI+0x70]; MOV EDX,[EBX+0x70]   <- P1 site (6 bytes, two whole MOVs)
// 067245  8b 41 30 / 2b 42 30    MOV EAX,[ECX+0x30]; SUB EAX,[EDX+0x30]   ; EAX/ECX/EDX written before read, EFLAGS dead
//         ... CVTDQ2PS x3, MULSS x3, ADDSS x2, CALL 0x012a80 (SQRTSS), CVTTSS2SI ECX ...
// 0672a7  3b c8 / 0f 8f ff 00 00 00   CMP ECX,EAX (EAX = (r*0x1028f + 0x8000) >> 16); JG 0x0673ae
// 0673ae  class words again: neither 7 -> JNE 0x067e7e (the continuation); a 7 reuses the distance
// 067e7e  8b 5c 24 44 / 8b c3 / 83 38 00   the continuation reloads EBX, EAX, writes EFLAGS; ESI live (kept by the stub)
// P2, the swept scan 0x066660 (EBX = candidate, EDI = reference):
// 066859  8b 43 70 8b 57 70      MOV EAX,[EBX+0x70]; MOV EDX,[EDI+0x70]   <- P2 site (6 bytes, two whole MOVs)
// 0668b7  03 44 24 2c / 8b 74 24 14   R = candidate +0xa4 + sweep [ESP+0x2c]; ESI = [ESP+0x14] (the count)
// 0668c3  3b c8 / 0f 8f 50 01 00 00   CMP ECX,EAX; JG 0x066a1b (the continuation)
// 066a1b  8b 1b / 83 3b 00 ...   next candidate; 0x066a49 `cmp esi,1` reads ESI = the count: the reject restores it
// 012a80  55 8b ec 83 e4 f8 f3 0f 51 c0 8b e5 5d c3   the sqrt helper: SQRTSS XMM0,XMM0 only, plain RET
namespace x3m::fl::collide_box_cull_fl {
namespace ap = x3m::collide_box_cull::core;
namespace W = x3m::fl::collide_windows;

constexpr std::uint32_t p1_function_rva = 0x066de0, p2_function_rva = 0x066660;
constexpr std::uint32_t p1_site_rva = 0x06723f, p1_compare_rva = 0x0672a7, p1_reject_rva = 0x0673ae,
                        p1_continue_rva = 0x067e7e, p1_sqrt_call_rva = 0x067280, radius_store_rva = 0x06704d;
constexpr std::uint32_t p2_site_rva = 0x066859, p2_compare_rva = 0x0668c3, p2_continue_rva = 0x066a1b,
                        p2_sqrt_call_rva = 0x0668b2, p2_count_reload_rva = 0x0668bb;
constexpr std::uint32_t sqrt_helper_rva = 0x012a80;
constexpr unsigned site_length = 6, ret_pop = 0;
constexpr unsigned char p1_site[site_length] = {0x8b, 0x4f, 0x70, 0x8b, 0x53, 0x70};
constexpr unsigned char p2_site[site_length] = {0x8b, 0x43, 0x70, 0x8b, 0x57, 0x70};
// Object layout (both sites): class word +0x48, physics block +0x70 (signed integer position +0x30/+0x34/+0x38),
// radius +0xa4: AP's offsets.
constexpr unsigned class_offset = ap::class_offset, physics_offset = ap::physics_offset,
                   radius_offset = ap::radius_offset, position_offset = ap::position_offset;
constexpr std::uint16_t class_gate = ap::class_gate;
constexpr unsigned p1_radius_slot = 0x34, p2_sweep_slot = 0x2c, p2_count_slot = 0x14;
// The FL margin (collision-box.md, "Conservative FL bound"): the native distance D satisfies D > m - 257 for
// m = max|d| < 2^30 under every MXCSR rounding mode, DAZ and FZ; so m > T = R' + 256 implies D > R.
constexpr std::int32_t margin = 256;
constexpr std::uint32_t mxcsr_admit_mask = 0x1020; // precision masked (0x1000) and precision already sticky (0x20)

// ---- host model of the emitted decision (integer for integer) ----
inline bool p1_threshold(std::int32_t radius_sum, std::int32_t* threshold) {
    if (radius_sum < 0) return false;
    const std::int64_t a = std::int64_t(radius_sum) + (radius_sum >> 5);
    if (a > 0x7fffffff) return false;
    const std::int64_t t = a + margin;
    if (t > 0x7fffffff) return false;
    *threshold = static_cast<std::int32_t>(t);
    return true;
}
inline bool p2_threshold(std::int32_t candidate_radius, std::int32_t sweep, std::int32_t* threshold) {
    const std::int64_t r = std::int64_t(candidate_radius) + sweep;
    if (r > 0x7fffffff || r < -0x7fffffff - 1) return false;
    const std::int64_t t = r + margin;
    if (t > 0x7fffffff) return false;
    *threshold = static_cast<std::int32_t>(t);
    return true;
}
inline bool fp_admitted(std::uint32_t mxcsr) {
    return (mxcsr & mxcsr_admit_mask) == mxcsr_admit_mask;
}
inline bool p1_box_reject(std::uint16_t class_a, std::uint16_t class_b, std::int32_t dx, std::int32_t dy,
                          std::int32_t dz, std::int32_t radius_sum, std::uint32_t mxcsr) {
    std::int32_t t = 0;
    if (class_a == class_gate || class_b == class_gate || !p1_threshold(radius_sum, &t)) return false;
    return ap::box_reject(dx, dy, dz, t) && fp_admitted(mxcsr);
}
inline bool p2_box_reject(std::int32_t dx, std::int32_t dy, std::int32_t dz, std::int32_t candidate_radius,
                          std::int32_t sweep, std::uint32_t mxcsr) {
    std::int32_t t = 0;
    if (!p2_threshold(candidate_radius, sweep, &t)) return false;
    return ap::box_reject(dx, dy, dz, t) && fp_admitted(mxcsr);
}
// ---- host model of the native distance (scalar SSE float32 as decoded; the host build is -msse2 -mfpmath=sse) ----
inline std::int32_t engine_distance(std::int32_t dx, std::int32_t dy, std::int32_t dz) {
    const float x = static_cast<float>(dx), y = static_cast<float>(dy), z = static_cast<float>(dz);
    float s = x * x;
    s = s + y * y;
    s = s + z * z;
    const float root = std::sqrt(s);
    if (!(root < 2147483648.0f) || !(root > -2147483649.0f)) return static_cast<std::int32_t>(0x80000000u);
    return static_cast<std::int32_t>(root);
}
inline std::int32_t p1_engine_threshold(std::int32_t radius_sum) {
    const std::int64_t scaled = std::int64_t(radius_sum) * 0x1028f + 0x8000;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(scaled) >> 16));
}
inline bool p1_engine_reject(std::int32_t dx, std::int32_t dy, std::int32_t dz, std::int32_t radius_sum) {
    return engine_distance(dx, dy, dz) > p1_engine_threshold(radius_sum);
}
inline bool p2_engine_reject(std::int32_t dx, std::int32_t dy, std::int32_t dz, std::int32_t candidate_radius,
                             std::int32_t sweep) {
    return engine_distance(dx, dy, dz) > ap::wrapped_difference(candidate_radius, ap::wrapped_difference(0, sweep));
}

// ---- stub encoders ----
// Entered by the dispatcher's `jmp [entry]` with the site's exact registers and ESP (no return address). Integer only:
// no call, no x87/SSE register, no memory write but the optional counters and the stub's own balanced stack words; one
// STMXCSR (a read) on a prospective reject only. Every exit restores ESP; native exits leave EAX/ECX/EDX as garbage the
// displaced MOVs overwrite, EFLAGS dead (the first instruction after either site writes them).
constexpr unsigned stub_capacity = 224;
class Writer {
public:
    explicit Writer(unsigned char* out)
        : out_(out) {}
    unsigned length() const { return n_; }
    void byte(unsigned char b) {
        if (n_ < stub_capacity) out_[n_] = b;
        ++n_;
    }
    void bytes(const unsigned char* p, unsigned n) {
        for (unsigned i = 0; i < n; ++i) byte(p[i]);
    }
    void dword(std::uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) byte(static_cast<unsigned char>((v >> (8 * i)) & 0xff));
    }
    void jcc(unsigned char condition, unsigned label) { // 0F 8x rel32 to a label resolved later
        byte(0x0f);
        byte(condition);
        if (fixes_ < 16) fix_[fixes_++] = Fix{n_, label};
        dword(0);
    }
    void label(unsigned id) { label_[id] = n_; }
    bool resolve() {
        if (n_ > stub_capacity || fixes_ >= 16) return false;
        for (unsigned i = 0; i < fixes_; ++i) {
            const std::uint32_t rel = std::uint32_t(label_[fix_[i].label]) - std::uint32_t(fix_[i].at + 4);
            for (unsigned k = 0; k < 4; ++k) out_[fix_[i].at + k] = static_cast<unsigned char>((rel >> (8 * k)) & 0xff);
        }
        return true;
    }

private:
    struct Fix {
        unsigned at, label;
    };
    unsigned char* out_;
    unsigned n_ = 0;
    Fix fix_[16]{};
    unsigned fixes_ = 0;
    unsigned label_[2]{};
};
constexpr unsigned label_native_pop = 0, label_native = 1;
constexpr unsigned char je = 0x84, jne = 0x85, js = 0x88, jo = 0x80, jae = 0x83, jle = 0x8e;
inline void emit_inc(Writer& w, std::uint32_t slot) {
    if (!slot) return;
    w.byte(0xff);
    w.byte(0x05);
    w.dword(slot);
}
inline void emit_code(Writer& w, std::initializer_list<unsigned char> code) {
    for (unsigned char b : code) w.byte(b);
}
// |reg| (TEST; JNS +2; NEG): INT_MIN stays INT_MIN, the unsigned cap compare catches it.
inline void emit_abs(Writer& w, unsigned char test_modrm, unsigned char neg_modrm) {
    emit_code(w, {0x85, test_modrm, 0x79, 0x02, 0xf7, neg_modrm});
}
// The local FP admission (collision-box.md, "Local FP admission decision"): PUSH EAX; STMXCSR [ESP]; POP EAX;
// AND EAX,0x1020; CMP EAX,0x1020; JNE native. A read: no LDMXCSR, no FP arithmetic.
inline void emit_fp_gate(Writer& w, unsigned label) {
    emit_code(w, {0x50, 0x0f, 0xae, 0x1c, 0x24, 0x58, 0x25});
    w.dword(mxcsr_admit_mask);
    w.byte(0x3d);
    w.dword(mxcsr_admit_mask);
    w.jcc(jne, label);
}
// P1 stub. EAX/ECX/EDX free (written by the displaced MOVs and 0x067245 before any read); ESI is live on both exits, so
// it is saved around its use as the max register (one PUSH, popped on every path that pushed it).
//   cmp byte [enabled],0 ; je native ; [inc dword [entered]]
//   cmp word [edi+0x48],7 ; je native ; cmp word [ebx+0x48],7 ; je native
//   mov eax,[esp+0x34] ; test eax,eax ; js native ; mov edx,eax ; sar edx,5 ; add eax,edx ; jo native
//   add eax,256 ; jo native ; push esi
//   mov ecx,[edi+0x70] ; mov edx,[ebx+0x70] ; mov esi,[ecx+0x30] ; sub esi,[edx+0x30] ; |esi| ; push eax
//   mov eax,[ecx+0x34] ; sub eax,[edx+0x34] ; |eax| ; cmp esi,eax ; cmovb esi,eax      (and +0x38)
//   pop eax ; cmp esi,0x40000000 ; jae native_pop ; cmp esi,eax ; jle native_pop ; FP gate -> native_pop
//   pop esi ; [inc dword [rejected]] ; jmp base+0x067e7e
// native_pop: pop esi
// native:     jmp [next]
inline unsigned encode_p1_stub(std::uint32_t at, std::uint32_t enabled, ap::Counters counters, std::uint32_t next_slot,
                               std::uint32_t continue_target, unsigned char out[stub_capacity]) {
    Writer w(out);
    emit_code(w, {0x80, 0x3d});
    w.dword(enabled);
    w.byte(0x00);
    w.jcc(je, label_native);
    emit_inc(w, counters.entered);
    emit_code(w, {0x66, 0x83, 0x7f, std::uint8_t(class_offset), std::uint8_t(class_gate)});
    w.jcc(je, label_native);
    emit_code(w, {0x66, 0x83, 0x7b, std::uint8_t(class_offset), std::uint8_t(class_gate)});
    w.jcc(je, label_native);
    emit_code(w, {0x8b, 0x44, 0x24, std::uint8_t(p1_radius_slot), 0x85, 0xc0});
    w.jcc(js, label_native);
    emit_code(w, {0x8b, 0xd0, 0xc1, 0xfa, 0x05, 0x03, 0xc2});
    w.jcc(jo, label_native);
    w.byte(0x05);
    w.dword(std::uint32_t(margin));
    w.jcc(jo, label_native);
    w.byte(0x56);
    emit_code(w, {0x8b, 0x4f, std::uint8_t(physics_offset), 0x8b, 0x53, std::uint8_t(physics_offset)});
    emit_code(w, {0x8b, 0x71, std::uint8_t(position_offset), 0x2b, 0x72, std::uint8_t(position_offset)});
    emit_abs(w, 0xf6, 0xde);
    w.byte(0x50);
    for (unsigned axis = 1; axis < 3; ++axis) {
        const std::uint8_t off = std::uint8_t(position_offset + 4 * axis);
        emit_code(w, {0x8b, 0x41, off, 0x2b, 0x42, off});
        emit_abs(w, 0xc0, 0xd8);
        emit_code(w, {0x3b, 0xf0, 0x0f, 0x42, 0xf0});
    }
    w.byte(0x58);
    emit_code(w, {0x81, 0xfe});
    w.dword(std::uint32_t(ap::axis_cap));
    w.jcc(jae, label_native_pop);
    emit_code(w, {0x3b, 0xf0});
    w.jcc(jle, label_native_pop);
    emit_fp_gate(w, label_native_pop);
    w.byte(0x5e);
    emit_inc(w, counters.rejected);
    w.byte(0xe9);
    w.dword(continue_target - (at + w.length() + 4));
    w.label(label_native_pop);
    w.byte(0x5e);
    w.label(label_native);
    emit_code(w, {0xff, 0x25});
    w.dword(next_slot);
    return w.resolve() ? w.length() : 0;
}
// P2 stub. EAX/ECX/EDX/ESI free (written by the displaced MOVs, 0x066863 and 0x066866 before any read).
//   cmp byte [enabled],0 ; je native ; [inc dword [entered]]
//   mov ecx,[ebx+0xa4] ; add ecx,[esp+0x2c] ; jo native ; add ecx,256 ; jo native
//   mov eax,[ebx+0x70] ; mov edx,[edi+0x70] ; mov esi,[eax+0x30] ; sub esi,[edx+0x30] ; |esi| ; push ecx
//   mov ecx,[eax+0x34] ; sub ecx,[edx+0x34] ; |ecx| ; cmp esi,ecx ; cmovb esi,ecx      (and +0x38)
//   pop ecx ; cmp esi,0x40000000 ; jae native ; cmp esi,ecx ; jle native ; FP gate -> native
//   [inc dword [rejected]] ; mov esi,[esp+0x14] (the count, as 0x0668bb) ; jmp base+0x066a1b
// native: jmp [next]
inline unsigned encode_p2_stub(std::uint32_t at, std::uint32_t enabled, ap::Counters counters, std::uint32_t next_slot,
                               std::uint32_t continue_target, unsigned char out[stub_capacity]) {
    Writer w(out);
    emit_code(w, {0x80, 0x3d});
    w.dword(enabled);
    w.byte(0x00);
    w.jcc(je, label_native);
    emit_inc(w, counters.entered);
    emit_code(w, {0x8b, 0x8b});
    w.dword(radius_offset);
    emit_code(w, {0x03, 0x4c, 0x24, std::uint8_t(p2_sweep_slot)});
    w.jcc(jo, label_native);
    emit_code(w, {0x81, 0xc1});
    w.dword(std::uint32_t(margin));
    w.jcc(jo, label_native);
    emit_code(w, {0x8b, 0x43, std::uint8_t(physics_offset), 0x8b, 0x57, std::uint8_t(physics_offset)});
    emit_code(w, {0x8b, 0x70, std::uint8_t(position_offset), 0x2b, 0x72, std::uint8_t(position_offset)});
    emit_abs(w, 0xf6, 0xde);
    w.byte(0x51);
    for (unsigned axis = 1; axis < 3; ++axis) {
        const std::uint8_t off = std::uint8_t(position_offset + 4 * axis);
        emit_code(w, {0x8b, 0x48, off, 0x2b, 0x4a, off});
        emit_abs(w, 0xc9, 0xd9);
        emit_code(w, {0x3b, 0xf1, 0x0f, 0x42, 0xf1});
    }
    w.byte(0x59);
    emit_code(w, {0x81, 0xfe});
    w.dword(std::uint32_t(ap::axis_cap));
    w.jcc(jae, label_native);
    emit_code(w, {0x3b, 0xf1});
    w.jcc(jle, label_native);
    emit_fp_gate(w, label_native);
    emit_inc(w, counters.rejected);
    emit_code(w, {0x8b, 0x74, 0x24, std::uint8_t(p2_count_slot)});
    w.byte(0xe9);
    w.dword(continue_target - (at + w.length() + 4));
    w.label(label_native);
    emit_code(w, {0xff, 0x25});
    w.dword(next_slot);
    return w.resolve() ? w.length() : 0;
}
// Measured lengths of the emitted stubs (counters on / off); the host test and the fixture decode them.
constexpr unsigned p1_stub_length_counted = 193, p1_stub_length_plain = 181;
constexpr unsigned p2_stub_length_counted = 164, p2_stub_length_plain = 152;

// The claimed span reads back as our five-byte jump plus the displaced MOV's untouched last byte.
inline bool claimed_span(const unsigned char now[site_length], const unsigned char jump[5], const unsigned char* site) {
    return !std::memcmp(now, jump, 5) && now[5] == site[5];
}
static_assert(W::box_windows[2].rva == p1_site_rva && W::box_windows[6].rva == p2_site_rva, "the site windows");
static_assert(W::box_windows[4].rva == p1_continue_rva && W::box_windows[7].rva == p2_continue_rva, "continuations");
static_assert(W::box_windows[8].rva == sqrt_helper_rva, "the sqrt helper window");
static_assert((p2_site_rva & 7u) + 5u <= 8u, "P2's five jump bytes lie in one aligned qword (atomic)");
static_assert((p1_site_rva & 7u) + 5u > 8u, "P1's five jump bytes cross an aligned qword (plain write, install window)");
}
