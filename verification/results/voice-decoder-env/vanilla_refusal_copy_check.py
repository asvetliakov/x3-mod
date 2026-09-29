"""`manage.py launch --vanilla --direct` against a scratch copy of a real bottle's cxbottle.conf with the two GStreamer
entries uncommented (read-only on the bottle; nothing launched): --dry-run warns and exits 0, a real launch is refused
with exit 2 before the installer lock or any child (the --vanilla-with-bottle-env pass is covered by
test_voice_decoder_install on a synthetic game). Run from the repository root:
    python3 verification/results/voice-decoder-env/vanilla_refusal_copy_check.py [BOTTLE] [SCRATCH_DIR]
Result 2026-09-29 (bottle X3): results-vanilla-refusal.txt."""
import contextlib
import importlib.util
import io
import re
import sys
import tempfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
bottle = sys.argv[1] if len(sys.argv) > 1 else 'X3'
scratch = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(tempfile.mkdtemp())
real = Path.home() / 'Library/Application Support/CrossOver/Bottles' / bottle
(scratch / bottle).mkdir(parents=True, exist_ok=True)
text = (real / 'cxbottle.conf').read_text()
text = re.sub(r'^;\s*("GST_(PLUGIN_PATH|REGISTRY)_1_0")', r'\1', text, flags=re.M)  # the user's commented-out lines
(scratch / bottle / 'cxbottle.conf').write_text(text)
sys.path.insert(0, str(ROOT))
spec = importlib.util.spec_from_file_location('manage', ROOT / 'tools/manage.py')
manage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manage)
manage.CROSSOVER_BOTTLES = scratch
print('scratch status', manage.voice_bottle_env_status(scratch / bottle, real / 'drive_c/X3'))
for label, extra in (('dry-run', ['--dry-run']), ('launch', [])):
    error = io.StringIO()
    argv = ['manage.py', 'launch', '--bottle', bottle, '--vanilla', '--direct', *extra]
    with mock.patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(error), \
            mock.patch.object(manage, 'launch_teed', side_effect=AssertionError('must never launch')), \
            mock.patch.object(manage.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            mock.patch.object(manage.media_package, 'installer_lock', side_effect=AssertionError('lock reached')) \
            if not extra else contextlib.nullcontext():
        try:
            code = manage.main() or 0
        except SystemExit as exit_error:
            code = exit_error.code
    rows = [l for l in error.getvalue().splitlines() if 'bottle env' in l or l.startswith(('warning:', 'launch refused:'))]
    print(f'{label}: exit {code}')
    for row in rows:
        print('  ' + row)
