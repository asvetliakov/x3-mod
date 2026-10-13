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

## Answers (disassembly 2026-10-13, `ship-navigation-ai.md` §10.10)

1. **Capital off-axis pass: yes, bounded.** Order 5 ends when the ship's centre is within (|v|/10 + r_ship)·(1 + (tf−1)/4)
   of the stored order point `f+0x240` (the gate origin for a capital), checked every frame at `0x00454138..0x004546e3`;
   the leaf only gets a stack copy, so our redirected point does not move the trigger. A capital can therefore pass up to
   about r_ship + |v|/10 off the axis: 0.5 r_gate (552 m) holds for the M2/M1/TL medians and fails for hulls under 552 m.
   Small ships must pass within ≈ r_ship + |v|/10 of their point B on the 0.55 r arc (≈ 40 m for an M3), so their lane is
   the stored point the KC sets, so a small-ship lane split needs the KC's B or a write to `f+0x240`. "CanWarp" is player-only.
2. **Arrival placement: the engine's, and off-axis is possible at a new site.** The sector post pass launch
   `0x00444170` computes the exit point P (non-capital: a 15° fan by exit slot on the upper 0.55 r arc; capital: on the
   axis, 22 m up; both r_ship in front of the gate plane), sets the ship facing out, starts order 0x21 (900 ms, speed
   zeroed, ends at P at rest), then order 0x1e pushes it straight out at 120 m/s (large) / 240 m/s keeping its lateral
   offset until r_gate (large) / r_gate/4 clear. Nothing re-centres it. Site W never runs on an arrival (the old exit
   spread was inert). An offset of up to 0.6 r_gate written at `0x00444606` (`8b 43 70 | 8b 4b 50`) carries through;
   the only geometric limit is Terran rings (offset + r_ship ≤ 0.663 r). The arrival-side half-disc lane is therefore
   feasible for capitals and small ships alike.
3. **No advance signal for capitals** (their gate-pair slot is script data only). Non-capitals poll
   `SA_IsWarpOutSlotAvailable` and give lead time only when all 6 exit slots are taken. The earliest per-ship event is the
   `SA_StartInHangar` core `0x00443920`, 0–1 main-loop iterations before the launch; the ship reaches P 900 ms after it
   appears. So the runway-clear rule works from the appearance event, and a `ship_gate_launch` row at `0x00444606` settles
   the remaining inferred facts in one flight.

Consequence for the design: both halves of the lane split are feasible (outgoing capitals through an off-axis point, since
the trigger tolerates ≈ r_ship; arrivals placed off-axis at the new site), so the time rule reduces to the appearance
moment plus the two-Titan coincidence. Small-ship outgoing lanes need the stored order point, not only the leaf point.
