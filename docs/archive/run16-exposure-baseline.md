# Run 16: unresolved-input exposure counterfactual

**Offline policy comparison only; not new-meter game validation.** The seven
recovered FP16 images all produce a fresh target of **+2 EV** under the new
policy because they reach its upper clamp. The old policy produces
**+5.66 to +8 EV** on the same inputs. These results illustrate the value of
the narrower exposure range; they do not establish the visual quality or
stability of automatic exposure.

## What was recovered, and what is missing

`/tmp/x3-bottleX3-run16/recovered-hdr-inputs.json` records seven 1280×768,
7,864,320-byte `hdr_1_<frame>.rgba16f` files. The recovery matched filenames,
sizes and source modification times to successful `hdr_readback` records in
`session-20260913-000103-212.log`. SHA-256 values were measured at recovery;
the original log did not record hashes. Raw images remain local and untracked.

`MotionOutput::hdr_writeback` copies `hdr_->target()` before the write-back:
this is the **unresolved engine-gamma FP16 image**. All seven matching
`motion_output_frame` records say `taa_resolved=1 taa_hdr=1`. The real
write-back, and hence the new meter, instead samples `hdr_resolved_` after a
successful TAA resolve. No resolved or presented readbacks exist for this
snapshot. The recorded run used AgX, gamma2.2 decode and manual EV 0; it did
not run the new auto-exposure policy.

Consequently, these inputs cannot reconstruct the actual meter result,
deadband/adaptation trajectory, TAA history under a different exposure, or
the presented image. The calculation below applies the policy to the available
unresolved inputs as a counterfactual, not as a substitute for those missing
images. FP16 storage does not make the game's lighting scene-referred.

## Scalar comparison

The reviewed reference decodes engine RGB using gamma2.2, clamps luminance
to 1e-4…64, and reduces log2 luminance into 80×48 tiles (mean and maximum).
The new policy excludes tiles below 1/512, uses the centre-weighted lit median
for its key, and an unweighted p99 tile maximum for its highlight ceiling.
The table uses fresh targets, before deadband or temporal adaptation.

| Frame | Lit tiles | Old target EV | New key EV | Highlight ceiling EV | New fresh target EV |
|---|---:|---:|---:|---:|---:|
| 2615 | 27.45% | +8.00 | +4.54 | +4.19 | +2.00 |
| 2783 | 26.48% | +8.00 | +4.91 | +4.16 | +2.00 |
| 3003 | 9.09% | +8.00 | +5.22 | +4.48 | +2.00 |
| 3546 | 27.53% | +6.85 | +5.20 | +4.41 | +2.00 |
| 9222 | 68.33% | +7.01 | +4.04 | +4.49 | +2.00 |
| 9842 | 38.88% | +5.98 | +4.74 | +4.18 | +2.00 |
| 10020 | 37.81% | +5.66 | +4.86 | +4.24 | +2.00 |

The first three old targets also clamp: their unconstrained key requests are
+9.63, +9.87 and +9.37 EV. The old comparison continues the original
edge-clamped reduction to 1×1 and uses its ±8 EV range. At this resolution,
that reduction differs from simply averaging the new tile means.
Every new key and highlight ceiling remains above +2 EV, so this snapshot
does **not** demonstrate the lit statistic alone finding a satisfactory
unclamped exposure. The user-managed new-meter run remains necessary.

## Reproduction and checks

The authored [analysis command](../../tools/analysis/analyze_unresolved_exposure.py)
streams the log and binary inputs, verifies recovery sizes and hashes, rejects
failed/wrong-format records, non-basename paths, nonpositive dimensions,
nonfinite input and incompatible frame metadata, then calls the space-aware
Python reference. It stores only seven scalar summaries and provenance in
[the numeric report](../../verification/results/run16-unresolved-exposure.json).
JSON validation confirmed seven finite summaries and seven active +2 EV
clamps. Eight deliberately invalid metadata cases were rejected. The rerun
after the provenance review retained every scalar result exactly. No timing
or GPU-equivalence claim is made: the reference calculates in double precision.

Run against the exposure branch's reference until it is merged; the script
has no implicit import dependency on that branch:

```sh
python3 tools/analysis/analyze_unresolved_exposure.py /tmp/x3-bottleX3-run16 \
  --reference-dir .claude/worktrees/agent-aea55d854948bd6a0/tools/analysis \
  --output /tmp/run16-unresolved-exposure.json
```

After merging, omit `--reference-dir` to use main. The report records exact
SHA-256 values for `exposure_reference.py`, `agx_reference.py`, the analyzer,
and recovery manifest before importing the references; it refuses to emit
results if any changes during calculation. The analyzed reference was on the
worktree based at `ee7d2dc`. The raw input hashes are recovery provenance,
never feature-admission requirements.
