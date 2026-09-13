# Review 50: HDR bloom live integration

Independent source and evidence review, 2026-09-13. Scope: production scene
callsite binding and packaging, the synchronous MotionOutput display handoff,
capture owner/lifetime/Reset policy, BloomPass invocation, focused host/X3
fixtures and launcher controls. The lower compositor bridge, BloomPass image and
GPU recovery matrices remain covered by reviews 43--45 and were not rerun.

## Result

**The opt-in live integration is approved with no open finding.** The reviewed
path is selected only by `X3M_HDR_BLOOM=1` together with MotionOutput, FP16 HDR,
AgX and the scene hook. Default-off keeps the existing one-way scene signal and
original compositor call. The working integration has not been installed or
run in the game, so this disposition supports a clean candidate build and
installation checkpoint rather than an appearance, FPS or gameplay claim.

The hook checks the exact five-byte call and the adjacent 25-byte caller
boundary, installs only inside the shared engine-patch window and gives the
bridge the verified original target. Its binding is copied and remains for the
process lifetime; device destruction no longer attempts to restore executable
bytes while a wrapper could still be returning. The unchanged bridge preserves
the qualified GPR/flags/x87/MXCSR/XMM/LastError contract and invokes original
exactly once. Failed patch ownership and explicit fixture shutdown retain the
existing rollback and external-quiescence rules.

Capture selects the exact engine owner and observed BeginScene thread. One
`shared_ptr` keeps the CPU context alive and one native AddRef pins the D3D
device across the unlocked original. The registered invocation owns its scene,
main, depth and candidate aliases until cleanup. Reset and ResetEx increment the
generation, revoke and drop DEFAULT-pool aliases before native entry, while
retaining the CPU/native pins through the original call. Cleanup unregisters
once, releases aliases under internal-operation suppression, drops the native
pin through `release_device`, then destroys the CPU pin. Final-reference
accounting combines persistent MotionOutput and BloomPass resources only when
neither component is releasing or probing references.

The MotionOutput callback is synchronous and retain/copy-only. It exposes the
actual clean AgX writeback image and display snapshot before redirect cleanup.
TAA-enabled admission requires this hook's successful HDR resolve; TAA-off uses
one temporary public `GetContainer` reference and releases it on every outcome.
The null callback bypasses the added snapshot, identity and container work. The
live BloomPass input copies the exact sharpen c23 block used by the writeback;
standalone callers retain the prior derived-constant behavior.

## Findings resolved during review

| # | Severity | Finding and resolution |
| --- | --- | --- |
| R1 | medium | The initial live handoff copied sharpen strength but recomputed c23, so bloom preparation was not guaranteed to use the exact display transform already applied to the scene. `BloomPrepare` now has an opt-in exact block, validates finite gain/texel steps/zero padding and uploads the captured block. The real X3 seam exercises a nonzero exact block; default standalone behavior is unchanged. |
| R2 | low | Initial prose implied every early refusal retained the exact scene-hook signal, although broadcasting GPU work to an unknown owner or wrong thread would violate admission. The contract now states the intended timing: safe-owner glow/pass refusals receive one ordinary scene-end call; unknown owner/thread runs original once and relies on the later established fallback chain. Bounded diagnostics record `ordinary_signal`, and the extracted production pre callback now covers invalid caller, unreadable/missing owner, wrong thread, Reset-active and safe-owner glow-off cases. |

## Evidence assessment

The X3 scene/callsite fixture passes **43/43 checks**. It covers the ordinary
tail path, bridge acceptance/refusal, actual caller PC, copied callback binding,
active-shutdown refusal, wrong-target/missing-callback rejection, byte restore
and late-install rejection. Its compact result is
[`scene-compositor-summary.json`](../../verification/results/bottle-X3/scene-compositor-summary.json).

