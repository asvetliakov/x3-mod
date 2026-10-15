// Review of the arrival round item 1 (fail-before): the gate collision-tree read's verdict (wk::gate_tree_state: 2 tree, 1 none, 0
// unknown) on the synthetic images of the fixture's unit, compiled against a given src/proxy (run.sh: 97f3804b and the working tree).
// Expected: bubble 1, terran 2, every other case 0 (none only under the positive guard of ship-navigation-ai.md section 10.9).
#include "ship_avoidance_walk_core.h"
#include <cstdio>
#include <unordered_map>
namespace wk = x3m::ship_avoidance::walk;
struct FakeMem {
    std::unordered_map<std::uint32_t, std::uint32_t> m;
    std::uint32_t u32(std::uint32_t at) { auto it = m.find(at); return it == m.end() ? 0u : it->second; }
    std::int32_t s32(std::uint32_t at) { return std::int32_t(u32(at)); }
    std::uint16_t u16(std::uint32_t at) { return std::uint16_t(u32(at & ~3u) >> (8 * (at & 3))); }
    void put(std::uint32_t at, std::uint32_t v) { m[at] = v; }
};
struct GateImg {
    FakeMem mem;
    std::uint32_t gate = 0x01004000, root = 0x01005000, next = 0x01010000, scene = 0x2a7, tail[64] = {};
    GateImg() { mem.put(gate + 0x48, 0x12); mem.put(gate + 0x70, root); mem.put(gate + 0x98, scene); mem.put(root + 0xc, root + 0x10); mem.put(root + 0x12c, 0x1000000); }
    std::uint32_t add(std::uint32_t parent, std::uint32_t flags = 0, int sc = -1) {
        const std::uint32_t c = next;
        next += 0x400;
        mem.put(c + 0xc, c + 0x10);
        mem.put(c + 0x18, parent);
        mem.put(c + 0x258, sc < 0 ? scene : std::uint32_t(sc));
        mem.put(c + 0x12c, flags);
        mem.put(c, parent + 0x10);
        std::uint32_t& t = tail[((parent - 0x01005000u) / 0x400u) & 63u];
        if (t) mem.put(t, c); else mem.put(parent + 0xc, c);
        t = c;
        return c;
    }
};
int main() {
    int bad = 0;
    auto rep = [&](const char* name, GateImg& g, unsigned want) {
        const unsigned s = wk::gate_tree_state(g.mem, g.gate);
        std::printf("%-12s state %u want %u %s\n", name, s, want, s == want ? "ok" : "FAIL");
        bad += s != want;
    };
    GateImg bubble; bubble.add(bubble.root); rep("bubble", bubble, 1);
    GateImg terran; terran.add(terran.root); terran.add(terran.root); terran.add(terran.root, 0x1000000); rep("terran", terran, 2);
    GateImg foreign; foreign.mem.put(foreign.root + 0xc, 0x02000000); rep("foreign", foreign, 0);
    GateImg back; back.mem.put(back.add(back.root) + 0x18, 0x01009000); rep("back_ptr", back, 0);
    GateImg noscene; noscene.mem.put(noscene.gate + 0x98, 0); rep("no_scene", noscene, 0);
    GateImg stale; stale.add(stale.root, 0, stale.scene + 1); rep("stale_scene", stale, 0);
    GateImg cls; cls.add(cls.root); cls.mem.put(cls.gate + 0x48, 7); rep("class", cls, 0);
    GateImg unal; unal.add(unal.root); unal.mem.put(unal.root + 0xc, unal.mem.u32(unal.root + 0xc) + 2); rep("misaligned", unal, 0);
    GateImg deep4; deep4.add(deep4.add(deep4.add(deep4.add(deep4.root)))); rep("depth4", deep4, 0);
    GateImg wide; for (int i = 0; i < 65; ++i) wide.add(wide.root); rep("budget", wide, 0);
    GateImg chain; chain.mem.put(chain.add(chain.root), 0x01009010); rep("chain_end", chain, 0);
    std::printf("RESULT %d of 11 cases wrong\n", bad);
    return 0;
}
