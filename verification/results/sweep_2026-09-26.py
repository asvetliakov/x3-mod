#!/usr/bin/env python3
"""Repository hygiene sweep of verification/results (2026-09-26).

Decides for every tracked file under verification/results whether it stays in
git (KEEP) or leaves it (REMOVE) and writes sweep_2026-09-26.txt beside this
script: one line per file `DECISION RULE SIZE PATH`, then a summary per
top-level entry and per rule.  It never deletes anything; `--git-rm` prints
the removal list (one path per line, whole directories collapsed) for
`git rm -q -r --pathspec-from-file=-`.

Rules (priority order):
  n  never remove: the protected directories and run89/90/91 records
  b  bottle-X3/**, *-candidate-qualification.json, *-candidate-install.json
  a  referenced from a tracked file outside verification/results (path, glob,
     template, basename, or directory reference)
  d  as a, where the referencing file is a feature ledger docs/verification/*.md
     A pattern reference (glob, `<case>` placeholder, %s/{} template) keeps only
     non-raw matches: it names a file family or a writer's output, not evidence.
  c  producing script (*.py, *.sh) or a JSON/TXT summary under 200 KB in a
     kept directory (the results root counts as kept)
  e  raw output: .log/.out/.bin/.dds, .png over 200 KB, JSON/TXT over 200 KB
  f  runNNN-* per-flight directory of a completed run, unreferenced
  g  dropped-feature directory, unreferenced
  -  none of the above: kept by default
Run from the repository root:  python3 verification/results/sweep_2026-09-26.py
The committed .txt was written at 55ddd90b, before the removal; a rerun on a
later tree lists only the survivors.  `--link-check [REF]` lists the literal
verification/results paths named in tracked markdown that are missing now but
were tracked at REF (default HEAD); exit 1 when any is newly missing.
"""
import bisect
import collections
import fnmatch
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
RES = 'verification/results/'
OUT = os.path.join(ROOT, RES, 'sweep_2026-09-26.txt')
LIMIT = 200_000
SELF = {RES + 'sweep_2026-09-26.py', RES + 'sweep_2026-09-26.txt'}
PROTECTED_DIRS = {'bottle-X3', 'launcher-defaults', 'logging-tiers', 'config-file',
                  'shadow-single-map-removal', 'fog-family-data', 'field-of-view'}
PROTECTED_FILE = re.compile(r'^run(89|90|91)-candidate-')
CANDIDATE_RECORD = re.compile(r'-candidate-(qualification|install)\.json$')
RAW_EXT = {'.log', '.out', '.bin', '.dds'}
# Dropped features (rule g); matched against the top-level entry name.
DROPPED = re.compile(r'(effects-stage|effects-modern|chase-view-docking|chase-restore|'
                     r'fov-inverse|inverse-map|(^|-)ssr(-|$)|screen-space-refl|'
                     r'shadow-single|single-shadow|replay-admission|lod-scale)')
# Reference sources are every tracked text file outside verification/results
# except ignore/attribute rules, which are not references.
NOT_SOURCES = {'.gitignore', '.gitattributes'}
TOKEN = re.compile(r'[A-Za-z0-9_.\-/*%<>~]*(?:\{[^{}\s]{0,80}\}[A-Za-z0-9_.\-/*%<>~]*)+'
                   r'|[A-Za-z0-9_.\-/*%<>~]+')
QUOTES = '`\'"'


def git_files():
    out = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True, check=True).stdout
    return [p for p in out.decode().split('\0') if p]


def read_text(path):
    full = os.path.join(ROOT, path)
    try:
        if os.path.getsize(full) > 8_000_000:
            return None
        data = open(full, 'rb').read()
    except OSError:
        return None
    if b'\0' in data[:8192]:
        return None
    return data.decode('utf-8', 'replace')


def expand_braces(s):
    m = re.search(r'\{([^{}]*,[^{}]*)\}', s)
    if not m:
        return [s]
    out = []
    for alt in m.group(1).split(','):
        out.extend(expand_braces(s[:m.start()] + alt + s[m.end():]))
    return out


def to_glob(s):
    s = re.sub(r'\{[^{}]*\}|<[^<>]*>|%[0-9.]*[sdx]', '*', s)
    return re.sub(r'\*+', '*', s)


