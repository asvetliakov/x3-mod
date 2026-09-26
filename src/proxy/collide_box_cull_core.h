#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Portable core of the sector-collide bounding-box early-out
// (docs/reverse-engineering/sector-collide.md sections 6 and 10;
// docs/architecture/engine-frame-time.md 2.1): the verified byte windows of
// the two square-root pair tests, the two six-byte trampoline sites, the stub
// encoders, the host models of the engine's compare and of the stub's box
// test, and the per-frame counter window. No Windows dependency so the host
// tests compile it directly.
//
// Both sites are the first instruction of a per-pair distance test whose only
// consumer is `dist > R -> reject`, and the reject label writes nothing. The
// stub computes the integer box the engine already uses for class-0/7 pairs
// (|dx|, |dy|, |dz| against a threshold) with a margin that provably exceeds
// the float32 rounding and the truncation of the engine's own distance, and
// jumps to the engine's continue label when the box rejects; otherwise it
// falls into the displaced instructions. A pair the engine would keep is
// never rejected: max|d| > T with every |d| < 2^30 implies the engine's
// dist > R (proof in the note, checked by the host model below and by the
// X3 CPU fixture on the engine's own bytes).
namespace x3m::collide_box_cull::core {
// ---- P1: the all-pairs loop L2 of 0x0045d250 ----
// 0045d516  75 76                 JNE  0x0045d58e               ; neither class is 7 (the only inbound branch)
// 0045d588  0f 8f 02 0a 00 00     JG   0x0045df90               ; class-7 box reject (fall-through otherwise)
// 0045d58e  8b 4b 70              MOV  ECX,[EBX+0x70]           <- site (6 bytes displaced: two whole MOVs)
// 0045d591  8b 51 30              MOV  EDX,[ECX+0x30]
// 0045d594  8b 46 70              MOV  EAX,[ESI+0x70]
// 0045d597  2b 50 30              SUB  EDX,[EAX+0x30]           ; first flag writer after the site
// 0045d59a  83 c1 30              ADD  ECX,0x30
// 0045d59d  83 c0 30              ADD  EAX,0x30
// 0045d5a0  89 54 24 28           MOV  [ESP+0x28],EDX
// 0045d5a4  db 44 24 28           FILD dword [ESP+0x28]         ; first x87 push
// ...
// 0045d5e7  8b c8                 MOV  ECX,EAX                  ; dist = trunc(sqrt(fl32(sum)))
// 0045d5e9  89 4c 24 1c           MOV  [ESP+0x1c],ECX
// 0045d5ed  8b 44 24 24           MOV  EAX,[ESP+0x24]           ; r = [A+0xa4] + [B+0xa4] (0x0045d48a)
// 0045d5f1  ba 8f 02 01 00        MOV  EDX,0x1028f
// 0045d5f6  f7 ea                 IMUL EDX
// 0045d5f8  05 00 80 00 00        ADD  EAX,0x8000
// 0045d5fd  83 d2 00              ADC  EDX,0
// 0045d600  0f ac d0 10           SHRD EAX,EDX,0x10             ; R = (r*0x1028f + 0x8000) >> 16
// 0045d604  3b c8                 CMP  ECX,EAX
// 0045d606  0f 8f c0 00 00 00     JG   0x0045d6cc               ; dist > R
// ...
// 0045d6cc  0f b7 43 48           MOVZX EAX,word [EBX+0x48]
// 0045d6d0  bf 07 00 00 00        MOV  EDI,7
// 0045d6d5  66 3b c7              CMP  AX,DI
// 0045d6d8  74 0a                 JE   0x0045d6e4               ; class-7 pairs reuse the distance
// 0045d6da  66 39 7e 48           CMP  word [ESI+0x48],DI
// 0045d6de  0f 85 ac 08 00 00     JNE  0x0045df90               ; neither 7: continue, nothing written
// ...
// 0045df90  8b 74 24 3c           MOV  ESI,[ESP+0x3c]           ; the continue label
// 0045df94  83 3e 00              CMP  dword [ESI],0
// 0045df97  89 74 24 14           MOV  [ESP+0x14],ESI
// 0045df9b  0f 85 6f f4 ff ff     JNE  0x0045d410
constexpr std::uintptr_t p1_function_va = 0x0045d250, p1_function_end_va = 0x0045e0b8;
constexpr std::uintptr_t p1_site_va = 0x0045d58e, p1_next_va = 0x0045d594;
constexpr std::uintptr_t p1_compare_va = 0x0045d5e7, p1_reject_va = 0x0045d6cc, p1_continue_va = 0x0045df90;
constexpr std::uintptr_t p1_sqrt_call_va = 0x0045d5da, p1_ftol_call_va = 0x0045d5e2;
constexpr unsigned p1_compare_offset = 0x59, p1_reject_offset = 0x13e, p1_continue_offset = 0xa02;
constexpr unsigned site_length = 6;
constexpr unsigned p1_site_window_length = 26, p1_compare_window_length = 37, p1_reject_window_length = 24,
                   p1_continue_window_length = 11;
constexpr unsigned char p1_site_window[p1_site_window_length] = {0x8b, 0x4b, 0x70, 0x8b, 0x51, 0x30, 0x8b, 0x46, 0x70,
                                                                 0x2b, 0x50, 0x30, 0x83, 0xc1, 0x30, 0x83, 0xc0, 0x30,
                                                                 0x89, 0x54, 0x24, 0x28, 0xdb, 0x44, 0x24, 0x28};
constexpr unsigned char p1_compare_window[p1_compare_window_length] = {
    0x8b, 0xc8, 0x89, 0x4c, 0x24, 0x1c, 0x8b, 0x44, 0x24, 0x24, 0xba, 0x8f, 0x02, 0x01, 0x00, 0xf7, 0xea, 0x05, 0x00,
    0x80, 0x00, 0x00, 0x83, 0xd2, 0x00, 0x0f, 0xac, 0xd0, 0x10, 0x3b, 0xc8, 0x0f, 0x8f, 0xc0, 0x00, 0x00, 0x00};
constexpr unsigned char p1_reject_window[p1_reject_window_length] = {0x0f, 0xb7, 0x43, 0x48, 0xbf, 0x07, 0x00, 0x00,
                                                                     0x00, 0x66, 0x3b, 0xc7, 0x74, 0x0a, 0x66, 0x39,
                                                                     0x7e, 0x48, 0x0f, 0x85, 0xac, 0x08, 0x00, 0x00};
constexpr unsigned char p1_continue_window[p1_continue_window_length] = {0x8b, 0x74, 0x24, 0x3c, 0x83, 0x3e,
                                                                         0x00, 0x89, 0x74, 0x24, 0x14};
constexpr unsigned char p1_site[site_length] = {0x8b, 0x4b, 0x70, 0x8b, 0x51, 0x30};
constexpr unsigned p1_radius_sum_slot = 0x24; // [ESP+0x24] at the site: the radius sum the engine scales at 0x0045d5ed
// ---- P2: the swept scan L1 of 0x0045cab0 ----
// 0045cc5e  74 1c                 JE   0x0045cc7c               ; inbound
// 0045cc70  75 0a                 JNE  0x0045cc7c               ; inbound
// 0045cc7c  8b 47 70              MOV  EAX,[EDI+0x70]           <- site (6 bytes displaced: two whole MOVs)
// 0045cc7f  8b 48 30              MOV  ECX,[EAX+0x30]
// 0045cc82  8b 73 70              MOV  ESI,[EBX+0x70]           ; ESI written before any read: dead at the site
// 0045cc85  2b 4e 30              SUB  ECX,[ESI+0x30]           ; first flag writer after the site
// 0045cc88  8b 50 34              MOV  EDX,[EAX+0x34]
// 0045cc8b  2b 56 34              SUB  EDX,[ESI+0x34]
// 0045cc8e  83 c0 30              ADD  EAX,0x30
// 0045cc91  8b 40 08              MOV  EAX,[EAX+0x8]
// 0045cc94  2b 46 38              SUB  EAX,[ESI+0x38]
// 0045cc97  89 4c 24 30           MOV  [ESP+0x30],ECX
// 0045cc9b  db 44 24 30           FILD dword [ESP+0x30]         ; first x87 push
// ...
// 0045cce6  8b 8f a4 00 00 00     MOV  ECX,[EDI+0xa4]
// 0045ccec  03 4c 24 20           ADD  ECX,[ESP+0x20]           ; R = sweep half-length + candidate radius
// 0045ccf0  3b c1                 CMP  EAX,ECX                  ; dist > R
// 0045ccf2  0f 8f 0f 01 00 00     JG   0x0045ce07
// ...
// 0045ce07  8b 3f                 MOV  EDI,[EDI]                ; the continue label
// 0045ce09  83 3f 00              CMP  dword [EDI],0
// 0045ce0c  89 7c 24 24           MOV  [ESP+0x24],EDI
// 0045ce10  0f 85 fa fd ff ff     JNE  0x0045cc10
constexpr std::uintptr_t p2_function_va = 0x0045cab0, p2_function_end_va = 0x0045cf57;
constexpr std::uintptr_t p2_site_va = 0x0045cc7c, p2_next_va = 0x0045cc82;
constexpr std::uintptr_t p2_compare_va = 0x0045cce6, p2_continue_va = 0x0045ce07;
constexpr std::uintptr_t p2_sqrt_call_va = 0x0045ccd9, p2_ftol_call_va = 0x0045cce1;
constexpr unsigned p2_compare_offset = 0x6a, p2_continue_offset = 0x18b;
constexpr unsigned p2_site_window_length = 35, p2_compare_window_length = 18, p2_continue_window_length = 9;
constexpr unsigned char p2_site_window[p2_site_window_length] = {
    0x8b, 0x47, 0x70, 0x8b, 0x48, 0x30, 0x8b, 0x73, 0x70, 0x2b, 0x4e, 0x30, 0x8b, 0x50, 0x34, 0x2b, 0x56, 0x34,
    0x83, 0xc0, 0x30, 0x8b, 0x40, 0x08, 0x2b, 0x46, 0x38, 0x89, 0x4c, 0x24, 0x30, 0xdb, 0x44, 0x24, 0x30};
constexpr unsigned char p2_compare_window[p2_compare_window_length] = {
    0x8b, 0x8f, 0xa4, 0x00, 0x00, 0x00, 0x03, 0x4c, 0x24, 0x20, 0x3b, 0xc1, 0x0f, 0x8f, 0x0f, 0x01, 0x00, 0x00};
constexpr unsigned char p2_continue_window[p2_continue_window_length] = {0x8b, 0x3f, 0x83, 0x3f, 0x00,
                                                                         0x89, 0x7c, 0x24, 0x24};
constexpr unsigned char p2_site[site_length] = {0x8b, 0x47, 0x70, 0x8b, 0x48, 0x30};
constexpr unsigned p2_sweep_slot = 0x20; // [ESP+0x20] at the site: the sweep half-length the engine adds at 0x0045ccec
// The two helpers both sites call (checked by the production install, not by
// the fixture, whose replicas sit at other addresses):
// 00412440  55 8b ec 83 e4 f8 d9 45 08 d9 fa 8b e5 5d c3        FLD [EBP+8]; FSQRT (result left on the x87 stack)
// 0052b5d0  83 3d ec 19 66 00 00 74 2d 55 8b ec 83 ec 08 83 e4 f8 dd 1c 24 f2 0f 2c 04 24 c9 c3
//           SSE2 path when *0x006619ec != 0: FSTP qword; CVTTSD2SI (truncation; 0x80000000 when out of range)
constexpr std::uintptr_t sqrt_helper_va = 0x00412440, ftol_helper_va = 0x0052b5d0;
constexpr unsigned sqrt_helper_length = 15, ftol_helper_length = 28;
constexpr unsigned char sqrt_helper[sqrt_helper_length] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0xd9, 0x45,
                                                           0x08, 0xd9, 0xfa, 0x8b, 0xe5, 0x5d, 0xc3};
