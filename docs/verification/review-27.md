# Review 27: loading trampolines, light hooks, resource reader (IN PROGRESS, paused)

Independent review of the loading branch (`a752167` "Loading: light no-SSE
hooks, probe batch 2 trampolines, gated fast resource reader") on top of main
`28a003d`, merged as `20f7ffd` "Merge main into loading branch (pre-review
27)". Scope: `engine_patch.{h,cpp}` (the trampoline arena and call-site
redirects), `loading_probes.{h,cpp}` (12 byte-verified entry patches with the
return-address hijack), `loading_trace_light.{h,cpp}` (the no-SSE hook rows
and the probe handlers), `resource_reader*.{h,cpp}` (gated fast/verify
resource reader and the catalogue handle pool), the `loading_trace`,
`capture`, `loader`, CMake and `manage.py` wiring, `run_resource_reader.py`
and its fixture, `check_no_x87.py`, the analysis scripts and their tests, and
the five docs. No game was launched; no Wine command has run yet in this
review.

**Status: paused on the orchestrator's request (account switch) after the
merge, the clean rebuild and the first read of `engine_patch`,
`loading_probes` and `loading_trace_light`. Items marked TODO are unreviewed
or unrun.**

## Merge notes

- `git merge main` conflicted only in five regenerated fixture result files
  (`verification/results/loading-mesh-fixture.txt`,
  `loading-trace-fixture.txt`, `loading-trace-mesh-summary.json`,
  `mesh-adjacency-cache-{on,off}-fixture.txt`); main's copies were taken. The
  suite chain (TODO) regenerates them.
- The branch commit `a752167` deleted `verification/probe/wine_lock.py`
  (79 lines) although the branch's own docs and `docs/status.md` still refer
  to it and AGENTS.md makes it mandatory. Restored from main (finding 1).
- Clean rebuild (`cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
  -DCMAKE_BUILD_TYPE=RelWithDebInfo; cmake --build build -j4`): 0 warnings,
  `build/d3d9.dll` SHA-256
  `8a08a0c798d911516dae0e239e8c9371d3754e24fed1b06eecd9b12e1dcd5d3f`
  (11,005,939 bytes; pre-fix hash, no source changed yet).

## Checklist

### 1. Trampolines (`engine_patch`) — partly reviewed

- Read: `src/proxy/engine_patch.{h,cpp}` in full, `loading_probes.cpp` in
  full. Mechanism: `claim()` reads `length` bytes, compares against the spec
  (fail closed with `bytes_mismatch`/`unreadable`/`invalid_spec`), emits
  tail (displaced bytes + `jmp back`), a 4-aligned `entry` word and a
  `jmp [entry]` dispatcher into an 8 KB `PAGE_EXECUTE_READ` arena (RWX only
  while an `Emitter` holds the SRW lock), then writes `jmp dispatcher` over
  the first five bytes under `VirtualProtect` and restores the old
  protection; flush/protect failure rolls the bytes back. `restore()`
  checks the five patched bytes are still ours before writing the originals
  back and leaves the arena callable. `push_front()` swaps the chain head
  with a single aligned 4-byte store; the entry stub writes its continuation
  before the head is swapped (ordered installation).
- TODO: verify the twelve `expected` byte strings against the installed
  `X3AP.exe` (read-only) and the Ghidra listing; confirm no relative branch,
  no EIP-relative instruction and a clean instruction boundary at `length`
  for every site (`name_resolve`/`crt_fgetc`/`texture_body`/`texture_loader`
  displace `push -1; push imm32` SEH prologues: fine; `find_wrapper`
  displaces `push ebx/ebp/esi/edi; mov ebp,0x138`: fine; the rest are
  `sub esp`/`push`/`mov` sequences — all need the byte check).
- TODO: no overlap with the scene hook at `0x004721b1` and the object-trace
  call-site hook at `0x004c5228` (none of the twelve sites is within 16
  bytes of either address by inspection; still to confirm ordered
  installation in `loader.cpp`/`capture.cpp`).
