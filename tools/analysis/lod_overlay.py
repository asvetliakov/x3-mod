#!/usr/bin/env python3
"""Build a numbered addon CAT/DAT overlay that adds one coarser LOD record to bodies.

Merged-LOD pilot mechanism (docs/architecture/merged-lod-feasibility.md section 6,
docs/reverse-engineering/body-format-bob1.md): for each named body, copy its
coarsest existing LOD (or record --source-record N; points, part flags, per-group 7-int records and the 10
part ints are copied, nothing is recomputed; a merged group lists its records in the
order its faces first use the points, as every shipped group does), collapse every part's groups and
add the result as a new LOD record. No decimation. Alpha materials are those
whose effect parameters enable alpha testing or blending (g_AlphaTestEnable or
g_AlphaBlendEnable non-zero); an alpha texture alone does not count (the pilot
ships' lattice materials have both off and draw opaque, run257). A flagged
material whose alpha cannot drop below 1 (g_AlphaValue 1, an alpha texture that
is 255 everywhere such as NONE_WHITE or none, a diffuse alpha of 255 everywhere;
alpha_can_drop) and whose blend is source-over with depth writes
(blend_neutral_at_one) is opaque too (the Terran plate materials, Run 79 A),
unless its occlusion map is not the atlas one (occlusion_outliers). Collapse
(--collapse), per part:
  glow (default): groups whose material has a mostly bright light map (share of
    texels with Rec.601 luma above --glow-luma at least --glow-share, measured on
    the smallest mip with a side >= 64; tools/analysis/body_materials.py) keep
    their own material, one group per such material: these carry the engine
    glows (exhaust materials' light maps, run257). The remaining faces collapse
    like two. Window lights on near-black light maps are dropped.
  glow-area P (--collapse glow-area P, 0 <= P <= 100): glow, plus the smallest
    set of the other real-light-map materials (a resolvable, non-NONE_* light
    map), ranked by face area in the coarsest record (ties: lower index first),
    whose area covers P % of their total area; each keeps its own group like a
    glow material (body_materials.py rule f). P = 0 is glow.
  two: at most two groups per part: faces of alpha materials go into a second
    group with the dominant alpha material among them by face count; the rest
    into one group with the dominant opaque material.
  one: one group per part with the dominant material (alpha faces turn solid).
  atlas: one group per part with ONE synthesized opaque material per body whose
    diffuse and light-map textures are atlases baked from every opaque material of
    the record (tools/analysis/lod_atlas.py: span tiles, integer UV shifts, points
    duplicated where faces need different shifts, UVs rewritten into the atlas,
    tangent records regenerated per group; DXT1/DXT5 or --atlas-format a8r8g8b8;
    a bump atlas unless --no-atlas-bump; alpha slot NULL, specular NULL unless
    --atlas-specular); alpha faces
    as in two. The atlas side is the first of --atlas-size, 2x, ... up to
    --atlas-max-size that leaves >= 2 atlas texels per screen pixel at the switch
    size T (the NAME=T / --threshold value), with one scale for every tile, or
    else with the clamped layout (lod_atlas.plan_layout: faces repeating their
    texture over more than lod_atlas.OUTLIER_SPAN periods are span-clamped, and no
    tile holds more than 2 texels per pixel). The atlases are added to the same
    catalogue as dds/x3m_lod_<body>_<slot>.pck (new names; nothing is shadowed).
    Light bleed guard (--light-bleed-max Y, default 4, 0 = off; lod_atlas.guard_bleed): a tile
    whose light, read at the level the widened light-map fetch uses at T, gains a mean luminance
    above Y/255 from a neighbour is flagged light_bleed; the atlas is repacked with the emitter
    tiles in their own region when that costs at most --light-bleed-scale-loss (default 0.15) of
    the atlas scale, else the flagged materials keep their own groups (kept_light_bleed, one extra
    draw each; group label kept:matN). Only a tile covering at least --light-bleed-share (default
    0.02) of the atlased surface (mesh-space face area) counts; a smaller one is at a high mip at T,
    a few pixels wide, and is reported as light_bleed_ignored (its share and texel size) without a
    remedy or an extra draw.
Faces of merged materials get the dominant material's textures over their own
UVs (a look limit of the pilot; not with atlas).
Synthesized material (default; --no-synth-material turns it off): for each
material that merged faces collapse onto (per body, over every part's merged
classes), the face-area-weighted mean of every FLOAT effect parameter named
g_Mat* (diffuse, specular strength and power, reflection, occlusion, fresnel)
over the merged materials that carry it is computed; if it differs from the
dominant material's value (16.16, rounded), a copy of the dominant material with
those parameters replaced (textures and every other parameter unchanged, record
index = its position) is appended to the MAT6 table and the merged groups point
at it. Existing indices do not move, so record 0 is untouched; the written body
must parse back with the same table and every group index inside it.
MAT3 bodies (no MAT5/MAT6 section) are refused unless --force-mat3: the loader
gives every group of the coarsest record material 0x485 when such a body has
more than 3 LODs (body-format-bob1.md section 4), which would hit the pad
(compact writes exactly 3 records; the refusal is kept for every placement).

Record placement (lod-selection.md, "What the selection really does, end to
end"): the loop picks sel = highest i with s < trunc(T_i*f), else 0; at View
Distance Very High (the X3 bottle) the tail draws clamp(sel - 1, 0, n-1), so the
last record is never drawn in the main view and a two-record body always draws
LOD 0. A plain append would never be seen there. The record at index k is drawn
at Very High exactly when record k+1 is the first hit. The engine also sets
node+0x130 |= 0x100000 at a final index >= 3 (0047d51e; lod-child-hide.md section 4),
which switches the node's materials from the BUMPMAP to the DEFAULT technique and
drops a texture slot; whether DEFAULT samples the light map is unknown, so a
coarse record at index >= 3 may lose the glow groups. Hence:
  compact (default, every body; T_pad from --threshold or NAME=T, required):
    rewrite the ladder as [record 0 (unchanged), C:T_1, pad:T_pad] with T_1 the
    original record 1's threshold (T_pad on a single-LOD body) and the pad a copy
    of C. C is index 1 and the pad index 2, so the final index never reaches 3 and
    the index rule never sets 0x100000 (the < 20 px, 0x1000000-view and per-node
    threshold sets at 0047d26b..0047d28e still apply). At Very High s < T_pad*f
    hits the pad and the -1 draws C; s >= T_pad*f falls through C's T_1 (below
    T_pad) to record 0. At Low..High the pad (same mesh) draws below T_pad*f and
    record 0 above. The original records 1..n-1 are dropped: with T_pad above
    their thresholds they are unreachable in the main view anyway (pad placement
    keeps them as dead weight). T_1 must be below T_pad (refused otherwise unless
    --force-threshold: T_pad*f <= s < T_1*f would then draw C at Low..High) and
    T_pad >= 2. The collision mesh is built from the last record, now the pad,
    which has the original coarsest record's points and faces, so it is
    unchanged. The env-map view (0x1000000) draws record 1, now C, where s >= T_pad.
  pad (kept for the record; T_pad from --threshold or NAME=T, required): append
    the coarse record C with T_last (T_pad on a single-LOD body), then a pad copy
    of C with T_pad: ladder [T_0 .. T_last, C:T_last, pad:T_pad]. Walking from the
    end, s < T_pad*f hits the pad first, and the Very High -1 draws C; s >= T_pad*f
    falls through every original threshold (all below T_pad) and C's to record 0.
    So at Very High C replaces the whole ladder below T_pad and record 0 draws
    above it; LOD 1..n-1 of the original ladder become unreachable at every
    setting (intended: they carry most of the draws). At Low..High the pad (same
    mesh) draws below T_pad. T_pad must exceed every original threshold of
    records 1..n-1 (refused otherwise unless --force-threshold) and be >= 2.
    Original records are untouched.
  before-last (kept for the record; draws C only below T_last*f): insert the
    coarse record before the last one with T_new = T_last (--threshold overrides with any T <= T_last).
    With T_new <= T_last the walk hits the old last record first whenever
    s < T_last*f, so at Very High the -1 lands on the new record exactly in the
    band the old n-2 record used to cover, and at High and below the new record
    is never drawn (the old last still wins that band). The engine does not
    require descending thresholds, only the first hit from the top, so every
    ladder shape (including x/y/z/30) is accepted. At Very High this shows the
    old last record's geometry, collapsed, where it was never shown before.
  append-pad (kept for the record; --threshold T always required,
    T >= 3, and T < T_last for a multi-LOD body): append the coarse record with
    T, then a pad copy of it with T - 1, so the coarse record sits at index n-2
    of the new ladder. At Very High it draws below (T-1)*f and the band
    (T-1)*f <= s < T*f draws LOD 0 (single-LOD) or the old last record
    (multi-LOD). At Low..High the pad draws below (T-1)*f and the record itself
    in that band, i.e. the coarse mesh below T*f.
Hide-at-coarsest (0047d4d7): a node with node+0x12c & 0x8000 hides when its
final index is n-1. At Very High the final index never reaches n-1 in the main
view, so the hide does not fire there at all; at Low..High it fires at the
(new) last record, i.e. with pad for flagged nodes below T_pad*f. With
append-pad on a single-LOD body it becomes reachable at Low..High for the first
time (flagged nodes hide below (T-1)*f). With compact the last record is the pad
(index 2), so flagged nodes hide below T_pad*f at Low..High, as with pad. The old --keep-coarsest-hidden option
was removed: its premise (the last record is drawn) does not hold at Very High. The pilot flight
must still check flagged nodes.

Source record (--source-record N, default the coarsest record n-1): C is built from
record N's geometry (points, part flags and part ints, groups, 7-int records) and every
collapse, including the glow/area material selection and the synthesized materials, reads
that record. N = 0 gives C the fine silhouette, so only the texture resampling changes at
the switch (with --collapse atlas). NAME=T@N (or NAME=T,N) sets N per body. When N is not the
coarsest record, the pad (never drawn at Very High; the collision tree is built from the
last record at creation, lod-child-hide.md section 3) is a byte copy of the original coarsest
record with threshold T_pad instead of a copy of C, so the collision geometry stays vanilla's;
its material indices index the original table, a prefix of the written one. At Low..High the
pad is what draws below T_pad*f, so there the body shows the original coarsest record. A group of C referencing more
than MAX_POINTS (60,000, headroom for D3DXCleanMesh bowtie splits) distinct points is refused (the engine clones a subgroup of more
than 65,535 vertices with a 32-bit index buffer, 0x004bcb60, a path not qualified here,
mesh-buffer-rewrite.md); --collapse atlas splits every group above lod_atlas.MAX_GROUP_POINTS
(60,000, headroom for the bowtie vertices D3DXCleanMesh adds at load) instead.

Placement: addon/NN.cat/.dat with NN the next contiguous free addon slot. The
engine resolver (body-format-bob1.md section 7, 0x004e7590) takes a loose file
first, otherwise the highest-numbered catalogue holding the name under any body
extension, with no mod selection needed; extension order applies only within
that layer. The overlay member keeps the winning member's exact archive path.
A body whose winning resource is loose is refused. The engine opens a member
with fopen + fseek(long) (loading-orchestration.md step 11, 0x004e8827), so no
dat above 2^31 - 1 bytes is ever written, whatever --max-dat-bytes says (default
2,000,000,000, clamped to 2^31 - 1 with a note; the largest vanilla dat is
2,114,263,875 B). --batch splits a
larger overlay over consecutive slots NN, NN+1, ... in plan order, each body with
its atlas members inside one archive; every slot has its own marker listing its
bodies and overlay_slots (all slots of the overlay), and the batch record lists
the slots with their member counts and bytes. The single-body mode refuses
instead of splitting. The mount loop stops at the first missing number
(0x004ec9e0), so the slots stay contiguous; a tree with addon/01..12 mounted
(Mayhem 3) shows room for at least 12 addon slots.

Safety: the game's archives are only read. Every installed CAT/DAT is hashed
before and after a real run; any change fails the run and removes the outputs.
Outputs go to --out DIR (mirroring the game layout); the game directory is a
target only with --install, which refuses to overwrite anything. Source bodies
are always read with every marker-carrying catalogue skipped. While an overlay is
installed (addon/NN.x3m-lod.json) the tool refuses unless --replace: then the
marker's originals hash must match the installed archives other than our slots
(every live and retired one), the new overlay takes the lowest slot NN of the old
one, and the old overlay's other slots are removed when they are the highest
addon numbers, else retired. With --install the old three files per slot are renamed to
*.x3m-replaced, the new ones written and the asides deleted last. If moving
aside, writing or the originals check fails, every file actually moved is put
back (os.replace over any new output at its path) and the new outputs are
removed only once all of them are back; a file that cannot be put back is left
as *.x3m-replaced and reported. A failure to delete an aside after success is a
warning only (the new overlay stays; delete the aside by hand).
--install refuses while X3AP.exe runs (verification/probe/game_guard.py; an
unreadable process table also refuses) unless --force-running: the game keeps
the old CAT/DAT open with the catalogue index in memory. On native Windows the
process check needs `ps` (absent there, so it refuses without --force-running),
and renaming or deleting a CAT/DAT the game holds open fails unless the game
opened it with FILE_SHARE_DELETE; the rename then fails and the restore path
runs.

Screen reference (--display WxH, default 1920x1080; --screen-width W overrides): T is in the
1280-wide reference of lod-selection.md (s = r*640/D is resolution independent; real px =
s*m00*width/1280). The projection keeps the vertical field of view (assumption: the engine
derives m00 from the aspect ratio at a fixed vertical FOV, so a 16:9 display shows the
768-line reference height over H lines), hence the effective reference width is
H*1280/768 (1800 for 1080 lines), the width the atlas texel rule uses. The rule does not refuse
below 2 texels/px: the ratio is reported per body and the batch counts bodies below 1.0. The texel
floor is area-weighted (lod_atlas.texel_floor): each tile's share is its faces' mesh-space area over
the atlased (opaque, not hidden) surface (measured; the share of the projected screen area is
inferred proportional to it, ignoring view direction and occlusion). Tiles below --min-texels
(default 0.5) atlas texels per screen pixel at the switch size, plus the span-clamped faces (counted
at ratio 0), are starved; a body is refused as texel_floor only when the starved parts cover more
than --texel-floor-share (default 0.10) of that surface. Otherwise the starved tiles are accepted
and listed as texel_clamped (tile, ratio, share, starved share). The weighted ratio is the tile ratio
at the --texel-floor-share area quantile (refuse <=> weighted < --min-texels); --texel-floor-share 0
is the per-tile minimum rule. The batch summary lists per body the aspect factor k, T_class and T_pad
(below), the layout radius r_body (normalised body units), the flown world radius r_world and the
switch distance D = r_world*640/T at T_class and T_pad in km (lod_batch_census.attach_world: ~505
units per metre, inferred; '-' without a flown radius), the thresholds, min_ratio, weighted ratio
and starved share, lowest weighted first.

Texel fallback (batch; --texel-fallback W, default 1.0 texels/px, 0 disables): a body the texel
floor would refuse at T_pad is not refused but gets a lower switch size T_fb, so its merged record
appears farther out, where the sparse atlas does not show. Atlas texels per screen pixel scale as
1/T, so T_fb = round(T_pad * weighted / W) (W taken as max(W, --min-texels)); the layout is rebuilt at
T_fb and the step repeats (at most 3 layouts) until weighted >= W and the starved share is within
--texel-floor-share. T_fb must stay >= max(T_1, T_pad / 4, 2) (the ladder's record 1 threshold, a
relative floor, the engine's s = 1 minimum); otherwise, or when W is not reached, the body stays
refused texel_floor
(lod_batch_census.texel_fallback). An accepted body builds at T_fb (the pad threshold) and its
record carries texel_fallback (T_pad -> T_fb, weighted and starved before/after, atlas sizes, switch
km with a flown radius); the summary lists the fallback bodies. The option is in the batch settings,
so changing it makes --sync rebuild. The single-body mode keeps the threshold it is given.

Batch mode (--batch; docs/architecture/merged-lod-feasibility.md "Batch mode"): one command
builds an overlay over every eligible ship and station of the installed game, vanilla plus
every numbered addon catalogue a mod adds. The census module (lod_batch_census.run, with
.bob members, text bodies and the trailing-byte tolerance) enumerates every winning body of
ships/, stations/ and others/ (--include-other adds the rest under the station rule), applies
the rule ships T_class = min(200, max(80, 2.5*T_1)) (80 for a single-record body), stations and
others 150, then the aspect factor T_pad = round(T_class * clamp(k, 1, K_max)) (k from record 0's
half-extents, 1 for a cube; K_max --aspect-cap SHIPS,STATIONS, default 1.5,2.0; --no-aspect keeps
T_class; lod_batch_census.aspect_k), source record 0, compact placement, --collapse atlas with --atlas-specular on, and
bakes the eligible bodies in worker processes (--jobs, default min(cpu-2, 6, RAM // 7 GiB - 1),
at least 1, so 2 on a 24 GiB host: a worker holds one body's decoded textures at a time and
reaches ~7 GB RSS on the biggest stations; every worker process is replaced after one body). --only FILE restricts the run to the bodies named in FILE
(one per line; lod_batch_census sectors.txt rows and eligible_bodies.txt NAME=T@N lines are
accepted, the rule still decides T). The compact guard "T_pad not below T_1" is waived
automatically when the source record is 0: C is then the full LOD 0 geometry, so C drawing in
the Low..High band T_pad*f <= s < T_1*f (where the guard would otherwise refuse) is harmless;
the guard stays for decimated sources (a coarser source record). A text winner (.pbd/.bod) is
compiled by bob1.parse_text and written as the binary member of the same stem (.pbb/.bob; the
overlay slot is the highest catalogue and binary beats text inside a layer); the marker records
its source_member; --binary-only leaves text bodies out. parse_text follows the engine's text
loader 0x00483f20 (body-text-loader.md), so the member loads into the model the game builds from
the text: engine normals, per-record position scale, no tangent records (a bump-mapped text body
draws with a zero tangent basis in vanilla too). Refusal reasons: text_parse_error (a text body
outside that grammar; a MATERIAL3 text body is mat3), ambiguous_body_ext (both a binary and a text member), trailing_bytes
(more than MAX_TRAILING stray bytes after /BOB; up to MAX_TRAILING are tolerated with a
warning, the parser 0x00481aa0 returns at /BOB and never reads them), material_outside_table
(a group material index past the table; since 2026-09-24 a negative index -N is a texture animation that
--collapse atlas maps onto its material and bakes with the start frame, lod_atlas.animated_record), occlusion_mismatch (second UV set with
differing occlusion decals inside one merged group), mat3, no_opaque, dominant_slot_missing
(unreachable since 2026-09-24: the dominant is taken among the materials that declare the needed
slots and an effect that declares no light map keeps none), excluded_effect (every opaque material
on lod_atlas.KEPT_EFFECTS, planet_haze.fx / asteroid.fx, which otherwise keep their own groups),
no_diffuse (since 2026-09-24 only a material without a t_DiffuseTexture parameter; a NULL diffuse
bakes the black placeholder), texture_unresolved, texture_animation_unsupported (TAT_MOVIE, TAT_TAGSINGLESTEP, a
non-zero start UV offset, an animated group left out of the atlas), texture_generated (a MPF_GENERATED
Materials row, drawn at run time), pil_missing (a jpg/tga texture without
Pillow), and the lod_atlas reasons. Mixed effects and the second UV set are handled, not refused
(lod_atlas notes). A change to lod_atlas.py or this file changes tool_sha256, so the next --sync
rebuilds every body.
Atlas member names are dds/x3m_lod_<stem>_<hash6>_<slot>.pck with the hash from the member
path (qualified_stem), unique within an overlay and stable across runs.

Markers and slots in batch mode: the overlay starts at the next contiguous free addon number and
takes as many consecutive numbers as --max-dat-bytes requires (Placement above).
Every run validates every addon/NN.x3m-lod.json marker by hash (installed_markers): the
marker records its own slot's overlay cat/dat sha256, and a marker whose hashes do not match the files
beside it is orphaned (a mod overwrote the slot): it is reported, its catalogue is read as a
source like any mod catalogue, and --install removes the orphaned marker. The live markers (valid
hashes, or a legacy marker without them) sharing one overlay_slots list are the previous overlay
(live_overlays; an orphaned slot of it simply drops out): batch mode supersedes it
without --replace. Its live slots that form the top of the addon numbering are reused from
their lowest number (the --replace move-aside/rollback path, over every slot at once; a failure
restores every slot); slots of it above the new overlay's end are removed, the others retired;
if none is at the top (a mod added higher numbers) the new
overlay goes to the next free numbers. A stale orphaned marker with no cat/dat beside it in a slot the
overlay grows into refuses the install; remove that marker by hand. Each retired old slot's cat/dat are
replaced by a retired catalogue
holding one inert text member x3m_lod/retired_NN.txt (never a zero-entry CAT or 0-byte DAT)
with a marker recording it as retired (contiguity must hold; the engine stops at the first
gap; engine acceptance of a retired slot still needs a launch). A legacy marker (no overlay
hashes, the pilot's shape) is trusted only when every body it names is in the catalogue beside
it with the recorded overlay_decoded_sha256 (legacy_verified); otherwise it is orphaned, so a
mod that overwrote that slot is neither skipped as a source nor retired or replaced. --sync
rebuilds only the bodies whose inputs changed (inputs_sha256 over the decoded body and every
texture its tiles read) or that are new, and copies the other bodies' members (body + atlases,
verified by sha256) from any live slot of the previous overlay; the previous overlay must have been built
with the same rule, width, atlas options and tool sources (tool_sha256 over lod_atlas.py,
lod_overlay.py, bob1.py and lod_batch_census.py in the settings). addon/mods/*.cat
are detected and a warning gives how many overlay bodies a selected mod would override.
The before/after archive check is a cat sha256 plus dat size and mtime by default
(--hash-archives hashes every dat; mod trees are gigabytes); the marker records the mode.
The batch writes a record JSON (--record, default x3m-lod-batch.json under --out, beside the marker on --install, else in the
working directory; on a real run only after the overlay was written, so a failed or refused install
leaves the previous record untouched) with the counts by reason, atlas sizes and bytes, the texels/px ratio per
body, the per-sector resident estimate (lod_batch_census.sector_report over its census files;
--budget-mb N, default 512, warns when a sector exceeds N), per-body timings and the
extrapolated full-set wall time, the light-bleed guard per body (light_bleed = tiles over the limit,
light_bleed_counted = those at or above --light-bleed-share, light_bleed_ignored = the smaller ones with
their share, kept_light_bleed, remedy; the per-body rows print light_bleed=<tiles> counted=<tiles>
ignored=<tiles> kept=<materials>, the summary lists
the affected bodies, the record's light_bleed.bodies), and a *-bodies.txt log with every body's plan.

  python3 tools/analysis/lod_overlay.py --dry-run --threshold 50 ships/argon/argon_TL
  python3 tools/analysis/lod_overlay.py --out /tmp/x3m-lod ships/argon/argon_TL=50 stations/others/military_outpost_middleb=100
  python3 tools/analysis/lod_overlay.py --batch --dry-run --only verification/results/lod-overlay-batch/sectors.txt --out /tmp/x3m-batch
  python3 tools/analysis/lod_overlay.py --batch --sync --install
"""
import argparse
import contextlib
import gzip
import hashlib
import io
import json
import multiprocessing
import os
import sys
import struct
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bob1  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import Assets, unpack, write_catalogue  # noqa: E402

