#pragma once
// Engine-side occlusion skip (X3M_OCCLUSION_CULL=engine; docs/architecture/occlusion-cull.md "Engine-side skip";
// docs/reverse-engineering/engine-side-occlusion-cull.md): the pure parts of the duty-cycle probe. A third stub on the
// shared cull_small_parts claim of the cull/LOD pass 0x0047cfe0 at 0x0047d2a2 (cull_small_parts_core.h holds the
// window, the site and the liveness facts) sends a ship part node down the engine's own size-cull instruction
// 0x0047d2c3 (renderable bit cleared for this view and frame, LOD selection and the whole render visit skipped,
// children still visited) when a table published by the proxy at the sector view's Clear lists the node. The proxy
// lists a node for frame N only when its draw-level cull (occlusion_cull_core.h) skipped every draw of that node in
// frame N-1; the node then draws nothing in N, is listed by nobody for N+1 (no draws in N), goes through the draw path
// in N+1 (where the previous block's result decides as before) and is listed again for N+2: every hidden node
// alternates proxy-skip and engine-skip, the proxy's listing and testing run unchanged. Reveal latency: one frame when
// the reveal lands on an engine-skip frame, two when it lands on a proxy-skip frame (that frame's skip rests on a test
// issued before the hull changed and the next frame's table is built from it), three with an age-2 result; the
// draw-level cull alone is one (two with age 2). The table is a fixed open-addressed array keyed on the node pointer
// (the pass's EDI is the draw path's scope node, measured 100 % in the note), read by the stub with a bounded linear
// probe; every entry carries guards: the model id (+0x140, dereferenced by the pass itself before the site: a freed
// address reused by another body is not skipped), the publish frame stamp (an entry from another frame is not
// skipped) and a window around the node's camera-space position +0xf0..+0xf8: integer fixed point, the node's +0x30
// origin relative to the sector camera through its /65536 basis rows (chase-lead-reticle.md "the +0xf0 vector",
// writers 0x00420ec8/0x00420f60/0x00420ff8; the per-view transform walk 0x0047b800 rewrites it before the pass), so
// the window is relative: +-1/256 of the largest |component|, floor 1 unit, signed compares (about 10 px at the
// user's 2560 px focal length for a part on the view axis; a camera cut, teleport or fast turn leaves the window and
// the node is kept). A node is withheld from the table once every `retest` frames on its own phase
// (core::retest_phase, the visible-part re-test's stagger); it then takes the draw path, where the draw-level verdict
// still applies.
//
// Single-thread assumption: the Clear that publishes, the pass that reads and the Present that reads the counters and
// disarms all run on the engine's render thread (the Clear hook runs inside the engine's Clear call at 0x00472260, the
// pass 0x0047cfe0 follows it on the same thread through the walker 0x0047e780, Present ends the frame on it), so no
// lock is needed and no reader can see a half-written entry. Pure: no D3D, no Windows, no allocation; the stub is
// emitted from encode_stub() by occlusion_engine_cull.cpp and proven byte for byte by the host test.
#include "occlusion_cull_core.h"
#include "cull_small_parts_core.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace x3m::occlusion_cull::engine {
enum class Mode : std::uint8_t { off = 0, on = 1, engine = 2 };
// `on` (also unset or empty), `off`, or `engine` (the draw-level cull plus the engine-side skip); anything else refused.
inline bool parse_mode(const char* text, Mode* mode) {
    if (!text || !*text || !std::strcmp(text, "on")) {
        *mode = Mode::on;
        return true;
    }
    if (!std::strcmp(text, "off")) {
        *mode = Mode::off;
        return true;
    }
    if (!std::strcmp(text, "engine")) {
        *mode = Mode::engine;
        return true;
    }
    return false;
}
inline const char* mode_name(Mode m) {
    return m == Mode::engine ? "engine" : m == Mode::on ? "on" : "off";
}

