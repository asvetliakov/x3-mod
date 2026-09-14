# LOD threshold scale (`--lod-scale`, `X3M_LOD_SCALE`)

Default off. `tools/manage.py launch --lod-scale <factor>` (1.0..4.0) pushes
every mesh LOD switch distance of the engine's threshold loop out by `factor`;
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
| Validation | exact executable (`object_trace::executable_verified`, SHA-256 `fdbf3418…`, size, base 0x400000), the 17-byte window `0047d440..0047d450` (`8b 03 / db 40 34 / 8b 0d 34 6f 60 00 / d8 89 60 07 00 00`), factor finite in [1, 4], `engine_patch` install window open |
| Write | on the backend-load path (`initialize_log`, after the game-phase claims, before the device exists): `VirtualProtect`, `engine_patch::write_code` (plain copy: the span crosses the qword at `0x47d450`, acceptable only inside the install window), `FlushInstructionCache`, read-back compare, protection restored; any failed step rolls the original bytes back (`patch_rolled_back`) or, if even that fails, keeps the site registered (`rollback_failed`) for `shutdown()` |
| Mirror | written before the patch is live; refreshed at every `Present` (two bounded `engine_memory::read`s, one aligned store only when the game value changed) and after every `Reset`, in case the bring-up path re-runs |
| Rule | game value finite and in `[1.0, 1.4]` -> `mirror = game / factor` (applied); otherwise `mirror = the game's own bits` (the multiply gives the vanilla result). At install the game value is normally not yet written, so the install line reports `reason=game_value_pending` and the first Present after `004d8f10` logs `lod_scale_value … applied=<factor>` |
| Restore | `DLL_PROCESS_DETACH` -> `lod_scale::shutdown()`: refuses bytes it does not own, else the six original bytes return under the same VirtualProtect/Flush discipline |
| Log | `lod_scale requested=<f> applied=<f or 0> game_value=<v> proxy_value=<v> patched=<1/0> reason=<ok/game_value_pending/factor_out_of_range/late_claim/executable_mismatch/bytes_mismatch/protect_failed/patch_rolled_back/rollback_failed>` once at install; `lod_scale_value game_value= proxy_value= applied=` on each change of the game value (at most 16 lines) |

"Fail closed" is the vanilla multiply: with a mirror that holds the game's own
bits the patched instruction computes exactly what the original would, so an
unwritten or out-of-band value never scales anything. Before the config struct
exists the mirror holds 1.0, the engine's own first constant (`004d97c9`); the
loop cannot run without a device, so that value is never consumed.

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

## Portability

A memory patch of the same non-relocatable EXE with `VirtualProtect`,
`FlushInstructionCache` and `ReadProcessMemory` on the own process: native
Windows behaves identically by construction (Windows-compatible source; not
run natively, like the other patches in `platform-portability.md`).
