// Host double of the own sun hook's pure parts (no Windows, no D3D): the probe's eligibility test and the main-view
// walk (src/proxy/own_sun_hook_core.h), the sites, and the lens-flare gain through the fog
// (renderer::fog_sky_lens_gain). Prints key=value lines; the assertions are in
// verification/analysis/test_own_sun_hook.py and test_fl_sun_occlusion.py.
#include "../../src/proxy/own_sun_hook_core.h"
#include "../../src/renderer/fog_transmittance.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

using namespace x3m::own_sun_hook::core;
using namespace x3m::renderer;
namespace {
long number(const char* text) {
    return std::strtol(text, nullptr, 0);
}
// eligible <owner> <view> <main> <owner_flags>
int eligible_command(char** a) {
    std::printf("eligible=%d\n", int(eligible(std::uintptr_t(number(a[0])), std::uintptr_t(number(a[1])),
                                              std::uintptr_t(number(a[2])),
                                              std::uint32_t(std::strtoul(a[3], nullptr, 0)))));
    return 0;
}
// view <case>: a fake engine image for main_view
int view_command(const char* name) {
    std::map<std::uint32_t, std::uint32_t> memory;
    const std::uint32_t registry = 0x1000, header = 0x2000, buckets = 0x3000, link_a = 0x4000, link_b = 0x4100,
                        cockpit = 0x5000, camera = 0x6000, handle = 0x2a;
    memory[cockpit_registry_va] = registry;
    memory[registry] = header;
    memory[registry + 0x10] = handle;
    memory[header] = buckets;
    memory[header + 4] = 8;
    memory[buckets + 4 * (7 & handle)] = link_a;
    memory[link_a] = link_b;
    memory[link_a + 4] = 0x99;
    memory[link_a + 8] = 0x7777;
    memory[link_b] = 0;
    memory[link_b + 4] = handle;
    memory[link_b + 8] = cockpit;
    memory[cockpit + 0x58] = camera;
    memory[camera + view_flags] = view_flag_sector_camera | 1;
    const std::string which = name;
    if (which == "no_marker")
        memory[camera + view_flags] = 1;
    else if (which == "no_handle")
        memory[registry + 0x10] = 0;
    else if (which == "bad_count")
        memory[header + 4] = 6;
    else if (which == "missing")
        memory[link_b + 4] = 0x55;
    else if (which == "cycle")
        memory[link_b] = link_a, memory[link_b + 4] = 0x55;
    else if (which == "misaligned")
        memory[cockpit + 0x58] = camera + 2;
    else if (which == "unreadable")
        memory.erase(camera + view_flags);
    auto read = [&](std::uintptr_t address, void* out, std::size_t size) {
        for (std::size_t i = 0; i < size; i += 4) {
            const auto it = memory.find(std::uint32_t(address + i));
            if (it == memory.end()) return false;
            std::memcpy(static_cast<char*>(out) + i, &it->second, 4);
        }
        return true;
    };
    std::printf("view=0x%lx\n", static_cast<unsigned long>(main_view(read)));
    return 0;
}
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string command = argv[1];
    if (command == "eligible" && argc == 6) return eligible_command(argv + 2);
    if (command == "view" && argc == 3) return view_command(argv[2]);
    // foggain <G> <on> <extinction> <start> <cap> <chroma r g b> <tint>: the lens gain through the fog toward the sky
    // (renderer::fog_sky_lens_gain, motion_output_lens_bracket_inc.h's lens_bracket_begin).
    if (command == "foggain" && argc == 11) {
        FogTransmittanceLaw law;
        law.on = number(argv[3]) != 0;
        law.extinction = float(std::atof(argv[4]));
        const float tint = float(std::atof(argv[10]));
        for (unsigned i = 0; i < 3; ++i) law.k[i] = 1.f + tint * (1.f - float(std::atof(argv[7 + i])));
        law.start = float(std::atof(argv[5]));
        law.cap = float(std::atof(argv[6]));
        float t = -1.f, gain = -1.f;
        fog_sky_lens_gain(law, float(std::atof(argv[2])), &t, &gain);
        std::printf("t=%.9g gain=%.9g\n", double(t), double(gain));
        return 0;
    }
    if (command == "sites") {
        std::printf(
            "probe_site=0x%lx probe_target=0x%lx lens_site=0x%lx lens_target=0x%lx gates=0x%llx gates_length=%u\n",
            (unsigned long)probe_site_va, (unsigned long)probe_target_va, (unsigned long)lens_site_va,
            (unsigned long)lens_target_va, (unsigned long long)probe_gates_fnv1a, probe_gates_length);
        return 0;
    }
    return 2;
}
