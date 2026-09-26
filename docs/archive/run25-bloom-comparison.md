# Run 25 B: bloom blocked by pure-device admission

Run 25 requested bloom, but the installed bloom pass did not attach. The game
created the D3D9 device with flags `0x00000052`: FPU preservation, pure device
and hardware vertex processing. The pass returns `D3DERR_NOTAVAILABLE`
(`0x8876086a`) when `D3DCREATE_PUREDEVICE` is present and reports
`reason=vertex_processing`. It retained zero bloom references and never emitted
a prepare, commit or frame row.

The user's report of no visible difference is therefore expected: this run
did not render bloom. It is a useful compatibility failure witness, not bloom
image-quality or performance acceptance. The automatic-exposure path remained
active and comparable to run 24 A, and all captured frames on both sides used
the same +2 EV target and applied exposure.

## Provenance and comparison settings

The verified helper snapshot is
`/tmp/x3-bottleX3-run25/session-20260913-201145-216.log`: 165,124,548 bytes,
3,088,452 lines and SHA-256
`abfc92b2ac4e74a68eaf38818ab59e2b04e8024b75fdf49287e1e71f78927adc`.
The helper matched it to the completed live log under
`/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures/`;
no second snapshot was made. The log reaches frame 14,560 and a last Present at
266.475 s, followed by complete motion/HDR summaries rather than a partial row.

The installed source remains checkpoint `10e447b`; the 12,884,608-byte
`d3d9.dll` has SHA-256
`d8f67c33e0139606f4624600ddc0b9ef95faa1abf14914329016e51f01dd6ee3`.
The intended A/B settings otherwise match [run 24 A](run24-exposure-baseline.md):
AgX with the automatic space-aware meter, gamma-2.2 decode, EV range -3 to +2,
zero EV offset, sharpen 0, mip bias 0, vanilla camera, linear materials off,
and motion/depth/TAA with eight-sample jitter. The combined loading accelerators
were also active. The sole requested rendering difference was bloom.

## Bloom admission result

The startup sequence is unambiguous:

```text
create_device adapter=0 flags=00000052 ...
create_device_result hr=00000000
bloom_attach device=1 result=8876086a reason=vertex_processing references=0
```

`0x52` includes `D3DCREATE_PUREDEVICE` (`0x10`) and
`D3DCREATE_HARDWARE_VERTEXPROCESSING` (`0x40`), so this is specifically the
pure-device guard, not absence of hardware vertex processing. The production
attach guard rejects pure or software vertex processing before allocating its
pass resources.

The final periodic admission row is emitted at entry to call 14,400, before that
call is classified. It accounts for the preceding 14,399 refusals:

| Refusal class | Count | Interpretation |
| --- | ---: | --- |
| Scene handoff not yet observed | 1,189 | Ordinary early calls before the renderer's qualified scene handoff. The first bounded refusal reports `ordinary_signal=1`. |
| Bloom pass unavailable | 13,210 | Calls after attach had already failed. The first bounded refusal also reports `ordinary_signal=1`. |
| Glow off | 0 | The game Glow state did not disable admission. |
| Caller, owner, device, nested, thread, reset, boundary or post-qualification | 0 each | None was the blocking class. |

There is no `bloom_prepare`, `bloom_commit`, `bloom_frame`, bloom timing metric
or successful bloom-device row. In particular, the required
`bloom_prepare ready=1` and `bloom_commit committed=1` acceptance evidence is
absent. This is an attach-level capability refusal rather than a mid-frame
fallback. A repeat can test bloom only after device creation provides the state
readback required by the pass and that change is qualified. The reviewed
[creation policy](../architecture/renderer-device-creation.md) removes only the
optional pure flag for the enhanced renderer; it retains the pass's guard
against unavailable state reads. This source correction is not yet installed.

## Automatic exposure remained active

The HDR attach and meter configuration exactly match A. All HDR self-tests,
meter resources, AgX write-back and the two-channel meter chain report success.
The menu/game separation is also healthy: frames 60--1,140 have no redirected
scene, meter status 1 and no adaptation step; the first active gameplay report
is frame 1,200 after the save gap. It has eight cumulative steps, target +2 and
applied EV +0.996, then converges through +1.819, +1.979 and +1.997 at frames
1,260, 1,320 and 1,380. By frame 1,440 it is effectively +2. Frames 14,460
onward return to meter status 1 and retain the last gameplay exposure without
stepping on menu pixels.

