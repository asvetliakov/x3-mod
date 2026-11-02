#!/bin/sh
# Own sun step 2 (docs/architecture/own-sun-pass.md section 14): build the hook fixture
# (verification/probe/build_own_sun_hook.py, production flags, thunk shape and float audit) and run it natively on the
# Windows host through WSL interop (no Wine, no game). Output: hook_native.txt (OWN SUN HOOK checks=40 failures=0 since
# the lens bracket's deletion on 2026-11-02; 46 before, with the lens-flare gain fixture's gain_native.txt beside it).
# The Wine form is `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_own_sun_hook.py`.
set -e
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
(cd "$ROOT/verification/probe" && python3 build_own_sun_hook.py > /dev/null)
cd "$ROOT/build/verification/own-sun-hook"
./own_sun_hook_fixture.exe > "$HERE/hook_native.txt"
tail -1 "$HERE/hook_native.txt"
