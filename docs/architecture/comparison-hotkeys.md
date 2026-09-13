# Same-run exposure and bloom comparisons

2026-09-13. Installed in candidate `75dbbed`. **In-game control/notice appearance
and native Windows behavior remain unverified**. The separate
[space-exposure evaluation](space-exposure-policy.md) selects fixed EV 0 as the
production default. No automatic-meter parameters change here.

## Controls and truthful state

Launch with `--motion-output --hdr --hdr-tonemap --hdr-bloom` to prepare the
features. Keep the game's Glow setting enabled: the recovered compositor
boundary depends on it. Existing launch commands keep HDR, AgX and enhanced
bloom opt-in. AgX production initialization now uses fixed EV 0, multiplier 1;
`--hdr-exposure auto` opts into the existing meter. `--hdr-exposure fixed|auto`
requires `--hdr-tonemap` when explicitly supplied. The launcher always writes
`X3M_HDR_EXPOSURE`, overriding a stale inherited AUTO value, and clears stale
`X3M_HDR_EV_MANUAL`. An explicit `--hdr-ev-manual` remains authoritative over the
policy option. Direct environment configuration accepts `auto`, `manual` or
`fixed`; absent or unrecognized mode remains fixed.

Hold **Ctrl+Shift**, then press **F9** for AUTO ↔ fixed EV 0 or **F10** for bloom
ON ↔ OFF. Each function key needs a new press. Ctrl+Shift must already be held
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

## Exposure handoff and capability preparation

`HdrConfig` retains its old component default for standalone callers and
fixtures. Production capture initialization explicitly selects manual zero and
`allow_auto_toggle=true`. `HdrPass::attach` uses the existing format, shader and
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

GPU qualification must check legibility, visibility after AgX/bloom, unchanged
F8/history inputs, current-frame state/EV logs, default fixed → AUTO → fixed
handoff without stale-history flash, both bloom modes with original-once alpha
parity, unavailable capability text, and alt-tab/Reset behavior. Two Clear API
calls do not imply two backend GPU operations: many glyph rectangles may incur
driver work, so visible-notice frame cost still needs measurement. Cross-
compilation and host fakes establish source/transaction evidence, not native
Windows or CrossOver pixels/performance. No Wine, gameplay or installation was
performed for this checkpoint.

## Combined candidate

The reviewed implementation is installed with the pure-device creation fix.
The actual combined route passes eight material cells and the emission live
fixture, exercising fixed-mode HDR alongside ownership, TAA and Reset. The
selected automatic-exposure fixture passes 120 frames / 245 checks; two hostile
WRAP cases add 190 checks. These are selected-case passes, not a full-project
suite result. The retained production DLL passes its load check and x87 audit;
[the single install record](../../verification/results/linear-material-install.json)
binds source, binaries, rollback and scoped evidence. Run 7 still needs to
verify visible controls, notice cost and actual game compositor execution.
