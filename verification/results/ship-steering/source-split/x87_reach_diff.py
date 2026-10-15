"""The functions check_no_x87.py walks from its roots in two DLLs, and the difference (the source split of 2026-10-15: which functions a
split made reachable as separate symbols). Uses check_no_x87.py's own disassembly and walk.
  python3 x87_reach_diff.py BEFORE.dll AFTER.dll"""
import json, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import check_no_x87 as cx  # noqa: E402


def reach(dll):
    out = subprocess.run([sys.executable, str(ROOT / 'verification/probe/check_no_x87.py'), str(dll)], capture_output=True, text=True)
    summary = json.loads(out.stdout)
    functions = cx.disassemble(Path(dll))
    seen = cx.walk(functions, summary['roots'].values())
    return summary, set(seen)


def demangle(names):
    r = subprocess.run(['i686-w64-mingw32-c++filt', '-_'], input='\n'.join(names), capture_output=True, text=True)
    return r.stdout.split('\n')


def main():
    a, ra = reach(sys.argv[1])
    b, rb = reach(sys.argv[2])
    print('before: %s, reachable %d, violations %d' % (a['result'], a['reachable_functions'], len(a['violations'])))
    print('after:  %s, reachable %d, violations %d' % (b['result'], b['reachable_functions'], len(b['violations'])))
    norm = lambda n: re.sub(r'(12_GLOBAL__N_1)L(\d)', r'\1\2', re.sub(r'\.(part|isra|constprop|cold)\.\d+', r'.\1',
                                                                             n.replace('20x3m_ship_steering_rt', '12_GLOBAL__N_1')))
    na = {norm(n) for n in ra}
    nb = {norm(n) for n in rb}
    for tag, s in (('only before', sorted(na - nb)), ('only after', sorted(nb - na))):
        print('%s: %d' % (tag, len(s)))
        for n, d in zip(s, demangle(s)):
            print('   ', d[:160])
    return 0


if __name__ == '__main__':
    sys.exit(main())
