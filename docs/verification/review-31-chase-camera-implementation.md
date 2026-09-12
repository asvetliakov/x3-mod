# Review 31b: chase camera implementation

Independent implementation review of the chase-camera prototype
(`X3M_CAMERA=chase`) on branch `worktree-agent-a0d6c2dbb3804c581`, reviewed at
commit `0296154`. Scope: the trampoline at `0x00420e06`, the pose pipeline, the
engine reads/writes, the build/style rules and the tests. The architecture and
the disassembly study are reviewed separately
([review-31-chase-camera-architecture.md](review-31-chase-camera-architecture.md)).

Nothing was installed into the bottle and the game was not launched. Evidence:
`docs/architecture/chase-camera.md`,
`docs/reverse-engineering/external-camera.md`, `src/proxy/chase_camera.{h,cpp}`,
`src/proxy/chase_camera_math.h`, `src/proxy/engine_patch.{h,cpp}`, the
`loader.cpp`/`capture.cpp`/`motion_output.cpp` wiring, `tools/manage.py`, and
the two test modules.

## Result

| Item | Outcome |
| --- | --- |
| `python3 -m unittest verification.analysis.test_chase_camera verification.analysis.test_chase_camera_site` | **35 tests, OK** (27 pipeline + 8 site; was 21 + 8, six added by this review) |
| `verification/probe/verify_chase_camera_site.py` (installed `X3AP.exe`, read-only) | **PASS** — sha256, size, pe, site_bytes, relocation, interior_branches, function_prologue, main_loop_order, atomic_write |
| `cmake --build build -j4` (mingw-i686, RelWithDebInfo, gcc 16.2.0) | clean, no warnings |
| `verification/probe/check_no_x87.py build/d3d9.dll` | **PASS**, 0 violations |
| `build/d3d9.dll` SHA-256 | `a95e3dd600050361e7f26090f2d72c5406dd2452041bc0413b8c8e81f42514ff` |

**Verdict: ready for a first user run** (`launch --direct --camera chase`), with
the open items below to be checked against that run's log rather than blocking
it. Every refusal path leaves the vanilla pose, the default is off, and the site
restores with the last device.

## 1. Trampoline at `0x00420e06` — verified

* **Relocation.** `engine_patch::claim` (`engine_patch.cpp:104-111`) recomputes
  the displaced `jz`'s rel32 as `target - (tail + rel32_offset + 4)` with
  `target = address + rel32_offset + 4 + original_rel`. For this site
  `0x00420e06 + 6 + 4 + 0x209 = 0x00421019`, matching the study. The
  **taken** path therefore reaches the same absolute address from the tail; the
  **not-taken** path falls through into the `e9` back-jump to
  `spec.address + spec.length = 0x00420e10`, the instruction after the site
  (`engine_patch.cpp:111`). `spec.rel32_offset + 4 <= spec.length` is enforced
  (`:96`) and the expected bytes still carry the *original* rel32, so the byte
  match stays exact. `test_rebased_rel32_keeps_the_target` pins the arithmetic.
* **Flags.** The order is handler → `cmp` → `jz`, not `cmp` → handler → `jz`:
  `push_front` puts the stub in front of the chain and the stub ends with
  `jmp [next]` into the tail, where the relocated `cmp` *recomputes* ZF
  immediately before the relocated `jz`. So the flags the `jz` consumes are the
  game's own, produced 2 instructions earlier, and could not be perturbed even
  if the stub leaked flags. The `pushfd`/`popfd` bracket is still correct
  (LIFO with `pushad`/`popad`) and preserves the incoming flags for anything
  else. No finding.
* **EBX contract.** `pushad` stores EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI at
  descending addresses, so from the block base the order is EDI, ESI, EBP, ESP,
  EBX, … and `regs[4]` is EBX (`chase_camera.cpp:76`). The pointer is
  `lea eax,[esp+0x80]` taken *after* `sub esp,0x80`, i.e. exactly the pushad
  block base. `handle()` only reads `regs`, so EBX reaches the relocated `cmp`
  unmodified.
* **SSE/alignment.** XMM0–7 are saved with **`movups`** (`0F 11 /r`, ModRM
  `0x44|(i<<3)`, SIB `0x24`, disp8 `16*i`; `disp8 <= 112` fits), so there is no
  16-byte requirement on the game's 4-byte-aligned stack and **no SSE
  instruction executes before an alignment is established** — there is none to
  establish. The C entry point carries `force_align_arg_pointer`
  (`chase_camera.cpp:162`) and the whole unit is built with `-mstackrealign
  -mincoming-stack-boundary=2` (`CMakeLists.txt:39`), so the handler's own
  `movaps` spills are aligned.
