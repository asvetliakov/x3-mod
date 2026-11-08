# Weapon look layer, scratched 2026-11-08

Between 2026-11-06 and 2026-11-08 the weapon redo built a full look layer on top of the shield hit: hull hits with a
fireball burst, sparks, glow, ember, scorch, smoke and per-class anatomies (ring fronts, ion lightning, beam spots),
death explosions with a fire material, rays, sparks, a fire wave and the big tiers' clone row R', capsule bolts drawn in
place of the game's bullets, and per-class muzzle flashes, each with its programs, fixture cases and lab tab. After
flying Run246 the user judged every one of these looks wrong in kind, not in tuning: they were free-floating impostors
authored from design notes and gates, sized and timed by us and judged on synthetic scenes. The shield effect, bound to
the hull's own geometry (the bubble and the hull decal), was the one accepted look and stays in production.

The code is on branch `effects-look-archive-2026-11-08`; the notes below are the designs it implemented. The strip on
branch `weapons` kept the spawn-site hooks, the event record, classifier and weapon table, the census, the stage
framework and the shield bubble (ledger: [weapon-effects.md](../../verification/weapon-effects.md), "Look layer
scratched (2026-11-08)"). The redo starts from projectiles anchored to the game's own geometry.

| Note | Subject |
| --- | --- |
| [weapon-look.md](weapon-look.md), [weapon-look-fable.md](weapon-look-fable.md) | the shared look language and the step plan |
| [hull-burst-look.md](hull-burst-look.md) | the hull hit's burst, star flash, glow and smoke |
| [explosion-look.md](explosion-look.md), [explosion-references.md](explosion-references.md) | the death explosion and its references |
| [fire-material.md](fire-material.md) | the fireball family's fire material |
| [bolt-muzzle-look.md](bolt-muzzle-look.md), [bolt-muzzle-references.md](bolt-muzzle-references.md) | the bolts and muzzle flashes |
