# Review 46: chase-camera first-flight correction

Independent source review of the correction prompted by the first user flight.
The reviewed source is based on `09f835c`; the camera remains opt-in and the
installed DLL is unchanged while qualification runs. No high or medium finding
remains open.

## Reviewed result

The position source now follows the engine's exact ordinary-camera branch:
`ref+0x54 != 0 && *(short*)(*(ref+0x54)+0x48) == 1` selects render-node
`+0x30`; the other branch selects `+0xb0`. I independently checked the predicate
at `0x0044fe20` and both uses in `0x004205e0` against the installed X3AP image
(`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`).
The camera no longer treats a base-versus-render position difference as boom
motion. Its effective orientation still comes from the native identity
`view_relative_basisᵀ × vanilla_camera_basis`, so the position correction does
not substitute a diagnostic basis for production state.

The current-view correction is also sound. `0x004205e0` first calls
`0x004218b0`, whose ordinary mode reaches `0x00422c5c` and regenerates
`cockpit+0xf0` from the current angles before the cockpit-scene camera is built
at `0x00420787`. The old previous-frame premise was false. The optional scene
fix now applies `replacement × current_vanillaᵀ` and remains disabled by
default.

Required anchor reads validate non-null, four-byte-aligned x86 pointers and
reject 32-bit address wrap. A failed required read returns without publishing a
partial anchor. The render-domain comparison and both basis reads are diagnostic:
their failure records an unavailable value and does not refuse a valid camera
frame. These are game layouts behind the existing documented Win32 reader, with
no Wine or graphics-backend dependency.

The new default `offset_y=0.45` projects a centred native anchor to 72.5% of
screen height in the portable camera model. This is an initial setting derived
from the supplied reference; the actual ship silhouette and feel still require
the user's game run.

## Findings resolved during review

- The first native-anchor controls were compiled into the host driver but never
  invoked by the Python suite. A test now sends the `N` command and requires all
  16 selector, bounds, failure and translation controls to pass.
- An early revision made the second position domain and basis diagnostics
  prerequisites for the camera. Required position selection is now separate;
  missing optional diagnostics cannot disable the feature.
- The first split rotation/position clamp fields were cumulative on a log row
  otherwise scoped to one report window. The handler now accumulates per-step
  counter deltas under the existing statistics lock and resets only the
  report-window totals after taking the snapshot.
- The optional scene fix and its documentation used the previous hook output as
  the source basis. Targeted disassembly showed that `+0xf0` is regenerated
  earlier in the same cockpit update. The math, synthetic control and
  architecture/reverse-engineering notes now use the current vanilla basis.
- First-flight documentation now attributes the 70–75% target to our approximate
  reference-image reading, keeps the user's report as "trembling while flying",
  and does not claim that the source correction has fixed the visual symptom.

## Verification and performance review

I independently ran the 37 portable camera tests and eight executable-site
tests: all 45 passed. The native-reader group reported 16 checks and zero
failures. Its base-domain sequence gives a maximum 199.3429565-unit camera error
with the former `+0xb0` anchor and zero with the selected `+0x30` anchor; the
render-domain branch also remains exactly translation-invariant, with no clamp.
The current-basis scene correction differs from its expected matrix by
`1.11e-16`; the deliberately old previous-frame reference differs by about
`0.2942`. The default-framing control reaches exactly 0.725 in normalized screen
height. Python compilation, CLI help inspection, a chase-default `--dry-run`,
and `git diff --check` also passed. The dry run does not set an offset override,
so production uses the compiled 0.45 default.

The old anchor helper made two bounded reads per active camera frame: the node
pointer and one 64-byte position/basis block. The replacement makes three reads
when the owner is null, four when it is present but not type 1, and six on the
base-domain branch; at most three are optional diagnostics. The added work is
stack-only, allocation-free, uses the existing per-frame region cache and
statistics lock, and is not on a draw path. This static review finds no avoidable
unbounded work. Aggregate handler timing in the next telemetry flight must
measure the actual cost; it is not yet a game-FPS result.

The reviewed file identities before qualification were:

