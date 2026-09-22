"""Producer for the run68-candidate-qualification.json audit and comparison fields.

  python3 verification/results/run68-candidate-audit.py pe BASE.dll CANDIDATE.dll
      import DLLs / imported functions added or removed, export names, x3m_ exports
      (parses `i686-w64-mingw32-objdump -p`).
  python3 verification/results/run68-candidate-audit.py diff OLD.json NEW.json
      leaf-by-leaf comparison of two fixture summaries (changed / added / removed paths).
"""
import json
import re
import subprocess
import sys


def pe(path):
    text = subprocess.run(['i686-w64-mingw32-objdump', '-p', path], check=True, capture_output=True, text=True).stdout
    dlls, imports, exports, dll, mode = set(), set(), [], None, None
    for line in text.splitlines():
        m = re.match(r'\s*DLL Name: (\S+)', line)
        if m:
            dll, mode = m.group(1).lower(), 'imp'
            dlls.add(dll)
            continue
        if line.startswith('[Ordinal/Name Pointer] Table'):
            mode = 'exp'
            continue
        if mode == 'imp':
            m = re.match(r'\s+[0-9a-f]{8}\s+<none>\s+[0-9a-f]{4}\s+(\S+)', line)
            if m:
                imports.add(dll + '!' + m.group(1))
            elif not line.strip():
                mode = None
        elif mode == 'exp':
            m = re.match(r'\s+\[\s*\d+\]\s+\+base\[\s*\d+\]\s+[0-9a-f]{4}\s+(\S+)', line)
            if m:
                exports.append(m.group(1))
    return dlls, imports, exports


def flat(o, p=''):
    if isinstance(o, dict):
        for k, v in o.items():
            yield from flat(v, p + '/' + str(k))
    elif isinstance(o, list):
        for i, v in enumerate(o):
            yield from flat(v, p + '[%d]' % i)
    else:
        yield p, o


def main():
    if sys.argv[1] == 'pe':
        a, b = pe(sys.argv[2]), pe(sys.argv[3])
        print(json.dumps(dict(
            imported_dlls=[len(a[0]), len(b[0])], new_dlls=sorted(b[0] - a[0]), removed_dlls=sorted(a[0] - b[0]),
            imported_functions=[len(a[1]), len(b[1])], new_functions=sorted(b[1] - a[1]), removed_functions=sorted(a[1] - b[1]),
            exports=len(b[2]), export_names=b[2], x3m_exports=[e for e in b[2] if 'x3m' in e.lower()],
            exports_equal_base=a[2] == b[2]), indent=1))
    else:
        a = dict(flat(json.load(open(sys.argv[2]))))
        b = dict(flat(json.load(open(sys.argv[3]))))
        changed = {k: [a[k], b[k]] for k in a if k in b and a[k] != b[k]}
        print(json.dumps(dict(leaves=[len(a), len(b)], changed=changed,
                              added=sorted(set(b) - set(a)), removed=sorted(set(a) - set(b))), indent=1))


if __name__ == '__main__':
    main()
