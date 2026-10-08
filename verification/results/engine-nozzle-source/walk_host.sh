#!/bin/sh
# The node walk's host cost (docs/architecture/engine-nozzle-source.md "Implementation"; the ledger figure in
# docs/verification/engine-effects.md): builds the harness of test_engine_nozzle_walk.py and keeps its JSON beside this
# script (walk_host_out.json). Host arm64, clang++ -O2; the reader is opaque to the compiler.
set -eu
cd "$(dirname "$0")/../../.."
out=$(mktemp -d)
clang++ -std=c++17 -O2 -Wall -Wextra -Werror verification/probe/engine_nozzle_walk_host.cpp -o "$out/host"
"$out/host" > verification/results/engine-nozzle-source/walk_host_out.json
python3 -c "import json; d = json.load(open('verification/results/engine-nozzle-source/walk_host_out.json')); print(json.dumps(d['cost'])); print('checks', d['checks'], 'failures', d['failures'])"
# The same walk through the production reader (engine_memory.cpp against the mock <windows.h> of
# test_engine_memory_shutdown.py): walk_memory_host_out.json (review S1).
PYTHONPATH=verification/analysis:verification/probe python3 -c "
import sys, pathlib
import test_engine_memory_shutdown as mem
from source_text import source_text
d = pathlib.Path(sys.argv[1]); (d / 'windows.h').write_text(mem.MOCK_WINDOWS)
(d / 'engine_memory_under_test_inc.h').write_text(source_text(pathlib.Path('src/proxy/engine_memory.cpp')).replace(mem.COPY, 'host_copy(out, in, size);'))
" "$out"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror -I "$out" -I src/proxy verification/probe/engine_nozzle_walk_memory_host.cpp -o "$out/memory_host"
"$out/memory_host" | tee verification/results/engine-nozzle-source/walk_memory_host_out.json
rm -rf "$out"
