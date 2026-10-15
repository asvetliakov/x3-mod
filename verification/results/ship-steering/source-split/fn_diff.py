"""Per-function code identity of two builds (the source split of 2026-10-15 is pure moves: every function compiles to the same
instructions). Disassembles both binaries (objdump -d --no-show-raw-insn -C), keeps the functions whose name matches --match,
normalises addresses (immediate addresses, call/jump targets, rip/abs offsets to symbols keep only the symbol name) and compares
each function's instruction list.

  python3 -I fn_diff.py BEFORE AFTER [--objdump i686-w64-mingw32-objdump] [--match REGEX] [--show N]

Prints: functions only before, only after, identical, differing (with their instruction counts); exit 0."""
import argparse, re, subprocess, sys
from collections import defaultdict


def functions(path, objdump, match):
    out = subprocess.run([objdump, '-d', '--no-show-raw-insn', '-C', '-w', path], capture_output=True, text=True, check=True).stdout
    fns = defaultdict(list)
    cur = None
    for line in out.split('\n'):
        m = re.match(r'^[0-9a-f]+ <(.*)>:$', line)
        if m:
            name = m.group(1)
            # anonymous-namespace and the split's runtime namespace are one namespace for this comparison
            name = name.replace('x3m_ship_steering_rt::', '(anonymous namespace)::')
            name = re.sub(r'\[clone \.(part|isra|constprop|cold)\.\d+\]', r'[clone .\1]', name)
            cur = name if re.search(match, name) else None
            continue
        if cur is None or not line.strip():
            continue
        m = re.match(r'^\s*[0-9a-f]+:\s*(.*)$', line)
        if not m:
            continue
        ins = m.group(1)
        ins = re.sub(r'x3m_ship_steering_rt::', '(anonymous namespace)::', ins)
        ins = re.sub(r'\b0x[0-9a-f]{5,}\b', 'ADDR', ins)              # absolute addresses
        ins = re.sub(r'\b[0-9a-f]{5,}\s+<', '<', ins)                  # call/jmp targets: keep the symbol
        ins = re.sub(r'<([^>+]*)\+0x[0-9a-f]+>', r'<\1+OFF>', ins)      # label offsets inside a function
        ins = re.sub(r'\[clone \.(part|isra|constprop|cold)\.\d+\]', r'[clone .\1]', ins)
        fns[cur].append(ins)
    return fns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('before')
    ap.add_argument('after')
    ap.add_argument('--objdump', default='i686-w64-mingw32-objdump')
    ap.add_argument('--match', default='.')
    ap.add_argument('--show', type=int, default=20)
    a = ap.parse_args()
    b = functions(a.before, a.objdump, a.match)
    c = functions(a.after, a.objdump, a.match)
    only_b = sorted(set(b) - set(c))
    only_c = sorted(set(c) - set(b))
    same = [n for n in b if n in c and b[n] == c[n]]
    diff = [n for n in b if n in c and b[n] != c[n]]
    print('functions before %d after %d; identical %d, differing %d, only before %d, only after %d' %
          (len(b), len(c), len(same), len(diff), len(only_b), len(only_c)))
    for n in diff[:a.show]:
        print('  DIFF %s (%d -> %d instructions)' % (n[:150], len(b[n]), len(c[n])))
    for n in only_b[:a.show]:
        print('  ONLY BEFORE %s (%d instructions)' % (n[:150], len(b[n])))
    for n in only_c[:a.show]:
        print('  ONLY AFTER %s (%d instructions)' % (n[:150], len(c[n])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
