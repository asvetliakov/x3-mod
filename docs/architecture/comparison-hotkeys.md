# Same-run exposure and bloom comparisons

Controls were installed in candidate `75dbbed`; runs 26/27 exercise their notices
and toggles in game. Native Windows remains unverified. The 2026-09-14
[exposure decision](space-exposure-policy.md), updated 2026-09-15, selects the
current production source default: Auto capped at +1.3 EV. Run 27 exercised the
earlier explicit +1.5-EV cap. The control protocol and meter equations are unchanged;
installation state is recorded in [status](../status.md).

## Controls and truthful state

Launch with `--motion-output --hdr --hdr-tonemap --hdr-bloom` to prepare the
features. Keep the game's Glow setting enabled: the recovered compositor
boundary depends on it. Existing launch commands keep HDR, AgX and enhanced
bloom opt-in. AgX production initialization uses Auto capped at +1.3 EV;
`--hdr-exposure fixed` selects fixed EV 0, multiplier 1. `--hdr-exposure fixed|auto`
requires `--hdr-tonemap` when explicitly supplied. The launcher always writes
`X3M_HDR_EXPOSURE`, overriding a stale inherited policy, and clears stale
`X3M_HDR_EV_MANUAL`. An explicit `--hdr-ev-manual` remains authoritative over the
policy option. Direct environment configuration accepts `auto`, `manual` or
`fixed`; absent mode selects Auto, while an unrecognized or truncated explicit mode
falls back to fixed.

Hold **Ctrl+Shift**, then press **F9** for AUTO ↔ fixed EV 0 or **F10** for bloom
ON ↔ OFF (**F11** toggles the ambient occlusion chain when `--ambient-occlusion` is on; no
notice, one `ambient_occlusion_toggle` log line per press, `docs/architecture/ambient-occlusion.md`
"Step 2"; **F12** switches the sun shadows off and on with `--sun-shadow-apply`,
"Sun shadows at rest" below). Three more keys switch one emitter gain between its configured
gain and native, without recreating anything: **F4** the hull light-map gain
alone (`--hull-lightmap-gain G`, the self-illumination term of the 100 opaque
hull programs, `linear-emission-cost.md` "Hull light-map gain"), **F5** the
additive bullets (`--screen-emission-additive G`, the nine SM1 screen pairs)
and **F6** the effects group: the emission source gain
(`--emission-source-gain G`, all twenty engine/effects pairs) together with the
docking and gate guide lights (`--hull-emitters`, the twelve hull programs'
ONE/ONE draws of `emitter-plan.md` phase 3), which follow that gain
(`--hull-emission-gain G` overrides the value; F7 is the telemetry phase marker
and F8 the capture key). The two hull families were paired on F4 between
2026-09-18 and the run 41 regrouping; each still has its own flag and option,
so the light map is judged apart from the emitters, and the 2026-09-16 F4
effect key had gone with the undone family split (`linear-emission-cost.md`
"Screen substitution"). One F6 press logs `emission_source_gain_toggle` and
`hull_emission_gain_toggle`, and with them two `renderer_comparison` lines
under `key=ctrl_shift_f6`, one per half (the guide light's refusal is written
`GUIDE N/A` so both halves fit the notice's 36 columns); the hull line carries `key=ctrl_shift_f4` or
`key=ctrl_shift_f6`, the state of the family the key drove in `enabled=`, and
both families' states in `hull_enabled=`/`lightmap_enabled=`. Each key only decides
whether the per-draw path selects the variant that was already built at
CreatePixelShader time (and, for F6, whether a screen draw gets its DESTBLEND
ONE substitution), so an off option draws with the native program, the native
blend and no substitution, exactly like a refused draw; an option that was
not requested, or the source gain at 1 (no variant is created), answers with
a logged no-op shown as UNAVAILABLE. The additive key has no gain-1 case:
`G = 1` still draws with DESTBLEND ONE (and the alpha attenuation, if any),
so F5 switches it whenever the option is requested. The three keys are polled
whenever any comparison sampler is open, so a press in a run launched without
`--hdr --motion-output` (or without that option) logs its refusal rather than
being silently dropped. Each press logs `hull_emission_gain_toggle`,
`screen_emission_additive_toggle` or `emission_source_gain_toggle` with the
acceptance, new state and gain, plus the usual `renderer_comparison`
line under key `ctrl_shift_f4`/`f5`/`f6`, and takes over the notice's second
line (LIGHTMAP/BULLETS/EMISSION/GUIDE ON/OFF/UNAVAILABLE, all of them when
several keys land in one sample, and both EMISSION and GUIDE for one F6 press)
until the next F9/F10 press.
With `--telemetry`, the additive option also logs one
`screen_emission_additive_frame device=… frame=… admitted=… refused=… pairs=<hex
mask of the nine table indices admitted this frame> toggled=<option currently
on>` line per Present, with the counters reset every frame. Each function key needs a new press. Ctrl+Shift must already be held
in the previous foreground frame sample, preventing a modifier change from
turning a held function key into a press. F8 capture is unchanged. Input is
sampled once at the existing frame boundary, after Present and before the next
scene latch, only when HDR and AgX were requested. Ordinary launches incur no
new comparison key or foreground queries. The foreground window must belong to
the game process. A sampled
focus loss or Reset disarms the keys until a foreground sample and release.
Polling cannot detect an entire focus-away/back interval during which no frame
was sampled; no window-procedure hook is added. The prior-frame chord rule also
suppresses the ordinary held-chord return in that case, but is not a claim of
complete unseen focus-transition detection.

## Sun shadows at rest

**Ctrl+Shift+F12** switches the sun shadows off and on at a frame boundary
while `--sun-shadow-apply` is on; without that option the key is not polled at
all and a press is ignored. It exists because no GPU timer query works on this
backend: the only instrument for the cascades' fill and clear cost is the
`frame_end` median with the shadows on versus off at the same spot, at rest.

The press flips one boolean that the scene end tests once. Off, the scene end
runs neither the replay transaction (no cascade map is cleared or drawn, no
live record and no retained caster is issued) nor the apply quad, so no
`shadow_replay_depth` and no `sun_shadow_apply_frame` line is written for that
frame; the frame's geometry leases are retired exactly as a replayed frame
retires them. Everything else keeps running unchanged: the sun lane, the
caster-candidate counter and its per-frame line, caster retention's own
bookkeeping, TAA and F8 capture. The presented image is simply the un-shadowed
one. Nothing is created or released by a press, and there is no per-draw cost:
the gate is one bool test at the scene end.

Both edges void everything a past replay published — the retained per-cascade
bases, the single map's view rows and its basis — as a Reset and a refusal do,
so an off interval can never leave a stale map for the apply quad or for an F8
dump: while off the map reports itself invalid and is not dumped at all, and
the first frame back on replays every cascade in full, the far one included
whatever the budget's alternate-frame rule would say (its retained basis is
gone, so it must not stay absent for a frame). A Reset inside an off interval
releases the maps and no transaction recreates them until the toggle comes back.

