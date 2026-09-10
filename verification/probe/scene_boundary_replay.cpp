// Original portable adapter for event-only tests. No D3D/game linkage.
#include "../../src/renderer/scene_boundary.h"
#include <iostream>
#include <sstream>
#include <string>
using namespace x3m::renderer;
static void surface(std::istream& in, Surface& s) {
    in >> s.known >> s.identity >> s.container >> s.width >> s.height >> s.format >> s.msaa;
}
int main() {
    SceneBoundarySelector selector;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line); char operation; in >> operation;
        if (operation == 'P') {
            SceneSignatures signatures;
            for (auto& pair : signatures.background) in >> pair.vs >> pair.ps;
            for (auto& pair : signatures.bloom) in >> pair.vs >> pair.ps;
            selector = SceneBoundarySelector(signatures);
            // Caller storage may change after construction; profile is a copy.
            signatures.background[2] = {};
        } else if (operation == 'B') {
            std::uint64_t device, generation, frame; in >> device >> generation >> frame;
            selector.begin_frame(device, generation, frame);
        } else if (operation == 'I') selector.invalidate();
        else if (operation == 'E') {
            Event e; unsigned kind;
            in >> kind >> e.sequence >> e.result_known >> e.result;
            e.kind = static_cast<EventKind>(kind);
            surface(in,e.rt); surface(in,e.depth);
            in >> e.viewport.known >> e.viewport.x >> e.viewport.y >> e.viewport.width
               >> e.viewport.height >> e.viewport.min_z >> e.viewport.max_z >> e.only_rt0
               >> e.rt_index >> e.vs >> e.ps >> e.texture0 >> e.draw_state_known
               >> e.topology >> e.primitives >> e.z_enable >> e.z_write
               >> e.clear_flags >> e.rect_count >> e.clear_z;
            surface(in,e.source); surface(in,e.destination);
            in >> e.source_rect_null >> e.destination_rect_null;
            if (!in) { std::cerr << "Malformed fixture event\n"; return 2; }
            const auto candidate = selector.before_clear(e);
            const auto confirmed = selector.observe(e);
            if (candidate.valid || confirmed.valid)
                std::cout << "S " << e.sequence << ' ' << candidate.valid << ' ' << confirmed.valid
                          << ' ' << candidate.color.identity << ' ' << candidate.depth.identity
                          << ' ' << candidate.depth_epoch << '\n';
        } else if (operation == 'Q') {
            std::cout << "Q " << static_cast<unsigned>(selector.state()) << ' '
                      << static_cast<unsigned>(selector.rejection()) << ' ' << selector.rejection_sequence() << '\n';
        } else return 2;
    }
}
