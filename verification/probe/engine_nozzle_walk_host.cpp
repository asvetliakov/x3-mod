// Host driver of the node-sourced engine nozzles' portable core (src/proxy/engine_nozzle_walk_core.h;
// docs/architecture/engine-nozzle-source.md). Runs the scenarios of verification/analysis/test_engine_nozzle_walk.py
// over a synthetic memory image (an arena at a virtual base, read through the core's Reader with explicit unreadable
// holes) and prints one JSON object: the option parser, a root's child list walk (the hull part, a drawn main jet, an
// undrawn main jet, a hidden jet, an RCS jet, a jet with a wrong back-pointer, a jet with a non-unit scale, a jet
// with a throttle out of range, an engine-culled jet, a far-copied jet, the sentinel), the dedupe order (draw, far,
// culled, then node) through engine_far_jets_core.h Seen, the undrawn jet's record through far_record with the
// live throttle, the 256 bound, an unreadable element, an empty list, the root set's capacity and identity, the
// light's hold rule with a walked root (engine_light_core.h hold_ships), and the walk's cost over 256 roots of 108
// children (the ledger figure). No Windows dependency, no game bytes.
#include "../../src/proxy/engine_nozzle_walk_core.h"
#include "../../src/proxy/engine_light_core.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
namespace nz = x3m::engine_nozzle::core;
namespace fj = x3m::engine_far_jets::core;
namespace ee = x3m::engine_effects::core;
namespace el = x3m::engine_light::core;

