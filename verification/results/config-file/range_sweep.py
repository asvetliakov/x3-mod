#!/usr/bin/env python3
"""Settings file review (2026-09-26): every numeric schema entry's range against the bounds its read site accepts.

For each int/float entry of tools/config/schema.py the read site (the line naming the variable plus the next three) is
searched for a literal or named bound: `v>=LO&&v<=HI` / `v>LO&&v<=HI` (and n/value/x), `meter_parameter(L"NAME", LO, HI,`,
`env_number(L"NAME", DEFAULT, LO, HI)`, `material_gain(L"NAME", out[, MAX])` (0..MAX, default 16); a named bound
(`renderer::fog_strength_max`) is resolved from its `name = value` definition under src/. The site's interval is compared
with the schema interval that shares its upper bound. Lists and sites with their own parsers are listed as `manual` and
were compared by hand (docs/architecture/config-file.md, "Review fixes"). Writes range_sweep.txt beside this script; exit 1
on a mismatch.

    python3 verification/results/config-file/range_sweep.py
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402

SOURCES = {p: p.read_text(errors='replace') for p in (ROOT / 'src').rglob('*') if p.suffix in ('.cpp', '.h')}
NUMBER = r'-?(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:e-?[0-9]+)?f?'
TOKEN = r'[-\w.:]+'


def constant(token, depth=0):
    token = token.strip()
    if re.fullmatch(NUMBER, token):
        return float(token.rstrip('f'))
    name = token.split('::')[-1]
    for text in SOURCES.values():
        match = re.search(rf'\b{re.escape(name)}\s*=\s*({NUMBER}|\w+)\s*[,;]', text)
        if match:
            return constant(match.group(1), depth + 1) if depth < 2 else None
    return None


def site_text(env):
    blocks = []
    for text in SOURCES.values():
        lines = text.split('\n')
        for i, line in enumerate(lines):
            if f'L"{env}"' in line:
                blocks.append(' '.join(lines[i:i + 4]))
    return blocks


def site_bounds(env, blocks):
    for block in blocks:
        for pattern, kind in ((rf'meter_parameter\(L"{env}",\s*({TOKEN}),\s*({TOKEN}),', 'closed'),
                              (rf'env_number\(L"{env}",\s*{TOKEN},\s*({TOKEN}),\s*({TOKEN})\)', 'closed')):
            m = re.search(pattern, block)
            if m:
                return constant(m.group(1)), constant(m.group(2)), False
        m = re.search(rf'material_gain\(L"{env}",[^,)]+(?:,\s*({TOKEN}))?\)', block)
        if m:
            return 0.0, constant(m.group(1)) if m.group(1) else 16.0, False
        start = block.index(f'L"{env}"')
        m = re.search(rf'\b(?:v|n|value|x|alpha_value)\s*(>=|>)\s*(?:double\()?({TOKEN}?)\)?\s*&&\s*(?:v|n|value|x|alpha_value)\s*<=\s*'
                      rf'(?:double\()?({TOKEN}?)(?:\))?(?=[);&])', block[start:])
        if m:
            return constant(m.group(2)), constant(m.group(3)), m.group(1) == '>'
    return None


def main():
    lines, mismatches = [], []
    for e in schema.SETTINGS:
        if e['type'] not in ('int', 'float', 'int_list', 'float_list') or e['env_only']:
            continue
        rng = e['range'] or ()
        if e['type'].endswith('_list') or e['elements']:
            lines.append(f"manual   {e['key']}: list, schema range={rng} elements={e['elements']} counts={e['counts']}")
            continue
        bounds = site_bounds(e['env'], site_text(e['env']))
        if bounds is None or None in bounds[:2]:
            lines.append(f"manual   {e['key']}: no literal bound at the site; schema {rng or 'unchecked'}")
            continue
        lo, hi, open_min = bounds
        match = [r for r in rng if abs(r[1] - hi) <= 1e-9 * max(1.0, abs(hi))]
        ok = bool(match) and abs(match[0][0] - lo) <= 1e-9 * max(1.0, abs(lo)) and match[0][2] == open_min
        lines.append(f"{'agree   ' if ok else 'MISMATCH'} {e['key']}: site {'(' if open_min else '['}{lo:g}, {hi:g}] schema {rng}")
        if not ok:
            mismatches.append(e['key'])
    text = '\n'.join(lines) + f"\n{sum(l.startswith('agree') for l in lines)} agree, {len(mismatches)} mismatch, " \
                              f"{sum(l.startswith('manual') for l in lines)} manual\n"
    (HERE / 'range_sweep.txt').write_text(text)
    print(text, end='')
    return 1 if mismatches else 0


if __name__ == '__main__':
    raise SystemExit(main())