Each press logs `sun_shadow_toggle device= state= frame= accepted=`; a device
with neither the replay nor the apply requested answers `accepted=0` and
changes nothing, as an unrequested ambient-occlusion press does. There is no
notice line and no `renderer_comparison` record, as with F11. `shadow_toggle=`
on the `shadow_replay_depth` line is printed on enabled frames only — a frame
toggled off writes no such line at all. A triage therefore splits `frame_end`
medians by the `sun_shadow_toggle` events, which bound the intervals, and by
the absence of the shadow line inside an off interval; `shadow_toggle=` states
what each written line ran under. The key follows the same edge, chord and
focus rules as F4-F6 and F9-F11; a held key, a modifier change or a focus loss
is not a press.

A dark two-line top-left panel lasts three seconds. It shows AUTO/fixed effective
EV and bloom ON/OFF only when the current frame used the relevant processing;
otherwise it says WAITING, REQUESTED or UNAVAILABLE. Capability refusal never
silently creates the missing feature. An unavailable AUTO capability alongside
a valid manual tonemap is shown as `FIXED EV … / NO AUTO`. Existing custom EV
bounds still clamp requested manual zero, so a nonzero effective fixed EV is
printed honestly. An unavailable safe drawing boundary can suppress the panel;
the request is always logged.

`renderer_comparison` logs the device/frame, request key, acceptance, exposure
mode/effective EV, capability reason and whether this frame used AgX, plus bloom
requested/ready/used/effective state. A second line before the next Present
records the frame actually produced. `bloom_off_filter_runs=1` makes the OFF
path's remaining filtering cost explicit. Normal HDR frame telemetry uses the
HdrPass's current mode and EV as before.

