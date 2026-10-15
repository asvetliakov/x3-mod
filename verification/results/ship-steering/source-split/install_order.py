"""The hook install order across the source split, in the source: the sequence of engine_patch calls, slot / live-byte writes and the
calls to install_all inside install_all(), x3m::ship_steering::initialize() and shutdown(), in statement order, from bf12aaa2's
ship_steering.cpp and from the split's ship_steering_install.cpp. Statement order is execution order for these calls (each has side
effects the compiler may not reorder); the DLL's call instructions are not in that order after block layout, so the source is compared.
  python3 -I install_order.py BEFORE.cpp AFTER.cpp       (e.g. git show bf12aaa2:src/proxy/ship_steering.cpp > /tmp/b.cpp)"""
import re, sys

SIGS = ('\nbool install_all(', '\nbool initialize() {', '\nbool shutdown() {')
EVENT = re.compile(r'engine_patch::\w+|\binstall_all\(|\bx3m_ship_\w+\s*=(?!=)|\b(?:patches_|gate_patches_)\[[^\]]*\]|\btraffic_patch_\b|'
                   r'\bwarn_site_\b|\b\w+_live_\.store\([^)]*\)')


def strip(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return re.sub(r'"(?:[^"\\]|\\.)*"', '""', text)


def body(src, sig):
    start = src.index(sig)
    depth, i = 0, src.index('{', start)
    while True:
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
        i += 1


def events(path):
    src = strip(open(path).read())
    out = []
    for sig in SIGS:
        out.append('== ' + sig.strip())
        out += [re.sub(r'\s+', ' ', m.group(0)) for m in EVENT.finditer(body(src, sig))]
    return out


def main():
    a, b = events(sys.argv[1]), events(sys.argv[2])
    same = a == b
    print('install order (install_all, initialize, shutdown): %d events before, %d after; %s' % (len(a), len(b),
                                                                                           'identical' if same else 'DIFFERENT'))
    if not same:
        import difflib
        for l in difflib.unified_diff(a, b, lineterm='', n=1):
            print('   ', l)
    return 0 if same else 1


if __name__ == '__main__':
    sys.exit(main())
