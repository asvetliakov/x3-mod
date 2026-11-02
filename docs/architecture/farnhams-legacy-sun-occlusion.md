# FL sun occlusion hooks (step 9)

> **Superseded (2026-11-02, own sun steps 2 and 3).** The partial-occlusion override this note describes is deleted
> (its files, `core::decide`, the deferral, the `override=1` rows), and the hook no longer always claims both calls.
> Current design and as-built state: [own-sun-pass.md](own-sun-pass.md) section 8 ("FL differences") and section 14
> ("Step 3 as built: the FL hidden answer"); hook sites, ABI and main-view facts:
> [FL sun occlusion RE note](../reverse-engineering/fl/sun-occlusion.md). The text below is kept as history.

AP's flare probe override and lens bracket ([sun partial occlusion](sun-partial-occlusion.md), AP contract
[lens-flare visibility](../reverse-engineering/lens-flare-visibility.md)) on X3FL.exe:
[`sun_occlusion_fl.cpp`](../../src/engine/fl/sun_occlusion_fl.cpp) provides `sun_occlusion.h` for the fl target (the ap
target keeps `sun_occlusion.cpp`); the consumer (`motion_output_sun_occlusion_inc.h`), `core::decide`, `Ready`, the
`Deferral` and the visibility pass are shared. Sites and ABI:
[FL sun occlusion, "Verified hook sites and ABI"](../reverse-engineering/fl/sun-occlusion.md#verified-hook-sites-and-abi-2026-11-01).
Host evidence: [ledger](../verification/farnhams-legacy.md), "Step 9".

**The two seams.** The flare probe `call` at base+`0x8af0a` (target `0xa3fd0`, its only caller) answers 0 / 1 for the
frame's single eligible sun record or forwards; the walk's own `test eax,eax` stores record+0x30 = 0 on 1. The lens
`call` at base+`0x8bcb7` (target `0x97680`, one of three callers: only this site) runs every selected lens body of every
record; the thunk opens the bracket (capture's `sun_lens_begin` → `MotionOutput::sun_occlusion_begin`, the visibility
pass) around it. No record pointer reaches the lens call: attribution is the probe latch, as on AP.

**ABI differences from AP.** AP passes (record, view) and (view) as cdecl stack words; FL passes registers. Probe:
ECX = record, EDX = view, no stack argument, bare RET. The thunk saves EFLAGS, ECX and EDX, calls the handler with
them as cdecl words, answers with `popfd; ret` (ECX/EDX as at the call) or forwards with `popfd; jmp [target]`, ESP at
the return address. The log mode calls the original as `fastcall`. Lens: EDX = view, ECX a padding word the target
pushes unread; the thunk restores both and EFLAGS before `call [target]`, and keeps the original's EAX/ECX/EDX and
EFLAGS across `end` (dead at the site; kept anyway). The probe handler saves XMM0-7 (the mutation run shows the
handler's Win32 calls change them) and LastError; begin / end save XMM0-7 and the x87/MXCSR/LastError image around
the SSE listener. Built `-fno-exceptions -mno-sse -mno-mmx -mfpmath=387`.

**Gate 1 reads +0xec.** `core::decide` replicates the probe's three early gates after `eligible`; FL's flags word is
`[config slot 0x1a83b0] + 0xec` (AP `+0xfc`); the record and view offsets are AP's (static_asserts against the core).

**Main view.** The active cockpit's camera (cockpit +0x58 with the sector-camera bit 0x10000), the AP registry walk
over X3FL.exe's registry slot `0x1a82e4`, used only when `sector_background_fl` admits its anchors
(`main_view=registry`); otherwise the override never answers and the bracket stays (`main_view=none`).

**The non-atomic lens claim.** `0x8bcb7 % 8 = 7`: the five bytes straddle a qword, so `claim_call` copies them
plainly (`lens_write=plain`); both claims run only inside the install window (capture's `initialize_log`, before the
engine renders), the lens claim first, undone if the probe claim fails. After the first Present every claim is
refused (`late_claim`, nothing written).

**Fog-transmittance lens gain (both editions).** With the volumetric fog configured the bracket is installed (also
without `X3M_SUN_OCCLUSION`, observe-only), and each bracket applies `G × T_sun` to the lens draws, `T_sun` the
luminance of the fog law's transmittance at the look's column cap (`renderer::fog_sky_lens_gain`); `T = 1` in a frame
without the density composite.

**Accepted interim limitations (2026-11-02).** `T_sun` is the mean-density column of the look to its sky cap, not
sampled along the sun's ray nor from the march's own density, so a sun seen through a gap in the cloud is dimmed like
one behind it. The gain dims every ONE/ONE/ADD draw in the lens bracket, ship and station flares included, on both
editions whenever the fog composite applied in that frame. Both go away when the engine's sun body is replaced by the
mod's own sun (`docs/architecture/own-sun-pass.md`, in progress, not yet on main).

**Expected rows (first flight, inferred).** Once: `fl.sun_occlusion status=active base=0x… probe=base+0x8af0a
lens=base+0x8bcb7 probe_write=atomic lens_write=plain override=1 log=0 bracket=1 main_view=registry anchors=17
failed_anchor=none log_s=10`, `lens_flare_gain value=1.0000 configured=0 reason=unset bracket=active fog=1`,
`sun_occlusion_config override=1 …`. Every 10 s: `fl.sun_occlusion_sample … brackets≈frames own>0 …` with
`main_view` nonzero in flight. With `--sun-occlusion-log`: `sun_probe … eligible=1`, `sun_visibility … single=1
answered=…`, `sun_lens_draw …`; an SM3 sun body adds `sun_lens_body_unclassifiable … note=blocked_vanilla_probe`
(the deferral: the vanilla probe decides). `--perf`: `lens_flare_gain_frame … gain=<G×T> base=1.0000 fog=1
fog_t=<T>` every 300 frames while the gain is below 1; The Hole at run 13's inputs predicts `fog_t` ≈ 0.58
([`sky_transmittance.py`](../../verification/results/fl-run13-sun-fog/sky_transmittance.py), plain column).

**What a flight must show.** The sun disappears behind hulls in FL (all or nothing while the SM3 chain is deferred);
in The Hole the sun's flare is visibly dimmer than in a clear sector. Wrong: `status=anchor_mismatch`,
`main_view=none`, `own=0` in every sample while the sun is on screen, or a `sun_occlusion_blocked` row.