constexpr unsigned char ftol_helper[ftol_helper_length] = {0x83, 0x3d, 0xec, 0x19, 0x66, 0x00, 0x00, 0x74, 0x2d, 0x55,
                                                           0x8b, 0xec, 0x83, 0xec, 0x08, 0x83, 0xe4, 0xf8, 0xdd, 0x1c,
                                                           0x24, 0xf2, 0x0f, 0x2c, 0x04, 0x24, 0xc9, 0xc3};
// Object layout the stubs read (every field dereferenced by the engine on the
// same pair before or right after the site): class word, physics block, radius.
constexpr unsigned class_offset = 0x48, physics_offset = 0x70, radius_offset = 0xa4, position_offset = 0x30;
constexpr std::uint16_t class_gate = 7; // class-7 pairs reuse the distance at 0x0045d6e4: never short-circuited
constexpr unsigned ret_pop = 4;
// The margin: T = R' + (R' >> 5) + 64 at P1 with R' = r (the engine's R is
// (r*0x1028f + 0x8000) >> 16 < 1.01 r + 0.5), T = R + 64 at P2. The engine's
// dist = trunc(sqrt(fl32(S))) with S >= m^2 (m = max|d|) is at least
// m(1 - 2^-25) - 1 - m*2^-51 (float32 unit roundoff halved by the square root,
// truncation, double intermediates), i.e. > m - 34 for m < 2^30; so
// m > T >= R + 35 implies dist > R. The cap keeps every |d| below 2^30:
// then S < 3*2^60 and dist < 2^31, so CVTTSD2SI cannot saturate to INT_MIN
// (which the engine compares as "not far"; a saturated pair is never rejected).
constexpr std::int32_t axis_cap = 0x40000000;
constexpr std::int32_t margin = 64;

