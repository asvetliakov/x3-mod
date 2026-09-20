#!/bin/sh
# Standalone actual-wrapper fixture; never rebuilds or loads the proxy DLL.
set -eu
cd "$(dirname "$0")"
fixture_dir=build/surface_lease
sh build_admission_dependencies.sh "$fixture_dir"
fixture_flags="-std=c++17 -O2 -Wall -Wextra -Werror -pthread -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2"
i686-w64-mingw32-g++ $fixture_flags -c ../../src/ownership/d3d9_ownership.cpp -o "$fixture_dir/ownership.o"
# Only the fixture's CPU witness TU suppresses EH so its own prologue/epilogue
# cannot mask the actual API's preservation behavior. Production retains EH.
i686-w64-mingw32-g++ $fixture_flags -fno-exceptions -c surface_lease_fixture.cpp -o "$fixture_dir/fixture.o"
i686-w64-mingw32-g++ $fixture_flags -static "$fixture_dir/fixture.o" "$fixture_dir/ownership.o" \
 "$fixture_dir/application_admission.o" "$fixture_dir/application_admission_abi.o" \
 ../../src/ownership/execution_state.cpp ../../src/ownership/finite_buffer_evidence.cpp ../../src/ownership/portable_managed_upload.cpp \
 -o "$fixture_dir/surface_lease_fixture.exe" -ldxguid -luser32 -ladvapi32