## FPS overlay

`--fps-overlay` (`X3M_FPS_OVERLAY=1`, default off, no prerequisite) draws one
line on the presented image, one panel below the hotkey notice:
`FPS 61.3  16.3 MS  DRAWS 638`, and `SHADOWS ON|OFF` under it when
`--sun-shadow-apply` is on (the Ctrl+Shift+F12 state). **Ctrl+Alt+F7** (Alt
is the Option key under Wine on macOS) hides and shows it; each press logs one
`fps_overlay_toggle` line. Every Ctrl+Shift function key is owned (F4–F6 and
F9–F12 above, F7 the telemetry phase marker, F8 the capture key) and F1–F3
are the engine's cockpit, external and target views, so the overlay uses the
Alt variant: the chord requires Ctrl and Alt down and Shift up, and the
marker (`telemetry.cpp`) requires Ctrl and Shift. Pressed Shift-first
(Ctrl+Shift+F7) the marker fires and the overlay never does; pressing Shift
while Ctrl+Alt+F7 is still held stamps one `telemetry_phase_marker` line
(harmless) and toggles nothing more, because the overlay edges on the raw F7
latch. The sampler applies the Alt/Shift rule outside its Ctrl+Shift arm,
under the same focus latch as the other keys. A launch with only
`--fps-overlay` polls this chord and nothing else: the emitter keys F4–F6 are
polled only with an emitter option on.

The numbers come from a one-second sliding window of four 250 ms buckets over
the same `QueryPerformanceCounter` clock as the `frame_end` line: FPS is
frames per second over the window, the ms figure is the mean Present-to-Present
interval (the frame interval, not GPU time; a driver that queues frames shows
the throughput, not the latency), and DRAWS is the mean hooked draw count per
frame. The text is rebuilt when a bucket closes, about four times per second,
and each rebuild costs the same glyph pass as a notice change; the
`SHADOWS` line follows the Ctrl+Shift+F12 state the frame it changes (one
compare per shown frame). Showing the overlay again after hiding it starts a
fresh window, so the hidden span never enters the interval; the first line
appears 250 ms after that. A load stall shows as one low reading for at most
a second.

Mechanism and cost: the `FpsOverlay` accumulator (`src/proxy/fps_overlay.h`)
is pure tick arithmetic, and the bitmap is a second `ComparisonNotice` instance
with its panel row at 72, so it shares the glyph table, the two `Clear` calls,
the state transaction and its restore order. The draw runs at the notice's
site in `present`, after the tonemap and apply passes, under the same
foreground, Reset, compositor, bloom and comparison-boundary admission and the
same device pin; a failed draw or restore logs one `renderer_fps_overlay`
line once per failure episode (`FpsOverlay::draw_outcome`), keeps the mode
on and retries the next frame, so a lost device recovers on its own after
Reset. Shown, a
frame pays one `QueryPerformanceCounter`, three integer adds, one compare,
the foreground query and the notice draw: two `Clear` calls, one with the
panel and one with N glyph rectangles (about 500 for a two-line overlay),
plus the state reads and restores around them. The rectangle clip is cached
per text and target size (`ComparisonNotice::clip_passes`), so it and the
text rebuild run about four times per second, not per frame. The overlay
therefore perturbs its own reading by exactly that per-frame cost, which is
unmeasured in game; two `Clear` calls need not be two backend operations, so
the driver-side cost of the N rectangles is unknown. Hidden or off, one
branch, and without the option the key is not polled. Device Reset empties
the window and the bitmap and keeps the visibility. Host coverage:
`verification/analysis/test_fps_overlay.py` (the accumulator's window
arithmetic, ms rounding, toggle, Reset and second-line semantics, no
allocation; the Present-path wiring; the launcher environment), the sampler
fixture (the Ctrl+Alt+F7 edge, Shift exclusion, Alt and Ctrl requirements,
focus latch) and the notice fixture (the row-72 instance, the clip cache).
In-game and native Windows behavior are unverified.

## Volumetric fog

