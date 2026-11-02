#!/bin/sh
# Own sun step 2 (docs/architecture/own-sun-pass.md section 14): build the hook fixture and the lens-flare gain fixture
# (verification/probe/build_own_sun_hook.py, production flags, thunk shape and float audit) and run both natively on the
# Windows host through WSL interop (no Wine, no game; the gain fixture opens one 128x128 window with a HAL device).
# Outputs: hook_native.txt (OWN SUN HOOK checks=46 failures=0), gain_native.txt (RESULT PASS checks=8 failures=0).
# The Wine form is `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_own_sun_hook.py`.
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
(cd "$ROOT/verification/probe" && python3 build_own_sun_hook.py > /dev/null)
cd "$ROOT/build/verification/own-sun-hook"
./own_sun_hook_fixture.exe > "$HERE/hook_native.txt"
./lens_flare_gain_fixture.exe > "$HERE/gain_native.txt"
tail -1 "$HERE/hook_native.txt"
tail -1 "$HERE/gain_native.txt"
