# Run53B fog-card flicker triage

Source: `/tmp/x3-bottleX3-run194/session-20260920-215930-212.log`.
The companion JSON supplies the full compact witness and UTC conversion.

## Observation

The launch selected replacement cards at 0.02 (density scale 1.0). Ctrl+Alt+F9
toggles replacement and Ctrl+Alt+F10 changes its strength. Five complete 32-frame
F8 bursts were preserved: 4833-4864, 6550-6581, 11077-11108, 14822-14853 and
31481-31512. The first is bluewell with replacement off. The final is
foggreenoutlands, profile 2, at 0.03 / scale 1.5, from 18:07:05.803Z through
18:07:14.273Z.

The final burst has 214 source-card draws over all 32 frames (4-9 per frame),
using the established VS/PS pair `7b6393fe2d3e1d85` /
`f7e0b6647a3bfa62`. Bounded reports immediately before, inside and after it
show active applied replacement, no refusal or fault, and matching observed and
suppressed counts. No fog toggle, strength change, sector/profile change or
failed fog frame occurs inside the burst.

## Inference

The cut detector reports cuts on frames 31495-31505 and 31509-31512. The current path clears
`fog_cards_.armed` after each cut; the next frame is a stacked warm-up, which
allows the replacement volume but does not suppress native cards. Repeated cuts
therefore explain a native-card reappearance mechanism while the user moves the
camera. The first inferred warm-up begins at frame 31496 and ends after frame 31506;
the second begins at frame 31510 and continues through the captured interval:
the whole-frame present difference is 339,047 pixels at or above 24 RGB codes
at entry and 548,539 at exit. This is an event/source-path and pixel-correlated
witness; it does not attribute every individual changed pixel to the cards.

The relevant ownership is `src/proxy/motion_output_fog_inc.h:163-168` and
`:324-327`; the policy behavior is `src/proxy/fog_card_policy.h:11-24`.
All 32 present captures have been compared and the entry/exit pixel witness is
recorded above. The next work is a source-policy fix and focused host sequence
coverage; no additional flight trace is requested for this mechanism.