With `--volumetric-fog` (`docs/architecture/volumetric-fog.md`, "Stage 1
implementation") two more chords use the overlay's Alt rule (Ctrl and Alt
down, Shift up; raw F9/F10 latches of their own, so Ctrl+Shift+F9/F10 stay
exposure and bloom and a held key never becomes a press by changing
modifiers): **Ctrl+Alt+F9** toggles the pass (`volumetric_fog_toggle`),
**Ctrl+Alt+F10** steps the strength through 0.005/0.01/0.02/0.03/0.05
(`volumetric_fog_strength`). No notice and no report; with `--fps-overlay` the
second line carries `FOG 0.020`, `FOG 0.020 IDLE` (the sector rule holds the
medium at zero) or `FOG OFF` beside the `SHADOWS` state, rewritten the frame
it changes. The Alt key is polled with either option. Without the option no
key is polled. In-game behavior is unverified.

## Exposure handoff and capability preparation

`HdrConfig` retains its old component default for standalone callers and
fixtures. Production capture initialization explicitly selects Auto, maximum EV
1.0, and `allow_auto_toggle=true`; manual zero remains the comparison reference. `HdrPass::attach` uses the existing format, shader and
self-test gates to prepare AUTO when either AUTO is selected or that flag is
set. Its normal target setup prepares the meter chain for the available
capability even while fixed. A chain failure disables AUTO capability; no key
press builds a shader, runs a probe or allocates a target. Component callers
that start manual without opting into preparation still cannot toggle AUTO.
Fixed mode performs no meter draw, readback, statistics reduction or adaptation
step per frame. Startup capability self-tests still exercise the optional meter.

The mode switch runs only after a closed, unblocked frame boundary. Both
directions set requested manual EV to zero, reset adaptation to neutral,
discard both pending meter-ring samples and reset ring index/latch timing.
AUTO therefore starts at neutral and consumes only fresh future samples.
AgX, decode/look, material settings and the exposure parameters are unchanged.
The temporal pass remains enabled but its history is invalidated once on the
switch, so the next eligible resolve seeds fresh history. History contains
engine-space color, not pre-exposure; the invalidation is a conservative
handoff for the changed luminance-weighting parameter, not an exposure rescale.
Reset preserves the selected mode through the retained HdrPass configuration;
its existing target release discards old meter surfaces and pending results.

## Bloom OFF preserves the comparison boundary

Both modes execute the original compositor exactly once and retain the same
qualified, pre-original resolved scene and latched AgX constants. OFF supplies
`BloomPrepare.filter.strength=0` to the existing candidate, then uses the same
RGB-only replacement. This suppresses bloom contribution without reinstating
native glow, changing alpha/HUD ordering or altering temporal history. It
retains the current filter pyramid and composition costs; OFF is a visual
isolation control, **not a no-bloom performance baseline**. No shader or
renderer-pass implementation changes are needed for the zero-strength path.
A missing compositor admission, Glow off, failed preparation or failed commit
remains a requested state, not an effective ON/OFF claim.

## Late notice state transaction

There was no existing renderer notice path. `ComparisonNotice` uses authored
5×7 bitmap glyphs at 2× scale, with at most 36 characters per line. Its panel is
48 pixels high with width fitted to the text (at most 448 pixels), beginning
at (16,16). Glyph geometry changes only with text. It owns bounded CPU arrays
(about 79 KiB/device), no GPU objects, COM references, shaders, textures, font
library or state blocks. The inactive notice performs zero D3D calls; ordinary
draw hooks acquire no new allocations, locks, lookups or validation.

Immediately after `MotionOutput::before_present` completes scene processing,
and before native Present, the caller admits the notice only outside scenes,
queries, recording, Reset, compositor invocation and state-loss quarantine.
It pins the shared Device context and a native device reference through the
containing Present. The existing injected-operation guard protects all Get,
Set, Clear and temporary surface-alias Release callbacks from final-reference
inference or reentrant Reset. Notice drawing uses the saved native slots.
The final device pin deliberately uses normal hooked Release, after the outer
Present lock is released: it must retain the established final-owned-resource
retirement when a callback dropped the application's last reference. The CPU
owner remains pinned through that release. The holder is declared before the
outer lock so final profiler shutdown cannot wait while Present still holds it.

Every supported RT slot, depth surface, viewport and backbuffer reference is
obtained before mutation. The notice detaches depth and optional MRTs, binds
only the final backbuffer and uses a full viewport. Two `Clear` calls draw the
background and bounded glyph rectangles; rectangles are clipped to the actual
backbuffer. [Microsoft documents rectangle/viewport clipping and clearing all
bound render targets](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-clear),
which is why secondary MRTs are explicitly detached. Clear needs no added
BeginScene/EndScene bracket or shader/render-state changes.

