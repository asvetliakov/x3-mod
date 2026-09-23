# LOD threshold scale (`--lod-scale`, `X3M_LOD_SCALE`)

Default off. `tools/manage.py launch --lod-scale <factor>` (0.25..4.0) scales
every mesh LOD switch distance of the engine's threshold loop by `factor`;
a factor below 1 pulls the switch distances in instead (`mirror = game / factor`
grows, so the truncated thresholds `T_i` only get larger and cannot collapse),
which trades detail on far objects for fewer draws;
without the option nothing is patched and `X3M_LOD_SCALE` is dropped from the
child environment. It needs no other option. Evidence and the ranking of the
alternatives: [lod-selection.md](../reverse-engineering/lod-selection.md).
Code: `src/proxy/lod_scale.{h,cpp}`, `src/proxy/lod_scale_core.h`; probe
`verification/probe/verify_lod_scale_site.py`; test
`verification/analysis/test_lod_scale_patch.py`.

## What it changes

The loop `0047d429..0047d46e` selects LOD `i` when `s < (int)(LODrec_i[+0x34] * f)`
with `f = *(0x606f34)+0x760`, a float the device bring-up `004d8f10` writes once
from the shader-quality setting (1.0 on any ps_2_0+ adapter, up to 1.4). The
switch distance is proportional to `1/f`, so the proxy feeds the multiply
`f / factor` instead of `f`. Nothing else in the image reads `+0x760`; the
fixed-distance branch for parentless `+0x12c & 0x80000000` nodes never does and
stays where it is.

## The patch (option 1 of the note)

| Item | Value |
| --- | --- |
| Site | `0x0047d44b`, `d8 89 60 07 00 00` = `fmul dword [ecx+0x760]` |
| Replacement | `d8 0d <disp32>` = `fmul dword [mirror]`, six bytes, `mirror` a 4-aligned `std::atomic<uint32_t>` in the DLL's data (32-bit image, any static reaches by disp32) |
| Boundaries | same length; the next instruction stays `call 0x0052b5d0` at `0x0047d451` (probe check `next_instruction_boundary`) |
| CPU state | FMUL m32 for FMUL m32: no general register, EFLAGS or x87 stack change; ECX is dead after the original load anyway |
| Validation | factor parsed locale-independently (`[+]digits[.digits]`, `invalid_factor` otherwise, the raw setting logged) and in [0.25, 4]; `engine_patch` install window open; executable identity (`object_trace::executable_verified`: structure, global anchors, size, base; no file hash, `docs/reverse-engineering/executable-identity.md`); the 17-byte window `0047d440..0047d450` (`8b 03 / db 40 34 / 8b 0d 34 6f 60 00 / d8 89 60 07 00 00`); the config pointer `*(0x606f34)` readable with its `+0x760` word (`config_unreadable` otherwise); this DLL pinned (`pin_failed` otherwise) |
| Write | on the backend-load path (`initialize_log`, after the game-phase claims, before the device exists): `VirtualProtect`, `engine_patch::write_code` (plain copy: the span crosses the qword at `0x47d450`, acceptable only inside the install window), `FlushInstructionCache`, read-back compare, protection restored; any failed step rolls the original bytes back (`patch_rolled_back`) or, if even that fails, keeps the site registered (`rollback_failed`) for `shutdown()` |
| Mirror | seeded with the game's current `+0x760` bits (the vanilla operand) before the patch is live; refreshed at every `BeginScene` and `Present` (two bounded `engine_memory::read`s, one aligned store only when the game value changed) and after every `Reset`, in case the bring-up path re-runs |
| Rule | game value finite and in `[1.0, 1.4]` -> `mirror = game / factor` (applied); otherwise `mirror = the game's own bits` (the multiply gives the vanilla result). At install the game value is normally not yet written, so the install line reports `reason=game_value_pending` and the first refresh after `004d8f10` logs `lod_scale_value … applied=<factor>` |
| Log | `lod_scale requested=<raw setting> applied=<f or 0> game_value=<v> proxy_value=<v> patched=<1/0> reason=<ok/game_value_pending/invalid_factor/late_claim/executable_mismatch/bytes_mismatch/config_unreadable/pin_failed/protect_failed/patch_rolled_back/rollback_failed> write=<none/atomic/plain>` once at install (`plain` is expected: the span crosses a qword, see Write); `lod_scale_value game_value= proxy_value= applied=` on each change of the game value (at most 16 lines) |

