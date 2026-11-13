# Custom projectile stage and muzzle flash, removed 2026-11-13

From FL 58 A to FL 63 A (2026-11-08 to 2026-11-09) the projectile redo drew FL's bolts and muzzle flashes itself: the
IRE as a line of light (core, glow, hot centre, a streak bridged from the previous frame's head, the comet trail), from
a pool of live bullets the object tap registered, with the game's instanced bullet batch skipped per body and the lone
draws of those bodies dropped in the draw hook; and a muzzle flash of its own (a stage flash anchored to the bolt's
birth, a hull pulse on the surface behind the gun, the muzzle stub skipping the game's flash). On 2026-11-09 the user
chose the re-shade of the game's own draws instead (the bullet batch and lone draws, the flash draw:
`weapon_effects = reshade`, accepted through FL 72 A). The custom path ran in FL's other stage modes only and read
only the dev tuning file's legacy keys; it was removed on 2026-11-13.

**Last commit holding the code: `04d518bf3`** (branch `reshade`). Git history keeps it; `git show 04d518bf3:<path>`
restores any file below.

| Note | Subject |
| --- | --- |
| [projectile-look-custom.md](projectile-look-custom.md) | the fighter-tracer family, its look, anchor, calibration, verification, budget, implementation, options and unknowns (the custom sections of `docs/architecture/projectile-look.md`) |
| [muzzle-flash-look-custom.md](muzzle-flash-look-custom.md) | the custom muzzle flash, its hull pulse and the game-flash skip (the "Superseded 2026-11-09" section of `docs/architecture/muzzle-flash-look.md`) |

Files removed (as of `04d518bf3`):

| Area | Files |
| --- | --- |
| Stage and pool | `src/proxy/weapon_projectile_stage.h`, `src/proxy/weapon_effects_projectile.h`; `MotionOutput::run_projectiles`, `projectile_drop`, `projectile_take_frame` (`motion_output_weapon_effects_inc.h`) and their members |
| Programs | `src/effects/weapon_projectile_{vs,ps}.hlsl`, `src/effects/weapon_pulse_ps.hlsl`, `src/renderer/weapon_projectile_{vertex,pixel}_program_inc.h`, `src/renderer/weapon_pulse_pixel_program_inc.h`, `verification/results/weapon-{projectile-vertex,projectile-pixel,pulse-pixel}-program.json`; `WeaponEffectsPass::run_projectiles` and the projectile objects and caps (`src/renderer/weapon_effects_pass.{h,cpp}`) |
| Hooks | the FL batch skip stub `x3m_weapon_effects_stub_batch_fl` and `x3m_projectile_pool`, the object tap's registration (`projectile_tap`), the muzzle stub's skip (`projectile_muzzle`), the stage modes' claims of the object tap, the muzzle and the batch (`src/proxy/weapon_effects_hook.cpp`, `weapon_effects_patch.{h,cpp}`) |
| Rows | `projectile_site`, `projectile_rows`, `projectile_device`, `projectile_state`, the `projectile_*`, `muzzle_skipped/unskipped/evicted`, `flash_*` and `pulse_*` fields of FL's `weapon_effects_frame` |
| Tuning | the families, enabled rows and the fourteen keys `core_px` .. `hull_pulse_r` of `src/proxy/weapon_projectile_family.h` (now unknown keys of the dev file, logged once) |
| Fixtures | the D3D fixture's `projectiles` and `muzzle_flash` sections and the pulse ghost (`PROJECTILE`, `FLASH`, `PULSE*` rows, `verification/probe/weapon_effects_fixture.cpp`), the projectile look dump (`weapon_effects_projectile_dump.cpp` keeps the re-shade dumps), the FL hook fixture's batch-skip, pool and muzzle-skip cases (`weapon_effects_hook_fixture_fl.cpp`), the host test's family resolution and legacy key cases |

The evidence of those flights stays where it was recorded: the ledger [weapon-effects.md](../../verification/weapon-effects.md)
(FL 58 A to FL 63 A), `verification/results/projectile-look/` and `verification/results/muzzle-flash-look/`.