MARKER_SUFFIX = '.x3m-lod.json'
REPLACED_SUFFIX = '.x3m-replaced'
TEXT_BODY_EXTENSIONS = ('.pbd', '.bod')     # text bodies: compiled with bob1.parse_text, written as .pbb/.bob


def text_compiles(tree):
    """True when the compiled text tree serialises and parses back to the same sections."""
    try:
        return bob1.parse_binary(bob1.serialise(tree))['sections'] == tree['sections']
    except (bob1.FormatError, struct.error, OverflowError):
        return False


MAX_TRAILING = 8            # stray bytes after /BOB tolerated with a warning (86 of 94 failing mod bodies carry 1-2)
DISPLAY = (1920, 1080)
REFERENCE = (1280, 768)     # lod-selection.md reference frame of the threshold metric
LIVE_MARKERS = ('valid', 'legacy')
OURS_MARKERS = LIVE_MARKERS + ('retired', 'unreadable')   # catalogues never read as body sources
OUR_SLOTS = LIVE_MARKERS + ('retired',)     # slots left out of the originals hash
DAT_LIMIT = 2 ** 31 - 1     # the engine opens a member with fopen + fseek(long) (0x004e8827): offset + size must fit
MAX_DAT_BYTES = 2_000_000_000   # default --max-dat-bytes; the largest vanilla dat (02.dat) is 2,114,263,875 B


def qualified_stem(member):
    """Atlas name stem of a body member path: file stem + 6 hex of sha1(lower-case posix path), so
    colliding stems (ships/terran/terran_M3 vs ships/usc/terran_m3) get distinct, stable names."""
    p = member.replace('\\', '/')
    return f'{Path(p).stem}_{hashlib.sha1(p.lower().encode()).hexdigest()[:6]}'


def effective_width(display=None, screen_width=None):
    """Reference width for the texel rule: --screen-width, else H*1280/768 of --display."""
    if screen_width is not None:
        return int(screen_width)
    w, h = display or DISPLAY
    return int(round(h * REFERENCE[0] / REFERENCE[1]))


def parse_display(text):
    try:
        w, h = (int(v) for v in text.lower().split('x'))
    except ValueError:
        raise argparse.ArgumentTypeError(f'--display needs WxH, got {text!r}') from None
    if w < 320 or h < 240:
        raise argparse.ArgumentTypeError(f'--display {text}: too small')
    return w, h


def running_game():
    """Process lines of a running X3AP game (verification/probe/game_guard.py); raises
    RuntimeError when the process table cannot be read (callers refuse)."""
    probe = str(Path(__file__).resolve().parents[2] / 'verification' / 'probe')
    if probe not in sys.path:
        sys.path.insert(0, probe)
    from game_guard import game_running
    return game_running()


def slot_set(slots):
    """None, one slot number or an iterable of them -> a set of slot numbers."""
    if slots is None:
        return set()
    return {int(slots)} if isinstance(slots, int) else {int(s) for s in slots}


def original_archives(game, exclude_slot=None):
    """Every installed NN.cat/.dat and addon/NN.cat/.dat pair except the addon slots in exclude_slot (one
    number or an iterable: all slots of a multi-slot overlay plus retired slots)."""
    cats = sorted(game.glob('[0-9][0-9].cat')) + sorted((game / 'addon').glob('[0-9][0-9].cat'))
    skip = {game / 'addon' / f'{s:02d}.cat' for s in slot_set(exclude_slot)}
    return [p for cat in cats if cat not in skip for p in (cat, cat.with_suffix('.dat'))]


def our_slots(markers):
    """Addon slots whose catalogue is ours (live or retired marker): excluded from the originals hash."""
    return {m['slot'] for m in markers if m['slot'] and m['status'] in OUR_SLOTS}


def live_overlays(markers):
    """{overlay_slots tuple: [live markers]}: the live markers grouped by the overlay they belong to (a
    multi-slot overlay's markers share overlay_slots; a single-slot or legacy marker is its own group).
    A slot of the group whose marker is orphaned (a mod overwrote it) is simply absent."""
    groups = {}
    for m in markers:
        if m['status'] in LIVE_MARKERS:
            groups.setdefault(tuple(m['overlay_slots']), []).append(m)
    return {k: sorted(v, key=lambda m: m['slot']) for k, v in groups.items()}


def dat_bytes(members):
    return sum(len(d) for _, d in members)


def check_dat_limit(slot, members):
    """Hard refusal: the engine cannot reach a member ending past 2^31 - 1 (fseek(long)); regardless of
    --max-dat-bytes no such dat is written."""
    n = dat_bytes(members)
    if n > DAT_LIMIT:
        raise SystemExit(f'addon/{slot:02d}.dat would be {n} bytes, above 2^31 - 1 = {DAT_LIMIT} (the engine seeks'
                         ' catalogue members with a signed 32-bit offset); refusing, nothing written')


def pack_slots(plans, max_bytes, first=1):
    """[[plan, ...], ...]: the plans in order, each body's members (its .pbb/.bob and its atlas members)
    in one archive; the next archive starts when a body would push the current dat past the cap,
    min(max_bytes, DAT_LIMIT) (a larger --max-dat-bytes splits at the limit). SystemExit when one body
    alone exceeds the cap; the archives are numbered from first in the messages."""
    cap = min(max_bytes, DAT_LIMIT)
    out, size = [], 0
    for p in plans:
        n = dat_bytes(p['members'])
        if n > cap:
            raise SystemExit(f'{p["name"]}: its members need {n} dat bytes, more than '
                             + (f'--max-dat-bytes {cap}' if cap == max_bytes else f'2^31 - 1 = {cap}')
                             + ' in one archive; nothing written')
        if not out or size + n > cap:
            out.append([])
            size = 0
        out[-1].append(p)
        size += n
    for i, group in enumerate(out):
        check_dat_limit(first + i, [m for p in group for m in p['members']])
    return out


def slot_plan(prev_slots, nxt, count, start):
    """(new, replaced, retired, removed) for an overlay of count archives at start..start+count-1 given the
    previous overlay's live slots and the next free slot nxt: replaced = previous slots inside the new range;
    removed = previous slots above it when every slot from its end up to nxt - 1 is ours (deleting them keeps
    the numbering contiguous); retired = the other previous slots (kept as retired catalogues)."""
    new = list(range(start, start + count))
    if new[-1] > 99:
        raise SystemExit(f'the overlay needs addon/{start:02d}..{new[-1]:02d}; addon numbers stop at 99')
    prev = sorted(slot_set(prev_slots))
    above = list(range(new[-1] + 1, nxt))
    removed = above if above and all(s in prev for s in above) else []
    replaced = [s for s in prev if s in new]
    retired = [s for s in prev if s not in new and s not in removed]
    return new, replaced, retired, removed


def originals_digest(hashes):
    return hashlib.sha256(json.dumps(sorted(hashes.items())).encode()).hexdigest()


def installed_markers(game):
    """[dict(path, slot, manifest, status)] for every addon/NN.x3m-lod.json, status one of
    'valid' (the marker's overlay cat/dat sha256 match the files beside it), 'legacy' (a marker
    without overlay hashes, from before 2026-09-23, whose every recorded body member is present
    in the catalogue beside it with the recorded overlay_decoded_sha256: legacy_verified), 'retired'
    (the retired catalogue we wrote, hashes match), 'orphaned' (hashes differ, the files are gone,
    or a legacy marker carries no body proof or its members do not match: a mod overwrote the
    slot) or 'unreadable'. Every marker is validated against its own slot's files; overlay_slots is the
    marker's overlay_slots list (all slots of a multi-slot overlay, consecutive, containing its own slot)
    or [slot] for a single-slot, legacy or retired marker; a malformed list makes the marker unreadable."""
    out = []
    for path in sorted((Path(game) / 'addon').glob('*' + MARKER_SUFFIX)):
        m = dict(path=path, slot=None, manifest=None, status='unreadable', overlay_slots=None)
        try:
            manifest = json.loads(path.read_text())
            slot = int(manifest['slot'])
            if path.name != f'{slot:02d}{MARKER_SUFFIX}':
                raise ValueError('marker name does not match its slot')
            group = manifest.get('overlay_slots', [slot])
            if not (isinstance(group, list) and group and all(type(s) is int for s in group) and slot in group
                    and group == list(range(group[0], group[0] + len(group)))):
                raise ValueError('overlay_slots is not a consecutive slot list holding the marker\'s slot')
        except (ValueError, KeyError, TypeError, OSError, AttributeError):
            out.append(m)
            continue
        m.update(slot=slot, manifest=manifest, overlay_slots=group)
        cat = path.with_name(f'{slot:02d}.cat')
        expected = manifest.get('overlay_sha256')
        if not cat.exists() or not cat.with_suffix('.dat').exists():
            m['status'] = 'orphaned'
        elif not isinstance(expected, dict):
            m['status'] = 'legacy' if legacy_verified(cat, manifest) else 'orphaned'
        else:
            got = hash_files([cat, cat.with_suffix('.dat')])
            if (got[str(cat)], got[str(cat.with_suffix('.dat'))]) == (expected.get('cat'), expected.get('dat')):
                m['status'] = 'retired' if manifest.get('retired') else 'valid'
            else:
                m['status'] = 'orphaned'
        out.append(m)
    return out


def legacy_verified(cat, manifest):
    """True when a marker without overlay hashes still proves the catalogue is ours: it names at
    least one body, and every named body member is in the catalogue beside it with the recorded
    overlay_decoded_sha256 (the pilot marker shape). Anything else (no bodies, a missing member,
    a different sha, an unreadable catalogue) is no proof: a mod may have overwritten the slot. In a
    multi-slot overlay each slot's marker names only the bodies stored in that slot, so every slot is
    proved against its own catalogue; a body member in another slot of the overlay is no proof for this one."""
    bodies = manifest.get('bodies') if isinstance(manifest, dict) else None
    if not isinstance(bodies, list) or not bodies:
        return False
    try:
        entries = {e['path'].lower(): e for e in read_catalogue(cat)}
        with cat.with_suffix('.dat').open('rb') as f:
            for b in bodies:
                member, want = b.get('member'), b.get('overlay_decoded_sha256')
                e = entries.get(str(member).lower()) if member else None
                if e is None or not want:
                    return False
                f.seek(e['offset'])
                data = unpack(bytes(v ^ 0x33 for v in f.read(e['size'])))
                if hashlib.sha256(data).hexdigest() != want:
                    return False
    except (ValueError, OSError, KeyError, TypeError, EOFError):
        return False
    return True