* **Stack discipline.** x86-32 has no red zone; the stub only touches memory
  *after* moving ESP (`sub esp,0x80` precedes the `movups` stores) and restores
  ESP symmetrically, so a nested interrupt or an APC cannot clobber live data.
  Conventional.
* **Size / arena.** Stub = 121 bytes emitted into a 160-byte reserve
  (1+1+6+40+7+1+5+3+40+6+1+1 = 112, plus a 6-byte `jmp [next]`, ≤3 pad bytes and
  the 4-byte continuation slot); tail + entry + dispatcher = 28 bytes of a
  33-byte reserve. Total ~150 bytes of the 8192-byte arena.
* **Install window / removal.** Claimed from `loader.cpp load_backend`
  (`loader.cpp:66-69`), before the device exists; `capture.cpp` closes the
  window at the first Present, after which `claim` returns `late_claim` and
  `initialize()` logs `installed=0`. `chase_camera::shutdown()` runs when the
  last device is destroyed (`capture.cpp:461`) and `engine_patch::restore`
  refuses with `restore_not_owned` if the five bytes are not ours.
* **Byte verification.** The in-process claim compares all ten bytes; the
  read-only probe additionally checks the executable identity, the function
  prologue `55 8b ec 83 e4 f0`, that the span decodes to exactly two whole
  instructions with exactly one relocatable rel32 at the declared offset, that
  no branch in `FUN_004205e0` targets `0x00420e07..0x00420e0f`, and the
  main-loop call order. Independently confirmed the five patched bytes straddle
  the qword at `0x00420e08`, so `atomic_write=0` (plain copy) is expected and
  is what the probe asserts.
* **No overlap.** The only engine address our sources patch within 16 KB of the
  site is the site itself; `scene_hook` (`0x004721b1`) is 0x51AB above, the
  object-trace callsite and every loading probe are further still. Checked by
  scanning every `0x004xxxxx`/`0x005xxxxx` literal in `src/proxy`.

## 2. The math (`chase_camera_math.h`) — verified

* **Closed form.** `x(t) = (x₀ + (v₀ + ωx₀)t)e^{−ωt}` and
  `v(t) = (v₀ − ω(v₀ + ωx₀)t)e^{−ωt}` are the exact critically damped solution
  (`A = x₀`, `B = v₀ + ωx₀`); `spring_step` (`:129-137`) implements both, and
  `test_spring_closed_form_direct` checks them against the analytic expression
  to 10 places. Frame-rate independence follows and is tested at 30/120 Hz.
* **Large angles / wrap at π.** `x` is **re-measured every frame** as
  `log(targetᵀ · B_c)` (`:268`) — the row-vector equivalent of
  `log(R_target·R_camᵀ)` — never integrated, so `|x| ≤ π` by construction and no
  wrap can accumulate. `log_rotation` handles the symmetric π case from the
  diagonal (`:86-93`); that branch was **untested** and is now covered
  (`test_rotation_vector_at_pi_takes_the_diagonal_axis_branch`,
  `test_rotation_vector_beyond_pi_comes_back_the_short_way`, plus a 179° ship
  flip through the pipeline).
* **Drift.** `B_c = target · exp(x)` is rebuilt each frame from freshly read
  engine state and re-orthonormalized (`:271-272`); the only state carried
  across frames is a 3-vector, so matrix drift is structurally impossible. A new
  10⁵-step run with continuous yaw and roll keeps `max|B·Bᵀ−I|` and `|det−1|`
  below 1e-9 and `|B_c − R_view'·B_ship|` below 1e-9.
* **Handedness.** Rows = right/up/forward in world space, row-vector products
  (`world = local × B`), left-handed with `right = up × forward` and `det = +1`
  (`orthonormalize`, `:44-56`). This matches `camera_reprojection.h`, which
  documents `view = world × V` with `V₃ₓ₃` the transpose of that basis, and the
  engine's own `camera.basis = R_view × B_ship` (study §3). `exp_rotation`
  emits the transpose of the column-form Rodrigues matrix, which is the correct
  row-vector form; `log_rotation` inverts it (`k_x ∝ W[1][2] − W[2][1]`).
* **Lag clamp.** `clamp_spring` (`:140-148`) clamps `|x|`, the norm of the
  rotation vector — not per axis — and removes the outward velocity component so
  the clamp does not fight the spring. Same shape for the boom.
* **Boom spring in the ship frame.** `x_p = o_prev − o_t` with both expressed in
  world but *derived from the ship frame*, so constant velocity produces zero lag
  (tested) and only turns and boom changes swing.
