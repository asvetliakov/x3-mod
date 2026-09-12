"""Host-only controls for the narrow temporal fixture timeout drain."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

PROBE = Path(__file__).resolve().parents[1] / 'probe'
sys.path.insert(0, str(PROBE))
spec = importlib.util.spec_from_file_location('temporal_cleanup_runner', PROBE / 'temporal_run.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class TemporalCleanup(unittest.TestCase):
    def test_only_exact_fixture_with_shader_argument_and_live_process(self):
        shader = Path('/work/src/resolve.hlsl')
        rows = ('11 S Y:\\work\\temporal_resolve.exe C:\\X3\\d3dx9_37.dll Z:/work/src/resolve.hlsl\n'
                '12 S winewrapper.exe --run Y:\\work\\temporal_resolve.exe Z:/work/src/resolve.hlsl\n'
                '13 S Y:\\work\\X3AP.exe Z:/work/src/resolve.hlsl\n'
                '14 S Y:\\work\\temporal_resolve.exe Z:/different/resolve.hlsl\n'
                '15 Z Y:\\work\\temporal_resolve.exe Z:/work/src/resolve.hlsl\n'
                '16 S Y:\\work\\temporal_resolve.exe Z:/work/src/resolve.hlsl.other\n'
                '17 S Y:\\work\\temporal_resolve.exe Z:/work/src/resolve.hlsl extra\n')
        with patch.object(runner.subprocess, 'check_output', return_value=rows):
            self.assertEqual(set(runner.fixture_children(shader)), {11})

    def test_natural_completion_drains_without_signal(self):
        with patch.object(runner, 'fixture_children', side_effect=[{11: 'fixture'}, {}]), \
             patch.object(runner.time, 'sleep') as sleep, patch.object(runner.os, 'kill') as kill:
            self.assertTrue(runner.drain_fixture(Path('/shader'))['drained'])
            sleep.assert_called_once_with(1)
            kill.assert_not_called()

    def test_signal_rechecks_exact_command_against_pid_reuse(self):
        # First grace expires; the PID's command then changes before signalling.
        with patch.object(runner, 'fixture_children', side_effect=[{11:'original'}, {11:'original'}, {11:'replacement'}, {}]), \
             patch.object(runner.time, 'monotonic', side_effect=[0, 0, 16, 16, 16]), \
             patch.object(runner.time, 'sleep'), patch.object(runner.os, 'kill') as kill:
            result = runner.drain_fixture(Path('/shader'))
            self.assertTrue(result['drained'])
            self.assertEqual(result['signalled'], [])
            kill.assert_not_called()

    def test_matching_child_terminated_after_grace(self):
        with patch.object(runner, 'fixture_children', side_effect=[{11:'fixture'}]*3 + [{}]), \
             patch.object(runner.time, 'monotonic', side_effect=[0, 0, 16, 16, 16]), \
             patch.object(runner.time, 'sleep'), patch.object(runner.os, 'kill') as kill:
            result = runner.drain_fixture(Path('/shader'))
            self.assertTrue(result['drained'])
            kill.assert_called_once_with(11, runner.signal.SIGTERM)

    def test_matching_child_killed_only_after_term_grace(self):
        with patch.object(runner, 'fixture_children', side_effect=[{11:'fixture'}]*6 + [{}]), \
             patch.object(runner.time, 'monotonic', side_effect=[0, 0, 16, 16, 27, 27]), \
             patch.object(runner.time, 'sleep'), patch.object(runner.os, 'kill') as kill:
            result = runner.drain_fixture(Path('/shader'))
            self.assertTrue(result['drained'])
            self.assertEqual([c.args for c in kill.call_args_list],
                             [(11, runner.signal.SIGTERM), (11, runner.signal.SIGKILL)])


if __name__ == '__main__':
    unittest.main()