- Shadow-stack hijack (`x3m_probe_enter`/`x3m_probe_exit`): read. The
  entry stub frame is `pushfd; pushad` so `regs[9]` is the return-address
  slot; the exit stub reserves a slot (`sub esp,4`) so `&regs[10]` is ESP
  after the callee's `ret n` and the matching slot is `here-4-ret_pop`.
  Stale entries (frames unwound past by exception/longjmp) have lower slot
  addresses and are discarded from the top while `depth>1`; the popped
  entry's slot is compared and a mismatch counted as `desync`. Callee EAX/EDX
  and flags survive via `popad`/`popfd`. Per-thread `Shadow` (64 entries,
  1.5 KB) is `HeapAlloc`ed lazily under a CAS-guarded `TlsAlloc`. Overflow
  at depth 64 leaves the frame un-hijacked (counted, still called).
  Findings 2–5 below. TODO: the fixture's longjmp test, `ret n` mismatch
  behaviour on a real site, CPU-state contract on the ARM64/FEX bottle.

### 2. Light hooks TU — partly reviewed

- Read `loading_trace_light.{h,cpp}` in full. Integer-only: 64-bit
  counters through `lock cmpxchg8b` (`exchange64`/`load64`/`add64` are
  correct; `max64` is a documented racy read-then-exchange); `Span`
  accounting uses plain 64-bit integer add/sub/compare (GCC emits
  `add/adc`, no x87); `uint64_t(regs[6])*regs[7]` is an integer widening
  multiply. No logging, no double. `TlsAlloc` once in `initialize()`;
  TODO: `TlsFree` on unload (finding 4), `check_no_x87.py` run, analysis
  tests, the `frame_end elapsed_ms/dt_ms/qpc` addition, the rows list and
  `CpuCallBoundary` coverage.

### 3. Resource reader — TODO

### 4. Gates and defaults — TODO

### 5. Performance pass — TODO (per-call work in the light rows is two QPC
reads, two TLS calls, ~10 `lock cmpxchg8b`; probes add one QPC, one TLS
read and the shadow push/pop; `record_path` is bounded at 16 records)

### 6. Docs — TODO

## Findings and fixes

1. **Fixed (blocking).** `a752167` removed `verification/probe/wine_lock.py`;
   every runner invocation in AGENTS.md, `docs/status.md` and the branch's
   own docs requires it. Restored from main unchanged.
2. **Open (medium, design).** `engine_patch.cpp:65-68` `claim()` writes the
   five-byte `jmp` with `memcpy` over live code. Safe only while no other
   thread can execute the site; the probes install from DLL initialisation
   before the game's loading threads exist, and the resource reader chains
   through `push_front` (an atomic pointer store) rather than a second
   patch, so this holds today. Document the assumption in
   `docs/reverse-engineering/loading-probes.md` "Patch mechanics" and
   refuse `claim()` after the first frame (`capture` knows).
3. **Open (low).** `engine_patch.cpp:78-84`: when the post-write
   `VirtualProtect` rollback fails (`rollback_failed`) the site stays
   patched but `patched_in=false`, so `restore()` will not undo it. Set
   `patched_in=true` on that path so shutdown still tries.
4. **Open (low).** `loading_trace_light.cpp` `shadow()`: per-thread
   `Shadow` blocks are never freed and neither `shadow_slot` nor `span_slot`
   is `TlsFree`d on shutdown; with the DLL never unloaded in production this
   is a bounded leak (one 1.5 KB block per thread that hits a probe). Add a
   `light::shutdown()` that frees the slots after `loading_probes::shutdown()`.
5. **Open (low).** `x3m_probe_enter` `ResourceRead`/`ReadDispatch` and
   `x3m_probe_exit` `ResourceOpen` dereference `ctx+4` after `plausible()`
   (aligned, 64 KB..2 GB) with no SEH guard; a plausible but unmapped value
   in EAX/ESI would fault inside a game thread. The production callers pass
   heap file objects, so the risk is confined to foreign callers; note in
   the docs or guard with `IsBadReadPtr`-free `VirtualQuery` once per
   distinct pointer page.

## Suite results

TODO — no suite has run. The chain to run under `wine_lock.py`, one at a
time, after a clean rebuild: `run_motion_output.py`, `run_temporal_pass.py`,
`temporal_run.py` (rerun once on timeout), `run_ownership_integration.py`,
`run_scene_capture.py`, `run_loading_trace.py`, `run_gz_buffer.py`
(`X3M_FIXTURE_BOTTLE=X3`), `run_resource_reader.py` (X3),
`run_object_lifetime.py`, `run_object_trace.py`, `run_mesh_adjacency_cache.py`,
`run_mesh_cache_hook.py`, generator `--check`, `check_no_x87.py`,
`unittest discover` with `PYTHONPATH=verification/probe`.

## What `docs/status.md` must say (orchestrator)

TODO — review incomplete; do not merge into main or install from this branch
until the checklist above is closed.
