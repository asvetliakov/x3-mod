# Chase camera: first-flight anchor-domain correction

2026-09-13. The first user flight applied the reviewed camera hook, but the user
reported trembling while flying and requested lower, bottom-centre ship
framing. Approximately 70–75% of screen height is our estimate from the supplied
reference, not a numeric target specified by the user. The run used
`--direct --camera chase`; TAA, motion output, HDR
and telemetry were off. The log was retained locally as
`/tmp/x3-game-session-20260913-054829-212/session-20260913-054829-212.log`.
Its latest camera report counted 5,871 applied of 6,650 active visits and 779
refusals/pass-through frames; sampled refusal rows were internal view, with no
invalid-input, numeric or write failure reported in those samples.
Only two entry/re-entry snaps occurred; continuous flight did not repeatedly
snap. The earlier combined clamp count was 2,724, so it could not distinguish
orientation and boom clamping. These observations rule out a simple
install failure or repeated pipeline reset as the reported symptom.

The new anchor selection is a confirmed source correction. The exact amount of
domain separation in that flight was not recorded, so fixing the user's visual
symptom remains a gameplay acceptance question.

## The native camera chooses between two position domains

The earlier study labelled `0x0044fe20` as a docking/carried-object test without
decompiling it. Its actual predicate is:

`ref+0x54 != 0 && *(short*)(*(ref+0x54)+0x48) == 1`.

The user-visible meaning of owner type 1 is not needed for the implementation;
it must not be treated as synonymous with docking. For the ordinary external
follow path, connect mode 0 with `cockpit+0x1a0 & 4` clear:

| Native predicate | Ship anchor used to build camera position | Basis used to rotate boom and view |
| --- | --- | --- |
| True | `*(ref+0x70)+0x30` | `0x00450520`: reference type `*(short*)(ref+0x48)==7` uses `*(ref+0x50)+0x870`; other types use node `+0x40` |
| False | `*(ref+0x70)+0xb0` | node `+0xc0` |

The true position branch is `0x00420ad3..0x00420b19`, with the node `+0x30`
anchor added at `0x00420afb`. The false branch is
`0x00420b1e..0x00420b67`, adding node `+0xb0` at `0x00420b49`.
The basis branch tests the same helper at `0x00420bc4`; its true multiply is
`0x00420be5`, false multiply `0x00420c02`. Both preserve
`camera_basis = view_relative_basis × native_ship_basis`.

The previous handler always used node `+0xb0` as `ship_pos`. On the native
true branch it therefore constructed a spring target containing the difference
between base and render-ready positions. That difference is not a camera boom.
The render-frame routine calls node traversal `0x0047bc20` at `0x00472256`,
after cockpit update; that traversal writes node `+0xc0` at `0x0047bea5` and
`+0xb0/+0xb4/+0xb8` at `0x0047c047..0x0047c053`. It is not valid to assume
these render-derived fields equal the current base-domain native follow anchor
before traversal runs.

The correction reads the exact native predicate and selects the matching
position. Orientation still uses `view_relative_basisᵀ × vanilla_camera_basis`,
so it keeps the native camera's own effective basis. Matching native and
render-ready basis reads are diagnostic only: a missing diagnostic basis or
second position domain does not refuse an otherwise valid required anchor.
All required reads validate non-null four-byte-aligned x86 pointers and reject
address arithmetic wrap. They use the existing documented Win32 memory reader;
no backend-private data or new code hook is introduced.

## The previous hook output does not accumulate into the next target

For connect mode 0, `0x004218cf..0x004218dd` subtracts one from the mode and
takes the unsigned default branch to `0x004228a8`. It then reaches the current
angle updates and unconditionally ends at `0x00422c41..0x00422c5c`:
current angles are cockpit `+0x90/+0x94/+0x98`, ESI is cockpit `+0xf0`, and
`0x004f0270` regenerates the vanilla view basis. Unchanged angles do not bypass
this write. The call to this routine at `0x004205fb` precedes all camera
construction in that update.

The original study's claimed `+0xf0` writer at `0x00420a27` was a reversed
operand interpretation: EAX points to `+0xf0` as an input; the stack argument
points to the destination. It is also on the internal-view branch, which the
external view bypasses at `0x004207c6`. The early jump at `0x004207a7` requires
the null cockpit reference field (`cockpit+0xc`), and the chase handler's
active-cockpit gate refuses that case.
These paths do not explain repeated pitch accumulation in the accepted view.

This also corrects the optional cockpit-scene-camera adjustment. At
`0x00420787` the engine consumes **current vanilla** `+0xf0`, not the previous
hook output. The optional correction is consequently
`new_view_relative × current_vanilla_view_relativeᵀ × scene_basis`, preserving
the native shake transform. It remains off by default and unverified visually.

## Framing, bounded diagnostics and verification

`X3M_CHASE_OFFSET_Y` now defaults to 0.45 instead of 0.12. For a centred native
anchor, the pipeline's pitch projects that anchor at `(1+0.45)/2 = 72.5%` of
screen height. An elevated native boom, the ship's silhouette and camera lag
can shift the visible ship centre. The spring time constants, distance scale
and lag clamps are unchanged.

The existing report cadence now includes native branch/sample counts,
native-versus-render position separation and its minimum/maximum, matching
native and render-ready basis deviations, separate report-window orientation
and boom clamp counts, and minimum/maximum lag. These use bounded stack/state
storage; no per-draw work, per-frame log, heap allocation or new lock is added.
The next telemetry run also has the already-integrated aggregate handler CPU
timings. They exclude the assembly stub and CPU save/restore, and are not FPS.

Host verification passes 37 camera tests plus eight executable-site tests.
The exact production native reader executes 16 synthetic controls covering
both native branches, ship/non-ship basis selection, pointer alignment/null/
wrap rejection, required-read failure without output publication, and optional
diagnostic-read failure without disabling the camera. In the translation
regression, independently alternating the render position creates a maximum
199.3429565-unit camera error with the old anchor and zero with the corrected
anchor; the render-domain branch also stays exactly translation-invariant,
without clamps. This establishes the mechanism on synthetic inputs, not its
size in the user's flight. The optional scene correction has maximum matrix
error `1.11e-16`; the old previous-frame reference differs by `0.2942` in that
nonzero-motion control. A projection test checks the 72.5% anchor framing.

The trampoline, four-byte incoming-stack contract and CPU-state boundary are
unchanged. Host tests do not execute the live game handler or establish native
Windows runtime behavior. The corrected DLL requires fresh cross-compilation,
independent review and qualification before installation and user acceptance.

## Local evidence and reproduction

The installed executable remains the verified image with SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Fresh Ghidra output is local and untracked:
`/tmp/x3-camera-study/chase-run1-feedback.txt` (the view update listing and
helper decompilations), `chase-run1-node-domain.txt` (helper references and
supporting object code), and the earlier
`/tmp/x3-object-world-update-asm.txt` (render traversal writes).

The first extraction used the existing read-only project:

```sh
env JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -noanalysis -readOnly \
  -scriptPath tools/analysis -postScript X3CameraState.java \
  /tmp/x3-camera-study/chase-run1-feedback.txt \
  ins:004218b0 range:00422c41:22 dec:0044fe20 dec:00450520 data:004f0270
```

No game or Wine process was started for this investigation.