def specific(pattern):
    """A glob counts as a reference only when its last component is anchored."""
    last = pattern.rstrip('/').split('/')[-1]
    stem = re.sub(r'\.[A-Za-z0-9]+$', '', last)
    literal = stem.replace('*', '')
    if len(literal) < 6:
        return False
    runs = [r for r in stem.split('*')]
    # One wildcard run at most, or a literal head of at least four characters.
    return last.count('*') <= 1 or len(runs[0]) >= 4


def completed_runs():
    """Launch labels and capture numbers of Completed/Accepted table rows."""
    labels, captures = set(), set()
    for doc in ('docs/verification/user-runs.md', 'docs/archive/user-runs-completed.md'):
        text = read_text(doc) or ''
        for line in text.splitlines():
            cells = [c.strip() for c in line.split('|')]
            if len(cells) < 6 or not re.match(r'^\d', cells[1]):
                continue
            if not re.match(r'(Completed|Accepted)', cells[-2]):
                continue
            lab = cells[1].replace(' ', '').lower()
            num = re.match(r'\d+', lab).group(0)
            for letter in re.findall(r'[a-z]', lab) or ['']:
                labels.add(num + letter)
            labels.add(num)
            for m in re.finditer(r'\bruns? ?(\d+)(?:[-–/](\d+))?', line, re.I):
                a = int(m.group(1)); b = int(m.group(2) or a)
                if b >= a and b - a < 20:
                    captures.update(range(a, b + 1))
    return labels, captures


def run_dir_completed(name, labels, captures):
    m = re.match(r'^run(\d+)(?:-(\d+))?(?:-|$)', name)
    if not m:
        return None
    lm = re.search(r'-run(\d+)([a-z])(?:-|$)', name)
    if lm and (lm.group(1) + lm.group(2)) in labels:
        return True
    a = int(m.group(1)); b = int(m.group(2) or a)
    if any(n in captures for n in range(a, b + 1)):
        return True
    return str(a) in labels and a < 100