"Fail closed" is the vanilla multiply: with a mirror that holds the game's own
bits the patched instruction computes exactly what the original would, so an
unwritten or out-of-band value never scales anything. If the config
struct or its word is not readable on the load path, nothing is patched.

## Ordering of the bring-up write and the first LOD pass

`+0x760` is written by `004d8f10`, called once from the device-creation path
`004dac90` at `004db058` after `CreateDevice` (the pixel-shader profile probe
needs the device). The threshold loop lives in `0047cfe0`, reached from the
frame routine `00471f50`, which the main loop calls at `00403f34` (the
`render` phase marker) after the device exists. The proxy therefore sees the
written value at the first `BeginScene` of the device, which the frame routine
issues before its scene draws; that refresh is the evidence-backed hook, and
`Present`/`Reset` refreshes cover any later rewrite. Whether `004dac90` runs
again on a resolution change is not established (lod-selection.md, "Unknown");
if it does, at most the LOD pass of one frame runs on the previous mirror,
which was itself a valid in-band value. Not established either: that no LOD
pass runs between the write and the first `BeginScene`; such a pass would use
the pre-write bits (typically 0, the unwritten word), which vanilla would have
consumed identically only before the write.

## Lifetime

Once the patch is live the DLL is pinned (`GetModuleHandleExW` with
`GET_MODULE_HANDLE_EX_FLAG_PIN`, documented), so the absolute operand the game
executes can never point into freed memory; the pin is taken before the write
and its failure refuses the patch. `DllMain(DLL_PROCESS_DETACH)` restores the
six bytes only for a dynamic unload (`lpReserved == NULL`, unreachable once
pinned); at process exit (`lpReserved != NULL`) every other thread has already
been terminated and the code is left as it is, so no executable memory is
rewritten under the loader lock at exit.

## Cost and the cap

The whole ladder shifts together: an object at distance `D` draws what an
object at `D/factor` draws today. From the capture joins in the note, a factor
of 2-3 costs about **4-7x the triangles and 13-15x the draw calls per distant
station body** (`542a`: 1 draw/1 842 tri at LOD 3 against 15 draws/11 538 tri
at LOD 2) and about 2x per large ship crossing the 0/1 step. The draw-call
multiplication also multiplies the proxy's per-draw motion/material work.

The cap of 4 is a guard, not a measurement: `T_i = (int)(LODrec[+0x34] * f)`
is integer-truncated and the record values were not read, so a small record
(the engine guards `< 2` at `0047d3ea`) divided far enough becomes `T = 0` and
that level is never selectable, collapsing the node to LOD 0. Raise the cap
only after the record values are known.

## In game (run 22)

First gameplay run of the patch (snapshot `/tmp/x3-bottleX3-run51/`, log
`session-20260915-030036-468.log`, installed DLL `53a0d8a7…` from `509a273`).
The install line is
`lod_scale requested=2.0 applied=0 game_value=0 proxy_value=0 patched=1 reason=game_value_pending write=plain`,
the expected pending form, followed by exactly one
`lod_scale_value game_value=1 proxy_value=0.5 applied=2`: the bring-up wrote
1.0, the mirror took `1/2` and the scale stayed applied for the rest of the
session. The ordering above is therefore what the game does.

`object_context` LOD shares, run 51 (n=5,699) against run 49 (n=23,737, the
unscaled build):

| LOD | Run 51 (2×) | Run 49 (1×) |
| ---: | ---: | ---: |
| 0 | 94.9 % | 87.4 % |
| 1 | 0 % | 5.5 % |
| 2 | 4.8 % | 6.4 % |
| 3 | 0.28 % | 0.64 % |

The shift is in the expected direction, but the two sessions flew different
scenes, so this is not a controlled comparison. The user calls the detail
"slightly better" and asks for 3×; run 23 repeats the run-49 route at
`--lod-scale 3` for a before/after on one route, with the frame cost
([motion-output.md](../verification/motion-output.md), "User run 22").

## Portability

A memory patch of the same non-relocatable EXE with `VirtualProtect`,
`FlushInstructionCache` and `ReadProcessMemory` on the own process: native
Windows behaves identically by construction (Windows-compatible source; not
run natively, like the other patches in `platform-portability.md`).
