#!/bin/sh
# Build the fixture against the HEAD camera-gate hold program (the "before" resolve), then restore the new header.
set -eu
W=/Users/asvetl/x3-mod/.claude/worktrees/agent-acafed9a659b7bc99
H=$W/src/renderer/temporal_resolve_far_camera_hold_program_inc.h
cp "$H" /private/tmp/claude-501/hold_new.h
cp /private/tmp/claude-501/hold_old.h "$H"
cd "$W/verification/probe"
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -DX3M_QUAD_FVF_SWITCH -static temporal_pass_fixture.cpp ../../src/renderer/temporal_pass.cpp -o build/temporal_pass_fixture_before.exe -luser32 || true
cp /private/tmp/claude-501/hold_new.h "$H"
cmp "$H" /private/tmp/claude-501/hold_new.h && echo restored
