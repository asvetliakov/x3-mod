# Review 47: production bloom programs

The frozen production bundle is approved with no blocking findings. It contains
nine authored SM3 programs: six extraction variants, downsample, upsample and
the selected bloom-plus-AgX composition shader. The bundle stores 7,047 DWORDs
(28,188 bytes). Existing quad, TAA-sharpen and HDR-writeback programs are reused
through typed accessors and were not recompiled.

## Independent audit

The final command was:

```text
python3 verification/probe/wine_lock.py --holder bloom-programs --timeout 1 python3 tools/shaders/generate_bloom_programs.py --check
```

It exited zero and compiled exactly nine programs. The terminal result reports
`passed=true`, `mode=check`, `game_launched=false` and
`old_programs_recompiled=false`. The retained summary SHA-256 is
`283ca8d2404e341ee596325bf6c75b6c9591f26ee77cf4a2b0dc4c29f81e6fc9`; its
183-byte terminal log matches the recorded
`0f09dca185ac8c040cc7cec8e61b0cc1cac7191be471614b55dd3fe682b6d688` hash.
All 50 before/after inputs were stable, and an independent post-run pass found
all 50 current files at those hashes.

The reviewer reconstructed every DWORD array from the nine production headers.
Word counts and raw bytecode hashes match both the production manifests and the
retained native compiler records. Each production manifest preserves its native
compiler, source, include and tool provenance. Eight programs resolve to the
accepted filter summary `1f4b943c…0afe`; the composition program resolves to
`158534bc…83b4`, whose selected 108-slot `bloom_agx` candidate passed. The
rejected 597-slot fused diagnostic was neither selected nor promoted. The
largest production program remains the 362-slot sRGB generic extractor under
the 512-slot SM3 limit.

The five host tests for packaging, ordering, DWORD identity, tamper rejection
and retained-input drift passed against this frozen bundle. The production
header also passed the prior i686 constexpr/type/frame compilation check.

The compact [result record](../../verification/results/bloom-programs-check.json)
preserves the full 50-input hash map, terminal provenance, per-program bytecode
identity and both promotion-summary hashes.

## Scope

This qualifies reproducible packaging and native D3DX compilation on the tested
Steam-bottle compiler path. The check creates no D3D device, launches no game,
and builds or installs no renderer DLL. GPU execution, game integration and
behavior on native Windows remain separate verification work.
