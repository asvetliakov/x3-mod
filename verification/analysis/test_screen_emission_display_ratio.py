"""Run-17 defect measurement (screen_emission_display_ratio): the packed law
equals the native screen blend for one fragment on black, loses the soft
tail under overlap, and option (a) is native by construction."""
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

    def test_option_a_is_native_for_every_overlap_and_background(self):
        for overlap in (1, 8, 16):
            for background in ((0.0, 0.0, 0.0), (0.2, 0.22, 0.16)):
                rows, _ = ratio.measure(background=background, overlap=overlap)
                for row in rows:
                    self.assertEqual(row['option_a'], row['native'], (overlap, background, row))


if __name__ == '__main__':
    unittest.main()