// Node fields the stub reads (besides the replayed span's): the view-space position (three words) and the model id.
constexpr unsigned position_offset = 0xf0, model_offset = core::model_offset;
static_assert(model_offset == 0x140, "the pass's own model read at 0x0047d2ec");
// The table: table_slots home slots plus table_probe spare entries at the end so a linear probe never wraps (no mask
// in the stub); 48-byte entries, multiplicative hash of the node into the top 9 bits.
constexpr unsigned table_slots = 512, table_probe = 4, table_entries = table_slots + table_probe, entry_bytes = 48;
constexpr unsigned hash_shift = 23; // 32 - log2(table_slots)
constexpr std::uint32_t hash_multiplier = 2654435761u;
static_assert((1u << (32 - hash_shift)) == table_slots, "the hash yields a home slot");
// The window: 1/256 of the largest |component| of the position, at least window_floor units (1/64 was ~40 px at a
// 2560 px focal length; review 2026-10-08).
constexpr unsigned window_shift = 8;
constexpr std::int32_t window_floor = 1;
struct Entry {
    std::uint32_t node, model, stamp;
    std::int32_t lo_x, hi_x, lo_y, hi_y, lo_z, hi_z;
    std::uint32_t draws, reserved0, reserved1;
};
static_assert(sizeof(Entry) == entry_bytes, "the stub's entry stride");
static_assert(offsetof(Entry, model) == 4 && offsetof(Entry, stamp) == 8 && offsetof(Entry, lo_x) == 12 &&
                  offsetof(Entry, hi_z) == 32 && offsetof(Entry, draws) == 36,
              "the stub's entry layout");
inline std::uint32_t hash_slot(std::uint32_t node) {
    return (node * hash_multiplier) >> hash_shift;
}
inline std::int32_t clamp32(std::int64_t v) {
    return v > 2147483647LL ? 2147483647 : v < -2147483648LL ? (-2147483647 - 1) : std::int32_t(v);
}
// The guard window of a position; false when all three words are 0 (the fields are not live: never published).
inline bool window(const std::int32_t pos[3], std::int32_t lo[3], std::int32_t hi[3]) {
    std::int64_t magnitude = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const std::int64_t a = pos[i] < 0 ? -std::int64_t(pos[i]) : std::int64_t(pos[i]);
        magnitude = a > magnitude ? a : magnitude;
    }
    if (!magnitude) return false;
    std::int64_t tolerance = magnitude >> window_shift;
    if (tolerance < window_floor) tolerance = window_floor;
    for (unsigned i = 0; i < 3; ++i) {
        lo[i] = clamp32(std::int64_t(pos[i]) - tolerance);
        hi[i] = clamp32(std::int64_t(pos[i]) + tolerance);
    }
    return true;
}

// window() into an entry: the entry interleaves lo/hi per axis (the stub compares them in that order).
inline bool window_into(const std::int32_t pos[3], Entry* e) {
    std::int32_t lo[3], hi[3];
    if (!window(pos, lo, hi)) return false;
    e->lo_x = lo[0];
    e->hi_x = hi[0];
    e->lo_y = lo[1];
    e->hi_y = hi[1];
    e->lo_z = lo[2];
    e->hi_z = hi[2];
    return true;
}

// The per-frame node ledger the draw path fills: every scene draw of a node counts, a draw the proxy skipped counts
// again, the node's model id and view-space position are taken at its first skipped draw. Fixed array, stamped per
// frame (nothing is cleared), touched[] lists this frame's slots for the publish.
constexpr unsigned ledger_slots = 1024, ledger_probe = 8;
constexpr unsigned ledger_hash_shift = 22; // 32 - log2(ledger_slots): the whole ledger is used
static_assert((1u << (32 - ledger_hash_shift)) == ledger_slots, "the ledger hash yields a home slot");
inline std::uint32_t ledger_slot(std::uint32_t node) {
    return (node * hash_multiplier) >> ledger_hash_shift;
}
struct Ledger {
    struct Slot {
        std::uint32_t node, stamp, model, draws, skipped;
        std::int32_t pos[3];
        bool pos_ok, pos_read;
    };
    Slot slots[ledger_slots]{};
    std::uint16_t touched[ledger_slots]{};
    unsigned touched_n = 0, dropped = 0; // dropped: draws of a node the probe window could not seat (never published)
    std::uint32_t frame = 0;
    std::uintptr_t view = 0; // the frame's camera pointer (the sector view), taken once per frame by the caller
    void begin(std::uint32_t f, std::uintptr_t camera) {
        frame = f;
        view = camera;
        touched_n = 0;
        dropped = 0;
    }
    // The node's slot this frame (created on first use), or null when its probe window is full.
    Slot* draw(std::uint32_t node) {
        if (!node) return nullptr;
        unsigned at = ledger_slot(node);
        for (unsigned probe = 0; probe < ledger_probe; ++probe, at = (at + 1) & (ledger_slots - 1)) {
            Slot& s = slots[at];
            if (s.stamp == frame && s.node == node) {
                ++s.draws;
                return &s;
            }
            if (s.stamp != frame) {
                s = Slot{};
                s.node = node;
                s.stamp = frame;
                s.draws = 1;
                touched[touched_n++] = std::uint16_t(at);
                return &s;
            }
        }
        ++dropped;
        return nullptr;
    }
    // A draw of the slot's node the proxy skipped; read(address, out, size) reads the node's position once per frame.
    template <class Read> void skipped(Slot* s, std::uint32_t model, Read& read) {
        if (!s) return;
        ++s->skipped;
        s->model = model;
        if (!s->pos_read) {
            s->pos_read = true;
            s->pos_ok = read(std::uintptr_t(s->node) + position_offset, s->pos, sizeof s->pos);
        }
    }
};