* **Distance scaling.** It is **not** derived from ship size: `distance_scale`
  multiplies the vanilla boom `p_v − p_ship` (`:258-259`), and the vanilla boom
  is the script-set cockpit `+0x130` the engine already sizes per ship class.
  Units are engine position units (int32), kept exact in doubles.
* **`offset_y`.** Applied as a local pitch of the camera basis about its own
  right axis, `δ = atan(offset_y · tan(half vfov))` (`:254-255`), which is a
  view-space displacement: the test asserts the ship lands at exactly
  `−offset_y` of the half screen height.
* **dt.** Clamped into `[0, max_dt]` (`:261`); `dt = 0` holds the state
  (`spring_step:131`, tested), a negative dt is treated as zero (test added), a
  huge dt after a menu is clamped to one `max_dt` step without snapping and the
  spring keeps converging (test added). `dt` is wall clock
  (`QueryPerformanceCounter`), so SETA does not enter (tested).
* **Guards.** Every engine-read input is checked: `finite()` on `ship_pos`,
  `vanilla_pos`, `vanilla_cam`, `view_rel`, on `half_vfov_tan` (and `> 0`) and
  on `dt` (`:224`); both bases must be within `max_orthonormality_error` of a
  proper rotation. Every output is checked: `to_int`/`to_fixed` reject
  out-of-int32 values before any write (`chase_camera.cpp:128`), and the state
  is re-tested for finiteness (`:281`) with a `NumericFailure` reset.
* **Mirror / determinant.** `orthonormality_error` includes `|det − 1|`
  (`:58-62`), so a mirrored basis is refused; tested with a `det = −1` input.

## 3. Engine reads and writes — verified

* Cockpit, camera, ref object and node pointers are non-null and 4-aligned (see
  F4), and every read goes through `engine_memory::read` with a bounded,
  validated span. Every read offset lies inside its declared block
  (`+0x1fc + 4 = 0x200`, `+0x304 + 4 <= 0x310`, `node +0xc0..+0xf0` inside the
  0x40-byte block read from `+0xb0`).
* Writes are exactly the documented fields: camera `+0x30/+0x34/+0x38`
  (position), camera `+0x40/+0x50/+0x60` and cockpit `+0xf0/+0x100/+0x110`
  (three int32 of each 16-byte row). The original 48 bytes are read first and
  only the first three words of each row are replaced, so the fourth words
  survive verbatim (`chase_camera.cpp:127-133`). Layout is 16.16 int32 rows,
  matching study §3/§5. Both destinations are re-validated as committed
  writable memory before the first byte is written.
* The writes happen in one handler call on the game thread between the pose
  being final and the engine's camera-relative object loop, so the frame cannot
  observe a partial update. Confirmed from the study that nothing after the
  site rewrites `+0x30..+0x3c` or `+0x40..+0x6f`, and that the engine rebuilds
  `+0xf0` from the view angles each frame — so there is **no feedback loop**
  through our own writes.
* No write outside the external back view: view mode 1 → verdict 1, connect
  modes 4/5/6/8/9 → verdict 3, the geometric back-view test → verdict 2, and a
  write refusal counts `write_refused` and takes a gap.
* All six designed snap reasons are implemented (first/ref/sector/mode/teleport/
  numeric) and tested; docking is covered by the connect-mode change. On a snap
  the state is set to the target and `snap_pending` is raised, which
  `motion_output.cpp:701-703` folds into both `in.cut` and `t.camera_cut` for
  that resolve — the TAA cut flag is set on the snap frame.
* Logging: `handle()` logs **nothing**, so the plain path has no per-frame
  output. `report()` is called from the `frame_end` block, which is already
  gated on a capture frame or `frame % 300 == 0` (`capture.cpp:519`), and
  returns immediately when the site is not patched. The telemetry line carries
  the verdict, snap reason, view/connect mode, `lag_deg`, `pos_lag`,
  `distance`, `dt_ms` and `basis_dev_deg`.

## 4. Build and style — verified

`chase_camera.cpp` is in the `d3d9` target and inherits `-msse2 -mfpmath=sse
-mstackrealign -mincoming-stack-boundary=2`; the handler's C entry adds
`force_align_arg_pointer`. No allocation on the frame path (three stack buffers,
~1.4 KB) and no lock other than the diagnostics SRWLOCK (see O5). Functions and
the site are documented at the declaration. No generated include fragments, so
the `*_inc.h` rule does not apply. `manage.py` validates the flags and
`--dry-run` prints the environment; defaults are off (`X3M_CAMERA` unset, or
`vanilla`, patches nothing — `wanted()` requires exactly `chase`). `env_double`
uses a `wcstod` end-pointer check and every value is then range-checked by
`chase::valid()`, which also rejects `nan`/`inf` spellings on every tunable.
`check_no_x87.py` stays PASS (its roots are the light-boundary hooks; this
handler runs under the full boundary and is correctly outside that rule — see
O10).

