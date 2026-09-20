#!/bin/sh
# The analytic fixture source and accepted evidence remain historical.
# It cannot link against or qualify the replacement spatial FogPass.
cat >&2 <<'NOTICE'
The analytic fog fixture is historical and superseded. No build was started.
Use verification/probe/fog_spatial_build.py with --asset-root, --asset-data and
--output; then fog_spatial_run.py. See verification/probe/fog_spatial_fixture.md.
NOTICE
exit 2
