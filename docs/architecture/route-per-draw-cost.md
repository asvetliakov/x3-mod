# Route per-draw cost: the three large pieces and the envelope

Design note, 2026-09-18; nothing here is implemented. **(M)** measured, **(I)**
inferred from measured numbers or read code, **(A)** assumed. Code and numbers
are from the bench worktree `worktree-agent-aae4564ee5361c29e` (base
`3e43ce58`): [engine-frame-time.md](engine-frame-time.md) §2.2, the ledger
entry "2026-09-18 — routed-draw cost bench" in
[motion-output.md](../verification/motion-output.md) and
`verification/results/bottle-X3/route-bench-{before-attr,after2}.json`.

**Ratified 2026-09-19 (orchestrator)** with these conditions. Lever 1 stage A
with 2a is scheduled after the run43 candidate. Condition (2) of lever 1 is
not left as an assumption: a direct value-only call that returns any failing
HRESULT hands that result to the wrapper's `observe_result` on a cold path, so
a device-loss code is observed exactly as today and nothing rests on reading
wined3d (Windows parity by construction). Lever 3 follows lever 1 as fixtures
first, then one flight with a `mask != 15` counter and `lazy_flushes` on the
frame line; it does not become default before that flight. 2b stays closed
until `retention_scene_end` ordering is traced. Stage B stays closed.

## Budget

Run129 c2: 9.7 µs proxy-only per routed draw (M) × 830 = **8.05 ms** of a
32 ms busy frame. Bench medians after the trims (`after2`, 400 routed draws,
µs per DrawPrimitive, M): off 1.48, perdraw 8.75, lazy 6.74, perdraw-ownership
10.98, perdraw-depth 12.40, perdraw-cascades 13.12. Differences: route 7.27,
wrapper **+2.23**, lease **+1.42**, cascades + retention +0.72, lazy **−2.01**.
Run-to-run noise ±0.15 (M). One µs per routed draw is 0.83 ms per frame.

| Lever | µs/draw | × draws | ms/frame | Status |
| --- | --- | --- | --- | --- |
| 1. Direct native path for the route's value-only calls | 1.6-1.8 (I) of 2.23 (M) | 830 | 1.3-1.5 | open first |
| 3. Hook-free lazy RT (bindings held, masks never held) | ≤ 2.0 (M bench, one run) | 830 | ≤ 1.66, minus flushes | open second |
| 2a. Light lock view (no FNSAVE pair) for the audited draw path | 0.5 (I) | ~705 leased | 0.35 | rides with 1 |
| 2b. Lease borrows the retention store's references | 0.5-0.7 (I) | ~705 leased | 0.35-0.5 | open last, conditional |
| Sum | | | **3.4-4.0 of 8.05** | |

## 1. The ownership wrapper on the route's own calls — 2.23 µs (M)

**What is paid.** `native<Fn>(slot)` reads `native_[slot]`, the saved
originals of the hooked device; under `X3M_OWNERSHIP=1` that is the wrapper,
so every route call enters a `Device::` forwarder (M, code): an
`ApplicationAdmissionAbi` scope (null monitor by default: no FNSAVE), `unwrap`
for an interface input (`registry_mutex` + `application_nodes.find`), the
native call, `observe_result`. A routed draw makes ~21 changing calls, ~11
`GetRenderState` reads and two jitter writes (M, §2.2): ~34 entries, ≈ 65 ns
each (I). `off-ownership` equals `off` (1.41 vs 1.48, M): the wrapper costs
nothing on the draw itself.

**What the wrapper protects, per call class.**

- Interface outputs (`GetVertexDeclaration`, `GetRenderTarget`, `GetTexture`,
  every `Create*`): adoption gives the canonical identity the shadow, the
  candidate records and the retention store key on. **Must stay wrapped.**
- Interface inputs (`SetVertexShader`, `SetPixelShader`, `SetRenderTarget`):
  the route's variants and targets are created through `native_[]`, so they
  are wrappers, and the undo restores the application's own wrappers; the
  native device needs each one's `backend`. **Stay wrapped in stage A.**
- `AddRef`/`Release` on application objects, Reset, Present, state-block
  begin/end, `copy_auto_depth`: logical counts, loss retirement, the recording
  guard. **Must stay wrapped.**
