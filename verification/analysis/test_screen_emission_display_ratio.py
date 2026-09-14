"""Run-17 defect measurement (screen_emission_display_ratio): the withdrawn
step C packed law equals the native screen blend for one fragment on black
and loses the soft tail under overlap; option (a) and the ratified step E law
at g = 1 are native by construction (tail/core parity), and g > 1 lifts the
bolt without changing which pixels it touches."""
import unittest

import screen_emission_display_ratio as ratio


class DisplayRatioTests(unittest.TestCase):
    def test_single_fragment_on_black_is_native(self):
        rows, _ = ratio.measure(overlap=1)
        for row in rows:
            self.assertAlmostEqual(row['packed_gain_1'], row['native'], places=9, msg=row)

    def test_overlap_collapses_the_tail_but_not_the_core(self):
        rows, ratios = ratio.measure(overlap=8)
        core, tail = rows[0], rows[-1]
        self.assertGreater(core['packed_gain_1'] / core['native'], 0.8)
        self.assertLess(tail['packed_gain_1'] / tail['native'], 0.3)
        self.assertLess(ratios['packed_gain_1'][-1], ratios['native'][-1] / 3)
        # A gain lifts the core past native before the tail ratio recovers.
        self.assertGreater(core['packed_gain_4'], core['native'])
        self.assertLess(ratios['packed_gain_4'][-1], ratios['native'][-1])

    def test_step_e_at_unit_gain_is_native_with_the_tail_to_core_ratio(self):
        for overlap in (1, 8, 16):
            for background in ((0.0, 0.0, 0.0), (0.2, 0.22, 0.16)):
                rows, ratios = ratio.measure(background=background, overlap=overlap)
                for row in rows:
                    self.assertAlmostEqual(row['step_e_gain_1'], row['native'], places=9, msg=(overlap, background, row))
                for a, b in zip(ratios['step_e_gain_1'], ratios['native']):
                    self.assertAlmostEqual(a, b, places=9, msg=(overlap, background))
        # The float64 law itself, not only its display luminance, is native at g = 1.
        chain = ratio.fragments(3.0, 8, 3.0, 2.0)
        for background in ((0.0, 0.0, 0.0), (0.2, 0.22, 0.16)):
            for a, b in zip(ratio.step_e(background, chain, gain=1.0), ratio.native(background, chain)):
                self.assertAlmostEqual(a, b, places=12)

    def test_step_e_gain_lifts_the_whole_bolt_and_keeps_the_withdrawn_law_distinct(self):
        rows, ratios = ratio.measure(overlap=8)
        for row in rows:
            self.assertGreater(row['step_e_gain_2'], row['native'], row)
            self.assertGreater(row['step_e_gain_4'], row['step_e_gain_2'], row)
        # The withdrawn law collapsed the tail; step E keeps the tail/core ratio at every gain within the display's compression.
        self.assertLess(ratios['packed_gain_1'][-1], ratios['native'][-1] / 3)
        self.assertGreater(ratios['step_e_gain_2'][-1], ratios['packed_gain_1'][-1] * 2)
        # Untouched pixels stay untouched: E = 0 where the chain wrote nothing.
        self.assertEqual(ratio.step_e((0.2, 0.22, 0.16), [], gain=4.0), tuple(ratio.encode(ratio.decode(x)) for x in (0.2, 0.22, 0.16)))

    def test_option_a_is_native_for_every_overlap_and_background(self):
        for overlap in (1, 8, 16):
            for background in ((0.0, 0.0, 0.0), (0.2, 0.22, 0.16)):
                rows, _ = ratio.measure(background=background, overlap=overlap)
                for row in rows:
                    self.assertEqual(row['option_a'], row['native'], (overlap, background, row))


if __name__ == '__main__':
    unittest.main()
