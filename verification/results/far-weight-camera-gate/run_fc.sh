#!/bin/sh
# Run the far-camera fixture mode on the before and after executables (one Wine command at a time, under the lock).
W=/Users/asvetl/x3-mod/.claude/worktrees/agent-acafed9a659b7bc99
cd "$W"
for which in ${WHICH:-before after}; do
  exe=temporal_pass_fixture.exe; [ "$which" = before ] && exe=temporal_pass_fixture_before.exe
  X3M_FIXTURE_BOTTLE=X3 WINEDLLOVERRIDES=d3d9=b python3 verification/probe/wine_lock.py "/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine" --bottle X3 --no-update --dll d3d9=b --workdir "$W/verification/probe/build" "$W/verification/probe/build/$exe" 'C:\X3\d3dx9_37.dll' "Z:$W/src/temporal/depth_decode.hlsl" "Z:$W/src/temporal/resolve.hlsl" "Z:$W/src/temporal/taa_sharpen_ps.hlsl" far-camera > /private/tmp/claude-501/fc3_$which.txt 2>/dev/null
  echo "$which exit=$?"
done
for which in ${WHICH:-before after}; do
  echo "== $which"
  grep -a "FAR_CAMERA_PAN \|FAIL\|RESULT" /private/tmp/claude-501/fc3_$which.txt | sed 's/FAR_CAMERA_PAN //;s/yaw_px=[0-9.]* move_px=[0-9.]* comove=[01] //' | cut -c1-250
done