The extracted MotionOutput fixture passes **72 paired scenarios and 1,526
checks** over the actual five changed function bodies. It covers exact snapshot
forwarding, callback ordering, TAA success/failure admission, TAA-off container
ownership and null-callback parity. The extracted capture fixture passes **29
scenarios and 111 checks**, including both COM alias models, ordinary and
cross-thread final Release, combined busy-state suppression, Reset/ResetEx
success and failure, post owner/frame/thread/generation/glow changes, nested
admission, early refusal routing and safe-owner glow-off signaling.

The combined X3 record passes **714 checks**: 357 in each of
`X3M_OWNERSHIP=0/1`, 20 scenarios, zero skips. Every scenario performs actual
BloomPass preparation; eligible scenarios commit. It observes eight real
MotionOutput-held device references with HDR active and TAA configured, and
covers nonterminal GetDevice/Release, same-thread and worker final Release,
Reset/ResetEx success/failure, escaping and continued original exceptions,
DEFAULT reference removal before Reset, native-pin survival and CPU-context
destruction after cleanup. The bounded record and runner parser agree:
[`capture-bloom-x3-summary.json`](../../verification/results/bottle-X3/capture-bloom-x3-summary.json).

I independently ran the three affected host modules. All **11 tests passed**,
including recompilation/execution of the extracted MotionOutput and capture
bodies and nine strict combined-result parser controls. I also validated the
combined JSON's two modes, 20 unique cases, 714 checks and zero skips, and ran
`git diff --check`. No broad host suite or additional Wine run was warranted.

The reviewed CMake scratch audit retains the GNU/no-SafeSEH policy, uses the
configured MinGW tools, packages the isolated Clang SEH object and introduces
only `msvcrt.dll!_except_handler3`. The compared DLLs both have an empty load
configuration and characteristics `0x140`; the scene-hook object has no SJLJ
dependency. CMake policy-OFF and an unacknowledged helper link are rejected.
The scratch report SHA-256 is
`86ae68649f03b8bc9f9f75aff0e3014b8d2cf2747287ab6047ec7e236407c268`.

## Limits and reviewed identities

The combined X3 fixture uses a synthetic owner/original and test-only Ex-device
adoption. It does not execute the installed game callsite or live selector,
allocate lazy TemporalPass history, recover arbitrary SEH raised inside an
injected COM call, inspect a game image, or measure frame cost. Existing bridge,
MotionOutput and BloomPass component evidence covers their respective CPU,
history and GPU recovery boundaries. Production `Direct3DCreate9Ex` admission
still passes through without capture adoption. Native Windows runtime remains
unverified, as recorded in the portability note.

The core reviewed source identities were:

| File | SHA-256 |
| --- | --- |
| `src/proxy/scene_hook.cpp` | `b672966d729bdc4bd8d5822f67d9f0265ec77b3360455e10a080b6b30c5b4dc5` |
| `src/proxy/capture.cpp` | `2322accd348dc1136751d093b5d57a88631696b89328b281bbcd8de66815c600` |
| `src/proxy/motion_output.cpp` | `91117b57d365620b3bf9fa4658249bb3a1f90e63e381340bc1b886fcec75abc6` |
| `src/renderer/bloom_pass.cpp` | `8243370b0d6a257f7d4187ac0d55d9bfc43718e2f715c00e7e58be9db98c0eab` |
| `CMakeLists.txt` | `ebfcdb6916030fa494407a586923fb9de919e555e157abba69a8cce39e290d36` |
| `tools/manage.py` | `2c9497aa0b725b7051fa2792603468faa36a68659c21cfc91fa31c0fb9ffffa6` |
| scene-compositor result | `51f9e81edcef2c8813a799f2267443321ee76fd920e3f453713c4c4000b4dfc7` |
| combined X3 result | `271d0cdea39ba993112c42090ff7540ff1f4ccaee26ea3a558abd1819850a270` |

The combined fixture DLL is separately bound in its result as
`d0a8c30e181109533b6db1f8be1bbf5478260756689f9b7b65ca01a0dbe84631`.
This review performed no production build, installation, game launch or commit.
