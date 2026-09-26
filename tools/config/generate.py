#!/usr/bin/env python3
"""Writes the views of tools/config/schema.py (docs/architecture/config-file.md, section 2).

    python3 tools/config/generate.py          # rewrite the outputs
    python3 tools/config/generate.py --check  # exit 1 when an output differs from what the schema generates

Outputs (committed):
- src/config/config_schema_inc.h: the sorted C++ table the DLL's resolver searches, and config_default:: constants;
- assets/x3m.ini: the player's template, every user-facing key commented out with its default and a plain description.
The host test verification/analysis/test_config_schema.py imports the schema directly and runs --check.
"""
import argparse
import re
import sys
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import schema  # noqa: E402

HEADER = ROOT / 'src/config/config_schema_inc.h'
TEMPLATE = ROOT / 'assets/x3m.ini'
TYPE_NAMES = {'bool': 'Bool', 'int': 'Int', 'float': 'Float', 'int_list': 'IntList', 'float_list': 'FloatList', 'enum': 'Enum',
              'string': 'String', 'path': 'Path'}
FILE_LIMIT, LINE_LIMIT = 65536, 512


def version():
    match = re.search(r'project\(\s*\S+\s+VERSION\s+([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())
    return match.group(1) if match else 'unknown'


def validate():
    """Structural checks of the schema; raises ValueError."""
    keys, envs = set(), set()
    for e in schema.SETTINGS:
        key = e['key']
        if not re.fullmatch(r'[a-z0-9_]+', key) or key in keys:
            raise ValueError(f'bad or duplicate key {key!r}')
        if e['env'] != 'X3M_' + key.upper() or e['env'] in envs:
            raise ValueError(f'bad env name for {key}')
        keys.add(key)
        envs.add(e['env'])
        if e['type'] not in schema.TYPES or e['section'] not in schema.SECTIONS:
            raise ValueError(f'{key}: type or section')
        if e['env_only'] and not e['developer']:
            raise ValueError(f'{key}: an environment-only key is a developer key')
        if not e['developer'] and not e['description'].strip():
            raise ValueError(f'{key}: a user-facing key needs a description')
        if e['marker_of'] is not None and e['marker_of'] not in schema.BY_KEY:
            raise ValueError(f'{key}: marker of an unknown key')
        for r in e['requires']:
            if r not in schema.BY_KEY:
                raise ValueError(f'{key}: requires unknown {r}')
        if e['type'] == 'enum' and not e['choices']:
            raise ValueError(f'{key}: enum without choices')
        for text in (e['default'], e['off']):
            if text is not None and ('"' in text or '\\' in text or '\n' in text):
                raise ValueError(f'{key}: value text must be plain')
    for e in schema.SETTINGS:
        for alias in e['aliases']:
            if alias in keys:
                raise ValueError(f'alias {alias} collides with a key')


def c_string(text):
    return 'nullptr' if text is None else '"' + text + '"'


def c_double(value):
    return repr(float(value))


def default_constant(e):
    """One config_default:: line for an entry with a default, typed where the value is a scalar."""
    name, value = e['key'], e['default']
    if e['type'] == 'bool' and value in ('0', '1'):
        return f'constexpr bool {name} = {"true" if value == "1" else "false"};'
    if e['type'] == 'int' and re.fullmatch(r'-?[0-9]+', value):
        return f'constexpr long {name} = {int(value)};'
    if e['type'] == 'float' and value and re.fullmatch(r'-?[0-9.]+(e-?[0-9]+)?', value):
        literal = value if ('.' in value or 'e' in value) else value + '.0'
        return f'constexpr float {name} = {literal}f;'
    return f'constexpr const char {name}[] = "{value}";'


def header_text():
    ordered = sorted(schema.SETTINGS, key=lambda e: e['env'])
    index = {e['key']: i for i, e in enumerate(ordered)}
    intervals, element_sets = [], []  # the flattened range intervals, and per-position (first, count) pairs of list elements

    def add_range(rng):
        first = len(intervals)
        intervals.extend(rng or ())
        return first, len(rng or ())

    rows = []
    for e in ordered:
        range_first, range_count = add_range(e['range'])
        element_first, element_count = len(element_sets), len(e['elements'] or ())
        for position in e['elements'] or ():
            element_sets.append(add_range(position))
        mask = sum(1 << n for n in (e['counts'] or ()))
        if mask >= 256:
            raise ValueError(f"{e['key']}: counts above 7")
        flags = (1 if e['developer'] else 0) | (2 if e['env_only'] else 0)
        marker = index[e['marker_of']] if e['marker_of'] else -1
        rows.append(f'    {{"{e["env"]}", "{e["key"]}", Type::{TYPE_NAMES[e["type"]]}, {c_string(e["default"])}, {c_string(e["off"])}, '
                    f'{range_first}, {range_count}, {element_first}, {element_count}, {mask}, "{"|".join(e["choices"])}", {flags}, {marker}}},')
    lines = ['// Generated by tools/config/generate.py from tools/config/schema.py: do not edit, run the generator.',
             '// The settings the proxy reads (docs/architecture/config-file.md): the resolver in src/proxy/config.cpp',
             '// binary-searches `entries` by environment name; config_default:: carries each default as a typed constant.',
             '// No Windows header: included by the resolver and by the host parser test.',
             '#pragma once', '', 'namespace x3m::config::schema {',
             'enum class Type : unsigned char { Bool, Int, Float, IntList, FloatList, Enum, String, Path };',
             'enum Flag : unsigned char { developer = 1, env_only = 2 };',
             'struct Interval { double min, max; bool min_open; }; // min..max inclusive, (min, max] when min_open',
             'struct ElementRange { unsigned short first; unsigned char count; }; // the intervals of one list position',
             'struct Entry {',
             '    const char* env;           // X3M_<KEY>, sorted',
             '    const char* key;           // the INI key',
             '    Type type;',
             "    const char* default_value; // nullptr: nothing (the read site's own behaviour)",
             '    const char* off_value;     // the X3M_CONFIG=bare base; nullptr: unset',
             '    unsigned short range_first; unsigned char range_count;     // intervals[]: a value (every list element) in one of them',
             '    unsigned short element_first; unsigned char element_count; // element_ranges[]: per list position, overrides the range',
             '    unsigned char count_mask;  // bit n: a list of n elements is accepted (0: any count)',
             '    const char* choices;       // "|"-separated words ("" none): the enum set, or words a number or list also accepts',
             '    unsigned char flags;       // Flag',
             '    short marker_of;           // index of the key a launcher default marker follows, -1',
             '};',
             'struct Alias { const char* key; unsigned short entry; };',
             f'constexpr unsigned interval_count = {len(intervals)};',
             'constexpr Interval intervals[interval_count] = {']
    lines += [f'    {{{c_double(lo)}, {c_double(hi)}, {"true" if open_min else "false"}}},' for lo, hi, open_min in intervals]
    lines += ['};', f'constexpr unsigned element_range_count = {max(len(element_sets), 1)};',
              'constexpr ElementRange element_ranges[element_range_count] = {']
    lines += [f'    {{{first}, {count}}},' for first, count in element_sets] or ['    {0, 0},']
    lines += ['};', f'constexpr unsigned entry_count = {len(ordered)};', f'constexpr const char schema_date[] = "{schema.SINCE}";',
              'constexpr Entry entries[entry_count] = {']
    lines += rows
    lines.append('};')
    aliases = sorted((alias, index[e['key']]) for e in schema.SETTINGS for alias in e['aliases'])
    lines.append(f'constexpr unsigned alias_count = {len(aliases)};')
    if aliases:
        lines.append('constexpr Alias aliases[alias_count] = {')
        lines += [f'    {{"{a}", {i}}},' for a, i in aliases]
        lines.append('};')
    else:
        lines.append('constexpr Alias aliases[1] = {{nullptr, 0}}; // none yet')
    lines += ['}', '', 'namespace x3m::config_default {']
    lines += [default_constant(e) for e in sorted(schema.SETTINGS, key=lambda e: e['key']) if e['default'] is not None and not e['env_only']]
    lines += ['}', '']
    return '\n'.join(lines)


def wrap(text):
    # ' = ' stays on one line ("1 = on" is never split): wrapped with no-break spaces, which textwrap does not split at.
    lines = textwrap.wrap(text.replace(' = ', '\u00a0=\u00a0'), width=100, break_long_words=False, break_on_hyphens=False)
    return ['; ' + line.replace('\u00a0', ' ') for line in lines]


def template_text():
    out = [f'; X3 Modern Renderer {version()}: settings file x3m.ini (schema {schema.SINCE}).',
           ';',
           '; Put this file next to d3d9.dll in the game folder (the folder with X3AP.exe). The mod works without it:',
           '; every setting below is commented out (the ";" in front) and shows the value the mod uses anyway.',
           '; To change a setting, delete the ";" at the start of its line and edit the value after "=".',
           '; To go back to the default, put the ";" back or delete the line.',
           ';',
           '; The log file x3m.log (next to d3d9.dll) lists which settings this file changed and any line it could not',
           '; use. The [sections] only group the settings; a setting works in any section.',
           '']
    for section in schema.SECTIONS:
        entries = [e for e in schema.user_facing() if e['section'] == section]
        if not entries:
            continue
        out.append(f'[{section}]')
        for e in entries:
            out += wrap(e['description'])
            if e['requires']:
                out.append('; Needs: ' + ', '.join(e['requires']) + '.')
            value = schema.shown_value(e)
            out.append(f';{e["key"]} = {value}' if value != '' else f';{e["key"]} =')
            out.append('')
    text = '\n'.join(out).rstrip('\n') + '\n'
    return text


def outputs():
    validate()
    return {HEADER: header_text(), TEMPLATE: template_text()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true', help='compare instead of writing; exit 1 on a difference')
    args = parser.parse_args(argv)
    try:
        produced = outputs()
    except ValueError as error:
        print(f'FAIL schema: {error}')
        return 1
    template = produced[TEMPLATE]
    if len(template.encode()) >= FILE_LIMIT or max(len(l.encode()) for l in template.splitlines()) >= LINE_LIMIT:
        print('FAIL template exceeds the parser bounds')
        return 1
    stale = []
    for path, text in produced.items():
        current = path.read_text() if path.is_file() else None
        if current != text:
            stale.append(path)
            if not args.check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
    for path in stale:
        print(('FAIL stale: ' if args.check else 'wrote ') + str(path.relative_to(ROOT)))
    if args.check:
        print('PASS' if not stale else 'FAIL', f'{len(schema.SETTINGS)} settings, {len(schema.user_facing())} in the template')
        return 1 if stale else 0
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
