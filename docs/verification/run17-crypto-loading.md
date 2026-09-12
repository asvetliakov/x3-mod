# Run 17: CryptoAPI cache loading acceptance

Run 17 is the first X3 gameplay run of the reviewed CryptoAPI provider/key
cache. The cache admitted the game's signature-verifier contract and reused one
provider and one imported key across the 844-check bulk. All 844 bulk checks
still ran the original hash and RSA signature operations, with zero reported
verification failures. No unexpected crypto refusal, fallback, ownership fault,
overflow or desynchronisation appears in the completed log.

This accepts the cache's targeted behavior on CrossOver Preview. It does not
verify native Windows behavior, and the overall save-load duration is not a
controlled cache-on/cache-off comparison.

## Provenance and configuration

The user reported that they loaded the save and then exited. `game_guard` was
empty before and after the final copy. The source remained the same inode, size,
and nanosecond modification time across the copy. Both independently retained
final copies have SHA-256
`b2c3e3dfde41d741b0adcf4f5facca11288900888bdc6c7c435d921528a05654`.

| Item | Value |
| --- | --- |
| Session | `session-20260913-024655-216` |
| Source | `/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures/session-20260913-024655-216.log` |
| Canonical analysis copy | `/tmp/x3-bottleX3-run17/session-20260913-024655-216.final.log` |
| Independent final copy | `/tmp/x3-bottleX3-run17/session-20260913-024655-216.log` |
| Size / lines | 10,486,233 bytes / 210,257 lines |
| Source mtime | `1789253287842404188` ns |
| Copy metadata | `/tmp/x3-bottleX3-run17/final-provenance.plist`; `/tmp/x3-bottleX3-run17/completed-snapshot.json` |
| Matching artifacts | 50 logged shader binaries under `/tmp/x3-bottleX3-run17/artifacts/`; aggregate manifest SHA-256 `479a5d671cc063877aaab4030a78f72600ef38f844c7b3c369310c4c7c452336` |
| Matching readbacks | none logged |
| Installed qualification | checkpoint `c85c5b5`, production source `ae03d9a`, DLL SHA-256 `ae2482fd5146c62898fbe20c45441d9d14183d705c50e3d03872094ec635b193` |

The runtime identifies itself as renderer version 0.4, schema 2; it does not
embed a source commit or DLL hash, so installed-build attribution also relies on
the already verified installation and the completion manifest. The relevant
configuration was `crypt_cache requested=1 enabled=1 telemetry=1 imports=1`,
with four provider slots, four key slots and a 512-byte blob limit. Telemetry and
the loading probes were enabled; motion output, TAA, HDR, the scene hook and the
mesh cache were disabled. No `gz_buffer ... enabled=1` line is present.

Forty of 41 loading-import hooks installed. The only absent row was
`MoveFileExA`, which is outside the crypto path. All nine logged CryptoAPI hooks
installed, and the acquire/release/import/destroy lifecycle rows each reported
`cache_active=1`. All 12 engine probes installed with `status=active`, including
the signature-check probe at `0x004cabc0`.

## Crypto admission and continuity

The log contains two `crypt_cache scope=window` delta rows and no session or
teardown total. The values below are therefore sums of the reported windows,
not an assertion about an unreported process-lifetime tail. The source file is
closed and stable after the user's exit, but the log ends on a cursor-poll row
and contains no teardown marker.

| Counter | Cold two-check window | 844-check bulk | Reported-window sum |
| --- | ---: | ---: | ---: |
| Logical acquires | 6 | 2,532 | 2,538 |
| Provider hits / misses | 1 / 1 | 844 / 0 | 845 / 1 |
| Failed native passthrough | 1 | 0 | 1 |
| Imports / import hits | 2 / 1 | 844 / 844 | 846 / 845 |
| Deletes emulated / native passthrough | 3 / 1 | 1,688 / 0 | 1,691 / 1 |
| Releases / suppressed | 2 / 2 | 844 / 844 | 846 / 846 |
| Key destroys / suppressed | 2 / 2 | 844 / 844 | 846 / 846 |
| Busy passthrough / import passthrough / eviction | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |

