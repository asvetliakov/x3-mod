// Host-only driver for docs/architecture/fade-owner-shadow-receiver.md section 8 (open issue 1): the original-shading
// share forms (linear_material_original_sun_share_pixel_variant: plain, light-map gained, widened; depth on) built
// with the fade-owner transform off and on (material_motion_configure_fade_owner, X3M_FADE_RT2_OWNER). Reads every
// ps_ program of the local corpus, writes `<name>-<form>-<owner>.bin` into a caller-supplied local folder and prints
// one JSON object naming the forms created. test_original_sun_share.py executes them (RT2 .a through c218). No game
// bytes are embedded.
#include "../../src/renderer/linear_material.h"
#include "../../src/renderer/material_motion.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::renderer;
using Words = std::vector<std::uint32_t>;
namespace fs = std::filesystem;
namespace {
Words read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto bytes = stream.tellg();
    if (!stream || bytes <= 0 || bytes % 4) throw std::runtime_error("read original");
    Words result(static_cast<std::size_t>(bytes) / 4);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(result.data()), bytes);
    if (!stream) throw std::runtime_error("read original");
    return result;
}
void write(const fs::path& path, const Words& words) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * 4));
    if (!stream) throw std::runtime_error("write local variant");
}
// The three share forms motion_output.cpp creates (fill K 0.05, gain 4 with the far fade's dynamic c217.w lane,
// widening K = B = 3); 1 bit per created form.
unsigned forms(const Words& original, const fs::path& out, const std::string& name, const char* owner) {
    unsigned created = 0;
    const HullLightmapWiden widen{3.f, 3.f};
    for (unsigned form = 0; form < 3; ++form) {
        Words words;
        bool share = false, gain = false, widened = false;
        const auto result = form == 0 ? linear_material_original_sun_share_pixel_variant(
                                            original.data(), original.size(), 0.05f, words, true, share)
                                      : linear_material_original_sun_share_pixel_variant(
                                            original.data(), original.size(), 0.05f, words, true, share, 4.f, &gain,
                                            true, form == 2 ? &widen : nullptr, form == 2 ? &widened : nullptr);
        const bool ok = result == LinearMaterialResult::Applied && share && (form == 0 || gain) &&
                        (form != 2 || widened);
        if (!ok) continue;
        static const char* const names[] = {"plain", "gain", "widen"};
        write(out / (name + "-" + names[form] + "-" + owner + ".bin"), words);
        created |= 1u << form;
    }
    return created;
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: fade_owner_share_forms <programs> <output>");
        const fs::path in(argv[1]), out(argv[2]);
        std::vector<fs::path> paths;
        for (const auto& entry : fs::directory_iterator(in))
            if (entry.path().filename().string().rfind("ps_", 0) == 0) paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        std::cout << "{\"rows\":[";
        bool first = true;
        for (const auto& path : paths) {
            const Words original = read(path);
            const std::string name = path.stem().string();
            material_motion_configure_fade_owner(false);
            const unsigned off = forms(original, out, name, "off");
            material_motion_configure_fade_owner(true);
            const unsigned on = forms(original, out, name, "on");
            if (!off && !on) continue;
            std::cout << (first ? "" : ",") << "{\"name\":\"" << name << "\",\"off\":" << off << ",\"on\":" << on
                      << "}";
            first = false;
        }
        material_motion_configure_fade_owner(false);
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