## 5. Findings

### Fixed in this commit

| # | Severity | Where | Finding |
| --- | --- | --- | --- |
| F1 | medium | `tools/manage.py:152-158` | `--camera-offset-y` was validated as `0 ≤ v ≤ 1`, rejecting the documented and DLL-accepted negative half of `[−1, 1]` (the range test keyed off the variable *name* suffix and `_Y` fell into the default branch). Replaced with an explicit per-variable range table mirroring `chase::valid()`; `--camera-offset-y=-0.5` now passes and `1.5` is refused with the range in the message. |
| F2 | medium | `src/proxy/chase_camera.cpp:172` | `PreserveCpuState::capture()` is `fnsave; frstor`, so the handler **inherits the game's x87 stack**. This is a mid-function site, not a call boundary, and the pipeline reaches x87 code: `objdump` shows 10 `fabs` (gcc's `fldl/fabs/fstpl` expansion of `std::fabs`), `fildll/fadds`, and the libm `exp/acos/atan/tan` calls are x87 throughout. A non-empty inherited stack would push our values into indefinites — or fault if the game's control word unmasks invalid-operation. Added `fninit` after the save so our code starts from an empty, fully masked, round-to-nearest FPU; the destructor's `FRSTOR` puts the game's tags, registers and control word back before the relocated `cmp`/`jz`. Verified the instructions preceding `0x00420e06` are a 16-byte integer copy, so this is defence in depth rather than a live bug. |
| F3 | medium | `src/proxy/chase_camera.cpp:82-83` | One global `pipeline`/`qpc_last` served **every** cockpit the registry walk `0x0041cde0` updates. Two cockpit objects would interleave their poses and split one frame's dt into the same springs. Added a `last_cockpit` identity guard: a change of the cockpit pointer is a gap, so the next frame snaps instead of mixing state. Single-cockpit behaviour is unchanged. |
| F4 | low | `src/proxy/chase_camera.cpp:86` | `ref_object` was the only engine pointer not checked for 4-alignment (camera and node were), contrary to the contract in `chase_camera.h`. Now `((camera \| ref_object) & 3) == 0`. |
| F5 | low | `src/proxy/chase_camera.cpp:200` | `while (here < next) e.byte(0xcc)` in `emit_stub` spins forever if the `Emitter` ever exhausts its reserve, because an exhausted emitter reports `here() == nullptr < next`. Unreachable today (121 of 160 bytes) but now bounded by `e.ok()`. |
| F6 | low | `docs/architecture/chase-camera.md` | The critically damped step reaches 63 % in ≈2.15τ, not 1.7τ ( `(1+u)e^{−u} = 0.37 ⇒ u ≈ 2.15` ); 95 % in ≈4.75τ was right. Corrected, and the doc now records the `fninit`, the per-cockpit state rule and the signed `offset_y`. |

### Open — recommendations