- Value-only calls (`Set/GetRenderState`, `Set/GetSamplerState`,
  `SetVertexShaderConstantF`, `SetPixelShaderConstantF`,
  `GetStreamSourceFreq`): no interface crosses; the forwarder adds only the
  admission scope and `observe_result`. **Provably the proxy's own and
  bypassable**: ~28 of the ~34 entries (I).

**Recommended.** At attach, when ownership is on, take
`ownership::borrowed_native_device(wrapped)` (the documented renderer seam of
`d3d9_ownership.h`: valid while the caller holds a wrapper reference, must not
escape, bindings restored before returning, which is the route's existing
contract) and fill a second table `direct_[slot]` from that device's vtable for
the seven value-only slots; the route's value calls use `direct_` with the
native device pointer. Without ownership `direct_` aliases `native_`, so the
default path and native Windows are byte-for-byte the present code. The seam
is portable COM (a vtable call on a documented interface); no Wine export,
layout or hash is involved.

Conditions that keep the contract: (1) with a published `X3M_ADMISSION=1`
monitor `direct_` is not used, decided once at attach (the monitor is
immutable; the admission ledgers keep counting route calls as today); (2)
these seven calls document only `D3D_OK`/`D3DERR_INVALIDCALL`, loss is
reported by Present, TestCooperativeLevel, Reset and resource calls, which
stay wrapped (A; settle by reading the wined3d entry points, and assert in the
seam DLL that no direct call returns a loss code); (3) the table is dropped at
detach and before the final logical release; the native device survives Reset
(I); (4) the route never issues while a state block records (M for
`set_render_state`; A for `before_draw`, confirm in review).

**Saving.** Ceiling 2.23 µs × 830 = 1.85 ms; stage A ≈ 28/34 of it, 1.3-1.5 ms
(I). Added hot-path cost: none per draw. Native Windows: same code, smaller
gain, unverified. **Stage B (closed for now):** cached `backend` pointers for
the six interface-input binds would recover ≈ 0.3-0.4 µs = 0.25-0.3 ms (I) but
puts native object pointers inside the route; not worth the identity risk
until stage A is measured short.

**Acceptance.** Bench: `perdraw-ownership` within 0.6 µs of `perdraw` (from
2.23). Contract: `run_ownership.py` + `verify_ownership.py`,
`run_ownership_integration.py` (15 cases) + its verifier and fallback runner,
`run_ownership_admission.py`, `run_application_admission_abi.py` unchanged;
every `seam-ownership-*` case of `run_motion_output.py` with 0 field
differences against the pre-change DLL outside timings and hashes;
`run_state_hook_benchmark.py` PRESERVE rows intact; `check_no_x87.py` clean.
All under `X3M_FIXTURE_BOTTLE=X3` and `wine_lock.py`.

## 2. The depth-replay lease — 1.42 µs per leased draw (M; 1.9 before the trim)

**Paid per candidate** (`note_candidate_draw`, `note_depth_geometry`; M, code):
two `ownership::get_buffer_lock_view` calls (VB, IB), each a `CounterAbiState`
shell (FNSAVE+FRSTOR in, FRSTOR+LDMXCSR out) around `registry_mutex`,
`application_nodes.find` and a sidecar copy; one `GetRenderState(CULLMODE)`;
one `GetVertexDeclaration` through the wrapper (output adoption under the
mutex); `AddRef` on the VB and IB wrappers (mutex each). At the scene end,
three wrapper `Release` calls per lease: the bench times DrawPrimitive only,
so these ~2,100 releases per frame are **outside** the 1.42 µs and unmeasured
(I). Run129 leased 704 of 825 routed draws (M): ceiling 1.42 × 704 = 1.0 ms.

**2a. Light lock view (with lever 1).** An FNSAVE/FRSTOR pair is ≈ 250 ns
under FEX (M, [state-call-fast-path.md](state-call-fast-path.md) (c): 1004 ns
for four). The draw hooks run under `LightCallBoundary` and are audited
x87-free, so for that caller the shell protects nothing: x87 is untouched by
construction and the boundary restores MXCSR and LastError. Add an internal
entry without the shell, reachable only from the audited draw path, and let
`check_no_x87.py` walk `get_buffer_lock_view_core` (the audit is the build
gate and fails closed). Every other caller keeps the shelled entry. Saving
≈ 0.5 µs × ~705 = 0.35 ms (I); on native Windows tens of ns, same source.

