// Executes the production retain callback; COM and display inputs are scripted.
#include "../../src/temporal/bloom.h"
#include "../../src/temporal/sharpen.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

struct Resource {
    unsigned references = 1;
    void AddRef() { ++references; }
};
struct Device {
    std::uint64_t id = 7, frame = 42;
};
struct Display {
    bool valid = true;
    x3::temporal::AgxConstants agx{};
    x3::temporal::AgxDecode decode = x3::temporal::AgxDecode::gamma22;
    float sharpen = .75f;
    x3::temporal::SharpenConstants sharpen_constants{};
};
struct Input {
    Resource* scene = nullptr;
    struct {
        Resource* main = nullptr;
    } boundary;
    x3::temporal::AgxConstants agx{};
    x3::temporal::AgxDecode decode = x3::temporal::AgxDecode::none;
    float sharpen = 0;
    x3::temporal::SharpenConstants sharpen_constants{};
    bool exact_sharpen = false;
    x3::temporal::BloomParams filter{};
};
float bloom_source_clamp = x3::temporal::kAgxClampOff; // mirrors capture.cpp (inert, --bloom-source-clamp)
struct CompositorInvocation {
    Device* owner;
    void* device;
    bool revoked = false;
    Input input{};
};
struct MotionHdrScene {
    void* device;
    Resource* scene;
    Resource* main;
    std::uint64_t device_id, frame;
    Display display{};
};
#include "comparison_handoff_under_test_inc.h"

int main() {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool ok) {
        ++checks;
        if (!ok) ++failures;
    };
    // Two retains in a row: the handoff is deterministic and always at full
    // strength (the Ctrl+Shift+F10 zero-gain A/B went on 2026-09-26).
    float active_gain = 0;
    for (bool again : {false, true}) {
        Device owner;
        Resource texture, main;
        CompositorInvocation call{&owner, &owner};
        MotionHdrScene scene{&owner, &texture, &main, owner.id, owner.frame};
        scene.display.agx.exposure[0] = 2.8284271f;
        scene.display.sharpen_constants.values[0] = .59460354f;
        retain_compositor_scene(&call, scene);
        check(texture.references == 2 && main.references == 2);
        check(call.input.scene == &texture && call.input.boundary.main == &main);
        check(!std::memcmp(&call.input.agx, &scene.display.agx, sizeof scene.display.agx));
        check(call.input.decode == scene.display.decode && call.input.sharpen == scene.display.sharpen);
        check(call.input.exact_sharpen && !std::memcmp(&call.input.sharpen_constants, &scene.display.sharpen_constants,
                                                       sizeof scene.display.sharpen_constants));
        check(call.input.filter.strength == 1.f);
        check(call.input.filter.authored_glow_gain > 0 && call.input.filter.authored_glow_gain <= 4.f);
        check(call.input.filter.highlight_gain == .05f);
        check(call.input.filter.levels == 5 && call.input.filter.threshold == 1.f && call.input.filter.knee == .5f);
        if (again) check(call.input.filter.authored_glow_gain == active_gain);
        active_gain = call.input.filter.authored_glow_gain;
        // The callback is retain-once even when invoked again before cleanup.
        retain_compositor_scene(&call, scene);
        check(texture.references == 2 && main.references == 2);
    }
    for (unsigned fault = 0; fault < 8; ++fault) {
        Device owner;
        Resource texture, main;
        CompositorInvocation call{&owner, &owner};
        MotionHdrScene scene{&owner, &texture, &main, owner.id, owner.frame};
        switch (fault) {
        case 0: call.revoked = true; break;
        case 1: scene.device = nullptr; break;
        case 2: ++scene.device_id; break;
        case 3: ++scene.frame; break;
        case 4: scene.display.valid = false; break;
        case 5: scene.scene = nullptr; break;
        case 6: scene.main = nullptr; break;
        case 7: call.input.scene = &texture; break;
        }
        retain_compositor_scene(&call, scene);
        check(texture.references == 1 && main.references == 1);
        check(!call.input.boundary.main);
        check(call.input.filter.authored_glow_gain == 0 && call.input.filter.strength == .05f);
    }
    std::printf("comparison_bloom_handoff checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
