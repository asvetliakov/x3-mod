# Gate traffic controller (queued design, after amendment 17)

Status: queued 2026-10-13 by user decision, to be designed after Run160 (amendment 17) flies. Not implemented.

## The problem it replaces

Gate transit is the KC story script's state machine (`docs/reverse-engineering/gate-queue-script.md`): a capital is
sent to a parking point on a 666 m grid, waits, rotates on the spot, then flies through along the axis. Our layer
(`capital-gate-transit.md`, amendments 1–17) owns only the point the ship flies to and its speed, and every amendment
so far has worked around the grid, the rotate and the single pass point. Crowds at one gate (Run158, Run159) are where
that breaks.

## Sketch

1. **Half-disc lanes, defined in the gate's own frame.** Outgoing ships pass through one half of the disc, arriving
   ships are placed and spread into the other half. Because each gate carries its own frame, the rule is identical on
   both sides of a link and needs no state shared between sectors (the far sector is not simulated in detail, so no
   controller can know an arrival in advance). Small ships: the pass point on the disc and the exit spread (site W)
   are already ours. Capitals: depends on §Open questions 1–2.
2. **Outgoing sequencer per gate, in the DLL.** Capitals committed to a gate get a sequence by arrival and a single
   funnel ending at the disc; while waiting they fly a slow wide holding loop (never a stop, never a rotate in place).
   The KC's grid point stays the nominal target; the funnel end is where the ship is when the KC issues the
   fly-through. Level 1: no script change, no new engine calls.
3. **Time rule only where space cannot separate.** If capitals must pass through the centre, or arrivals must appear
   there, a "runway clear" hold: an appearing ship or a capital inside the ring closes the funnel until it is one hull
   length off the axis; arrivals have priority (they cannot wait, a departing ship can loop once more). With off-centre
   passes and placements it shrinks to the rare two-Titan coincidence (disc 2 r_gate ≈ 2.2 km across, two hulls
   ≈ 1.2 km wide combined).
4. **Fast jump-in spread for arriving capitals** (scenario backlog item 2): move the appearing capital off the axis
   quickly.
5. **Level 2, if the script's rotate and wait are still the visible clunk after level 1:** the DLL issues the
   fly-through order itself once the ship is lined up, with a one-line clearance patch in a loose `L\x3story.obj`
   (`gate-queue-script.md` §6) so the script hands over instead of fighting. Needs the order-5 entry and the script's
   reaction disassembled first.

Rejected: teleporting capitals to or through the gate (visible, breaks the engine's warp-in placement and the script's
transit state); a full replacement of the gate script (the sector change, the far-side placement and the slot
bookkeeping are deep in script and engine; nothing gained over level 2).

## Open questions (disassembly dispatched 2026-10-13, to land in `ship-navigation-ai.md` §10.10)

1. What ends order 5 and triggers the transfer: distance to the order point, to the gate origin, a plane crossing, or
   a volume test? Can a capital pass 0.5 r_gate off the axis and still transfer?
2. Who places an arriving ship (engine 0x21 handler vs KC `GetWarpInPos`), and can a capital be placed off the axis at
   site W without breaking orientation, the order chain, the slot bookkeeping or the +z launch?
3. Is there any signal in the current sector that a ship is about to arrive, and if not, what is the earliest
   observable event?