struct PublishStats {
    unsigned candidates = 0;  // nodes with draws
    unsigned published = 0;   // entries in the table
    unsigned withheld = 0;    // fully skipped nodes withheld on their phase frame (the draw-level verdict applies)
    unsigned partial = 0;     // nodes with some but not all draws skipped
    unsigned no_position = 0; // fully skipped nodes whose position could not be read or is all zero
    unsigned overflow = 0;    // fully skipped nodes the table's probe window could not seat
};
// The stub's verdicts, mirrored for the host test and the fixture (lookup() is the stub's logic in C++).
enum class Verdict : std::uint8_t { miss = 0, skip, model, stamp, position };
struct Table {
    Entry entries[table_entries]{}; // first: the stub's table address is the Table's
    std::uint16_t used[table_entries]{};
    unsigned used_n = 0;
    void clear() {
        for (unsigned i = 0; i < used_n; ++i) entries[used[i]] = Entry{};
        used_n = 0;
    }
    // Rebuilds the table from the ledger of the previous frame: a node whose every draw the proxy skipped, whose
    // position is live and whose phase is not this frame's. `stamp` is this frame's stamp (the stub compares it).
    unsigned publish(const Ledger& l, std::uint32_t stamp, unsigned retest, PublishStats* st) {
        clear();
        for (unsigned t = 0; t < l.touched_n; ++t) {
            const Ledger::Slot& s = l.slots[l.touched[t]];
            if (s.stamp != l.frame || !s.draws) continue;
            ++st->candidates;
            if (!s.skipped) continue; // drawn: never listed
            if (s.skipped != s.draws) {
                ++st->partial;
                continue;
            }
            Entry e{};
            if (!s.pos_ok || !window_into(s.pos, &e)) {
                ++st->no_position;
                continue;
            }
            if (retest > 1 && stamp % retest == core::retest_phase(s.node, s.model, retest)) {
                ++st->withheld;
                continue;
            }
            e.node = s.node;
            e.model = s.model;
            e.stamp = stamp;
            e.draws = s.draws;
            unsigned at = hash_slot(s.node);
            bool seated = false;
            for (unsigned probe = 0; probe < table_probe && !seated; ++probe, ++at) {
                if (entries[at].node) continue;
                entries[at] = e;
                used[used_n++] = std::uint16_t(at);
                seated = true;
            }
            if (seated)
                ++st->published;
            else
                ++st->overflow;
        }
        return st->published;
    }
    Verdict lookup(std::uint32_t node, std::uint32_t model, const std::int32_t pos[3], std::uint32_t stamp) const {
        unsigned at = hash_slot(node);
        for (unsigned probe = 0; probe < table_probe; ++probe, ++at) {
            const Entry& e = entries[at];
            if (e.node == node) {
                if (e.model != model) return Verdict::model;
                if (e.stamp != stamp) return Verdict::stamp;
                if (pos[0] < e.lo_x || pos[0] > e.hi_x || pos[1] < e.lo_y || pos[1] > e.hi_y || pos[2] < e.lo_z ||
                    pos[2] > e.hi_z)
                    return Verdict::position;
                return Verdict::skip;
            }
            if (!e.node) return Verdict::miss;
        }
        return Verdict::miss;
    }
    const Entry* find(std::uint32_t node) const {
        unsigned at = hash_slot(node);
        for (unsigned probe = 0; probe < table_probe; ++probe, ++at) {
            if (entries[at].node == node) return &entries[at];
            if (!entries[at].node) return nullptr;
        }
        return nullptr;
    }
};
// The stub (285 bytes), entered by the dispatcher's `jmp [entry]` (or the previous head's `jmp [next]`) with the
// site's exact register state and ESP (no return address; [ESP+0x28] = the pass's view argument, EDI = the node):
//    0  83 3d abs32 00      CMP  dword [armed],0        ; 0 outside a published frame (Present disarms)
//    7  74 5a               JE   continue
//    9  a1 abs32            MOV  EAX,[sector_view]
//   14  39 44 24 28         CMP  [ESP+0x28],EAX         ; the pass's view: the sector view only
//   18  75 4f               JNE  continue
//   20  ff 05 abs32         INC  dword [visits]         ; sector-view visits while armed (published > 0, skipped 0 explained)
//   26  69 c7 b1 79 37 9e   IMUL EAX,EDI,0x9e3779b1     ; hash_multiplier (2654435761, the tables' slot_of); EAX dead
//                                                       ;   at the site (written at 0x0047d2a7)
//   32  c1 e8 17            SHR  EAX,23                 ; home slot 0..511
//   35  6b c0 30            IMUL EAX,EAX,48             ; entry offset
//   38  8b 88 abs32         MOV  ECX,[EAX+table]        ; probe 0: entry.node (ECX dead: the displaced MOV writes it)
//   44  3b cf               CMP  ECX,EDI
//   46  74 39               JE   hit
//   48  85 c9               TEST ECX,ECX
//   50  74 2f               JE   continue               ; an empty slot ends the probe
//   52  83 c0 30            ADD  EAX,48
//   55  ..                  probe 1 (17 bytes), 72 probe 2 (17 bytes)
//   89  8b 88 abs32 3b cf 74 06   probe 3: MOV; CMP; JE hit (falls into continue)
//   99  ff 25 abs32         JMP  [next]                 ; continue: the previous chain head or the tail
//  105  8b 8f 40 01 00 00   MOV  ECX,[EDI+0x140]        ; hit: model id
//  111  3b 88 abs32         CMP  ECX,[EAX+table+4]
//  117  75 52               JNE  reject_model
//  119  8b 0d abs32         MOV  ECX,[stamp]
//  125  3b 88 abs32         CMP  ECX,[EAX+table+8]
//  131  75 50               JNE  reject_stamp
//  133  8b 8f f0 00 00 00   MOV  ECX,[EDI+0xf0]         ; camera-space x
//  139  3b 88 abs32         CMP  ECX,[EAX+table+12]     ; lo_x
//  145  7c 4e               JL   reject_position
//  147  3b 88 abs32         CMP  ECX,[EAX+table+16]     ; hi_x
//  153  7f 46               JG   reject_position
//  155  8b 8f f4 00 00 00   MOV  ECX,[EDI+0xf4]         ; y: 161 CMP lo_y; 167 JL; 169 CMP hi_y; 175 JG
//  177  8b 8f f8 00 00 00   MOV  ECX,[EDI+0xf8]         ; z: 183 CMP lo_z; 189 JL; 191 CMP hi_z; 197 JG
//  199  eb 24               JMP  skip
//  201  ff 05 abs32         INC  dword [rejected_model]  ; reject_model
//  207  ff 25 abs32         JMP  [next]
//  213  ff 05 abs32 ff 25 abs32   reject_stamp: INC [rejected_stamp]; JMP [next]
//  225  ff 05 abs32 ff 25 abs32   reject_position: INC [rejected_position]; JMP [next]
//  237  ff 05 abs32         INC  dword [skipped_parts]  ; skip
//  243  8b 88 abs32         MOV  ECX,[EAX+table+36]     ; the node's draw count of the ledger frame
//  249  01 0d abs32         ADD  [skipped_draws],ECX
//  255  8b 4f 18            MOV  ECX,[EDI+0x18]         ; replay: 0x0047d2a2..0x0047d2b9 so ECX/EAX arrive at the
//  258  85 c9               TEST ECX,ECX                ;   cull exactly as the engine leaves them (both dead there)
//  260  8b 87 d8 01 00 00   MOV  EAX,[EDI+0x1d8]
//  266  74 0c               JE   cull
//  268  8b 89 d8 01 00 00   MOV  ECX,[ECX+0x1d8]
//  274  3b c8               CMP  ECX,EAX
//  276  7e 02               JLE  cull
//  278  8b c1               MOV  EAX,ECX
//  280  e9 rel32            JMP  0x0047d2c3             ; cull: the engine's `and [edi+0x12c],~2; jmp 0x0047d2d1`
// No call, no Win32, no floating point: LastError and the x87 stack are untouched by construction. EFLAGS are dead
// on every exit (the tail's displaced TEST regenerates them for the JE at 0x0047d2ad, the cull AND overwrites them),
// as for the other two stubs. ESP, EDI, ESI, EBX, EBP and EDX are not written. Per visit: disarmed one compare;
// armed outside the sector view two more; in the sector view the visit count, the hash (3) and one probe per
// occupied slot on the chain (5 each, at most 4), a hit nine compares. Executed on the synthetic pass of
// cull_small_parts_fixture.cpp (engine_section) chained with the other two stubs in both orders.
constexpr unsigned stub_length = 285, stub_visits = 20, stub_probe = 38, stub_probe_length = 17, stub_continue = 99,
                   stub_hit = 105, stub_reject_model = 201, stub_reject_stamp = 213, stub_reject_position = 225,
                   stub_skip = 237, stub_replay = 255, stub_cull = 280;