def main():
    tracked = git_files()
    results = [p for p in tracked if p.startswith(RES) and p not in SELF]
    rel = {p: p[len(RES):] for p in results}
    size = {p: os.path.getsize(os.path.join(ROOT, p)) for p in results}
    top = {p: (rel[p].split('/')[0] if '/' in rel[p] else '<root>') for p in results}
    dirs = sorted({t for t in top.values() if t != '<root>'})
    by_rel = {rel[p]: p for p in results}
    by_base = collections.defaultdict(list)
    for p in results:
        by_base[os.path.basename(p)].append(p)

    sources = [p for p in tracked if not p.startswith(RES) and p not in NOT_SOURCES]
    file_refs = collections.defaultdict(set)   # results path -> referencing sources
    dir_refs = collections.defaultdict(set)    # top-level dir -> referencing sources
    patterns = collections.defaultdict(set)    # glob -> sources
    dirset = set(dirs)
    under = collections.defaultdict(list)       # rel path or directory prefix -> files
    for p in results:
        comps = rel[p].split('/')
        for i in range(1, len(comps) + 1):
            under['/'.join(comps[:i])].append(p)
    for src in sources:
        text = read_text(src)
        if not text:
            continue
        for m in TOKEN.finditer(text):
            tok = m.group(0).rstrip('.,:;)')
            before = text[m.start() - 1] if m.start() else ''
            after = text[m.end()] if m.end() < len(text) else ''
            quoted = before in QUOTES and after in QUOTES
            if len(tok) < 5 or len(tok) > 300:
                continue
            cands = expand_braces(tok)[:64] if '{' in tok and ',' in tok else [tok]
            for cand in cands:
                cand = cand.strip(',')
                if '/results/' in '/' + cand or cand.startswith('results/'):
                    tail = cand.split('results/', 1)[1] if cand.startswith(('verification/results/', 'results/')) \
                        else cand.split('/results/', 1)[1]
                else:
                    tail = cand
                tail = tail.strip('/')
                if not tail:
                    continue
                last = tail.split('/')[-1]
                if any(c in tail for c in '*{}%<>') and not any(c in last for c in '*{}%<>') \
                        and '.' in last:
                    for p in by_base.get(last, ()):     # e.g. glob('**/gz-buffer-summary.json')
                        file_refs[p].add(src)
                    continue
                if any(c in tail for c in '*{}%<>'):
                    in_results = tail != cand
                    template = not src.endswith('.md') and any(c in tail for c in '{%') and \
                        re.search(r'\.(json|txt|log|py|png|csv|md)$', tail)
                    g = to_glob(tail).replace('[', '?').replace(']', '?')
                    if (in_results or template) and specific(g):
                        patterns[g].add(src)
                    continue
                if tail in by_rel:
                    file_refs[by_rel[tail]].add(src)
                parts = tail.split('/')
                if parts[0] in dirset and (len(parts) > 1 or quoted or tail != cand
                                            or tok.endswith('/')):
                    if len(parts) == 1 or (len(parts) == 2 and parts[1] == ''):
                        dir_refs[parts[0]].add(src)
                    else:
                        sub = '/'.join(parts)
                        hit = under.get(sub, ())
                        for p in hit:
                            file_refs[p].add(src)
                        if not hit and parts[0] in dirset:
                            dir_refs[parts[0]].add(src)   # names a missing file: keep dir
                for p in by_base.get(parts[-1], ()):
                    if '/' not in rel[p] or ('/' in tail and rel[p].endswith('/' + tail)):
                        file_refs[p].add(src)
                for i, comp in enumerate(parts[:-1]):
                    if comp in dirset and i == 0 and tail == cand:
                        dir_refs[comp].add(src)
    glob_hits = {}
    glob_refs = collections.defaultdict(set)   # results path -> sources naming it only by a pattern
    for g, srcs in patterns.items():
        hit = [p for p in results if fnmatch.fnmatchcase(rel[p], g)
               or ('/' not in g and fnmatch.fnmatchcase(os.path.basename(p), g))]
        glob_hits[g] = hit
        for p in hit:
            glob_refs[p].update(srcs)

    def ledger(srcs):
        return any(s.startswith('docs/verification/') and s.endswith('.md')
                   and s not in ('docs/verification/user-runs.md',) for s in srcs)

    def via(srcs):
        led = sorted(s for s in srcs if ledger([s]))
        return 'via=' + (led or sorted(srcs))[0] + ('+%d' % (len(srcs) - 1) if len(srcs) > 1 else '')

    def is_raw(p):
        ext = os.path.splitext(p)[1].lower()
        return ext in RAW_EXT or (ext == '.png' and size[p] > LIMIT) or \
            (ext in ('.json', '.txt') and size[p] >= LIMIT)

    labels, captures = completed_runs()
    dir_has_file_ref = collections.Counter(top[p] for p in file_refs)
    dir_fate = {}   # top-level dir -> None (kept) or (rule, reason)
    for d in dirs:
        if d in PROTECTED_DIRS or d in dir_refs or dir_has_file_ref[d]:
            dir_fate[d] = None
        elif run_dir_completed(d, labels, captures):
            dir_fate[d] = 'f'
        elif DROPPED.search(d):
            dir_fate[d] = 'g'
        else:
            dir_fate[d] = None

    rows = []
    for p in sorted(results):
        r, t, b, ext = rel[p], top[p], os.path.basename(p), os.path.splitext(p)[1].lower()
        if t in PROTECTED_DIRS or PROTECTED_FILE.match(r):
            dec = ('KEEP', 'b' if t == 'bottle-X3' else 'n')
        elif CANDIDATE_RECORD.search(b):
            dec = ('KEEP', 'b')
        elif t in dir_refs:
            dec = ('KEEP', 'd' if ledger(dir_refs[t]) else 'a', via(dir_refs[t]))
        elif p in file_refs:
            dec = ('KEEP', 'd' if ledger(file_refs[p]) else 'a', via(file_refs[p]))
        elif p in glob_refs and not is_raw(p):
            dec = ('KEEP', 'd' if ledger(glob_refs[p]) else 'a', via(glob_refs[p]) + ' (pattern)')
        elif dir_fate.get(t):
            dec = ('REMOVE', dir_fate[t])
        elif ext in ('.py', '.sh'):
            dec = ('KEEP', 'c')
        elif ext in ('.json', '.txt') and size[p] < LIMIT:
            dec = ('KEEP', 'c')
        elif is_raw(p):
            dec = ('REMOVE', 'e', via(glob_refs[p]) + ' (pattern only)' if p in glob_refs else '')
        else:
            dec = ('KEEP', '-')
        rows.append((dec[0], dec[1], size[p], p, dec[2] if len(dec) > 2 else ''))

    per_top = collections.defaultdict(lambda: [0, 0, 0, 0])
    per_rule = collections.defaultdict(lambda: [0, 0])
    for dec, rule, sz, p, _ in rows:
        k = per_top[top[p]]
        if dec == 'KEEP':
            k[0] += 1; k[1] += sz
        else:
            k[2] += 1; k[3] += sz
        per_rule[(dec, rule)][0] += 1; per_rule[(dec, rule)][1] += sz
    with open(OUT, 'w') as fh:
        fh.write('# verification/results sweep 2026-09-26: DECISION RULE BYTES PATH [via=FIRST_REFERENCE+N] '
                 '(rules in sweep_2026-09-26.py)\n')
        for row in rows:
            fh.write(('%s %s %d %s %s' % row).rstrip() + '\n')
        fh.write('\n# summary per rule: DECISION RULE FILES BYTES\n')
        for (dec, rule), (n, sz) in sorted(per_rule.items()):
            fh.write('# %s %s %d %d\n' % (dec, rule, n, sz))
        fh.write('\n# summary per top-level entry: NAME KEPT_FILES KEPT_BYTES REMOVED_FILES REMOVED_BYTES\n')
        for t, (kn, kb, rn, rb) in sorted(per_top.items()):
            fh.write('# %s %d %d %d %d\n' % (t, kn, kb, rn, rb))
        fh.write('\n# glob and template references: PATTERN MATCHED_FILES\n')
        for g, hit in sorted(glob_hits.items()):
            if hit:
                fh.write('# %s %d\n' % (g, len(hit)))

    if '--git-rm' in sys.argv:
        removed = {p for dec, _, _, p, _ in rows if dec == 'REMOVE'}
        whole = {t for t in dirs if all(q in removed for q in results if top[q] == t)}
        for t in sorted(whole):
            print(RES + t)
        for p in sorted(removed):
            if top[p] not in whole:
                print(p)
    else:
        for (dec, rule), (n, sz) in sorted(per_rule.items()):
            print('%-6s %s %5d files %12d bytes' % (dec, rule, n, sz))
        print('wrote', os.path.relpath(OUT, ROOT))


