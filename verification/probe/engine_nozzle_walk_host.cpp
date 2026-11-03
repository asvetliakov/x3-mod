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
#include "../../src/proxy/engine_node_layout.h"
#include <cmath>
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
    // ---- hidden-emitter anchors (plumes-no-glow-hulls.md step 1) on both edition layouts
    {
        // A node block as far as C (+0x260): the model at the layout's offset, a decoy model at the other edition's.
        struct Ext {
            std::uint32_t handle, model, parent, flags12c = nz::hidden_flag, flags130 = nz::jet_pair, mode = 0;
            std::uint32_t scale70 = 17, scale80 = nz::scale_one, scale84 = nz::scale_one, scale88 = 0x10000; // z 1.0
            std::int32_t position[3] = {0, 0, 0};
            std::int32_t bx[3] = {0x10000, 0, 0}, bz[3] = {0, 0, 0x10000};
        };
        constexpr std::uint32_t emitter_model = 30000, decoy_model = 31000, glow_model = 20000;
        const auto make_ext = [](Image& im, const Ext& n, unsigned model_offset, unsigned decoy_offset) {
            const std::uint32_t at = im.alloc(0x270);
            std::uint32_t* b = im.at(at);
            b[nz::parent_offset / 4] = n.parent;
            b[nz::handle_offset / 4] = n.handle;
            b[nz::scale70_offset / 4] = n.scale70;
            b[nz::scale80_offset / 4] = n.scale80;
            b[nz::scale84_offset / 4] = n.scale84;
            b[nz::scale88_offset / 4] = n.scale88;
            for (unsigned i = 0; i < 3; ++i) {
                b[nz::position_offset / 4 + i] = std::uint32_t(n.position[i]);
                b[nz::basis_x_offset / 4 + i] = std::uint32_t(n.bx[i]);
                b[nz::basis_z_offset / 4 + i] = std::uint32_t(n.bz[i]);
            }
            b[0xd0 / 4 + 1] = nz::scale_one;
            b[nz::flags12c_offset / 4] = n.flags12c;
            b[nz::flags130_offset / 4] = n.flags130;
            b[decoy_offset / 4] = decoy_model;
            b[model_offset / 4] = n.model;
            b[nz::mode_offset / 4] = n.mode;
            return at;
        };
        struct Predicates {
            static bool emitter(void*, std::uint32_t model) { return model == emitter_model; }
            static bool steering(void*, std::uint32_t model) { return model == ee::steering_model; }
        };
        const auto options = [&](unsigned model_offset) {
            nz::Options o;
            o.anchors = true;
            o.model_offset = model_offset;
            o.is_emitter = &Predicates::emitter;
            o.is_steering = &Predicates::steering;
            return o;
        };
        const x3m::engine_node_layout::Layout layouts[2] = {x3m::engine_node_layout::ap, x3m::engine_node_layout::fl};
        const char* names[2] = {"ap", "fl"};
        // The reference size: k(R) from the documented anchors (0.35 at 150, 0.25 at 500, log-linear) x floor_scale 0.5
        // at R = 32000 x 0.01 = 320 record units, in double, independent of floor_ratio_at.
        const double R = 320., k_ref = .5 * (.35 + (.25 - .35) * std::log(R / 150.) / std::log(500. / 150.));
        std::printf("\"anchors\":{");
        for (unsigned e = 0; e < 2; ++e) {
            const unsigned mo = layouts[e].model, other = layouts[1 - e].model;
            Image im(1 << 20);
            const std::uint32_t root = im.alloc(0x270), view = 9;
            NodeSpec hull{0x50, 0x11, root};
            hull.flags130 = 0;
            // The Argon Heavy Centaur's shape: four hidden emitters (C = 0, the M6M family; C = 1, the drones), the
            // second turned to point along -x, the third at full throttle; a fifth hidden C = 0 child of another body
            // (the drones' fx_engine_terran_transportdrone): no emitter identity.
            Ext a{0x70, emitter_model, root};
            a.position[0] = -1000; a.position[1] = 200; a.position[2] = -4882;
            Ext b{0x71, emitter_model, root};
            b.mode = 1;
            b.position[0] = 1000; b.position[2] = -4882;
            b.bx[0] = 0; b.bx[2] = 0x10000; b.bz[0] = 0x10000; b.bz[2] = 0; // model z along +x: the plume along -x
            Ext c{0x72, emitter_model, root};
            c.scale88 = 0x20000; // z 2.0: s 1
            c.position[1] = -300; c.position[2] = -4800;
            Ext d{0x73, emitter_model, root};
            d.position[2] = -4900;
            Ext other_body{0x74, glow_model, root};
            const std::vector<std::uint32_t> nodes = {make_node(im, hull), make_ext(im, a, mo, other), make_ext(im, b, mo, other),
                                                      make_ext(im, c, mo, other), make_ext(im, d, mo, other),
                                                      make_ext(im, other_body, mo, other)};
            link(im, root, nodes);
            nz::WalkStats st;
            std::vector<fj::Raw> anchors;
            unsigned jets = 0;
            const bool complete = nz::walk(root, view, &Image::read, &im, &st, options(mo), [&](const fj::Raw&) { ++jets; },
                                           [&](const fj::Raw& raw) { anchors.push_back(raw); });
            check(complete && st.anchors == 4 && anchors.size() == 4 && jets == 0 && st.hidden == 5 && st.mode_reads == 5 &&
                      st.candidates == 5 && st.not_emitter == 1 && !st.glow && !st.anchor_rejected,
                  "four emitters anchored, the fifth hidden body refused for its identity");
            // The record law (the stage's: far_record, the ship radius, anchor_record).
            ee::Body body{};
            body.value = 17.f;
            body.lists = ee::list_jet;
            body.cluster = ee::grey;
            std::vector<ee::Record> records;
            for (const fj::Raw& raw : anchors) {
                ee::Record r{};
                check(ee::far_record(raw, .01f, 7, &body, 11, &r) == ee::FarVerdict::record, "the emitter's far_record");
                float radius = 0.f, k = 0.f;
                ee::parent_radius_in_record(32000, raw.scale70, raw.scale80, r.size, &radius);
                x3m::engine_plumes::floor_ratio_at(x3m::engine_plumes::default_look, radius, &k);
                check(nz::anchor_record(k, radius, &r), "anchor_record");
                records.push_back(r);
            }
            const ee::Record& r0 = records[0];
            const ee::Record& r1 = records[1];
            const ee::Record& r2 = records[2];
            const bool origin = std::fabs(r0.origin[0] + 10.f) < 1e-4f && std::fabs(r0.origin[1] - 2.f) < 1e-4f &&
                                std::fabs(r0.origin[2] + 48.82f) < 1e-3f;
            const bool axis = r0.axis[0] == 0.f && r0.axis[1] == 0.f && r0.axis[2] == -1.f && r1.axis[0] == -1.f &&
                              r1.axis[1] == 0.f && r1.axis[2] == 0.f;
            const bool size = std::fabs(double(r0.size) - k_ref * R) < 1e-3 && r0.size == r1.size && r0.size == r2.size;
            const bool throttle = std::fabs(r0.s - .75f / 1.75f) < 1e-6f && r0.z == 1.f && r2.s == 1.f && r2.z == 2.f;
            const std::uint16_t want = std::uint16_t(ee::flag_node | nz::flag_anchor | (unsigned(ee::cyan) << ee::cluster_shift));
            const bool flags = r0.flags == want && r0.body == -1 && r0.node_handle == 0x70 && r0.model == emitter_model &&
                               r1.node_handle == 0x71 && records[3].node_handle == 0x73;
            check(origin && axis && size && throttle && flags, "the anchor records' origin, axis, size, throttle and flags");
            // The other edition's offset reads the decoy: no emitter, no anchor.
            nz::WalkStats wrong;
            unsigned wrong_anchors = 0;
            nz::walk(root, view, &Image::read, &im, &wrong, options(other), [](const fj::Raw&) {},
                     [&](const fj::Raw&) { ++wrong_anchors; });
            check(wrong_anchors == 0 && wrong.not_emitter == 5 && wrong.candidates == 5, "the other edition's offset: the decoy");
            // The throttle source: the drive's +0x88 of the emitter (0.25 idle -> 2.0), and the placement value 0x28f of an
            // undriven dummy refused.
            im.at(nodes[1])[nz::scale88_offset / 4] = 0x4000;
            im.at(nodes[4])[nz::scale88_offset / 4] = 0x28f;
            std::vector<fj::Raw> idle;
            nz::WalkStats st_idle;
            nz::walk(root, view, &Image::read, &im, &st_idle, options(mo), [](const fj::Raw&) {},
                     [&](const fj::Raw& raw) { idle.push_back(raw); });
            ee::Record ri{};
            ee::far_record(idle[0], .01f, 7, &body, 12, &ri);
            check(idle.size() == 3 && st_idle.anchor_rejected == 1 && ri.s == 0.f && ri.z == .25f,
                  "s follows the emitter's +0x88; an undriven 0x28f dummy is refused");
            // Anchors only after the list's end: a cut list (an unreadable element after the emitters) gives none.
            im.at(nodes[1])[nz::scale88_offset / 4] = 0x10000;
            im.at(nodes[4])[nz::scale88_offset / 4] = 0x10000;
            im.holes.push_back({nodes[5], nodes[5] + 4});
            nz::WalkStats st_cut;
            unsigned cut_anchors = 0;
            const bool cut_complete = nz::walk(root, view, &Image::read, &im, &st_cut, options(mo), [](const fj::Raw&) {},
                                               [&](const fj::Raw&) { ++cut_anchors; });
            im.holes.clear();
            check(!cut_complete && cut_anchors == 0 && st_cut.unreadable == 1, "an incomplete walk keeps no anchor");
            // The default options (the walk before step 1) see five hidden jets and emit nothing.
            nz::WalkStats st_old;
            unsigned old_emitted = 0;
            nz::walk(root, view, &Image::read, &im, &st_old, [&](const fj::Raw&) { ++old_emitted; });
            check(old_emitted == 0 && st_old.hidden == 5 && st_old.mode_reads == 0 && st_old.anchors == 0,
                  "without anchors the hidden emitters stay dropped, no C read");
            std::printf("%s\"%s\":{\"model_offset\":%u,\"complete\":%u,\"anchors\":%u,\"candidates\":%u,\"not_emitter\":%u,"
                        "\"mode_reads\":%u,\"origin\":[%.4f,%.4f,%.4f],\"axis\":[%.1f,%.1f,%.1f],\"axis_turned\":[%.1f,%.1f,%.1f],"
                        "\"size\":%.4f,\"size_ref\":%.4f,\"s\":[%.4f,%.4f],\"z\":[%.3f,%.3f],\"flags\":%u,\"body\":%d,"
                        "\"decoy_anchors\":%u,\"decoy_not_emitter\":%u,\"idle_s\":%.3f,\"undriven_rejected\":%u,"
                        "\"cut_anchors\":%u,\"old_hidden\":%u}",
                        e ? "," : "", names[e], mo, unsigned(complete), st.anchors, st.candidates, st.not_emitter, st.mode_reads,
                        double(r0.origin[0]), double(r0.origin[1]), double(r0.origin[2]), double(r0.axis[0]), double(r0.axis[1]),
                        double(r0.axis[2]), double(r1.axis[0]), double(r1.axis[1]), double(r1.axis[2]), double(r0.size),
                        k_ref * R, double(r0.s), double(r2.s), double(r0.z), double(r2.z), unsigned(r0.flags), int(r0.body),
                        wrong_anchors, wrong.not_emitter, double(ri.s), st_idle.anchor_rejected, cut_anchors, st_old.hidden);
        }
        // A visible-jet hull (the 181 FL / 172 stock ships that carry emitters beside their glow parts): its records with
        // the rule on are byte-identical to the walk before step 1, and its emitters stay dropped. Also the travel-mode
        // swap (the main set hidden with C 0x7001, nothing shown): glow by C, no anchor; a hull whose only shown jet is
        // an RCS puff: the emitters anchor; a probe walk (jets off) of the glow hull: nothing.
        {
            const unsigned mo = x3m::engine_node_layout::ap.model, other = x3m::engine_node_layout::fl.model;
            Image im(1 << 20);
            const std::uint32_t root = im.alloc(0x270), view = 9;
            Ext main1{0x80, glow_model, root};
            main1.flags12c = 0;
            main1.mode = 0x7001;
            main1.scale70 = 4000;
            main1.position[2] = -5000;
            Ext main2 = main1;
            main2.handle = 0x81;
            main2.scale88 = 0x18000;
            main2.position[0] = 700;
            Ext emitter{0x82, emitter_model, root};
            emitter.mode = 1;
            Ext rcs_hidden{0x83, ee::steering_model, root};
            rcs_hidden.mode = 0x10;
            rcs_hidden.scale88 = 0x28f;
            Ext rcs_shown{0x84, ee::steering_model, root};
            rcs_shown.flags12c = 0;
            rcs_shown.mode = 0x20;
            rcs_shown.scale88 = 0x8000;
            const std::vector<std::uint32_t> nodes = {make_ext(im, emitter, mo, other), make_ext(im, rcs_hidden, mo, other),
                                                      make_ext(im, main1, mo, other), make_ext(im, rcs_shown, mo, other),
                                                      make_ext(im, main2, mo, other)};
            link(im, root, nodes);
            const auto records_of = [&](bool with_rule, nz::WalkStats* st, unsigned* anchors) {
                std::vector<ee::Record> out;
                const auto to_record = [&](const fj::Raw& raw) {
                    ee::Record r{};
                    if (ee::far_record(raw, .01f, -1, nullptr, 21, &r) == ee::FarVerdict::record) out.push_back(r);
                };
                *anchors = 0;
                if (with_rule)
                    nz::walk(root, view, &Image::read, &im, st, options(mo), to_record, [&](const fj::Raw&) { ++*anchors; });
                else
                    nz::walk(root, view, &Image::read, &im, st, to_record);
                return out;
            };
            nz::WalkStats st_old, st_new;
            unsigned anchors_old = 0, anchors_new = 0;
            const std::vector<ee::Record> before = records_of(false, &st_old, &anchors_old);
            const std::vector<ee::Record> after = records_of(true, &st_new, &anchors_new);
            const bool identical = before.size() == 2 && after.size() == before.size() &&
                                   !std::memcmp(before.data(), after.data(), before.size() * sizeof(ee::Record));
            const bool stats_same = st_old.children == st_new.children && st_old.jets == st_new.jets &&
                                    st_old.hidden == st_new.hidden && st_old.guard_parent == st_new.guard_parent &&
                                    st_old.guard_scale == st_new.guard_scale && st_old.guard_throttle == st_new.guard_throttle &&
                                    st_old.emitted == st_new.emitted && st_old.unreadable == st_new.unreadable;
            check(identical && stats_same && anchors_new == 0 && st_new.glow == 1, "a visible-jet hull: records byte-identical, no anchor");
            // The travel-mode swap: the main pair hidden (C 0x7001 keeps the visible bit), the RCS puff hidden too.
            for (unsigned i : {2u, 3u, 4u}) im.at(nodes[i])[nz::flags12c_offset / 4] = nz::hidden_flag;
            nz::WalkStats st_travel;
            unsigned travel_anchors = 0;
            records_of(true, &st_travel, &travel_anchors);
            check(travel_anchors == 0 && st_travel.glow == 1, "a hidden 0x1000 main set is a glow part");
            // Only an RCS puff shown (a steering body) beside the emitter: not glow; the emitter anchors.
            im.at(nodes[2])[nz::flags130_offset / 4] = 0;
            im.at(nodes[4])[nz::flags130_offset / 4] = 0;
            im.at(nodes[3])[nz::flags12c_offset / 4] = 0;
            nz::WalkStats st_rcs;
            unsigned rcs_anchors = 0;
            records_of(true, &st_rcs, &rcs_anchors);
            check(rcs_anchors == 1 && st_rcs.glow == 0, "a shown RCS puff is no glow part");
            // A probe walk of the glow hull (main jets restored): no jet record, no anchor.
            im.at(nodes[2])[nz::flags130_offset / 4] = nz::jet_pair;
            im.at(nodes[4])[nz::flags130_offset / 4] = nz::jet_pair;
            im.at(nodes[2])[nz::flags12c_offset / 4] = 0;
            im.at(nodes[4])[nz::flags12c_offset / 4] = 0;
            nz::Options probe = options(mo);
            probe.jets = false;
            nz::WalkStats st_probe;
            unsigned probe_jets = 0, probe_anchors = 0;
            nz::walk(root, view, &Image::read, &im, &st_probe, probe, [&](const fj::Raw&) { ++probe_jets; },
                     [&](const fj::Raw&) { ++probe_anchors; });
            check(probe_jets == 0 && probe_anchors == 0 && st_probe.glow == 1 && st_probe.emitted == 0, "a probe walk emits anchors only");
            std::printf(",\"parity\":{\"records\":%u,\"identical\":%u,\"stats_same\":%u,\"anchors\":%u,\"glow\":%u,"
                        "\"travel_anchors\":%u,\"rcs_only_anchors\":%u,\"probe_jets\":%u,\"probe_anchors\":%u}",
                        unsigned(after.size()), unsigned(identical), unsigned(stats_same), anchors_new, st_new.glow,
                        travel_anchors, rcs_anchors, probe_jets, probe_anchors);
        }
        // The ship-only gate: a non-ship root (a station, a missile, debris) whose hidden emitters placement left at
        // (1, 1, 0x28f) and no drive ever moved (class 7 alone is driven): no anchor, every emitter rejected.
        {
            const unsigned mo = x3m::engine_node_layout::ap.model, other = x3m::engine_node_layout::fl.model;
            Image im(1 << 20);
            const std::uint32_t root = im.alloc(0x270);
            Ext e1{0x90, emitter_model, root};
            e1.scale88 = 0x28f;
            Ext e2 = e1;
            e2.handle = 0x91;
            e2.mode = 1;
            link(im, root, {make_ext(im, e1, mo, other), make_ext(im, e2, mo, other)});
            nz::WalkStats st;
            unsigned n = 0;
            const bool complete = nz::walk(root, 9, &Image::read, &im, &st, options(mo), [](const fj::Raw&) {},
                                           [&](const fj::Raw&) { ++n; });
            check(complete && n == 0 && st.anchor_rejected == 2 && st.candidates == 2 && !st.glow, "a non-ship root's undriven emitters");
            std::printf(",\"non_ship\":{\"anchors\":%u,\"rejected\":%u}", n, st.anchor_rejected);
        }
        // The probe table: due at first sight, not after a walk without anchors, again after retry_ms of game time
        // (frames to retry at 50 fps, 20 ms a frame: SETA 1x vs 10x), or retry_frames without the clock; an anchored root
        // always due; a load clears it; note returns the last verdict.
        {
            auto probes = std::make_unique<nz::AnchorProbes>();
            const std::uint32_t r = 0x4000;
            const bool first = probes->due(r, 10, 0.);
            const auto none = probes->note(r, 10, 0., nz::AnchorProbes::none);
            const bool after = !probes->due(r, 11, 16.7);
            probes->note(r, 11, 16.7, nz::AnchorProbes::anchored);
            const auto again = probes->note(r, 11, 16.7, nz::AnchorProbes::anchored);
            // An anchored hull skipped one frame (drawn at 10, not at 11: no anchor record, so ShipRoots lapses after
            // frame 12): the gather's next probe at frame 13 is due at once; a glow verdict waits for retry_ms.
            auto ships = std::make_unique<nz::ShipRoots>();
            auto table = std::make_unique<nz::AnchorProbes>();
            const std::uint32_t hull = 0x5000, glow_root = 0x6000;
            ships->mark(hull, 10);
            ships->mark(glow_root, 10);
            table->note(hull, 10, 200., nz::AnchorProbes::anchored);
            table->note(glow_root, 10, 200., nz::AnchorProbes::glow);
            unsigned rewalk = 0, glow_rewalk = 0;
            for (unsigned f = 11; f < 40; ++f) {
                const double clock = 200. + 20. * (f - 10);
                if (!rewalk && !ships->recent(hull, f) && table->due(hull, f, clock)) rewalk = f;
                if (!glow_rewalk && !ships->recent(glow_root, f) && table->due(glow_root, f, clock)) glow_rewalk = f;
            }
            const auto frames_to_retry = [&](double step_ms) {
                auto t = std::make_unique<nz::AnchorProbes>();
                t->note(r, 0, 0., nz::AnchorProbes::none);
                double clock = 0.;
                for (unsigned f = 1; f < 10000; ++f) {
                    clock += step_ms;
                    if (t->due(r, f, clock)) return f;
                }
                return 0u;
            };
            const unsigned seta1 = frames_to_retry(20.), seta10 = frames_to_retry(10. * 20.), stalled = frames_to_retry(0.);
            probes->clear();
            const bool cleared = probes->due(r, 12, 33.);
            check(first && none == nz::AnchorProbes::none && after && again == nz::AnchorProbes::anchored && seta1 == 100 &&
                      seta10 == 10 && stalled == nz::AnchorProbes::retry_frames && cleared && rewalk == 13 && !glow_rewalk,
                  "the probe table; an anchored hull skipped one frame is re-walked at once");
            // The probe's cost per non-recent routed draw (300 draws over 60 roots, half of them present).
            for (unsigned i = 0; i < 30; ++i) probes->note(0x10000 + i * 0x270, 1, 0., nz::AnchorProbes::none);
            volatile unsigned long long hits = 0;
            double best = 1e30;
            for (unsigned round = 0; round < 200; ++round) {
                const double t0 = now_us();
                unsigned h = 0;
                for (unsigned d = 0; d < 300; ++d) h += probes->due(0x10000 + (d % 60) * 0x270, 2, 10.);
                const double dt = now_us() - t0;
                hits = hits + h;
                best = dt < best ? dt : best;
            }
            std::printf(",\"probe\":{\"first_due\":%u,\"after_walk_due\":%u,\"previous\":%u,\"frames_to_retry_seta1\":%u,"
                        "\"frames_to_retry_seta10\":%u,\"frames_to_retry_stalled\":%u,\"cleared_due\":%u,\"ns_per_draw\":%.1f,"
                        "\"anchored_rewalk_frame\":%u,\"glow_rewalk_frame\":%u}}",
                        unsigned(first), unsigned(!after), unsigned(again), seta1, seta10, stalled, unsigned(cleared),
                        best * 1000. / 300, rewalk, glow_rewalk);
        }
        std::printf(",");
    }
    // ---- cost: 256 roots of 108 children (the installed maximum), 4 of them main jets (the capitals' share), through a
    // reader the compiler cannot inline (an opaque pointer) with the results sunk: contiguous nodes (each ship's parts
    // one after another, the prefetcher's case) and scattered nodes (a random permutation of the same blocks: every
    // hop a cache miss, the conservative figure), a warm root, and the root set's probe per routed draw.
    {
        constexpr unsigned ships = 256, parts = 108;
        volatile nz::Reader opaque_reader = &Image::read;
        volatile unsigned long long sink = 0;
        // Step 1's walks over the same image: the rule on (one predicate per shown jet; no hidden jet here, so no C read)
        // and the anchors-only walk of a probe-admitted root.
        struct CostPredicates {
            static bool emitter(void*, std::uint32_t model) { return model == 30000; }
            static bool steering(void*, std::uint32_t model) { return model == ee::steering_model; }
        };
        nz::Options rule_on;
        rule_on.anchors = true;
        rule_on.is_emitter = &CostPredicates::emitter;
        rule_on.is_steering = &CostPredicates::steering;
        nz::Options probe_walk = rule_on;
        probe_walk.jets = false;
        const auto measure = [&](bool scattered, double* per_root, double* per_root_mean, double* warm,
                                 const nz::Options* opt = nullptr, unsigned jets_per_root = 4) {
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
                    const auto emit = [&](const fj::Raw& raw) { emitted += seen->insert(raw.handle, raw.view_handle); };
                    if (opt)
                        nz::walk(root, 9, opaque_reader, &im, &st, *opt, emit, [&](const fj::Raw&) { ++emitted; });
                    else
                        nz::walk(root, 9, opaque_reader, &im, &st, emit);
                    children += st.children;
                }
                const double dt = now_us() - t0;
                total += dt;
                best = dt < best ? dt : best;
            }
            sink = sink + emitted + children;
            check(emitted == rounds * ships * std::uint64_t(jets_per_root) && children == rounds * ships * parts,
                  "the cost run walked every part once per round");
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
        double rule_scattered = 0, rule_mean = 0, rule_warm = 0, probe_scattered = 0, probe_mean = 0, probe_warm = 0;
        measure(true, &rule_scattered, &rule_mean, &rule_warm, &rule_on, 4);
        measure(true, &probe_scattered, &probe_mean, &probe_warm, &probe_walk, 0);
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
                    "\"us_per_frame_256_ships\":%.1f,\"probe_ns_per_draw\":%.1f,\"rounds\":40,\"us_per_root_rule_on\":%.3f,"
                    "\"us_per_root_rule_on_mean\":%.3f,\"us_per_root_probe_walk\":%.3f,\"us_per_root_probe_walk_mean\":%.3f},",
                    ships, parts, 4u, scattered, scattered_mean, scattered * 1000. / parts, scattered_warm, contiguous, contiguous_mean,
                    contiguous_warm, 2 * scattered, 5 * scattered, scattered * ships, probe_best * 1000. / 250, rule_scattered,
                    rule_mean, probe_scattered, probe_mean);
    }
    std::printf("\"checks\":%u,\"failures\":%u}\n", checks, failures);
    return failures ? 1 : 0;
}