The first miss and failed passthrough are the designed cold-fill behavior. The
recorded `probe_error=0x80090016` is `NTE_BAD_KEYSET`, the expected result when
the first delete probes an absent scratch keyset. At the last report one
provider and one key were cached. There was no busy fallback, unqualified
import passthrough or eviction.

The signature probe recorded 846 calls and 846 exits, split into two cold checks
in 20.8937 ms and the 844-check bulk in 135.3276 ms. It reported zero overflow
and zero desynchronisation. Across those 846 checks, the native CryptoAPI rows
recorded:

| Operation | Calls | Failures | Time |
| --- | ---: | ---: | ---: |
| `CryptAcquireContextA` | 2 | 1 expected cold delete | 14.2899 ms |
| `CryptImportKey` | 1 | 0 | 0.8432 ms |
| `CryptCreateHash` | 846 | 0 | 1.5896 ms |
| `CryptHashData` | 846 | 0 | 39.6130 ms |
| `CryptVerifySignatureA` | 846 | 0 | 92.1625 ms |
| `CryptGetHashParam` | 1,692 | 0 | 0.4467 ms |
| `CryptDestroyHash` | 846 | 0 | 1.1357 ms |

The one hash creation, hash input, signature verification and hash destruction
per observed check, plus two successful hash-parameter reads per check, show
that provider/key reuse did not bypass the security decision. Physical provider
creation/import happened only for the cold fill; the cache suppressed the
qualified release and key-destroy calls after recording their logical lifetime.

The earlier X3/FEX run B measured the same 844-check batch at 12.835 s inside
`0x004cabc0`, including 10.346 s in 2,532 provider-acquisition calls. Run 17's
844-check bulk took 0.1353276 s in the same probe: 12.6997 s less, or about
94.85 times faster. This is direct acceptance of the targeted crypto-path gain
for this workload. It is separate from whole-load timing.

## Loading phases

`tools/analysis/analyze_loading_profile.py` streamed the final log and found no
sampling-profile blocks. Its presentation-gap labels are mechanical heuristics,
not stopwatch measurements:

| Gap | Interval after coverage start | Duration | Hooked exclusive time | Evidence |
| --- | ---: | ---: | ---: | --- |
| Unlabelled | 4.006–6.779 s | 2.610 s | 0.433 s | no phase rule matched |
| Main-menu load | 7.504–15.876 s | 8.142 s | 4.900 s | 1,017 adjacency calls and 299.19 MB texture-helper input |
| Save load | 23.517–51.183 s | **27.574 s** | 16.128 s | 14,461,803 `gzread` calls and one successful `gzopen` |
| Return/menu work | 63.625–70.866 s | 6.642 s | 4.200 s | 1,017 adjacency calls and 298.14 MB texture-helper input |

Within the save gap the analyzer counted 359,874 `inflate` calls, 3,342 mesh
creates and adjacency calls, and 2,358 resource-load probe entries/exits. Every
timed probe shown in the gap had matching calls and exits; all probe overflow
and desynchronisation totals were zero. Count-only probes such as
`read_dispatch` and `crt_fgetc` intentionally have no exit count. Ordinary
failed filename probes and the two `D3DXCleanMesh` failures remain engine/API
outcomes, not cache admission faults.

The closest documented same-save comparison is iteration 11: menu 8.386 s and
save 35.707 s, with the same 14,461,803 save-stream `gzread` calls, 359,874
inflates and 3,342 adjacency calls. Run 17's heuristic save gap is 8.133 s
(22.8%) shorter. The runs are not a controlled cache A/B: iteration 11 used an
older build with route, TAA, scene hook and gz read-ahead enabled, while run 17
disabled those rendering paths, enabled the crypto cache, and has no enabled
gz-buffer record. The whole-load difference is encouraging but cannot be
assigned wholly to the cache. The exact 844-check probe comparison above is the
sound performance result.

The derived machine-readable record is
`verification/results/game-run17-crypto-loading.json`. Raw logs and shader bytes
remain under `/tmp/x3-bottleX3-run17/` and are not tracked.

No crypto fix or DLL change is indicated before run group 2a. The next user run
can keep this installed build and exercise reader verification with `--direct
--telemetry --resource-read verify --dat-handles`; its result remains a separate
acceptance decision.