// The stub's decision, integer for integer (host model of the emitted code).
// `wrapped` differences are the engine's own 32-bit subtractions.
inline std::int32_t wrapped_difference(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
inline bool box_reject(std::int32_t dx, std::int32_t dy, std::int32_t dz, std::int32_t threshold) {
    // |d| as the stub computes it (INT_MIN stays INT_MIN: unsigned max then the cap catches it).
    auto mag = [](std::int32_t d) {
        return d < 0 ? static_cast<std::uint32_t>(0u - static_cast<std::uint32_t>(d)) : static_cast<std::uint32_t>(d);
    };
    std::uint32_t m = mag(dx);
    if (mag(dy) > m) m = mag(dy);
    if (mag(dz) > m) m = mag(dz);
    if (m >= static_cast<std::uint32_t>(axis_cap)) return false;
    return static_cast<std::int32_t>(m) > threshold;
}
// P1 threshold from the engine's radius sum; false when the stub would not
// attempt a box (negative sum, or an overflowing T): the engine path runs.
inline bool p1_threshold(std::int32_t radius_sum, std::int32_t* threshold) {
    if (radius_sum < 0) return false;
    const std::int64_t t = std::int64_t(radius_sum) + (radius_sum >> 5) + margin;
    if (t > 0x7fffffff) return false;
    *threshold = static_cast<std::int32_t>(t);
    return true;
}
inline bool p1_box_reject(std::uint16_t class_a, std::uint16_t class_b, std::int32_t dx, std::int32_t dy,
                          std::int32_t dz, std::int32_t radius_sum) {
    std::int32_t t = 0;
    if (class_a == class_gate || class_b == class_gate || !p1_threshold(radius_sum, &t)) return false;
    return box_reject(dx, dy, dz, t);
}
// P2 threshold: candidate radius + sweep half-length as the engine adds them
// (32-bit), plus the margin; an overflow on either add leaves the engine path.
inline bool p2_threshold(std::int32_t candidate_radius, std::int32_t sweep, std::int32_t* threshold) {
    const std::int64_t r = std::int64_t(candidate_radius) + sweep;
    if (r > 0x7fffffff || r < -0x7fffffff - 1) return false;
    const std::int64_t t = r + margin;
    if (t > 0x7fffffff) return false;
    *threshold = static_cast<std::int32_t>(t);
    return true;
}
inline bool p2_box_reject(std::int32_t dx, std::int32_t dy, std::int32_t dz, std::int32_t candidate_radius,
                          std::int32_t sweep) {
    std::int32_t t = 0;
    if (!p2_threshold(candidate_radius, sweep, &t)) return false;
    return box_reject(dx, dy, dz, t);
}
// The engine's distance as decoded: three FILDs, products and sums on the x87
// (double intermediates under FEX_X87REDUCEDPRECISION; the 80-bit native
// intermediates differ by < 2^-52 relative, inside the margin), FSTP to
// float32, FLD + FSQRT, FSTP qword + CVTTSD2SI (truncation; 0x80000000 when
// the value is out of range).
inline std::int32_t engine_distance(std::int32_t dx, std::int32_t dy, std::int32_t dz) {
    const double sum = double(dx) * double(dx) + double(dy) * double(dy) + double(dz) * double(dz);
    const float rounded = static_cast<float>(sum);
    const double root = std::sqrt(static_cast<double>(rounded));
    if (!(root < 2147483648.0)) return static_cast<std::int32_t>(0x80000000u);
    return static_cast<std::int32_t>(root);
}
// R at P1: IMUL EDX (signed 64-bit product), ADD/ADC 0x8000, SHRD 16 (the low 32 bits of the shifted 64-bit value).
inline std::int32_t p1_engine_threshold(std::int32_t radius_sum) {
    const std::int64_t scaled = std::int64_t(radius_sum) * 0x1028f + 0x8000;
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(scaled) >> 16));
}
inline bool p1_engine_reject(std::int32_t dx, std::int32_t dy, std::int32_t dz, std::int32_t radius_sum) {
    return engine_distance(dx, dy, dz) > p1_engine_threshold(radius_sum);
}
inline bool p2_engine_reject(std::int32_t dx, std::int32_t dy, std::int32_t dz, std::int32_t candidate_radius,
                             std::int32_t sweep) {
    return engine_distance(dx, dy, dz) > wrapped_difference(candidate_radius, wrapped_difference(0, sweep));
}