namespace {
// The image: an arena at `base`; reads are refused outside it and inside the holes.
struct Image {
    static constexpr std::uint32_t base = 0x10000000u;
    std::vector<std::uint8_t> bytes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> holes; // [begin, end)
    std::uint32_t next = base + 0x1000;                          // the next free address (16-byte aligned)
    explicit Image(std::size_t size) : bytes(size, 0) {}
    std::uint32_t alloc(unsigned size) {
        const std::uint32_t at = next;
        next += (size + 15u) & ~15u;
        if (next - base > bytes.size()) std::abort();
        return at;
    }
    std::uint32_t* at(std::uint32_t address) { return reinterpret_cast<std::uint32_t*>(bytes.data() + (address - base)); }
    static bool read(void* context, std::uint32_t address, void* out, unsigned size) {
        const Image& im = *static_cast<const Image*>(context);
        if (address < base || std::uint64_t(address) + size > std::uint64_t(base) + im.bytes.size()) return false;
        for (const auto& h : im.holes)
            if (address < h.second && address + size > h.first) return false;
        std::memcpy(out, im.bytes.data() + (address - base), size);
        return true;
    }
};
// One node block of the image: the list link at +0, the back-pointer, the handle, the scales, the identity basis, a
// position, the model and the flag words.
struct NodeSpec {
    std::uint32_t handle, model = 20000, parent, flags12c = 0, flags130 = nz::jet_pair;
    std::uint32_t scale70 = 40, scale80 = nz::scale_one, scale84 = nz::scale_one, scale88 = 0x4000; // z 0.25
    std::int32_t position[3] = {0, 0, 0};
};
std::uint32_t make_node(Image& im, const NodeSpec& n) {
    const std::uint32_t at = im.alloc(nz::node_bytes);
    std::uint32_t* b = im.at(at);
    b[nz::parent_offset / 4] = n.parent;
    b[nz::handle_offset / 4] = n.handle;
    b[nz::scale70_offset / 4] = n.scale70;
    b[nz::scale80_offset / 4] = n.scale80;
    b[nz::scale84_offset / 4] = n.scale84;
    b[nz::scale88_offset / 4] = n.scale88;
    for (unsigned i = 0; i < 3; ++i) b[nz::position_offset / 4 + i] = std::uint32_t(n.position[i]);
    b[0xc0 / 4] = b[0xd0 / 4 + 1] = b[0xe0 / 4 + 2] = nz::scale_one; // basis rows 16.16: identity
    b[nz::flags12c_offset / 4] = n.flags12c;
    b[nz::flags130_offset / 4] = n.flags130;
    b[nz::model_offset / 4] = n.model;
    return at;
}
// Links `nodes` under `root` in order, ending at a fresh sentinel (a 16-byte block whose +0 is 0).
void link(Image& im, std::uint32_t root, const std::vector<std::uint32_t>& nodes) {
    const std::uint32_t sentinel = im.alloc(16);
    *im.at(sentinel) = 0;
    std::uint32_t prev = 0;
    for (std::uint32_t n : nodes) {
        if (prev) *im.at(prev) = n;
        else *im.at(root + nz::child_offset) = n;
        prev = n;
    }
    if (prev) *im.at(prev) = sentinel;
    else *im.at(root + nz::child_offset) = sentinel;
}
unsigned checks = 0, failures = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}
double now_us() {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

int main() {
    std::printf("{");
    // ---- the option
    {
        nz::Source s = nz::Source::draw;
        const bool node = nz::parse_source("node", 4, &s) && s == nz::Source::node;
        const bool draw = nz::parse_source("draw", 4, &s) && s == nz::Source::draw;
        nz::Source keep = nz::Source::node;
        const bool refused = !nz::parse_source("nodes", 5, &keep) && !nz::parse_source("", 0, &keep) &&
                             !nz::parse_source("Node", 4, &keep) && !nz::parse_source("dra", 3, &keep) && keep == nz::Source::node;
        const wchar_t wide[] = L"draw";
        nz::Source w = nz::Source::node;
        const bool wide_ok = nz::parse_source(wide, 4, &w) && w == nz::Source::draw;
        check(node && draw && refused && wide_ok, "parse_source");
        std::printf("\"option\":{\"node\":%u,\"draw\":%u,\"refused\":%u,\"wide\":%u,\"default\":\"%s\"},", unsigned(node),
                    unsigned(draw), unsigned(refused), unsigned(wide_ok), nz::source_name(nz::default_source));
    }
    // ---- one ship's list: every kind of element the guards see
    {
        Image im(1 << 20);
        const std::uint32_t root = im.alloc(nz::node_bytes), view = 9;
        NodeSpec hull{0x50, 0x11, root};
        hull.flags130 = 0; // not a jet
        NodeSpec a{0x5a, 20000, root};          // drawn this frame (a draw record)
        NodeSpec b{0x5b, 20000, root};          // undrawn: the node record
        b.scale88 = 0x20000;                     // z 2.0 -> s 1.0, the live throttle
        b.position[0] = -30; b.position[1] = 20; b.position[2] = 20;
        NodeSpec c{0x5c, 20000, root};          // hidden by the drive
        c.flags12c = nz::hidden_flag;
        NodeSpec rcs{0x5d, ee::steering_model, root}; // v/00566: emitted by the walk, refused by far_record
        NodeSpec d{0x5e, 20000, 0x1234};        // wrong back-pointer
        NodeSpec e{0x5f, 20000, root};          // non-unit x scale
        e.scale80 = 0x8000;
        NodeSpec f{0x60, 20000, root};          // throttle beyond the brake ceiling
        f.scale88 = 0xa0000;
        NodeSpec f0{0x63, 20000, root};         // throttle below the drive's start value
        f0.scale88 = 0x146;
        NodeSpec g{0x61, 20000, root};          // engine-culled at the site (the handler's list)
        NodeSpec h{0x62, 20000, root};          // a far copy of the frame
        const std::vector<std::uint32_t> nodes = {make_node(im, hull), make_node(im, a), make_node(im, b), make_node(im, c),
                                                  make_node(im, rcs), make_node(im, d), make_node(im, e), make_node(im, f),
                                                  make_node(im, f0), make_node(im, g), make_node(im, h)};
        link(im, root, nodes);
        // The dedupe: the ring's draw record (a) and far copy (h) first, then the handler's culled pair (g).
        auto seen = std::make_unique<fj::Seen>();
        seen->begin();
        check(seen->insert(0x5a, view) && seen->insert(0x62, view) && seen->insert(0x61, view), "prime the dedupe");
        nz::WalkStats st;
        unsigned emitted = 0, duplicates = 0, records = 0, steering = 0;
        std::vector<std::uint32_t> order;
        ee::Record record{};
        const bool complete = nz::walk(root, view, &Image::read, &im, &st, [&](const fj::Raw& raw) {
            ++emitted;
            order.push_back(raw.handle);
            if (!seen->insert(raw.handle, raw.view_handle)) {
                ++duplicates;
                return;
            }
            ee::Record r{};
            const ee::FarVerdict v = ee::far_record(raw, 0.01f, -1, nullptr, 7, &r);
            if (v == ee::FarVerdict::steering) {
                ++steering;
                return;
            }
            check(v == ee::FarVerdict::record, "the undrawn jet's record");
            r.flags = std::uint16_t((r.flags & ~std::uint16_t(ee::flag_far)) | ee::flag_node); // the stage's retag
            record = r;
            ++records;
        });
        check(complete, "the list's end reached");
        check(st.children == 11 && st.jets == 10 && st.hidden == 1 && st.guard_parent == 1 && st.guard_scale == 1 &&
                  st.guard_throttle == 2 && st.emitted == 5 && !st.overflow && !st.unreadable,
              "the walk's counts");
        check(emitted == 5 && duplicates == 3 && records == 1 && steering == 1, "draw, far and culled win; one node record");
        check(order == std::vector<std::uint32_t>{0x5a, 0x5b, 0x5d, 0x61, 0x62}, "list order");
        const bool numbers = record.node_handle == 0x5b && record.model == 20000 && record.s == 1.f && record.z == 2.f &&
                             std::fabs(record.origin[0] + .3f) < 1e-6f && std::fabs(record.origin[1] - .2f) < 1e-6f &&
                             std::fabs(record.origin[2] - .2f) < 1e-6f && std::fabs(record.size - .4f) < 1e-6f &&
                             record.axis[2] == -1.f && record.frame == 7 && record.serial == 0;
        check(numbers, "the record's numbers: origin = position x 0.01, size = 40 x 0.01, s from +0x88");
        check((record.flags & ee::flag_node) && !(record.flags & ee::flag_far) && !(record.flags & ee::flag_steering) &&
                  (record.flags & ee::flag_unknown_body),
              "flag_node set, flag_far clear");
        // The same walk with the drive hiding the undrawn jet: no record (hidden counts), the walk still complete.
        im.at(nodes[2])[nz::flags12c_offset / 4] = nz::hidden_flag;
        seen->begin();
        seen->insert(0x5a, view);
        seen->insert(0x62, view);
        seen->insert(0x61, view);
        unsigned records_hidden = 0;
        nz::WalkStats st2;
        const bool complete2 = nz::walk(root, view, &Image::read, &im, &st2, [&](const fj::Raw& raw) {
            if (seen->insert(raw.handle, raw.view_handle) && raw.model != ee::steering_model) ++records_hidden;
        });
        check(complete2 && st2.hidden == 2 && records_hidden == 0 && st2.emitted == 4, "the hidden flag drops the record");
        std::printf("\"walk\":{\"complete\":%u,\"children\":%u,\"jets\":%u,\"hidden\":%u,\"guard_parent\":%u,\"guard_scale\":%u,"
                    "\"guard_throttle\":%u,\"emitted\":%u,\"duplicates\":%u,\"records\":%u,\"steering\":%u,\"order\":[",
                    unsigned(complete), st.children, st.jets, st.hidden, st.guard_parent, st.guard_scale, st.guard_throttle,
                    emitted, duplicates, records, steering);
        for (std::size_t i = 0; i < order.size(); ++i) std::printf("%s%u", i ? "," : "", order[i]);
        std::printf("],\"record\":{\"handle\":%u,\"s\":%.3f,\"z\":%.3f,\"origin\":[%.4f,%.4f,%.4f],\"size\":%.4f,\"flags\":%u,"
                    "\"flag_node\":%u,\"flag_far\":%u},\"hidden_again\":{\"hidden\":%u,\"records\":%u}},",
                    record.node_handle, double(record.s), double(record.z), double(record.origin[0]), double(record.origin[1]),
                    double(record.origin[2]), double(record.size), unsigned(record.flags), unsigned(ee::flag_node),
                    unsigned(ee::flag_far), st2.hidden, records_hidden);
    }
    // ---- the bound, an unreadable element, an empty list, a root whose +0xc is unreadable
    {
        Image im(1 << 22);
        const std::uint32_t root = im.alloc(nz::node_bytes);
        std::vector<std::uint32_t> nodes;
        for (unsigned i = 0; i < 300; ++i) {
            NodeSpec j{0x100 + i, 20000, root};
            nodes.push_back(make_node(im, j));
        }
        link(im, root, nodes);
        nz::WalkStats st;
        unsigned emitted = 0;
        const bool complete = nz::walk(root, 9, &Image::read, &im, &st, [&](const fj::Raw&) { ++emitted; });
        check(!complete && st.overflow == 1 && st.children == nz::walk_bound && emitted == nz::walk_bound && !st.unreadable,
              "the 256 bound cuts the list, the part read is used");
        // A list whose fourth element is unreadable: three read, cut, not complete.
        const std::uint32_t root2 = im.alloc(nz::node_bytes);
        std::vector<std::uint32_t> short_list;
        for (unsigned i = 0; i < 6; ++i) {
            NodeSpec j{0x200 + i, 20000, root2};
            short_list.push_back(make_node(im, j));
        }
        link(im, root2, short_list);
        im.holes.push_back({short_list[3], short_list[3] + nz::node_bytes});
        nz::WalkStats st2;
        unsigned emitted2 = 0;
        const bool complete2 = nz::walk(root2, 9, &Image::read, &im, &st2, [&](const fj::Raw&) { ++emitted2; });
        check(!complete2 && st2.unreadable == 1 && st2.children == 3 && emitted2 == 3 && !st2.overflow,
              "an unreadable element cuts the list");
        // The next pointer of the third element unreadable (a hole over its first word only): two read.
        im.holes.clear();
        im.holes.push_back({short_list[2], short_list[2] + 4});
        nz::WalkStats st3;
        const bool complete3 = nz::walk(root2, 9, &Image::read, &im, &st3, [&](const fj::Raw&) {});
        check(!complete3 && st3.unreadable == 1 && st3.children == 2, "an unreadable next pointer cuts the list");
        // The flag pair of the third element unreadable: the element counted, the walk cut there.
        im.holes.clear();
        im.holes.push_back({short_list[2] + nz::flags12c_offset, short_list[2] + nz::flags12c_offset + 8});
        nz::WalkStats st3b;
        const bool complete3b = nz::walk(root2, 9, &Image::read, &im, &st3b, [&](const fj::Raw&) {});
        check(!complete3b && st3b.unreadable == 1 && st3b.children == 3 && st3b.jets == 2, "an unreadable flag pair cuts the list");
        im.holes.clear();
        // An empty list: the root's +0xc is the sentinel.
        const std::uint32_t root3 = im.alloc(nz::node_bytes);
        link(im, root3, {});
        nz::WalkStats st4;
        const bool complete4 = nz::walk(root3, 9, &Image::read, &im, &st4, [&](const fj::Raw&) {});
        check(complete4 && st4.children == 0 && !st4.unreadable, "an empty list is complete");
        // A root outside the image: nothing read, not complete, unreadable.
        nz::WalkStats st5;
        const bool complete5 = nz::walk(0x7fff0000u, 9, &Image::read, &im, &st5, [&](const fj::Raw&) {});
        check(!complete5 && st5.unreadable == 1 && st5.children == 0, "an unreadable root");
        // The sentinel is told by its next pointer alone: a sentinel 4 bytes before the image's end still ends the list.
        const std::uint32_t root4 = im.alloc(nz::node_bytes);
        NodeSpec j{0x300, 20000, root4};
        const std::uint32_t only = make_node(im, j);
        const std::uint32_t tail_sentinel = Image::base + std::uint32_t(im.bytes.size()) - 4;
        *im.at(root4 + nz::child_offset) = only;
        *im.at(only) = tail_sentinel;
        *im.at(tail_sentinel) = 0;
        nz::WalkStats st6;
        unsigned emitted6 = 0;
        const bool complete6 = nz::walk(root4, 9, &Image::read, &im, &st6, [&](const fj::Raw&) { ++emitted6; });
        check(complete6 && st6.children == 1 && emitted6 == 1, "a sentinel at the image's end (no block read past it)");
        std::printf("\"bound\":{\"complete\":%u,\"overflow\":%u,\"children\":%u,\"emitted\":%u,\"unreadable\":{\"complete\":%u,"
                    "\"children\":%u,\"unreadable\":%u},\"next_unreadable_children\":%u,\"empty_complete\":%u,"
                    "\"bad_root_unreadable\":%u,\"tail_sentinel_complete\":%u},",
                    unsigned(complete), st.overflow, st.children, emitted, unsigned(complete2), st2.children, st2.unreadable,
                    st3.children, unsigned(complete4), st5.unreadable, unsigned(complete6));
    }
    // ---- the root set: identity, capacity, generations
    {
        auto roots = std::make_unique<nz::RootSet>();
        roots->begin();
        unsigned inserted = 0, repeated = 0;
        for (unsigned i = 0; i < 300; ++i) inserted += roots->insert(0x1000 + i * 0x160, 9, 0x2000, 0x3000);
        for (unsigned i = 0; i < 300; ++i) repeated += roots->insert(0x1000 + i * 0x160, 9, 0x2000, 0x3000);
        // Overflow: the 44 roots beyond the capacity, once per attempt (the second pass retries them; a root present
        // already is not an overflow).
        const unsigned overflow = roots->overflow;
        const bool capacity = inserted == nz::RootSet::capacity && repeated == 0 && roots->count == nz::RootSet::capacity &&
                              overflow == 2 * (300 - nz::RootSet::capacity);
        const int found = roots->find(0x1000 + 5 * 0x160), missing = roots->find(0x1000 + 299 * 0x160), zero = roots->find(0);
        const bool identity = found >= 0 && roots->root[found] == 0x1000 + 5 * 0x160 && roots->camera_handle[found] == 9 &&
                              missing < 0 && zero < 0 && !roots->insert(0, 9, 0, 0);
        roots->begin();
        const bool fresh = roots->count == 0 && roots->find(0x1000 + 5 * 0x160) < 0 && roots->insert(0x1000 + 5 * 0x160, 9, 0, 0);
        // Insertion order survives: the frame's walk visits roots in the order their hulls were drawn.
        roots->begin();
        roots->insert(0x30, 1, 0, 0);
        roots->insert(0x20, 1, 0, 0);
        roots->insert(0x10, 1, 0, 0);
        const bool ordered = roots->count == 3 && roots->root[roots->order[0]] == 0x30 && roots->root[roots->order[1]] == 0x20 &&
                             roots->root[roots->order[2]] == 0x10;
        check(capacity && identity && fresh && ordered, "the root set");
        // The ship filter (review S1): a root is a ship for recent_window frames after a record named it (frame 0
        // included), the own ship aside; a new mark extends it; unknown and zero roots never qualify; 600 marks over
        // 512 slots evict the stalest of a probe run and keep the latest.
        auto ships = std::make_unique<nz::ShipRoots>();
        ships->mark(0x5000, 0);
        ships->mark(0x6000, 5);
        // A mark at frame N qualifies N .. N + 2: the next frame's gather, plus one frame whose stage did not run.
        const bool window = ships->recent(0x5000, 0) && ships->recent(0x5000, 2) && !ships->recent(0x5000, 3) &&
                            ships->recent(0x6000, 5) && ships->recent(0x6000, 7) && !ships->recent(0x6000, 8) &&
                            !ships->recent(0x7000, 5) && !ships->recent(0, 5);
        ships->mark(0x6000, 20);
        const bool extended = ships->recent(0x6000, 22) && !ships->recent(0x6000, 23);
        for (unsigned i = 0; i < 600; ++i) ships->mark(0x9000 + i * 0x160, 100 + i);
        bool latest = true;
        for (unsigned i = 590; i < 600; ++i) latest = latest && ships->recent(0x9000 + i * 0x160, 100 + i);
        check(window && extended && latest, "the ship roots");
        std::printf("\"roots\":{\"capacity\":%u,\"inserted\":%u,\"repeated\":%u,\"overflow\":%u,\"identity\":%u,\"fresh\":%u,\"ordered\":%u,"
                    "\"ship_window\":%u,\"ship_extended\":%u,\"ship_latest\":%u},",
                    nz::RootSet::capacity, inserted, repeated, overflow, unsigned(identity), unsigned(fresh), unsigned(ordered),
                    unsigned(window), unsigned(extended), unsigned(latest));
    }
    // ---- the hold rule: a walked root with no record is never held; an unwalked one is held as before
    {
        auto prev = std::make_unique<el::ShipTable>(), cur = std::make_unique<el::ShipTable>();
        auto log = std::make_unique<el::DrawLog>();
        prev->clear();
        cur->clear();
        log->clear();
        const float world[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        const std::uint32_t roots[2] = {0x7000, 0x8000};
        for (unsigned i = 0; i < 2; ++i) {
            el::Light& l = prev->lights[prev->count];
            l = el::Light{};
            l.root = roots[i];
            l.handle = 0x5a + i;
            l.brightness = l.bound_brightness = 1.f;
            l.radius = 1.2f;
            l.anchor_count = 1;
            l.anchors[0].node = roots[i];
            l.anchors[0].handle = 0x70 + i;
            for (unsigned j = 0; j < 12; ++j) l.anchors[0].rows[j] = world[j];
            l.plate_count = 1;
            l.plates[0].position[0] = .3;
            l.plates[0].handle = l.handle;
            unsigned h = el::hash_key(roots[i], 0, el::ship_slots - 1);
            while (prev->slot[h]) h = (h + 1) & (el::ship_slots - 1);
            prev->slot[h] = std::uint16_t(prev->count + 1);
            ++prev->count;
            log->push(roots[i], 0, 0x70 + i, world); // both hulls drawn again
        }
        const std::uint32_t walked[1] = {roots[0]};
        el::hold_ships(*prev, cur.get(), *log, 60, walked, 1);
        const bool rule = cur->count == 1 && cur->lights[0].root == roots[1] && cur->stats.held == 1 && cur->stats.hold_walked == 1 &&
                          el::find_ship(*cur, roots[0]) < 0 && el::find_ship(*cur, roots[1]) == 0;
        cur->clear();
        el::hold_ships(*prev, cur.get(), *log, 60);
        const bool unchanged = cur->count == 2 && cur->stats.held == 2 && cur->stats.hold_walked == 0;
        check(rule && unchanged, "the hold skips a walked root; without a walked set it holds as before");
        std::printf("\"hold\":{\"walked_skipped\":%u,\"held_unwalked\":%u,\"without_walked_held\":%u},", unsigned(rule),
                    unsigned(rule), unsigned(unchanged));
    }
    // ---- cost: 256 roots of 108 children (the installed maximum), 4 of them main jets (the capitals' share), through a
    // reader the compiler cannot inline (an opaque pointer) with the results sunk: contiguous nodes (each ship's parts
    // one after another, the prefetcher's case) and scattered nodes (a random permutation of the same blocks: every
    // hop a cache miss, the conservative figure), a warm root, and the root set's probe per routed draw.
    {
        constexpr unsigned ships = 256, parts = 108;
        volatile nz::Reader opaque_reader = &Image::read;
        volatile unsigned long long sink = 0;
        const auto measure = [&](bool scattered, double* per_root, double* per_root_mean, double* warm) {
            const unsigned blocks = ships * (parts + 2);
            Image im(std::size_t(blocks) * nz::node_bytes + (1 << 16));
            std::vector<std::uint32_t> slots(blocks);
            for (unsigned i = 0; i < blocks; ++i) slots[i] = im.alloc(nz::node_bytes);
            if (scattered) { // a fixed permutation (LCG shuffle): the layout is the same on every run
                std::uint32_t state = 0x2545f491u;
                for (unsigned i = blocks - 1; i > 0; --i) {
                    state = state * 1664525u + 1013904223u;
                    const unsigned j = (state >> 8) % (i + 1);
                    std::swap(slots[i], slots[j]);
                }
            }
            std::vector<std::uint32_t> roots;
            unsigned slot = 0;
            for (unsigned s = 0; s < ships; ++s) {
                const std::uint32_t root = slots[slot++];
                std::vector<std::uint32_t> nodes;
                for (unsigned i = 0; i < parts; ++i) {
                    NodeSpec j{0x1000 + s * parts + i, 20000, root};
                    if (i % 27 != 0) j.flags130 = 0;
                    const std::uint32_t at = slots[slot++];
                    std::uint32_t* b = im.at(at);
                    b[nz::parent_offset / 4] = j.parent;
                    b[nz::handle_offset / 4] = j.handle;
                    b[nz::scale70_offset / 4] = j.scale70;
                    b[nz::scale80_offset / 4] = j.scale80;
                    b[nz::scale84_offset / 4] = j.scale84;
                    b[nz::scale88_offset / 4] = j.scale88;
                    b[0xc0 / 4] = b[0xd0 / 4 + 1] = b[0xe0 / 4 + 2] = nz::scale_one;
                    b[nz::flags130_offset / 4] = j.flags130;
                    b[nz::model_offset / 4] = j.model;
                    nodes.push_back(at);
                }
                const std::uint32_t sentinel = slots[slot++];
                *im.at(sentinel) = 0;
                *im.at(root + nz::child_offset) = nodes[0];
                for (unsigned i = 0; i + 1 < parts; ++i) *im.at(nodes[i]) = nodes[i + 1];
                *im.at(nodes[parts - 1]) = sentinel;
                roots.push_back(root);
            }
            auto seen = std::make_unique<fj::Seen>();
            const unsigned rounds = 40;
            double best = 1e30, total = 0;
            unsigned long long emitted = 0, children = 0;
            for (unsigned r = 0; r < rounds; ++r) {
                seen->begin();
                const double t0 = now_us();
                for (std::uint32_t root : roots) {
                    nz::WalkStats st;
                    nz::walk(root, 9, opaque_reader, &im, &st, [&](const fj::Raw& raw) {
                        emitted += seen->insert(raw.handle, raw.view_handle);
                    });
                    children += st.children;
                }
                const double dt = now_us() - t0;
                total += dt;
                best = dt < best ? dt : best;
            }
            sink = sink + emitted + children;
            check(emitted == rounds * ships * 4ull && children == rounds * ships * parts, "the cost run walked every part once per round");
            *per_root = best / ships;
            *per_root_mean = total / rounds / ships;
            double warm_best = 1e30;
            for (unsigned r = 0; r < 200; ++r) {
                const double t0 = now_us();
                unsigned long long c = 0;
                for (unsigned k = 0; k < 20; ++k) {
                    nz::WalkStats st;
                    nz::walk(roots[0], 9, opaque_reader, &im, &st, [&](const fj::Raw& raw) { c += raw.handle; });
                    c += st.children;
                }
                const double dt = (now_us() - t0) / 20;
                sink = sink + c;
                warm_best = dt < warm_best ? dt : warm_best;
            }
            *warm = warm_best;
        };
        double contiguous = 0, contiguous_mean = 0, contiguous_warm = 0, scattered = 0, scattered_mean = 0, scattered_warm = 0;
        measure(false, &contiguous, &contiguous_mean, &contiguous_warm);
        measure(true, &scattered, &scattered_mean, &scattered_warm);
        // The root set's probe: 250 routed draws of 5 ships per frame (every draw a hit after its ship's first).
        auto set = std::make_unique<nz::RootSet>();
        double probe_best = 1e30;
        unsigned long long probes = 0;
        for (unsigned r = 0; r < 200; ++r) {
            set->begin();
            const double t0 = now_us();
            for (unsigned d = 0; d < 250; ++d) probes += set->insert(0x1000 + (d % 5) * 0x160, 9, 0x2000, 0x3000);
            const double dt = now_us() - t0;
            probe_best = dt < probe_best ? dt : probe_best;
        }
        sink = sink + probes;
        std::printf("\"cost\":{\"roots\":%u,\"children\":%u,\"jets_per_root\":%u,\"us_per_root\":%.3f,\"us_per_root_mean\":%.3f,"
                    "\"ns_per_child\":%.1f,\"us_per_root_warm\":%.3f,\"us_per_root_contiguous\":%.3f,\"us_per_root_contiguous_mean\":%.3f,"
                    "\"us_per_root_contiguous_warm\":%.3f,\"us_per_frame_2_ships\":%.3f,\"us_per_frame_5_ships\":%.3f,"
                    "\"us_per_frame_256_ships\":%.1f,\"probe_ns_per_draw\":%.1f,\"rounds\":40},",
                    ships, parts, 4u, scattered, scattered_mean, scattered * 1000. / parts, scattered_warm, contiguous, contiguous_mean,
                    contiguous_warm, 2 * scattered, 5 * scattered, scattered * ships, probe_best * 1000. / 250);
    }
    std::printf("\"checks\":%u,\"failures\":%u}\n", checks, failures);
    return failures ? 1 : 0;
}
