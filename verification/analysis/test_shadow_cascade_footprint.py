"""Host contracts of the per-part minimum light-space footprint of the shadow
cascades (docs/architecture/shadow-cascades.md, "Minimum caster footprint";
docs/architecture/shadow-cascade-cost-policy.md, option (a)).

src/renderer/shadow_cascade_footprint_core.h and the measure the box test
yields are compiled natively (no Wine, no D3D) into one driver:

* the resolved thresholds of the production set 250 / 1,500 / 7,500 / 37,500 /
  150,000 at 4,096 texels, P = 8, m00 = 0.8, width 1,280: the cost note's
  22 / 111 / 557 units on c2 / c3 / c4, the texel floor on c0, and the same law
  at 1,920 wide and at P = 1 (where the texel floor binds instead);
* E_{k-1} is the nearest ACTIVE cascade (the sliding ladder drops cascades);
* the option off and an unusable camera latch drop nothing / keep the texel floor;
* the drop decision one unit under and over the bound, an unknown footprint
  (0, NaN) kept, a cascade beyond the set kept, and the whole-mask gate;
* the measure is the largest LATERAL side, so a long thin strut and a
  depth-long sliver both pass;
* renderer::shadow_cascade_bounds_mask's `lateral` output: the same value as the
  box's lateral side on the shared-sun path, one per cascade with per-cascade
  suns, zeroed with an unknown mask;
* shadow_cascade_pool's band for the option (0 = off, 64 max, NaN refused) and
  that the adaptive ladder's rebuilt set carries the value.

The launcher option's band is checked in test_shadow_cascades.py (LauncherOptions);
LauncherDefault here checks the default 8 (2026-09-22): applied with --shadow-cascades,
absent without, the opt-out 0 forwarded, an explicit value kept, and the DLL's
fallback with the variable absent equal to the launcher's default.
"""
import importlib.util
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#include "renderer/shadow_replay_projection.h"
#include <cstdio>
#include <cstring>
using namespace x3m;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d %s\n", __LINE__, #x); ++failures; } } while (0)
static bool close_to(double a, double b) { return (a > b ? a - b : b - a) <= 1e-3 * (b > 1. ? b : 1.); }
static renderer::CameraState camera() {
    renderer::CameraState c{}; c.valid = true; c.m00 = .8f; c.m11 = 4.f / 3.f;
    const float r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; std::memcpy(c.r, r, sizeof r);
    return c;
}
int main() {
    const float production[5] = {250.f, 1500.f, 7500.f, 37500.f, 150000.f};
    unsigned sizes[5] = {4096, 4096, 4096, 4096, 4096};
    const unsigned all = 31;
    renderer::ShadowCascadeFootprintLaw law{};
    // ---- the cost note's table: P = 8 at 1280 px, m00 = 0.8 (run239's latch)
    CHECK(renderer::shadow_cascade_footprint_law(8.f, .8f, 1280, production, sizes, &all, 5, law));
    CHECK(law.count == 5 && law.px == 8.f && law.gates());
    std::printf("LAW px=8 width=1280 min=%.4f,%.4f,%.4f,%.4f,%.4f\n", double(law.min_units[0]), double(law.min_units[1]),
                double(law.min_units[2]), double(law.min_units[3]), double(law.min_units[4]));
    CHECK(close_to(double(law.min_units[0]), 3. * 2. * 250. / 4096.)); // no cascade below c0: the texel floor alone
    CHECK(close_to(double(law.min_units[1]), 8. * .95 * 250. * 2. / (.8 * 1280.)));
    CHECK(close_to(double(law.min_units[2]), 22.2656) && close_to(double(law.min_units[3]), 111.328) && close_to(double(law.min_units[4]), 556.64));
    // 1920 wide: the screen term scales by 1280/1920, the texel floor does not.
    renderer::ShadowCascadeFootprintLaw wide{};
    CHECK(renderer::shadow_cascade_footprint_law(8.f, .8f, 1920, production, sizes, &all, 5, wide));
    for (unsigned k = 1; k < 5; ++k) CHECK(close_to(double(wide.min_units[k]), double(law.min_units[k]) * 1280. / 1920.));
    CHECK(wide.min_units[0] == law.min_units[0]);
    // P = 1: the three-texel floor binds on every cascade (the note's "screen bound for P >= 7" on c4).
    renderer::ShadowCascadeFootprintLaw small{};
    CHECK(renderer::shadow_cascade_footprint_law(1.f, .8f, 1280, production, sizes, &all, 5, small));
    for (unsigned k = 0; k < 5; ++k) CHECK(close_to(double(small.min_units[k]), 3. * 2. * double(production[k]) / 4096.));
    // ---- the ladder: E_{k-1} is the nearest ACTIVE cascade, a dropped one is not gated at all
    const unsigned dropped_c3 = 31u & ~(1u << 3);
    renderer::ShadowCascadeFootprintLaw slid{};
    CHECK(renderer::shadow_cascade_footprint_law(8.f, .8f, 1280, production, sizes, &dropped_c3, 5, slid));
    // c3 is not gated at all; c4's screen term now stands on c2's extent (111 u), below its own
    // three-texel floor (220 u), so the floor binds: the gate loosens exactly as the ladder does.
    CHECK(slid.min_units[3] == 0.f && close_to(double(slid.min_units[4]), 3. * 2. * 150000. / 4096.));
    CHECK(slid.min_units[4] < law.min_units[4]);
    CHECK(!renderer::shadow_cascade_footprint_drops(slid, 3, 1.f)); // an inactive cascade has no gate
    // ---- off and unusable inputs
    renderer::ShadowCascadeFootprintLaw off{};
    CHECK(!renderer::shadow_cascade_footprint_law(0.f, .8f, 1280, production, sizes, &all, 5, off) && !off.count && !off.gates());
    CHECK(!renderer::shadow_cascade_footprint_drops(off, 4, 1.f) && renderer::shadow_cascade_footprint_gate(off, 31, law.min_units) == 31u);
    CHECK(!renderer::shadow_cascade_footprint_law(64.001f, .8f, 1280, production, sizes, &all, 5, off));
    CHECK(!renderer::shadow_cascade_footprint_law(0.f / 0.f, .8f, 1280, production, sizes, &all, 5, off));
    CHECK(!renderer::shadow_cascade_footprint_law(8.f, .8f, 1280, production, sizes, &all, 6, off)); // beyond shadow_cascade_max
    renderer::ShadowCascadeFootprintLaw no_camera{};
    CHECK(renderer::shadow_cascade_footprint_law(8.f, 0.f, 1280, production, sizes, &all, 5, no_camera)); // no latch: the texel floor stands
    for (unsigned k = 0; k < 5; ++k) CHECK(close_to(double(no_camera.min_units[k]), 3. * 2. * double(production[k]) / 4096.));
    renderer::ShadowCascadeFootprintLaw no_width{};
    CHECK(renderer::shadow_cascade_footprint_law(8.f, .8f, 0, production, sizes, &all, 5, no_width) && no_width == no_camera);
    // ---- the drop decision
    const float bound = law.min_units[4];
    CHECK(renderer::shadow_cascade_footprint_drops(law, 4, bound - 1.f));
    CHECK(!renderer::shadow_cascade_footprint_drops(law, 4, bound));        // exactly at the bound: kept
    CHECK(!renderer::shadow_cascade_footprint_drops(law, 4, bound + 1.f));
    CHECK(!renderer::shadow_cascade_footprint_drops(law, 4, 0.f));           // unknown (no extent read yet): kept
    CHECK(!renderer::shadow_cascade_footprint_drops(law, 4, 0.f / 0.f));
    CHECK(!renderer::shadow_cascade_footprint_drops(law, 5, 1.f));           // beyond the set
    // The census sizes of the note: a 50 u antenna leaves c3 and c4, a 200 u part leaves c4 only,
    // 600 u and a 5,000 u hull stay everywhere, and nothing ever leaves c0..c2.
    const float census[4] = {50.f, 200.f, 600.f, 5000.f};
    const unsigned expect[4] = {0x18u, 0x10u, 0u, 0u};
    for (unsigned i = 0; i < 4; ++i) {
        float measures[renderer::shadow_cascade_max];
        for (float& m : measures) m = census[i];
        unsigned refused = 0;
        const unsigned kept = renderer::shadow_cascade_footprint_gate(law, 31, measures, &refused);
        std::printf("GATE units=%.0f refused=%u kept=%u\n", double(census[i]), refused, kept);
        CHECK(refused == expect[i] && kept == (31u & ~expect[i]));
    }
    { // one measure per cascade: only the cascade whose own box is small loses its bit
        float measures[renderer::shadow_cascade_max] = {1.f, 1000.f, 1000.f, 1000.f, 1000.f};
        unsigned refused = 0;
        CHECK(renderer::shadow_cascade_footprint_gate(law, 31, measures, &refused) == 31u && refused == 0u); // 1 u is above c0's 0.366 texel floor
        measures[0] = .1f;
        CHECK(renderer::shadow_cascade_footprint_gate(law, 31, measures, &refused) == 30u && refused == 1u);
        CHECK(renderer::shadow_cascade_footprint_gate(law, 0, measures, &refused) == 0u && refused == 0u); // an empty mask: nothing to gate
    }
    // ---- the measure: the largest LATERAL side, never the depth side
    {
        const float smin[3] = {-500.f, -25.f, -50000.f}, smax[3] = {500.f, 25.f, 50000.f}; // a 1,000 x 50 u strut, 100,000 u deep
        CHECK(close_to(double(renderer::shadow_cascade_footprint_lateral(smin, smax)), 1000.));
        CHECK(!renderer::shadow_cascade_footprint_drops(law, 4, renderer::shadow_cascade_footprint_lateral(smin, smax)));
        const float thin[3] = {-1.f, -1.f, -50000.f}, thick[3] = {1.f, 1.f, 50000.f}; // a depth-long sliver, 2 u across: gated
        CHECK(close_to(double(renderer::shadow_cascade_footprint_lateral(thin, thick)), 2.) && renderer::shadow_cascade_footprint_drops(law, 4, 2.f));
        const float bad[3] = {0.f, 0.f, 0.f};
        CHECK(renderer::shadow_cascade_footprint_lateral(bad, bad) == 0.f);
    }
    // ---- the box test's own output
    {
        renderer::ShadowCascadeSet set{};
        const float extents[3] = {250.f, 1500.f, 7500.f};
        CHECK(renderer::shadow_cascade_set(extents, 3, nullptr, nullptr, 640, set));
        const float sun[4] = {.30151134f, .90453403f, -.30151134f, 0};
        renderer::ShadowCascadeBounds bounds{};
        const auto c = camera();
        CHECK(renderer::shadow_cascade_bounds(c, sun, set, bounds) && bounds.shared);
        // A draw at the origin whose object AABB is 40 x 40 x 40: rows = the camera's own product.
        float rows[16]{};
        rows[0] = c.m00; rows[5] = c.m11; rows[10] = 1.f; rows[14] = 1.f; rows[11] = 200.f; rows[15] = 200.f;
        const float lo[3] = {-20, -20, -20}, hi[3] = {20, 20, 20};
        float lateral[renderer::shadow_cascade_max]{}, extent = 0.f;
        const int mask = renderer::shadow_cascade_bounds_mask(c, rows, bounds, lo, hi, nullptr, &extent, lateral);
        CHECK(mask > 0);
        std::printf("MASK mask=%d extent=%.4f lateral=%.4f,%.4f,%.4f\n", mask, double(extent), double(lateral[0]), double(lateral[1]), double(lateral[2]));
        for (unsigned k = 1; k < renderer::shadow_cascade_max; ++k) CHECK(lateral[k] == lateral[0]); // one shared box
        CHECK(lateral[0] > 0.f && lateral[0] <= extent);                                            // a lateral side is never the largest of the three
        // The whole-box extent of an axis-aligned 40-unit cube is 40 within the basis rotation.
        CHECK(close_to(double(extent), 40. * 1.7320508) || extent <= 40.f * 1.7320509f);
        // Per-cascade suns: every cascade forms its own box, so each gets its own measure.
        float suns[renderer::shadow_cascade_max * 4]{};
        for (unsigned k = 0; k < 3; ++k) { std::memcpy(suns + k * 4, sun, 16); }
        suns[4] = .8f; suns[5] = .6f; suns[6] = 0.f; // cascade 1 holds another direction
        renderer::ShadowCascadeBounds split{};
        CHECK(renderer::shadow_cascade_bounds_suns(c, suns, set, split) && !split.shared);
        float per_cascade[renderer::shadow_cascade_max]{};
        CHECK(renderer::shadow_cascade_bounds_mask(c, rows, split, lo, hi, nullptr, nullptr, per_cascade) >= 0);
        CHECK(per_cascade[0] > 0.f && per_cascade[1] > 0.f && per_cascade[1] != per_cascade[0]);
        // An unknown mask leaves the measures zeroed.
        float broken[16]; for (float& v : broken) v = 0.f / 0.f;
        float none[renderer::shadow_cascade_max]{1.f, 1.f, 1.f, 1.f, 1.f};
        CHECK(renderer::shadow_cascade_bounds_mask(c, broken, bounds, lo, hi, nullptr, nullptr, none) == -1);
        for (unsigned k = 0; k < renderer::shadow_cascade_max; ++k) CHECK(none[k] == 0.f);
    }
    // ---- the option's band on the set, and the adaptive ladder carrying it
    {
        renderer::ShadowCascadeSet set{};
        CHECK(renderer::shadow_cascade_set(production, 5, sizes, nullptr, 640, set));
        CHECK(set.min_footprint_px == 0.f);
        CHECK(renderer::shadow_cascade_pool(set, nullptr, renderer::shadow_cascade_static_from_none, false, 0.f, renderer::shadow_cascade_backface_from_texel, 8.f) && set.min_footprint_px == 8.f);
        CHECK(renderer::shadow_cascade_pool(set, nullptr, renderer::shadow_cascade_static_from_none, false, 0.f, renderer::shadow_cascade_backface_from_texel, 64.f));
        CHECK(renderer::shadow_cascade_pool(set, nullptr, renderer::shadow_cascade_static_from_none, false, 0.f, renderer::shadow_cascade_backface_from_texel, 0.f) && set.min_footprint_px == 0.f);
        const float bad_values[4] = {64.001f, 1e9f, -1.f, 0.f / 0.f};
        for (const float bad : bad_values) {
            renderer::ShadowCascadeSet refused = set;
            CHECK(!renderer::shadow_cascade_pool(refused, nullptr, renderer::shadow_cascade_static_from_none, false, 0.f, renderer::shadow_cascade_backface_from_texel, bad));
        }
        CHECK(renderer::shadow_cascade_pool(set, nullptr, renderer::shadow_cascade_static_from_none, false, 0.f, renderer::shadow_cascade_backface_from_texel, 8.f));
        renderer::ShadowCascadeSet adapted{};
        CHECK(renderer::shadow_cascade_adapt_c0(set, 675.f, adapted) && adapted.min_footprint_px == 8.f); // a policy in pixels does not slide with the extents
        renderer::ShadowCascadeFootprintLaw slid_law{};
        float slid_extents[renderer::shadow_cascade_max]{}; unsigned slid_sizes[renderer::shadow_cascade_max]{};
        for (unsigned k = 0; k < adapted.count; ++k) { slid_extents[k] = adapted.cascades[k].half_extent; slid_sizes[k] = adapted.cascades[k].size; }
        const unsigned active = adapted.active;
        CHECK(renderer::shadow_cascade_footprint_law(adapted.min_footprint_px, .8f, 1280, slid_extents, slid_sizes, &active, adapted.count, slid_law));
        CHECK(slid_law.min_units[1] > law.min_units[1]); // E0 675 instead of 250: cascade 1's bound grows with it
    }
    std::printf("RESULT %s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
'''


class FootprintLaw(unittest.TestCase):
    def test_driver(self):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            self.skipTest('no host C++ compiler')
        with tempfile.TemporaryDirectory(prefix='x3-cascade-footprint-') as directory:
            work = Path(directory)
            (work / 'driver.cpp').write_text(DRIVER)
            subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                            str(work / 'driver.cpp'), '-o', str(work / 'driver')], check=True)
            result = subprocess.run([str(work / 'driver')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout[-4000:] + result.stderr[-2000:])
            self.assertIn('RESULT PASS failures=0', result.stdout)
            # The resolved table the cost note quotes, printed by the driver.
            self.assertIn('LAW px=8 width=1280 min=0.3662,3.7109,22.2656,111.3281,556.6406', result.stdout)
            self.assertEqual(result.stdout.count('GATE '), 4)


class EnvironmentParse(unittest.TestCase):
    """The option's environment read in src/proxy/capture.cpp: an explicit zero or any
    non-positive value is off (a zero footprint must never disable the cascades), a
    truncated variable and an out-of-band or malformed value refuse with
    reason=min_footprint, and the value reaches shadow_cascade_pool."""

    def test_capture_block(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        block = source[source.index('X3M_SHADOW_CASCADE_MIN_FOOTPRINT'):]
        block = block[:block.index('shadow_cascade_pool(')]
        self.assertIn('float min_footprint=renderer::shadow_cascade_min_footprint_default;', block)  # absent: the default 8
        self.assertIn('if(n>=128)reason="min_footprint";', block)          # truncation is refused, never read
        self.assertIn('else if(v<=0.)min_footprint=0.f;', block)          # "0", "0.0", "-1": off
        self.assertIn('!(v==v))reason="min_footprint"', block)            # NaN
        self.assertIn('shadow_cascade_min_footprint_valid(v))reason="min_footprint"', block)
        self.assertIn('large_min,backface_from,min_footprint))reason="pool"', source.replace(' ', ''))


class LawMatchesTheNote(unittest.TestCase):
    """The law in the header is the one the cost policy note states."""

    def test_source_states_the_law(self):
        header = (ROOT / 'src/renderer/shadow_cascade_footprint_core.h').read_text()
        self.assertIn('min_k = max(P x 0.95 x E_{k-1} x 2 / (m00 x width), 3 x texel_k)', header)
        self.assertIn('constexpr float shadow_cascade_footprint_select_margin = .95f', header)
        self.assertIn('constexpr float shadow_cascade_footprint_texels = 3.f', header)
        self.assertIn('constexpr float shadow_cascade_min_footprint_max = 64.f', header)


def _launch(directory, *args):
    spec = importlib.util.spec_from_file_location('footprint_cascade_launch', ROOT / 'verification/analysis/test_shadow_cascades.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module.launch(directory, *args)


class LauncherDefault(unittest.TestCase):
    """The default 8 px (user selection after run251/run253, 2026-09-22)."""
    BASE = ['--motion-output', '--ownership', '--shadow-replay-depth']
    NAME = 'X3M_SHADOW_CASCADE_MIN_FOOTPRINT'

    def env(self, directory, *args):
        code, output, error = _launch(directory, *self.BASE, *args)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_default_rule(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.env(directory, '--shadow-cascades', 'default')[self.NAME], '8.0')           # applied
            self.assertEqual(self.env(directory, '--shadow-cascades', '250,1500,7500,37500,150000')[self.NAME], '8.0')
            self.assertNotIn(self.NAME, self.env(directory))                                                 # absent without the cascades
            self.assertEqual(self.env(directory, '--shadow-cascades', 'default', '--shadow-cascade-min-footprint', '0')[self.NAME], '0.0')  # opt-out
            self.assertEqual(self.env(directory, '--shadow-cascades', 'default', '--shadow-cascade-min-footprint', '24')[self.NAME], '24.0')  # explicit

    def test_dll_fallback_matches_the_launcher(self):
        header = (ROOT / 'src/renderer/shadow_cascade_footprint_core.h').read_text()
        dll = float(re.search(r'constexpr float shadow_cascade_min_footprint_default = ([0-9.]+)f;', header).group(1))
        spec = importlib.util.spec_from_file_location('footprint_manage', ROOT / 'tools/manage.py')
        manage = importlib.util.module_from_spec(spec); spec.loader.exec_module(manage)
        self.assertEqual(dll, manage.SHADOW_CASCADE_MIN_FOOTPRINT_DEFAULT)
        self.assertEqual(dll, 8.0)


if __name__ == '__main__':
    unittest.main()
