#!/bin/sh
# Build only. Parent owns execution under Wine/native Windows. No game invocation.
set -eu
if [ "$#" -ne 1 ]; then
    echo 'usage: build_media_package_config_fixture.sh NEW_OUTPUT_DIRECTORY' >&2
    exit 2
fi
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
output=$1
if [ -e "$output" ]; then
    echo 'output must be a fresh directory' >&2
    exit 2
fi
mkdir -p -- "$output"
compiler=${X3M_PACKAGE_FIXTURE_CXX:-i686-w64-mingw32-g++}
"$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -msse2 -mfpmath=sse \
    -mstackrealign -mincoming-stack-boundary=2 -municode -static \
    "$repo_dir/verification/probe/media_package_config_windows.cpp" \
    "$repo_dir/src/media/package_config.cpp" \
    -o "$output/package_config_windows.exe"
python3 "$script_dir/prepare_media_package_config_fixture.py" \
    --exe "$output/package_config_windows.exe" --output "$output/relocated"
