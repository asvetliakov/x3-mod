#pragma once
#include "weapon_projectile_family.h"

// The dev-only tuning file reader (weapon_projectile_tune_core.h; X3M_DEV_TUNING builds only, the release build has
// neither this reader nor its file path). Present thread, every frame: at most once a second GetFileAttributesExW on
// <game>\x3m\projectile_tune.json; a new last-write time reads the file into a fixed 1 KB buffer, parses it once and
// writes *tune (the fighter-tracer family) with one `projectile_tune reloaded` row, one `projectile_tune clamped` row
// per clamped key, or one `projectile_tune refused` row (the last values stay); a removed file returns to the baked
// values with one `projectile_tune reloaded ... source=defaults` row; a file that never existed writes nothing.
// LastError preserved. true when *tune changed.
namespace x3m::weapon_effects::projectile_tune {
bool poll(projectile_family::Tune* tune) noexcept;
}
