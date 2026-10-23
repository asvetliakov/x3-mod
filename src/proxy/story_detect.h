#pragma once
#include "story_builds.h"
#include <cstdint>
#include <cstring>

// Run-time detection of the story-script sites (docs/architecture/story-builds.md): from the KC VM the engine loaded,
// never from a per-build constant. The methods are found by class key and name through the VM's own tables, the way
// the engine's resolver 0x0049f330 does; the sites inside them by a short byte signature of the in-memory form, tested
// only at instruction starts of a decode walk from the method's entry and unique there; the IPs, return offsets and the
// KC patch window follow from the matches. A known build
// (story_builds.h) must reproduce its entry exactly (Fail::table_mismatch otherwise). Any failure names the method and
// the reason; the feature then stays inert for that object. Pure code over a fault-free reader
// `bool reader(std::uint32_t address, void* out, unsigned size)`, at most detection_read_budget calls per detection
// (Fail::read_budget beyond): no Windows header, no allocation, no floating point.
//
// VM layout (*0x006085e4; X3AP.exe SHA-256 fdbf3418..., static disassembly 2026-10-23):
//   +0x08 CODE buffer, +0x10 its size (loader 0x0049d332..0x0049d34e), +0x143c build id (0x0049d2a7);
//   +0x14 decoded string table, +0x18 its size (0x004b0740: name = [VM+0x14] + offset, offset < [VM+0x18]);
//   +0x1c class record count, +0x20 class records (0x004b06f0: 0x38-byte records sorted by the signed key at +0,
//   binary search; 0x0049f1e0 passes count EDX = [VM+0x1c], base EAX = [VM+0x20]); record +8 the class;
//   class +0x14 parent class, +0x28 method row count, +0x2c method rows (0x0049f330); row 16 bytes, +0 CODE entry,
//   +4 name offset, sorted by name (0x004b0740's binary search, unsigned byte compare);
//   +0x40 + g * 0x18, g = 1..31: native group g's NULL-terminated name table (char*[]); the load pass 0x0049e1a0
//   rewrites `82 <BE32 STRG name>` to `82 <u16 group> <u16 command>` with the first (g, index) whose name matches.
// In-memory CODE (0x0049e1a0): every multi-byte operand little-endian; jumps and calls (32..3a, 84..88) carry absolute
// CODE offsets. The peephole pass 0x0049e4f0 rewrites the opcode byte of patterns starting with 01/02 (+ jump), 0d..0f
// and 14..16 (+ index, indexed store, drop, drop n, increment), 17 (+ drop) and 64 (+ jump) into fused opcodes 8c..b1,
// and the first three bytes of `0d 0001 14 0002 24 83` into `83 83 83`; it then threads the operands of 32/33/34 through
// jmp chains and constant conditionals. No signature byte below is rewritten: the native (82), call (86/88), fork (6f)
// and child opcodes are never a pattern's head, and only the 32/33/34 operands are threaded.
// The decode walk (memory_width) uses the in-memory widths of the pass's own second walk 0x0049e810 (byte table
// 0x0049e9d4, jump table 0x0049e9a0), which cover each fused pattern whole; 78/79 carry little-endian u16 counts
// (0x0049e283, 0x0049e2e4). A byte the interpreter 0x004a26a0 has no handler of its own for (00, above b1, or a
// default-handler opcode 0x004a4301 other than 70 end) or an instruction past the method's end is Fail::decode_error.
namespace x3m::story {
constexpr unsigned vm_code = 0x08, vm_code_size = 0x10, vm_strings = 0x14, vm_strings_size = 0x18, vm_class_count = 0x1c,
                   vm_class_records = 0x20, vm_build = 0x143c, vm_group_names = 0x40, group_stride = 0x18;
constexpr unsigned group_first = 1, group_end = 0x20;
constexpr unsigned class_record_size = 0x38, record_class = 8;
constexpr unsigned class_parent = 0x14, class_method_count = 0x28, class_method_rows = 0x2c, method_row_size = 16;
// Bounds a sane VM stays inside (the installed objects: about 1,000 classes, 6,818 method rows, methods below 8 KiB).
constexpr unsigned class_count_limit = 0x4000, method_count_limit = 0x2000, chain_limit = 64, group_name_limit = 0x400;
constexpr unsigned name_limit = 48, scratch_size = 0x4000; // longest name compared; the largest method scanned
constexpr std::uint32_t code_size_limit = 0x01000000;
// Reader calls one detection may make (measured on the host images: 2,198-2,754 for x3story, 7 for x3config, 30 for
// x3intro); a VM whose tables would take more fails as Fail::read_budget.
constexpr unsigned detection_read_budget = 16384;
// Transient failures (Fail::unreadable, method_range, no_code) one key may retry before it is kept as retry_exhausted.
constexpr unsigned detect_retry_limit = 8;
constexpr unsigned char op_native = 0x82, op_callcls = 0x86, op_callself = 0x88, op_fork = 0x6f, op_jt = 0x33,
                        op_jmp = 0x32, op_pushw = 0x06, op_push1 = 0x02, op_drop = 0x24, op_end = 0x70, op_filler = 0x01;
// The ChangeTrackMusic fork child at PlayMusicWithFade's MOV_StopMovie: stack index -19 and its six frame cells by
// depth (old 18, loop return 13, new 11, fade 10, loop 9, change return 7). Measured on Mayhem (Run 189 k = -19, the
// cells' values each stop's shape check reads); the layout follows from the three music methods' frames, which are
// equal in both builds modulo relocation (verify_music_queue_kc.py). Not derivable from a byte signature: each stop
// checks the cell tags and the detected return offsets before it defers (music_keep_core.h deferrable_shape).
constexpr unsigned fork_child_stack_depth = 19;
inline constexpr unsigned fork_child_cell_depths[queue_cell_count] = {18, 13, 11, 10, 9, 7};

enum class Fail : unsigned {
    none = 0,
    no_vm,             // *0x006085e4 null
    unreadable,        // a VM field, record, row, name or method byte unreadable
    no_code,           // CODE buffer null or size 0 (x3config after its run, nothing loaded)
    bad_layout,        // a count or an offset outside the bounds above
    no_class,          // the class key has no record
    no_method,         // the class chain has no method of that name
    duplicate_method,  // two rows of that name in the class that has it
    method_range,      // entry outside CODE, or the method longer than scratch_size
    no_native,         // the native name is in no group table
    no_site,           // the signature does not occur at an instruction start of the method
    ambiguous_site,    // the signature occurs at more than one instruction start of the method
    branch_into_window,// a dword of WarpToSector points into the patch window's interior
    table_mismatch,    // a known build (story_builds.h) whose detected sites differ from its entry
    decode_error,      // the method's decode walk met an opcode without a handler or an instruction past its end
    read_budget,       // the detection would take more than detection_read_budget reads
    retry_exhausted,   // a transient failure seen detect_retry_limit + 1 times for one key (kept, no further retry)
    count
};
inline const char* fail_name(Fail f) noexcept {
    static const char* const names[unsigned(Fail::count)] = {
        "none",      "no_vm",     "unreadable",     "no_code",        "bad_layout",         "no_class",      "no_method",
        "duplicate_method", "method_range", "no_native", "no_site", "ambiguous_site", "branch_into_window", "table_mismatch",
        "decode_error", "read_budget", "retry_exhausted"};
    return unsigned(f) < unsigned(Fail::count) ? names[unsigned(f)] : "?";
}
// Which object the detection belongs to: a new key (a reload, another object) needs a new detection.
struct VmKey {
    std::uint32_t vm = 0, code = 0, size = 0, build = 0;
    bool operator==(const VmKey& o) const noexcept { return vm == o.vm && code == o.code && size == o.size && build == o.build; }
    bool operator!=(const VmKey& o) const noexcept { return !(*this == o); }
};
struct Range {
    std::uint32_t entry = 0, end = 0;
    bool contains(std::uint32_t at) const noexcept { return entry && at > entry && at <= end; } // a return offset
};
struct Outcome {
    Fail fail = Fail::none;
    const char* where = ""; // "150::StartGateWarp", "native:MOV_StopMovie", ...
    bool ok() const noexcept { return fail == Fail::none; }
};
// A failure of the moment the VM was read (a table or method not readable or not in range yet, no CODE yet): retried. Every
// other outcome is a fact of the loaded object and is kept for its key.
inline bool transient(Fail f) noexcept { return f == Fail::unreadable || f == Fail::method_range || f == Fail::no_code; }
// One feature's account of transient failures per key. keep() after each detection: true = keep the outcome for its key
// (a success or a definitive failure once `settled`; the (detect_retry_limit + 1)-th transient failure for one key,
// rewritten as retry_exhausted, at once); false = detect again at the next due check.
struct Retries {
    VmKey key;
    unsigned transients = 0;
    bool keep(const VmKey& k, Outcome& o, bool settled = true) noexcept {
        if (k != key) {
            key = k;
            transients = 0;
        }
        if (!transient(o.fail)) return settled;
        if (++transients <= detect_retry_limit) return false;
        o.fail = Fail::retry_exhausted;
        return true;
    }
};
// seta_restore: the two sites, and the ranges of StopFastForward's named callers (rows only).
struct SetaSites {
    VmKey key;
    bool detected = false; // the outcome is kept for `key` (Retries::keep); false: detect again
    Outcome outcome;
    std::uint32_t stop_ip = 0, gate_return = 0;
    static constexpr unsigned named_count = 5;
    Range named[named_count]{}; // gate, toggle, sector_warp, quick_warp, jumpdrive (0 = not in this object)
};
inline constexpr const char* seta_named[SetaSites::named_count] = {"gate", "toggle", "sector_warp", "quick_warp", "jumpdrive"};
// The named caller whose method holds a StopFastForward return offset; `bottom`: the task's bottom record.
inline const char* seta_return_name(const SetaSites* s, std::uint32_t offset, bool bottom) noexcept {
    if (bottom) return "engine_input";
    if (s && s->outcome.ok())
        for (unsigned i = 0; i < SetaSites::named_count; ++i)
            if (s->named[i].contains(offset)) return seta_named[i];
    return "other";
}
// music_queue: the KC patch window and the deferral site.
struct QueueSites {
    VmKey key;
    bool detected = false;
    Outcome outcome;
    std::uint32_t warp_method_entry = 0, warp_fork_offset = 0, warp_continue_offset = 0;
    unsigned char warp_window_memory[warp_window_length]{}, warp_patch_memory[warp_window_length]{};
    std::uint32_t kc_play_stop_ip = 0, kc_fade_stop_ip = 0, kc_play_ip = 0, kc_loop_music_return = 0, kc_change_track_return = 0;
    unsigned queue_stack_depth = fork_child_stack_depth;
    static constexpr const unsigned (&queue_cell_depths)[queue_cell_count] = fork_child_cell_depths; // the one literal
};

namespace detail {
inline std::uint32_t le32(const unsigned char* p) noexcept {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
struct Vm {
    std::uint32_t vm = 0, code = 0, size = 0, strings = 0, strings_size = 0, class_count = 0, records = 0, build = 0;
};
template <class R> bool read32(R& rd, std::uint32_t at, std::uint32_t& v) {
    return rd(at, &v, 4u);
}
// The reader with the detection's read budget: refuses every call past it and remembers that it did.
template <class R> struct Budgeted {
    R& rd;
    unsigned left;
    bool spent = false;
    bool operator()(std::uint32_t at, void* out, unsigned n) {
        if (!left) {
            spent = true;
            return false;
        }
        --left;
        return rd(at, out, n);
    }
};
// In-memory widths of opcodes 00..ff (0x0049e810's table; 78/79 variable; 0 = no handler of its own in the interpreter:
// 00, above b1, the default-handler opcodes 0x004a4301 other than 70).
struct WidthTable {
    unsigned char w[256];
    constexpr WidthTable() : w{} {
        for (unsigned op = 1; op <= 0xb1; ++op) w[op] = 1;
        const unsigned char none[] = {0x25, 0x26, 0x27, 0x31, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
                                      0x60, 0x61, 0x62, 0x63, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x71, 0x72, 0x73, 0x74,
                                      0x75, 0x76, 0x77, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x89, 0x8a, 0x8b, 0x9a,
                                      0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa8, 0xa9};
        const unsigned char two[] = {0x05, 0x47, 0x4c, 0x95};
        const unsigned char three[] = {0x06, 0x0d, 0x0e, 0x0f, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1a, 0x1b,
                                       0x1c, 0x1d, 0x1f, 0x20, 0x21, 0x23, 0x28, 0x29, 0x2c, 0x30, 0x48, 0x4d, 0x6e};
        const unsigned char five[] = {0x07, 0x0b, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
                                      0x49, 0x4e, 0x82, 0x84, 0x85, 0x86, 0x87, 0x88, 0x97, 0x98, 0x99};
        for (unsigned char op : none) w[op] = 0;
        for (unsigned char op : two) w[op] = 2;
        for (unsigned char op : three) w[op] = 3;
        for (unsigned op = 0x8c; op <= 0x94; ++op) w[op] = 4;
        for (unsigned char op : five) w[op] = 5;
        w[0x96] = 6;
        w[0xa4] = w[0xa5] = 8;
        w[0x0a] = w[0xa0] = w[0xa1] = 9;
        w[0xa6] = w[0xa7] = 10;
        w[0xa2] = w[0xa3] = 11;
    }
};
inline constexpr WidthTable memory_widths{};
// The width of the in-memory instruction at b[i] of a method b[0, length); 0 when it has no handler or ends past length.
inline unsigned memory_width(const unsigned char* b, unsigned i, unsigned length) noexcept {
    const unsigned char op = b[i];
    std::uint32_t w = memory_widths.w[op];
    if (op == 0x78 || op == 0x79) {
        if (length - i < 5) return 0;
        const std::uint32_t a = std::uint32_t(b[i + 1]) | std::uint32_t(b[i + 2]) << 8;
        const std::uint32_t c = std::uint32_t(b[i + 3]) | std::uint32_t(b[i + 4]) << 8;
        w = op == 0x78 ? 11 + 4 * a : 9 + 8 * (a + c);
    }
    return w && w <= length - i ? unsigned(w) : 0;
}
// The string at name offset `off` compared with `s` the way strcmp orders them (unsigned bytes): <0, 0, >0; `ok`
// false when unreadable or the offset is out of range. Only strlen(s) + 1 bytes decide the order.
template <class R> int compare_name(R& rd, const Vm& v, std::uint32_t off, const char* s, bool& ok) {
    const unsigned n = unsigned(std::strlen(s)) + 1;
    unsigned char buf[name_limit + 1]{};
    ok = false;
    if (n > name_limit || off >= v.strings_size) return 1;
    const unsigned avail = v.strings_size - off < n ? v.strings_size - off : n;
    if (!rd(v.strings + off, buf, avail)) return 1;
    ok = true;
    for (unsigned i = 0; i < avail; ++i) {
        const unsigned char a = static_cast<unsigned char>(s[i]), b = buf[i];
        if (a != b) return a < b ? -1 : 1;
        if (!a) return 0;
    }
    return 1; // the row's name ran past the table end before s ended: treat as greater (never equal)
}
template <class R> Outcome vm_fields(R& rd, std::uint32_t vm, Vm& v) {
    v = Vm{};
    v.vm = vm;
    if (!vm) return {Fail::no_vm, "vm"};
    if (!read32(rd, vm + vm_code, v.code) || !read32(rd, vm + vm_code_size, v.size) || !read32(rd, vm + vm_build, v.build) ||
        !read32(rd, vm + vm_strings, v.strings) || !read32(rd, vm + vm_strings_size, v.strings_size) ||
        !read32(rd, vm + vm_class_count, v.class_count) || !read32(rd, vm + vm_class_records, v.records))
        return {Fail::unreadable, "vm"};
    if (!v.code || !v.size) return {Fail::no_code, "vm"};
    if (v.size > code_size_limit || !v.strings || !v.strings_size || !v.records || !v.class_count ||
        v.class_count > class_count_limit)
        return {Fail::bad_layout, "vm"};
    return {};
}
// The class record for `key` (0x004b06f0's binary search over the signed keys), 0 when none.
template <class R> std::uint32_t class_of(R& rd, const Vm& v, std::int32_t key, bool& ok) {
    ok = true;
    std::uint32_t lo = 0, hi = v.class_count;
    while (lo < hi) {
        const std::uint32_t mid = lo + (hi - lo) / 2;
        std::int32_t k = 0;
        std::uint32_t cls = 0;
        const std::uint32_t at = v.records + mid * class_record_size;
        if (!rd(at, &k, 4u) || !read32(rd, at + record_class, cls)) {
            ok = false;
            return 0;
        }
        if (k == key) return cls;
        if (key < k)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}
// A method entry by class key and name: the class chain from the key's class through its parents, the first class
// with a row of that name (0x0049f330), found by 0x004b0740's binary search; the neighbours must not share the name.
template <class R> Outcome method_entry(R& rd, const Vm& v, std::int32_t key, const char* name, const char* where,
                                        std::uint32_t& entry) {
    entry = 0;
    bool ok = true;
    std::uint32_t cls = class_of(rd, v, key, ok);
    if (!ok) return {Fail::unreadable, where};
    if (!cls) return {Fail::no_class, where};
    for (unsigned depth = 0; cls && depth < chain_limit; ++depth) {
        std::uint32_t count = 0, rows = 0, parent = 0;
        if (!read32(rd, cls + class_method_count, count) || !read32(rd, cls + class_method_rows, rows) ||
            !read32(rd, cls + class_parent, parent))
            return {Fail::unreadable, where};
        if (count > method_count_limit || (count && !rows)) return {Fail::bad_layout, where};
        std::uint32_t lo = 0, hi = count;
        while (lo < hi) {
            const std::uint32_t mid = lo + (hi - lo) / 2;
            std::uint32_t row[2]{};
            if (!rd(rows + mid * method_row_size, row, 8u)) return {Fail::unreadable, where};
            const int c = compare_name(rd, v, row[1], name, ok);
            if (!ok) return {Fail::unreadable, where};
            if (c == 0) {
                const std::int64_t neighbours[2] = {std::int64_t(mid) - 1, std::int64_t(mid) + 1};
                for (const std::int64_t n : neighbours) {
                    if (n < 0 || n >= std::int64_t(count)) continue;
                    std::uint32_t other[2]{};
                    if (!rd(rows + std::uint32_t(n) * method_row_size, other, 8u)) return {Fail::unreadable, where};
                    if (compare_name(rd, v, other[1], name, ok) == 0 && ok) return {Fail::duplicate_method, where};
                    if (!ok) return {Fail::unreadable, where};
                }
                if (!row[0] || row[0] >= v.size) return {Fail::method_range, where};
                entry = row[0];
                return {};
            }
            if (c < 0)
                hi = mid;
            else
                lo = mid + 1;
        }
        cls = parent;
    }
    return {Fail::no_method, where};
}
// The method ends: for each range with an entry, the smallest CODE entry above it over every row of every class
// (the CODE size when none).
template <class R> Outcome method_ends(R& rd, const Vm& v, Range* ranges, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        if (ranges[i].entry) ranges[i].end = v.size;
    constexpr unsigned chunk = 64;
    std::uint32_t rows_buf[chunk * method_row_size / 4];
    for (std::uint32_t c = 0; c < v.class_count; ++c) {
        std::uint32_t cls = 0, count = 0, rows = 0;
        if (!read32(rd, v.records + c * class_record_size + record_class, cls)) return {Fail::unreadable, "classes"};
        if (!cls) continue;
        if (!read32(rd, cls + class_method_count, count) || !read32(rd, cls + class_method_rows, rows))
            return {Fail::unreadable, "classes"};
        if (count > method_count_limit || (count && !rows)) return {Fail::bad_layout, "classes"};
        for (std::uint32_t r = 0; r < count; r += chunk) {
            const unsigned take = count - r < chunk ? unsigned(count - r) : chunk;
            if (!rd(rows + r * method_row_size, rows_buf, take * method_row_size)) return {Fail::unreadable, "classes"};
            for (unsigned k = 0; k < take; ++k) {
                const std::uint32_t e = rows_buf[k * (method_row_size / 4)];
                for (unsigned i = 0; i < n; ++i)
                    if (ranges[i].entry && e > ranges[i].entry && e < ranges[i].end) ranges[i].end = e;
            }
        }
    }
    return {};
}
// A native's in-memory operand (u16 group, u16 command) by name, in 0x0049e1a0's search order.
template <class R> Outcome native_of(R& rd, const Vm& v, const char* name, const char* where, std::uint16_t& group,
                                     std::uint16_t& command) {
    const unsigned n = unsigned(std::strlen(name)) + 1;
    for (unsigned g = group_first; g < group_end; ++g) {
        std::uint32_t table = 0;
        if (!read32(rd, v.vm + vm_group_names + g * group_stride, table)) return {Fail::unreadable, where};
        if (!table) continue;
        for (unsigned i = 0; i < group_name_limit; ++i) {
            std::uint32_t p = 0;
            if (!read32(rd, table + 4 * i, p)) return {Fail::unreadable, where};
            if (!p) break;
            char buf[name_limit + 1]{};
            if (n <= name_limit && rd(p, buf, n) && std::memcmp(buf, name, n) == 0) {
                group = std::uint16_t(g);
                command = std::uint16_t(i);
                return {};
            }
        }
    }
    return {Fail::no_native, where};
}
// A method's bytes [entry, end) into `buf` (scratch_size).
template <class R> Outcome method_bytes(R& rd, const Vm& v, const Range& m, unsigned char* buf, unsigned& length,
                                        const char* where) {
    length = 0;
    if (!m.entry || m.end <= m.entry || m.end > v.size) return {Fail::method_range, where};
    if (m.end - m.entry > scratch_size) return {Fail::method_range, where};
    length = m.end - m.entry;
    if (!rd(v.code + m.entry, buf, length)) return {Fail::unreadable, where};
    return {};
}
// The only instruction start i of the method buf[0, length) (a decode walk from its entry, offset 0) where
// `match(buf, i, length)` holds; Fail::no_site / ambiguous_site otherwise, Fail::decode_error when the walk fails. A
// signature's bytes inside another instruction's operand are never tested.
template <class M> Outcome unique_site(const unsigned char* buf, unsigned length, M match, const char* where, unsigned& at) {
    unsigned hits = 0;
    for (unsigned i = 0; i < length;) {
        const unsigned w = memory_width(buf, i, length);
        if (!w) return {Fail::decode_error, where};
        if (match(buf, i, length)) {
            if (!hits) at = i;
            ++hits;
        }
        i += w;
    }
    if (!hits) return {Fail::no_site, where};
    if (hits > 1) return {Fail::ambiguous_site, where};
    return {};
}
inline bool native_at(const unsigned char* b, unsigned i, unsigned n, std::uint16_t g, std::uint16_t c) {
    return i + 5 <= n && b[i] == op_native && b[i + 1] == (g & 0xff) && b[i + 2] == (g >> 8) && b[i + 3] == (c & 0xff) &&
           b[i + 4] == (c >> 8);
}
inline bool call_at(const unsigned char* b, unsigned i, unsigned n, std::uint32_t target) {
    return i + 5 <= n && (b[i] == op_callcls || b[i] == op_callself) && le32(b + i + 1) == target;
}
} // namespace detail

namespace detail {
template <class R> void detect_seta(R& rd, std::uint32_t vm, SetaSites& out, unsigned char* scratch) {
    out = SetaSites{};
    out.detected = true;
    Vm v;
    out.outcome = vm_fields(rd, vm, v);
    out.key = VmKey{vm, v.code, v.size, v.build};
    if (!out.outcome.ok()) return;
    struct Want {
        std::int32_t cls;
        const char* name;
        const char* where;
        bool required;
    };
    static constexpr Want want[] = {{150, "StopFastForward", "150::StopFastForward", true},
                                    {150, "StartGateWarp", "150::StartGateWarp", true},
                                    {150, "ToggleFastForward", "150::ToggleFastForward", false},
                                    {150, "StartSectorWarp", "150::StartSectorWarp", false},
                                    {150, "QuickWarp", "150::QuickWarp", false},
                                    {2004, "JumpToSector", "2004::JumpToSector", false}};
    Range r[6]{};
    for (unsigned i = 0; i < 6; ++i) {
        const Outcome o = method_entry(rd, v, want[i].cls, want[i].name, want[i].where, r[i].entry);
        if (!o.ok() && (want[i].required || (o.fail != Fail::no_method && o.fail != Fail::no_class))) {
            out.outcome = o;
            return;
        }
    }
    if (!(out.outcome = method_ends(rd, v, r, 6)).ok()) return;
    std::uint16_t g = 0, c = 0;
    if (!(out.outcome = native_of(rd, v, "TI_SetTimeWarpFactor", "native:TI_SetTimeWarpFactor", g, c)).ok()) return;
    unsigned length = 0, at = 0;
    if (!(out.outcome = method_bytes(rd, v, r[0], scratch, length, want[0].where)).ok()) return;
    if (!(out.outcome = unique_site(scratch, length, [&](const unsigned char* b, unsigned i, unsigned n) { return native_at(b, i, n, g, c); },
                                    want[0].where, at)).ok())
        return;
    out.stop_ip = r[0].entry + at + 5;
    if (!(out.outcome = method_bytes(rd, v, r[1], scratch, length, want[1].where)).ok()) return;
    const std::uint32_t sff = r[0].entry;
    if (!(out.outcome = unique_site(scratch, length, [&](const unsigned char* b, unsigned i, unsigned n) { return call_at(b, i, n, sff); },
                                    want[1].where, at)).ok())
        return;
    out.gate_return = r[1].entry + at + 5;
    for (unsigned i = 0; i < SetaSites::named_count; ++i) out.named[i] = r[i + 1];
    if (const Build* b = find(v.build))
        if (b->seta_stop_ip != out.stop_ip || b->seta_gate_return != out.gate_return)
            out.outcome = {Fail::table_mismatch, b->name};
}
} // namespace detail
// SETA: 150::StopFastForward's only TI_SetTimeWarpFactor native (stop IP = site + 5) and 150::StartGateWarp's only call
// of StopFastForward (return offset = site + 5). At most `budget` reader calls.
template <class R>
void detect_seta(R& rd, std::uint32_t vm, SetaSites& out, unsigned char* scratch, unsigned budget = detection_read_budget) {
    detail::Budgeted<R> b{rd, budget};
    detail::detect_seta(b, vm, out, scratch);
    if (b.spent) out.outcome.fail = Fail::read_budget;
}

// The queue: 150::WarpToSector's only fork `6f; jt <p+20>` whose child is `pushw 500; push1; pushw 200; callcls
// 200::StopMusic; drop; end` (the window, continuation p + 20), with no dword of the method pointing into the window's
// interior; 200::PlayMusicWithFade's only MOV_StopMovie and MOV_PlayMovie natives, 200::MusicFadeOut's only
// MOV_StopMovie, 200::LoopMusic's only call of PlayMusicWithFade and 200::ChangeTrackMusic's only call of LoopMusic.
// At most `budget` reader calls.
namespace detail {
template <class R> void detect_queue(R& rd, std::uint32_t vm, QueueSites& out, unsigned char* scratch) {
    out = QueueSites{};
    out.detected = true;
    Vm v;
    out.outcome = vm_fields(rd, vm, v);
    out.key = VmKey{vm, v.code, v.size, v.build};
    if (!out.outcome.ok()) return;
    enum { warp, play, fade, loop, change, stop_music, methods };
    static constexpr struct {
        std::int32_t cls;
        const char* name;
        const char* where;
    } want[methods] = {{150, "WarpToSector", "150::WarpToSector"},         {200, "PlayMusicWithFade", "200::PlayMusicWithFade"},
                       {200, "MusicFadeOut", "200::MusicFadeOut"},         {200, "LoopMusic", "200::LoopMusic"},
                       {200, "ChangeTrackMusic", "200::ChangeTrackMusic"}, {200, "StopMusic", "200::StopMusic"}};
    Range r[methods]{};
    for (unsigned i = 0; i < methods; ++i)
        if (!(out.outcome = method_entry(rd, v, want[i].cls, want[i].name, want[i].where, r[i].entry)).ok()) return;
    if (!(out.outcome = method_ends(rd, v, r, methods)).ok()) return;
    std::uint16_t sg = 0, sc = 0, pg = 0, pc = 0;
    if (!(out.outcome = native_of(rd, v, "MOV_StopMovie", "native:MOV_StopMovie", sg, sc)).ok()) return;
    if (!(out.outcome = native_of(rd, v, "MOV_PlayMovie", "native:MOV_PlayMovie", pg, pc)).ok()) return;
    unsigned length = 0, at = 0;
    // WarpToSector: the fork window and its StopMusic(500) child.
    if (!(out.outcome = method_bytes(rd, v, r[warp], scratch, length, want[warp].where)).ok()) return;
    const std::uint32_t warp_entry = r[warp].entry, stop_entry = r[stop_music].entry;
    auto fork_child = [&](const unsigned char* b, unsigned i, unsigned n) {
        static constexpr unsigned char child[] = {op_pushw, 0xf4, 0x01, op_push1, op_pushw, 0xc8, 0x00};
        if (i + 20 > n || b[i] != op_fork || b[i + 1] != op_jt || le32(b + i + 2) != warp_entry + i + 20) return false;
        return std::memcmp(b + i + 6, child, sizeof child) == 0 && b[i + 13] == op_callcls && le32(b + i + 14) == stop_entry &&
               b[i + 18] == op_drop && b[i + 19] == op_end;
    };
    if (!(out.outcome = unique_site(scratch, length, fork_child, want[warp].where, at)).ok()) return;
    const std::uint32_t fork = warp_entry + at, cont = fork + 20;
    for (unsigned i = 0; i + 4 <= length; ++i) {
        const std::uint32_t d = le32(scratch + i);
        if (d > fork && d < fork + warp_window_length) {
            out.outcome = {Fail::branch_into_window, want[warp].where};
            return;
        }
    }
    out.warp_method_entry = warp_entry;
    out.warp_fork_offset = fork;
    out.warp_continue_offset = cont;
    const unsigned char window[warp_window_length] = {op_fork, op_jt, std::uint8_t(cont), std::uint8_t(cont >> 8),
                                                      std::uint8_t(cont >> 16), std::uint8_t(cont >> 24)};
    const unsigned char patch[warp_window_length] = {op_jmp, std::uint8_t(cont), std::uint8_t(cont >> 8),
                                                     std::uint8_t(cont >> 16), std::uint8_t(cont >> 24), op_filler};
    std::memcpy(out.warp_window_memory, window, warp_window_length);
    std::memcpy(out.warp_patch_memory, patch, warp_window_length);
    // PlayMusicWithFade: the stop and the play.
    if (!(out.outcome = method_bytes(rd, v, r[play], scratch, length, want[play].where)).ok()) return;
    if (!(out.outcome = unique_site(scratch, length, [&](const unsigned char* b, unsigned i, unsigned n) { return native_at(b, i, n, sg, sc); },
                                    want[play].where, at)).ok())
        return;
    out.kc_play_stop_ip = r[play].entry + at + 5;
    if (!(out.outcome = unique_site(scratch, length, [&](const unsigned char* b, unsigned i, unsigned n) { return native_at(b, i, n, pg, pc); },
                                    want[play].where, at)).ok())
        return;
    out.kc_play_ip = r[play].entry + at + 5;
    // MusicFadeOut: its stop.
    if (!(out.outcome = method_bytes(rd, v, r[fade], scratch, length, want[fade].where)).ok()) return;
    if (!(out.outcome = unique_site(scratch, length, [&](const unsigned char* b, unsigned i, unsigned n) { return native_at(b, i, n, sg, sc); },
                                    want[fade].where, at)).ok())
        return;
    out.kc_fade_stop_ip = r[fade].entry + at + 5;
    // The two return cells.
    const std::uint32_t play_entry = r[play].entry, loop_entry = r[loop].entry;
    if (!(out.outcome = method_bytes(rd, v, r[loop], scratch, length, want[loop].where)).ok()) return;
    if (!(out.outcome = unique_site(scratch, length, [&](const unsigned char* b, unsigned i, unsigned n) { return call_at(b, i, n, play_entry); },
                                    want[loop].where, at)).ok())
        return;
    out.kc_loop_music_return = r[loop].entry + at + 5;
    if (!(out.outcome = method_bytes(rd, v, r[change], scratch, length, want[change].where)).ok()) return;
    if (!(out.outcome = unique_site(scratch, length, [&](const unsigned char* b, unsigned i, unsigned n) { return call_at(b, i, n, loop_entry); },
                                    want[change].where, at)).ok())
        return;
    out.kc_change_track_return = r[change].entry + at + 5;
    if (const Build* b = find(v.build))
        if (b->warp_method_entry != out.warp_method_entry || b->warp_fork_offset != fork || b->warp_continue_offset != cont ||
            std::memcmp(b->warp_window_memory, out.warp_window_memory, warp_window_length) != 0 ||
            std::memcmp(b->warp_patch_memory, out.warp_patch_memory, warp_window_length) != 0 ||
            b->kc_play_stop_ip != out.kc_play_stop_ip || b->kc_fade_stop_ip != out.kc_fade_stop_ip ||
            b->kc_play_ip != out.kc_play_ip || b->kc_loop_music_return != out.kc_loop_music_return ||
            b->kc_change_track_return != out.kc_change_track_return)
            out.outcome = {Fail::table_mismatch, b->name};
}
} // namespace detail
template <class R>
void detect_queue(R& rd, std::uint32_t vm, QueueSites& out, unsigned char* scratch, unsigned budget = detection_read_budget) {
    detail::Budgeted<R> b{rd, budget};
    detail::detect_queue(b, vm, out, scratch);
    if (b.spent) out.outcome.fail = Fail::read_budget;
}
// The VM's current key (pointer, CODE buffer, size, build): whether a cached detection still applies.
template <class R> bool read_key(R& rd, std::uint32_t vm, VmKey& k) {
    k = VmKey{};
    k.vm = vm;
    return vm && detail::read32(rd, vm + vm_code, k.code) && detail::read32(rd, vm + vm_code_size, k.size) &&
           detail::read32(rd, vm + vm_build, k.build);
}
} // namespace x3m::story