// ---- stub encoders ----
// Both stubs are entered by the dispatcher's `jmp [entry]` with the site's exact
// register state and ESP (no return address), keep no state but the counters,
// call nothing, touch no x87 or SSE register, and leave EFLAGS dead as the
// site does (the first instruction after either site writes them). The
// counters are `inc dword [abs32]` on fixed slots, present only when the
// install asked for them (the fixture measures both variants).
struct Counters {
    std::uint32_t entered, rejected;
}; // absolute addresses, 0 = no counters
constexpr unsigned stub_capacity = 160;
class StubWriter {
public:
    unsigned length() const { return n_; }
    void byte(unsigned char b) {
        if (n_ < stub_capacity) out_[n_] = b;
        ++n_;
    }
    void bytes(const unsigned char* p, unsigned n) {
        for (unsigned i = 0; i < n; ++i) byte(p[i]);
    }
    void dword(std::uint32_t v) {
        byte(v & 0xff);
        byte((v >> 8) & 0xff);
        byte((v >> 16) & 0xff);
        byte((v >> 24) & 0xff);
    }
    // Short jump to a label resolved later (labels 0..3).
    void jcc(unsigned char opcode, unsigned label) {
        byte(opcode);
        fix_[fixes_].at = n_;
        fix_[fixes_].label = label;
        ++fixes_;
        byte(0);
    }
    void label(unsigned id) { label_[id] = n_; }
    bool resolve() {
        if (n_ > stub_capacity) return false;
        for (unsigned i = 0; i < fixes_; ++i) {
            const int rel = int(label_[fix_[i].label]) - int(fix_[i].at + 1);
            if (rel < -128 || rel > 127) return false;
            out_[fix_[i].at] = static_cast<unsigned char>(rel);
        }
        return true;
    }
    explicit StubWriter(unsigned char* out)
        : out_(out) {}

private:
    unsigned char* out_;
    unsigned n_ = 0;
    struct Fix {
        unsigned at, label;
    } fix_[16]{};
    unsigned fixes_ = 0;
    unsigned label_[4]{};
};
constexpr unsigned label_continue = 0, label_reject = 1, label_skip_a = 2, label_skip_b = 3;
inline void emit_inc(StubWriter& w, std::uint32_t slot) {
    if (slot) {
        w.byte(0xff);
        w.byte(0x05);
        w.dword(slot);
    }
}
// |reg| = reg < 0 ? -reg : reg on the wrapped difference: TEST; JNS +2; NEG.
inline void emit_abs(StubWriter& w, unsigned char test_modrm, unsigned char neg_modrm) {
    w.byte(0x85);
    w.byte(test_modrm);
    w.byte(0x79);
    w.byte(0x02);
    w.byte(0xf7);
    w.byte(neg_modrm);
}
// P1 stub. Free registers at the site: EAX/ECX/EDX (written by the displaced
// MOVs and 0x0045d594) and EDI (first touch on every path is a write: the
// reject label sets 7 at 0x0045d6d0, the loop head reloads it). EBX/ESI are
// the pair, EBP the frame; ESP is restored before either exit.
//   cmp byte [enabled],0 ; je continue
//   [inc dword [entered]]
//   cmp word [ebx+0x48],7 ; je continue ; cmp word [esi+0x48],7 ; je continue
//   mov eax,[esp+0x24] ; test eax,eax ; js continue ; mov edx,eax ; sar edx,5 ; add eax,edx ; jo continue ; add eax,64
//   ; jo continue mov ecx,[ebx+0x70] ; mov edx,[esi+0x70] mov edi,[ecx+0x30] ; sub edi,[edx+0x30] ; |edi| push eax mov
//   eax,[ecx+0x34] ; sub eax,[edx+0x34] ; |eax| ; cmp edi,eax ; cmovb edi,eax mov eax,[ecx+0x38] ; sub eax,[edx+0x38] ;
//   |eax| ; cmp edi,eax ; cmovb edi,eax pop eax cmp edi,0x40000000 ; jae continue ; cmp edi,eax ; jg reject
// continue: jmp [next]
// reject: [inc dword [rejected]] ; mov edi,7 ; jmp continue_label
inline unsigned encode_p1_stub(std::uint32_t at, std::uint32_t enabled, Counters counters, std::uint32_t next_slot,
                               std::uint32_t continue_target, unsigned char out[stub_capacity]) {
    StubWriter w(out);
    w.byte(0x80);
    w.byte(0x3d);
    w.dword(enabled);
    w.byte(0x00);
    w.jcc(0x74, label_continue);
    emit_inc(w, counters.entered);
    w.byte(0x66);
    w.byte(0x83);
    w.byte(0x7b);
    w.byte(0x48);
    w.byte(0x07);
    w.jcc(0x74, label_continue);
    w.byte(0x66);
    w.byte(0x83);
    w.byte(0x7e);
    w.byte(0x48);
    w.byte(0x07);
    w.jcc(0x74, label_continue);
    w.byte(0x8b);
    w.byte(0x44);
    w.byte(0x24);
    w.byte(0x24);
    w.byte(0x85);
    w.byte(0xc0);
    w.jcc(0x78, label_continue);
    w.byte(0x8b);
    w.byte(0xd0);
    w.byte(0xc1);
    w.byte(0xfa);
    w.byte(0x05);
    w.byte(0x03);
    w.byte(0xc2);
    w.jcc(0x70, label_continue);
    w.byte(0x83);
    w.byte(0xc0);
    w.byte(static_cast<unsigned char>(margin));
    w.jcc(0x70, label_continue);
    w.byte(0x8b);
    w.byte(0x4b);
    w.byte(0x70);
    w.byte(0x8b);
    w.byte(0x56);
    w.byte(0x70);
    w.byte(0x8b);
    w.byte(0x79);
    w.byte(0x30);
    w.byte(0x2b);
    w.byte(0x7a);
    w.byte(0x30);
    emit_abs(w, 0xff, 0xdf);
    w.byte(0x50);
    w.byte(0x8b);
    w.byte(0x41);
    w.byte(0x34);
    w.byte(0x2b);
    w.byte(0x42);
    w.byte(0x34);
    emit_abs(w, 0xc0, 0xd8);
    w.byte(0x3b);
    w.byte(0xf8);
    w.byte(0x0f);
    w.byte(0x42);
    w.byte(0xf8);
    w.byte(0x8b);
    w.byte(0x41);
    w.byte(0x38);
    w.byte(0x2b);
    w.byte(0x42);
    w.byte(0x38);
    emit_abs(w, 0xc0, 0xd8);
    w.byte(0x3b);
    w.byte(0xf8);
    w.byte(0x0f);
    w.byte(0x42);
    w.byte(0xf8);
    w.byte(0x58);
    w.byte(0x81);
    w.byte(0xff);
    w.dword(static_cast<std::uint32_t>(axis_cap));
    w.jcc(0x73, label_continue);
    w.byte(0x3b);
    w.byte(0xf8);
    w.jcc(0x7f, label_reject);
    w.label(label_continue);
    w.byte(0xff);
    w.byte(0x25);
    w.dword(next_slot);
    w.label(label_reject);
    emit_inc(w, counters.rejected);
    w.byte(0xbf);
    w.dword(class_gate);
    const std::uint32_t here = at + w.length() + 5;
    w.byte(0xe9);
    w.dword(continue_target - here);
    return w.resolve() ? w.length() : 0;
}
// P2 stub. Free registers at the site: EAX/ECX/EDX (written by the displaced
// MOVs and 0x0045cc88) and ESI (written at 0x0045cc82 before any read). EBX
// is the swept object, EDI the candidate (the continue label reads it).
//   cmp byte [enabled],0 ; je continue
//   [inc dword [entered]]
//   mov ecx,[edi+0xa4] ; add ecx,[esp+0x20] ; jo continue ; add ecx,64 ; jo continue
//   mov eax,[edi+0x70] ; mov edx,[ebx+0x70]
//   mov esi,[eax+0x30] ; sub esi,[edx+0x30] ; |esi|
//   push ecx
//   mov ecx,[eax+0x34] ; sub ecx,[edx+0x34] ; |ecx| ; cmp esi,ecx ; cmovb esi,ecx
//   mov ecx,[eax+0x38] ; sub ecx,[edx+0x38] ; |ecx| ; cmp esi,ecx ; cmovb esi,ecx
//   pop ecx
//   cmp esi,0x40000000 ; jae continue ; cmp esi,ecx ; jg reject
// continue: jmp [next]
// reject: [inc dword [rejected]] ; mov esi,edx (= [ebx+0x70], as 0x0045cc82 leaves it) ; jmp continue_label
inline unsigned encode_p2_stub(std::uint32_t at, std::uint32_t enabled, Counters counters, std::uint32_t next_slot,
                               std::uint32_t continue_target, unsigned char out[stub_capacity]) {
    StubWriter w(out);
    w.byte(0x80);
    w.byte(0x3d);
    w.dword(enabled);
    w.byte(0x00);
    w.jcc(0x74, label_continue);
    emit_inc(w, counters.entered);
    w.byte(0x8b);
    w.byte(0x8f);
    w.dword(radius_offset);
    w.byte(0x03);
    w.byte(0x4c);
    w.byte(0x24);
    w.byte(static_cast<unsigned char>(p2_sweep_slot));
    w.jcc(0x70, label_continue);
    w.byte(0x83);
    w.byte(0xc1);
    w.byte(static_cast<unsigned char>(margin));
    w.jcc(0x70, label_continue);
    w.byte(0x8b);
    w.byte(0x47);
    w.byte(0x70);
    w.byte(0x8b);
    w.byte(0x53);
    w.byte(0x70);
    w.byte(0x8b);
    w.byte(0x70);
    w.byte(0x30);
    w.byte(0x2b);
    w.byte(0x72);
    w.byte(0x30);
    emit_abs(w, 0xf6, 0xde);
    w.byte(0x51);
    w.byte(0x8b);
    w.byte(0x48);
    w.byte(0x34);
    w.byte(0x2b);
    w.byte(0x4a);
    w.byte(0x34);
    emit_abs(w, 0xc9, 0xd9);
    w.byte(0x3b);
    w.byte(0xf1);
    w.byte(0x0f);
    w.byte(0x42);
    w.byte(0xf1);
    w.byte(0x8b);
    w.byte(0x48);
    w.byte(0x38);
    w.byte(0x2b);
    w.byte(0x4a);
    w.byte(0x38);
    emit_abs(w, 0xc9, 0xd9);
    w.byte(0x3b);
    w.byte(0xf1);
    w.byte(0x0f);
    w.byte(0x42);
    w.byte(0xf1);
    w.byte(0x59);
    w.byte(0x81);
    w.byte(0xfe);
    w.dword(static_cast<std::uint32_t>(axis_cap));
    w.jcc(0x73, label_continue);
    w.byte(0x3b);
    w.byte(0xf1);
    w.jcc(0x7f, label_reject);
    w.label(label_continue);
    w.byte(0xff);
    w.byte(0x25);
    w.dword(next_slot);
    w.label(label_reject);
    emit_inc(w, counters.rejected);
    w.byte(0x8b);
    w.byte(0xf2);
    const std::uint32_t here = at + w.length() + 5;
    w.byte(0xe9);
    w.dword(continue_target - here);
    return w.resolve() ? w.length() : 0;
}
constexpr unsigned p1_stub_length_counted = 139, p1_stub_length_plain = 127;
constexpr unsigned p2_stub_length_counted = 117, p2_stub_length_plain = 105;