| # | Severity | Where | Recommendation |
| --- | --- | --- | --- |
| O1 | medium | `chase_camera_math.h:268-271` | The orientation spring's velocity `rot.v` is a world-frame vector that is **not transported** when the target basis rotates between frames. The position `x` is re-measured (correct), but `v` keeps its old world direction. Second-order at the 10° default clamp; if `lag_clamp_deg` is tuned much above ~30° expect a small extra swing. Fix if it shows: rotate `v` by `log(target_prevᵀ·target)` before the step. |
| O2 | medium | `chase_camera.cpp:117-120` | `half_vfov_tan = tan(π·fov298/65536) × H` with `H` from `camera+0x304` is a static inference; the study only proves `+0x298` is a binary angle. It feeds only the `offset_y` pitch, so a wrong `H` mis-frames the ship by a few degrees rather than breaking the view. Add `half_vfov_tan` to the per-300-frame line and check it against the known 4:3/16:9 values on the first run. |
| O3 | medium | `chase_camera_math.h:209-215` | `back_view`'s constants (0.6, 1.5, 0.7) have no runtime evidence. A back view whose script places the boom high passes through as verdict 2, and an unexpected external view could be captured. Log the observed `boom_local` on the first run before tightening or loosening them. |
| O4 | low | `chase_camera.cpp:53-56` | `writable_cached` caches only the positive result, keyed on the raw pointer, and is never invalidated; a freed-and-reallocated cockpit or camera would be written through. Also re-queries `VirtualQuery` every frame on the refusing path. Consider keying it to `engine_memory`'s frame epoch if those objects ever prove short-lived. |
| O5 | low | `chase_camera.cpp:101, 141` | The handler takes `stats_lock` exclusively once per frame on the game thread. Uncontended (tens of ns), but the diagnostics could be relaxed atomics if it ever shows in a profile. |
| O6 | low | `chase_camera_math.h:248` | The teleport threshold is `snap_ratio × boom` **per frame**, not per second, so it scales with frame time and boom length. At the 20× default and a ~200-unit boom that is ~4000 units/frame — far above X3 flight speeds and far below a gate jump, so the discrimination is safe; it is just not a velocity. Worth a sentence in the doc. |
| O7 | low | `tools/manage.py`, `src/proxy/camera_state.cpp` | `X3M_CAMERA_*` now names two unrelated subsystems: the pre-existing TAA `X3M_CAMERA_CUT_DEG`/`X3M_CAMERA_LOG` and the chase camera's tunables; `--dry-run` prints them side by side. Consider renaming the new ones to `X3M_CHASE_*` before the first user run, while nothing depends on them. |
| O8 | low | `chase_camera.cpp:57-65` | `env_double` silently returns the default for an empty variable (`n == 0` is indistinguishable from "not set") and for one longer than 63 characters. Not reachable through `manage.py`; a hand-set `X3M_CAMERA_ROT_TAU=` would be ignored rather than refused. |
| O9 | low | `chase_camera.cpp:41, 149-152` | `basis_dev_deg` is refreshed only on an applied frame, so the reported value is stale in the pass-through verdicts. Cosmetic — the reader can tell from `verdict`. |
| O10 | info | build | `chase_camera.cpp.obj` contains x87 despite `-msse2 -mfpmath=sse` (gcc expands `std::fabs` through the x87 stack; doubles returned by non-inlined helpers go in `st(0)`; libm transcendentals are x87). `camera_state.cpp` and `motion_output.cpp` are the same, and `check_no_x87.py` — which only walks the light-boundary hooks — stays PASS. Not a defect given the full boundary and F2, but the plan's "SSE2-only object code" claim should be stated as "SSE2 arithmetic under the full CPU boundary". |

## 6. Test coverage

`test_chase_camera.py` covered the first-frame snap, the closed-form step
response, frame-rate/SETA independence, both clamps, the five snap reasons, the
pass-through verdicts, the NaN/mirror guards, `distance_scale`, the `offset_y`
framing, the `camera = view_rel × ship` identity and the exp/log + 16.16 round
trip. Gaps found and closed (six tests, `21 → 27`):

1. `test_rotation_vector_at_pi_takes_the_diagonal_axis_branch` — the previously
   dead `log_rotation` branch for the symmetric matrix at `|r| = π`, over five
   axes and three angles, checking the recovered axis up to the inherent sign.
2. `test_rotation_vector_beyond_pi_comes_back_the_short_way` — a 200° rotation
   vector must come back as 160° about the opposite axis.
3. `test_half_turn_of_the_ship_stays_within_the_clamp_and_converges` — a 179°
   instantaneous ship flip stays inside the clamp, stays orthonormal and
   converges.
4. `test_negative_dt_is_treated_as_no_time` — a backwards QPC step.
5. `test_pause_then_resume_clamps_the_first_step_and_keeps_tracking` — a 30 s
   gap is clamped to one `max_dt` step, does not snap, produces no NaN, and the
   spring keeps converging.
6. `test_orthonormality_and_the_identity_hold_over_100k_steps` — 10⁵ frames
   (~28 min at 60 Hz) of continuous yaw + roll: 0 refusals, exactly 1 snap
   (the first frame), `max|B·Bᵀ−I| < 1e-9`, the lag never exceeds the clamp and
   never collapses to zero, and `|B_c − R_view'·B_ship| < 1e-9` throughout.

The last one needed a new in-process `L steps dt yaw_rate roll_rate` command in
`verification/probe/chase_camera_host.cpp` (the stdin pipe would otherwise
dominate); it flies the ship along its own forward axis rather than around a
growing radius, which is what the teleport snap threshold expects.

Remaining, deliberately not added: a Wine `rel32_offset` case in the
engine-patch fixture chain (already the design's first follow-up — it would be
the only coverage of the relocated `jz` actually executing), and any test of
`handle()` itself, which needs the process.