def original_assets(game, markers=None):
    """(Assets, skipped sources): every catalogue with a live, retired or unreadable marker beside
    it is left out; an orphaned marker's catalogue (a mod overwrote the slot) is read as a source."""
    assets = Assets(Path(game))
    markers = installed_markers(game) if markers is None else markers
    ours = {m['path'].with_name(f'{m["slot"]:02d}.cat') for m in markers if m['slot'] and m['status'] in OURS_MARKERS}
    ours |= {m['path'].with_name(m['path'].name[:-len(MARKER_SUFFIX)] + '.cat') for m in markers
             if m['status'] == 'unreadable'}
    marked = lambda e: 'cat' in e and e['cat'] in ours
    skipped = sorted({e['source'] for v in assets.entries.values() for e in v if marked(e)})
    if skipped:
        for key in list(assets.entries):
            kept = [e for e in assets.entries[key] if not marked(e)]
            if kept:
                assets.entries[key] = kept
            else:
                del assets.entries[key]
    return assets, skipped


def hash_files(paths):
    out = {}
    for p in paths:
        h = hashlib.sha256()
        with p.open('rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                h.update(block)
        out[str(p)] = h.hexdigest()
    return out


def fingerprint_files(paths, full=False):
    """{path: fingerprint}: sha256 of every .cat (small) and, with full, of every .dat; otherwise
    a .dat is 'size:<bytes>:mtime_ns:<ns>' (mod trees are gigabytes; --hash-archives hashes them)."""
    out = {}
    for p in paths:
        if full or p.suffix.lower() == '.cat':
            out.update(hash_files([p]))
        else:
            st = p.stat()
            out[str(p)] = f'size:{st.st_size}:mtime_ns:{st.st_mtime_ns}'
    return out


def archive_digest(game, exclude_slot, mode):
    """(digest, fingerprints) of the installed archives other than exclude_slot in mode 'sha256'
    (every file hashed) or 'fingerprint' (fingerprint_files)."""
    fp = (hash_files if mode == 'sha256' else fingerprint_files)(original_archives(game, exclude_slot))
    return originals_digest(fp), fp


def next_slot(game, count=1):
    """The next contiguous free addon number (the engine mounts addon/01.cat upwards until the first missing
    number, 0x004ec9e0); count consecutive free numbers must fit below 100. A multi-slot overlay counts like
    any other catalogues here."""
    nums = sorted(int(p.stem) for p in (game / 'addon').glob('[0-9][0-9].cat'))
    if nums != list(range(1, len(nums) + 1)):
        raise SystemExit(f'addon catalogues are not contiguous from 01: {nums}')
    if len(nums) + count > 99:
        raise SystemExit('no free addon slot' if count == 1 else f'no {count} free consecutive addon slots')
    return len(nums) + 1


def dominant_material(groups):
    faces = Counter()
    for g in groups:                       # Counter keeps first-insertion order for ties
        faces[g['material']] += len(g['faces'])
    return max(faces, key=lambda m: faces[m]) if faces else None


COLLAPSES = ('glow', 'two', 'one', 'glow-area', 'atlas')
KEEPING = ('glow', 'glow-area')      # collapses whose kept materials keep their own groups
GLOW_LUMA = 0.5          # texel counts as bright above this Rec.601 luma
GLOW_SHARE = 0.25        # light map is "mostly bright" at this bright-texel share
ALPHA_PARAMS = (b'g_alphatestenable', b'g_alphablendenable')


ALPHA_VALUE_PARAM = b'g_alphavalue'       # SPTYPE_FLOAT 16.16 material alpha constant (65536 = 1.0)
ENABLE_GLOW_PARAM = b'g_enableglow'       # 1: the effects' output alpha is the light map's alpha
_OPAQUE_TEXTURES = {}                     # (decoded sha256, kind, channels) -> every texel 255: per process


def alpha_flagged(material):
    """True when the material's effect parameters enable alpha testing or alpha blending."""
    return any(typ in (0, 1) and name.lower() in ALPHA_PARAMS and val and val[0]
               for name, typ, val in material.get('params', ()))


def texture_opaque(assets, name, channels, cache=None):
    """True when the texture the engine binds for `name` (lod_atlas.texture_source: the file, or the suffix
    placeholder when none loads) is 255 in `channels` (indices into RGBA) at every mip level; None for a NULL
    name or an id drawn without a texture; False when it does not resolve or decode (counted as varying)."""
    import lod_atlas
    try:
        src = lod_atlas.texture_source(assets, name)
    except (lod_atlas.AtlasError, ValueError):
        return False
    if src is None:
        return None
    data, kind, info = src
    cache = _OPAQUE_TEXTURES if cache is None else cache
    key = (info['decoded_sha256'], kind, tuple(channels))
    if key not in cache:
        try:
            if kind == 'dds':
                levels = lod_atlas.dds_format(data)[2]
                cache[key] = all((lod_atlas.decode_dds(data, k)[:, :, list(channels)] == 255).all()
                                 for k in range(max(1, levels)))
            else:
                cache[key] = bool((lod_atlas.decode_image(data, name)[:, :, list(channels)] == 255).all())
        except (lod_atlas.AtlasError, ValueError, struct.error, OSError):     # OSError: Pillow decode
            cache[key] = False
    return cache[key]


def alpha_can_drop(assets, material, cache=None):
    """True when the material's alpha can fall below 1: g_AlphaValue below 1.0, a t_DiffuseTexture whose alpha
    channel is not 255 everywhere, with g_EnableGlow non-zero a t_LightMapTexture whose alpha is not 255
    everywhere (the effects' output alpha is AlphaValue x (EnableGlow ? LightMap.a : Diffuse.a),
    station-material-distance.md "Shader and effect contracts"), or a t_AlphaTexture that is not 255 in all four
    channels at every level (that dataflow does not read the alpha map; kept as a conservative check, a
    NONE_WHITE placeholder passes). A NULL alpha texture is no alpha source; a NULL diffuse is the opaque black
    placeholder (lod_atlas.NULL_DIFFUSE_TEXEL, inferred). A texture that does not resolve or decode counts as
    varying. g_EnableGlow is read from the material's parameters only: the engine also sets it per draw from the
    glow option (0x004c36a5..0x004c380d), which is not modelled."""
    import body_materials
    glow = False
    for name, typ, val in material.get('params', ()):
        if name.lower() == ALPHA_VALUE_PARAM and typ == 2 and val and val[0] < 65536:
            return True
        if name.lower() == ENABLE_GLOW_PARAM and typ in (0, 1, 2) and val and val[0]:
            glow = True
    slots = body_materials.slots(material)
    for slot, channels in (('alpha', (0, 1, 2, 3)), ('diffuse', (3,))) + ((('light', (3,)),) if glow else ()):
        name = slots.get(slot)
        if name is not None and texture_opaque(assets, name, channels, cache) is False:
            return True
    return False


# D3DBLEND values whose blend result is the source colour when the source alpha is 1 (SRCALPHA/ONE over
# INVSRCALPHA/ZERO with D3DBLENDOP_ADD); an additive or multiplicative blend changes the image at alpha 1 too
NEUTRAL_SRC_BLEND, NEUTRAL_DEST_BLEND, BLENDOP_ADD = (2, 5), (1, 6), 1


def blend_neutral_at_one(material):
    """True when the material's blend state leaves the source colour unchanged at alpha 1 and it writes depth:
    blending off, or g_BlendOp ADD with g_SrcBlend SRCALPHA/ONE and g_DestBlend INVSRCALPHA/ZERO; and
    g_ZWriteEnable not 0 (a depth-less transparent layer drawn as opaque would occlude). An absent parameter
    counts as the neutral value."""
    p = {name.lower(): val[0] for name, typ, val in material.get('params', ()) if typ in (0, 1) and val}
    if p.get(b'g_zwriteenable', 1) == 0:
        return False
    if not p.get(b'g_alphablendenable', 0):
        return True
    return (p.get(b'g_blendop', BLENDOP_ADD) == BLENDOP_ADD and p.get(b'g_srcblend', 5) in NEUTRAL_SRC_BLEND
            and p.get(b'g_destblend', 6) in NEUTRAL_DEST_BLEND)


def occlusion_outliers(materials, record, flagged, alpha):
    """Flagged, otherwise opaque materials (flagged - alpha) of the visible parts of `record` whose occlusion map
    (lod_atlas.occlusion_name, None for absent / NULL / NONE_*) is not the atlas occlusion map of their effect,
    the maps of the effect's unflagged opaque materials. When the effect has no unflagged opaque material, its
    candidates are atlased only if they all carry one map; otherwise all of them stay alpha (the body stays
    no_opaque as under the flag rule; no candidate is chosen to set the map, so no plate of another map starts
    borrowing a diffuse). Atlasing an outlier would refuse the body occlusion_mismatch (one occlusion texture per
    merged material; terran_TL_atmolifter)."""
    import lod_atlas
    faces = Counter()
    for part in record['parts']:
        if part['flags'] & lod_atlas.HIDDEN_PART:
            continue
        for g in part['groups']:
            if 0 <= g['material'] < len(materials) and g['material'] not in alpha:
                faces[g['material']] += len(g['faces'])
    by_effect = {}
    for m in faces:                                # Counter keeps first-use order
        by_effect.setdefault(lod_atlas.effect_name(materials[m]), []).append(m)
    out = set()
    for mis in by_effect.values():
        cand = [m for m in mis if m in flagged]
        base = {lod_atlas.occlusion_name(materials[m]) for m in mis if m not in flagged}
        if not base:
            if len({lod_atlas.occlusion_name(materials[m]) for m in cand}) > 1:
                out |= set(cand)
            continue
        out |= {m for m in cand if lod_atlas.occlusion_name(materials[m]) not in base}
    return out


def alpha_materials(materials, assets=None, cache=None, record=None):
    """Group material indices (array positions, not the record's u16) drawn with alpha: the effect
    parameters enable alpha testing or alpha blending AND (with `assets`) the flags can change the image: the
    alpha can drop below 1 (alpha_can_drop) or the blend state is not neutral at alpha 1 (blend_neutral_at_one).
    Any other flagged material (alpha 1 everywhere, source-over blend) draws as opaque (blend and test are
    no-ops at alpha 1: inferred, the effect's pixel shader is not disassembled) and is atlased like any opaque
    material, unless (with `record`, the record to collapse) its occlusion map differs from its effect's atlas
    occlusion map (occlusion_outliers): it then stays alpha. Without `assets` the flag rule alone (a superset)."""
    flagged = {i for i, m in enumerate(materials) if alpha_flagged(m)}
    if assets is None:
        return flagged
    alpha = {i for i in flagged if not blend_neutral_at_one(materials[i])
             or alpha_can_drop(assets, materials[i], cache)}
    if record is not None:
        alpha |= occlusion_outliers(materials, record, flagged, alpha)
    return alpha


def glow_materials(assets, materials, used=None, luma=GLOW_LUMA, share=GLOW_SHARE):
    """Indices (of `used`, default all) whose light map has a bright-texel share >= share."""
    import body_materials                      # texture decoding lives there
    cache, out = {}, set()
    for i in (range(len(materials)) if used is None else used):
        light = body_materials.slots(materials[i]).get('light') if 0 <= i < len(materials) else None
        if light is not None and body_materials.texture_info(assets, light, cache, luma).get('bright', 0) >= share:
            out.add(i)
    return out


def light_area_materials(assets, materials, record, exclude=frozenset(), percent=100.0):
    """Rule f: (kept set, covered area, total area) over the materials of `record` whose
    light map is real (resolvable, not NONE_*) and not in `exclude`, ranked by face area."""
    import body_materials
    cache, areas = {}, {}
    for part in record['parts']:
        for g in part['groups']:
            mi = g['material']
            if mi in exclude or not 0 <= mi < len(materials):
                continue
            if mi not in areas:
                light = body_materials.slots(materials[mi]).get('light')
                if light is None or body_materials.light_status(
                        body_materials.texture_info(assets, light, cache)) != 'real':
                    continue
                areas[mi] = 0.0
            areas[mi] += sum(body_materials.face_area(record['points'], f) for f in g['faces'])
    kept, covered, total = body_materials.area_select(areas, percent)
    return set(kept), covered, total


def first_use_records(groups, faces):
    """The groups' 7-int records in the order `faces` first use their points (the shipped
    convention: every shipped group lists its records so), one per point (the first group's
    record wins); records of points no face uses follow in their original order."""
    by = {}
    for g in groups:
        for e in g['extra']:
            by.setdefault(e[0], e)
    used = dict.fromkeys(i for f in faces for i in f[:3])
    return [by[i] for i in used if i in by] + [e for g in groups for e in g['extra'] if e[0] not in used]


def merged_group(groups, precomputed, remap=None):
    m = dominant_material(groups)
    g = {'material': (remap or {}).get(m, m), 'faces': [f for g in groups for f in g['faces']]}
    if precomputed:
        g['extra'] = first_use_records(groups, g['faces'])
    return g


def collapse_classes(groups, alpha=frozenset(), collapse='two', kept=frozenset()):
    """One part's groups -> [(kind, groups)] in output order, kind 'merged' (collapsed onto
    the class's dominant material) or 'kept' (one kept material's own groups)."""
    if collapse not in COLLAPSES:
        raise ValueError(f'unknown collapse {collapse!r}')
    kept = kept if collapse in KEEPING else frozenset()
    rest = [g for g in groups if g['material'] not in kept]
    if collapse == 'one':
        classes = [('merged', rest)]
    else:
        own = list(dict.fromkeys(g['material'] for g in groups if g['material'] in kept))
        classes = ([('merged', [g for g in rest if g['material'] not in alpha])]
                   + [('kept', [g for g in groups if g['material'] == m]) for m in own]
                   + [('merged', [g for g in rest if g['material'] in alpha])])
    return [(k, c) for k, c in classes if c]


def is_light_param(name, typ):
    return typ == 2 and name.lower().startswith(b'g_mat')


def synth_materials(materials, coarsest, alpha=frozenset(), collapse='two', kept=frozenset()):
    """Append synthesized materials (module notes) to `materials` in place; returns
    (remap {dominant: new index}, report [dict(index, dominant, absorbed, params)]), params
    = [(name, dominant 16.16, weighted mean 16.16 float, written 16.16)] per g_Mat* FLOAT."""
    import body_materials
    weights = {}
    for part in coarsest['parts']:
        for kind, cls in collapse_classes(part['groups'], alpha, collapse, kept):
            if kind != 'merged':
                continue
            w = weights.setdefault(dominant_material(cls), {})
            for g in cls:
                w[g['material']] = w.get(g['material'], 0.0) + sum(
                    body_materials.face_area(coarsest['points'], f) for f in g['faces'])
    remap, report = {}, []
    for d, w in weights.items():
        if not 0 <= d < len(materials) or 'params' not in materials[d]:
            continue
        rows, params = [], []
        for name, typ, val in materials[d]['params']:
            if is_light_param(name, typ):
                num = den = 0.0
                for mi, area in w.items():
                    if not 0 <= mi < len(materials):
                        continue
                    v = [pv for pn, pt, pv in materials[mi].get('params', ())
                         if pt == 2 and pn.lower() == name.lower()]
                    if v:
                        num += area * v[0][0]; den += area
                if den > 0:
                    mean = num / den
                    rows.append((name.decode('latin1'), val[0], mean, int(round(mean))))
                    val = [int(round(mean))]
            params.append((name, typ, val))
        if any(r[1] != r[3] for r in rows):
            new = dict(materials[d], index=len(materials), params=params)
            remap[d] = len(materials)
            materials.append(new)
            report.append(dict(index=remap[d], dominant=d, absorbed=sorted(w), params=rows))
    return remap, report


def coarse_record(coarsest, threshold, alpha=frozenset(), collapse='two', glow=frozenset(), remap=None):
    """New LOD: coarsest's points and part data copied; per part the groups are collapsed
    to one (collapse 'one'), to opaque + alpha groups (collapse 'two', alpha = material
    indices that alpha-test or alpha-blend; the alpha group follows the opaque one), or
    (collapse 'glow' / 'glow-area', glow = the kept materials) to the opaque group, one
    group per kept material in first-use order, then the alpha group. remap maps a merged
    group's dominant material to a synthesized one (synth_materials)."""
    if collapse not in COLLAPSES:
        raise ValueError(f'unknown collapse {collapse!r}')
    parts = []
    for part in coarsest['parts']:
        new = {'flags': part['flags'], 'groups': part['groups']}
        if part['groups']:
            pre = part['flags'] & bob1.PART_PRECOMPUTED
            new['groups'] = [merged_group(c, pre, remap if k == 'merged' else None)
                             for k, c in collapse_classes(part['groups'], alpha, collapse, glow)]
        if 'bounds' in part:
            new['bounds'] = list(part['bounds'])
        parts.append(new)
    lod = {'value': threshold, 'flags': coarsest['flags']}
    if 'bones' in coarsest:
        lod['bones'] = list(coarsest['bones'])
    lod['points'] = coarsest['points']
    if 'weights' in coarsest:
        lod['weights'] = coarsest['weights']
    lod['parts'] = parts
    return lod


PLACEMENTS = ('compact', 'pad', 'before-last', 'append-pad')


def default_placement(ladder):
    return 'compact'


def place(ladder, coarse, placement, threshold, name='body', force_threshold=False):
    """Insert the coarse record (value set here); returns (new index, pad index or None).

    compact: the ladder becomes [record 0, C, pad copy of C]: C at index 1 with T_1
      (the original record 1's threshold; T_pad for a single-LOD body), the pad at
      index 2 with T_pad; the original records 1..n-1 are dropped. T_pad required,
      >= 2, and above T_1 unless force_threshold.
    pad: C at index n with T_last (T_pad for a single-LOD body), then a pad copy at
      n+1 with T_pad; T_pad required, >= 2, and above every original threshold of
      records 1..n-1 unless force_threshold.
    before-last: new record at index n-1 (old last moves to n), 1 <= T_new <= T_last,
      default T_last (any ladder shape, since only the first hit from the top matters).
    append-pad: new record at index n with T, then a pad copy at n+1 with T - 1;
      T is always required, 3 <= T, and T < T_last for a multi-LOD body."""
    n = len(ladder)
    if placement == 'compact':
        if threshold is None:
            raise SystemExit(f'{name}: compact placement needs a pad threshold (--threshold T or NAME=T)')
        t = threshold
        if t < 2:
            raise SystemExit(f'{name}: pad threshold {t} must be >= 2 (s >= 1, so a smaller one is never hit)')
        t_c = ladder[1]['value'] if n >= 2 else t
        if t_c > t and not force_threshold:
            raise SystemExit(f'{name}: pad threshold {t} must not be below the record 1 threshold {t_c}'
                             ' (C would draw at Low..High above T_pad); --force-threshold overrides')
        coarse['value'] = t_c
        pad = dict(coarse, value=t)
        del ladder[1:]
        ladder.extend([coarse, pad])
        return 1, 2
    if placement == 'pad':
        if threshold is None:
            raise SystemExit(f'{name}: pad placement needs a pad threshold (--threshold T or NAME=T)')
        t = threshold
        if t < 2:
            raise SystemExit(f'{name}: pad threshold {t} must be >= 2 (s >= 1, so a smaller one is never hit)')
        above = [l['value'] for l in ladder[1:] if l['value'] >= t]
        if above and not force_threshold:
            raise SystemExit(f'{name}: pad threshold {t} must exceed every original threshold'
                             f' {[l["value"] for l in ladder[1:]]} (records 1..n-1); --force-threshold overrides')
        coarse['value'] = ladder[-1]['value'] if n >= 2 else t
        pad = dict(coarse, value=t)
        ladder.extend([coarse, pad])
        return n, n + 1
    if placement == 'before-last':
        if n < 2:
            raise SystemExit(f'{name}: before-last needs at least two LOD records; use append-pad')
        t_last = ladder[-1]['value']
        t = t_last if threshold is None else threshold
        if not 1 <= t <= t_last:
            raise SystemExit(f'{name}: before-last threshold {t} must satisfy 1 <= T <= T_last = {t_last}')
        coarse['value'] = t
        ladder.insert(n - 1, coarse)
        return n - 1, None
    if threshold is None:
        raise SystemExit(f'{name}: append-pad needs --threshold')
    t = threshold
    if t < 3:
        raise SystemExit(f'{name}: append-pad threshold {t} must be >= 3 (the pad gets T - 1 >= 2)')
    if n >= 2 and t >= ladder[-1]['value']:
        raise SystemExit(f'{name}: append-pad threshold {t} must be below the last threshold {ladder[-1]["value"]}')
    coarse['value'] = t
    pad = dict(coarse, value=t - 1)
    ladder.extend([coarse, pad])
    return n, n + 1


MAX_POINTS = 60000         # refusal limit for a merged group (non-atlas collapses; as lod_atlas.MAX_GROUP_POINTS)


MIN_TEXELS = 0.5            # texel floor: tiles below this many atlas texels per screen pixel are starved
TEXEL_FLOOR_SHARE = 0.10    # texel_floor refusal: starved tiles cover more than this share of the atlased surface
TEXEL_FALLBACK = 1.0        # batch: a texel_floor body gets a lower T so its weighted ratio reaches this (0: refuse)
MAX_DEFAULT_JOBS = 6        # a worker on the biggest stations reaches ~7 GB RSS (2026-09-23 dry run)
WORKER_BYTES = 7 << 30      # RAM budget per baking worker (that peak); the default keeps one budget spare
LIGHT_BLEED_MAX = 4.0       # lod_atlas.LIGHT_BLEED_MAX: mean added light luminance (0..255) that flags a tile
LIGHT_BLEED_SCALE_LOSS = 0.15   # lod_atlas.LIGHT_BLEED_SCALE_LOSS: the largest atlas scale drop a repack may cost
LIGHT_BLEED_SHARE = 0.02    # lod_atlas.LIGHT_BLEED_SHARE: the atlased-surface share from which a bleeding tile counts
ATLAS_DEFAULTS = dict(sizes=(1024, 2048), fmt='dxt', specular=False, bump=True, screen_width=effective_width(),
                      min_texels=MIN_TEXELS, texel_floor_share=TEXEL_FLOOR_SHARE, texel_fallback=TEXEL_FALLBACK,
                      light_bleed_max=LIGHT_BLEED_MAX, light_bleed_scale_loss=LIGHT_BLEED_SCALE_LOSS,
                      light_bleed_share=LIGHT_BLEED_SHARE)


def host_memory_bytes():
    """Physical RAM in bytes (sysconf), None when the host does not report it."""
    try:
        return os.sysconf('SC_PAGE_SIZE') * os.sysconf('SC_PHYS_PAGES')
    except (ValueError, OSError, AttributeError):
        return None


def default_jobs():
    """min(cpu - 2, MAX_DEFAULT_JOBS, RAM // WORKER_BYTES - 1), at least 1: 2 on a 24 GiB host."""
    jobs = min((os.cpu_count() or 2) - 2, MAX_DEFAULT_JOBS)
    ram = host_memory_bytes()
    if ram:
        jobs = min(jobs, max(1, ram // WORKER_BYTES - 1))
    return max(1, jobs)


def atlas_collapse(assets, name, entry, mats, record, alpha, threshold, synth, opts):
    """--collapse atlas: (C, synth report incl. the atlas materials, atlas build, extra catalogue members)."""
    import lod_atlas
    if threshold is None:
        raise SystemExit(f'{name}: --collapse atlas sizes the atlas for the switch size; pass NAME=T or --threshold')
    body = qualified_stem(entry['path'])
    try:
        res = lod_atlas.build(assets, body, mats, record, alpha, threshold * opts['screen_width'] / 1280, opts['sizes'],
                              opts['fmt'], opts['specular'], synth, opts['bump'],
                              light_bleed_max=opts.get('light_bleed_max', LIGHT_BLEED_MAX),
                              light_bleed_scale_loss=opts.get('light_bleed_scale_loss', LIGHT_BLEED_SCALE_LOSS),
                              light_bleed_share=opts.get('light_bleed_share', LIGHT_BLEED_SHARE))
    except lod_atlas.AtlasError as exc:
        raise SystemExit(f'{name}: {exc}') from None
    low = res['layout']['min_ratio']
    texel = lod_atlas.texel_floor(lod_atlas.tile_rows(res['layout']), opts.get('min_texels', 0),
                                  opts.get('texel_floor_share', TEXEL_FLOOR_SHARE))
    if texel['refuse']:
        raise SystemExit(f'{name}: texel_floor: tiles below --min-texels {opts["min_texels"]:g} atlas texels per'
                         f' screen pixel at the switch size cover {100 * texel["starved_share"]:.1f} % of the atlased'
                         f' surface, above --texel-floor-share {100 * texel["floor_share"]:g} % (min {low:.2f},'
                         f' area-weighted {texel["weighted_texels_per_px"]:.2f}); it would draw blurred')
    for slot, member in res['members'].items():
        stem = member.rsplit('.', 1)[0]
        taken = [e['source'] for ext in lod_atlas.DDS_LOOKUP[1] for e in assets.candidates(stem + ext)]
        if taken:
            raise SystemExit(f'{name}: atlas texture {stem} already exists in {taken}; it would be shadowed')
    extra = [(res['members'][s], lod_atlas.stored(e['dds'])) for s, e in res['encoded'].items()]
    res['summary'] = lod_atlas.summary(res)
    res['summary']['texel'] = texel
    kept = set(res.get('kept', ()))
    res['summary']['kept_light_bleed_draws'] = sum(1 for p in res['record']['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART
                                                   for g in p['groups'] if g['material'] in kept)
    if res.get('kept_effects'):
        fx = set(res['kept_effects'])
        res['summary']['kept_effect_draws'] = sum(1 for p in res['record']['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART
                                                  for g in p['groups'] if g['material'] in fx)
    return res['record'], res['synth'], res, extra


def plan_body(assets, name, threshold, placement=None, force_threshold=False, collapse='glow', force_mat3=False,
              glow_luma=GLOW_LUMA, glow_share=GLOW_SHARE, area_percent=None, synth=True, atlas_opts=None,
              source_record=None):
    entry = bob1.resolve_body(assets, name)
    if 'loose' in entry:
        raise SystemExit(f'{name}: winning resource is loose file {entry["path"]}; a catalogue cannot override it')
    data = assets.read_entry(entry)
    text = entry['path'].lower().endswith(TEXT_BODY_EXTENSIONS)
    member = entry['path']
    if text:
        # A text winner is compiled to BOB1 (bob1.parse_text) and written as the binary member of
        # the same stem: our slot is the highest catalogue and binary beats text inside a layer.
        if bob1.kind(data) or bytes(data[:3]) == b'BOB':
            raise SystemExit(f'{name}: text_parse_error: {entry["path"]} is a text member holding binary data')
        try:
            tree = bob1.parse_text(data)
        except bob1.FormatError as exc:
            raise SystemExit(f'{name}: text_parse_error: {exc}') from None
        member = entry['path'][:-4] + {'.pbd': '.pbb', '.bod': '.bob'}[entry['path'][-4:].lower()]
        if not text_compiles(tree):
            raise SystemExit(f'{name}: text_parse_error: the compiled text body does not serialise and parse back')
        trailing = 0
    else:
        if bob1.kind(data) != 'BOB1':
            raise SystemExit(f'{name}: {entry["path"]} is not a BOB1 body (magic {data[:4]!r})')
        try:
            tree = bob1.parse(data, MAX_TRAILING)
        except bob1.FormatError as exc:
            if 'trailing bytes' in str(exc):
                raise SystemExit(f'{name}: {exc} (more than the {MAX_TRAILING} the parser tolerates)') from None
            raise
        trailing = tree.get('trailing_bytes', 0)
        if bob1.serialise(tree) != (data[:len(data) - trailing] if trailing else data):
            raise SystemExit(f'{name}: writer does not reproduce this body byte for byte; refusing')
    if not any(t in ('MAT5', 'MAT6') for t, _ in tree['sections']) and not force_mat3:
        raise SystemExit(f'{name}: MAT3 body (no per-body materials); the loader rewrites the coarsest'
                         ' record of such a body with > 3 LODs to material 0x485; --force-mat3 overrides')
    ladder = bob1.lods(tree)
    before = list(ladder)
    placement = placement or default_placement(ladder)
    src_index = len(ladder) - 1 if source_record is None else source_record
    if not 0 <= src_index < len(ladder):
        raise SystemExit(f'{name}: --source-record {source_record} outside the body\'s records 0..{len(ladder) - 1}')
    source = ladder[src_index]
    mats = bob1.materials(tree)
    if collapse == 'atlas' and any(t in ('MAT5', 'MAT6') for t, _ in tree['sections']):
        import lod_atlas               # face groups -N are texture animations: mapped onto their material, atlased
        try:                           # with the start frame; the untouched pad copy keeps -N (drawn by the engine)
            lod_atlas.animated_record(mats, source, assets)
        except lod_atlas.AtlasError as exc:
            raise SystemExit(f'{name}: {exc}') from None
        bad = sorted({g['material'] for l in (source, before[-1]) for p in l['parts'] for g in p['groups']
                      if g['material'] >= len(mats)})
    else:
        bad = sorted({g['material'] for l in (source, before[-1]) for p in l['parts'] for g in p['groups']
                      if not 0 <= g['material'] < len(mats)})
    if bad and any(t in ('MAT5', 'MAT6') for t, _ in tree['sections']):
        raise SystemExit(f'{name}: group material index {bad} outside the material table (0..{len(mats) - 1});'
                         ' the ad signs carry one such negative-index group; refusing')
    alpha = alpha_materials(mats, assets, record=source)
    used = sorted({g['material'] for p in source['parts'] for g in p['groups']})
    glow = glow_materials(assets, mats, used, glow_luma, glow_share) if collapse in KEEPING else set()
    area_kept, area = set(), None
    if collapse == 'glow-area':
        area_kept, covered, total = light_area_materials(assets, mats, source, glow, area_percent)
        area = dict(percent=area_percent, covered=covered, total=total,
                    record_area=sum(group_area(source, g) for p in source['parts'] for g in p['groups']))
    kept = glow | area_kept
    n_mats = len(mats)
    atlas, extra = None, []
    if collapse == 'atlas':
        new, synth_report, atlas, extra = atlas_collapse(assets, name, entry, mats, source, alpha, threshold,
                                                         synth, dict(ATLAS_DEFAULTS, **(atlas_opts or {})))
    else:
        remap, synth_report = synth_materials(mats, source, alpha, collapse, kept) if synth else ({}, [])
        new = coarse_record(source, None, alpha, collapse, kept, remap)
    widest = max((len({i for f in g['faces'] for i in f[:3]}) for p in new['parts'] for g in p['groups']),
                 default=0)
    if widest > MAX_POINTS:
        raise SystemExit(f'{name}: a group of C built from record {src_index} references {widest} points, above'
                         f' {MAX_POINTS} (65,535 less headroom for D3DXCleanMesh): a larger subgroup needs the unqualified 32-bit'
                         ' index path; use --collapse atlas (which splits such groups) or a coarser source record')
    t_1 = before[1]['value'] if len(before) >= 2 else None
    guard_waived = (placement == 'compact' and src_index == 0 and threshold is not None and t_1 is not None
                    and t_1 > threshold and not force_threshold)      # C is the full LOD 0: harmless above T_pad
    new_index, pad_index = place(ladder, new, placement, threshold, name, force_threshold or guard_waived)
    pad_source = None
    if pad_index is not None and src_index != len(before) - 1:
        # the pad only feeds the collision tree (built from the last record at creation,
        # lod-child-hide.md section 3): keep the original coarsest record there
        ladder[pad_index] = dict(before[-1], value=ladder[pad_index]['value'])
        pad_source = len(before) - 1
    out = bob1.serialise(tree)
    back = bob1.parse(out)
    check = bob1.lods(back)
    if [l['value'] for l in check] != [l['value'] for l in ladder]:
        raise SystemExit(f'{name}: re-parse of the written body failed')
    back_mats = bob1.materials(back)
    if (back_mats != mats or len(back_mats) != n_mats + len(synth_report)
            or any(m.get('index') != i for i, m in enumerate(back_mats[n_mats:], n_mats))):
        raise SystemExit(f'{name}: the material table does not parse back as written')
    per_body = any(t in ('MAT5', 'MAT6') for t, _ in back['sections'])   # MAT3 indexes the global table
    if per_body and any(g['material'] >= len(back_mats) for l in check for p in l['parts'] for g in p['groups']):
        raise SystemExit(f'{name}: a group material index is outside the written material table')
    with entry['cat'].with_suffix('.dat').open('rb') as f:
        f.seek(entry['offset'])
        head = bytes(v ^ 0x33 for v in f.read(2))
    stored = gzip.compress(out, mtime=0) if head == b'\x1f\x8b' or member.lower().endswith('.pbb') else out
    return dict(name=name, source=entry['source'], member=member, before=before, ladder=ladder,
                **({'source_member': entry['path']} if text else {}),
                new=new, new_index=new_index, collapse=collapse, glow=glow,
                alpha=alpha | {s['index'] for s in synth_report if s['dominant'] in alpha and not s.get('atlas')},
                area_kept=area_kept, area=area, synth=synth_report, source_materials=n_mats,
                pad_index=pad_index, placement=placement, atlas_build=atlas, extra_members=extra,
                source_record=src_index, pad_source=pad_source, trailing_bytes=trailing,
                guard_waived=guard_waived, atlas=atlas and atlas['summary'],
                source_decoded_sha256=hashlib.sha256(data).hexdigest(),
                overlay_decoded_sha256=hashlib.sha256(out).hexdigest(),
                decoded_bytes=(len(data), len(out)), stored=stored)


def group_area(record, group):
    import body_materials
    return sum(body_materials.face_area(record['points'], f) for f in group['faces'])


def record_bytes(lod):
    w = bob1.Writer()
    bob1.write_lod(w, lod)
    return len(w.b)


def ladder_text(ladder):
    return (f'thresholds {["-"] + [l["value"] for l in ladder[1:]]}'
            f' draws {[bob1.lod_summary(l)["draws"] for l in ladder]}'
            f' record bytes {[record_bytes(l) for l in ladder]}')


def group_kind(plan, g):
    return ('atlas' if plan.get('atlas') and g['material'] in plan['atlas'].get('materials', [plan['atlas']['material']]) else
            'kept' if plan.get('atlas') and g['material'] in plan['atlas'].get('kept_light_bleed', ()) else
            'glow' if g['material'] in plan.get('glow', ()) else
            'light' if g['material'] in plan.get('area_kept', ()) else
            'alpha' if g['material'] in plan['alpha'] else 'opaque')


def group_label(plan, g):
    syn = {s['index']: s['dominant'] for s in plan.get('synth', ())}
    m = g['material']
    return f'{group_kind(plan, g)}:mat{m}' + (f'~{syn[m]}' if m in syn else '') + f':{len(g["faces"])}f'


def describe(plan, out=None):
    out = out or sys.stdout
    sr = plan.get('source_record', len(plan['before']) - 1)
    s_old, s_new = bob1.lod_summary(plan['before'][sr]), bob1.lod_summary(plan['new'])
    print(f'{plan["name"]}: {plan["source"]}:{plan.get("source_member", plan["member"])}', file=out)
    if plan.get('source_member'):
        print(f'  text body compiled to BOB1 (bob1.parse_text); written as {plan["member"]}', file=out)
    if plan.get('trailing_bytes'):
        print(f'  warning: {plan["trailing_bytes"]} stray byte(s) after /BOB in the source member (tolerated up to'
              f' {MAX_TRAILING}; the engine parser returns at /BOB); the overlay member carries none', file=out)
    if plan.get('guard_waived'):
        print(f'  compact guard waived: T_pad {plan["ladder"][plan["pad_index"]]["value"]} is below T_1'
              f' {plan["before"][1]["value"]}; C is the full LOD 0 geometry, so C drawing at Low..High in the'
              ' band above T_pad is harmless', file=out)
    print(f'  ladder before: {ladder_text(plan["before"])}', file=out)
    print(f'  ladder after:  {ladder_text(plan["ladder"])}', file=out)
    groups = [group_label(plan, g) for p in plan['new']['parts'] for g in p['groups']]
    pad_of = ('' if plan.get('pad_source') is None else f' of original LOD{plan["pad_source"]}'
              f' ({len(plan["ladder"][plan["pad_index"]]["points"]) if plan["pad_index"] else 0} points)')
    pad = (f' + pad copy{pad_of} LOD{plan["pad_index"]} threshold={plan["ladder"][plan["pad_index"]]["value"]}'
           if plan['pad_index'] else '')
    th = [l['value'] for l in plan['before'][1:]]
    vh = bob1.drawable([l['value'] for l in plan['ladder'][1:]], 'very-high')
    n = len(plan['before'])
    dropped = (f' (original LOD1..{n - 1} dropped)' if plan['placement'] == 'compact' and n > 1 else '')
    print(f'  {plan["placement"]}: new LOD{plan["new_index"]} threshold={plan["new"]["value"]}{pad}{dropped}'
          f' from source LOD{sr} (points {s_old["points"]}, faces {s_old["faces"]})'
          f' flags={plan["new"]["flags"]:#x} points={s_new["points"]} parts={s_new["parts"]}'
          f' groups/part {s_old["groups_per_part"]} -> {s_new["groups_per_part"]} draws {s_old["draws"]} ->'
          f' {s_new["draws"]} faces={s_new["faces"]} collapse={plan["collapse"]} groups={groups}'
          f' part_flags={[hex(f) for f in s_new["part_flags"]]}', file=out)
    print(f'  main-view drawable at Very High (f=1): before {bob1.drawable(th, "very-high")} after {vh}'
          f' (new record drawable: {plan["new_index"] in vh})', file=out)
    new_th = [l['value'] for l in plan['ladder'][1:]]
    for view in bob1.VIEW_DISTANCE:
        print(f'  {view:>9} drawable {bob1.drawable(new_th, view)} by s: before'
              f' {bob1.format_bands(bob1.selection_bands(th, view))} | after'
              f' {bob1.format_bands(bob1.selection_bands(new_th, view))}', file=out)
    if plan.get('area'):
        a = plan['area']
        print(f'  glow-area {a["percent"]:g} %: light-map materials {sorted(plan["area_kept"])} cover'
              f' {a["covered"] / a["total"] if a["total"] else 0:.3f} of the real-light-map area outside the'
              f' glow set ({a["covered"] / a["record_area"] if a["record_area"] else 0:.3f} of the record);'
              f' glow {sorted(plan["glow"])}', file=out)
    for syn in plan.get('synth', ()):
        rows = ' '.join(f'{n}={dom / 65536:.4g}->{w / 65536:.4g}(mean {mean / 65536:.4g})'
                        for n, dom, mean, w in syn['params'] if dom != w)
        what = 'atlas material' if syn.get('atlas') else 'synthesized'
        print(f'  {what} mat{syn["index"]} = mat{syn["dominant"]} with the area-weighted g_Mat* mean over'
              f' {syn["absorbed"]}: {rows or "(unchanged)"}', file=out)
    if plan.get('atlas'):
        import lod_atlas
        for line in lod_atlas.format_summary(plan['atlas']):
            print('  ' + line, file=out)
    print(f'  decoded bytes {plan["decoded_bytes"][0]} -> {plan["decoded_bytes"][1]},'
          f' stored {len(plan["stored"])} (gzip), overlay sha256 {plan["overlay_decoded_sha256"][:16]}', file=out)


def parse_collapse(text):
    """'glow' / 'two' / 'one' / 'glow-area=P' (0 <= P <= 100) -> (collapse, percent or None)."""
    name, sep, value = text.partition('=')
    if name == 'glow-area':
        try:
            percent = float(value)
        except ValueError:
            raise argparse.ArgumentTypeError('glow-area needs a percentage: --collapse glow-area P') from None
        if not 0 <= percent <= 100:
            raise argparse.ArgumentTypeError(f'glow-area percentage {value} outside 0..100')
        return name, percent
    if sep or name not in COLLAPSES:
        raise argparse.ArgumentTypeError(f'unknown collapse {text!r} (glow, two, one, glow-area P, atlas)')
    return name, None


def join_collapse(argv):
    """Rewrite '--collapse glow-area P' (and '--collapse=glow-area P') to '--collapse glow-area=P'."""
    out, i = [], 0
    argv = list(argv)
    while i < len(argv):
        v = argv[i]
        if v == '--collapse' and argv[i + 1:i + 2] == ['glow-area'] and i + 2 < len(argv):
            out += [v, f'glow-area={argv[i + 2]}']; i += 3
        elif v == '--collapse=glow-area' and i + 1 < len(argv):
            out.append(f'--collapse=glow-area={argv[i + 1]}'); i += 2
        else:
            out.append(v); i += 1
    return out


def aspect_cap(text):
    """--aspect-cap SHIPS,STATIONS (lod_batch_census.parse_aspect_cap; imported late: it imports this module)."""
    import lod_batch_census
    return lod_batch_census.parse_aspect_cap(text)


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bodies', nargs='*', help='body name as the scene references it, or member path;'
                                              ' NAME=T sets that body\'s threshold (overrides --threshold)')
    ap.add_argument('--threshold', type=int,
                    help='compact: T_pad, required, not below the original record 1 threshold;'
                         ' pad: T_pad, required, above every original threshold of records 1..n-1;'
                         ' before-last: T <= T_last, default T_last;'
                         ' append-pad: required, 3 <= T, below T_last for multi-LOD bodies')
    ap.add_argument('--force-threshold', action='store_true',
                    help='compact: accept a T_pad below the record 1 threshold (waived anyway for source record 0);'
                         ' pad: accept a T_pad that does not exceed every original threshold')
    ap.add_argument('--placement', choices=PLACEMENTS, help='placement (default: compact)')
    ap.add_argument('--collapse', type=parse_collapse, default=('glow', None),
                    metavar='{glow,two,one,glow-area P,atlas}',
                    help='glow (default): bright-light-map materials keep their group, the rest as two;'
                         ' glow-area P (or glow-area=P): glow plus the largest real-light-map materials up to'
                         ' P %% of their area; two: opaque + alpha-tested/blended group per part;'
                         ' one: a single group per part; atlas: one opaque atlas material per effect file'
                         ' (+ the alpha group as two)')
    ap.add_argument('--atlas-size', type=int, default=1024,
                    help='atlas: first atlas side tried (power of two, default 1024)')
    ap.add_argument('--atlas-max-size', type=int, default=2048,
                    help='atlas: largest side tried when the smaller one leaves < 2 atlas texels per screen'
                         ' pixel at the switch size (default 2048)')
    ap.add_argument('--display', type=parse_display, default=DISPLAY, metavar='WxH',
                    help='display resolution (default 1920x1080); the texel rule\'s reference width is'
                         ' H * 1280 / 768 (the projection keeps the vertical field of view; module notes)')
    ap.add_argument('--screen-width', type=int,
                    help='override the reference width derived from --display. T is in the 1280-wide reference'
                         ' (real px = s * m00 * width / 1280), so the texel rule is applied at T * W / 1280')
    ap.add_argument('--atlas-format', choices=('dxt', 'a8r8g8b8'), default='dxt',
                    help='atlas: dxt (default; DXT1, DXT5 for a slot with alpha) or a8r8g8b8 (uncompressed)')
    ap.add_argument('--atlas-bump', action=argparse.BooleanOptionalAction, default=True,
                    help='atlas: bake a bump (normal map) atlas and point t_BumpTexture at it (default on;'
                         ' --no-atlas-bump leaves the slot NULL)')
    ap.add_argument('--atlas-specular', action='store_true',
                    help='atlas: also bake a specular atlas (default: t_SpecularTexture NULL; always on in --batch)')
    ap.add_argument('--atlas-preview', type=Path,
                    help='atlas: write atlas_preview_<body>_<slot>.png (<= 512 px, tile outlines) into DIR')
    ap.add_argument('--source-record', type=int, metavar='N',
                    help='build C from record N\'s geometry (default: the coarsest record; 0 = LOD 0, the fine'
                         ' mesh); NAME=T@N (or NAME=T,N) sets it per body. A C group above 60,000 points is refused'
                         ' (atlas: split). When N is not the coarsest record the pad is the original coarsest record')
    ap.add_argument('--no-synth-material', action='store_true',
                    help='merged groups keep the dominant material itself (no area-weighted g_Mat* copy)')
    ap.add_argument('--glow-luma', type=float, default=GLOW_LUMA,
                    help=f'glow: luma above which a light-map texel is bright (default {GLOW_LUMA})')
    ap.add_argument('--glow-share', type=float, default=GLOW_SHARE,
                    help=f'glow: bright-texel share that makes a light map a glow map (default {GLOW_SHARE})')
    ap.add_argument('--replace', action='store_true',
                    help='replace the installed x3m-lod overlay (its slot; originals hash must match)')
    ap.add_argument('--force-running', action='store_true',
                    help='allow --install while X3AP.exe is running or the process table cannot be read. A'
                         ' running game keeps the old CAT/DAT open (and its catalogue index in memory); a DAT'
                         ' it reopens after the swap would not match that index')
    ap.add_argument('--force-mat3', action='store_true', help='accept a MAT3 body (see the module notes)')
    ap.add_argument('--hash-archives', action='store_true',
                    help='hash every installed .dat before and after the run (default: cat sha256 plus dat'
                         ' size and mtime)')
    ap.add_argument('--game', type=Path, default=bob1.DEFAULT_GAME)
    ap.add_argument('--out', type=Path, help='output root (receives addon/NN.cat/.dat)')
    ap.add_argument('--install', action='store_true', help='write into the game directory (never overwrites)')
    ap.add_argument('--slot', type=int, help='addon catalogue number 1..99 (default and required: the next'
                                              ' contiguous free slot)')
    ap.add_argument('--force-slot', action='store_true', help='allow a --slot other than the next contiguous one')
    ap.add_argument('--dry-run', action='store_true', help='print the planned records; write nothing')
    ap.add_argument('--max-dat-bytes', type=int, default=MAX_DAT_BYTES, metavar='N',
                    help=f'largest overlay .dat (default {MAX_DAT_BYTES:,}); --batch splits a larger overlay over'
                         ' consecutive addon slots, each body with its atlases inside one archive; the single-body mode refuses. N above'
                         f' 2^31 - 1 = {DAT_LIMIT:,} splits at that limit (noted in the summary), and a dat above it is'
                         ' never written (the engine seeks members with a signed 32-bit offset)')
    b = ap.add_argument_group('batch mode (module notes, "Batch mode")')
    b.add_argument('--batch', action='store_true',
                   help='fleet-wide overlay over every eligible ship and station (ships/, stations/, others/):'
                        ' rule ships min(200, max(80, 2.5 T_1)), stations 150; source record 0; compact;'
                        ' --collapse atlas with --atlas-specular')
    b.add_argument('--include-other', action='store_true',
                   help='batch: include the other top directories (effects, environments, ...) under the station rule')
    b.add_argument('--only', type=Path, metavar='FILE',
                   help='batch: restrict to the bodies named in FILE (one per line; sectors.txt rows and'
                        ' eligible_bodies.txt NAME=T@N lines accepted; the rule still sets T)')
    b.add_argument('--binary-only', action='store_true',
                   help='batch: leave the winning text bodies (.pbd/.bod) out of the enumeration')
    b.add_argument('--sync', action='store_true',
                   help='batch: reuse the previous overlay\'s members for bodies whose inputs did not change')
    b.add_argument('--jobs', type=int, default=default_jobs(),
                   help='batch: worker processes for the census and the baking (default min(cpu count - 2, 6,'
                        ' RAM // 7 GiB - 1), at least 1: a worker baking one of the biggest stations reaches'
                        ' ~7 GB RSS, and every worker process is replaced after each body)')
    b.add_argument('--min-texels', type=float, default=MIN_TEXELS, metavar='F',
                   help=f'atlas / batch: a tile whose atlas gives fewer than F atlas texels per screen pixel at'
                        f' the display reference is starved (default {MIN_TEXELS}; 0 disables); the body is refused'
                        ' (reason texel_floor) when its starved tiles cover more than --texel-floor-share of the'
                        ' atlased surface; the ratio is still reported for every body')
    b.add_argument('--texel-floor-share', type=float, default=TEXEL_FLOOR_SHARE, metavar='S',
                   help=f'atlas / batch: share (0..1) of the atlased surface (face area) the starved tiles may'
                        f' cover; they are accepted and listed as texel_clamped (default {TEXEL_FLOOR_SHARE};'
                        ' 0 refuses any starved tile)')
    b.add_argument('--texel-fallback', type=float, default=TEXEL_FALLBACK, metavar='W',
                   help='batch: a body the texel floor would refuse at T_pad is built at the lower switch size'
                        ' T_fb = round(T_pad * weighted / W) (rebuilt up to 3 times until the area-weighted ratio'
                        ' reaches W texels/px), T_fb >= max(T_1, T_pad/4, 2), else refused texel_floor (default'
                        f' {TEXEL_FALLBACK:g}; 0 disables)')
    b.add_argument('--light-bleed-max', type=float, default=LIGHT_BLEED_MAX, metavar='Y',
                   help='atlas / batch: light-atlas bleed guard (lod_atlas.guard_bleed): a tile whose light, sampled'
                        ' at the level the widened light-map fetch reads at the switch size (ceil(log2 texels/px) +'
                        ' log2 of the hull light-map widening 4) with a +-1 texel box, gains a mean luminance above'
                        f' Y/255 over its own light map is flagged light_bleed; the atlas is repacked with the emitter'
                        ' tiles in their own region, else the flagged materials keep their own groups (one extra draw'
                        f' each, kept_light_bleed) (default {LIGHT_BLEED_MAX:g}; 0 disables)')
    b.add_argument('--light-bleed-scale-loss', type=float, default=LIGHT_BLEED_SCALE_LOSS, metavar='F',
                   help='atlas / batch: a light-bleed repack is accepted only when the atlas scale drops by at most'
                        ' F (0..1) relative to the original pack; otherwise the flagged tiles keep their own groups'
                        f' (default {LIGHT_BLEED_SCALE_LOSS:g}; 0 allows only a repack at the same scale)')
    b.add_argument('--light-bleed-share', type=float, default=LIGHT_BLEED_SHARE, metavar='S',
                   help='atlas / batch: a light-bleed tile counts for the remedy (repack or kept group) only when'
                        ' its mesh-space face area is at least S (0..1) of the atlased surface; smaller tiles sit'
                        ' at a high mip at the switch size, a few pixels wide, and are reported as'
                        f' light_bleed_ignored with their share (default {LIGHT_BLEED_SHARE:g}; 0 counts every tile)')
    b.add_argument('--aspect-cap', type=aspect_cap,
                   default=(1.5, 2.0), metavar='SHIPS,STATIONS',
                   help='batch: K_max of the aspect factor, T_pad = round(T_class * clamp(k, 1, K_max))'
                        ' (default 1.5,2.0)')
    b.add_argument('--no-aspect', action='store_true', help='batch: T_pad = T_class (no aspect factor)')
    b.add_argument('--radius-log', action='append', type=Path, metavar='FILE',
                   help='batch: flight census text with r=/radius= and body= for the switch distance in km'
                        ' (default: lod_batch_census.RADIUS_SOURCES present)')
    b.add_argument('--budget-mb', type=float, default=512.0,
                   help='batch: warn when a flown sector\'s resident atlas estimate exceeds this (default 512)')
    b.add_argument('--record', type=Path,
                   help='batch: record JSON path (default x3m-lod-batch.json under --out or beside the marker on --install, else in the working'
                        ' directory); *-summary.txt and *-bodies.txt are written beside it')
    return ap


def main(argv=None):
    ap = build_parser()
    a = ap.parse_args(join_collapse(sys.argv[1:] if argv is None else argv))
    a.collapse, a.area_percent = a.collapse
    if a.batch and a.bodies:
        ap.error('--batch takes no body names; restrict the set with --only FILE')
    if not a.batch and not a.bodies:
        ap.error('name at least one body, or pass --batch')
    if a.force_slot and a.slot is None:
        ap.error('--force-slot needs --slot')
    if a.source_record is not None and a.source_record < 0:
        ap.error('--source-record must be >= 0')
    if not (0 <= a.glow_luma < 1 and 0 < a.glow_share <= 1):
        ap.error('--glow-luma must be in [0, 1) and --glow-share in (0, 1]')
    pow2 = lambda v: v >= 64 and not v & (v - 1)
    if not (pow2(a.atlas_size) and pow2(a.atlas_max_size) and a.atlas_size <= a.atlas_max_size <= 8192):
        ap.error('--atlas-size and --atlas-max-size must be powers of two, 64 <= size <= max size <= 8192')
    if a.screen_width is not None and a.screen_width < 320:
        ap.error('--screen-width must be >= 320')
    if a.jobs < 1:
        ap.error('--jobs must be >= 1')
    if a.max_dat_bytes < 1:
        ap.error('--max-dat-bytes must be >= 1')
    sizes, n = [], a.atlas_size
    while n <= a.atlas_max_size:
        sizes.append(n)
        n *= 2
    width = effective_width(a.display, a.screen_width)
    if a.min_texels < 0:
        ap.error('--min-texels must be >= 0')
    if not 0 <= a.texel_floor_share < 1:
        ap.error('--texel-floor-share must be in [0, 1)')
    if a.texel_fallback < 0:
        ap.error('--texel-fallback must be >= 0')
    if not a.light_bleed_max >= 0:
        ap.error('--light-bleed-max must be >= 0')
    if not 0 <= a.light_bleed_scale_loss < 1:
        ap.error('--light-bleed-scale-loss must be in [0, 1)')
    if not 0 <= a.light_bleed_share <= 1:
        ap.error('--light-bleed-share must be in [0, 1]')
    a.atlas_opts = dict(sizes=tuple(sizes), fmt=a.atlas_format, specular=a.atlas_specular or a.batch,
                        bump=a.atlas_bump, screen_width=width, min_texels=a.min_texels,
                        texel_floor_share=a.texel_floor_share, texel_fallback=a.texel_fallback,
                        light_bleed_max=a.light_bleed_max, light_bleed_scale_loss=a.light_bleed_scale_loss,
                        light_bleed_share=a.light_bleed_share)
    a.hash_mode = 'sha256' if a.hash_archives else 'fingerprint'
    game = a.game.resolve()
    root = None
    if not a.dry_run:
        if a.install == (a.out is not None):
            raise SystemExit('pass exactly one of --out DIR or --install')
        if a.install and not a.force_running:
            try:
                lines = running_game()
            except RuntimeError as exc:
                raise SystemExit(f'--install: cannot tell whether the game is running ({exc});'
                                 ' pass --force-running to override') from None
            if lines:
                raise SystemExit(f'--install: the game is running ({lines[0]}); quit it first'
                                 ' (--force-running overrides)')
        root = game if a.install else a.out.resolve()
        if not a.install and (root == game or root.is_relative_to(game)):
            raise SystemExit('--out must be outside the game directory (use --install to target it)')
    elif a.out is not None:
        root = a.out.resolve()
    markers = installed_markers(game)
    for m in markers:
        if m['status'] == 'orphaned':
            print(f'warning: marker {m["path"].name} is orphaned (addon/{m["slot"]:02d}.cat/.dat do not match its'
                  ' hashes: a mod overwrote the slot); that catalogue is read as a mod source'
                  + (' and the marker is removed on --install' if a.install else ''))
        elif m['status'] == 'unreadable':
            print(f'warning: marker {m["path"].name} is unreadable; its catalogue is not read as a source')
    if a.batch:
        a.collapse, a.area_percent = 'atlas', None
        return batch(a, game, root, markers)
    groups = live_overlays(markers)
    live = [m for ms in groups.values() for m in ms]
    replace_slot, prev_slots = None, []
    exclude = our_slots(markers)
    if live and not a.replace:
        raise SystemExit(f'an x3m-lod overlay is already installed ({live[0]["path"].name}); remove it first'
                         ' or pass --replace')
    if a.replace:
        if len(groups) != 1:
            raise SystemExit(f'--replace needs exactly one installed x3m-lod overlay, found {len(groups)}')
        manifest, prev_slots = live[0]['manifest'], [m['slot'] for m in live]
        replace_slot = prev_slots[0]         # the new overlay takes the lowest slot of the previous one
        try:
            expected = manifest['originals_sha256']
        except (KeyError, TypeError) as exc:
            raise SystemExit(f'--replace: unreadable marker {live[0]["path"].name} ({exc})') from None
        mode = manifest.get('originals_mode', 'sha256')
        if archive_digest(game, exclude, mode)[0] != expected:
            raise SystemExit(f'--replace: the installed archives other than our addon slots {sorted(exclude)} do'
                             f' not match the originals {mode} in {live[0]["path"].name}; refusing')
        if a.slot is not None and a.slot != replace_slot:
            raise SystemExit(f'--replace: --slot {a.slot} differs from the installed overlay slot {replace_slot}')
    if replace_slot is not None:
        slot = replace_slot
    elif a.slot is None:
        slot = next_slot(game)
    else:
        slot = a.slot
        if not 1 <= slot <= 99:
            raise SystemExit(f'--slot {slot}: must be 1..99')
        if not a.force_slot and slot != next_slot(game):
            raise SystemExit(f'--slot {slot}: the next contiguous free slot is {next_slot(game)}'
                             ' (pass --force-slot to override)')
    cat_rel = f'addon/{slot:02d}.cat'
    for rel in (cat_rel, f'addon/{slot:02d}.dat', f'addon/{slot:02d}{MARKER_SUFFIX}'):
        if (game / rel).exists() and replace_slot is None:
            raise SystemExit(f'{rel} already exists in the game directory')

    new, _, retire, remove = slot_plan(prev_slots, next_slot(game), 1, slot)
    before = None if a.dry_run else archive_digest(game, exclude, a.hash_mode)[1]
    assets, skipped = original_assets(game, markers)
    if skipped:
        print(f'source bodies read without the overlay catalogue(s) {skipped}')
    plans = []
    for arg in a.bodies:
        name, sep, t = arg.partition('=')
        t, sep_n, n = t.replace(',', '@').partition('@')
        try:
            threshold = int(t) if sep and t else a.threshold
            source_record = int(n) if sep_n else a.source_record
        except ValueError:
            raise SystemExit(f'{arg}: NAME=T[@N] needs an integer threshold and source record') from None
        if source_record is not None and source_record < 0:
            raise SystemExit(f'{arg}: the source record must be >= 0')
        plans.append(plan_body(assets, name, threshold, a.placement, a.force_threshold, a.collapse, a.force_mat3,
                               a.glow_luma, a.glow_share, a.area_percent, not a.no_synth_material, a.atlas_opts,
                               source_record))
    for p in plans:
        p['members'] = [(p['member'], p['stored'])] + list(p['extra_members'])
    check_member_names(plans)
    for p in plans:
        describe(p)
    textures = [m for p in plans for m, _ in p['extra_members']]
    print(f'target {cat_rel} + .dat: {len(plans)} member(s) + {len(textures)} atlas texture(s),'
          f' dat bytes {sum(len(d) for p in plans for _, d in p["members"])}')
    members = [m for p in plans for m in p['members']]
    if dat_bytes(members) > a.max_dat_bytes:
        raise SystemExit(f'{cat_rel}: {dat_bytes(members)} dat bytes exceed --max-dat-bytes {a.max_dat_bytes} (the'
                         ' single-body mode writes one archive; name fewer bodies or use --batch); nothing written')
    check_dat_limit(slot, members)
    if a.dry_run:
        print('dry run: nothing written')
        return 0
    written, moved = commit_outputs(a, game, root, [(slot, members, [body_manifest(p) for p in plans])], prev_slots,
                                    before, exclude, retire=retire, remove=remove)
    if a.atlas_preview:                    # after a successful write only
        import lod_atlas
        a.atlas_preview.mkdir(parents=True, exist_ok=True)
        for p in plans:
            if p['atlas_build']:
                body = Path(p['member']).stem
                for slot_name, e in p['atlas_build']['encoded'].items():
                    path = a.atlas_preview / f'atlas_preview_{body}_{slot_name}.png'
                    level = lod_atlas.preview(e['dds'], p['atlas_build']['layout'], path)
                    print(f'preview {path} (mip {level})')
    print(f'wrote {", ".join(str(w) for w in written)}; {len(before)} original archive files unchanged'
          + (f'; replaced the installed addon/{slot:02d} overlay' if moved else ''))
    return 0


def check_member_names(plans):
    members = [m for p in plans for m, _ in p['members'][:1]]
    if len({m.lower() for m in members}) != len(members):
        raise SystemExit('the same body was named twice')
    textures = [m for p in plans for m, _ in p['members'][1:]]
    if len({m.lower() for m in textures}) != len(textures):
        raise SystemExit('two bodies produce the same atlas texture name (qualified stem collision)')


def drawn_groups(lod):
    """Groups of the record's non-hidden parts (the engine skips HIDDEN_PART parts; lod_atlas)."""
    import lod_atlas
    return sum(len(p['groups']) for p in lod['parts'] if not p['flags'] & lod_atlas.HIDDEN_PART)


def body_manifest(p):
    """Per-body marker record of a plan (single and batch mode); 'draws' counts the drawn groups of
    C (hidden parts excluded), 'groups' every group."""
    return dict(
        name=p['name'], source=p['source'], member=p['member'],
        **({'source_member': p['source_member']} if p.get('source_member') else {}),
        placement=p['placement'], collapse=p['collapse'],
        glow=sorted(p['glow']), area_kept=sorted(p['area_kept']), new_lod=p['new_index'], pad_lod=p['pad_index'],
        source_materials=p['source_materials'], source_record=p['source_record'], pad_source=p['pad_source'],
        trailing_bytes=p.get('trailing_bytes', 0), guard_waived=bool(p.get('guard_waived')),
        synth=[dict(index=s['index'], dominant=s['dominant'], absorbed=s['absorbed'],
                    params=[dict(name=n, dominant=d, mean=round(m, 1), written=w) for n, d, m, w in s['params']],
                    **({'atlas': True, 'effect': s.get('effect')} if s.get('atlas') else {}))
               for s in p['synth']],
        **({'atlas': p['atlas']} if p.get('atlas') else {}),
        source_thresholds=[l['value'] for l in p['before']],
        thresholds=[l['value'] for l in p['ladder']],
        threshold=p['new']['value'],
        pad_threshold=p['ladder'][p['pad_index']]['value'] if p['pad_index'] else None,
        source_decoded_sha256=p['source_decoded_sha256'],
        overlay_decoded_sha256=p['overlay_decoded_sha256'],
        draws=drawn_groups(p['new']), groups=bob1.lod_summary(p['new'])['draws'],
        members=[dict(path=m, sha256=hashlib.sha256(d).hexdigest(), bytes=len(d)) for m, d in p['members']])


def slot_files(root, slot):
    cat = root / f'addon/{slot:02d}.cat'
    return [cat, cat.with_suffix('.dat'), cat.with_name(cat.stem + MARKER_SUFFIX)]


def commit_outputs(a, game, root, layout, prev_slots, before, exclude, manifest_extra=None, retire=(), remove=(),
                   remove_markers=()):
    """Write every (slot, members, bodies) of layout as addon/NN.cat/.dat plus its marker under root, turn
    the slots in retire into retired catalogues and, on --install, delete the previous overlay's slots in
    remove, with the move-aside / restore protocol of the module notes over all slots at once: on --install
    every existing file of a previous-overlay slot (prev_slots, retire, remove) is moved aside first; any
    failure puts every moved file back and removes the new outputs. Any other existing target refuses.
    exclude is the slot set left out of the originals check. On --install the orphaned markers in
    remove_markers are deleted last. Returns (targets, moved)."""
    retire, remove = list(retire), list(remove)
    new = [s for s, _, _ in layout]
    ours = slot_set(prev_slots) | set(retire) | set(remove)
    written = [slot_files(root, s) for s in new]
    retired_files = [slot_files(root, s) for s in retire]
    targets = [p for fs in written + retired_files for p in fs]
    removed = [p for s in remove for p in slot_files(root, s)] if a.install else []
    slot_of = {p: s for s in new + retire + remove for p in slot_files(root, s)}
    replacing = a.install and bool(ours)
    asides = [(w, w.with_name(w.name + REPLACED_SUFFIX)) for w in targets + removed]
    for w in targets:
        if w.exists() and not (replacing and slot_of[w] in ours):
            raise SystemExit(f'refusing to overwrite existing addon/{slot_of[w]:02d} outputs under {root}')
    if replacing:
        for _, aside in asides:
            if aside.exists():
                raise SystemExit(f'{aside} exists (an interrupted --replace?); resolve it by hand')

    moved = []               # (target, aside) pairs actually moved aside, in order
    state = {'writing': False}

    def restore():
        """Put every moved file back (replacing a new output at its path), then, only if all
        of them are back and writing had started, remove the remaining new outputs."""
        stuck = []
        for w, aside in reversed(moved):
            if aside.exists():
                try:
                    os.replace(aside, w)
                except OSError as exc:
                    stuck.append(f'{aside} -> {w}: {exc}')
        if stuck:
            print('error: could not restore the replaced overlay; left in place: ' + '; '.join(stuck),
                  file=sys.stderr)
            return
        if state['writing']:
            restored = {w for w, _ in moved}
            for w in targets:
                if w not in restored:
                    w.unlink(missing_ok=True)
    try:
        if replacing:
            for w, aside in asides:
                if w.exists():
                    w.rename(aside)
                    moved.append((w, aside))
        state['writing'] = True
        write_overlay(a, game, layout, before, written, exclude, manifest_extra, retired_files, retire)
    except BaseException:
        restore()
        raise
    left = []
    for _, aside in moved:                 # last step; a failure here is only a warning
        try:
            aside.unlink()
        except OSError as exc:
            left.append(f'{aside} ({exc})')
    if a.install:
        for path in remove_markers:
            try:
                path.unlink()
                print(f'removed orphaned marker {path.name}')
            except OSError as exc:
                left.append(f'{path} ({exc})')
    if left:
        print('warning: the new overlay is installed but these files could not be removed'
              ' (delete them by hand): ' + '; '.join(left), file=sys.stderr)
    return targets, moved


def retired_member(retire, slot):
    """The one member of a retired catalogue: a tiny text file under a unique x3m_lod/ name that no
    engine loader resolves, so no zero-entry CAT / 0-byte DAT is ever mounted."""
    return (f'x3m_lod/retired_{retire:02d}.txt',
            f'x3m-lod overlay slot {retire:02d} retired; the overlay lives in addon/{slot:02d}\n'.encode())


def write_overlay(a, game, layout, before, written, exclude, manifest_extra=None, retired_files=(), retire=()):
    for slot, members, _ in layout:        # every archive checked before the first byte is written
        check_dat_limit(slot, members)
    group = [s for s, _, _ in layout]
    for (slot, members, bodies), files in zip(layout, written):
        write_catalogue(files[0], members)
        overlay = hash_files(files[:2])
        manifest = dict(tool='tools/analysis/lod_overlay.py', slot=slot, overlay_slots=group, collapse=a.collapse,
                        glow_luma=a.glow_luma, glow_share=a.glow_share, area_percent=a.area_percent,
                        synth_material=not a.no_synth_material, display=list(a.display),
                        screen_width=a.atlas_opts['screen_width'],
                        **({'atlas_options': dict(a.atlas_opts, sizes=list(a.atlas_opts['sizes']))}
                           if a.collapse == 'atlas' else {}),
                        bodies=bodies, originals=len(before), originals_sha256=originals_digest(before),
                        originals_mode=a.hash_mode,
                        overlay_sha256={'cat': overlay[str(files[0])], 'dat': overlay[str(files[1])]},
                        **(manifest_extra or {}))
        files[2].write_text(json.dumps(manifest, indent=1) + '\n')
    for rslot, files in zip(retire, retired_files):
        write_catalogue(files[0], [retired_member(rslot, group[0])])
        rh = hash_files(files[:2])
        files[2].write_text(json.dumps(dict(
            tool='tools/analysis/lod_overlay.py', slot=rslot, retired=True, retired_by=group[0],
            overlay_sha256={'cat': rh[str(files[0])], 'dat': rh[str(files[1])]},
            note='retired catalogue with one inert text member (no body, texture or type resource): the x3m-lod'
                 ' overlay moved to a higher slot because a mod added addon numbers above this one; the file'
                 ' pair keeps the addon numbering contiguous. Engine acceptance of a retired slot is not yet'
                 ' verified in a launch'), indent=1) + '\n')
    after = archive_digest(game, exclude, a.hash_mode)[1]
    ours = {p for fs in list(written) + list(retired_files) for p in fs}
    changed = sorted(k for k in before.keys() | after.keys() if before.get(k) != after.get(k)
                     and not any(Path(k) == w for w in ours))
    if changed:
        raise SystemExit(f'original archives changed during the run ({changed}); outputs removed')


# --- batch mode ----------------------------------------------------------------------------

TOOL_FILES = ('lod_atlas.py', 'lod_overlay.py', 'bob1.py', 'lod_batch_census.py')   # the census decides T_pad


def tool_sha256():
    """sha256 over the source of the three modules that shape an overlay member: a tool change makes
    --sync rebuild every body (the settings no longer match)."""
    h = hashlib.sha256()
    here = Path(__file__).resolve().parent
    for name in TOOL_FILES:
        h.update((here / name).read_bytes())
    return h.hexdigest()


def parse_only(path):
    """Body keys named in FILE: plain names, NAME=T@N lines (eligible_bodies.txt) or lod_batch_census
    sectors.txt rows ('   62 draws ships/argon/argon_TL ...'); '#' starts a comment."""
    import lod_batch_census as census
    names = set()
    for line in Path(path).read_text().splitlines():
        line = line.split('#', 1)[0].strip()
        if not line or line.startswith('=='):
            continue
        tok = line.split()
        if len(tok) >= 3 and tok[1] == 'draws':
            name = tok[2]
        elif tok[0].isdigit() or tok[0].startswith('eligible'):
            continue
        else:
            name = tok[0].split('=', 1)[0]
        names.add(census.body_key(name))
    return names


def bake_body(assets, row, atlas_opts):
    """Plan and bake one census-eligible body for the batch (compact, atlas, source record 0, T_pad
    from the row); returns a compact dict (no records) or dict(name, refused=reason)."""
    import lod_atlas
    t0 = time.time()
    out = io.StringIO()
    try:
        p = plan_body(assets, row['name'], row['t_pad'], 'compact', False, 'atlas', False, GLOW_LUMA, GLOW_SHARE,
                      None, True, atlas_opts, 0)
        describe(p, out)
    except SystemExit as exc:
        return dict(name=row['name'], refused=str(exc), seconds=time.time() - t0)
    except (bob1.FormatError, lod_atlas.AtlasError, FileNotFoundError, ValueError) as exc:
        return dict(name=row['name'], refused=f'{type(exc).__name__}: {exc}', seconds=time.time() - t0)
    finally:
        assets.cache.clear()
    p['members'] = [(p['member'], p['stored'])] + list(p['extra_members'])
    manifest = body_manifest(p)
    manifest['inputs_sha256'] = row.get('inputs_sha256')
    return dict(name=p['name'], member=p['member'], source=p['source'], members=p['members'], manifest=manifest,
                text=out.getvalue(), draws=manifest['draws'], atlas=p['atlas'], reused=False,
                trailing=p.get('trailing_bytes', 0), guard_waived=bool(p.get('guard_waived')),
                seconds=time.time() - t0)


def bleed_fields(atlas):
    """Batch row fields of an atlas summary's light-bleed guard: light_bleed (tiles over the limit),
    light_bleed_counted (those at or above the share, remedied), light_bleed_ignored (the smaller ones: mats,
    level, share, texels, added), kept_light_bleed (materials kept as their own groups), kept_light_bleed_draws
    (their groups in C), light_bleed_remedy; {} when the guard was off or the summary predates it."""
    b = (atlas or {}).get('light_bleed')
    if not b:
        return {}
    ign = [{k: r.get(k) for k in ('mats', 'level', 'share', 'texels', 'added')} for r in b.get('ignored') or []]
    return dict(light_bleed=len(b['flagged']) + len(ign), light_bleed_counted=len(b['flagged']),
                light_bleed_ignored=ign, kept_light_bleed=list(b['kept']),
                kept_light_bleed_draws=(atlas or {}).get('kept_light_bleed_draws', 0), light_bleed_remedy=b['remedy'])


_BAKE = {}


def _bake_init(game, atlas_opts):
    _BAKE['assets'] = original_assets(Path(game))[0]
    _BAKE['atlas_opts'] = atlas_opts


def bake_safely(assets, row, atlas_opts):
    try:
        return bake_body(assets, row, atlas_opts)
    except Exception as exc:                        # one bad body must not end the batch
        return dict(name=row['name'], refused=f'{type(exc).__name__}: {exc}', seconds=0.0)


def _bake_work(row):
    return bake_safely(_BAKE['assets'], row, _BAKE['atlas_opts'])


BAKE_REASONS = (('texel_floor', 'texel_floor'), ('trailing bytes', 'trailing_bytes'), ('text_parse_error', 'text_parse_error'),
                ('writer does not reproduce', 'writer_mismatch'), ('MAT3 body', 'mat3'),
                ('outside the material table', 'material_outside_table'), ('loose file', 'loose_winner'),
                ('already exists in', 'atlas_name_taken'), ('references', 'group_too_large'),
                ('not a BOB1', 'not_bob1'), ('no body resource', 'not_found'))


def bake_reason(message):
    """Refusal code of a bake failure message: the plan_body refusals, then the lod_atlas reasons
    (lod_batch_census.ATLAS_REASONS), else bake_other."""
    import lod_batch_census as census
    for needle, code in BAKE_REASONS:
        if needle in message:
            return code
    code = census.atlas_reason(message)
    return 'bake_other' if code == 'atlas_other' else code


def reuse_previous(prev, eligible, settings, notes):
    """{name: plan} of the eligible bodies whose members can be copied from the previous overlay:
    same batch settings, same inputs_sha256, every member present with its recorded sha256. prev is
    dict(slots, markers) of the previous overlay's live slots; a member is looked up in the body's own slot
    first, then in every other live slot of that overlay (an orphaned slot is not among them: its bodies
    are rebuilt)."""
    out = {}
    names = '/'.join(f'{s:02d}' for s in prev['slots'])
    if any((m['manifest'].get('batch') or {}).get('settings') != settings for m in prev['markers']):
        notes.append(f'--sync: the previous overlay addon/{names} was built with different settings'
                     ' (rule, width or atlas options); every body is rebuilt')
        return out
    entries, dats = {}, {}                 # slot -> {path: entry}; slot -> open dat
    for m in prev['markers']:
        cat = m['path'].with_name(f'{m["slot"]:02d}.cat')
        try:
            entries[m['slot']] = {e['path']: e for e in read_catalogue(cat)}
        except (ValueError, OSError) as exc:
            notes.append(f'--sync: cannot read addon/{m["slot"]:02d}.cat ({exc}); its bodies are rebuilt')
    old = {b['name'].lower(): (b, m['slot']) for m in prev['markers'] if m['slot'] in entries
           for b in m['manifest'].get('bodies', ())}

    def read(slot, e):
        if slot not in dats:
            dats[slot] = stack.enter_context(
                prev['markers'][0]['path'].with_name(f'{slot:02d}.dat').open('rb'))
        dats[slot].seek(e['offset'])
        return bytes(v ^ 0x33 for v in dats[slot].read(e['size']))

    with contextlib.ExitStack() as stack:
        for r in eligible:
            b, home = old.get(r['name'].lower(), (None, None))
            if not b or not b.get('inputs_sha256') or b['inputs_sha256'] != r.get('inputs_sha256'):
                continue
            got, came = [], None
            for mem in b.get('members', ()):
                for slot in [home] + [s for s in entries if s != home]:
                    e = entries[slot].get(mem['path'])
                    if e is not None and e['size'] == mem['bytes']:
                        data = read(slot, e)
                        if hashlib.sha256(data).hexdigest() == mem['sha256']:
                            got.append((mem['path'], data))
                            came = slot if came is None else came
                            break
                else:
                    break
            else:
                if got:
                    out[r['name']] = dict(name=r['name'], member=b['member'], source=b['source'], members=got,
                                          manifest=dict(b, reused_from=came), draws=b.get('draws'),
                                          atlas=b.get('atlas'), reused=True, trailing=b.get('trailing_bytes', 0),
                                          guard_waived=b.get('guard_waived', False), seconds=0.0,
                                          text=f'{r["name"]}: reused from addon/{came:02d} ({len(got)} members,'
                                               f' inputs {r["inputs_sha256"][:16]})\n')
    return out


def batch(a, game, root, markers):
    import lod_atlas
    import lod_batch_census as census
    t_start = time.time()
    notes = []
    orphaned = [m for m in markers if m['status'] == 'orphaned']
    groups = live_overlays(markers)
    if len(groups) > 1:
        raise SystemExit(f'--batch: more than one live x3m-lod overlay'
                         f' ({[m["path"].name for ms in groups.values() for m in ms]}); resolve by hand')
    prev = None
    if groups:
        (ms,) = groups.values()
        prev = dict(slots=[m['slot'] for m in ms], markers=ms)
    nxt = next_slot(game)
    top = []                               # previous live slots forming the top of the addon numbering
    while prev is not None and nxt - 1 - len(top) in prev['slots']:
        top.insert(0, nxt - 1 - len(top))
    slot = top[0] if top else nxt          # the first slot; the count follows from the dat sizes after baking
    if a.slot is not None and a.slot != slot and not a.force_slot:
        raise SystemExit(f'--slot {a.slot}: the batch slot is {slot} (pass --force-slot to override)')
    if a.slot is not None and a.force_slot:
        slot = a.slot
    exclude = our_slots(markers)           # the originals hash leaves out every slot of ours
    for m in orphaned:
        notes.append(f'orphaned marker {m["path"].name}: addon/{m["slot"]:02d} was overwritten by a mod; read as a'
                     ' source' + ('; the marker is removed on --install' if a.install else ''))
    mods = sorted((game / 'addon' / 'mods').glob('*.cat'))
    only = parse_only(a.only) if a.only else None
    width = a.atlas_opts['screen_width']
    opts = dict(sizes=a.atlas_opts['sizes'], include_other=a.include_other, widths=(width,),
                texel=dict(min_texels=a.min_texels, floor_share=a.texel_floor_share, fallback=a.texel_fallback),
                rule=dict(census.RULE, aspect=not a.no_aspect, aspect_ship=a.aspect_cap[0],
                          aspect_station=a.aspect_cap[1]))
    settings = dict(rule=opts['rule'], screen_width=width, display=list(a.display), collapse='atlas',
                    source_record=0, placement='compact', include_other=a.include_other,
                    atlas=dict(a.atlas_opts, sizes=list(a.atlas_opts['sizes'])), tool_sha256=tool_sha256())
    t0 = time.time()
    rows, skipped = census.run(game, opts, a.jobs, only=only, include_text=not a.binary_only)
    census.attach_world(rows, census.world_radii(a.radius_log or census.default_radius_logs()))
    census_s = time.time() - t0
    rows.sort(key=lambda r: r['name'].lower())
    if skipped:
        notes.append(f'source bodies read without the overlay catalogue(s) {skipped}')
    floor, starved = [], {}                # texel floor: refused before baking, ratio kept in the record
    for r in rows:
        est = (r.get('atlas') or {}).get(width, {})
        ratio = est.get('ratio')
        if est.get('tiles') is not None:
            r['texel'] = lod_atlas.texel_floor(est['tiles'], a.min_texels, a.texel_floor_share)
        if r['eligible'] and a.min_texels and ratio is not None and r.get('texel', {}).get('refuse', False):
            r['refuse'].append('texel_floor')
            r['eligible'] = False
            r['ratio'] = ratio
            floor.append((r['name'], ratio))
            starved[r['name']] = r['texel']['starved_share']
    eligible = [r for r in rows if r['eligible']]
    reused = {}
    if a.sync:
        if prev is None:
            notes.append('--sync: no previous overlay; every body is built')
        else:
            reused = reuse_previous(prev, eligible, settings, notes)
    to_bake = [r for r in eligible if r['name'] not in reused]
    t0 = time.time()
    if a.jobs <= 1 or len(to_bake) <= 1:
        assets, _ = original_assets(game, markers)
        results = [bake_safely(assets, r, a.atlas_opts) for r in to_bake]
    elif to_bake:
        with multiprocessing.get_context('spawn').Pool(min(a.jobs, len(to_bake)), _bake_init,
                                                       (str(game), a.atlas_opts), maxtasksperchild=1) as pool:
            results = pool.map(_bake_work, to_bake, chunksize=1)
    else:
        results = []
    bake_s = time.time() - t0
    by_name = {r['name']: r for r in rows}
    plans = []
    for res in results:
        row = by_name[res['name']]
        if 'refused' in res:
            row['refuse'].append('bake:' + bake_reason(res['refused']))
            row['bake_error'] = res['refused'][:200]
            row['eligible'] = False
        else:
            plans.append(res)
    plans += list(reused.values())
    plans.sort(key=lambda p: p['name'].lower())
    check_member_names(plans)
    packed = pack_slots(plans, a.max_dat_bytes, slot)   # refuses before the record or any archive is written
    cap = min(a.max_dat_bytes, DAT_LIMIT)
    if cap < a.max_dat_bytes:
        notes.append(f'--max-dat-bytes {a.max_dat_bytes} is above 2^31 - 1; the overlay is split at {cap} (the'
                     ' engine seeks catalogue members with a signed 32-bit offset)')
    new, replaced, retire, remove = slot_plan(prev['slots'] if prev else (), nxt, max(1, len(packed)), slot)
    layout = [(s, [m for p in ps for m in p['members']], [p['manifest'] for p in ps]) for s, ps in zip(new, packed)]
    slot_rows = [dict(slot=s, bodies=len(bs), members=len(ms), bytes=dat_bytes(ms)) for s, ms, bs in layout]
    if retire:
        notes.append(f'previous overlay addon/{"/".join(f"{s:02d}" for s in prev["slots"])} is no longer at the top of'
                     f' the addon numbering (mod catalogues up to addon/{nxt - 1:02d}); the new overlay takes'
                     f' addon/{new[0]:02d}..{new[-1]:02d} and {", ".join(f"addon/{s:02d}" for s in retire)} become'
                     ' valid empty catalogues (marker: retired)')
    for p in plans:
        row = by_name[p['name']]
        atlas_bytes = sum(len(d) for _, d in p['members'][1:])
        row['baked'] = dict(reused=p['reused'], draws=p['draws'], atlas_size=(p['atlas'] or {}).get('size'),
                            ratio=(p['atlas'] or {}).get('min_texels_per_px'), atlas_bytes=atlas_bytes,
                            member_bytes=len(p['members'][0][1]), seconds=round(p['seconds'], 2),
                            guard_waived=p['guard_waived'],
                            atlas_materials=len((p['atlas'] or {}).get('materials', [0])),
                            **bleed_fields(p['atlas']))
        if 'atlas' in row and width in row['atlas']:
            row['atlas'][width]['bytes'] = row['atlas'][width]['bytes_cap2048'] = atlas_bytes
    built = [p for p in plans if not p['reused']]
    # sectors and budget
    by_key = {census.body_key(r['name']): r for r in rows}
    repo = Path(__file__).resolve().parents[2]
    sector_lines, sectors, budget = [], {}, []
    for label, path, frame in census.SECTORS:
        p = repo / path
        if not p.exists():
            sector_lines.append(f'== {label}: {path} missing')
            continue
        lines, n_el, tot = census.sector_report(f'{label} ({path} frame {frame})', census.parse_census(p, frame),
                                                by_key, (width,))
        sector_lines += lines
        sectors[label] = dict(eligible=n_el, atlas_bytes=tot[width][0])
        if tot[width][0] > a.budget_mb * 1e6:
            budget.append(f'warning: sector {label}: resident atlas estimate {tot[width][0] / 1e6:.2f} MB exceeds'
                          f' --budget-mb {a.budget_mb:g}')
    mod_notes = []
    for mc in mods:
        try:
            keys = {census.body_key(e['path']) for e in read_catalogue(mc)
                    if e['path'].lower().endswith(bob1.BODY_EXTENSIONS)}
        except (ValueError, OSError) as exc:
            mod_notes.append(f'warning: addon/mods/{mc.name}: unreadable ({exc})')
            continue
        hit = sum(1 for p in plans if census.body_key(p['name']) in keys)
        mod_notes.append(f'warning: addon/mods/{mc.name} ({len(keys)} bodies) overrides the overlay for {hit} of its'
                         f' {len(plans)} bodies while that mod is selected in the launcher')
    # summary
    cnt = lambda it: dict(sorted(Counter(it).items(), key=lambda x: (-x[1], x[0])))
    reasons = cnt(x for r in rows for x in r['refuse'])
    filters = cnt(x for r in rows if not r['refuse'] for x in r['filter'])
    cats = cnt(r['cat'] for r in rows)
    sizes = cnt(p['atlas']['size'] for p in plans if p['atlas'])
    ratios = sorted((p['atlas']['min_texels_per_px'], p['name']) for p in plans if p['atlas']
                    and p['atlas'].get('min_texels_per_px') is not None)
    below1 = [n for r, n in ratios if r < 1.0]
    below2 = sum(1 for r, _ in ratios if r < 2.0)
    atlas_total = sum(len(d) for p in plans for _, d in p['members'][1:])
    member_total = sum(len(p['members'][0][1]) for p in plans)
    draws_before = sum(by_name[p['name']].get('r0_drawn', 0) for p in plans)
    draws_after = sum(p['draws'] for p in plans)
    multi = [p['name'] for p in plans if p['atlas'] and len(p['atlas'].get('materials', [0])) > 1]
    uv2 = [p['name'] for p in plans if p['atlas'] and p['atlas'].get('uv2_points')]
    per_body = bake_s / len(built) if built else 0.0
    candidates = sum(1 for r in rows if 'text_parse_error' not in r['refuse'] and 'category_other' not in r['filter'])
    full_est = per_body * len(rows) if only is not None else bake_s
    trailing = sum(1 for p in plans if p['trailing'])
    waived = sum(1 for p in plans if p['guard_waived'])
    # texel fallback, after the bake: accepted (built or refused at bake) and the texel_floor refusals split
    # by why the fallback did not save them (guard, W not reached, no fallback tried)
    fb_of = lambda r: r.get('texel_fallback') or {}
    fallback = [r for r in rows if fb_of(r).get('accepted') and not r['filter']
                and all(x.startswith('bake:') for x in r['refuse'])]
    fb_baked = [r for r in fallback if r['eligible']]
    fb_bake_refused = [r for r in fallback if not r['eligible']]
    tf_rows = [r for r in rows if 'texel_floor' in r['refuse']]
    fb_guard = [r for r in tf_rows if fb_of(r).get('guard')]
    fb_short = [r for r in tf_rows if fb_of(r) and not fb_of(r).get('guard')]
    fb_none = [r for r in tf_rows if not fb_of(r)]
    fb_line = lambda rs, bake=False: ''.join(
        f'; {r["name"]} {census.fallback_text(r["texel_fallback"])}'
        + (f' REFUSED at bake ({",".join(r["refuse"])})' if bake else '') for r in rs)
    clamped_bodies = [r['name'] for r in rows if r['eligible'] and (r.get('texel') or {}).get('texel_clamped')]
    bleed_rows = [(p['name'], bleed_fields(p['atlas'])) for p in plans if bleed_fields(p['atlas']).get('light_bleed')]
    bleed_off = not a.light_bleed_max
    texel_lines = [f'texel rule per body at {width} wide (k = aspect factor, T = T_pad, r_body = layout radius in'
                   f' body units, r_world = flown radius, D = r_world*640/T in km at T_class->T_pad ({census.UNITS_PER_M:g}'
                   f' units/m, inferred); ratios in atlas texels per screen pixel at the switch size; weighted = ratio'
                   f' at the {a.texel_floor_share:g} area quantile; starved = share below --min-texels'
                   f' {a.min_texels:g}; lowest weighted first):']
    for r in sorted((r for r in rows if r.get('texel')),
                    key=lambda r: (r['texel']['weighted_texels_per_px'] is None,
                                   r['texel']['weighted_texels_per_px'] or 0.0, r['name'].lower())):
        x, est = r['texel'], r['atlas'][width]
        w = x['weighted_texels_per_px']
        texel_lines.append(
            f'  {r["name"]} {census.aspect_text(r)} thr={",".join(str(t) for t in r.get("thresholds", ())) or "-"}'
            f' px={r.get("t_pad", 0) * width / 1280:.1f}'
            f' size={est.get("size")} min={est["ratio"]:.3f} weighted={"-" if w is None else f"{w:.3f}"}'
            f' starved={100 * x["starved_share"]:.2f}% clamped={len(x["texel_clamped"])}'
            f'{" layout=clamped" if est.get("clamped") else ""}'
            f' {"ELIGIBLE" if r["eligible"] else "refuse=" + ",".join(r["refuse"] + r["filter"])}'
            + census.bleed_text(r)
            + (f' texel_fallback="{census.fallback_text(r["texel_fallback"])}"' if r.get('texel_fallback') else ''))
    summary = [
        f'batch: game {game}; display {a.display[0]}x{a.display[1]} -> reference width {width}; rule ships'
        f' T_class = min({opts["rule"]["t_cap"]:g}, max({opts["rule"]["ship_min"]:g}, {opts["rule"]["ship_factor"]:g} x T_1)),'
        f' stations/others {opts["rule"]["station_t"]:g}; aspect factor '
        + (f'K_max ships {opts["rule"]["aspect_ship"]:g} stations {opts["rule"]["aspect_station"]:g}'
           f' ({sum(1 for r in rows if r.get("t_pad") != r.get("t_class"))} bodies with T_pad != T_class)'
           if opts['rule']['aspect'] else 'off (--no-aspect)')
        + f'; source record 0, compact (T_1 guard waived: {waived} bodies);'
        f' atlas sizes {list(a.atlas_opts["sizes"])}, {a.atlas_opts["fmt"]}, specular on',
        f'bodies enumerated {len(rows)}{" (--only " + str(a.only) + ")" if a.only else ""}: '
        + ', '.join(f'{k} {v}' for k, v in cats.items())
        + f'; eligible {len(eligible)}; built {len(built)} + reused {len(reused)} = {len(plans)} overlay bodies',
        'refused by reason (a body counts once per reason): '
        + (', '.join(f'{k} {v}' for k, v in reasons.items()) or 'none'),
        'filtered: ' + (', '.join(f'{k} {v}' for k, v in filters.items()) or 'none'),
        f'atlas sizes {sizes}; atlas bytes {atlas_total / 1e6:.2f} MB (stored, gzip DDS); body members'
        f' {member_total / 1e6:.2f} MB; dat bytes {atlas_total + member_total}',
        f'texels per px at {width} wide: below 1.0 {len(below1)}'
        + (f' ({", ".join(below1[:12])}{", ..." if len(below1) > 12 else ""})' if below1 else '')
        + f', below 2.0 {below2}, of {len(ratios)} built; refused texel_floor (tiles below --min-texels'
          f' {a.min_texels:g} cover more than --texel-floor-share {a.texel_floor_share:g} of the surface) {len(floor)}'
        + (': ' + ', '.join(f'{n} {100 * starved[n]:.1f} % (min {r:.2f})'
                            for n, r in sorted(floor, key=lambda x: -starved[x[0]])[:12])
           + (', ...' if len(floor) > 12 else '') if floor else '')
        + f'; eligible with texel_clamped tiles (starved share <= {a.texel_floor_share:g}) {len(clamped_bodies)}'
        + (f' ({", ".join(clamped_bodies[:12])}{", ..." if len(clamped_bodies) > 12 else ""})' if clamped_bodies else ''),
        (f'texel_fallback (--texel-fallback {a.texel_fallback:g}: T_fb = round(T_pad x weighted / W), T_fb >='
         f' max(T_1, T_pad/4, 2)): built {len(fb_baked)}' + fb_line(fb_baked)
         + f'; refused at bake {len(fb_bake_refused)}' + fb_line(fb_bake_refused, True)
         + f'; texel_floor {len(tf_rows)} = at the guard {len(fb_guard)}' + fb_line(fb_guard)
         + f' + W not reached in {census.FALLBACK_STEPS} steps {len(fb_short)}' + fb_line(fb_short)
         + f' + no fallback {len(fb_none)}')
        if a.texel_fallback else 'texel_fallback off (--texel-fallback 0)',
        f'draws below T_pad per instance, summed over the overlay bodies: {draws_before} -> {draws_after}'
        f' (drawn groups of record 0 -> of C, hidden parts excluded, alpha groups included); bodies with one atlas'
        f' material per effect ({len(multi)}):'
        f' {", ".join(multi[:12])}{", ..." if len(multi) > 12 else ""}; second UV set passed through ({len(uv2)}):'
        f' {", ".join(uv2[:12])}{", ..." if len(uv2) > 12 else ""}; stray trailing bytes tolerated: {trailing}',
        ('light_bleed off (--light-bleed-max 0)' if bleed_off else
         f'light_bleed (--light-bleed-max {a.light_bleed_max:g}: mean added light luminance at L = ceil(log2 texels/px)'
         f' + {lod_atlas.WIDEN_LEVELS}, counted from share {a.light_bleed_share:g}): bodies {len(bleed_rows)},'
         f' tiles {sum(b["light_bleed"] for _, b in bleed_rows)}'
         f' (counted {sum(b["light_bleed_counted"] for _, b in bleed_rows)},'
         f' ignored {sum(len(b["light_bleed_ignored"]) for _, b in bleed_rows)}),'
         f' kept groups {sum(b["kept_light_bleed_draws"] for _, b in bleed_rows)}'
         + ''.join(f'; {n}{census.bleed_text(dict(baked=b))} remedy={b["light_bleed_remedy"]}'
                   for n, b in bleed_rows)),
        f'timing: census {census_s:.1f} s, baking {bake_s:.1f} s for {len(built)} bodies with {a.jobs} jobs'
        f' ({per_body:.2f} s per body wall); extrapolated full set: {full_est:.0f} s'
        + (f' (this run x {len(rows)} enumerated / {len(built)} built; upper bound, every candidate baked)'
           if only is not None else ' (this run is the full set)') + f'; total {time.time() - t_start:.1f} s',
        f'target addon/{slot:02d}.cat + .dat' + (f' .. addon/{new[-1]:02d}' if len(new) > 1 else '')
        + f' ({len(new)} slot{"s" if len(new) > 1 else ""}, dat cap {cap}): '
        + ', '.join(f'addon/{r["slot"]:02d} {r["bodies"]} bodies {r["members"]} members {r["bytes"]} B'
                    for r in slot_rows)
        + (f'; replaces the previous overlay in {", ".join(f"addon/{s:02d}" for s in replaced)}' if replaced else '')
        + ''.join(f'; addon/{s:02d} retired to an empty catalogue' for s in retire)
        + (f'; previous overlay slots removed on --install: {", ".join(f"addon/{s:02d}" for s in remove)}'
           if remove else '')
        + (f'; orphaned markers: {[m["path"].name for m in orphaned]}' if orphaned else '')]
    summary += notes + mod_notes + sector_lines + budget + texel_lines
    for line in summary:
        print(line)
    # record
    if a.record is not None:
        record_path = a.record
    elif root is not None:                 # --out DIR, or the game's addon/ on --install (beside the marker)
        record_path = (root / 'addon' if a.install else root) / 'x3m-lod-batch.json'
    else:
        record_path = Path.cwd() / f'x3m-lod-batch-{slot:02d}.json'
    record = dict(
        tool='tools/analysis/lod_overlay.py --batch', dry_run=bool(a.dry_run), install=bool(a.install), game=str(game),
        slot=slot, slots=slot_rows, max_dat_bytes=a.max_dat_bytes, dat_cap=cap, retired_slot=retire[0] if retire else None,
        retired_slots=retire, removed_slots=remove, previous_slot=prev['slots'][0] if prev else None,
        previous_slots=prev['slots'] if prev else [], sync=bool(a.sync),
        settings=settings, only=str(a.only) if a.only else None, binary_only=a.binary_only, jobs=a.jobs,
        counts=dict(enumerated=len(rows), by_category=cats, eligible=len(eligible), built=len(built),
                    reused=len(reused), overlay_bodies=len(plans), candidates=candidates),
        refused=reasons, filtered=filters, atlas_sizes=sizes,
        bytes=dict(atlas=atlas_total, members=member_total, dat=atlas_total + member_total),
        ratio=dict(below_1=below1, below_2=below2, measured=len(ratios), min_texels=a.min_texels,
                   texel_floor={n: round(r, 4) for n, r in floor}, texel_floor_share=a.texel_floor_share,
                   texel_floor_starved={n: round(starved[n], 4) for n, _ in floor},
                   texel_clamped=clamped_bodies, texel_fallback_w=a.texel_fallback,
                   texel_fallback={r['name']: dict(t_pad=r['texel_fallback']['t_pad'], t_fb=r['t_pad'],
                                                   km=r['texel_fallback'].get('km_after'),
                                                   built=r['eligible'],
                                                   **({} if r['eligible'] else {'refused': r['refuse']}))
                                   for r in fallback},
                   texel_fallback_guard={r['name']: r['texel_fallback']['guard'] for r in fb_guard},
                   texel_fallback_not_reached=[r['name'] for r in fb_short]),
        draws=dict(record0=draws_before, overlay=draws_after), mixed_effect_bodies=multi, uv2_bodies=uv2,
        light_bleed=dict(max=a.light_bleed_max, widen_levels=lod_atlas.WIDEN_LEVELS, share=a.light_bleed_share,
                         bodies={n: b for n, b in bleed_rows}),
        timing=dict(census_s=round(census_s, 2), bake_s=round(bake_s, 2), per_body_s=round(per_body, 3),
                    extrapolated_full_s=round(full_est, 1), total_s=round(time.time() - t_start, 2)),
        sectors=sectors, budget_mb=a.budget_mb, budget_warnings=budget, notes=notes + mod_notes,
        orphaned_markers=[m['path'].name for m in orphaned],
        bodies=[dict(name=r['name'], cat=r['cat'], member=r.get('member'), t_pad=r.get('t_pad'),
                     t_pad_below_t1=r.get('t_pad_below_t1', False), r0_drawn=r.get('r0_drawn'),
                     refuse=r['refuse'], filter=r['filter'], eligible=r['eligible'],
                     trailing=r.get('trailing', 0), inputs_sha256=r.get('inputs_sha256'),
                     **({'ratio': round(r['ratio'], 4)} if 'ratio' in r else {}),
                     texture_sources=r.get('texture_sources'),
                     estimate=({k: v for k, v in r['atlas'][width].items() if k != 'tiles'}
                               if (r.get('atlas') or {}).get(width) else None), **r.get('baked', {}),
                     **{k: r[k] for k in ('thresholds', 't_class', 'aspect_k', 'threshold_aspect', 'aspect_note',
                                          'radius_body', 'radius_world', 'switch_km', 'switch_km_class') if k in r},
                     **({'texel': r['texel']} if r.get('texel') else {}),
                     **({'texel_fallback': r['texel_fallback']} if r.get('texel_fallback') else {}),
                     **({'error': r['atlas_error']} if r.get('atlas_error') else {}),
                     **({'bake_error': r['bake_error']} if r.get('bake_error') else {}))
                for r in rows],
        summary=summary)

    def write_record():
        """The record and its two text files; on a real run only after commit_outputs succeeded, so a failed
        or refused install leaves the previous record (which describes the installed overlay) untouched."""
        record_path.parent.mkdir(parents=True, exist_ok=True)
        record_path.write_text(json.dumps(record, indent=1, default=str) + '\n')
        stem = record_path.with_suffix('')
        Path(str(stem) + '-summary.txt').write_text('\n'.join(summary) + '\n')
        Path(str(stem) + '-bodies.txt').write_text(''.join(p['text'] + '\n' for p in plans))
        print(f'record {record_path} (+ -summary.txt, -bodies.txt)')
    if a.dry_run:
        write_record()
        print('dry run: nothing written')
        return 0
    if not plans:
        raise SystemExit('--batch: no eligible body; nothing to write')
    before = archive_digest(game, exclude, a.hash_mode)[1]
    extra = dict(batch=dict(settings=settings, counts=record['counts'], timing=record['timing'], slots=slot_rows,
                            retired_slot=retire[0] if retire else None, retired_slots=retire, removed_slots=remove,
                            record=str(record_path)))
    written, moved = commit_outputs(a, game, root, layout, prev['slots'] if prev else (), before, exclude, extra,
                                    retire, remove, [m['path'] for m in orphaned])
    write_record()
    print(f'wrote {", ".join(str(w) for w in written)}; {len(before)} original archive files unchanged'
          + (f'; replaced the installed {", ".join(f"addon/{s:02d}" for s in replaced)} overlay'
             if moved and replaced else '')
          + ''.join(f'; retired addon/{s:02d}' for s in retire)
          + (f'; removed the previous overlay slots {", ".join(f"addon/{s:02d}" for s in remove)}'
             if remove and a.install else ''))
    return 0


def cli():
    try:
        return main()
    except (FileNotFoundError, bob1.FormatError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(cli())
