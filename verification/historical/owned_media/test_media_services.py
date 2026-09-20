"""Connected production services; synthetic worker events, real Clock/FrameLease/copy core."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def overlay(folder):
    # Root-authorized temporary include overlay while independently owned
    # dependencies finish integration. Symlinks keep the actual source intact.
    roots = [ROOT]
    for variable, fallback in (
        ('X3M_SERVICES_DESTINATION', '/tmp/x3-media-destination-integration'),
        ('X3M_SERVICES_WORKER', '/tmp/x3-media-worker-sample'),
    ):
        roots.append(Path(os.environ.get(variable, fallback)))
    for source_root in roots:
        for directory in ('src/proxy', 'src/media', 'src/ownership'):
            base = source_root / directory
            if not base.exists():
                continue
            for source in base.rglob('*'):
                if not source.is_file() or source.suffix not in ('.h', '.cpp'):
                    continue
                target = folder / source.relative_to(source_root)
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.exists():
                    target.symlink_to(source)
    clock = ROOT / 'src/media/owned_clock/clock.h'
    if not clock.exists() or 'bool set_end(' not in clock.read_text():
        clock = Path(os.environ.get('X3M_SERVICES_CLOCK', '/tmp/x3-media-clock-bound')) / 'src/media/owned_clock/clock.h'
    if not clock.exists() or 'bool set_end(' not in clock.read_text():
        raise RuntimeError('Reviewed canonical Clock::set_end dependency is unavailable')
    target = folder / 'src/media/owned_clock/clock.h'
    if target.exists():
        target.unlink()
    target.parent.mkdir(parents=True, exist_ok=True)
    target.symlink_to(clock)
    probe = folder / 'verification/probe'
    probe.mkdir(parents=True)
    for name in ('media_services_fixture.cpp', 'media_presentation_gate_contract_fixture.h'):
        (probe / name).symlink_to(ROOT / 'verification/probe' / name)
    return folder


class ServicesTests(unittest.TestCase):
    def test_connected_services(self):
        with tempfile.TemporaryDirectory(prefix='x3-services-') as directory:
            folder = overlay(Path(directory))
            sources = (
                'src/proxy/media_services.cpp', 'src/proxy/media_playback.cpp',
                'src/media/playback_runtime.cpp', 'src/proxy/media_startup.cpp',
                'src/proxy/media_presentation_gate.cpp', 'src/proxy/media_destination.cpp',
                'verification/probe/media_services_fixture.cpp',
            )
            binary = folder / 'fixture'
            command = [shutil.which('c++'), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
                       '-I', str(folder / 'src/proxy'), '-I', str(folder / 'src/media'),
                       '-I', str(folder / 'verification/probe'),
                       *(str(folder / source) for source in sources), '-o', str(binary)]
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            print(result.stdout.strip())
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('failures=0 allocations=0', result.stdout)


if __name__ == '__main__':
    unittest.main()
