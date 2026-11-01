# FL scene-graph diagnostic census

2026-10-30, bounded offline mapping of the existing AP `scene_graph_census`
diagnostic. These are approximate, refusable telemetry inputs, never gameplay
or retained-object authority. No production edit, native execution, Wine, game
launch, installation or commit was performed. Addresses are preferred VAs at
`0x400000`; FL runtime addresses use the recognized loaded base plus RVA.

## Evidence

```sh
python3 verification/results/fl-scene-graph-census/verify.py
```

The [producer](../../../verification/results/fl-scene-graph-census/verify.py) and
[result](../../../verification/results/fl-scene-graph-census/verification.json)
measure **55 operand assertions, 13 region SHA-256 hashes, 457 instructions,
26 independently checked region edges, four HIGHLOW operands, six rejected
changed-displacement controls**, and two synthetic address maps. The negative
controls decode changed operands without relying on image/region hashes. The
producer reuses the project PE32/Capstone tools and boundary-origin check.
Clean input `/mnt/c/X3FL-orig/X3FL.exe`: 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
These hashes are offline provenance, not a new runtime identity policy.

Read-only Ghidra `FLRegistry/X3FL.exe` corroborates ownership of the constructor
`0x4825c0`, task allocator `0x4be850`, cut allocator `0x4af300`, serialized
node reader `0x492ae0`, and VM factory `0x4c7db0`. Local context output remains
`/tmp/fl-census-context.local.txt`; no raw game bytes or decompiler output is
tracked. No broad frame-phase mapping was needed.

## Complete finite field profile

Let R be the manager and VM the script VM. All ordinary field offsets read by
`read_counts`, `walk_unattached` and `body_name` match AP except node model ID.

| Census input | AP | FL and operand evidence |
| --- | --- | --- |
| Manager global | `0x608518` | `0x5aa100`, RVA **`0x1aa100`**; `0x4a2290` loads it, `0x4a2298` selects map `+0xc`. Reuse existing body-metadata/lifetime profile root. |
| VM global | `0x6085e4` | `0x5aa174`, RVA **`0x1aa174`**; factory result published at `0x477561`. Already a relocation-aware recognition anchor in `engine_profile_fl.h`. |
| Nodes/scenes/cuts | R `+0xc/+0x10/+0x84` | Same. Constructor stores maps at `0x48261a/2639/273f`; scene allocator `0x4a4ab4/b7` and cut allocator `0x4af389/38f` select the corresponding maps. |
| Tasks | VM `+0` | Same. `0x4be8a2/a6` passes `[VM]` to auto-ID insert, stores task ID at task `+8`; `0x4bea55/58/5a` removes that same ID from `[VM]`. |
| Hash table header | 16 B; buckets `+0`, capacity `+4`, auto-ID `+8`, count `+0xc` | Same; reuse [FL lifetime map contract](object-lifetimes.md). Count/bucket indices remain 3/1. |
| Unattached list | R `+0x28` head, `+0x2c` zero, `+0x30` tail predecessor | Same. Constructor `0x48266c..2681` initializes these exact sentinel slots. Allocation appends at tail `0x4a23ac..23be`. |
| Node links/model | Next `+0`, previous `+4`, model `+0x140` | Links unchanged; model **`+0x144`**. `0x4a22cc` initializes model=-1; `0x4a23a2` instead initializes FL `+0x140` to zero. Reuse [FL cull-chain model evidence](cull-chain.md). |
| Body header | R `+0xb4/+0xb8/+0xbc` fixed/dynamic/slots | Same contiguous three DWORDs; see [body-name lifetime](body-name-lifetime.md). Initialized fixed count remains 11000. |
| Body record | Stride `0x1c`, name pointer `+0xc` | Same. Getter `0x486803..6812`; ID mapping branches `0x4867c7..67e9` agree with existing `body_slot`. Numeric null-name default remains `v\%05d`. |

The table count cap (10 million), dynamic body count cap (one million), walk
bound (300000), histogram size/probe budget (1024/32), top count (8), name scan
limits, overflow checks and refusal behavior are observer policy, not FL engine
limits. Reuse them. The walk remains newest-first over previous links, terminating
on R+0x28 whose previous slot is the zero at R+0x2c; no list/tree reinterpretation
or extra per-node edition branch is needed.

## Selection, reads and minimal implementation delta

`capture::initialize_log` already calls `engine_profile::bootstrap()` before
`scene_graph_census::initialize`; FL's post-cookie startup owner can bootstrap
earlier still. Select the edition/profile once during census initialization:
recognized FL uses `loaded_base+0x1aa100`, `loaded_base+0x1aa174`, and model offset
`0x144`; preserve AP's fixed layout. The current census has no independent
AP executable gate; edition admission must be explicit in the port.
Unknown/refused or mismatched images must not fall through to AP addresses.
No new executable fingerprint or per-node profile recognition is needed.

The smallest shared-core delta is a supplied model offset in `walk_unattached`
(default AP `0x140`), plus selected manager/VM addresses already accepted by
`read_counts`. Body-name code needs no edition-specific offset. A small profile
object may carry all three values; report uses the preselected fields.

