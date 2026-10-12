"""Replay check of join_budget.py's plant + A13 law against the Run157 lead-in leg of the M2 leader 0x8c30aad0
(measured start, frame 18000: s = -5600 m from X, |e| = 1838 m, nose 71.1 deg off F toward the axis, yaw rate 606 binary/s
= 3.3 deg/s unwinding; measured 17.55 s later at frame 18240: |e| 430 m, nose 32.3 deg, rate 418 = 2.3 deg/s; the leg was
then cut by the engine's 0x12 at 18243). Usage: python3 -I a13_replay.py"""
import importlib.util, math, os
sp = importlib.util.spec_from_file_location('jb', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'join_budget.py'))
jb = importlib.util.module_from_spec(sp); sp.loader.exec_module(jb)
sh = jb.SHIPS['M2']
S = jb.Sim(sh, -5600.0, 1838.0, -71.1); S.w = math.radians(606 / 65536 * 360)
def law():
    L = max(0.75 * S.v, 2 * S.v / S.W, 0.3 * sh['r']); S.step(min(S.x + L, jb.CROSS), 0.0, 100.0)
while S.t < 17.55: law()
print('model at +17.55 s: |e| %.0f m (measured 430), nose %.1f deg (measured 32.3), rate %.1f deg/s (measured 2.3)' % (abs(S.y), abs(math.degrees(S.psi)), abs(math.degrees(S.w))))
while S.x < 0 and S.t < 120: law()
print('model continued to X (no 0x12): t %.1f s, nose %.1f deg, |e| %.0f m, axis crossings %d, largest excursion past the axis %.0f m' % (S.t, S.nose_err(), abs(S.y), S.cross, S.over))
