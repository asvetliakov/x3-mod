#!/usr/bin/env python3
"""Build only the standalone two-worker clock/draw fixture; never execute Wine."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from build_media_playback_fixture import FLAGS, LIBS
from prepare_lav_fixture import verify_record

ROOT = Path(__file__).resolve().parents[2]
PRODUCTION_INPUTS = ('src/media/lav_worker.cpp','src/media/lav_worker.h','src/media/lav_graph_inc.h',
                     'src/media/playback_runtime.h','src/media/package_config.h','src/media/package_config.cpp','src/media/owned_clock/clock.h','src/media/owned_clock/exact_time.h')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', default='i686-w64-mingw32-g++')
    parser.add_argument('--lav-provider-record', type=Path, required=True)
    args = parser.parse_args()
    provider = args.lav_provider_record.resolve()
    verified = verify_record(provider)
    output = args.output.resolve()
    if output.exists() or output.with_suffix('.build.json').exists():
        parser.error('refuse to overwrite a retained executable or build record')
    output.parent.mkdir(parents=True, exist_ok=True)
    source = ROOT/'verification/probe/media_worker_clock_fixture.cpp'
    helper = source.with_name('media_lav_fixture_inc.h')
    command = [args.compiler, *FLAGS, '-I'+str(ROOT/'src/media'), '-isystem', str(provider.parent/'include'),
               str(source), str(ROOT/'src/media/lav_worker.cpp'), str(ROOT/'src/media/package_config.cpp'), '-o', str(output), *LIBS, '-lddraw']
    subprocess.run(command, check=True)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    record = dict(schema=1, kind='worker_clock_build_v2', command=command,
                  toolchain=subprocess.check_output([args.compiler, '--version'], text=True).splitlines()[0],
                  source_sha256=sha(source), lav_helper_sha256=sha(helper), exe_sha256=sha(output), exe=str(output),
                  lav_provider=verified, configuration_delta='production_transport_process_module_pin_package_reader_owned_pins_assignment_quiescence_SupportSeeking_TRUE_unique_cohort_real_two_texture_draw_pause_waits_actual_peer_selection',
                  production_inputs={name:sha(ROOT/name) for name in PRODUCTION_INPUTS},
                  supported_modes=['worker-clock-two-textures','eof-tail'],
                  helper_role='frozen_reference_provenance_not_compiled',
                  clock_commit='0abe0a44', clock_headers={name:sha(ROOT/'src/media/owned_clock'/name) for name in ('clock.h','exact_time.h')})
    output.with_suffix('.build.json').write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
