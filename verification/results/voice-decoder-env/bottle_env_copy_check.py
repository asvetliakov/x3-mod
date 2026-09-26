"""`manage.py voice-decoder --bottle-env apply/remove` on a scratch copy of a real bottle's cxbottle.conf (read-only on
the bottle): prints the diff of the applied copy, whether the backup equals the original, the launcher status and
whether remove restores the original bytes. Run from the repository root:
    python3 verification/results/voice-decoder-env/bottle_env_copy_check.py [BOTTLE] [SCRATCH_DIR]
Result 2026-09-27 (bottle X3): results-bottle-env.txt."""
import filecmp
import importlib.util
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
bottle = sys.argv[1] if len(sys.argv) > 1 else 'X3'
scratch = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(tempfile.mkdtemp())
real = Path.home() / 'Library/Application Support/CrossOver/Bottles' / bottle
(scratch / bottle).mkdir(parents=True, exist_ok=True)
shutil.copy2(real / 'cxbottle.conf', scratch / bottle / 'cxbottle.conf')
sys.path.insert(0, str(ROOT))
sys.argv = [sys.argv[0]]
spec = importlib.util.spec_from_file_location('manage', ROOT / 'tools/manage.py')
manage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manage)
manage.CROSSOVER_BOTTLES = scratch
game = real / 'drive_c/X3'
arguments = ['--bottle', bottle, '--game-dir', str(game)]
with mock.patch.object(manage.media_package, 'assert_game_closed'):  # the copy is not the live bottle
    print('apply exit', manage.voice_decoder_command(['--bottle-env', 'apply', *arguments]))
    print(subprocess.run(['diff', str(real / 'cxbottle.conf'), str(scratch / bottle / 'cxbottle.conf')],
                         capture_output=True, text=True).stdout, end='')
    print('backup identical to the original', filecmp.cmp(real / 'cxbottle.conf', scratch / bottle / 'cxbottle.conf.x3m-bak',
                                                          shallow=False))
    print('launcher status', manage.voice_bottle_env_status(scratch / bottle, game))
    print('remove exit', manage.voice_decoder_command(['--bottle-env', 'remove', *arguments]))
print('remove restored the original bytes', filecmp.cmp(real / 'cxbottle.conf', scratch / bottle / 'cxbottle.conf', shallow=False))