Restore covers every attempted setter, including one that reported failure
after mutation. RT0 and optional MRTs are restored first, then depth, with
viewport last because [SetRenderTarget resets the viewport](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setrendertarget).
Every cleanup is attempted and the first restoration HRESULT wins. A failed
read causes no mutation. A failed draw hides the notice and logs its result;
successful cleanup permits ordinary rendering. A failed restoration latches
the established motion submission quarantine without replacing an earlier
error. Subsequent native draws are blocked until the existing successful
Reset/resynchronization recovery. Native Present is still submitted once.
The panel is deliberately last: it never enters FP16 metering, TAA history,
bloom input or engine HUD state. It overlays final displayed pixels only.

## Verification and remaining qualification

The focused host test links the production notice intact to scripted public
API outcomes and executes the production exposure handoff. It covers held
keys, modifier changes, sampled focus loss/regain, initial held keys, Reset
disarming, independent/simultaneous controls, pending-meter discard, fixed
multiplier 1, custom EV clamps and capability refusal without mutation.
Notice tests cover every saved-state read failure, every attempted setter
failing before/after mutation, combined restore failures, both Clear failures,
known null optional bindings, reference balance and small/backbuffer-clipped
geometry. The idle/allocation witness executes 100,000 checks without a D3D
call or allocation. Parser tests execute production argument validation and
environment assignments without launch or filesystem side effects.

Initial verification passed: 9 focused unittests (`test_comparison_hotkeys`,
`test_hdr_display_snapshot`, `test_motion_wrap_states`). The new control/handoff
fixture has 10,107 checks and the notice fixture 108,073 checks, each passing
both optimized and ASan/UBSan host execution; the latter reports zero owned
allocations. Existing writeback and WRAP evidence passed unchanged. The four
affected production translation units (`capture`, `motion_output`, `hdr_pass`,
`comparison_notice`) cross-compiled cleanly with MinGW x86, SSE2 and the
four-byte incoming-stack contract. `git diff --check` passed.

Review fixes additionally execute the actual notice holder and production
release/Reset/text functions in the capture lifetime fixture. The witness
drops the last application reference during notice and Present callbacks,
under native-object and ownership-alias models, and verifies final retirement
after the outer lock, exactly one simulated Present and CPU-owner lifetime.
It also verifies unattempted bloom is WAITING, an attempted refusal is
UNAVAILABLE, successful current-frame commitment determines ON, and Reset
hides the notice. Polling is gated before all new input/foreground queries
when HDR+AgX were not requested. All five affected comparison/lifetime tests
passed after review fixes, including 40 lifetime scenarios / 186 checks; the
capture translation unit cross-compiled again and the diff check remained clean.

The initial source checkpoint above performed no Wine run or installation.
Subsequent qualification and gameplay evidence are recorded below. Source/host
checks do not establish native Windows behavior or GPU cost. In particular, two
Clear API calls need not become two backend operations: drawing many glyph
rectangles can incur driver work, so notice cost remains unmeasured.

## Combined candidate

The reviewed implementation is installed with the pure-device creation fix.
The actual combined route passes eight material cells and the emission live
fixture, exercising fixed-mode HDR alongside ownership, TAA and Reset. The
selected automatic-exposure fixture passes 120 frames / 245 checks; two hostile
WRAP cases add 190 checks. These are selected-case passes, not a full-project
suite result. The retained production DLL passes its load check and x87 audit;
[the single install record](../../verification/results/linear-material-install.json)
binds source, binaries, rollback and scoped evidence. Runs 26/27 subsequently
show the controls/notices in game and confirm actual compositor execution. Run
27 records 94 accepted mode requests and all 77 sampled bloom prepare/commit
pairs succeed; its F8 buffers precede final bloom and are not a notice pixel oracle.
Notice cost, unavailable-capability appearance, focus/Reset behavior in more
scenes, and native Windows execution remain separate acceptance limits.

The 2026-09-14 default change starts **Auto → fixed → Auto**. All five affected
host tests pass, including existing release/sanitized controls and notice cases.
That checkpoint's launcher dry-run verified Auto/+1.5 without explicit policy
or EV options; the vanilla dry-run also passed. Explicit Auto/+1.5 ran in run 27
and remains historical evidence for the controls. The current production source
keeps Auto and lowers its default ceiling to +1.0; explicit +1.5 remains supported.
