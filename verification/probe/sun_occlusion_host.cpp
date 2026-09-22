// Host double of the partial-sun-occlusion pure parts (no Windows, no D3D): the probe decision
// and gate order, the readiness state machine, the record -> footprint mapping, the blend
// classification, the main-view walk (src/proxy/sun_occlusion_core.h) and the structural pixel
// wrap (src/renderer/lens_visibility_variant.cpp). Prints key=value lines; the assertions are in
// verification/analysis/test_sun_occlusion.py.
#include "../../src/proxy/sun_occlusion_core.h"
#include "../../src/renderer/lens_visibility_variant.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace x3m::sun_occlusion::core;
using namespace x3m::renderer;
namespace {
long number(const char* text) { return std::strtol(text, nullptr, 0); }
// decide <ready> <owner> <view> <main> <owner_flags> <video> <viewflags> <x> <y> <top> <bottom> <left> <right>
int decide_command(char** a) {
    ProbeInputs in;
    in.ready = number(a[0]) != 0; in.record_owner = std::uintptr_t(number(a[1])); in.view = std::uintptr_t(number(a[2])); in.main_view = std::uintptr_t(number(a[3]));
    in.owner_flags = std::uint32_t(std::strtoul(a[4], nullptr, 0));
    in.video_flags = std::uint32_t(std::strtoul(a[5], nullptr, 0)); in.view_flags = std::uint32_t(std::strtoul(a[6], nullptr, 0));
    in.x = std::int32_t(number(a[7])); in.y = std::int32_t(number(a[8]));
    in.top = std::int32_t(number(a[9])); in.bottom = std::int32_t(number(a[10])); in.left = std::int32_t(number(a[11])); in.right = std::int32_t(number(a[12]));
    std::printf("decision=%d eligible=%d\n", int(decide(in)), int(eligible(in.record_owner, in.view, in.main_view, in.owner_flags)));
    return 0;
}
// ready <script>: b = begin_frame, p<record> = probe (prints the answer), k = pass ok, x = block, r = reset
int ready_command(int count, char** a) {
    Ready state; std::string answers;
    for (int i = 0; i < count; ++i) {
        const char c = a[i][0];
        if (c == 'b') state.begin_frame(); else if (c == 'k') state.pass_ok = true; else if (c == 'x') state.blocked = true; else if (c == 'r') state.reset();
        else if (c == 'p') answers += state.probe(std::uintptr_t(number(a[i] + 1)), true) ? '1' : '0';
        else if (c == 'd') answers += state.probe(std::uintptr_t(number(a[i] + 1)), false) ? '1' : '0';
    }
    std::printf("answers=%s single=%d\n", answers.c_str(), int(state.single()));
    return 0;
}
// footprint <x> <y> <size> <acc> <fov> <scale_x> <width> <height> <radius_scale>   (full-screen rect)
int footprint_command(char** a) {
    Latch l; l.valid = true; l.x = std::int32_t(number(a[0])); l.y = std::int32_t(number(a[1])); l.size = std::int32_t(number(a[2])); l.accumulator = std::int32_t(number(a[3]));
    l.fov = std::uint32_t(number(a[4])); l.scale_x = std::int32_t(number(a[5])); l.top = l.left = 0; l.bottom = l.right = 0x10000;
    const Footprint f = footprint(l, unsigned(number(a[6])), unsigned(number(a[7])), float(std::atof(a[8])));
    std::printf("valid=%d known=%d saturated=%d u=%.7f v=%.7f ru=%.7f rv=%.7f\n", int(f.valid), int(f.radius_known), int(f.saturated), f.u, f.v, f.radius_u, f.radius_v);
    return 0;
}
// blend <enable> <src> <dst> <op> <srgb> <alphatest> <ref> <func> <fog>
int blend_command(char** a) {
    BlendState s; std::uint32_t* fields[] = {&s.enable, &s.src, &s.dst, &s.op, &s.srgb_write, &s.alpha_test, &s.alpha_ref, &s.alpha_func, &s.fog};
    for (unsigned i = 0; i < 9; ++i) *fields[i] = std::uint32_t(number(a[i]));
    std::printf("scale=%u\n", unsigned(classify_blend(s)));
    return 0;
}
// view <case>: a fake engine image for main_view
int view_command(const char* name) {
    std::map<std::uint32_t, std::uint32_t> memory;
    const std::uint32_t registry = 0x1000, header = 0x2000, buckets = 0x3000, link_a = 0x4000, link_b = 0x4100, cockpit = 0x5000, camera = 0x6000, handle = 0x2a;
    memory[cockpit_registry_va] = registry; memory[registry] = header; memory[registry + 0x10] = handle;
    memory[header] = buckets; memory[header + 4] = 8;
    memory[buckets + 4 * (7 & handle)] = link_a;
    memory[link_a] = link_b; memory[link_a + 4] = 0x99; memory[link_a + 8] = 0x7777;
    memory[link_b] = 0; memory[link_b + 4] = handle; memory[link_b + 8] = cockpit;
    memory[cockpit + 0x58] = camera; memory[camera + view_flags] = view_flag_sector_camera | 1;
    const std::string which = name;
    if (which == "no_marker") memory[camera + view_flags] = 1;
    else if (which == "no_handle") memory[registry + 0x10] = 0;
    else if (which == "bad_count") memory[header + 4] = 6;
    else if (which == "missing") memory[link_b + 4] = 0x55;
    else if (which == "cycle") memory[link_b] = link_a, memory[link_b + 4] = 0x55;
    else if (which == "misaligned") memory[cockpit + 0x58] = camera + 2;
    else if (which == "unreadable") memory.erase(camera + view_flags);
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
// variant <scale> <hex words...>
int variant_command(int count, char** a) {
    std::vector<std::uint32_t> words, out{0xdeadbeefu};
    for (int i = 1; i < count; ++i) words.push_back(std::uint32_t(std::strtoul(a[i], nullptr, 16)));
    LensVisibilityLayout layout{};
    const auto result = lens_visibility_pixel_variant(words.data(), words.size(), LensVisibilityScale(number(a[0])), out, &layout);
    std::printf("result=%s sampler=%u constant=%u output=%u fetch=%u words=", lens_visibility_result_name(result), layout.sampler, layout.constant, layout.output_temporary, layout.fetch_temporary);
    for (std::size_t i = 0; i < out.size(); ++i) std::printf("%s%08x", i ? "," : "", out[i]);
    std::printf("\n");
    return 0;
}
// clipvariant <scale> <texcoord> <dx> <dy> <core_f> <hex words...>
int clip_variant_command(int count, char** a) {
    std::vector<std::uint32_t> words, out{0xdeadbeefu};
    for (int i = 5; i < count; ++i) words.push_back(std::uint32_t(std::strtoul(a[i], nullptr, 16)));
    LensVisibilityLayout layout{};
    const auto result = lens_visibility_pixel_clip_variant(words.data(), words.size(), LensVisibilityScale(number(a[0])), unsigned(number(a[1])), float(std::atof(a[2])), float(std::atof(a[3])), number(a[4]) != 0, out, &layout);
    std::printf("result=%s sampler=%u depth_sampler=%u constant=%u output=%u fetch=%u words=", lens_visibility_result_name(result), layout.sampler, layout.depth_sampler, layout.constant, layout.output_temporary, layout.fetch_temporary);
    for (std::size_t i = 0; i < out.size(); ++i) std::printf("%s%08x", i ? "," : "", out[i]);
    std::printf("\n");
    return 0;
}
// vsvariant <texcoord> <hex words...>
int vertex_variant_command(int count, char** a) {
    std::vector<std::uint32_t> words, out{0xdeadbeefu};
    for (int i = 1; i < count; ++i) words.push_back(std::uint32_t(std::strtoul(a[i], nullptr, 16)));
    unsigned matrix = 999; bool origin = false;
    const auto result = lens_visibility_vertex_variant(words.data(), words.size(), unsigned(number(a[0])), out, &matrix, &origin);
    std::printf("result=%s matrix=%u origin=%d words=", lens_visibility_result_name(result), matrix, int(origin));
    for (std::size_t i = 0; i < out.size(); ++i) std::printf("%s%08x", i ? "," : "", out[i]);
    std::printf("\n");
    return 0;
}
// texcoord <vs hex words...> -- <ps hex words...>
int texcoord_command(int count, char** a) {
    std::vector<std::uint32_t> vs, ps; bool second = false;
    for (int i = 0; i < count; ++i) { if (!std::strcmp(a[i], "--")) { second = true; continue; } (second ? ps : vs).push_back(std::uint32_t(std::strtoul(a[i], nullptr, 16))); }
    std::printf("texcoord=%u\n", lens_visibility_free_texcoord(vs.data(), vs.size(), ps.data(), ps.size()));
    return 0;
}
// body <known> <sun_u> <sun_v> <aspect> <16 row floats>
int body_command(char** a) {
    float rows[16];
    for (unsigned i = 0; i < 16; ++i) rows[i] = float(std::atof(a[4 + i]));
    const BodyCentre c = classify_body(rows, number(a[0]) != 0, float(std::atof(a[1])), float(std::atof(a[2])), float(std::atof(a[3])));
    std::printf("body=%u u=%.6f v=%.6f distance=%.6f\n", unsigned(c.body), c.u, c.v, c.distance_u);
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string command = argv[1];
    if (command == "decide" && argc == 15) return decide_command(argv + 2);
    if (command == "ready") return ready_command(argc - 2, argv + 2);
    if (command == "footprint" && argc == 11) return footprint_command(argv + 2);
    if (command == "blend" && argc == 11) return blend_command(argv + 2);
    if (command == "view" && argc == 3) return view_command(argv[2]);
    if (command == "variant" && argc >= 4) return variant_command(argc - 2, argv + 2);
    if (command == "clipvariant" && argc >= 8) return clip_variant_command(argc - 2, argv + 2);
    if (command == "vsvariant" && argc >= 4) return vertex_variant_command(argc - 2, argv + 2);
    if (command == "texcoord" && argc >= 4) return texcoord_command(argc - 2, argv + 2);
    if (command == "body" && argc == 22) return body_command(argv + 2);
    if (command == "saturated" && argc == 4) { std::printf("saturated=%d\n", int(size_saturated(std::int32_t(number(argv[2])), std::int32_t(number(argv[3]))))); return 0; }
    if (command == "hold") { // r = ran, s = skipped and holdable, f = not holdable
        Hold hold; std::string out;
        for (int i = 2; i < argc; ++i) out += hold.step(argv[i][0] == 'r', argv[i][0] == 's') ? '1' : '0';
        std::printf("usable=%s\n", out.c_str()); return 0;
    }
    if (command == "alpha" && argc == 3) { std::printf("alpha=%.7f\n", smoothing_alpha(std::atof(argv[2]))); return 0; }
    // jitter <active> <jx_px> <jy_px> <width> <height>: the visibility pass's RT2 read offset in uv.
    if (command == "jitter" && argc == 7) { const JitterUv j = jitter_uv(number(argv[2]) != 0, float(std::atof(argv[3])), float(std::atof(argv[4])), unsigned(number(argv[5])), unsigned(number(argv[6]))); std::printf("u=%.8f v=%.8f\n", j.u, j.v); return 0; }
    if (command == "sites") {
        std::printf("probe_site=0x%lx probe_target=0x%lx lens_site=0x%lx lens_target=0x%lx gates=0x%llx gates_length=%u\n", (unsigned long)probe_site_va,
                    (unsigned long)probe_target_va, (unsigned long)lens_site_va, (unsigned long)lens_target_va, (unsigned long long)probe_gates_fnv1a, probe_gates_length);
        return 0;
    }
    return 2;
}