**2b. Can the record carry the verdict across frames?** Partly. The retention
store (`shadow_retention_core.h`) keys a caster draw by node serial, `DrawKey`
(VB/IB/declaration allocation ids, range) and two `BufferStamp`s (wrapper
identity, allocation, generation, **revision**), holds its own references in
live mode, and `known_declaration()` already serves refused sightings. A
leased draw that matches a live record could borrow the store's references:
no `GetVertexDeclaration`, no AddRefs, no scene-end Releases. The two lock
views cannot be carried: they are the at-draw bookend and the revision is part
of the match; a cached verdict without them replays a buffer the game rewrote.
Saving 0.5-0.7 µs per matched draw plus the unmeasured releases, 0.35-0.5 ms (I).

**Risk; why 2b is last.** The retention header says every Release the store
owes happens at the scene end **before the replay** (I, comment not traced). A
flush or eviction there frees objects a borrowed lease still names, so 2b is
admissible only if records seen this frame are pinned until
`release_depth_leases()`. Live mode only; census and retention-off keep
today's lease. §2.2's refused "declaration reference dedupe" changed the
identity the store keys; 2b reuses the stored identity.

**Acceptance.** Bench `perdraw-depth` − `perdraw-ownership` ≤ 0.9 (2a), ≤ 0.4
under `perdraw-cascades` (2b). `seam-ownership-shadow-replay-on`, `-cascades`,
`-cascades-casters-20`, `-far-refused`, `seam-ownership-shadow-retention-live`
(9743 checks, `retained_compared 28`), `-census` and the pool cases: 0 field
differences. 2b adds a fixture frame that flushes the store (sun switch) while
leases are borrowed and reads the reference counts back through the wrapper.

## 3. Hook-free lazy RT — ≤ 2.0 µs per routed draw (M bench)

**Why today's lazy mode needs the light setter hooks.** Lazy holds RT1/RT2
**and** `COLORWRITEENABLE1/2 = 15` across consecutive routed draws, saving the
application's masks once (`lazy_write1_/2_`). While held, the device's masks
are the route's. So an application write of either mask must flush first
(`before_set_render_state`, the only use lazy makes of the `SetRenderState`
hook; M, code), a read must see the application's value (`get_render_state`),
and target reads its bindings (`get_rt`, `get_rt_data`). `capture.cpp` turns
this into `state_hooks reason=lazy_rt`, installing slot 57 and, through
`state_hooks()`, slot 69: +64/+57 ns on 49,598 calls ≈ 3.1 ms against ≈ 1.85
ms saved (M per call, I per frame). Everything else that can observe the
bindings already flushes through hooks kept in every configuration: unrouted
draws (`before_draw`), Clear, SetRenderTarget, SetDepthStencilSurface,
StretchRect, ColorFill, state blocks, EndScene, Present, Reset (20
`restore_bindings` sites, M).

**Recommended contract change: hold the bindings, never hold the masks.** The
per-draw route already reads both masks at every routed draw (part of the
< 0.1 µs eleven reads, M). Keep that read in lazy mode and branch:
- mask == 15 (the D3D9 default; A that the game leaves it): write nothing. The
  device holds the application's own value, so its writes and reads during the
  hold are simply correct and nothing needs observing.
- mask != 15: that draw takes the per-draw mask path (set 15, draw, restore in
  the undo) as `perdraw` does today; the RT bindings may stay held.

`lazy_rt` stops being a `state_hooks` reason, so neither light setter hook is
installed and `get_render_state` is not needed; `get_rt`/`get_rt_data` stay
(heavy hooks the game rarely calls, free per frame). The engine's memoizing
state manager (`FUN_004b5620`,
[constant-uploads.md](../reverse-engineering/constant-uploads.md)) is
disturbed less than by `perdraw`, which rewrites both masks around every draw.
Flush points are unchanged: the first call that can observe a target, an
unrouted draw, scene end.

