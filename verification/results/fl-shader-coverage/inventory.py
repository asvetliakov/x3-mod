#!/usr/bin/env python3
"""Offline inventory of every shader program Farnham's Legacy can create, against stock AP.

Reads the numbered archives of both installations in the executable's mount
order (`tools/analysis/catalogue_mounts.py`: FL root + addon2, AP root +
addon), resolves overridden virtual paths to the last mounted catalogue, and
identifies every embedded program by the proxy's key: FNV-1a 64 over the
complete token stream plus the DWORD count. Writes derived facts only (ids,
lengths, models, catalogue and effect names); no shader words.

Cross-checks, all offline:
* the program dumps the FL proxy wrote in flight (`--captures`, ps_/vs_<id>.bin)
  must be inventory members with the same FNV and SHA-256;
* every `shader kind=.. id=.. bytes=..` row of the given proxy logs must be an
  inventory member with that length; ids outside the AP set are listed;
* the physical `addon/` series FL does not mount: shader paths it holds that
  `addon2/` does not would change the effective set if it were mounted.

usage: inventory.py [--fl DIR] [--ap DIR] [--captures DIR] [--log NAME=PATH]...
                    [--output inventory.json] [--summary summary.json]
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from catalogue_mounts import edition, mounted_catalogues, numbered_run  # noqa: E402
from effect_passes import _archive_effects  # noqa: E402
from index_shaders import embedded_shaders, fnv1a64  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402

SHADER_ROW = re.compile(r'^shader kind=(vs|ps) id=([0-9a-f]{16}) bytes=(\d+)', re.M)


def effective_programs(game):
    """id -> program record over the resolved effect set of one installation."""
    programs = {}
    effects = 0
    for catalogue, path, data in _archive_effects(game):
        effects += 1
        parts = path.split('/')
        for item in embedded_shaders(data):
            identifier = '%s_%s' % (item['stage'], item['fnv1a64'])
            record = programs.setdefault(identifier, {
                'id': identifier, 'kind': item['stage'], 'model': item['model'],
                'fnv1a64': item['fnv1a64'], 'dwords': item['bytes'] // 4, 'sha256': item['sha256'],
                'catalogues': set(), 'effect_paths': set(), 'basenames': set(),
                'profile_directories': set()})
            if record['sha256'] != item['sha256'] or record['dwords'] != item['bytes'] // 4:
                raise ValueError('FNV collision within one installation: ' + identifier)
            record['catalogues'].add(catalogue)
            record['effect_paths'].add(path)
            record['basenames'].add(Path(path).stem)
            record['profile_directories'].add(parts[1] if len(parts) > 2 else '(none)')
    return programs, effects


def shader_paths(catalogues):
    return {entry['path'] for cat in catalogues for entry in read_catalogue(cat)
            if entry['path'].startswith('shader/') and entry['path'].endswith('.fb')}


def family(record):
    """'xt' when every effect using the program is an XT material, else 'conventional'."""
    return 'xt' if all(name.startswith('xt_') for name in record['basenames']) else 'conventional'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--fl', type=Path, default=Path('/mnt/c/X3FL'))
    parser.add_argument('--ap', type=Path, default=Path('/mnt/c/X3AP-orig'))
    parser.add_argument('--captures', type=Path, default=Path('/mnt/c/X3FL/x3-modern-captures'))
    parser.add_argument('--log', action='append', default=[], metavar='NAME=PATH',
                        help='proxy log whose `shader` rows are cross-checked (repeatable)')
    parser.add_argument('--output', type=Path, default=HERE / 'inventory.json')
    parser.add_argument('--summary', type=Path, default=HERE / 'summary.json')
    arguments = parser.parse_args()
    if edition(arguments.fl) != 'fl' or edition(arguments.ap) != 'ap':
        raise SystemExit('--fl must be an FL installation and --ap a stock AP one')
    fl, fl_effects = effective_programs(arguments.fl)
    ap, ap_effects = effective_programs(arguments.ap)
    for identifier in set(fl) & set(ap):
        if fl[identifier]['sha256'] != ap[identifier]['sha256']:
            raise ValueError('same FNV, different bytes across editions: ' + identifier)
    shared = sorted(set(fl) & set(ap))
    added = sorted(set(fl) - set(ap))
    dropped = sorted(set(ap) - set(fl))

    def tally(ids, programs):
        return dict(sorted(Counter('%s_%s' % (programs[i]['kind'], programs[i]['model']) for i in ids).items()))

    # Captured program dumps and logged creations.
    captures = {'directory': str(arguments.captures), 'files': 0, 'in_inventory': 0, 'name_mismatch': [],
                'not_in_inventory': [], 'not_in_ap': []}
    for path in sorted(arguments.captures.glob('[pv]s_*.bin')) if arguments.captures.is_dir() else []:
        data = path.read_bytes()
        identifier = path.stem
        captures['files'] += 1
        if '%s_%s' % (identifier[:2], fnv1a64(data)) != identifier:
            captures['name_mismatch'].append(identifier)
        record = fl.get(identifier)
        if record and record['sha256'] == hashlib.sha256(data).hexdigest():
            captures['in_inventory'] += 1
        else:
            captures['not_in_inventory'].append(identifier)
        if identifier not in ap:
            captures['not_in_ap'].append(identifier)
    logs = {}
    for item in arguments.log:
        name, _, path = item.partition('=')
        rows = {('%s_%s' % (kind, ident), int(size)) for kind, ident, size in
                SHADER_ROW.findall(Path(path).read_text(errors='replace'))}
        ids = {ident for ident, _ in rows}
        logs[name] = {
            'path': path, 'distinct_programs': len(ids),
            'by_kind': dict(sorted(Counter(i[:2] for i in ids).items())),
            'not_in_fl_inventory': sorted(i for i in ids if i not in fl),
            'length_mismatch': sorted(i for i, size in rows if i in fl and fl[i]['dwords'] * 4 != size),
            'not_in_ap': sorted(i for i in ids if i not in ap),
            'not_in_ap_by_family': dict(sorted(Counter(family(fl[i]) for i in ids if i not in ap and i in fl).items())),
        }
    # The addon series FL does not mount.
    addon_paths = shader_paths(numbered_run(arguments.fl / 'addon'))
    addon2_paths = shader_paths(numbered_run(arguments.fl / 'addon2'))

    def public(record, shared_flag):
        # Archive location, compact: catalogues, profile directories, the
        # first virtual path and the path count for every program; every path
        # and basename only for the FL-only programs (the shared ones are in
        # the AP sweep's alias table).
        paths = sorted(record['effect_paths'])
        item = {'id': record['id'], 'kind': record['kind'], 'model': record['model'],
                'fnv1a64': record['fnv1a64'], 'dwords': record['dwords'],
                'shared_with_ap': shared_flag, 'catalogues': sorted(record['catalogues']),
                'profile_directories': sorted(record['profile_directories']),
                'effect_path': paths[0], 'effect_path_count': len(paths)}
        if not shared_flag:
            item.update(family=family(record), basenames=sorted(record['basenames']), effect_paths=paths)
        return item
    inventory = {
        'schema': 1,
        'scope': 'Every program of the effective FL shader archives (root + addon2, last mounted wins), '
                 'keyed by FNV-1a 64 + DWORD count; derived names and lengths only.',
        'fl_game': str(arguments.fl), 'ap_game': str(arguments.ap),
        'mount_order': {'fl': [str(c.relative_to(arguments.fl)) for c in mounted_catalogues(arguments.fl)],
                        'ap': [str(c.relative_to(arguments.ap)) for c in mounted_catalogues(arguments.ap)]},
        'programs': [public(fl[i], i in ap) for i in sorted(fl)],
        'dropped_ap_programs': [{'id': i, 'kind': ap[i]['kind'], 'model': ap[i]['model'], 'dwords': ap[i]['dwords'],
                                 'basenames': sorted(ap[i]['basenames'])} for i in dropped],
    }
    added_ps = [i for i in added if fl[i]['kind'] == 'ps']
    summary = {
        'schema': 1,
        'producer': str(Path(__file__).relative_to(ROOT)),
        'producer_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'mount_order': inventory['mount_order'],
        'effective_effects': {'fl': fl_effects, 'ap': ap_effects},
        'programs': {'fl': len(fl), 'ap': len(ap), 'shared': len(shared), 'fl_only': len(added),
                     'ap_only_dropped': len(dropped)},
        'by_model': {'fl': tally(fl, fl), 'ap': tally(ap, ap), 'shared': tally(shared, fl),
                     'fl_only': tally(added, fl), 'ap_only_dropped': tally(dropped, ap)},
        'vertex_programs_all_shared': all(i in ap for i in fl if fl[i]['kind'] == 'vs')
                                      and all(i in fl for i in ap if ap[i]['kind'] == 'vs'),
        'fl_only_ps_by_family': dict(sorted(Counter(family(fl[i]) for i in added_ps).items())),
        'fl_only_ps_dwords': {'min': min(fl[i]['dwords'] for i in added_ps),
                              'max': max(fl[i]['dwords'] for i in added_ps),
                              'over_1392': sum(fl[i]['dwords'] > 1392 for i in added_ps),
                              'over_1791': sum(fl[i]['dwords'] > 1791 for i in added_ps)},
        'fl_only': [{'id': i, 'dwords': fl[i]['dwords'], 'family': family(fl[i]),
                     'basenames': sorted(fl[i]['basenames'])} for i in added],
        'unmounted_addon': {'shader_paths': len(addon_paths),
                            'absent_from_addon2': len(addon_paths - addon2_paths),
                            'effect': 'none: every addon shader path is overridden by addon2'
                                      if not addon_paths - addon2_paths else 'would change the effective set'},
        'captures': captures,
        'logs': logs,
    }
    arguments.output.write_text(json.dumps(inventory, separators=(',', ':')) + '\n')
    arguments.summary.write_text(json.dumps(summary, indent=1) + '\n')
    print(json.dumps({k: summary[k] for k in ('programs', 'by_model', 'vertex_programs_all_shared',
                                               'fl_only_ps_by_family', 'fl_only_ps_dwords', 'unmounted_addon')},
                     indent=1))
    print(json.dumps({'captures': {k: (v if not isinstance(v, list) else len(v)) for k, v in captures.items()},
                      'logs': {n: {k: (v if not isinstance(v, list) else len(v)) for k, v in log.items()}
                               for n, log in logs.items()}}, indent=1))


if __name__ == '__main__':
    main()
