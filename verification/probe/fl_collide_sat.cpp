// Native fixture of the FL collide_sat_sse2 report (src/engine/fl/collide_sat_sse2_fl.cpp; docs/reverse-engineering/fl/
// collision-sat.md, "Port (2026-11-03)"). It maps a private 64 KB-aligned copy of X3FL.exe with its relocations applied
// (fl_seta_image.h) and runs the production initialize() against it: the native-SSE row for a set and an unset setting,
// one row per load, nothing claimed or written (no arena, the whole image byte-identical to the pristine copy), LastError
// kept; a flipped byte in the SAT body, in its call window and in a relocated operand each refused with the window's
// reason; no identity refused as executable_mismatch. Built -DX3M_EDITION_FL=1 -DX3M_COLLIDE_SAT_FL_FIXTURE by
// verification/analysis/test_fl_collide_sat.py, run natively through WSL interop (no Wine).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "fl_seta_image.h"
#include "../../src/engine/fl/collide_sat_sse2_fl.h"
#include "../../src/engine/fl/collide_sat_sse2_sites_fl.h"
#include "../../src/proxy/engine_patch.h"

namespace sat = x3m::collide_sat_sse2;
namespace S = x3m::fl::collide_sat_fl;
namespace W = x3m::fl::collide_windows;
namespace engine_patch = x3m::engine_patch;

static std::string last_row;
static unsigned rows = 0;
namespace x3m {
void log(const char* format, ...) {
    char line[1024];
    std::va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    last_row = line;
    ++rows;
    std::printf("LOG %s\n", line);
}
}
namespace x3m::fl {
std::uintptr_t module_base() {
    return 0; // the fixture's main module is not X3FL.exe
}
}
static unsigned checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s | %s\n", what, last_row.c_str());
    }
}
static FlImage img;
static bool has(const char* cell) {
    return last_row.find(cell) != std::string::npos;
}
static bool untouched() {
    return !std::memcmp(img.base, img.pristine.data(), img.size);
}
// One fresh load: rearm, initialize, return whether it reported native SSE; one row per load.
static bool load(std::uintptr_t base) {
    sat::fixture_rearm();
    sat::fixture_base(base);
    const unsigned before = rows;
    SetLastError(0xfeedc0de);
    const bool native = sat::initialize();
    check(GetLastError() == 0xfeedc0de, "initialize preserves LastError");
    check(rows == before + 1, "one row per load");
    return native;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 2) {
        std::printf("FAIL arguments argc=%d\n", argc);
        return 2;
    }
    if (!map_fl_image(argv[1], img)) {
        std::printf("SKIP image\n");
        return 0;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(img.base);
    const unsigned arena = engine_patch::arena_used();

    SetEnvironmentVariableW(L"X3M_COLLIDE_SAT_SSE2", L"1");
    check(load(base), "requested: native SSE reported");
    check(has("requested=1 patched=0 edition=fl status=native_sse reason=engine_already_sse") &&
              has("implementation=engine_native_sse setting_controls_native=0"),
          "the native_sse row");
    check(sat::native_sse() && !std::strcmp(sat::state(), "engine_already_sse"), "state engine_already_sse");
    const unsigned once = rows;
    check(sat::initialize() && rows == once, "a repeated initialize writes no second row");
    check(untouched() && engine_patch::arena_used() == arena, "nothing written, nothing emitted");

    SetEnvironmentVariableW(L"X3M_COLLIDE_SAT_SSE2", nullptr);
    check(load(base) && has("requested=0 patched=0 edition=fl status=native_sse"), "unset: still the native row");
    SetEnvironmentVariableW(L"X3M_COLLIDE_SAT_SSE2", L"0");
    check(load(base) && has("requested=0 patched=0 edition=fl status=native_sse"), "off: the setting cannot turn it off");

    // refusals: a byte of the body, of the call window, and a relocated operand's value
    const struct {
        std::uint32_t rva;
        const char* reason;
        bool dword;
    } flips[] = {{S::sat_rva + 0x40, "reason=sat_body_mismatch", false},
                 {S::call_window_rva + 2, "reason=sat_call_mismatch", false},
                 {S::sat_rva + W::sat_fixups[0].offset, "reason=sat_body_mismatch", true}};
    for (const auto& f : flips) {
        if (f.dword) img.u32(f.rva) += 4;
        else img.base[f.rva] ^= 0x01;
        check(!load(base) && has("status=refused") && has(f.reason) && !sat::native_sse(), f.reason);
        img.restore(f.rva, 4);
    }
    check(untouched(), "image restored after the flips");
    check(!load(0) && has("status=refused reason=executable_mismatch"), "no identity: executable_mismatch");
    check(sat::shutdown() && untouched() && engine_patch::arena_used() == arena, "shutdown: nothing to restore");
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
