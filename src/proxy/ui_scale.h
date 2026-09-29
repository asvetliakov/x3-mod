#pragma once
#include <cstdint>

// UI scale (X3M_UI_SCALE=auto|1..3; unset, empty or 1 = off; docs/architecture/ui-scale.md,
// docs/reverse-engineering/gui-scale.md section 5 strategy (b)). Enlarges the in-game 2D UI (the node-flag
// 0x200 instances: menus, sidebars, HUD panels, ticker) by s through ten verified engine sites claimed
// through engine_patch, all or none (ui_scale_sites.h): the pixel orthographic projection's 2D exit at
// 0x004be246 (P scaled about its anchor, the active cockpit's HUD camera excluded), the script's screen
// size readers (cases 0x71/0x72 of 0x00493b40 return round(W/s), round(H/s)), the four script mouse-delta
// reads (deltas divided by s with a per-site remainder) and the one native store of the script's cursor
// (multiplied back to real pixels) and the two INS natives that take the script's click point (cases 0x64/0x28,
// the point multiplied the same way so the overlay icon hit test measures real pixels). The scale is resolved at device creation from the presentation
// parameters (`auto` = back-buffer height / 1080 in quarter steps, clamped to [1, 3]) and the sites are
// claimed there, inside the engine_patch install window (a device created after the first Present is
// refused, late_claim); a Reset under `auto` re-derives s into the stubs' data cells without touching code.
// Under X3M_DEBUG=1 one further claim at the function entry 0x004bdee0 counts, per camera, the 0x200 and
// the other instances that pass, printed as one ui_scale_frame row per Present (the note's open item 1);
// it is independent of the scale and refuses when the submit-phase fixture group holds the same bytes.
namespace x3m::ui_scale {
bool initialize(); // backend-load path: the setting, the executable, every window; the diagnostic claim under debug
// CreateDevice succeeded with this back buffer: resolves `auto`, claims the ten sites when s != 1. Logs one
// ui_scale_install row. false when nothing is patched (off, refused or already installed).
bool device_created(unsigned width, unsigned height);
// A Reset with a new back buffer: under `auto` the scale follows the height (data cells only).
void after_reset(unsigned width, unsigned height);
// Per Present: refreshes the excluded camera (the active cockpit's HUD camera) and, under debug with the
// diagnostic claim live, prints and clears the per-camera counters.
void present(unsigned long long frame);
bool shutdown(); // dynamic-unload detach only; true when nothing stays registered
const char* state();
double scale();  // the scale in place (1 when off)
bool patched();  // any of the ten sites registered
bool diagnostic_patched();
}
