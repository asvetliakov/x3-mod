import array
import math
import tempfile
import unittest
from pathlib import Path

import voice_pcm_scan as scan

RATE = 44100
BUFFER_SAMPLES = RATE  # 1 s per application sample, as the replica's two-second buffer would be
FORMAT = dict(tag=1, channels=1, rate=RATE, bits=16)


def synthetic():
    """Three buffers of a 220 Hz tone carrying exactly one click, one dropout and one gap.

    Buffer 0: clean. Buffer 1: a click at 0.5 s into it (an interior jump, well past
    the 10 ms boundary window) and a 20 ms run of zeros at 0.75 s inside the tone.
    Buffer 2 starts 30 ms after buffer 1 ends (a timestamp gap) and is clean.
    """
    samples = array.array('h')
    for i in range(3 * BUFFER_SAMPLES):
        samples.append(int(12000 * math.sin(2 * math.pi * 220 * i / RATE)))
    click = BUFFER_SAMPLES + RATE // 2
    samples[click] = -30000 if samples[click] > 0 else 30000
    drop = BUFFER_SAMPLES + (3 * RATE) // 4
    for i in range(drop, drop + int(RATE * 0.020)):
        samples[i] = 0
    rows = []
    offset = 0
    t = 0.0
    for n in range(3):
        if n == 2:
            t += 0.030
        rows.append(dict(cycle=n + 1, bytes=BUFFER_SAMPLES * 2, offset=offset, t_start=t,
                         t_end=t + BUFFER_SAMPLES / RATE, src='sample'))
        offset += BUFFER_SAMPLES * 2
        t = rows[-1]['t_end'] if n != 2 else t
    return samples, rows, click, drop


class VoicePcmScanTests(unittest.TestCase):
    def test_synthetic_signal_reports_one_of_each_artefact(self):
        samples, rows, click, drop = synthetic()
        r = scan.scan(samples, FORMAT, rows)
        self.assertEqual(r['samples'], 3 * BUFFER_SAMPLES)
        self.assertAlmostEqual(r['seconds'], 3.0, places=6)
        self.assertEqual((r['jumps_interior'], r['jumps_boundary']), (2, 0))  # into and out of the single click
        self.assertEqual([e['t'] for e in r['first']['jump_interior']],
                         [round(click / RATE, 6), round((click + 1) / RATE, 6)])
        self.assertEqual(r['dropouts'], 1)
        self.assertEqual(r['first']['dropout'][0]['t'], round(drop / RATE, 6))
        self.assertEqual(r['first']['dropout'][0]['ms'], 20.0)
        self.assertEqual((r['timestamp_gaps'], r['timestamp_overlaps']), (1, 0))
        self.assertEqual(r['first']['gap'][0]['ms'], 30.0)
        self.assertEqual(r['peak'], 30000)

    def test_boundary_jumps_are_counted_separately_and_clean_audio_is_clean(self):
        samples, rows, _, _ = synthetic()
        clean = array.array('h', [int(12000 * math.sin(2 * math.pi * 220 * i / RATE)) for i in range(3 * BUFFER_SAMPLES)])
        for row in rows:
            row['t_start'] = row['offset'] / 2 / RATE
            row['t_end'] = row['t_start'] + BUFFER_SAMPLES / RATE
        r = scan.scan(clean, FORMAT, rows)
        self.assertEqual((r['jumps_interior'], r['jumps_boundary'], r['dropouts'], r['timestamp_gaps']), (0, 0, 0, 0))
        # A seam at the start of buffer 2 lands in the boundary class, not the interior one.
        seam = 2 * BUFFER_SAMPLES
        clean[seam] = 30000 if clean[seam] < 0 else -30000
        r = scan.scan(clean, FORMAT, rows)
        self.assertEqual(r['jumps_boundary'], 2)
        self.assertEqual(r['jumps_interior'], 0)
        self.assertEqual(r['first']['jump_boundary'][0]['t'], round(seam / RATE, 6))
        # Silence outside speech is not a dropout: zeroing a whole trailing buffer reports nothing.
        for i in range(2 * BUFFER_SAMPLES, 3 * BUFFER_SAMPLES):
            clean[i] = 0
        self.assertEqual(scan.scan(clean, FORMAT, rows)['dropouts'], 0)

    def test_log_parsing_and_round_trip_through_a_file(self):
        samples, rows, _, _ = synthetic()
        text = ('REPLICA_HEADER schema=1 mode=game-dmo-hook\n'
                'REPLICA_PCM_FORMAT stream=1 tag=1 ch=1 rate=44100 bits=16\n'
                + ''.join(f"REPLICA_PCM cycle={r['cycle']} bytes={r['bytes']} t_start={int(r['t_start']*1e7)} "
                          f"t_end={int(r['t_end']*1e7)} src=sample offset={r['offset']} written={r['bytes']}\n"
                          for r in rows))
        log = scan.parse_log(text)
        self.assertEqual(log['format'], FORMAT)
        self.assertEqual(len(log['buffers']), 3)
        self.assertAlmostEqual(log['buffers'][2]['t_start'], rows[2]['t_start'], places=6)
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'dump.pcm'
            path.write_bytes(samples.tobytes())
            loaded = scan.read_pcm(path, log['format'])
            self.assertEqual(len(loaded), len(samples))
            r = scan.scan(loaded, log['format'], log['buffers'])
            self.assertEqual((r['jumps_interior'], r['dropouts'], r['timestamp_gaps']), (2, 1, 1))
        with self.assertRaises(ValueError):
            scan.parse_log('REPLICA_HEADER schema=1\n')
        with self.assertRaises(ValueError):
            scan.read_pcm('/dev/null', dict(FORMAT, channels=2))


if __name__ == '__main__':
    unittest.main()