| File | SHA-256 |
| --- | --- |
| `src/proxy/chase_camera.cpp` | `029b0236d9482f087f57b1a2772e33c816f7dc9ed2bb5f4191f2b076ebeec763` |
| `src/proxy/chase_camera.h` | `4f102784d23b59f1df89dca50c82176849f8a1e9e10231f91378576685a1979e` |
| `src/proxy/chase_camera_math.h` | `7e3b10b597174b698d9ffd838e479c8a4979e6f4f114c38f61c5653d036c99ad` |
| `src/proxy/chase_camera_native.h` | `0f348abc94d1d44a57cf096a3fb095508f18daa86ce30d87f94951ddf3085c3d` |
| `verification/analysis/test_chase_camera.py` | `237fe8929ce5c0cd3d3f722b4e08263196c013309e2acea7308fc50f8c149be1` |
| `verification/probe/chase_camera_host.cpp` | `f6d1e9429458890ed70c3a31016f7cc404d8359b670b74d7161745e9498fefb9` |
| `verification/probe/chase_camera_native_host.h` | `6161640b6e0efe89193dd430d7dbc738d04784f664ee6a0cf26b49a67cb7cf36` |

The trampoline site, displaced instructions, register transport, four-byte
incoming-stack contract and full CPU-state boundary are unchanged. The new
selector is an inline helper and adds no export or calling-ABI surface.

## Independent qualification and artifact audit

The bounded [X3-only qualification](../../verification/results/chase-feedback-summary.json)
passed against 158 frozen build inputs, 75 analysis inputs and 11 scoped tool
inputs. Its SHA-256 is
`2da2d97c3f1816663e1afc2a9c66b89da30d6a220c07c13429e01b9131a77161`;
the durable [input manifest](../../verification/results/chase-feedback-inputs.json)
is `d811ab2566788ccb4b05070e0419091ef0ff0e48cc3d3510447713b70b3a77da`
and the [PE/object audit](../../verification/results/chase-feedback-pe-audit.json)
is `cc7af7d536c69f5b5efeead5a55f21edc608952efe0367f09adfcc3763f563c7`.
The exact retained candidate is SHA-256
`16d016d2f12847dbc47c88c3a241a628d7390354ffb02465b63723b14187de01`
(11,606,225 bytes). Full host discovery passed 972 tests, the focused camera
and site run passed 45, and the installed executable passed all nine read-only
site checks. The X3/FEX native-anchor control passed 16 checks and 100,000
applied stability frames; the X3 proxy load/default-off control passed both
cases and all 16 checks.

Independent artifact inspection matched the retained and current candidate,
all 41 response-file, disk and archive objects and every archive member's
bytes. Exactly one chase-camera object is linked and no bloom/bridge object is
present. The PE32 DLL retains the 17 local D3D9 exports and the same 14 import
DLLs / 192 symbols as installed baseline `47f1452e…27ad`; the light no-x87
boundary passes 211 reachable functions with zero violations. All 51 files in
the local evidence manifest, 23 summary-bound records and seven retained export
records match their recorded hashes and sizes.

The first X3 control's preflight incorrectly matched its own shell wrapper; its
shell lacked fail-fast and continued. The record preserves the observed PID,
does not invent the unavailable PPID, and states that no actual competing
process was found. `game_guard` was clear. The later export control used the
corrected ancestor-aware fail-fast preflight and found no conflict. A separate
initial entry-audit assertion expected exactly two `ldmxcsr` sites and exited
nonzero; retained disassembly shows the correct three sites: the local `0x1f80`
load and the normal and exception restores. The terminal audit records the
correct invariant. Neither bookkeeping correction caused a production change,
build rerun or runtime rerun.

**Verdict:** the reviewed source and exact candidate are ready for the source
checkpoint and guarded installation. This qualifies synthetic and proxy-load
behavior; it does not establish live handler writes, camera appearance or game
performance.

## Remaining acceptance

X3 binary/runtime qualification is complete; the candidate is not yet committed
or installed. The host suite does not execute the live production handler
against game cockpit memory. It establishes the corrected selection and
numerical mechanism on synthetic inputs, not the domain separation present in
the user's first flight.
Native Windows runtime behavior is also untested. The corrected DLL still needs
the source/evidence checkpoint and installation audit, followed by
the user-managed straight-flight, gentle-turn and settle check with telemetry.
That run must decide whether trembling is fixed, whether 0.45 gives the intended
bottom-centre framing, and what the handler costs in the game.
