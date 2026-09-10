#!/usr/bin/env python3
"""Archive-wide shader inspection, keeping copyrighted raw assets local only.

Extraction consumes the separate indexer's bounded stream findings, validates
every effect/stream digest against the archive, and deduplicates full programs.
Run the existing standalone D3DX disassembler on that local output, then use
inventory to publish derived metadata and every archive alias, never raw code.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct

from index_shaders import fnv1a64, shader_end, SM1_ARITY
from inspect_x3 import read_catalogue
from shader_constants import parse_ctab


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    # Large complete inventories are deliberately compact, stable JSON.
    path.write_text(json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n')


def local_directory(path, game=None):
    resolved = path.resolve()
    repository = Path(__file__).resolve().parents[2]
    for forbidden in (repository, game.resolve() if game else None):
        if forbidden and (resolved == forbidden or forbidden in resolved.parents):
            raise ValueError('Raw asset directory must be outside repository and game tree')
    resolved.mkdir(parents=True, exist_ok=True)
    return resolved


def gpu_words(code):
    """Remove comments for code deduplication, NOT effect semantic equivalence.

    Comment payloads can include preshaders/CTAB, so full-program hashes remain
    authoritative for runtime profiles. This additional digest groups identical
    GPU instructions only, including declarations and literal definitions.
    """
    words = struct.unpack(f'<{len(code)//4}I', code)
    if shader_end(words, 0) != len(words):
        raise ValueError('Incomplete token stream')
    major, minor = (words[0] >> 8) & 255, words[0] & 255
    result = [words[0]]
    pos = 1
    while pos < len(words):
        token = words[pos]
        if token == 0xffff:
            result.append(token)
            break
        opcode = token & 65535
        if opcode == 0xfffe:
            count = (token >> 16) & 32767
        elif major == 1:
            count = 2 if minor == 4 and opcode in (64, 66) else SM1_ARITY[opcode]
        else:
            count = (token >> 24) & 15
        if opcode != 0xfffe:
            result.extend(words[pos:pos + count + 1])
        pos += count + 1
    return struct.pack(f'<{len(result)}I', *result)


def extract(game, index_path, raw_directory, manifest_path):
    directory = local_directory(raw_directory, game)
    indexed_bytes = index_path.read_bytes()
    indexed = json.loads(indexed_bytes)
    grouped = defaultdict(list)
    for effect in indexed['effects']:
        grouped[effect['catalogue']].append(effect)
    programs, aliases, filename_digests, catalogues = {}, [], {}, []
    catalogue_entries = {}
    expected_effects = []
    for cat in sorted([*game.glob('[0-9][0-9].cat'), *game.glob('addon/[0-9][0-9].cat')]):
        relative = str(cat.relative_to(game))
        entries = read_catalogue(cat)
        catalogue_entries[relative] = {entry['path']: entry for entry in entries}
        if len(catalogue_entries[relative]) != len(entries):
            raise ValueError('Duplicate virtual paths within a catalogue need explicit handling')
        expected_effects.extend((relative, e['path']) for e in entries
                               if e['path'].startswith('shader/') and e['path'].endswith('.fb'))
        catalogues.append({'path': relative, 'cat_sha256': sha(cat.read_bytes()),
                           'dat_bytes': cat.with_suffix('.dat').stat().st_size})
    actual_effects = [(e['catalogue'], e['path']) for e in indexed['effects']]
    if sorted(expected_effects) != sorted(actual_effects):
        raise ValueError('Index does not cover every installed numbered-archive effect')
    for catalogue in sorted(grouped):
        cat = game / catalogue
        entries = catalogue_entries[catalogue]
        with cat.with_suffix('.dat').open('rb') as stream:
            for effect in grouped[catalogue]:
                entry = entries[effect['path']]
                stream.seek(entry['offset'])
                data = bytes(value ^ 0x33 for value in stream.read(entry['size']))
                if sha(data) != effect['effect_sha256']:
                    raise ValueError(f"Effect index stale: {catalogue}:{effect['path']}")
                counts = Counter()
                for item in effect['shaders']:
                    code = data[item['offset']:item['offset'] + item['bytes']]
                    if sha(code) != item['sha256'] or fnv1a64(code) != item['fnv1a64']:
                        raise ValueError('Stream index digest mismatch')
                    name = f"{item['stage']}_{item['fnv1a64']}"
                    if name in filename_digests and filename_digests[name] != item['sha256']:
                        raise ValueError('FNV filename collision')
                    filename_digests[name] = item['sha256']
                    counts[name] += 1
                    if name not in programs:
                        gpu = gpu_words(code)
                        (directory / f'{name}.bin').write_bytes(code)
                        programs[name] = {'id': name, 'stage': item['stage'], 'model': item['model'],
                            'bytes': len(code), 'fnv1a64': item['fnv1a64'], 'sha256': item['sha256'],
                            'gpu_tokens_sha256': sha(gpu), 'gpu_token_bytes': len(gpu)}
                aliases.append({'catalogue': catalogue, 'path': effect['path'],
                                'effect_sha256': effect['effect_sha256'], 'program_occurrences': dict(sorted(counts.items()))})
    result = {'schema': 1, 'index_sha256': sha(indexed_bytes), 'catalogues': catalogues,
              'programs': [programs[k] for k in sorted(programs)], 'effects': aliases}
    write_json(manifest_path, result)
    print(json.dumps({'effects': len(aliases), 'programs': len(programs),
                      'occurrences': sum(sum(e['program_occurrences'].values()) for e in aliases),
                      'effects_without_streams': sum(not e['program_occurrences'] for e in aliases),
                      'gpu_token_variants': len({p['gpu_tokens_sha256'] for p in programs.values()})}))


def compact_features(features):
    """Avoid repeated lane/CTAB descriptions; retain whole output and RGB/alpha dependencies."""
    result = {k: v for k, v in features.items() if k not in ('outputs', 'position_outputs', 'limitations')}
    result['outputs'] = []
    for output in features.get('outputs', []):
        item = {k: v for k, v in output.items() if k != 'lanes'}
        for key in ('all', 'rgb', 'alpha'):
            if key in item:
                item[key] = {**item[key], 'parameters': sorted({p['name'] for p in item[key]['parameters']})}
        result['outputs'].append(item)
    result['position_output_registers'] = [o['register'] for o in features.get('position_outputs', [])]
    return result


def family_inventory(effects, programs):
    """Group only observed filename suffix conventions, never inferred pass equivalence."""
    by_id = {p['id']: p for p in programs}
    grouped = defaultdict(list)
    for effect in effects:
        stem = Path(effect['path']).stem
        grouped[re.sub(r'(?:2s|_000[01])$', '', stem)].append(effect)
    result = []
    for name, aliases in sorted(grouped.items()):
        keys = sorted({key for e in aliases for key in e['program_occurrences']})
        candidates = [by_id[k] for k in keys]
        result.append({'family': name, 'basenames': sorted({Path(e['path']).stem for e in aliases}),
            'profile_directories': sorted({e['path'].split('/')[1] for e in aliases}),
            'toggle_directories': sorted({'/'.join(e['path'].split('/')[2:-1]) or '(base)' for e in aliases}),
            'effect_entries': len(aliases), 'distinct_effect_bytes': len({e['effect_sha256'] for e in aliases}),
            'occurrences': sum(sum(e['program_occurrences'].values()) for e in aliases),
            'programs': keys, 'model_counts': dict(sorted(Counter(p['stage'] + '_' + p['model'] for p in candidates).items())),
            'stage_counts': dict(sorted(Counter(p['stage'] for p in candidates).items())),
            'runtime_captured_stage_counts': dict(sorted(Counter(p['stage'] for p in candidates if p['runtime_captured']).items()))})
    return {'schema': 1, 'normalization': 'remove terminal 2s or _0000 or _0001 only; preserve other spelling and all exact aliases',
            'family_count': len(result), 'basename_count': len({Path(e['path']).stem for e in effects}), 'families': result}


def inventory(manifest_path, raw_directory, disassembly_directory, output, aliases_output, families_output, unknowns_output, runtime_directory=None):
    from shader_semantics import analyze
    manifest = json.loads(manifest_path.read_bytes())
    programs = []
    captures = {}
    if runtime_directory:
        for path in runtime_directory.glob('*.bin'):
            if path.name.startswith(('ps_', 'vs_')):
                captures[sha(path.read_bytes())] = path.name
    for record in manifest['programs']:
        code = (raw_directory / (record['id'] + '.bin')).read_bytes()
        if sha(code) != record['sha256']:
            raise ValueError('Extracted program changed')
        disassembly_path = disassembly_directory / (record['id'] + '.bin.txt')
        text_bytes = disassembly_path.read_bytes()  # Missing disassembly fails sweep.
        ctab = parse_ctab(code)
        features = analyze(text_bytes.decode('utf-8', errors='replace'), ctab)
        expected_model = '2_x' if record['model'] == '2_1' else record['model']
        if (features.get('stage') != record['stage'] or features.get('model') != expected_model
                or features.get('shader_model_sections') != 1):
            raise ValueError('Disassembly model/stage does not match indexed shader')
        programs.append({**record, 'disassembly_sha256': sha(text_bytes), 'ctab': ctab,
                         'features': compact_features(features), 'runtime_captured': record['sha256'] in captures})
    runtime_misses = sorted(name for digest, name in captures.items()
                            if digest not in {p['sha256'] for p in programs})
    result = {'schema': 1, 'scope': 'Complete indexed archive streams; conservative static features, not runtime proof',
              'index_sha256': manifest['index_sha256'], 'extraction_manifest_sha256': sha(manifest_path.read_bytes()),
              'catalogues': manifest['catalogues'], 'effect_count': len(manifest['effects']),
              'occurrence_count': sum(sum(e['program_occurrences'].values()) for e in manifest['effects']),
              'program_count': len(programs), 'gpu_token_variants': len({p['gpu_tokens_sha256'] for p in programs}),
              'model_counts': dict(sorted(Counter(p['stage'] + '_' + p['model'] for p in programs).items())),
              'runtime_capture_count': len(captures), 'runtime_capture_unmatched': runtime_misses,
              'dependency_scope': 'Conservative static possible dependencies; output lane detail omitted for compactness; no control-flow proof or runtime equivalence',
              'tool_source_sha256': {name: sha(Path(__file__).with_name(name).read_bytes())
                                    for name in ('sweep_shaders.py', 'shader_semantics.py', 'index_shaders.py', 'shader_constants.py', 'inspect_x3.py')},
              'programs': programs}
    write_json(output, result)
    write_json(aliases_output, {'schema': 1, 'index_sha256': manifest['index_sha256'],
                               'override_precedence': 'not inferred', 'effects': manifest['effects']})
    write_json(families_output, family_inventory(manifest['effects'], programs))
    unknowns = []
    for program in programs:
        features = program['features']
        unresolved = {o['register']: o['all']['unknown_reasons'] for o in features.get('outputs', [])
                      if o['all']['status'] == 'unknown'}
        if features.get('unknown_reasons') or unresolved:
            unknowns.append({'id': program['id'], 'global_reasons': features['unknown_reasons'],
                             'unknown_outputs': unresolved})
    write_json(unknowns_output, {'schema': 1, 'scope': 'Static dependency limitations, not necessarily invalid shaders',
                                'program_count': len(unknowns), 'programs': unknowns})
    print(json.dumps({k: v for k, v in result.items() if k not in ('programs', 'catalogues')}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    ex = commands.add_parser('extract')
    ex.add_argument('game', type=Path)
    ex.add_argument('--index', type=Path, required=True)
    ex.add_argument('--raw-directory', type=Path, required=True)
    ex.add_argument('--manifest', type=Path, required=True)
    inv = commands.add_parser('inventory')
    inv.add_argument('--manifest', type=Path, required=True)
    inv.add_argument('--raw-directory', type=Path, required=True)
    inv.add_argument('--disassembly-directory', type=Path, required=True)
    inv.add_argument('--output', type=Path, required=True)
    inv.add_argument('--aliases-output', type=Path, required=True)
    inv.add_argument('--families-output', type=Path, required=True)
    inv.add_argument('--unknowns-output', type=Path, required=True)
    inv.add_argument('--runtime-directory', type=Path)
    args = parser.parse_args()
    if args.command == 'extract':
        extract(args.game, args.index, args.raw_directory, args.manifest)
    else:
        inventory(args.manifest, args.raw_directory, args.disassembly_directory,
                  args.output, args.aliases_output, args.families_output, args.unknowns_output, args.runtime_directory)


if __name__ == '__main__':
    main()
