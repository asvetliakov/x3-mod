"""Source lines of the x87 instructions inside the functions matching the filters, in one object of a scratch build.

usage: x87_lines.py BUILD_DIR OBJECT_NAME FILTER [FILTER ...]
Prints one header line (build dir, object, filters, function count) and one row per x87 instruction
(function, file:line, instruction). Audits the fog hand-over's touched functions; the DLL-wide light-hook
audit is check_no_x87.py, redone at the gate on the current base.
"""
import re, subprocess, sys
from pathlib import Path
build, name, filters = Path(sys.argv[1]), sys.argv[2], sys.argv[3:]
obj = next(build.rglob(name))
text = subprocess.run(['i686-w64-mingw32-objdump', '-dl', '--no-show-raw-insn', '-C', str(obj)], capture_output=True, text=True).stdout
X87 = re.compile(r'\t(f(?:ld|ild|st|stp|istp|isttp|add|sub|subr|mul|div|divr|xch|ucom|ucomi|ucomip|com|comp|chs|abs|ldz|ld1|sqrt)[a-z]*)\b')
current, where, rows, functions = None, None, [], set()
for line in text.splitlines():
    head = re.match(r'^[0-9a-f]+ <(.*)>:$', line)
    if head:
        current = head.group(1)
        if any(k in current for k in filters): functions.add(current)
        continue
    loc = re.match(r'^(/.*:\d+)', line)
    if loc: where = loc.group(1).split('/')[-1]; continue
    if current and any(k in current for k in filters) and X87.search(line):
        rows.append('%s %s %s' % (current[:60], where, line.strip()[:60]))
print('# build=%s object=%s filters=%s functions=%d x87=%d' % (build.name, name, ','.join(filters), len(functions), len(rows)))
for row in rows: print(row)
