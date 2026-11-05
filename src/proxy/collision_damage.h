#pragma once

// Collision damage (X3M_COLLISION_DAMAGE, schema builtin 1; docs/reverse-engineering/collision-damage.md, "Implementation"):
//   1 or unset = vanilla, nothing patched;
//   0 = no hull or shield damage from collisions for any ship, player and AI alike: the contact block's join (AP
//       0x0045eaf4 7 B, FL base+0x68a58 8 B) jumps to a stub that zeroes both damage values and replays the displaced
//       instructions, and the planet-atmosphere gate (AP 0x0045dbaf, FL base+0x678bb) becomes `xor` so vanilla's own
//       zero-damage branch is taken. The bounce, position restore, AttackedBy, MakeDamage(0), the damage routine's impact
//       sound and hit effect, CollisionWarn and force feedback stay; bullets and missiles never reach the contact block.
// Both editions (AP 0x400000; FL module base + RVA). Fails closed: the executable identity, the engine_patch install
// window and all of the edition's byte windows before any write; the module is pinned (the engine jumps into its stub);
// a failed step rolls the earlier one back. One collision_damage row at attach (edition, sites, applied|skipped reason=).
namespace x3m::collision_damage {
bool initialize();   // backend-load path only (the engine_patch install window)
bool shutdown();     // dynamic unload only: both sites back, each only over this feature's bytes
const char* state(); // ok / disabled / <refusal>
// Per Present: at most one collision_damage_suppressed row per second while contacts were zeroed since the last.
void present(unsigned long long frame);
}