// ---- per-frame counters and the window ----
// The four counters are read and zeroed once per Present; the window keeps
// 300 frames and closes with the nearest-rank p50 and the max of each.
constexpr unsigned counter_count = 4; // p1 entered, p1 rejected, p2 entered, p2 rejected
inline constexpr const char* const counter_names[counter_count] = {"p1_pairs", "p1_rejected", "p2_cands",
                                                                   "p2_rejected"};
constexpr unsigned window_frames = 300;
struct WindowSummary {
    std::uint64_t frame = 0;
    unsigned frames = 0;
    std::uint64_t p50[counter_count]{}, max[counter_count]{}, sum[counter_count]{};
};
class Window {
public:
    unsigned count() const { return count_; }
    bool full() const { return count_ >= window_frames; }
    void add(std::uint64_t frame, const std::uint32_t values[counter_count]) {
        if (count_ < window_frames) {
            for (unsigned i = 0; i < counter_count; ++i) values_[i][count_] = values[i];
            ++count_;
        }
        for (unsigned i = 0; i < counter_count; ++i) {
            if (values[i] > max_[i]) max_[i] = values[i];
            sum_[i] += values[i];
        }
        last_frame_ = frame;
    }
    bool close(WindowSummary& out) {
        if (!count_) {
            reset();
            return false;
        }
        out.frame = last_frame_;
        out.frames = count_;
        for (unsigned i = 0; i < counter_count; ++i) {
            out.p50[i] = percentile(values_[i], 50);
            out.max[i] = max_[i];
            out.sum[i] = sum_[i];
        }
        reset();
        return true;
    }
    void reset() {
        count_ = 0;
        last_frame_ = 0;
        for (unsigned i = 0; i < counter_count; ++i) {
            max_[i] = 0;
            sum_[i] = 0;
        }
    }

private:
    // Nearest-rank percentile through an insertion into a scratch copy (300 entries, once per window).
    std::uint64_t percentile(const std::uint64_t* values, unsigned p) {
        for (unsigned i = 0; i < count_; ++i) {
            std::uint64_t v = values[i];
            unsigned j = i;
            while (j && scratch_[j - 1] > v) {
                scratch_[j] = scratch_[j - 1];
                --j;
            }
            scratch_[j] = v;
        }
        unsigned index = (count_ * p) / 100;
        if (index >= count_) index = count_ - 1;
        return scratch_[index];
    }
    std::uint64_t values_[counter_count][window_frames]{}, scratch_[window_frames]{}, max_[counter_count]{},
        sum_[counter_count]{};
    unsigned count_ = 0;
    std::uint64_t last_frame_ = 0;
};
}
