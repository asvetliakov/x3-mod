#!/usr/bin/env python3
"""Run one frozen worker sample fixture under the parent-owned X3 Wine lock."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

import bottle
from game_guard import game_running
from prepare_lav_fixture import BINARIES, verify_record
from prepare_lav_graph_provider import verify_record as verify_graph
from owned_lav_provider import verify_strict_record, protected_paths
from run_media_playback_fixture import (digest, validate_derivation, fixture_environment,
                                        launcher_prefix, supervise_host_process)
import media_worker_clock_evidence as evidence
import media_worker_transport_tail_evidence as tail_evidence
from build_media_worker_clock_fixture import PRODUCTION_INPUTS

ROOT = Path(__file__).resolve().parents[2]
LOG_LIMIT = 64*1024*1024
RUNTIME_FILES = (*BINARIES, 'provider.manifest', 'LAVFilters.Dependencies.manifest')


def locked_by_ancestor():
    """Check the existing lock owner's PID is in this runner's parent chain."""
    try:
        text = Path('/tmp/x3-wine-runner.lock').read_text()
        # The lease record is written by wine_lock.py while holding flock.
        owner = int(text.split()[0])
        pid = os.getppid()
        for _ in range(12):
            if pid == owner:
                return True
            if pid <= 1:
                break
            pid = int(subprocess.check_output(['ps', '-o', 'ppid=', '-p', str(pid)], text=True).strip())
    except (OSError, ValueError, IndexError, subprocess.SubprocessError):
        pass
    return False



def bind_runtime_assets(graph,manifest,runtime_media,reference_media):
    """Relocation changes paths, never the frozen provider/source payloads."""
    # The frozen graph record also protects SDK headers and root notices. They
    # are provenance inputs, not the installed runtime's filesystem layout.
    evidence.require(all(name in graph['files'] for name in RUNTIME_FILES),'frozen runtime cohort entry absent')
    files={name:manifest.parent/name for name in RUNTIME_FILES}
    evidence.require(manifest.name=='provider.manifest' and all(path.is_file() and path.stat().st_size==graph['files'][name]['bytes'] and digest(path)==graph['files'][name]['sha256'] for name,path in files.items()),'relocated runtime cohort bytes differ')
    evidence.require(runtime_media.is_file() and runtime_media.stat().st_size==reference_media.stat().st_size and digest(runtime_media)==digest(reference_media),'relocated source bytes differ')
    return files


def package_selection(exe):
    """Bind small installed records; actual public Windows reader is still run."""
    root=exe.parent
    def local(relative):
        path=(root/relative).resolve()
        evidence.require(path.is_relative_to(root) and not Path(relative).is_absolute(),'package record escapes fixture module root')
        return path
    def small(path):
        evidence.require(path.is_file() and not path.is_symlink() and 0<path.stat().st_size<=128*1024,'package record missing/oversized')
        return json.loads(path.read_text())
    install=root/'x3-modern-install.json';selection=small(install)['media']
    package_path=local(selection['package_record_relative']);package=small(package_path)
    source_path=local(selection['sources'][0]['source_record_relative']);source=small(source_path)
    evidence.require(digest(package_path)==selection['package_record_sha256'] and digest(source_path)==selection['sources'][0]['source_record_sha256'],'installed small record binding differs')
    manifest=local('x3-modern-media/'+package['provider']['manifest']);asset=local(source['asset'])
    protected={'package_install':install,'package_record':package_path,'package_source_record':source_path}
    protected.update({'package_payload_'+name:local('x3-modern-media/'+item['path']) for name,item in package['provider']['files'].items()})
    notices={name:item for name,item in package['provider']['files'].items() if item['role']=='notice'}
    evidence.require(set(notices)=={'notices/COPYING','notices/README.md','notices/FFmpeg-LICENSE.md'},'selected package notice rows differ')
    for name,item in notices.items():
        path=protected['package_payload_'+name]
        evidence.require(path==(manifest.parent/name).resolve() and path.is_file() and path.stat().st_size==item['bytes'] and digest(path)==item['sha256'],'installed package notice bytes differ: '+name)
    return manifest,asset,protected,dict(enabled=True,module_root=str(root),install=evidence.binding(install),package=evidence.binding(package_path),source=evidence.binding(source_path),source_id=source['id'],effective_flags=source['effective_flags'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'output', 'media', 'lav-provider-record', 'lav-graph-provider-record',
                 'derived-media-record', 'original-reference-result', 'strict-reference-result'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--exe-sha256', required=True)
    parser.add_argument('--package-config',action='store_true',required=True,help='exercise actual module-relative package reader and retained owner')
    parser.add_argument('--mode',choices=('worker-clock-two-textures','eof-tail'),default='worker-clock-two-textures')
    parser.add_argument('--eof-reference-result',type=Path)
    parser.add_argument('--runtime-manifest',type=Path)
    parser.add_argument('--runtime-media',type=Path)
    args = parser.parse_args()
    eof=args.mode=='eof-tail';scoped=tail_evidence if eof else evidence
    if eof != (args.eof_reference_result is not None):parser.error('only eof-tail requires --eof-reference-result')
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3' or not locked_by_ancestor():
        parser.error('set X3M_FIXTURE_BOTTLE=X3 and invoke through wine_lock.py')
    if game_running():
        parser.error('game is running')
    for name, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, name, value.resolve())
    for path in (args.exe, args.output, args.media, args.lav_provider_record, args.lav_graph_provider_record):
        if path.is_relative_to(bottle.bottle_dir().resolve()):
            parser.error('fixture artifacts/provider/media must be outside the bottle')
    if args.exe.with_name('d3d9.dll').exists():
        parser.error('fixture directory contains app-local d3d9.dll')
    if args.output.exists():
        parser.error('output must be a new private directory')
    try:
        official = verify_record(args.lav_provider_record)
        graph = verify_graph(args.lav_graph_provider_record, (bottle.bottle_dir().resolve(),))
        evidence.require(graph['inputs']['official']['record_sha256'] == digest(args.lav_provider_record), 'composed official origin differs')
        build_path = args.exe.with_suffix('.build.json')
        build = json.loads(build_path.read_text())
        evidence.require(build.get('kind')=='worker_clock_build_v2' and build.get('clock_commit')=='0abe0a44', 'owned clock build identity differs')
        evidence.require(build.get('clock_headers')=={name:digest(ROOT/'src/media/owned_clock'/name) for name in ('clock.h','exact_time.h')}, 'owned clock headers changed')
        evidence.require(build.get('production_inputs')=={name:digest(ROOT/name) for name in PRODUCTION_INPUTS} and args.mode in build.get('supported_modes',[]),'production transport inputs/mode differ')
        evidence.require(digest(args.exe) == args.exe_sha256 == build['exe_sha256'], 'frozen EXE identity mismatch')
        evidence.require(build['source_sha256'] == digest(ROOT/'verification/probe/media_worker_clock_fixture.cpp') and
                         build['lav_helper_sha256'] == digest(ROOT/'verification/probe/media_lav_fixture_inc.h') and
                         build['lav_provider'] == official, 'source/helper/header build binding differs')
        original_media = (bottle.game_dir()/'mov/00002.dat').resolve()
        derivation = validate_derivation(args.derived_media_record, args.media, original_media, 10000, True)
        record = dict(schema=1, kind=scoped.KIND, mode=args.mode, build=build,
                      exe=dict(path=str(args.exe), sha256=args.exe_sha256),
                      media=dict(path=str(args.media), size=args.media.stat().st_size, sha256=digest(args.media)),
                      lav_provider=evidence.binding(args.lav_provider_record),
                      lav_graph_provider=evidence.binding(args.lav_graph_provider_record),
                      original_reference=evidence.binding(args.original_reference_result),
                      strict_reference=evidence.binding(args.strict_reference_result), derived_media=derivation,
                      bottle=bottle.describe(), native_windows_run=False,
                      configuration_delta=build['configuration_delta'],
                      reference_compatibility='same_frozen_reference_cohorts_RGB_time_oracles_production_transport_extract_SupportSeeking_TRUE',
                      fixture_bounds=dict(call_ms=10000, progress_ms=10000, child_ms=60000, outer_seconds=95,
                                          captures=12 if eof else 40, capture_bytes=(12 if eof else 40)*evidence.FRAME_BYTES, readbacks=12 if eof else 52, text_bytes=LOG_LIMIT))
        if eof:record['eof_reference']=evidence.binding(args.eof_reference_result)
        manifest,runtime_media,package_protected,package_record=package_selection(args.exe)
        evidence.require(args.runtime_manifest in (None,manifest) and args.runtime_media in (None,runtime_media),'runtime override differs from module-relative package selection')
        record['package_config']=package_record
        evidence.require(not any(p.is_relative_to(bottle.bottle_dir().resolve()) for p in (manifest,runtime_media)),'runtime fixture assets must remain outside bottle')
        runtime_files=bind_runtime_assets(graph,manifest,runtime_media,args.media)
        record['runtime_paths']=dict(manifest=str(manifest),media=str(runtime_media),relocated=manifest.parent!=args.lav_graph_provider_record.parent or runtime_media!=args.media,
                                     cohort_byte_identity_verified=True,source_byte_identity_verified=True)
        missing=args.output/'missing-source.mkv'
        evidence.require(not missing.exists(), 'negative source unexpectedly exists')
        record['negative_source']=dict(path=str(missing), exists_before=False)
        expected = scoped.references(record)
        protected = dict(exe=args.exe, build=build_path, source=ROOT/'verification/probe/media_worker_clock_fixture.cpp',
                         helper=ROOT/'verification/probe/media_lav_fixture_inc.h', media=args.media, original_media=original_media,
                         derivation=args.derived_media_record, official=args.lav_provider_record,
                         graph=args.lav_graph_provider_record, original_reference=args.original_reference_result,
                         strict_reference=args.strict_reference_result, game_exe=bottle.game_dir()/'X3AP.exe',
                         bottle_config=bottle.bottle_dir()/'cxbottle.conf')
        protected.update({'production_'+name:ROOT/name for name in PRODUCTION_INPUTS})
        protected.update({'runtime_'+name:path for name,path in runtime_files.items()});protected['runtime_media']=runtime_media;protected.update(package_protected)
        if eof:protected['eof_reference']=args.eof_reference_result
        for prefix, path, files in (('official', args.lav_provider_record, official['files']),
                                    ('graph', args.lav_graph_provider_record, graph['files'])):
            protected.update({prefix+'_'+name: path.parent/name for name in files})
        strict_path = Path(graph['inputs']['strict']['record_path'])
        review_path = Path(graph['inputs']['patch_review']['path'])
        strict = verify_strict_record(strict_path, review_path)
        protected.update({'strict_input_'+k: v for k, v in protected_paths(strict_path, strict).items()})
        protected['patch_review'] = review_path
        before = {key: digest(path) for key, path in protected.items()}
    except (OSError, KeyError, ValueError, TypeError) as error:
        parser.error('worker preflight failed: '+str(error))
    args.output.mkdir(parents=True, exist_ok=False)
    env = fixture_environment(os.environ, args.output/'unused-gst', args.output)
    env.update(WINEDLLOVERRIDES='d3d9=b;winegstreamer=', GST_PLUGIN_PATH_1_0='', GST_PLUGIN_SYSTEM_PATH_1_0='')
    command = [*launcher_prefix(args.exe, True), '--missing-media', 'Z:'+str(missing).replace('/', '\\'),
               '--package-config','1','--mode', 'eof-tail' if eof else 'clock', '--media', 'Z:'+str(runtime_media).replace('/', '\\'), '--lav-manifest',
               'Z:'+str(manifest).replace('/', '\\')]
    record.update(command=command, environment={k: env.get(k) for k in
                  ('WINEDLLOVERRIDES', 'WINEDEBUG', 'GST_PLUGIN_PATH_1_0', 'GST_PLUGIN_SYSTEM_PATH_1_0')},
                  launcher_controls=dict(explicit_debugmsg=True, explicit_dll_override=True,
                                         wrapper_sha256=digest(Path(bottle.WINE)), effective_child_environment_observed=False))
    stdout, stderr = args.output/'stdout.log', args.output/'stderr.log'
    started = time.monotonic()
    with stdout.open('wb') as out, stderr.open('wb') as err:
        process = subprocess.Popen(command, cwd=args.output, env=env, stdout=out, stderr=err, start_new_session=True)
        record.update(supervise_host_process(process, [stdout, stderr], 95, LOG_LIMIT))
    record['wall_seconds'] = time.monotonic()-started
    record['negative_source']['exists_after']=missing.exists()
    record['unchanged'] = {key: path.is_file() and digest(path) == before[key] for key, path in protected.items()}
    record['final_log_bytes'] = {path.name: path.stat().st_size for path in (stdout, stderr)}
    if sum(record['final_log_bytes'].values()) > LOG_LIMIT:
        record['diagnostic_log_limit'] = dict(limit_bytes=LOG_LIMIT, observed_bytes=sum(record['final_log_bytes'].values()))
    # Revalidate reference captures as well as their result records after runtime.
    try:
        expected = scoped.references(record)
    except (OSError, ValueError, KeyError, TypeError) as error:
        record['unchanged']['reference_revalidation'] = False
        record['reference_error'] = str(error)
    rows = evidence.parse(stdout.read_text(errors='replace')) if stdout.stat().st_size <= LOG_LIMIT else {}
    result = scoped.finish(record, args.output, rows, expected)
    key='worker_transport_eof' if eof else 'worker_clock';accepted=key+'_accepted'
    record[key]=result;record[accepted]=result[accepted]
    (args.output/'result.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(dict(result=str(args.output/'result.json'), exit_code=record['exit_code'],
                          mode=args.mode,accepted=record[accepted],
                          outcome=result['outcome'], validation_error=result.get('validation_error'))))
    return 0 if record[accepted] else 2


if __name__ == '__main__':
    raise SystemExit(main())
