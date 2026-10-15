"""B.5 (gate-traffic-controller.md "B.5 Pointer liveness"): the gate traffic controller's frame boundary reads no engine memory. Scans
ship_steering.cpp from ctl_member_frame (the first function ctl_frame runs) through ctl_rows, and frame_boundary's body, and both
ship_gate_ctl*_core.h headers, for the engine readers (rd8/rd16/rd32/rds32, RawMem, call_eax/call_ecx): 0 hits."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROXY = ROOT / 'src/proxy'
READ = re.compile(r'\b(rd8|rd16|rd32|rds32)\s*\(|\bRawMem\b|\bcall_e[ac]x\b')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def function_body(src, signature):
    """The text of the function whose definition starts with `signature` (a line start), through its closing brace."""
    start = src.index(signature)
    depth, i = 0, src.index('{', start)
    while True:
        c = src[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
        i += 1


def hits(text):
    return [m.group(0) for m in READ.finditer(strip_comments(text))]


class BoundaryReads(unittest.TestCase):
    def test_controller_frame_reads_nothing(self):
        src = (PROXY / 'ship_steering.cpp').read_text()
        start = src.index('\nvoid ctl_member_frame(')
        end_fn = function_body(src, '\nvoid ctl_rows(')
        span = src[start:src.index(end_fn) + len(end_fn)]
        for name in ('ctl_member_frame', 'sb_frame_all', 'ctl_live_prove', 'ctl_live_leaves', 'ctl_frame', 'ctl_rows'):
            self.assertIn('void %s(' % name, span)  # the scanned span holds the boundary's functions
        self.assertEqual(hits(span), [])
        self.assertEqual(hits(function_body(src, '\nvoid frame_boundary(')), [])

    def test_e_obst_never_the_player_ship(self):
        # lane fix round 2 review item 4: site E's lane obstacle row returns before any bound when B is the player's ship (the member
        # exclusion's flag test, [obj+0x40] & kFlagPlayerObject), not only for A
        body = strip_comments(function_body((PROXY / 'ship_steering.cpp').read_text(), '\ninline void ctl_e_obst('))
        test = re.search(r'if \(cls_b == sites::kClassShip && \(rd32\(b \+ sites::kObjFlags\) & sites::kFlagPlayerObject\)\) return;', body)
        self.assertIsNotNone(test)
        self.assertLess(test.start(), body.index('gc::ObstBound o;'))

    def test_core_headers_read_nothing(self):
        for name in ('ship_gate_ctl_core.h', 'ship_gate_ctl_live_core.h'):
            self.assertEqual(hits((PROXY / name).read_text()), [], name)


if __name__ == '__main__':
    unittest.main()
