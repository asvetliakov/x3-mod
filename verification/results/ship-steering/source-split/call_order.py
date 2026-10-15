"""The sequence of direct calls (callee names, in address order) inside one function of two DLLs, compared: the hook install order of
x3m::ship_steering::initialize (the engine_patch claims and the slot writes) across the source split.
  python3 -I call_order.py BEFORE.dll AFTER.dll 'x3m::ship_steering::initialize()' [--filter REGEX]"""
import argparse, re, subprocess, sys


def calls(dll, fn, filt):
    out = subprocess.run(['i686-w64-mingw32-objdump', '-d', '--no-show-raw-insn', '-C', '-w', dll], capture_output=True, text=True,
                         check=True).stdout
    seq, inside = [], False
    for line in out.split('\n'):
        m = re.match(r'^[0-9a-f]+ <(.*)>:$', line)
        if m:
            inside = m.group(1) == fn
            continue
        if inside:
            m = re.search(r'\bcall\s+[0-9a-f]+ <([^>]*)>', line)
            if m:
                name = re.sub(r'\(.*', '', m.group(1).replace('(anonymous namespace)::', '').replace('x3m_ship_steering_rt::', ''))
                if re.search(filt, name):
                    seq.append(name)
    return seq


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('before')
    ap.add_argument('after')
    ap.add_argument('function')
    ap.add_argument('--filter', default='engine_patch|install|claim|restore|stub')
    a = ap.parse_args()
    b, c = calls(a.before, a.function, a.filter), calls(a.after, a.function, a.filter)
    print('%s: %d calls before, %d after; sequence %s' % (a.function, len(b), len(c), 'identical' if b == c else 'DIFFERENT'))
    for x in b:
        print('   ', x)
    if b != c:
        print('  after:')
        for x in c:
            print('   ', x)
    return 0 if b == c else 1


if __name__ == '__main__':
    sys.exit(main())
