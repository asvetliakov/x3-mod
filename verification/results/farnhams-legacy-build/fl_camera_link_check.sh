#!/bin/sh
# FL camera provider link evidence (docs/architecture/farnhams-legacy-camera.md): which camera_state
# object each edition links, the fl.camera row count in each DLL, and the warning count of both builds.
# usage: fl_camera_link_check.sh [build-fl] [build-ap] [build-fl.log] [build-ap.log]   (run from the repo root)
FL=${1:-build-fl}
AP=${2:-build-ap}
FL_LOG=${3:-}
AP_LOG=${4:-}
echo "fl_objects: $(ls $FL/CMakeFiles/d3d9.dir/src/engine/fl/*.obj $FL/CMakeFiles/d3d9.dir/src/proxy/camera_state*.obj 2>/dev/null | xargs -n1 basename | tr '\n' ' ')"
echo "ap_objects: $(ls $AP/CMakeFiles/d3d9.dir/src/engine/fl/*.obj $AP/CMakeFiles/d3d9.dir/src/proxy/camera_state*.obj 2>/dev/null | xargs -n1 basename | tr '\n' ' ')"
echo "fl_rsp_camera_state: $(grep -o '[^ ]*camera_state[^ ]*' $FL/CMakeFiles/d3d9.dir/objects1.rsp | tr '\n' ' ')"
echo "fl_nm_camera_state_symbols: $(i686-w64-mingw32-nm -C $FL/d3d9.dll | grep -c 'x3m::camera_state::')"
echo "fl_nm_anchor_symbols: $(i686-w64-mingw32-nm -C $FL/d3d9.dll | grep -c 'anchor')"
echo "fl_strings_fl_camera: $(strings $FL/d3d9.dll | grep -c '^fl\.camera')"
echo "ap_strings_fl_camera: $(strings $AP/d3d9.dll | grep -c '^fl\.camera')"
[ -n "$FL_LOG" ] && echo "fl_warnings: $(grep -ci warning "$FL_LOG")"
[ -n "$AP_LOG" ] && echo "ap_warnings: $(grep -ci warning "$AP_LOG")"
sha256sum $FL/d3d9.dll $AP/d3d9.dll
