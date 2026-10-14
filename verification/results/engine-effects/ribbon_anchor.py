"""The ribbon anchored inside the plume (2026-10-14): the host harness's ANCHOR rows and its cost against a base ref.

Compiles the HARNESS of verification/analysis/test_engine_ribbons.py twice with the host compiler: from the working
tree, and from `git show <base>` of the test and src/proxy/engine_ribbons_core.h (the rest of src/ from the working
tree). Prints the ANCHOR rows and the median of 5 runs of each COST row. Usage: ribbon_anchor.py [base, default HEAD].
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TEST = 'verification/analysis/test_engine_ribbons.py'
CORE = 'src/proxy/engine_ribbons_core.h'


def harness(text):
    return re.search(r"HARNESS = r'''(.*?)'''", text, re.S).group(1)


def build_and_run(directory, name, test_text, core_text):
    include = []
    if core_text is not None:
        proxy = directory / name / 'proxy'
        proxy.mkdir(parents=True)
        (proxy / 'engine_ribbons_core.h').write_text(core_text)
        (directory / name / 'renderer').symlink_to(ROOT / 'src/renderer')
        include = ['-I', str(proxy)]
    source, exe = directory / f'{name}.cpp', directory / name / 'harness'
    exe.parent.mkdir(parents=True, exist_ok=True)
    source.write_text(harness(test_text))
    compiler = shutil.which('clang++') or shutil.which('c++')
    subprocess.run([compiler, '-std=c++17', '-O2', *include, '-I', str(ROOT / 'src/proxy'), str(source), '-o', str(exe)], check=True)
    cost, rows, last = {}, [], ''
    for i in range(5):
        out = subprocess.run([str(exe)], capture_output=True, text=True).stdout
        for m in re.finditer(r'COST ribbons=(\d+) median_us=([\d.]+)', out):
            cost.setdefault(int(m.group(1)), []).append(float(m.group(2)))
        if i == 0:
            rows = [line for line in out.splitlines() if line.startswith('ANCHOR')]
            last = out.strip().splitlines()[-1]
    return rows, {k: sorted(v)[len(v) // 2] for k, v in sorted(cost.items())}, last


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
    show = lambda path: subprocess.run(['git', '-C', str(ROOT), 'show', f'{base}:{path}'], capture_output=True, text=True, check=True).stdout
    with tempfile.TemporaryDirectory(prefix='x3-ribbon-anchor-') as d:
        d = Path(d)
        rows, cost, last = build_and_run(d, 'tree', (ROOT / TEST).read_text(), None)
        _, base_cost, base_last = build_and_run(d, 'base', show(TEST), show(CORE))
    for row in rows:
        print(row)
    print(f'tree {last}; COST median of 5 (us) {cost}')
    print(f'base {base} {base_last}; COST median of 5 (us) {base_cost}')


if __name__ == '__main__':
    main()