def dir_exists(path, dirs):
    return path in dirs or any(d.startswith(path + '/') for d in dirs)


def link_check(ref):
    """Literal verification/results paths named in tracked markdown: missing now vs at ref."""
    now = set(git_files())
    old = set(subprocess.run(['git', 'ls-tree', '-r', '--name-only', ref], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout.split('\n'))
    now_dirs = {os.path.dirname(t) for t in now}
    old_dirs = {os.path.dirname(t) for t in old}
    names = set()
    for md in sorted(p for p in now if p.endswith('.md')):
        text = read_text(md) or ''
        for m in re.finditer(r'verification/results/[A-Za-z0-9_.\-/]+', text):
            if text[m.end():m.end() + 1] in '*{}<>%':   # a pattern, not a path
                if m.end() < len(text):
                    continue
            names.add(m.group(0).rstrip('.,:;/'))
    missing_now = sorted(n for n in names if n not in now and not dir_exists(n, now_dirs))
    missing_old = {n for n in names if n not in old and not dir_exists(n, old_dirs)}
    new = [n for n in missing_now if n not in missing_old]
    print('literal paths %d, missing now %d, missing at %s %d, newly missing %d'
          % (len(names), len(missing_now), ref, len(missing_old), len(new)))
    for n in new:
        print('NEWLY MISSING', n)
    return 1 if new else 0


if __name__ == '__main__':
    if '--link-check' in sys.argv:
        i = sys.argv.index('--link-check')
        sys.exit(link_check(sys.argv[i + 1] if len(sys.argv) > i + 1 else 'HEAD'))
    main()
