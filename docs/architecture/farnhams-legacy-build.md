# X3: Farnham's Legacy build edition

One source tree, two `d3d9.dll` builds chosen at configure time; no runtime edition detection, no adapters.

## Option and build directories

`X3M_EDITION` (cache string, default `ap`; other values stop configure) defines `X3M_EDITION_AP=1` or
`X3M_EDITION_FL=1` on the `d3d9` target; compiler flags are the same. Configure the README build with
`-B build-ap -DX3M_EDITION=ap` and `-B build-fl -DX3M_EDITION=fl`, then `cmake --build <dir> -j`.

## Source lists and the exclusion rule

The ap target links `X3M_AP_SOURCES`, the pre-switch literal list in its original order. The fl target
links `X3M_SHARED_SOURCES` (D3D-only, renderer, mixed files) and `X3M_AP_ENGINE_LINKED_SOURCES` (AP
engine modules FL links), both checked to be subsets of it; `X3M_AP_ENGINE_SOURCES` (ap only) is the
remainder. An engine module stays out of FL unless a retained file needs it at link time outside an
initializer (`motion_output`, its `*_inc.h`, a `capture.cpp` draw/compositor path) or a kept module
links it. Initializer, Present, Reset, report and shutdown calls in `capture.cpp`, `loader.cpp`,
`telemetry.cpp` are fenced `#if !X3M_EDITION_FL` (no define means AP, as in fixtures compiling
production sources); in FL `object_trace::executable_verified()` is `constexpr false`. An FL
engine provider replaces an AP module behind the same header: `src/proxy/camera_state.cpp` is ap only
and the fl target links `src/engine/fl/camera_state_fl.cpp` ([FL camera provider](farnhams-legacy-camera.md)).

Evidence in `verification/results/farnhams-legacy-build/`: `cmp_objs.py` and `cmp_dll.py` compare the AP
build with one of the pre-switch commit from the same source path and commit string
(`ap_object_compare.json`: 121 of 121 objects identical; `ap_dll_compare.json`: outside debug sections
only the link timestamps, CheckSum and debug section sizes differ), and `ap_address_scan.py` with
`ap_address_scan_{fl,ap}.json`. Of the 21 X3AP-range constants the scan finds statically reachable in
the FL DLL, three are real addresses, each behind state only AP initializers set
(`object_trace::active()`, engine_effects `identity_`, the fenced object_lifetime installer). The other
18 are shader tokens, allocation sizes and text bytes.

## FL identity contract (`src/engine/fl/executable_identity_fl.h`)

`x3m::fl::verified()`, `module_base()`, `mismatch_reason()`: the main module
(`GetModuleHandleW(nullptr)`) has i386 PE32 headers with X3FL.exe's link stamp 0x618416c8, SizeOfImage,
entry point RVA, file characteristics (LAA bit free), a SizeOfHeaders that holds the section table
(reason `headers`), the eight-section table, the entry anchor (`call rel32; jmp rel32` at the entry RVA,
no base relocation inside) and the file size on disk (2,804,736). Evaluated once, LastError preserved,
read with `ReadProcessMemory` on the own process. Constants are generated, never typed:
`tools/analysis/fl_identity_constants.py` writes `executable_identity_fl_inc.h` (`--check` compares).
`loader.cpp` logs one row after `initialize_log`: `fl.identity ok base=0x... timestamp=0x618416c8
image_size=0x00391000` or `fl.identity mismatch reason=<field>`.
`verification/analysis/test_fl_identity.py` mirrors the check in Python (X3FL.exe verifies, X3AP.exe
fails at `timestamp`); the C++ check has not run in a game yet.

## RVA rule

X3FL.exe is ASLR-relocatable (DllCharacteristics 0x8140, a `.reloc` section). Every FL address is an
RVA added to `fl::module_base()`, never 0x400000 based; the in-memory `ImageBase` is not compared. An
anchor or hook site must also be free of base relocations, or its bytes differ per load.

## CFG caveat for future indirect-call trampolines

X3FL.exe carries Control Flow Guard instrumentation (`.gfids`, load-config GuardFlags 0x100 =
CF_INSTRUMENTED, `__guard_check_icall_fptr` at preferred VA 0x550410) but does not set
IMAGE_DLLCHARACTERISTICS_GUARD_CF and has an empty guard function table, so CFG is not enforced
today. A hook at an indirect call site must treat `call [__guard_check_icall_fptr]` and the following
`call reg` as whole instructions. If an update enables CFG, a trampoline reached by an indirect call
must be a valid target: this DLL's code (a non-CFG module is all-valid), executable memory allocated
without `PAGE_TARGETS_INVALID`, or a target registered with `SetProcessValidCallTargets`.
