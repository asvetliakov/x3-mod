# Run162: frame -> (game_ms, real_ms, seta) map from the extracted rows (ship_trace t_ms carried forward; frame_end elapsed_ms;
# engine_stage seta). Imported by the sb_*.py scripts: tm = load(rows); tm.game(f), tm.real(f), tm.frame_at_game(ms).
import re, bisect
class TM:
    def __init__(s, path):
        g = {}; r = {}; s.seta = {}
        for l in open(path, errors='replace'):
            if l.startswith('ship_trace frame='):
                m = re.match(r'ship_trace frame=(\d+) t_ms=(\d+)', l); f = int(m[1])
                if f not in g: g[f] = int(m[2])
            elif l.startswith('ship_gate_ctl') or l.startswith('ship_gate_launch') or l.startswith('ship_gate_transition'):
                m = re.search(r' frame=(\d+) t_ms=(\d+)', l)
                if m and int(m[1]) not in g: g[int(m[1])] = int(m[2])
            elif l.startswith('frame_end '):
                m = re.search(r' frame=(\d+) .*elapsed_ms=(\d+)', l)
                if m: r[int(m[1])] = int(m[2])
            elif l.startswith('engine_stage ') and 'seta=' in l:
                m = re.search(r' frame=(\d+).*seta=(\d)', l); s.seta[int(m[1])] = int(m[2])
        s.gf = sorted(g); s.gv = [g[f] for f in s.gf]; s.rf = sorted(r); s.rv = [r[f] for f in s.rf]
    def game(s, f):
        i = bisect.bisect_right(s.gf, f) - 1; return s.gv[max(i, 0)]
    def real(s, f):
        i = bisect.bisect_right(s.rf, f) - 1; return s.rv[max(i, 0)]
    def frame_at_game(s, ms):
        i = bisect.bisect_left(s.gv, ms); return s.gf[min(i, len(s.gf) - 1)]
def load(path): return TM(path)
