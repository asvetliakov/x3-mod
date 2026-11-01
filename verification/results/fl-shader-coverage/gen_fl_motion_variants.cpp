// Host-side generator of every shader variant the proxy creates from an FL
// program (src/proxy/motion_output.cpp, CreatePixelShader / CreateVertexShader):
//   ps: material_motion_pixel_variant with current depth on (-motion-depth) and
//       off (-motion), and the sun-lane sibling of the depth variant
//       (material_motion_invalid_sun_share, -sunlane);
//   vs: material_motion_vertex_variant with current depth on and off.
// Part 4b adds, per FL pixel program, the original-shading forms the proxy
// builds on the motion variant (engine_light_kind order: 0 the plain motion
// variant at K = 0, 1 the fill, 2 the light-map gain, 3 its widened form, 4 the
// share producer, 5 the gained share, 6 the widened gained share) at the
// launcher defaults (K = 0.01, G = 4 with the far-fade dynamic gain, widening
// K = B = 4), each without (-lin<k>) and with the engine light (-eng<k>), the
// static-gain forms of kinds 2 and 3 (-static<k>), the depth-off forms of kinds
// 1-3 (-nodepth<k>), the LOD-strength twin of every parent (-str<k>, -streng<k>)
// and, for the twelve HDR additive hull programs, the hull emission gain
// (-hullgain G = 2, -hullclamp G = 2 with the normalizing clamp 0.5). Every form
// must apply with the out-flags its kind implies (the proxy's twin_matches_base
// rule); a refusal is a failure.
// Output files are game-derived bytes: keep the output directory untracked.
// usage: gen_fl_motion_variants <out dir> <programs dir> (ps_<id>|vs_<id>)...
#include "../../../src/renderer/linear_emission.h"
#include "../../../src/renderer/linear_material.h"
#include "../../../src/renderer/material_motion.h"
#include "../../../src/renderer/ps3_program_slots.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace x3m::renderer;
using Words = std::vector<std::uint32_t>;
static bool load(const std::string& path, Words& w) {
    std::ifstream s(path, std::ios::binary);
    if (!s) return false;
    std::vector<char> b((std::istreambuf_iterator<char>(s)), {});
    w.resize(b.size() / 4);
    std::memcpy(w.data(), b.data(), w.size() * 4);
    return true;
}
// The largest saved variant by ps_3_0 weighted slots (ps3_program_slots, the proxy's own count).
static std::size_t largest_slots = 0;
static void save(const std::string& path, const Words& w) {
    const std::size_t slots = ps3_program_slots(w.data(), w.size());
    largest_slots = slots > largest_slots ? slots : largest_slots;
    std::ofstream s(path, std::ios::binary);
    s.write(reinterpret_cast<const char*>(w.data()), std::streamsize(w.size() * 4));
}
// One original-shading form (and its strength twin); false on any refusal or flag mismatch.
static bool form(const std::string& out, const std::string& name, const Words& original, unsigned kind, bool engine,
                 bool dynamic, bool depth, const std::string& tag, std::size_t& largest, bool twin) {
    static const HullLightmapWiden widen{4.f, 4.f};
    OriginalVariantOptions options;
    if (kind >= 1) options.fill = 0.01f;
    if (kind == 2 || kind == 3 || kind == 5 || kind == 6) {
        options.lightmap_gain = 4.f;
        options.lightmap_dynamic = dynamic;
    }
    if (kind == 3 || kind == 6) options.widen = &widen;
    options.share = kind >= 4;
    Words parent;
    bool fill = false, share = false, gain = false, widened = false, lit = false;
    const auto r = linear_material_original_variant_pixel(original.data(), original.size(), options, engine, parent,
                                                          depth, &fill, &share, &gain, &widened, &lit);
    const bool expected = r == LinearMaterialResult::Applied && fill == (kind >= 1) && share == (kind >= 4) &&
                          gain == (options.lightmap_gain != 1.f) && widened == (options.widen != nullptr) &&
                          lit == engine;
    std::printf("%s form=%s result=%u fill=%d share=%d gain=%d widen=%d engine=%d words=%zu ok=%d\n", name.c_str(),
                tag.c_str(), unsigned(r), fill, share, gain, widened, lit, parent.size(), expected);
    if (!expected) return false;
    largest = parent.size() > largest ? parent.size() : largest;
    save(out + "/" + name + "-" + tag + ".bin", parent);
    if (!twin) return true;
    Words strength;
    StrengthTwinRefusal why = StrengthTwinRefusal::None;
    const auto t = linear_material_strength_twin_pixel_variant(parent.data(), parent.size(), original.data(),
                                                               original.size(), strength, nullptr, &why);
    std::printf("%s form=str-%s result=%u refusal=%s words=%zu\n", name.c_str(), tag.c_str(), unsigned(t),
                linear_material_strength_refusal_name(why), strength.size());
    if (t != LinearMaterialResult::Applied) return false;
    largest = strength.size() > largest ? strength.size() : largest;
    save(out + "/" + name + "-str" + tag + ".bin", strength);
    return true;
}
int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string out = argv[1], in = argv[2];
    int failures = 0;
    std::size_t largest = 0;
    for (int a = 3; a < argc; ++a) {
        const std::string name = argv[a];
        Words original;
        if (!load(in + "/" + name + ".bin", original)) {
            std::printf("%s missing\n", name.c_str());
            ++failures;
            continue;
        }
        if (name.compare(0, 3, "vs_") == 0) {
            for (const bool depth : {true, false}) {
                Words v;
                const auto r = material_motion_vertex_variant(original.data(), original.size(), v, depth);
                const std::string kind = depth ? "motion-depth" : "motion";
                std::printf("%s kind=%s result=%u original=%zu words=%zu\n", name.c_str(), kind.c_str(), unsigned(r),
                            original.size(), v.size());
                if (r != MaterialMotionResult::Applied) ++failures;
                else save(out + "/" + name + "-" + kind + ".bin", v);
            }
            continue;
        }
        const auto* row = material_motion_pixel_row(material_motion_fingerprint(original.data(), original.size()),
                                                    original.size());
        for (const bool depth : {true, false}) {
            Words v;
            const auto r = material_motion_pixel_variant(original.data(), original.size(), v, depth);
            const std::string kind = depth ? "motion-depth" : "motion";
            const bool writes_depth = row && material_motion_pixel_writes_depth(*row, depth);
            std::printf("%s kind=%s result=%u original=%zu words=%zu writes_depth=%d\n", name.c_str(), kind.c_str(),
                        unsigned(r), original.size(), v.size(), writes_depth);
            if (r != MaterialMotionResult::Applied) {
                ++failures;
                continue;
            }
            save(out + "/" + name + "-" + kind + ".bin", v);
            if (writes_depth) { // The proxy's sun-lane sibling exists only for a depth-writing variant.
                const bool lane = material_motion_invalid_sun_share(v);
                std::printf("%s kind=sunlane applied=%d words=%zu\n", name.c_str(), lane, v.size());
                if (!lane) ++failures;
                else save(out + "/" + name + "-sunlane.bin", v);
            }
        }
        // Original-shading forms (part 4b): the kind 0 base is the motion variant above (not rewritten).
        for (unsigned kind = 0; kind < 7; ++kind)
            for (const bool engine : {false, true}) {
                const std::string tag = (engine ? "eng" : "lin") + std::to_string(kind);
                if (!form(out, name, original, kind, engine, true, true, tag, largest, true)) ++failures;
            }
        for (unsigned kind : {2u, 3u})
            if (!form(out, name, original, kind, false, false, true, "static" + std::to_string(kind), largest, false))
                ++failures;
        for (unsigned kind : {1u, 2u, 3u})
            if (!form(out, name, original, kind, false, true, false, "nodepth" + std::to_string(kind), largest, false))
                ++failures;
        const auto hash = material_motion_fingerprint(original.data(), original.size());
        if (linear_emission_hull_program_reviewed(hash))
            for (const bool clamp : {false, true}) {
                Words gained;
                const auto r = linear_emission_hull_source_gain_variant(original.data(), original.size(), 2.f, gained,
                                                                        clamp ? .5f : 0.f, clamp);
                std::printf("%s form=hull%s result=%u words=%zu\n", name.c_str(), clamp ? "clamp" : "gain",
                            unsigned(r), gained.size());
                if (r != LinearEmissionResult::Applied) ++failures;
                else save(out + "/" + name + (clamp ? "-hullclamp.bin" : "-hullgain.bin"), gained);
            }
    }
    std::printf("largest=%zu slots=%zu failures=%d\n", largest, largest_slots, failures);
    return failures ? 1 : 0;
}
