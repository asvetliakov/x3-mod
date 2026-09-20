#!/usr/bin/env python3
"""Verify a retained, unmodified owned FFmpeg control provider.

This is static fixture provenance.  It does not qualify runtime behavior,
DirectShow/COM integration, the game, or native Windows execution.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess

from prepare_lav_fixture import PINNED_FILES
from prepare_lav_packet_headers import COMMIT as FFMPEG_COMMIT


MODULES = ('avutil-lav-60.dll', 'swresample-lav-6.dll',
           'avcodec-lav-62.dll', 'avformat-lav-62.dll')
EMPTY_SHA256 = hashlib.sha256(b'').hexdigest()
SYSTEM_IMPORTS = frozenset('advapi32.dll bcrypt.dll comctl32.dll crypt32.dll d3d9.dll '
                           'kernel32.dll msvcrt.dll ncrypt.dll ole32.dll oleaut32.dll '
                           'psapi.dll secur32.dll shell32.dll shlwapi.dll user32.dll '
                           'version.dll ws2_32.dll'.split())
REQUIRED_EXPORTS = frozenset('''
avformat_version avcodec_version avutil_version avformat_open_input
avformat_find_stream_info avformat_close_input av_read_frame av_seek_frame
avcodec_find_decoder avcodec_alloc_context3 avcodec_parameters_to_context
avcodec_open2 avcodec_free_context avcodec_send_packet avcodec_receive_frame
av_parser_init av_parser_parse2 av_parser_close av_new_packet av_packet_alloc
av_packet_free av_packet_unref av_frame_alloc av_frame_free av_frame_unref
av_rescale av_sha_alloc av_sha_init av_sha_update av_sha_final av_free
av_log_set_level av_lav_stream_parser_get_needed av_lav_stream_parser_init
av_lav_stream_parser_get_flags av_lav_stream_parser_update_flags
avformat_index_get_entries_count avformat_index_get_entry
'''.split())
RECORD_ROLES = ('build', 'configuration', 'toolchain', 'dependencies')
STRICT_PATCH_SHA256 = 'aaaa9132699076696ec99bec7cf3887d2b10b27b5ab141c99d48626226c239bd'
STRICT_CHANGED_FILES = ('libavformat/matroskadec_haali.c',)
STRICT_BASE_CONTROL_SHA256 = '283ed00dd2330eca3bfdfa800c68ab17b33ab8201a3781914ca978f76e15d13c'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def valid_sha(value):
    return isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value) is not None


def require(value, message):
    if not value:
        raise ValueError(message)


def _private_path(value, prefix, label):
    require(isinstance(value, str) and Path(value).is_absolute(), label+' path must be absolute')
    path = Path(value).resolve()
    require(path.is_relative_to(prefix), label+' path outside private prefix')
    return path


def _git_output(source, *args):
    try:
        return subprocess.check_output(['git', '-C', str(source), *args],
                                       stderr=subprocess.STDOUT, timeout=20)
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        raise ValueError('owned source git inspection failed') from error


def parse_objdump(output):
    imports = re.findall(r'^\s*DLL Name:\s*([^\r\n]+)', output, re.M)
    export_section = output.partition('[Ordinal/Name Pointer] Table')[2]
    exports = re.findall(
        r'^\s*\[\s*\d+\]\s+(?:\+base\[\s*\d+\]\s+[0-9a-fA-F]+\s+)?([^\s]+)\s*$',
        export_section, re.M)
    require(imports and exports, 'missing PE import/export evidence')
    return dict(machine='I386', imports=sorted(x.lower() for x in imports),
                exports=sorted(exports))


def inspect_pe(path, objdump='i686-w64-mingw32-objdump'):
    raw = path.read_bytes()
    require(len(raw) >= 0x40, 'truncated PE: '+path.name)
    offset = struct.unpack_from('<I', raw, 0x3c)[0]
    require(offset+6 <= len(raw) and raw[offset:offset+4] == b'PE\0\0' and
            struct.unpack_from('<H', raw, offset+4)[0] == 0x14c,
            'provider is not I386 PE: '+path.name)
    try:
        output = subprocess.check_output([objdump, '-p', str(path)], text=True,
                                         stderr=subprocess.STDOUT, timeout=30)
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        raise ValueError('PE inspection failed: '+path.name) from error
    try:
        return parse_objdump(output)
    except ValueError as error:
        raise ValueError(str(error)+': '+path.name) from error


def cohort_payload(data):
    return dict(source=data['source'], records=data['records'], headers=data['headers'])


def cohort_digest(data):
    encoded = json.dumps(cohort_payload(data), sort_keys=True,
                         separators=(',', ':')).encode()
    return hashlib.sha256(encoded).hexdigest()


def strict_cohort_digest(data):
    payload = cohort_payload(data)
    payload.update(base_control=data['base_control'], patch=data['patch'])
    encoded = json.dumps(payload, sort_keys=True, separators=(',', ':')).encode()
    return hashlib.sha256(encoded).hexdigest()


def verify_record(path, forbidden_roots=(), objdump='i686-w64-mingw32-objdump'):
    """Verify the record and every retained input it names; return parsed JSON."""
    path = path.resolve()
    data = json.loads(path.read_text())
    require(data.get('schema') == 1 and data.get('provider') == 'owned-ffmpeg' and
            data.get('role') == 'unmodified-control' and data.get('architecture') == 'x86',
            'unrecognized owned provider provenance')
    prefix = _private_path(data.get('private_prefix'), Path('/'), 'private prefix')
    require(prefix != Path('/') and path.is_relative_to(prefix),
            'owned provider record outside private prefix')
    forbidden = tuple(Path(x).resolve() for x in forbidden_roots)
    require(not any(prefix.is_relative_to(root) or root.is_relative_to(prefix)
                    for root in forbidden), 'private prefix overlaps forbidden root')

    source_data = data.get('source')
    require(isinstance(source_data, dict) and set(source_data) ==
            {'path', 'commit', 'head', 'status_sha256', 'diff_sha256'},
            'owned source provenance fields')
    source = _private_path(source_data['path'], prefix, 'source')
    require(source_data['commit'] == FFMPEG_COMMIT and source_data['head'] == FFMPEG_COMMIT,
            'owned source commit differs')
    require(source_data['status_sha256'] == EMPTY_SHA256 and
            source_data['diff_sha256'] == EMPTY_SHA256,
            'owned source is not recorded clean')
    head = _git_output(source, 'rev-parse', 'HEAD').decode().strip()
    status = _git_output(source, 'status', '--porcelain=v1', '--untracked-files=all')
    diff = _git_output(source, 'diff', '--binary', 'HEAD', '--')
    require(head == FFMPEG_COMMIT and hashlib.sha256(status).hexdigest() == EMPTY_SHA256 and
            hashlib.sha256(diff).hexdigest() == EMPTY_SHA256,
            'owned source changed or wrong HEAD')

    records = data.get('records')
    require(isinstance(records, dict) and set(records) == set(RECORD_ROLES),
            'owned build record set differs')
    for role in RECORD_ROLES:
        item = records[role]
        require(isinstance(item, dict) and set(item) == {'path', 'sha256'} and
                valid_sha(item['sha256']), role+' record fields')
        record_path = _private_path(item['path'], prefix, role+' record')
        require(digest(record_path) == item['sha256'], role+' record changed')

    headers = data.get('headers')
    require(isinstance(headers, dict) and set(headers) == {'commit', 'record_sha256'} and
            headers['commit'] == FFMPEG_COMMIT and valid_sha(headers['record_sha256']),
            'owned header identity differs')
    cohort = data.get('cohort_sha256')
    require(valid_sha(cohort) and cohort == cohort_digest(data), 'owned cohort binding differs')

    artifacts = data.get('artifacts')
    require(isinstance(artifacts, dict) and set(artifacts) == set(MODULES),
            'owned provider requires exactly four artifacts')
    build_record = json.loads(Path(records['build']['path']).read_text())
    expected_bindings = dict(source_commit=source_data['commit'],
        source_status_sha256=source_data['status_sha256'],
        source_diff_sha256=source_data['diff_sha256'],
        configuration_record_sha256=records['configuration']['sha256'],
        toolchain_record_sha256=records['toolchain']['sha256'],
        dependencies_record_sha256=records['dependencies']['sha256'],
        headers_record_sha256=headers['record_sha256'])
    require(build_record.get('bindings') == expected_bindings,
            'build record does not bind owned provenance cohort')
    require(build_record.get('artifacts') ==
            {name: artifacts[name].get('sha256') for name in MODULES},
            'build record does not bind exact four artifacts')
    observed_exports = set()
    allowed_imports = SYSTEM_IMPORTS | {x.lower() for x in MODULES}
    for name in MODULES:
        item = artifacts[name]
        require(isinstance(item, dict) and set(item) ==
                {'path', 'sha256', 'bytes', 'machine', 'imports', 'exports', 'cohort_sha256'},
                'owned artifact fields differ: '+name)
        rel = item['path']
        require(isinstance(rel, str) and rel == name and
                not PurePosixPath(rel).is_absolute() and '..' not in PurePosixPath(rel).parts,
                'owned artifact path differs: '+name)
        target = (path.parent/rel).resolve()
        require(target.is_relative_to(path.parent) and target.is_relative_to(prefix),
                'owned artifact path escapes provider: '+name)
        require(valid_sha(item['sha256']) and digest(target) == item['sha256'] and
                target.stat().st_size == item['bytes'], 'owned artifact changed: '+name)
        require(item['sha256'] != PINNED_FILES[name], 'official DLL mixed into owned provider: '+name)
        require(item['cohort_sha256'] == cohort, 'owned artifact cohort differs: '+name)
        actual = inspect_pe(target, objdump)
        require(item['machine'] == actual['machine'] and item['imports'] == actual['imports'] and
                item['exports'] == actual['exports'], 'owned PE record differs: '+name)
        require(all(x in allowed_imports for x in actual['imports']),
                'owned import closure differs: '+name)
        observed_exports.update(actual['exports'])
    require(REQUIRED_EXPORTS <= observed_exports, 'frozen packet fixture exports missing')
    return data


def verify_strict_record(path, patch_review_record, forbidden_roots=(),
                         objdump='i686-w64-mingw32-objdump'):
    """Verify the one reviewed strict-seek source mutation and its fresh DLL cohort."""
    path = path.resolve();patch_review_record = patch_review_record.resolve()
    data = json.loads(path.read_text())
    require(data.get('schema') == 1 and data.get('provider') == 'owned-ffmpeg' and
            data.get('role') == 'reviewed-strict' and data.get('architecture') == 'x86',
            'unrecognized strict provider provenance')
    prefix = _private_path(data.get('private_prefix'), Path('/'), 'strict private prefix')
    require(prefix != Path('/') and path.is_relative_to(prefix) and
            patch_review_record.is_relative_to(prefix),
            'strict provider/review outside private prefix')
    forbidden = tuple(Path(x).resolve() for x in forbidden_roots)
    require(not any(prefix.is_relative_to(root) or root.is_relative_to(prefix)
                    for root in forbidden), 'strict private prefix overlaps forbidden root')

    base = data.get('base_control')
    require(isinstance(base, dict) and set(base) == {'record_path', 'record_sha256'} and
            base['record_sha256'] == STRICT_BASE_CONTROL_SHA256,
            'strict base-control fields')
    base_path = Path(base['record_path']).resolve()
    require(base_path != path and digest(base_path) == base['record_sha256'],
            'strict base-control record changed')
    control = verify_record(base_path, forbidden_roots, objdump)

    patch = data.get('patch')
    require(isinstance(patch, dict) and set(patch) ==
            {'review_record_sha256', 'patch_sha256', 'base_commit', 'changed_files'} and
            patch['review_record_sha256'] == digest(patch_review_record) and
            patch['patch_sha256'] == STRICT_PATCH_SHA256 and
            patch['base_commit'] == FFMPEG_COMMIT and
            patch['changed_files'] == list(STRICT_CHANGED_FILES),
            'strict reviewed patch identity differs')
    review = json.loads(patch_review_record.read_text())
    require(review.get('schema') == 1 and review.get('role') == 'source-review-only' and
            review.get('base_commit') == FFMPEG_COMMIT and
            Path(review.get('source', '')).resolve() == Path(data.get('source', {}).get('path', '')).resolve() and
            Path(review.get('patch_path', '')).resolve().is_relative_to(prefix) and
            review.get('patch_sha256') == STRICT_PATCH_SHA256 and
            review.get('changed_files') == list(STRICT_CHANGED_FILES) and
            Path(review.get('base_control_record', '')).resolve() == base_path and
            review.get('base_control_record_sha256') == base['record_sha256'] and
            review.get('provider_compiled') is False and review.get('wine_executed') is False and
            review.get('tests', {}).get('passed') == 3 and
            review.get('tests', {}).get('mapping_combinations') == 16,
            'unrecognized strict patch review')
    require(digest(Path(review['patch_path']).resolve()) == STRICT_PATCH_SHA256,
            'reviewed strict patch file changed')

    source_data = data.get('source')
    require(isinstance(source_data, dict) and set(source_data) ==
            {'path', 'commit', 'head', 'status_sha256', 'diff_sha256', 'changed_files'} and
            source_data['commit'] == FFMPEG_COMMIT and source_data['head'] == FFMPEG_COMMIT and
            source_data['diff_sha256'] == STRICT_PATCH_SHA256 and
            source_data['changed_files'] == list(STRICT_CHANGED_FILES),
            'strict source provenance fields')
    source = _private_path(source_data['path'], prefix, 'strict source')
    head = _git_output(source, 'rev-parse', 'HEAD').decode().strip()
    status = _git_output(source, 'status', '--porcelain=v1', '--untracked-files=all')
    diff = _git_output(source, 'diff', '--binary', 'HEAD', '--')
    expected_status = b' M '+STRICT_CHANGED_FILES[0].encode()+b'\n'
    require(head == FFMPEG_COMMIT and status == expected_status and
            source_data['status_sha256'] == hashlib.sha256(status).hexdigest() and
            hashlib.sha256(diff).hexdigest() == STRICT_PATCH_SHA256,
            'strict source differs from reviewed patch')

    records = data.get('records')
    require(isinstance(records, dict) and set(records) == set(RECORD_ROLES),
            'strict build record set differs')
    for role in ('build', 'configuration'):
        item = records[role]
        require(isinstance(item, dict) and set(item) == {'path', 'sha256'} and
                valid_sha(item['sha256']), 'strict '+role+' record fields')
        record_path = _private_path(item['path'], prefix, 'strict '+role+' record')
        require(digest(record_path) == item['sha256'], 'strict '+role+' record changed')
    for role in ('toolchain', 'dependencies'):
        require(records.get(role) == control['records'][role],
                'strict '+role+' identity differs from control')
        require(digest(Path(records[role]['path'])) == records[role]['sha256'],
                'strict '+role+' record changed')
    headers = data.get('headers')
    require(headers == control['headers'], 'strict header identity differs from control')

    strict_configuration = json.loads(Path(records['configuration']['path']).read_text())
    control_configuration = json.loads(Path(control['records']['configuration']['path']).read_text())
    require(isinstance(strict_configuration.get('command'), list) and
            strict_configuration['command'] and
            Path(strict_configuration['command'][0]).resolve() == source/'configure' and
            Path(strict_configuration.get('cwd', '')).resolve() == prefix/'build',
            'strict configuration paths differ')
    normalized_strict = json.loads(json.dumps(strict_configuration))
    normalized_control = json.loads(json.dumps(control_configuration))
    normalized_strict['command'][0] = normalized_control['command'][0]
    normalized_strict['cwd'] = normalized_control['cwd']
    strict_generated = strict_configuration.get('generated_files')
    control_generated = control_configuration.get('generated_files')
    require(isinstance(strict_generated, dict) and
            set(strict_generated) == set(control_generated or {}),
            'strict generated configuration set differs')
    control_build = Path(control_configuration['cwd']).resolve()
    changed_generated = {'ffbuild/config.mak': b'SRC_PATH=',
                         'ffbuild/config.sh': b'source_path='}
    for name in strict_generated:
        strict_file=(prefix/'build'/name).resolve();control_file=(control_build/name).resolve()
        require(strict_file.is_relative_to(prefix/'build') and
                control_file.is_relative_to(control_build) and
                digest(strict_file) == strict_generated[name] and
                digest(control_file) == control_generated[name],
                'generated configuration hash differs: '+name)
        strict_raw=strict_file.read_bytes();control_raw=control_file.read_bytes()
        if name not in changed_generated:
            require(strict_raw == control_raw, 'generated configuration changed: '+name)
            continue
        marker=changed_generated[name]
        strict_lines=strict_raw.splitlines(keepends=True);control_lines=control_raw.splitlines(keepends=True)
        strict_matches=[i for i,line in enumerate(strict_lines) if line.startswith(marker)]
        control_matches=[i for i,line in enumerate(control_lines) if line.startswith(marker)]
        require(len(strict_matches) == len(control_matches) == 1 and
                strict_lines[strict_matches[0]].endswith(b'\n') and
                Path(strict_lines[strict_matches[0]][len(marker):-1].decode()).is_absolute() and
                Path(strict_lines[strict_matches[0]][len(marker):-1].decode()).resolve() == source and
                control_lines[control_matches[0]] == marker+b'src\n',
                'generated source-path assignment differs: '+name)
        strict_lines[strict_matches[0]]=marker+b'<SOURCE>\n'
        control_lines[control_matches[0]]=marker+b'<SOURCE>\n'
        require(strict_lines == control_lines,
                'generated configuration differs beyond source path: '+name)
    normalized_strict['generated_files'] = normalized_control['generated_files']
    require(normalized_strict == normalized_control,
            'strict configuration differs beyond source/build paths')

    cohort = data.get('cohort_sha256')
    require(valid_sha(cohort) and cohort == strict_cohort_digest(data),
            'strict cohort binding differs')
    artifacts = data.get('artifacts')
    require(isinstance(artifacts, dict) and set(artifacts) == set(MODULES),
            'strict provider requires exactly four artifacts')
    build_record = json.loads(Path(records['build']['path']).read_text())
    expected_bindings = dict(source_commit=source_data['commit'],
        source_status_sha256=source_data['status_sha256'],
        source_diff_sha256=source_data['diff_sha256'],
        configuration_record_sha256=records['configuration']['sha256'],
        toolchain_record_sha256=records['toolchain']['sha256'],
        dependencies_record_sha256=records['dependencies']['sha256'],
        headers_record_sha256=headers['record_sha256'],
        base_control_record_sha256=base['record_sha256'],
        patch_review_record_sha256=patch['review_record_sha256'],
        patch_sha256=patch['patch_sha256'])
    require(build_record.get('bindings') == expected_bindings,
            'strict build record does not bind reviewed cohort')
    require(build_record.get('artifacts') ==
            {name: artifacts[name].get('sha256') for name in MODULES},
            'strict build record does not bind exact four artifacts')
    artifact_build = build_record.get('artifact_build')
    require(isinstance(artifact_build, dict) and set(artifact_build) ==
            {'mode', 'output_root', 'command_record_sha256'} and
            artifact_build['mode'] == 'fresh-four' and
            Path(artifact_build['output_root']).resolve() == prefix/'build' and
            valid_sha(artifact_build['command_record_sha256']) and
            digest(prefix/'build-command-record.json') == artifact_build['command_record_sha256'],
            'strict fresh-four build binding differs')
    command_record = json.loads((prefix/'build-command-record.json').read_text())
    require(set(command_record) == {'mode', 'output_root', 'initial_output_entries',
            'source_commit', 'source_diff_sha256', 'steps', 'outputs',
            'build_operation_counts', 'linked_targets'} and
            command_record['mode'] == 'fresh-four' and
            Path(command_record['output_root']).resolve() == prefix/'build' and
            command_record['initial_output_entries'] == [] and
            command_record['source_commit'] == FFMPEG_COMMIT and
            command_record['source_diff_sha256'] == STRICT_PATCH_SHA256,
            'strict build-command identity differs')
    steps = command_record['steps']
    require(isinstance(steps, list) and [step.get('name') for step in steps] == ['configure', 'make'],
            'strict build steps differ')
    for step in steps:
        require(set(step) == {'name', 'argv', 'cwd', 'environment', 'pid', 'deadline_seconds',
                'exit_code', 'seconds', 'log', 'log_sha256'} and
                isinstance(step['argv'], list) and step['argv'] and
                Path(step['cwd']).resolve() == prefix/'build' and
                isinstance(step['environment'], dict) and
                type(step['pid']) is int and step['pid'] > 0 and
                type(step['deadline_seconds']) in (int, float) and step['deadline_seconds'] > 0 and
                step['exit_code'] == 0 and type(step['seconds']) in (int, float) and step['seconds'] >= 0 and
                Path(step['log']).resolve().is_relative_to(prefix) and valid_sha(step['log_sha256']) and
                digest(Path(step['log']).resolve()) == step['log_sha256'],
                'strict build step evidence differs')
    require(steps[0]['argv'] == strict_configuration['command'] and
            steps[0]['environment'] == strict_configuration['environment'] and
            steps[1]['argv'] == ['make', '-j4', 'libavformat/avformat-lav-62.dll'] and
            steps[1]['environment'] == strict_configuration['environment'],
            'strict configure/make recipe differs')
    outputs = command_record['outputs']
    require(isinstance(outputs, dict) and set(outputs) == set(MODULES),
            'strict fresh output set differs')
    for name in MODULES:
        output = outputs[name]
        require(isinstance(output, dict) and set(output) == {'path', 'bytes', 'sha256'} and
                Path(output['path']).resolve().is_relative_to(prefix/'build') and
                output['bytes'] == artifacts[name]['bytes'] and
                output['sha256'] == artifacts[name]['sha256'] and
                digest(Path(output['path']).resolve()) == output['sha256'],
                'strict fresh output binding differs: '+name)
    counts = command_record['build_operation_counts']
    require(isinstance(counts, dict) and set(counts) == {'CC', 'X86ASM', 'LD'} and
            all(type(value) is int and value > 0 for value in counts.values()),
            'strict build operation counts differ')
    linked = command_record['linked_targets']
    require(isinstance(linked, list) and len(linked) == len(MODULES) and
            all(any(name in row for row in linked) for name in MODULES),
            'strict linked target set differs')
    make_rows=Path(steps[1]['log']).read_text().splitlines()
    observed_counts={kind:sum(row.startswith(kind+'\t') for row in make_rows)
                     for kind in ('CC','X86ASM','LD')}
    observed_linked=[row for row in make_rows if row.startswith('LD\t')]
    require(counts == observed_counts and linked == observed_linked,
            'strict build log operation evidence differs')

    observed_exports = set();allowed_imports = SYSTEM_IMPORTS | {x.lower() for x in MODULES}
    for name in MODULES:
        item = artifacts[name]
        require(isinstance(item, dict) and set(item) ==
                {'path', 'sha256', 'bytes', 'machine', 'imports', 'exports', 'cohort_sha256'},
                'strict artifact fields differ: '+name)
        require(item['path'] == name and '..' not in PurePosixPath(item['path']).parts,
                'strict artifact path differs: '+name)
        target = (path.parent/name).resolve()
        require(target.is_relative_to(path.parent) and target.is_relative_to(prefix) and
                valid_sha(item['sha256']) and digest(target) == item['sha256'] and
                target.stat().st_size == item['bytes'], 'strict artifact changed: '+name)
        require(item['sha256'] != PINNED_FILES[name] and item['cohort_sha256'] == cohort,
                'strict artifact identity differs: '+name)
        actual = inspect_pe(target, objdump)
        require(item['machine'] == actual['machine'] and item['imports'] == actual['imports'] and
                item['exports'] == actual['exports'], 'strict PE record differs: '+name)
        require(item['imports'] == control['artifacts'][name]['imports'] and
                item['exports'] == control['artifacts'][name]['exports'],
                'strict PE closure differs from control: '+name)
        require(all(x in allowed_imports for x in actual['imports']),
                'strict import closure differs: '+name)
        observed_exports.update(actual['exports'])
    require(REQUIRED_EXPORTS <= observed_exports, 'strict packet fixture exports missing')
    return data


def protected_paths(path, data):
    """Return compact retained files whose bytes must remain unchanged during a run."""
    prefix = Path(data['private_prefix']).resolve()
    result = {'selected_provider_record': path.resolve()}
    result.update({'selected_provider_'+name: (path.parent/name).resolve() for name in MODULES})
    for role in RECORD_ROLES:
        record_path=Path(data['records'][role]['path']).resolve()
        if data.get('role') != 'reviewed-strict' or role in ('build','configuration'):
            record_path=_private_path(data['records'][role]['path'],prefix,role)
        result['selected_'+role+'_record']=record_path
    if data.get('role') == 'reviewed-strict':
        result['selected_base_control_record']=Path(data['base_control']['record_path']).resolve()
    return result
