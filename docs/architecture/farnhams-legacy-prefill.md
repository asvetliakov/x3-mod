# FL fog transit prefill (step 8)

AP's transit prefill ([fog hand-over, "R3 implementation"](fog-handover.md)) on X3FL.exe: during a transit stall the
existing `CreateTexture`/`CreateVertexBuffer` wrappers walk the engine's object list for the destination sector and
start its far fill, so the medium is warm on arrival instead of showing the vanilla cards during a cold fill (FL
run 13: The Hole transit frame 2,667 ms, then `coldfill=1 frames=45 fill_ms=471`, no `volumetric_fog_prefill` row).
Hook-free and read-only. The shared walk [`fog_prefill.h`](../../src/proxy/fog_prefill.h) takes a layout type;
[`fog_prefill_fl.cpp`](../../src/engine/fl/fog_prefill_fl.cpp) supplies X3FL.exe's. Layout proof:
[FL fog prefill §7](../reverse-engineering/fl/fog-prefill.md#7-verified-constants-2026-11-01). Host evidence:
[ledger](../verification/farnhams-legacy.md), "Step 8".

**What is read.** At most once per 250 ms after 250 ms without Present, on the Present thread: `[guard slot]`
(RVA `0x22de10`), `M = [manager slot]` (`0x1a82ec`), the tailpred `M+0x10`, then backward from the tail at most 8
nodes (`+0x48` class/subtype dword, `+4` prev) to the first class 1/subtype 0 node; its marker `+0x9c == 0xcafe`,
one 0x20-byte read at `+0x130` (scene, index = dword 3), id `+8`; then `[count slot]` (`0x1a8338`), `[table slot]`
(`0x1a83d0`) and sector_background_fl's record recipe (stride `0xdb8`, `+0x44`, name copied to its NUL). At most 56
bounded reads through `engine_memory::read`; nothing retained but the walk's value result and row counters.

**Differences from AP.** Every slot is `module_base() + RVA` (admitted once per process: `fl::verified()`,
`sector_background_fl::admitted()` — it anchors the count slot — and all 18 anchors). The guard read is new: the
id-map insert between the sector's AddTail and its `+0x130` zeroing allocates through the guarded allocator, so a
resource callback inside allocation recovery could see a linked, typed, alive sector with uninitialised scene and
index; a nonzero guard refuses the poll (`walk=guarded`). The AP instantiation has no guard read (AP objects
identical). The name is copied byte-wise (FL names are strlen+1 allocations).

**Expected rows (first flight, inferred).** Once at the first stall: `fl.prefill status=ready … guard_slot=<base+
0x22de10> anchors=18 failed=none log_s=10`, and `fl.prefill_sample row=1 polls=1 … walk=<status>`. In the
transit frame: `volumetric_fog_prefill … event=poll walk=found steps=<1..8> … family="foggreenoutlands…" stall_ms=…
action=started`, later polls `already_started`; earlier polls in the same stall may say `same_id` or `no_scene`.
On arrival: `volumetric_fog_prefill … event=confirmed lead_ms=… same_id=1 … adopted=1`, then one
`volumetric_fog_handover … coldfill=1 …` row whose `frames=` is a few frames (warm) instead of run 13's 45.

**What a flight must show.** Gate into a fogged sector (The Hole): the vanilla fog cards are not visible on
arrival, the fog medium is there from the first frames. Wrong: `fl.prefill status=anchor_mismatch` or
`sector_refused`; `walk=bad_*`/`malformed` in a transit; `event=discarded`; `walk=found` with `lead_ms` below
~0.5 s and cards still visible (the found poll came too late: the window share is unmeasured on FL).
Cost (fixture, measured, not game FPS): 3.9 µs per found poll with a 21-character name, 0.4 µs refused at the guard.
