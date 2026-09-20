#!/usr/bin/env python3
"""Run one frozen connected executable, only inside the root's X3 Wine lock."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import bottle
from game_guard import game_running
from prepare_lav_fixture import verify_record
from prepare_lav_graph_provider import verify_record as verify_graph
from run_media_playback_fixture import fixture_environment, launcher_prefix, supervise_host_process, validate_derivation
from run_media_worker_clock_fixture import locked_by_ancestor, package_selection, bind_runtime_assets
import media_connected_evidence as evidence
ROOT = Path(__file__).resolve().parents[2]
LOG_LIMIT = 4*1024*1024

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe','output','media','lav-provider-record','lav-graph-provider-record',
                'derived-media-record','original-reference-result','strict-reference-result','eof-reference-result'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--exe-sha256', required=True)
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3' or not locked_by_ancestor():
        parser.error('explicit X3 bottle and root wine_lock.py ancestor required')
    if game_running(): parser.error('game is running')
    for name,value in vars(args).items():
        if isinstance(value,Path): setattr(args,name,value.resolve())
    if args.output.exists(): parser.error('output must be a new private directory')
    for path in (args.exe,args.output,args.media,args.lav_provider_record,args.lav_graph_provider_record):
        if path.is_relative_to(bottle.bottle_dir().resolve()): parser.error('fixture/assets must be outside bottle')
    if args.exe.with_name('d3d9.dll').exists(): parser.error('fixture module root contains app-local d3d9.dll')
    try:
        official = verify_record(args.lav_provider_record)
        graph = verify_graph(args.lav_graph_provider_record,(bottle.bottle_dir().resolve(),))
        evidence.require(graph['inputs']['official']['record_sha256'] == evidence.digest(args.lav_provider_record), 'provider origin differs')
        build_path = args.exe.with_suffix('.build.json')
        build = json.loads(build_path.read_text())
        evidence.require(build['kind'] == 'media_connected_build_v1' and build['provider'] == official and
                         build['exe_sha256'] == args.exe_sha256 == evidence.digest(args.exe), 'frozen build/cohort differs')
        evidence.require(build['production_inputs'] and all((ROOT/key).is_file() and evidence.digest(ROOT/key) == value
                         for key,value in build['production_inputs'].items()), 'compiled source/header changed')
        evidence.require(evidence.audit_counter_contract(ROOT) == build['counter_contract'], 'compiled Counter contract differs')
        evidence.require(evidence.audit_image(args.exe) == build['image'], 'frozen PE mapping differs')
        original_media = bottle.game_dir()/'mov/00002.dat'
        derived = validate_derivation(args.derived_media_record,args.media,original_media,10000,True)
        manifest,media,package_paths,package = package_selection(args.exe)
        runtime_files = bind_runtime_assets(graph,manifest,media,args.media)
        evidence.require(not any(path.is_relative_to(bottle.bottle_dir().resolve()) for path in (manifest,media)), 'package assets inside bottle')
        record = dict(schema=1,kind=evidence.KIND,build=build,exe=evidence.binding(args.exe),
                      media=dict(path=str(args.media),sha256=evidence.digest(args.media)),derived_media=derived,
                      lav_provider=evidence.binding(args.lav_provider_record),lav_graph_provider=evidence.binding(args.lav_graph_provider_record),
                      original_reference=evidence.binding(args.original_reference_result),strict_reference=evidence.binding(args.strict_reference_result),
                      eof_reference=evidence.binding(args.eof_reference_result),package_config=package,bottle=bottle.describe(),native_windows_run=False)
        oracles = evidence.load_oracles(record)
        protected = dict(exe=args.exe,build=build_path,media=args.media,original_media=original_media,
                         original_reference=args.original_reference_result,strict_reference=args.strict_reference_result,
                         eof_reference=args.eof_reference_result,derivation=args.derived_media_record,
                         official=args.lav_provider_record,graph=args.lav_graph_provider_record,
                         game_exe=bottle.game_dir()/'X3AP.exe',bottle_config=bottle.bottle_dir()/'cxbottle.conf',runtime_media=media)
        protected.update(package_paths);protected.update({'runtime_'+key:path for key,path in runtime_files.items()})
        protected.update({'source_'+key:ROOT/key for key in build['production_inputs']})
        checker_names = ('media_connected_run.py','media_connected_evidence.py','media_connected_build.py',
                         'media_lav_evidence.py','media_worker_sample_evidence.py','run_media_playback_fixture.py',
                         'run_media_worker_clock_fixture.py','prepare_lav_fixture.py','prepare_lav_graph_provider.py',
                         'owned_lav_provider.py','bottle.py','game_guard.py','prepare_lav_packet_headers.py',
                         'media_worker_clock_evidence.py','media_worker_transport_tail_evidence.py',
                         'build_media_worker_clock_fixture.py','build_media_playback_fixture.py')
        record['checker_inputs'] = {name:evidence.binding(ROOT/'verification/probe'/name) for name in checker_names}
        protected.update({'checker_'+name:ROOT/'verification/probe'/name for name in checker_names})
        before = {key:evidence.digest(path) for key,path in protected.items()}
    except (OSError,KeyError,ValueError,TypeError) as error:
        parser.error('connected preflight: '+str(error))
    args.output.mkdir(parents=True,exist_ok=False)
    env = fixture_environment(os.environ,args.output/'unused-gst',args.output)
    env.update(WINEDLLOVERRIDES='d3d9=b;winegstreamer=',GST_PLUGIN_PATH_1_0='',GST_PLUGIN_SYSTEM_PATH_1_0='',
               FEX_X87REDUCEDPRECISION='1',WINEMSYNC='1')
    command = [*launcher_prefix(args.exe,True),'--output','Z:'+str(args.output).replace('/','\\')]
    record.update(command=command,environment={key:env.get(key) for key in
                  ('X3M_FIXTURE_BOTTLE','FEX_X87REDUCEDPRECISION','WINEMSYNC','WINEDLLOVERRIDES','WINEDEBUG')},
                  bounds=dict(child_seconds=90,outer_seconds=100,call_seconds=10,captures=32,frame_bytes=evidence.FRAME_BYTES,log_bytes=LOG_LIMIT))
    stdout,stderr = args.output/'stdout.log',args.output/'stderr.log'
    begin = time.monotonic()
    with stdout.open('wb') as out,stderr.open('wb') as err:
        proc = subprocess.Popen(command,cwd=args.output,env=env,stdout=out,stderr=err,start_new_session=True)
        record.update(supervise_host_process(proc,[stdout,stderr],100,LOG_LIMIT))
    record['wall_seconds'] = time.monotonic()-begin
    record['unchanged'] = {key:path.is_file() and evidence.digest(path) == before[key] for key,path in protected.items()}
    record['logs'] = {path.name:evidence.binding(path) for path in (stdout,stderr)}
    try:
        evidence.require(record.get('exit_code') == 0 and not record.get('outer_timeout') and
                         not record.get('diagnostic_log_limit') and all(record['unchanged'].values()), 'runtime/immutability failed')
        evidence.require(sum(path.stat().st_size for path in (stdout,stderr)) <= LOG_LIMIT,'diagnostic log bound exceeded')
        after_oracles = evidence.load_oracles(record)
        evidence.require(oracles == after_oracles,'frozen reference captures changed')
        rows = evidence.parse(stdout.read_text(errors='replace')+'\n'+stderr.read_text(errors='replace'))
        record['qualification'] = evidence.validate(rows,args.output,oracles)
    except (OSError,ValueError,KeyError,TypeError) as error:
        record['validation_error'] = str(error)
    result = args.output/'result.json';result.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(dict(result=str(result),passed='qualification' in record,error=record.get('validation_error'))))
    return 0 if 'qualification' in record else 1
if __name__ == '__main__':
    raise SystemExit(main())