Across 241 active reports, the target is +2 in every one; applied exposure is
exactly +2 in 229 reports and is below it only during initial convergence. No
neutral-target episode like run 24's short sub-1%-lit interval occurred. Every
meter/readback result is successful and every HDR fallback counter is zero.

The five B capture bursts all remain at fresh/applied EV +2:

| Burst / time | `avg_log_l` | Mean luma | Lit tiles | p99 tile max | Key / highlight limit |
| --- | ---: | ---: | ---: | ---: | ---: |
| 2180--2183, 59.604--61.238 s | -10.715 | 0.0005949--0.0005951 | 23.44--23.52% | 0.800--0.817 | +4.57--4.58 / +4.16--4.20 |
| 3241--3244, 72.146--73.991 s | -10.466 to -10.441 | 0.0007072--0.0007194 | 28.20--29.06% | 0.690--0.734 | +4.51--4.60 / +4.32--4.41 |
| 3574--3577, 79.317--83.424 s | -11.496 to -11.369 | 0.0003462--0.0003782 | 20.31--21.93% | 0.347--0.399 | +5.30--5.40 / +5.20--5.40 |
| 7596--7599, 156.468--158.404 s | -11.549 to -11.469 | 0.0003336--0.0003528 | 16.56--17.71% | 0.927--0.950 | +4.89--4.99 / +3.95--3.98 |
| 10168--10171, 192.011--195.157 s | -9.768 to -9.762 | 0.001147--0.001152 | 40.94--41.15% | 0.685--0.695 | +4.76--4.79 / +4.40--4.42 |

Every raw key and highlight bound remains above +2, so the configured ceiling
clamps every B capture exactly as it did all 16 A captures. This establishes
exposure-state comparability. It does not establish pixel alignment: A has four
bursts at different frames/times and B has five, with different scalar scene
statistics. Since bloom never attached, differences between unmatched images
cannot be assigned to bloom.

All five B bursts are complete. Each of their 20 frames has six successful
1280x768 readbacks: HDR/TAA FP16, motion FP32, pre-resolve/present BGRA8 and
depth R32F. All 120 files exist and total 865,075,200 bytes. The snapshot also
contains 54 shader binaries (171,652 bytes), for 174 non-log files and
865,246,852 bytes total. Every captured frame has TAA resolved with valid
history and depth, no cut, and zero motion apply/restore failure.

## Loading and frame-time scope

The shorter B sequence has three mechanically identified gaps: 7.544 s for the
menu load, 19.626 s for the bulk save and 6.883 s for return/menu work. All
combined loading routes stayed admitted: the reader handled 3,950 of 4,075
calls, with 125 ordinary non-gzip fallbacks; the DAT pool reused 3,362 of 3,374
opens; adjacency computed all 6,230 calls with no fallback or fault; crypto
retained the same 2,538-acquire / 845-hit pattern; and the 14,461,803 logical
save reads used 175 physical gz reads without error. Local streamed analysis is
under `/tmp/x3-run25-loading-profile/`. Run 25 has no sector travel and does not
supply a controlled loading comparison with run 24.

After removing load and deliberate F8 spans, B has 189 one-second gameplay
`frame_normal` windows. Their median maximum is 19.369 ms, their 95th-percentile
maximum is 60.615 ms, and three consecutive windows at 88.618--92.033 s contain
0.415--0.489 s maxima. Run 24 has a different, longer interaction/sector
sequence, so its 376 windows and more frequent long stalls are not an A/B
performance comparison.

In the three long B windows, timed maxima are 4.611 ms for TAA, 0.989 ms for HDR
write-back, 0.921 ms for the exposure meter, 0.271 ms for meter readback,
0.171 ms for route fill and 0.158 ms for Present. No bloom operation ran or was
timed. Consequently run 25 measures neither bloom CPU/GPU cost nor bloom's
effect on game frame rate; it only measures the unchanged HDR/TAA path plus the
failed admission checks.
