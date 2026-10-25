// Host-only check of the merged-LOD atlas mark parser and pointer table (src/proxy/lod_texture_mark.h;
// docs/architecture/overlay-material-strength.md 4). Builds on any host C++17 compiler; prints one JSON object the
// Python oracle (verification/analysis/test_material_strength.py) reads. Synthetic headers only: the DDS magic,
// the header size word and the three reserved words, no game bytes.
#include "../../src/proxy/lod_texture_mark.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace x3m::lod_texture_mark;
unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
std::vector<unsigned char> header(bool dds, std::uint32_t r0, std::uint32_t r1, std::uint32_t r2, std::size_t size = 256) {
    std::vector<unsigned char> h(size, 0);
    const auto put = [&](unsigned at, std::uint32_t v) {
        for (unsigned i = 0; i < 4 && at + i < h.size(); ++i) h[at + i] = (v >> (8 * i)) & 255;
    };
    put(0, dds ? 0x20534444u : 0x474e5089u);
    put(4, 124);
    put(32, r0);
    put(36, r1);
    put(40, r2);
    return h;
}
int main() {
    try {
        Mark m;
        // The baker's specular descriptor: slot 3 | lane 3 << 8 | K 2 << 16 | payload 1 << 24.
        const std::uint32_t specular = 3u | 3u << 8 | 2u << 16 | 1u << 24, diffuse = 0u;
        auto h = header(true, magic, 1, specular);
        require(parse(h.data(), h.size(), m) && m.version == 1 && m.slot == 3 && m.lane == 3 && m.k == 2 &&
                    m.payload == 1 && lane_of(m) == lane_strength_alpha,
                "specular mark parses to the strength lane");
        h = header(true, magic, 1, diffuse);
        require(parse(h.data(), h.size(), m) && m.slot == 0 && lane_of(m) == lane_none, "diffuse mark: counted, no lane");
        h = header(true, 0, 0, 0);
        require(!parse(h.data(), h.size(), m), "zero reserved words: unmarked");
        h = header(true, 0x5454564eu /* NVTT */, 0x20010000u, 0);
        require(!parse(h.data(), h.size(), m), "a tool signature is not the mark");
        h = header(false, magic, 1, specular);
        require(!parse(h.data(), h.size(), m), "not a DDS file");
        h = header(true, magic, 1, specular, 127);
        require(!parse(h.data(), h.size(), m), "short buffer");
        require(!parse(nullptr, 1000, m), "null data");
        h = header(true, magic, 2, specular);
        require(parse(h.data(), h.size(), m) && lane_of(m) == lane_none, "a later version: counted, no lane");
        h = header(true, magic, 1, 3u | 3u << 8 | 4u << 16 | 1u << 24);
        require(parse(h.data(), h.size(), m) && lane_of(m) == lane_none, "K != 2: no lane");
        h = header(true, magic, 1, 3u | 0u << 8 | 2u << 16 | 1u << 24);
        require(parse(h.data(), h.size(), m) && lane_of(m) == lane_none, "lane != w: no lane");
        // The table: insert, find, identity, pointer reuse, erase keeps the probe run intact, half-full refusal.
        static Table<64> table;
        int objects[40];
        require(!table.find(&objects[0]), "empty table finds nothing");
        require(table.insert(&objects[0], 11, lane_strength_alpha, 3) && table.count == 1, "insert");
        auto* e = table.find(&objects[0]);
        require(e && e->identity == 11 && e->lane == lane_strength_alpha && e->slot == 3, "find");
        require(table.insert(&objects[0], 12, lane_none, 0) && table.count == 1, "re-insert overwrites");
        e = table.find(&objects[0]);
        require(e && e->identity == 12 && e->lane == lane_none, "overwritten identity and lane");
        for (int i = 1; i < 32; ++i) require(table.insert(&objects[i], 100 + i, lane_strength_alpha, 3), "fill to half");
        require(table.count == 32 && !table.insert(&objects[32], 1, lane_strength_alpha, 3) && table.refused_full == 1,
                "half full refuses");
        table.erase(&objects[5]);
        require(!table.find(&objects[5]) && table.count == 31, "erase");
        for (int i = 1; i < 32; ++i)
            if (i != 5) {
                e = table.find(&objects[i]);
                require(e && e->identity == std::uint64_t(100 + i), "probe runs intact after erase");
            }
        require(!table.insert(nullptr, 1, 1, 1) && !table.find(nullptr), "null pointer refused");
        table.erase(&objects[39]); // absent: no change
        require(table.count == 31, "erase of an absent key is a no-op");
        // The exact process-wide live count: +1 per new entry, +0 on overwrite, -1 per erase, -count on destruction.
        require(live_marks.load() == 31, "live count equals the entries after inserts and erases");
        bool added = true;
        require(table.insert(&objects[1], 7, lane_strength_alpha, 3, &added) && !added && live_marks.load() == 31,
                "overwrite adds nothing");
        require(table.insert(&objects[5], 7, lane_strength_alpha, 3, &added) && added && live_marks.load() == 32,
                "a new entry adds one");
        table.erase(&objects[5]);
        require(live_marks.load() == 31, "erase subtracts one (the re-inserted probe run nets zero)");
        {
            Table<64> scoped;
            require(scoped.insert(&objects[36], 1, lane_strength_alpha, 3) && live_marks.load() == 32, "second table adds");
        }
        require(live_marks.load() == 31, "a destroyed table subtracts its entries");
        std::cout << "{\"checks\":" << checks << ",\"ok\":true}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"checks\":" << checks << ",\"ok\":false,\"error\":\"" << error.what() << "\"}\n";
        return 1;
    }
}
