#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Portable core of the sector-collide narrow-phase census
// (docs/reverse-engineering/sector-collide.md sections 11.5 and 11.7): the
// verified byte windows of the three sites, the stub encoders, the per-pair
// ring entry, the transform hash, the cross-frame memo comparison and the
// row ordering. No Windows dependency so the host tests compile it directly.
//
// Site 5  0x0045d665  call 0x0048ac80   the narrow phase of one accepted pair (call redirect, bracketed)
// Site 6  0x0048a9a5  call 0x0047f1b0   one leaf-part mesh-pair test (call redirect, plain counter)
// Site 7  0x004e2530  mov eax,[0x0060854c]   entry of the recursive BVH node-pair routine (entry trampoline, plain
// counter) Site 8  0x004e2190  sub esp,0x34; push ebx; push edi   entry of the leaf triangle-triangle test (entry
// trampoline, plain counter; 12.6)
//
// 0045d65d  51                    PUSH ECX                      ; rB (stack argument 2)
// 0045d65e  8b 4b 70              MOV  ECX,[EBX+0x70]           ; physA: REGISTER argument (EBX = object A)
// 0045d661  50                    PUSH EAX                      ; rA (stack argument 1)
// 0045d662  8b 46 70              MOV  EAX,[ESI+0x70]           ; physB: REGISTER argument (ESI = object B)
// 0045d665  e8 16 d6 02 00        CALL 0x0048ac80               <- site 5
// 0045d66a  83 c4 08              ADD  ESP,8                    ; first flag writer after the call
// 0045d66d  85 c0                 TEST EAX,EAX
// 0045d66f  7e 5b                 JLE  0x0045d6cc               ; <= 0: no contact
//
// 0048ac80  51 / 33 d2 / 56 / 89 91 90 01 00 00 / 89 90 90 01 00 00 ...   PUSH ECX; XOR EDX,EDX (first flag
//           writer); zeroes [ECX+0x180..0x190] and [EAX+0x180..0x190]; CALL 0x0048a890; ADD ESP,0x18; POP EDI/ESI/ECX;
//           RET
//
// 0048a993  d9 ee / 51 / d9 1c 24         FLDZ; PUSH ECX; FSTP dword [ESP]     ; tolerance 0.0f (x87 net zero)
// 0048a999  8b ce / 6a 01 / 6a 02 / 50 / 57 / 33 ff / 8b c3                    ; ECX, EAX, EDI are register arguments
// 0048a9a5  e8 06 48 ff ff        CALL 0x0047f1b0               <- site 6
// 0048a9aa  83 c4 14 / 85 c0 / 0f 84 91 00 00 00                ADD ESP,0x14; TEST; JE 0x0048aa46
//
// 004e2530  a1 4c 85 60 00        MOV  EAX,[0x0060854c]         <- site 7 (function entry; five inbound calls, all to
// the entry) 004e2535  83 ec 40              SUB  ESP,0x40                 ; first flag writer 004e2538  83 3d 34 69 59
// 00 00  CMP  dword [0x00596934],0 004e253f  53 55 56 57           PUSH EBX/EBP/ESI/EDI 004e2543  74 0e / 85 c0 / 7e 0a
// / 33 c0 / 5f 5e 5d 5b / 83 c4 40 / c3
//
// 004e2190  83 ec 34              SUB  ESP,0x34                 <- site 8 (function entry; sole caller 0x004e25cd,
// ESI/EAX register arguments) 004e2193  53 / 57               PUSH EBX; PUSH EDI 004e2195  8b f8 / 8b 46 44 / d9 40 04
// MOV EDI,EAX; MOV EAX,[ESI+0x44]; FLD dword [EAX+4]   ; reads no flag
namespace x3m::collide_narrow_census::core {
constexpr std::uintptr_t n5_site_va = 0x0045d665, n5_target_va = 0x0048ac80, n5_return_va = 0x0045d66a;
constexpr std::uintptr_t n6_site_va = 0x0048a9a5, n6_target_va = 0x0047f1b0;
constexpr std::uintptr_t n7_site_va = 0x004e2530, n7_next_va = 0x004e2535, n7_hits_va = 0x0060854c,
                         n7_mode_va = 0x00596934;
constexpr std::uintptr_t n8_site_va = 0x004e2190, n8_next_va = 0x004e2195, n8_caller_va = 0x004e25cd;
constexpr std::uintptr_t n5_function_va = 0x0045d250, n5_function_end_va = 0x0045e0b8;
constexpr std::uintptr_t n6_function_va = 0x0048a890, n6_function_end_va = 0x0048ac27;
constexpr std::uintptr_t n7_function_va = 0x004e2530, n7_function_end_va = 0x004e2780;
constexpr unsigned call_length = 5;
// Windows around the two call sites, at offsets relative to the site; the rel32 itself is checked as a target.
constexpr unsigned n5_pre_length = 8, n5_post_length = 7, n6_pre_length = 18, n6_post_length = 11;
constexpr unsigned char n5_pre_window[n5_pre_length] = {0x51, 0x8b, 0x4b, 0x70, 0x50, 0x8b, 0x46, 0x70};
constexpr unsigned char n5_post_window[n5_post_length] = {0x83, 0xc4, 0x08, 0x85, 0xc0, 0x7e, 0x5b};
constexpr unsigned char n6_pre_window[n6_pre_length] = {0xd9, 0xee, 0x51, 0xd9, 0x1c, 0x24, 0x8b, 0xce, 0x6a,
                                                        0x01, 0x6a, 0x02, 0x50, 0x57, 0x33, 0xff, 0x8b, 0xc3};
constexpr unsigned char n6_post_window[n6_post_length] = {0x83, 0xc4, 0x14, 0x85, 0xc0, 0x0f,
                                                          0x84, 0x91, 0x00, 0x00, 0x00};
// Site 7: the first 35 bytes of 0x004e2530 with the two abs32 operands (hits at +1, mode flag at +10) supplied by the
// caller.
constexpr unsigned n7_window_length = 35, n7_hits_operand = 1, n7_mode_operand = 10;
constexpr unsigned char n7_window[n7_window_length] = {
    0xa1, 0x4c, 0x85, 0x60, 0x00, 0x83, 0xec, 0x40, 0x83, 0x3d, 0x34, 0x69, 0x59, 0x00, 0x00, 0x53, 0x55, 0x56,
    0x57, 0x74, 0x0e, 0x85, 0xc0, 0x7e, 0x0a, 0x33, 0xc0, 0x5f, 0x5e, 0x5d, 0x5b, 0x83, 0xc4, 0x40, 0xc3};
inline void n7_expected(std::uint32_t hits, std::uint32_t mode, unsigned char out[n7_window_length]) {
    std::memcpy(out, n7_window, n7_window_length);
    std::memcpy(out + n7_hits_operand, &hits, 4);
    std::memcpy(out + n7_mode_operand, &mode, 4);
}
// Site 8: the first 13 bytes of 0x004e2190 (no relocated operand); the first five are the three displaced instructions.
constexpr unsigned n8_window_length = 13;
constexpr unsigned char n8_window[n8_window_length] = {0x83, 0xec, 0x34, 0x53, 0x57, 0x8b, 0xf8,
                                                       0x8b, 0x46, 0x44, 0xd9, 0x40, 0x04};
// The narrow-phase entry the site-5 stub calls (checked by the production
// install on the real image; the fixture carries a byte-exact replica): its
// register convention (ECX = physA, EAX = physB) is what the pre-window loads.
constexpr unsigned n5_callee_length = 103, n5_callee_rel32 = 0x5c;
// clang-format off
constexpr unsigned char n5_callee[n5_callee_length] = {
    0x51, 0x33,0xd2, 0x56, 0x89,0x91,0x90,0x01,0x00,0x00, 0x89,0x90,0x90,0x01,0x00,0x00, 0x57, 0x8d,0xb1,0x90,0x01,0x00,0x00,
    0x89,0x91,0x80,0x01,0x00,0x00, 0x89,0x91,0x84,0x01,0x00,0x00, 0x89,0x91,0x88,0x01,0x00,0x00, 0x89,0x91,0x8c,0x01,0x00,0x00,
    0x8d,0xb8,0x90,0x01,0x00,0x00, 0x57, 0x89,0x90,0x80,0x01,0x00,0x00, 0x89,0x90,0x84,0x01,0x00,0x00, 0x89,0x90,0x88,0x01,0x00,0x00,
    0x89,0x90,0x8c,0x01,0x00,0x00, 0x8b,0x54,0x24,0x18, 0x56, 0x52, 0x8b,0x54,0x24,0x1c, 0x52, 0x50, 0x51, 0xe8,0xb0,0xfb,0xff,0xff,
    0x83,0xc4,0x18, 0x5f, 0x5e, 0x59, 0xc3};
// clang-format on
constexpr unsigned n6_callee_length = 22;
constexpr unsigned char n6_callee[n6_callee_length] = {0x8b, 0x54, 0x24, 0x04, 0x8b, 0x52, 0x5c, 0x83,
                                                       0xec, 0x64, 0x85, 0xd2, 0x56, 0x8b, 0x74, 0x24,
                                                       0x70, 0x8b, 0x76, 0x5c, 0x75, 0x07};
// Object and physics-block (= scene node) fields the census records; every one
// is inside a block the engine dereferences on the same pair (the note's 1 and
// 11.3; [phys+0x190] is written by 0x0048ac80 itself, so 0x194 bytes are mapped).
constexpr unsigned flags40_offset = 0x40, flags44_offset = 0x44, class_offset = 0x48, subtype_offset = 0x4a,
                   physics_offset = 0x70, radius_offset = 0xa4;
constexpr unsigned position_offset = 0x30, saved_offset = 0xb0, saved_words = 4, xform_offset = 0xc0, xform_words = 11,
                   node_flags_offset = 0x12c, model_offset = 0x140;

struct ObjectKey {
    std::uint32_t object, physics;
    std::uint16_t cls, subtype;
    std::uint32_t flags40, flags44;
    std::int32_t radius, pos[3];
    std::uint32_t model, node_flags, xform_hash, saved_hash;
};
enum : std::uint8_t {
    had_previous = 1,
    same_position = 2,
    same_xform = 4,
    same_saved = 8,
    unchanged = 16,
    memo_hit = 32,
    memo_unsafe = 64,
    visits_differ = 128
};
struct Entry {
    ObjectKey a, b;
    std::int32_t result;
    std::uint32_t visits, mesh_pairs, ticks, tri_tests;
    std::uint8_t flags;
};
inline std::uint32_t load32(const unsigned char* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline std::uint32_t hash_words(const unsigned char* p, unsigned words, std::uint32_t h) {
    for (unsigned i = 0; i < words; ++i) {
        h = (h ^ load32(p + 4 * i)) * 0x01000193u;
        h ^= h >> 15;
    }
    return h;
}
// `object` and `physics` are the two blocks' bytes; the addresses are recorded as given.
inline ObjectKey read_key(std::uint32_t object_va, const unsigned char* object, std::uint32_t physics_va,
                          const unsigned char* physics) {
    ObjectKey k{};
    k.object = object_va;
    k.physics = physics_va;
    std::memcpy(&k.cls, object + class_offset, 2);
    std::memcpy(&k.subtype, object + subtype_offset, 2);
    k.flags40 = load32(object + flags40_offset);
    k.flags44 = load32(object + flags44_offset);
    k.radius = static_cast<std::int32_t>(load32(object + radius_offset));
    for (unsigned i = 0; i < 3; ++i) k.pos[i] = static_cast<std::int32_t>(load32(physics + position_offset + 4 * i));
    k.model = load32(physics + model_offset);
    k.node_flags = load32(physics + node_flags_offset);
    // The memo key of 11.6: the node matrix words, the model id and the node flags.
    std::uint32_t h = hash_words(physics + xform_offset, xform_words, 0x811c9dc5u);
    h = hash_words(physics + model_offset, 1, h);
    k.xform_hash = hash_words(physics + node_flags_offset, 1, h);
    // The saved position 0x0047f1b0 uses as the translation: hashed apart so a changing one is visible on its own.
    k.saved_hash = hash_words(physics + saved_offset, saved_words, 0x811c9dc5u);
    return k;
}
inline std::uint8_t compare_key(const ObjectKey& now, const ObjectKey& then) {
    std::uint8_t f = 0;
    if (now.pos[0] == then.pos[0] && now.pos[1] == then.pos[1] && now.pos[2] == then.pos[2]) f |= same_position;
    if (now.xform_hash == then.xform_hash && now.model == then.model && now.node_flags == then.node_flags)
        f |= same_xform;
    if (now.saved_hash == then.saved_hash) f |= same_saved;
    return f;
}
struct MemoSummary {
    std::uint32_t with_previous = 0, unchanged = 0, memo_hits = 0, memo_hit_visits = 0, memo_unsafe = 0,
                  visits_differ = 0, changed_position = 0, changed_xform = 0, changed_saved = 0;
};
// Annotates this frame's entries against the previous frame's. The pair loop is
// deterministic, so the previous entry of a pair is normally at the cursor:
// the search starts there and wraps (O(n) typical, O(n*m) worst, n,m <= 256).
inline MemoSummary annotate(Entry* now, unsigned count, const Entry* then, unsigned then_count) {
    MemoSummary s;
    unsigned cursor = 0;
    for (unsigned i = 0; i < count; ++i) {
        Entry& e = now[i];
        e.flags = 0;
        for (unsigned step = 0; step < then_count; ++step) {
            unsigned j = cursor + step;
            if (j >= then_count) j -= then_count;
            const Entry& p = then[j];
            if (p.a.object != e.a.object || p.b.object != e.b.object || p.a.physics != e.a.physics ||
                p.b.physics != e.b.physics)
                continue;
            cursor = j + 1 < then_count ? j + 1 : 0;
            const std::uint8_t both = compare_key(e.a, p.a) & compare_key(e.b, p.b);
            e.flags = static_cast<std::uint8_t>(had_previous | both);
            ++s.with_previous;
            if (!(both & same_position)) ++s.changed_position;
            if (!(both & same_xform)) ++s.changed_xform;
            if (!(both & same_saved)) ++s.changed_saved;
            if (both == (same_position | same_xform | same_saved)) {
                e.flags |= unchanged;
                ++s.unchanged;
                if (p.result <= 0) { // the memo would have answered "no contact"
                    if (e.result <= 0) {
                        e.flags |= memo_hit;
                        ++s.memo_hits;
                        s.memo_hit_visits += e.visits;
                        if (e.visits != p.visits) {
                            e.flags |= visits_differ;
                            ++s.visits_differ;
                        }
                    } else {
                        e.flags |= memo_unsafe;
                        ++s.memo_unsafe;
                    } // same key, the engine now reports a contact: the key misses an input
                }
            }
            break;
        }
    }
    return s;
}
constexpr unsigned ring_capacity = 256;
// Row order of the capture-frame listing: node-pair visits descending, stable.
inline void order_by_visits(const Entry* entries, unsigned count, std::uint16_t* order) {
    for (unsigned i = 0; i < count; ++i) {
        unsigned j = i;
        while (j && entries[order[j - 1]].visits < entries[i].visits) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = static_cast<std::uint16_t>(i);
    }
}

// ---- stub encoders ----
constexpr unsigned stub_capacity = 320;
class StubWriter {
public:
    explicit StubWriter(unsigned char* out, std::uint32_t at)
        : out_(out)
        , at_(at) {}
    unsigned length() const { return n_; }
    bool ok() const { return n_ <= stub_capacity; }
    void byte(unsigned char b) {
        if (n_ < stub_capacity) out_[n_] = b;
        ++n_;
    }
    void raw(std::initializer_list<unsigned char> list) {
        for (unsigned char b : list) byte(b);
    }
    void dword(std::uint32_t v) {
        byte(v & 0xff);
        byte((v >> 8) & 0xff);
        byte((v >> 16) & 0xff);
        byte((v >> 24) & 0xff);
    }
    void rel32(unsigned char opcode, std::uint32_t target) {
        byte(opcode);
        dword(target - (at_ + n_ + 4));
    }
    void patch32(unsigned at, std::uint32_t v) {
        for (unsigned i = 0; i < 4; ++i)
            if (at + i < stub_capacity) out_[at + i] = static_cast<unsigned char>(v >> (8 * i));
    }

private:
    unsigned char* out_;
    std::uint32_t at_;
    unsigned n_ = 0;
};
// pushfd; pushad; cld; sub esp,0x80; movups [esp+16i],xmm_i  /  the reverse.
inline void emit_save(StubWriter& w) {
    w.raw({0x9c, 0x60, 0xfc, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00});
    for (unsigned i = 0; i < 8; ++i)
        w.raw({0x0f, 0x11, static_cast<unsigned char>(0x44 | (i << 3)), 0x24, static_cast<unsigned char>(i * 16)});
}
inline void emit_restore(StubWriter& w) {
    for (unsigned i = 0; i < 8; ++i)
        w.raw({0x0f, 0x10, static_cast<unsigned char>(0x44 | (i << 3)), 0x24, static_cast<unsigned char>(i * 16)});
    w.raw({0x81, 0xc4, 0x80, 0x00, 0x00, 0x00, 0x61, 0x9d});
}
struct N5Operands {
    std::uint32_t return_va, target, pre_handler, post_handler, busy, nested, foreign;
};
// Site-5 stub: the redirected `call` lands here with the engine's return
// address on the stack and the callee's register arguments (ECX, EAX) live.
//   cmp dword [esp],return_va ; jne near foreign          (only the patched call site may be bracketed)
//   cmp byte [busy],0 ; jne nested                   (a re-entered narrow phase passes through uncounted)
//   mov byte [busy],1
//   <save> ; push esi ; push ebx ; call pre ; add esp,8 ; <restore>
//   lea esp,[esp+4]                                  (drops the engine's return address without touching EFLAGS)
//   call target                                      (pushes ours into the same slot: the callee sees the engine's
//   exact stack) <save> ; push eax ; call post ; add esp,4 ; <restore> mov byte [busy],0 ; jmp return_va (ESP as after
//   the callee's `ret`; every register, EFLAGS, x87 as the callee left them)
// nested:  inc dword [nested]  ; jmp target
// foreign: inc dword [foreign] ; jmp target
// <save>/<restore> keep EFLAGS, the eight integer registers and XMM0-7; the
// handlers execute no x87/MMX instruction (check_no_x87.py) and keep MXCSR
// and LastError (LightCallBoundary). EFLAGS are dead at the callee's entry
// (its first flag instruction is `xor edx,edx`), which the two `cmp`s rely on.
inline unsigned encode_n5_stub(std::uint32_t at, const N5Operands& o, unsigned char out[stub_capacity]) {
    StubWriter w(out, at);
    w.raw({0x81, 0x3c, 0x24});
    w.dword(o.return_va);
    w.raw({0x0f, 0x85});
    const unsigned fix_foreign = w.length();
    w.dword(0);
    w.raw({0x80, 0x3d});
    w.dword(o.busy);
    w.raw({0x00, 0x0f, 0x85});
    const unsigned fix_nested = w.length();
    w.dword(0);
    w.raw({0xc6, 0x05});
    w.dword(o.busy);
    w.byte(0x01);
    emit_save(w);
    w.raw({0x56, 0x53});
    w.rel32(0xe8, o.pre_handler);
    w.raw({0x83, 0xc4, 0x08});
    emit_restore(w);
    w.raw({0x8d, 0x64, 0x24, 0x04});
    w.rel32(0xe8, o.target);
    emit_save(w);
    w.byte(0x50);
    w.rel32(0xe8, o.post_handler);
    w.raw({0x83, 0xc4, 0x04});
    emit_restore(w);
    w.raw({0xc6, 0x05});
    w.dword(o.busy);
    w.byte(0x00);
    w.rel32(0xe9, o.return_va);
    const unsigned nested = w.length();
    w.raw({0xff, 0x05});
    w.dword(o.nested);
    w.rel32(0xe9, o.target);
    const unsigned foreign = w.length();
    w.raw({0xff, 0x05});
    w.dword(o.foreign);
    w.rel32(0xe9, o.target);
    if (!w.ok()) return 0;
    w.patch32(fix_nested, nested - (fix_nested + 4));
    w.patch32(fix_foreign, foreign - (fix_foreign + 4));
    return w.length();
}
// Site-6 stub: inc dword [counter] ; jmp target (the redirected call's callee; registers, stack and x87 untouched).
inline unsigned encode_n6_stub(std::uint32_t at, std::uint32_t counter, std::uint32_t target,
                               unsigned char out[stub_capacity]) {
    StubWriter w(out, at);
    w.raw({0xff, 0x05});
    w.dword(counter);
    w.rel32(0xe9, target);
    return w.length();
}
// Site-7 stub: inc dword [counter] ; mov eax,[hits] (the displaced instruction) ; jmp site+5. No clock, no call.
inline unsigned encode_n7_stub(std::uint32_t at, std::uint32_t counter, std::uint32_t hits, std::uint32_t next,
                               unsigned char out[stub_capacity]) {
    StubWriter w(out, at);
    w.raw({0xff, 0x05});
    w.dword(counter);
    w.byte(0xa1);
    w.dword(hits);
    w.rel32(0xe9, next);
    return w.length();
}
// Site-8 stub: inc dword [counter] ; sub esp,0x34 ; push ebx ; push edi (the three displaced instructions) ; jmp
// site+5. The inc's flags are overwritten by the re-executed `sub`, exactly the flags the engine's own `sub` leaves.
inline unsigned encode_n8_stub(std::uint32_t at, std::uint32_t counter, std::uint32_t next,
                               unsigned char out[stub_capacity]) {
    StubWriter w(out, at);
    w.raw({0xff, 0x05});
    w.dword(counter);
    w.raw({0x83, 0xec, 0x34, 0x53, 0x57});
    w.rel32(0xe9, next);
    return w.length();
}
constexpr unsigned n5_stub_length = 289, n6_stub_length = 11, n7_stub_length = 16, n8_stub_length = 16;

// ---- per-frame series of the 300-frame window (p50/max/sum each) ----
constexpr unsigned series_count = 4; // accepted pairs, mesh-pair tests, BVH node-pair visits, narrow-phase microseconds
inline constexpr const char* const series_names[series_count] = {"accepted", "mesh_pairs", "node_pairs", "narrow_us"};
}