Image recognition grants address interpretation, not an object lifetime lease.
The existing `engine_memory::read` validates regions and then directly copies;
that does not make a free between validation/copy safe. Factory publishes R
before body initialization; engine/body free can leave stale pointers/counts.
`fl_owned_binding` supplies owned metadata but explicitly no native borrow;
`fl_render_admission::current` exists only inside the qualified Render scope,
which the Present report cannot assume. These are established constraints,
not a request for new gameplay admission or a new observer framework.

For this diagnostic only, the parent proposes an FL reader using documented
`ReadProcessMemory(GetCurrentProcess(),...)`, requiring success and exact bytes
transferred, preserving LastError. That removes the unsafe direct-copy failure
mode without claiming snapshot consistency: decommit refuses, concurrent reuse
can still produce approximate values. Keep bounds/cycle/refusal flags and do not
retain native pointers between reports. All core reads, including name chunks
and globals, must use that reader. This is a design proposal, not executed
native evidence; performance must be measured before declaring the port complete.

Retain the 300-frame perf/debug gate, fixed storage, stride 1, and 1800-us budget
checked every 1024 walked nodes. The existing budget covers the walk and is
checked in chunks; top-name reads occur afterward, so it is not a strict whole-row
deadline. Kernel-copy overhead can reduce visited nodes and cause overshoot;
measure elapsed/read counts and preserve honest `capped` reporting.

## Insert attribution and hook boundary

Current FL `x3m_lifetime_fl_enter` does not fill the census caller table. A layout
port must preserve that honest unavailable/zero state; do not apply AP rules to
FL stack frames. No caller-attribution hook change is required for the initial
diagnostic field port. A future bounded attribution change can use raw FL return
sites with caller=0, or separately qualify the following derived frame rules:

| Classification | FL preferred return site | Stack evidence / caller read |
| --- | --- | --- |
| Auto-ID helper's insert | `0x50d026` | Four saved registers EBX/EBP/ESI/EDI plus one pushed value; enclosing frame is insert entry ESP+24. |
| Ordinary node allocator | `0x4a22a0` | Saves ECX/ESI/EDI, no auto-ID stack args: enclosing caller at auto-ID entry frame+16. |
| Related node allocator | `0x4a3e3e` | Saves ESI, register arguments: frame+8. |
| Camera allocator | `0x4a45fe` | Saves ESI, register arguments: frame+8. |
| Serialized node reader | `0x4934aa` | Stable EBP frame established at `0x492ae0/ae1`: caller at EBP+4. |

Runtime sites require rebasing. These are static normal-path frame derivations,
not executed nested/unwind transport acceptance. Reuse the existing FL whole-call
insert hook, lock, state/LastError preservation and per-invocation stack; its
input is ECX map/EDX key/one stack value, with caller cleanup. Do not add a seam
or change its liveness/reentrancy behavior as part of this field mapping.

## Meaningful acceptance for the implementing owner

Extend the existing host census harness with an FL synthetic address space at
two alternate loaded bases. Put contradictory sentinels at AP globals and node
`+0x140`, proving that counts, newest-first histogram and body names use only FL
roots and `+0x144`. Exercise null/unreadable globals/header/link/model/name,
foreign head, cycle, bound/time cap and dynamic-ID mapping. Unknown/refused
edition must invoke no reader; keep AP cases unchanged. Run the affected host
module and logging gate checks, not a full suite.

For the proposed native reader, a focused fixture should decommit/protect the
source between reads, refuse short/failed copies, preserve LastError, avoid any
AP address access, and assert no crash or retained name pointer. Measure dense
walk elapsed/nodes/syscall count under the same cap; diagnostic cost is not FPS.
Cross-compilation proves Windows-compatible source only. Native Windows behavior
and actual FL flight values remain unverified until separately executed/reported.

## Initial diagnostic port, 2026-10-30

The production census now selects the bootstrap-recognized edition at initialization.
Unrecognized/refused images leave the row disabled; AP keeps its fixed globals,
`+0x140` model and validated direct reader. FL selects the loaded-base RVAs above
and `+0x144` once, then uses `ReadProcessMemory(GetCurrentProcess(), ...)` for
all globals, headers, links, sampled models and copied names. API failure or a
short copy refuses that field. The enclosing row preserves LastError. The reader
retains no object/name pointer, takes no engine lease and calls no engine API;
readable freed storage may still yield stale telemetry, and concurrent mutation
may mix snapshots. This is approximate diagnostic data only. FL attribution is
explicitly unavailable/zero; lifecycle hooks and AP frame rules are unchanged.
The existing fixture bypass requires its compile define plus explicit synthetic
globals and does not affect production admission.

The fixed histogram, cycle/count/walk guards, one read per previous link plus
one sampled model read, and 1024-node QPC cadence are preserved. Selection is
outside the walk: no per-node recognition, allocation or locking was added.
The 1800 us threshold covers counts and walk progress, and is observed only at
1024-node checks; body names and formatting follow outside it. `walk_us` still
measures the entire row. A deterministic mock charging 1 us per RPM read yields
1024 walked nodes, 2065 total RPM calls and 2067 synthetic us, at both alternate
FL bases. This model quantifies granularity only. Subsequent native execution
passes 85 FL and 44 AP checks, including real RPM refusals; dense FL rows measure
2120–2360 us with the existing chunked threshold. Live game telemetry remains open.
See the [port ledger](../../verification/farnhams-legacy-scene-graph-census.md).
