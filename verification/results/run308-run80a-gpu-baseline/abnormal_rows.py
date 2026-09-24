"""Per session: row types whose name says refused/error/mismatch/fail/disagreement/fallback, and rows of any type carrying a
nonzero failure-like field (failures|failure|errors|mismatch\w*|clock_errors|read_failure|overflow|refused\w*=), counted
by (row type, field). usage: abnormal_rows.py RUN..."""
import glob, re, sys, collections
NAME = re.compile(r'refus|error|mismatch|fail|disagree|fallback|fault')
FIELD = re.compile(r' ((?:\w*_)?(?:failures?|errors|mismatch\w*|mismatched|overflow|refused\w*|rejected))=([1-9]\d*)')
for run in sys.argv[1:]:
    log = sorted(glob.glob(f'/tmp/x3-bottleX3-run{run}/session-*.log'))[0]
    names = collections.Counter(); fields = collections.Counter()
    for l in open(log, errors='replace'):
        t = l.split(' ', 1)[0]
        if NAME.search(t): names[t] += 1
        for k, v in FIELD.findall(l): fields[(t, k)] += 1
    print(f'run{run} row types: {dict(sorted(names.items()))}')
    print(f'run{run} rows with nonzero field: {dict(sorted((f"{a}.{b}", c) for (a, b), c in fields.items()))}')
