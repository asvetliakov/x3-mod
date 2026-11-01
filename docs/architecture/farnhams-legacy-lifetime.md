# FL registry lifetime (step 3b)

`src/engine/fl/object_lifetime_fl.cpp` is FL's `x3m::object_lifetime` ([build](farnhams-legacy-build.md)); with the
[step-3a hook](farnhams-legacy-object-identity.md) `history_available_` is true and the TAA motion history runs.
Facts: [FL object lifetimes](../reverse-engineering/fl/object-lifetimes.md); AP: [object lifetimes](../reverse-engineering/object-lifetimes.md).

## Sites (`tools/analysis/fl_object_lifetime_constants.py` → `object_lifetime_fl_inc.h`, `--check`)

| Event | RVA | Bytes | ABI (all plain `ret`, caller cleanup) | Patch |
| --- | --- | --- | --- | --- |
| Insert | `0x10cf30` | `53 8b da 57 8b f9` | ECX map, EDX key, `[esp+4]` value; 42 direct callers | `engine_patch::claim` (atomic) |
| Remove | `0x10d040` | `56 57 8b f9 8b f2` | ECX map, EDX key; whole entry; 37 callers | `claim` (atomic) |
| Destroy | `0x10d120` | `53 8b d9 83 3b 00` | ECX map; next instruction `je` reads the replayed CMP's ZF; 33 callers | `claim` (atomic) |
| Load | `0x7984c` | `e8 bf 9c 01 00` → `0x93510` | ECX stream, no stack word; the target's only caller | `claim_call` (plain: crosses a qword) |

Capstone per entry: six bytes of whole instructions, no relative/absolute operand, relocation or branch into the
span, every `ret` a bare `c3`; insert reads one stack word, the others none (load target: `load_target_stack_scan.py`). Anchors tie slot `0x1aa100` (+0xc registry, +0x28 handle) to the helpers (camera removal `0xa489b`, teardown
`0x8aba0`, birth `0xa2290`, allocate-key→insert `0x10d01e`), each relocated only at its engine disp32.

## Model

AP semantics: retire before insert/remove forward; publish a serial only after insert returns with the key
mapped to the value; destroy clears, marks the registry dead, raises `engine_memory::begin_shutdown`; load and a
foreign unwind clear and bump `load_epoch` before forwarding; `current()` re-checks node and camera membership per
draw; 262144 slots, overflow disables. FL: ECX/EDX and one stack word; an insert finishing while another registry
mutation is in flight stays unpublished (`unpublished_nested`: no read of a header a rehash may rewrite); the
journal refuses registration (shadow retention off, `journal`). **Load survivors:** FL identities, the view camera
among them, survive a load ([registry binding](../reverse-engineering/fl/registry-binding.md)), so after a normal
load return the install walk runs again under the lock with no mutation in flight (hooked writers wait in
enter()) and adopts every key not already live with a fresh serial (`rebaselined`); keys born during the load
keep their insert serials. 20,002 entries: 1.4 ms (fixture, measured).

**Install before the engine** (run 3: `registry=0`): as in AP the snapshot finds nothing (`baseline=no_engine`),
status `active_without_baseline`, `active()` true (four patches live, observation on, empty table), which device
creation latches. Map calls before the engine global exists are `dormant`; the first after binds the registry.

## CPU state, CFG, cost

The dispatcher (AP's shape) saves EFLAGS, all GPRs and an FXSAVE image, registers a real SEH record, forwards
the input state and keeps the target's output GPRs, EFLAGS and FX state; enter/leave keep LastError. Built
`-fno-exceptions -mno-sse -mno-mmx -mfpmath=387`; `verification/results/fl-object-lifetime/hook_cpu_state_audit.py`
finds no x87 or XMM operand outside the two FXSAVE/FXRSTOR envelopes. The game reaches the arena by direct
`jmp`/`call rel32`: CFG does not apply. Cost: `fixture.json` TIMING (measured; host noise ±50 %).

## Rows for the flight

`fl.lifetime status=… baseline=… write=atomic,atomic,atomic,plain failed_site=none` once, then every
`X3M_FL_LIFETIME_LOG` s (`fl_lifetime_log`, default 10, 0 off; written at Present) `fl.lifetime_sample` with the
period's helper counts, publications, re-baselines, occupancy, `current()` known/unknown and `top_reason`,
registry, binds, `rebind_without_destroy`, epochs. TAA: `motion_output_device … history_available=1`,
`motion_output_frame` `routed`/`matched`/`history_current`, `sun_shadow_lane_refusals history=`; expected values
in the [ledger](../verification/farnhams-legacy.md), "Step 3b".

## Open

Factory constructor-unwind (same note): its cleanup frees
an engine before the global is published, so no bound registry is involved. Any engine freed without a registry
destroy shows as `rebind_without_destroy` ≥ 1 when the next registry binds, or `object_lifetime_disabled
reason=registry_unavailable`. Bind seam `0x8261a`, factory scope `0x77692`: not implemented. Ownership write
tracking follows `trace && lifetime`; the fade-region bound stays fenced.
