#pragma once
#include <cstdint>
#include <cstring>
#include "cull_census_core.h"

// Portable core of the scene-graph census row (docs/reverse-engineering/
// script-task-scheduler.md 1 and 4; docs/reverse-engineering/object-lifetimes.md,
// "Run382"): the render manager's four hash-table counts, a bounded walk of the
// list of nodes attached to no scene with a model-id histogram, and the
// insert-caller attribution of the node registry. Every engine read goes through
// the caller's reader (production: engine_memory::read); no Windows dependency,
// so the host test compiles it directly.
namespace x3m::scene_graph_census::core {
// R = *render_manager_va. Hash tables are {+0 buckets, +4 bucket count, +8 next
// auto-id, +0xc entry count} (insert 0x004efbf0 counts at 0x004efc9e, remove
// 0x004efd30 at 0x004efd8f).
constexpr std::uintptr_t render_manager_va = 0x00608518, script_vm_va = 0x006085e4;
static_assert(render_manager_va == x3m::cull_census::core::body_global_va, "the body table lives in the same manager");
constexpr unsigned node_table_offset = 0x0c, scene_table_offset = 0x10, cut_table_offset = 0x84, vm_task_table_offset = 0x00;
constexpr unsigned table_header_size = 16, table_bucket_count_index = 1, table_count_index = 3;
constexpr std::uint32_t count_limit = 10000000; // a count above it (or negative) is refused
// Pass A of 0x0048f550 (0x0048f557 mov esi,[eax+0x28]; 0x0048f56e mov esi,[esi]):
// an embedded list header at R+0x28 {head, 0, tailpred}; node +0 next, +4 prev.
// Nodes are appended at the tail on creation (0x00486d10 AddTail at 0x00486e28..
// 0x00486e38: node+4 = [R+0x30], [old tail] = node, node+0 = R+0x2c, [R+0x30] = node),
// so the census walks newest first: from the tailpred [R+0x30] over +4 until the node
// whose +4 is 0, the header's own head slot R+0x28 (its "prev" is the zero at R+0x2c).
// A time-capped walk has then seen the most recently created nodes.
constexpr unsigned unattached_list_offset = 0x28, list_tailpred_offset = 0x30;
constexpr unsigned node_prev_offset = 0x04, node_model_offset = 0x140; // model id (object-identity.md;
                                                                       // -1 at creation, 0x00486da9)
constexpr std::uint32_t walk_bound = 300000;
constexpr unsigned budget_check_interval = 1024; // nodes between two budget checks (one QPC each)
// Model-id histogram: open addressing, fixed table, no allocation.
constexpr unsigned histogram_slots = 1024, histogram_probe = 32, top_count = 8;
static_assert(!(histogram_slots & (histogram_slots - 1)), "power of two");

struct Count {
    bool valid;
    std::uint32_t value;
};
struct Counts {
    bool manager;                              // *render_manager_va readable and non-null
    std::uint32_t manager_address;
    Count nodes, scenes, cuts, cut_buckets, tasks;
};
struct HistogramSlot {
    std::int32_t id;
    std::uint32_t count; // 0 = empty slot
};
struct Walk {
    std::uint32_t length = 0;       // nodes on the list (the tail slot excluded)
    std::uint32_t sampled = 0;      // nodes whose model id was read (every stride-th)
    std::uint32_t other = 0;        // sampled nodes that found no histogram slot
    std::uint32_t distinct = 0;     // histogram keys in use
    bool truncated = false;         // a read failed, a link was null, or the walk ended on a foreign head
    bool cycle = false;             // Brent's detector met a node twice
    bool bounded = false;           // walk_bound nodes walked
    bool capped = false;            // the time budget expired
};
struct TopEntry {
    std::int32_t id;
    std::uint32_t count;
};

template <class Read> bool read_u32(Read& read, std::uintptr_t address, std::uint32_t* out) {
    return read(address, out, 4);
}
// One table header; the count (and bucket count) refused when negative or above count_limit.
template <class Read> void read_table(Read& read, std::uint32_t owner, unsigned offset, Count* count, Count* buckets) {
    *count = {false, 0};
    if (buckets) *buckets = {false, 0};
    std::uint32_t table = 0, header[4]{};
    if (!owner || !read_u32(read, std::uintptr_t(owner) + offset, &table) || !table ||
        !read(std::uintptr_t(table), header, table_header_size))
        return;
    if (header[table_count_index] <= count_limit) *count = {true, header[table_count_index]};
    if (buckets && header[table_bucket_count_index] <= count_limit) *buckets = {true, header[table_bucket_count_index]};
}
template <class Read> Counts read_counts(Read& read, std::uintptr_t manager_va = render_manager_va,
                                         std::uintptr_t vm_va = script_vm_va) {
    Counts c{};
    std::uint32_t r = 0, vm = 0;
    c.manager = read_u32(read, manager_va, &r) && r;
    c.manager_address = r;
    if (c.manager) {
        read_table(read, r, node_table_offset, &c.nodes, nullptr);
        read_table(read, r, scene_table_offset, &c.scenes, nullptr);
        read_table(read, r, cut_table_offset, &c.cuts, &c.cut_buckets);
    }
    if (read_u32(read, vm_va, &vm) && vm) read_table(read, vm, vm_task_table_offset, &c.tasks, nullptr);
    return c;
}

inline void histogram_add(HistogramSlot* table, Walk& walk, std::int32_t id) {
    std::uint32_t at = (std::uint32_t(id) * 2654435761u) & (histogram_slots - 1);
    for (unsigned n = 0; n < histogram_probe; ++n, at = (at + 1) & (histogram_slots - 1)) {
        HistogramSlot& s = table[at];
        if (!s.count) {
            s.id = id;
            s.count = 1;
            ++walk.distinct;
            return;
        }
        if (s.id == id) {
            ++s.count;
            return;
        }
    }
    ++walk.other;
}
// The top `top_count` slots by count (ties: the lower id first), descending; returns how many.
inline unsigned histogram_top(const HistogramSlot* table, TopEntry* top) {
    unsigned n = 0;
    for (unsigned i = 0; i < histogram_slots; ++i) {
        if (!table[i].count) continue;
        const TopEntry e{table[i].id, table[i].count};
        unsigned at = n < top_count ? n++ : top_count;
        while (at > 0 && (top[at - 1].count < e.count || (top[at - 1].count == e.count && top[at - 1].id > e.id))) {
            if (at < top_count) top[at] = top[at - 1];
            --at;
        }
        if (at < top_count) top[at] = e;
    }
    return n;
}

// Walks the list at manager+0x28 newest first (bounded, Brent cycle guard, no
// allocation): counts every node and reads the model id of every stride-th node into
// `table` (cleared here). `expired()` is asked every budget_check_interval nodes. One
// 4-byte read per node plus one per sampled node.
template <class Read, class Expired>
Walk walk_unattached(Read& read, std::uint32_t manager, unsigned stride, HistogramSlot* table, Expired&& expired,
                     std::uint32_t bound = walk_bound) {
    Walk walk{};
    std::memset(table, 0, sizeof(HistogramSlot) * histogram_slots);
    if (!stride) stride = 1;
    std::uint32_t node = 0;
    if (!manager || !read_u32(read, std::uintptr_t(manager) + list_tailpred_offset, &node)) {
        walk.truncated = true;
        return walk;
    }
    const std::uint32_t head = manager + unattached_list_offset;
    std::uint32_t saved = node, power = 1, lambda = 0, until_sample = 0, until_check = budget_check_interval;
    for (;;) {
        std::uint32_t next = 0; // the previous node: the walk goes newest first
        if (!node || !read_u32(read, std::uintptr_t(node) + node_prev_offset, &next)) {
            walk.truncated = true;
            break;
        }
        if (!next) { // the terminating slot: the list's own head, else the walk left the list
            walk.truncated = node != head;
            break;
        }
        ++walk.length;
        if (!until_sample) {
            std::int32_t id = 0;
            if (!read(std::uintptr_t(node) + node_model_offset, &id, 4)) {
                walk.truncated = true;
                break;
            }
            ++walk.sampled;
            histogram_add(table, walk, id);
            until_sample = stride;
        }
        --until_sample;
        if (walk.length >= bound) {
            walk.bounded = true;
            break;
        }
        if (!--until_check) {
            until_check = budget_check_interval;
            if (expired()) {
                walk.capped = true;
                break;
            }
        }
        node = next;
        if (node == saved) {
            walk.cycle = true;
            break;
        }
        if (++lambda == power) { // Brent: move the checkpoint, double the window
            saved = node;
            power <<= 1;
            lambda = 0;
        }
    }
    return walk;
}

// The body name of a model id as the cull census resolves it (cull_census_core.h,
// body_slot / body_default_name): table header read at manager+0xb4, slot name
// pointer, the name scanned in page-bounded chunks. `out` receives a space-free
// token of at most body_name_cap characters, or "model<id>" when the id does not
// resolve (invalid id, unreadable slot or name, no NUL within body_name_scan).
constexpr unsigned name_size = x3m::cull_census::core::body_suffix_size;
template <class Read> void body_name(Read& read, std::uint32_t manager, std::int32_t id, char* out /* name_size */) {
    using namespace x3m::cull_census::core;
    char name[body_name_scan];
    const char* shown = nullptr;
    std::int32_t head[3]{};
    std::uint32_t index = 0, p = 0;
    if (manager && read(std::uintptr_t(manager) + body_fixed_count_offset, head, sizeof head) &&
        head[0] == body_fixed_count && head[1] >= 0 && head[1] < body_dynamic_limit && head[2] &&
        body_slot(id, head[0], head[1], &index)) {
        const std::uint64_t entry =
            std::uint64_t(std::uint32_t(head[2])) + std::uint64_t(index) * body_slot_stride + body_slot_name_offset;
        if (entry <= 0xfffffffcu && read(std::uintptr_t(entry), &p, 4)) {
            if (!p) {
                body_default_name(id, name);
                shown = name;
            } else {
                unsigned have = 0;
                while (have < body_name_scan && !shown) {
                    const std::uintptr_t at = std::uintptr_t(p) + have;
                    unsigned chunk = unsigned(0x1000 - (at & 0xfff));
                    if (chunk > body_name_scan - have) chunk = body_name_scan - have;
                    if (!read(at, name + have, chunk)) break;
                    for (unsigned i = have; i < have + chunk; ++i)
                        if (!name[i]) {
                            shown = name;
                            break;
                        }
                    have += chunk;
                }
            }
        }
    }
    if (shown && shown[0]) {
        char suffix[body_suffix_size];
        format_body(suffix, shown); // " body=<name>"
        std::memcpy(out, suffix + 6, std::strlen(suffix + 6) + 1);
        return;
    }
    // "model<id>": signed decimal
    const char prefix[] = "model";
    std::memcpy(out, prefix, 5);
    unsigned at = 5;
    std::uint32_t u = id < 0 ? 0u - std::uint32_t(id) : std::uint32_t(id);
    if (id < 0) out[at++] = '-';
    char digits[11];
    unsigned n = 0;
    do {
        digits[n++] = char('0' + u % 10);
        u /= 10;
    } while (u);
    while (n) out[at++] = digits[--n];
    out[at] = 0;
}

// Insert callers of the node registry, at the object-lifetime Insert hook on
// 0x004efbf0. esp0 is the stack address of the hooked call's return address.
// The auto-id register 0x004efcc0 calls the insert at 0x004efd09 (return
// 0x004efd0e) after push ebx/esi/edi and two argument pushes, so its own
// return address (into the allocator) is at esp0 + 4 + 8 + 12 = esp0 + 24.
// `site` is the return address into the allocator (auto-id path) or into the
// direct caller; `caller` is the allocator's own return address where a rule
// below knows its frame, else 0.
constexpr std::uint32_t autoid_insert_return = 0x004efd0e;
constexpr unsigned autoid_return_offset = 24;
struct CallerRule {
    std::uint32_t return_site; // the return address that selects the rule
    bool via_ebp;              // true: [EBP + offset]; false: [frame + offset]
    std::uint8_t offset;
};
// Frames read from the installed EXE (verification/analysis/test_scene_graph_census.py pins the bytes).
// frame = the stack address of `return_site` (the called auto-id register's entry ESP).
constexpr CallerRule caller_rules[] = {
    // 0x00486d10: push ecx/ebx/ebp/esi/edi (20 bytes), push ebx (node), call 0x004efcc0 at 0x00486d78
    {0x00486d7d, false, 4 + 4 + 20},
    // 0x004885a0: push ecx/esi/edi (12 bytes), push edi (node), call 0x004efcc0 at 0x00488605
    {0x0048860a, false, 4 + 4 + 12},
    // 0x00488c70 (0x790-byte node): push ebx/esi/edi (12 bytes), push edi (node), call 0x004efcc0 at 0x00488cd6
    {0x00488cdb, false, 4 + 4 + 12},
    // 0x00479d10: push ebp; mov ebp,esp; and esp,-16 (EBP frame, EBP not written before the call);
    // direct call of 0x004efbf0 at 0x0047a6ad on R+0xc with key [node+0x28]
    {0x0047a6b2, true, 4},
};
constexpr unsigned caller_rule_count = sizeof caller_rules / sizeof caller_rules[0];
template <class Read>
void resolve_insert_caller(Read& read, std::uintptr_t esp0, std::uint32_t return_address, std::uint32_t ebp,
                           const CallerRule* rules, unsigned rule_count, std::uint32_t* site, std::uint32_t* caller) {
    std::uintptr_t frame = esp0;
    std::uint32_t at = return_address;
    *caller = 0;
    if (return_address == autoid_insert_return) {
        frame = esp0 + autoid_return_offset;
        if (!read_u32(read, frame, &at)) {
            *site = return_address;
            return;
        }
    }
    *site = at;
    for (unsigned i = 0; i < rule_count; ++i)
        if (rules[i].return_site == at) {
            const std::uintptr_t address = rules[i].via_ebp ? std::uintptr_t(ebp) + rules[i].offset : frame + rules[i].offset;
            std::uint32_t value = 0;
            if (read_u32(read, address, &value)) *caller = value;
            return;
        }
}
// Per-row insert counts by (site, caller): fixed 32 slots, linear search.
constexpr unsigned caller_slots = 32, caller_top = 8;
struct CallerSlot {
    std::uint32_t site, caller, count;
};
struct CallerTable {
    CallerSlot slots[caller_slots];
    std::uint32_t used, total, dropped;
};
inline void caller_add(CallerTable& t, std::uint32_t site, std::uint32_t caller) {
    ++t.total;
    for (unsigned i = 0; i < t.used; ++i)
        if (t.slots[i].site == site && t.slots[i].caller == caller) {
            ++t.slots[i].count;
            return;
        }
    if (t.used == caller_slots) {
        ++t.dropped;
        return;
    }
    t.slots[t.used++] = {site, caller, 1};
}
// Sorts the used slots by count, descending (ties keep insertion order); at most 32.
inline void caller_sort(CallerTable& t) {
    for (unsigned i = 1; i < t.used; ++i) {
        const CallerSlot s = t.slots[i];
        unsigned j = i;
        while (j > 0 && t.slots[j - 1].count < s.count) {
            t.slots[j] = t.slots[j - 1];
            --j;
        }
        t.slots[j] = s;
    }
}
}