**Saving.** 2.01 µs × 830 = 1.66 ms for one run per frame. A flush is two
unbinds ≈ 1.3 µs (0.64 per bind, M). No game session has run `rt_mode=lazy`
(no ledger hit), so runs per frame are **unknown**; with 825 of ~861 draws
routed (run129/132, M) at most ~36 breaks ≈ 0.05 ms. After lever 1 each
removed call is cheaper: expect ≈ 1.4 ms (I).

**Risk.** The restore contract now rests on the mask read being current at
each routed draw; with hooks off it is a native `GetRenderState`, so it is (M,
hybrid unhook). A main-target change flushes first (existing hook; MRT size
rule). Native Windows: documented calls only; a device that fails the `Get*`
capability check falls back to the hooked configuration and today's lazy
logic. Unverified on Windows.

**Acceptance (byte-identical set).** `production-lazy-on`, `seam-lazy-on`,
`seam-ownership-lazy-on`, `seam-burst-perdraw`, `seam-burst-lazy` and the six
`-shadow-off` twins: colour, motion, depth and state hashes equal to the
`perdraw` twins and the pre-change DLL; `set_rt` per frame stays 12 (lazy) and
20 (perdraw); the burst step "write `COLORWRITEENABLE1 = 7` while RT1 is held,
route a draw, read back 7, restore 15" passes **without** the
`get_render_state` hook; a new burst step starts a hold with the mask already
at 7. The `state_hooks` line reads `installed=0 reason=none` under lazy and
`run_state_hook_benchmark.py` shows both setters at the unhooked ~11 ns. Then
one user flight with `--motion-rt-mode lazy`: `lazy_flushes` and `set_rt` per
frame decide the default.

## 4. The hook envelope per routed draw

Default path (M, code): **one** `LightCallBoundary` on the draw hook (9.9 ns)
and one null admission scope. The route's own calls and `render_state` reads
go through `native_[]`, the saved originals: **no proxy envelope** on them.
`call_preserved` (FNSAVE/FRSTOR) runs only for a written log line or the
once-per-second summary: zero per draw. The only per-draw FNSAVE/FRSTOR are
the two `CounterAbiState` shells of a leased draw's lock views (lever 2a). The
`Get/SetLastError` pairs in the lease functions are redundant under the draw
hook's boundary but cost ns and keep those functions safe for other callers.
Nothing hoists per frame: AGENTS.md requires CPU/LastError transparency at
each boundary the game observes, and the draw hook already uses the light
one. With `X3M_ADMISSION=1` each wrapper entry adds two `AdmissionState`
images (~34 entries ≈ 17 µs per routed draw, I): never read route cost off an
admission session.

## Recommended order

1. Lever 1 stage A with 2a (internal, no restore-contract change,
   fixture-provable): ≈ 1.6-1.9 ms.
2. Lever 3 (restore-contract change; fixtures, then one flight): ≈ 1.4 ms.
3. Lever 2b only if the store's scene-end ordering admits a pin: ≈ 0.4 ms.
Each step re-runs the route bench (`--label`) and appends to the ledger.

## Stays closed

- Current lazy mode as default: −1.2 ms net (I; 3.1 ms hooks vs 1.85 saved).
- State-read caching: < 0.1 µs (9.07 vs 8.98, M) → < 0.08 ms.
- Jitter writes 0.18 µs → 0.15 ms (TAA needs them); cascades +0.25 and
  retention +0.26 µs → 0.2 ms each (feature work, not overhead).
- `--telemetry-draw` +1.5 µs: diagnostic only. Wrapper stage B ≈ 0.25-0.3 ms.
- Dropping the undo's VS/PS or mask restore: the engine's memoizing state
  manager would render the next pass with the variant.
- Per-frame declaration reference dedupe: changes retention identities (§2.2).
- Holding RT1/RT2 across unrouted draws: undefined RT1 contents (section 3).

## Unknowns and what settles them

- Does the game ever write `COLORWRITEENABLE1/2` (states 190/191): the
  `--frame-timing` state-write census or a mask != 15 counter on the frame
  line; affects lever 3's fallback rate, not correctness.
- Routed-run length per frame: `lazy_flushes` in the lever 3 flight.
- The seven value-only calls never return a loss code: read wined3d; Windows
  behavior unverified.
- Store release order against the replay (2b): read `retention_scene_end`.
- Scene-end cost of ~2,100 wrapper releases: in no bench; a QPC pair around
  `release_depth_leases()`.
