# FL object identity

TAA's motion history is keyed on each draw's engine scope (`x3m::object_trace`, `src/proxy/object_trace.h`).
In FL it comes from `src/engine/fl/object_trace_fl.cpp` (ap keeps `src/proxy/object_trace.cpp`;
[build](farnhams-legacy-build.md)). Step 3a makes `active()` true; `object_lifetime` stays off, so
`history_available_` stays false. Step 3b (lifetime) is separate. Rows and evidence:
[ledger](../verification/farnhams-legacy.md), "Step 3a".

## Step 3a: scope hook

**Site** ([material-submit.md](../reverse-engineering/fl/material-submit.md) section 1): `call 0x4e0f30` at RVA
`0xe7142`, the target's only direct caller, then the caller's `add esp,0x10`. At entry ECX = descriptor,
EDX = node, `[esp+4..+16]` = camera, flags, light1, light2; plain RET. `tools/analysis/fl_object_trace_constants.py`
writes `object_trace_fl_inc.h` (`--check`): site `e8 e9 9d ff ff 83 c4 10`, entry `53 8b dc 83 ec 08`, engine
anchor at RVA `0xa2290` (disp32 the only relocated word, slot RVA `0x1aa100`, registry at +0xc), and every
field offset from a native operand: parent +0x18, handle +0x28, scale +0x70/+0x80, position +0xb0, basis
+0xc0 (rows 16 bytes apart), flags +0x12c/+0x130, **model +0x144, LOD +0x150** (AP +0x140/+0x14c).

**Gate** (`initialize()` in `load_backend`'s FL block, `X3M_OBJECT_TRACE` default on): `fl::verified()`, the
eight site bytes with rel32 = target − (site + 5), the six entry bytes, the engine anchor at the live base;
then the install window (closed at the first Present: `late_claim`), TLS, pin, `engine_patch::claim_call`
(one `lock cmpxchg8b`, rollback). One `fl.trace status=` row; later TLS failures stop observation while the
redirect keeps forwarding.

**Dispatcher** (AP's shape): copies ECX/EDX and the four words into a 40-byte scope whose first two words are
a real SEH record, publishes it in TLS (nesting by `parent`/`depth`), forwards ECX/EDX from the scope and the
words from the caller's frame, calls, `add esp,0x10`, keeps EAX/EDX/ECX, pops, unregisters SEH, RETs; the
caller then removes its own words. A foreign unwind clears only that scope. `current()` reads node[0..0x154),
the camera handle and the registry lazily inside the open call: the engine dereferences the same node for the
whole call, so the entry-time copy under a complete-pass borrow is not used. No matrix bits, no `alpha13c`.
CFG does not apply to a direct `call rel32` rewrite (`.gfids` empty, GuardFlags 0x100, not enforced).

**CPU state.** Built `-fno-exceptions -mno-sse -mno-mmx -mfpmath=387`; the dispatch path has no x87 and no XMM
instruction (`verification/results/fl-object-trace/hook_cpu_state_audit.py`). EFLAGS are dead on both sides
(gated `sub esp,8` / `add esp,0x10`). A Win32 call may change volatile XMM state (fixture row
`XMM_AFTER_VIRTUALQUERY`: natively XMM0-5 zero after one VirtualQuery), so first-sighting reads, the QPC
check and rows run inside an XMM0-7 save; log() also runs under `call_preserved`.

**Diagnostics.** Every `X3M_FL_TRACE_LOG` seconds (`fl_trace_log`, default 10, 0 off; QPC every 256 entries,
first entering thread only, no allocation) one `fl.trace_sample` row: entries, nesting, null and unreadable
counts, distinct nodes/cameras (256 per period, `256+` saturated), handle/LOD/model ranges over first
sightings, last node, registry and its change, `current()` calls, other-thread entries.

**Consumers that newly run under FL with `active()` true**

| Site | Path | Decision |
| --- | --- | --- |
| motion_output.cpp 1521/1555 | fade/overlay arm: `fade_identity()` keys the hysteresis by node | accepted (AP behaviour) |
| motion_output.cpp 9325 | `record_fade_refused` node/model/LOD | capture-only |
| capture.cpp 813 | `read_draw_input` scope fields | capture-only |
| capture.cpp 1053 | `object_context` rows; AP evidence block (0x608504/0x606f34) | capture-only; block fenced |
| capture.cpp 3549 | `object_bounds_mode` (`X3M_OBJECT_BOUNDS_LOG`/debug) | capture-only |
| loader.cpp 284 | ownership `track_buffer_writes` | fenced (off until 3b) |
| fade_region.cpp `scope()` | AP descriptor layout for the fade bound | fenced (full viewport) |
| motion_output.cpp 5683, engine effects, occlusion ledger | need history, `engine_hook_`, `executable_verified()` | still off |

**Evidence** (host): fixture `verification/probe/fl_object_trace.cpp` (328 checks, native via WSL interop);
hook ≈18 ns per entry, `current()` ≈65 ns (fixture, not in game).