struct StubWords {
    std::uint32_t armed, view, stamp, table, visits, skipped_parts, skipped_draws, rejected_model, rejected_stamp,
        rejected_position;
};
inline void encode_stub(std::uint32_t at, const StubWords& w, std::uint32_t cull_target, std::uint32_t next_slot,
                        unsigned char out[stub_length]) {
    unsigned n = 0;
    auto b = [&](unsigned char v) { out[n++] = v; };
    auto d = [&](std::uint32_t v) {
        std::memcpy(out + n, &v, 4);
        n += 4;
    };
    auto rel8 = [&](unsigned target) { b(static_cast<unsigned char>(static_cast<int>(target) - static_cast<int>(n + 1))); };
    auto cmp_entry = [&](unsigned field) { // cmp ecx,[eax+table+field]
        b(0x3b);
        b(0x88);
        d(w.table + field);
    };
    auto load_node = [&](std::uint32_t offset) { // mov ecx,[edi+offset]
        b(0x8b);
        b(0x8f);
        d(offset);
    };
    auto inc = [&](std::uint32_t counter) { // inc dword [counter]
        b(0xff);
        b(0x05);
        d(counter);
    };
    auto reject = [&](std::uint32_t counter) { // inc dword [counter]; jmp [next]
        inc(counter);
        b(0xff);
        b(0x25);
        d(next_slot);
    };
    // clang-format off
    b(0x83); b(0x3d); d(w.armed); b(0x00);   // 0
    b(0x74); rel8(stub_continue);            // 7
    b(0xa1); d(w.view);                      // 9
    b(0x39); b(0x44); b(0x24); b(0x28);      // 14
    b(0x75); rel8(stub_continue);            // 18
    inc(w.visits);                           // 20
    b(0x69); b(0xc7); d(hash_multiplier);    // 26 (0x9e3779b1: the same hash as Table::publish / hash_slot)
    b(0xc1); b(0xe8); b(0x17);               // 32
    b(0x6b); b(0xc0); b(0x30);               // 35
    for (unsigned probe = 0; probe < table_probe; ++probe) { // 38, 55, 72, 89
        b(0x8b); b(0x88); d(w.table);
        b(0x3b); b(0xcf);
        b(0x74); rel8(stub_hit);
        if (probe + 1 == table_probe) break;
        b(0x85); b(0xc9);
        b(0x74); rel8(stub_continue);
        b(0x83); b(0xc0); b(0x30);
    }
    b(0xff); b(0x25); d(next_slot);          // 99 continue
    load_node(model_offset);                 // 105 hit
    cmp_entry(4);                            // 111
    b(0x75); rel8(stub_reject_model);        // 117
    b(0x8b); b(0x0d); d(w.stamp);            // 119
    cmp_entry(8);                            // 125
    b(0x75); rel8(stub_reject_stamp);        // 131
    for (unsigned axis = 0; axis < 3; ++axis) { // 133, 155, 177
        load_node(position_offset + 4 * axis);
        cmp_entry(12 + 8 * axis);
        b(0x7c); rel8(stub_reject_position);
        cmp_entry(16 + 8 * axis);
        b(0x7f); rel8(stub_reject_position);
    }
    b(0xeb); rel8(stub_skip);                // 199
    reject(w.rejected_model);                // 201
    reject(w.rejected_stamp);                // 213
    reject(w.rejected_position);             // 225
    inc(w.skipped_parts);                    // 237 skip
    b(0x8b); b(0x88); d(w.table + 36);       // 243
    b(0x01); b(0x0d); d(w.skipped_draws);    // 249
    b(0x8b); b(0x4f); b(0x18);               // 255 replay
    b(0x85); b(0xc9);
    b(0x8b); b(0x87); d(cull_small_parts::core::threshold_1d8_offset);
    b(0x74); rel8(stub_cull);
    b(0x8b); b(0x89); d(cull_small_parts::core::threshold_1d8_offset);
    b(0x3b); b(0xc8);
    b(0x7e); rel8(stub_cull);
    b(0x8b); b(0xc1);
    b(0xe9); d(cull_target - (at + stub_length)); // 280 cull
    // clang-format on
}
}
