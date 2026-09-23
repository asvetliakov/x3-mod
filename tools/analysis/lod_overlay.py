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
ships' lattice materials have both off and draw opaque, run257). Collapse
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
    size T (the NAME=T / --threshold value). The atlases are added to the same
    catalogue as dds/x3m_lod_<body>_<slot>.pck (new names; nothing is shadowed).
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
A body whose winning resource is loose is refused.

Safety: the game's archives are only read. Every installed CAT/DAT is hashed
before and after a real run; any change fails the run and removes the outputs.
Outputs go to --out DIR (mirroring the game layout); the game directory is a
target only with --install, which refuses to overwrite anything. Source bodies
are always read with every marker-carrying catalogue skipped. While an overlay is
installed (addon/NN.x3m-lod.json) the tool refuses unless --replace: then the
marker's originals hash must match the installed archives other than slot NN,
the new overlay takes slot NN. With --install the old three files are renamed to
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
below 2 texels/px: the ratio is reported per body and the batch counts bodies below 1.0; a body
whose atlas would fall below --min-texels (default 0.5) is refused as texel_floor with its ratio.

Batch mode (--batch; docs/architecture/merged-lod-feasibility.md "Batch mode"): one command
builds an overlay over every eligible ship and station of the installed game, vanilla plus
every numbered addon catalogue a mod adds. The census module (lod_batch_census.run, with
.bob members, text bodies and the trailing-byte tolerance) enumerates every winning body of
ships/, stations/ and others/ (--include-other adds the rest under the station rule), applies
the rule ships T_pad = min(200, max(80, 2.5*T_1)) (80 for a single-record body), stations and
others 150, source record 0, compact placement, --collapse atlas with --atlas-specular on, and
bakes the eligible bodies in worker processes (--jobs, default min(cpu-2, 6, RAM // 7 GiB - 1),
at least 1, so 2 on a 24 GiB host: a worker holds one body's decoded textures at a time and
reaches ~7 GB RSS on the biggest stations; every worker process is replaced after one body). --only FILE restricts the run to the bodies named in FILE
(one per line; lod_batch_census sectors.txt rows and eligible_bodies.txt NAME=T@N lines are
accepted, the rule still decides T). The compact guard "T_pad not below T_1" is waived
automatically when the source record is 0: C is then the full LOD 0 geometry, so C drawing in
the Low..High band T_pad*f <= s < T_1*f (where the guard would otherwise refuse) is harmless;
the guard stays for decimated sources (a coarser source record). Refusal reasons: text_body
(a .pbd/.bod winner), ambiguous_body_ext (both a binary and a text member), trailing_bytes
(more than MAX_TRAILING stray bytes after /BOB; up to MAX_TRAILING are tolerated with a
warning, the parser 0x00481aa0 returns at /BOB and never reads them), material_outside_table
(a negative group material index, the ad signs), occlusion_mismatch (second UV set with
differing occlusion decals inside one merged group), mat3, no_opaque, dominant_slot_missing,
texture_unresolved, pil_missing (a jpg/tga/bmp texture without Pillow), and the lod_atlas
reasons. Mixed effects and the second UV set are handled, not refused (lod_atlas notes).
Atlas member names are dds/x3m_lod_<stem>_<hash6>_<slot>.pck with the hash from the member
path (qualified_stem), unique within an overlay and stable across runs.

Markers and slots in batch mode: the overlay slot is the next contiguous free addon number.
Every run validates every addon/NN.x3m-lod.json marker by hash (installed_markers): the
marker records the overlay cat/dat sha256, and a marker whose hashes do not match the files
beside it is orphaned (a mod overwrote the slot): it is reported, its catalogue is read as a
source like any mod catalogue, and --install removes the orphaned marker. A live marker (valid
hashes, or a legacy marker without them) names the previous overlay: batch mode supersedes it
without --replace. If its slot is still the highest addon number the new overlay takes that
slot (the --replace move-aside/rollback path); otherwise (a mod added higher numbers) the new
overlay goes to the next slot and the old slot's cat/dat are replaced by a retired catalogue
holding one inert text member x3m_lod/retired_NN.txt (never a zero-entry CAT or 0-byte DAT)
with a marker recording it as retired (contiguity must hold; the engine stops at the first
gap; engine acceptance of a retired slot still needs a launch). A legacy marker (no overlay
hashes, the pilot's shape) is trusted only when every body it names is in the catalogue beside
it with the recorded overlay_decoded_sha256 (legacy_verified); otherwise it is orphaned, so a
mod that overwrote that slot is neither skipped as a source nor retired or replaced. --sync
rebuilds only the bodies whose inputs changed (inputs_sha256 over the decoded body and every
texture its tiles read) or that are new, and copies the other bodies' members (body + atlases,
verified by sha256) from the previous overlay dat; the previous overlay must have been built
with the same rule, width, atlas options and tool sources (tool_sha256 over lod_atlas.py,
lod_overlay.py and bob1.py in the settings). addon/mods/*.cat
are detected and a warning gives how many overlay bodies a selected mod would override.
The before/after archive check is a cat sha256 plus dat size and mtime by default
(--hash-archives hashes every dat; mod trees are gigabytes); the marker records the mode.
The batch writes a record JSON (--record, default x3m-lod-batch.json under --out, beside the marker on --install, else in the
working directory) with the counts by reason, atlas sizes and bytes, the texels/px ratio per
body, the per-sector resident estimate (lod_batch_census.sector_report over its census files;
--budget-mb N, default 512, warns when a sector exceeds N), per-body timings and the
extrapolated full-set wall time, and a *-bodies.txt log with every body's plan.

  python3 tools/analysis/lod_overlay.py --dry-run --threshold 50 ships/argon/argon_TL
  python3 tools/analysis/lod_overlay.py --out /tmp/x3m-lod ships/argon/argon_TL=50 stations/others/military_outpost_middleb=100
  python3 tools/analysis/lod_overlay.py --batch --dry-run --only verification/results/lod-overlay-batch/sectors.txt --out /tmp/x3m-batch
  python3 tools/analysis/lod_overlay.py --batch --sync --install
"""
import argparse
import gzip
import hashlib
import io
import json
import multiprocessing
import os
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bob1  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import Assets, unpack, write_catalogue  # noqa: E402

MARKER_SUFFIX = '.x3m-lod.json'
REPLACED_SUFFIX = '.x3m-replaced'
MAX_TRAILING = 8            # stray bytes after /BOB tolerated with a warning (86 of 94 failing mod bodies carry 1-2)
DISPLAY = (1920, 1080)
REFERENCE = (1280, 768)     # lod-selection.md reference frame of the threshold metric
LIVE_MARKERS = ('valid', 'legacy')
OURS_MARKERS = LIVE_MARKERS + ('retired', 'unreadable')   # catalogues never read as body sources


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


def original_archives(game, exclude_slot=None):
    cats = sorted(game.glob('[0-9][0-9].cat')) + sorted((game / 'addon').glob('[0-9][0-9].cat'))
    skip = None if exclude_slot is None else game / 'addon' / f'{exclude_slot:02d}.cat'
    return [p for cat in cats if cat != skip for p in (cat, cat.with_suffix('.dat'))]


def originals_digest(hashes):
    return hashlib.sha256(json.dumps(sorted(hashes.items())).encode()).hexdigest()


def installed_markers(game):
    """[dict(path, slot, manifest, status)] for every addon/NN.x3m-lod.json, status one of
    'valid' (the marker's overlay cat/dat sha256 match the files beside it), 'legacy' (a marker
    without overlay hashes, from before 2026-09-23, whose every recorded body member is present
    in the catalogue beside it with the recorded overlay_decoded_sha256: legacy_verified), 'retired'
    (the retired catalogue we wrote, hashes match), 'orphaned' (hashes differ, the files are gone,
    or a legacy marker carries no body proof or its members do not match: a mod overwrote the
    slot) or 'unreadable'."""
    out = []
    for path in sorted((Path(game) / 'addon').glob('*' + MARKER_SUFFIX)):
        m = dict(path=path, slot=None, manifest=None, status='unreadable')
        try:
            manifest = json.loads(path.read_text())
            slot = int(manifest['slot'])
            if path.name != f'{slot:02d}{MARKER_SUFFIX}':
                raise ValueError('marker name does not match its slot')
        except (ValueError, KeyError, TypeError, OSError):
            out.append(m)
            continue
        m.update(slot=slot, manifest=manifest)
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
    a different sha, an unreadable catalogue) is no proof: a mod may have overwritten the slot."""
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


def next_slot(game):
    nums = sorted(int(p.stem) for p in (game / 'addon').glob('[0-9][0-9].cat'))
    if nums != list(range(1, len(nums) + 1)):
        raise SystemExit(f'addon catalogues are not contiguous from 01: {nums}')
    if len(nums) >= 99:
        raise SystemExit('no free addon slot')
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


def alpha_materials(materials):
    """Group material indices (array positions, not the record's u16) whose effect
    parameters enable alpha testing or alpha blending."""
    return {i for i, m in enumerate(materials)
            if any(typ in (0, 1) and name.lower() in ALPHA_PARAMS and val and val[0]
                   for name, typ, val in m.get('params', ()))}


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


MIN_TEXELS = 0.5            # texel floor: below this many atlas texels per screen pixel a body is refused (texel_floor)
MAX_DEFAULT_JOBS = 6        # a worker on the biggest stations reaches ~7 GB RSS (2026-09-23 dry run)
WORKER_BYTES = 7 << 30      # RAM budget per baking worker (that peak); the default keeps one budget spare
ATLAS_DEFAULTS = dict(sizes=(1024, 2048), fmt='dxt', specular=False, bump=True, screen_width=effective_width(),
                      min_texels=MIN_TEXELS)


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
                              opts['fmt'], opts['specular'], synth, opts['bump'])
    except lod_atlas.AtlasError as exc:
        raise SystemExit(f'{name}: {exc}') from None
    low = res['layout']['min_ratio']
    if opts.get('min_texels') and low < opts['min_texels']:
        raise SystemExit(f'{name}: texel_floor: the atlas would give {low:.2f} atlas texels per screen pixel at the'
                         f' switch size, below --min-texels {opts["min_texels"]:g}; it would draw blurred')
    for slot, member in res['members'].items():
        stem = member.rsplit('.', 1)[0]
        taken = [e['source'] for ext in lod_atlas.DDS_LOOKUP[1] for e in assets.candidates(stem + ext)]
        if taken:
            raise SystemExit(f'{name}: atlas texture {stem} already exists in {taken}; it would be shadowed')
    extra = [(res['members'][s], lod_atlas.stored(e['dds'])) for s, e in res['encoded'].items()]
    res['summary'] = lod_atlas.summary(res)
    return res['record'], res['synth'], res, extra


def plan_body(assets, name, threshold, placement=None, force_threshold=False, collapse='glow', force_mat3=False,
              glow_luma=GLOW_LUMA, glow_share=GLOW_SHARE, area_percent=None, synth=True, atlas_opts=None,
              source_record=None):
    entry = bob1.resolve_body(assets, name)
    if 'loose' in entry:
        raise SystemExit(f'{name}: winning resource is loose file {entry["path"]}; a catalogue cannot override it')
    if entry['path'].lower().endswith(('.pbd', '.bod')):
        raise SystemExit(f'{name}: winning resource {entry["path"]} is a text body; only BOB1 bodies are overlaid')
    data = assets.read_entry(entry)
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
    bad = sorted({g['material'] for l in (source, before[-1]) for p in l['parts'] for g in p['groups']
                  if not 0 <= g['material'] < len(mats)})
    if bad and any(t in ('MAT5', 'MAT6') for t, _ in tree['sections']):
        raise SystemExit(f'{name}: group material index {bad} outside the material table (0..{len(mats) - 1});'
                         ' the ad signs carry one such negative-index group; refusing')
    alpha = alpha_materials(mats)
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
    stored = gzip.compress(out, mtime=0) if head == b'\x1f\x8b' or entry['path'].lower().endswith('.pbb') else out
    return dict(name=name, source=entry['source'], member=entry['path'], before=before, ladder=ladder,
                new=new, new_index=new_index, collapse=collapse, alpha=alpha_materials(mats), glow=glow,
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
    print(f'{plan["name"]}: {plan["source"]}:{plan["member"]}', file=out)
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
    b.add_argument('--sync', action='store_true',
                   help='batch: reuse the previous overlay\'s members for bodies whose inputs did not change')
    b.add_argument('--jobs', type=int, default=default_jobs(),
                   help='batch: worker processes for the census and the baking (default min(cpu count - 2, 6,'
                        ' RAM // 7 GiB - 1), at least 1: a worker baking one of the biggest stations reaches'
                        ' ~7 GB RSS, and every worker process is replaced after each body)')
    b.add_argument('--min-texels', type=float, default=MIN_TEXELS, metavar='F',
                   help=f'atlas / batch: refuse a body (reason texel_floor) whose atlas would give fewer than F'
                        f' atlas texels per screen pixel at the display reference (default {MIN_TEXELS}; 0 disables);'
                        ' the ratio is still reported for every body')
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
    sizes, n = [], a.atlas_size
    while n <= a.atlas_max_size:
        sizes.append(n)
        n *= 2
    width = effective_width(a.display, a.screen_width)
    if a.min_texels < 0:
        ap.error('--min-texels must be >= 0')
    a.atlas_opts = dict(sizes=tuple(sizes), fmt=a.atlas_format, specular=a.atlas_specular or a.batch,
                        bump=a.atlas_bump, screen_width=width, min_texels=a.min_texels)
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
    live = [m for m in markers if m['status'] in LIVE_MARKERS]
    replace_slot = None
    if live and not a.replace:
        raise SystemExit(f'an x3m-lod overlay is already installed ({live[0]["path"].name}); remove it first'
                         ' or pass --replace')
    if a.replace:
        if len(live) != 1:
            raise SystemExit(f'--replace needs exactly one installed x3m-lod overlay, found {len(live)}')
        manifest, replace_slot = live[0]['manifest'], live[0]['slot']
        try:
            expected = manifest['originals_sha256']
        except (KeyError, TypeError) as exc:
            raise SystemExit(f'--replace: unreadable marker {live[0]["path"].name} ({exc})') from None
        mode = manifest.get('originals_mode', 'sha256')
        if archive_digest(game, replace_slot, mode)[0] != expected:
            raise SystemExit(f'--replace: the installed archives other than addon/{replace_slot:02d} do not match'
                             f' the originals {mode} in {live[0]["path"].name}; refusing')
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

    before = None if a.dry_run else archive_digest(game, replace_slot, a.hash_mode)[1]
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
    if a.dry_run:
        print('dry run: nothing written')
        return 0
    members = [m for p in plans for m in p['members']]
    written, moved = commit_outputs(a, game, root, slot, replace_slot, members, [body_manifest(p) for p in plans], before)
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
        name=p['name'], source=p['source'], member=p['member'], placement=p['placement'], collapse=p['collapse'],
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


def commit_outputs(a, game, root, slot, replace_slot, members, bodies, before, manifest_extra=None, retire=None,
                   remove_markers=()):
    """Write addon/NN.cat/.dat and the marker under root (and, with retire=M, an empty catalogue with a
    retired marker at addon/MM) with the move-aside / restore protocol of the module notes; on
    --install the orphaned markers in remove_markers are deleted last. Returns (written, moved)."""
    cat = root / f'addon/{slot:02d}.cat'
    written = [cat, cat.with_suffix('.dat'), cat.with_name(cat.stem + MARKER_SUFFIX)]
    retired_files = []
    if retire is not None:
        rcat = root / f'addon/{retire:02d}.cat'
        retired_files = [rcat, rcat.with_suffix('.dat'), rcat.with_name(rcat.stem + MARKER_SUFFIX)]
    targets = written + retired_files
    replacing = a.install and (replace_slot is not None or retire is not None)
    asides = [(w, w.with_name(w.name + REPLACED_SUFFIX)) for w in targets]
    if replacing:
        for _, aside in asides:
            if aside.exists():
                raise SystemExit(f'{aside} exists (an interrupted --replace?); resolve it by hand')
    elif any(p.exists() for p in targets):
        raise SystemExit(f'refusing to overwrite existing addon/{slot:02d} outputs under {root}')

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
        write_overlay(a, game, members, bodies, slot, before, written, replace_slot if retire is None else retire,
                      manifest_extra, retired_files, retire)
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


def write_overlay(a, game, members, bodies, slot, before, written, exclude_slot, manifest_extra=None,
                  retired_files=(), retire=None):
    write_catalogue(written[0], members)
    overlay = hash_files(written[:2])
    manifest = dict(tool='tools/analysis/lod_overlay.py', slot=slot, collapse=a.collapse,
                    glow_luma=a.glow_luma, glow_share=a.glow_share, area_percent=a.area_percent,
                    synth_material=not a.no_synth_material, display=list(a.display),
                    screen_width=a.atlas_opts['screen_width'],
                    **({'atlas_options': dict(a.atlas_opts, sizes=list(a.atlas_opts['sizes']))}
                       if a.collapse == 'atlas' else {}),
                    bodies=bodies, originals=len(before), originals_sha256=originals_digest(before),
                    originals_mode=a.hash_mode,
                    overlay_sha256={'cat': overlay[str(written[0])], 'dat': overlay[str(written[1])]},
                    **(manifest_extra or {}))
    written[2].write_text(json.dumps(manifest, indent=1) + '\n')
    if retired_files:
        write_catalogue(retired_files[0], [retired_member(retire, slot)])
        rh = hash_files(retired_files[:2])
        retired_files[2].write_text(json.dumps(dict(
            tool='tools/analysis/lod_overlay.py', slot=retire, retired=True, retired_by=slot,
            overlay_sha256={'cat': rh[str(retired_files[0])], 'dat': rh[str(retired_files[1])]},
            note='retired catalogue with one inert text member (no body, texture or type resource): the x3m-lod'
                 ' overlay moved to a higher slot because a mod added addon numbers above this one; the file'
                 ' pair keeps the addon numbering contiguous. Engine acceptance of a retired slot is not yet'
                 ' verified in a launch'), indent=1) + '\n')
    after = archive_digest(game, exclude_slot, a.hash_mode)[1]
    ours = set(written) | set(retired_files)
    changed = sorted(k for k in before.keys() | after.keys() if before.get(k) != after.get(k)
                     and not any(Path(k) == w for w in ours))
    if changed:
        raise SystemExit(f'original archives changed during the run ({changed}); outputs removed')


# --- batch mode ----------------------------------------------------------------------------

TOOL_FILES = ('lod_atlas.py', 'lod_overlay.py', 'bob1.py')


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


BAKE_REASONS = (('texel_floor', 'texel_floor'), ('trailing bytes', 'trailing_bytes'), ('text body', 'text_body'),
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
    same batch settings, same inputs_sha256, every member present with its recorded sha256."""
    out = {}
    pm = prev['manifest']
    if (pm.get('batch') or {}).get('settings') != settings:
        notes.append(f'--sync: the previous overlay addon/{prev["slot"]:02d} was built with different settings'
                     ' (rule, width or atlas options); every body is rebuilt')
        return out
    old_cat = prev['path'].with_name(f'{prev["slot"]:02d}.cat')
    try:
        entries = {e['path']: e for e in read_catalogue(old_cat)}
    except (ValueError, OSError) as exc:
        notes.append(f'--sync: cannot read addon/{prev["slot"]:02d}.cat ({exc}); every body is rebuilt')
        return out
    old = {b['name'].lower(): b for b in pm.get('bodies', ())}
    with old_cat.with_suffix('.dat').open('rb') as f:
        for r in eligible:
            b = old.get(r['name'].lower())
            if not b or not b.get('inputs_sha256') or b['inputs_sha256'] != r.get('inputs_sha256'):
                continue
            got = []
            for mem in b.get('members', ()):
                e = entries.get(mem['path'])
                if e is None or e['size'] != mem['bytes']:
                    break
                f.seek(e['offset'])
                data = bytes(v ^ 0x33 for v in f.read(e['size']))
                if hashlib.sha256(data).hexdigest() != mem['sha256']:
                    break
                got.append((mem['path'], data))
            else:
                if got:
                    out[r['name']] = dict(name=r['name'], member=b['member'], source=b['source'], members=got,
                                          manifest=dict(b, reused_from=prev['slot']), draws=b.get('draws'),
                                          atlas=b.get('atlas'), reused=True, trailing=b.get('trailing_bytes', 0),
                                          guard_waived=b.get('guard_waived', False), seconds=0.0,
                                          text=f'{r["name"]}: reused from addon/{prev["slot"]:02d} ({len(got)} members,'
                                               f' inputs {r["inputs_sha256"][:16]})\n')
    return out


def batch(a, game, root, markers):
    import lod_atlas
    import lod_batch_census as census
    t_start = time.time()
    notes = []
    orphaned = [m for m in markers if m['status'] == 'orphaned']
    live = [m for m in markers if m['status'] in LIVE_MARKERS]
    if len(live) > 1:
        raise SystemExit(f'--batch: more than one live x3m-lod overlay ({[m["path"].name for m in live]}); resolve by hand')
    prev = live[0] if live else None
    nxt = next_slot(game)
    if prev is not None and prev['slot'] == nxt - 1:
        slot, retire = prev['slot'], None
    elif prev is not None:
        slot, retire = nxt, prev['slot']
        notes.append(f'previous overlay addon/{prev["slot"]:02d} is no longer the highest addon slot (mod catalogues'
                     f' up to addon/{nxt - 1:02d}); the new overlay takes addon/{slot:02d} and addon/{retire:02d}'
                     ' becomes a valid empty catalogue (marker: retired)')
    else:
        slot, retire = nxt, None
    if a.slot is not None and a.slot != slot and not a.force_slot:
        raise SystemExit(f'--slot {a.slot}: the batch slot is {slot} (pass --force-slot to override)')
    if a.slot is not None and a.force_slot:
        slot = a.slot
    for m in orphaned:
        notes.append(f'orphaned marker {m["path"].name}: addon/{m["slot"]:02d} was overwritten by a mod; read as a'
                     ' source' + ('; the marker is removed on --install' if a.install else ''))
    mods = sorted((game / 'addon' / 'mods').glob('*.cat'))
    only = parse_only(a.only) if a.only else None
    width = a.atlas_opts['screen_width']
    opts = dict(sizes=a.atlas_opts['sizes'], include_other=a.include_other, widths=(width,), rule=dict(census.RULE))
    settings = dict(rule=opts['rule'], screen_width=width, display=list(a.display), collapse='atlas',
                    source_record=0, placement='compact', include_other=a.include_other,
                    atlas=dict(a.atlas_opts, sizes=list(a.atlas_opts['sizes'])), tool_sha256=tool_sha256())
    t0 = time.time()
    rows, skipped = census.run(game, opts, a.jobs, only=only, include_text=True)
    census_s = time.time() - t0
    rows.sort(key=lambda r: r['name'].lower())
    if skipped:
        notes.append(f'source bodies read without the overlay catalogue(s) {skipped}')
    floor = []                             # texel floor: refused before baking, ratio kept in the record
    for r in rows:
        ratio = (r.get('atlas') or {}).get(width, {}).get('ratio')
        if r['eligible'] and a.min_texels and ratio is not None and ratio < a.min_texels:
            r['refuse'].append('texel_floor')
            r['eligible'] = False
            r['ratio'] = ratio
            floor.append((r['name'], ratio))
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
    for p in plans:
        row = by_name[p['name']]
        atlas_bytes = sum(len(d) for _, d in p['members'][1:])
        row['baked'] = dict(reused=p['reused'], draws=p['draws'], atlas_size=(p['atlas'] or {}).get('size'),
                            ratio=(p['atlas'] or {}).get('min_texels_per_px'), atlas_bytes=atlas_bytes,
                            member_bytes=len(p['members'][0][1]), seconds=round(p['seconds'], 2),
                            guard_waived=p['guard_waived'],
                            atlas_materials=len((p['atlas'] or {}).get('materials', [0])))
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
    candidates = sum(1 for r in rows if 'text_body' not in r['refuse'] and 'category_other' not in r['filter'])
    full_est = per_body * len(rows) if only is not None else bake_s
    trailing = sum(1 for p in plans if p['trailing'])
    waived = sum(1 for p in plans if p['guard_waived'])
    summary = [
        f'batch: game {game}; display {a.display[0]}x{a.display[1]} -> reference width {width}; rule ships'
        f' T_pad = min({opts["rule"]["t_cap"]:g}, max({opts["rule"]["ship_min"]:g}, {opts["rule"]["ship_factor"]:g} x T_1)),'
        f' stations/others {opts["rule"]["station_t"]:g}; source record 0, compact (T_1 guard waived: {waived} bodies);'
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
        + f', below 2.0 {below2}, of {len(ratios)} built; refused texel_floor (< {a.min_texels:g}) {len(floor)}'
        + (': ' + ', '.join(f'{n} {r:.2f}' for n, r in sorted(floor, key=lambda x: x[1])[:12])
           + (', ...' if len(floor) > 12 else '') if floor else ''),
        f'draws below T_pad per instance, summed over the overlay bodies: {draws_before} -> {draws_after}'
        f' (drawn groups of record 0 -> of C, hidden parts excluded, alpha groups included); bodies with one atlas'
        f' material per effect ({len(multi)}):'
        f' {", ".join(multi[:12])}{", ..." if len(multi) > 12 else ""}; second UV set passed through ({len(uv2)}):'
        f' {", ".join(uv2[:12])}{", ..." if len(uv2) > 12 else ""}; stray trailing bytes tolerated: {trailing}',
        f'timing: census {census_s:.1f} s, baking {bake_s:.1f} s for {len(built)} bodies with {a.jobs} jobs'
        f' ({per_body:.2f} s per body wall); extrapolated full set: {full_est:.0f} s'
        + (f' (this run x {len(rows)} enumerated / {len(built)} built; upper bound, every candidate baked)'
           if only is not None else ' (this run is the full set)') + f'; total {time.time() - t_start:.1f} s',
        f'target addon/{slot:02d}.cat + .dat'
        + (f' (replaces the previous overlay in that slot)' if prev is not None and retire is None else '')
        + (f'; addon/{retire:02d} retired to an empty catalogue' if retire is not None else '')
        + (f'; orphaned markers: {[m["path"].name for m in orphaned]}' if orphaned else '')]
    summary += notes + mod_notes + sector_lines + budget
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
        slot=slot, retired_slot=retire, previous_slot=prev['slot'] if prev else None, sync=bool(a.sync),
        settings=settings, only=str(a.only) if a.only else None, jobs=a.jobs,
        counts=dict(enumerated=len(rows), by_category=cats, eligible=len(eligible), built=len(built),
                    reused=len(reused), overlay_bodies=len(plans), candidates=candidates),
        refused=reasons, filtered=filters, atlas_sizes=sizes,
        bytes=dict(atlas=atlas_total, members=member_total, dat=atlas_total + member_total),
        ratio=dict(below_1=below1, below_2=below2, measured=len(ratios), min_texels=a.min_texels,
                   texel_floor={n: round(r, 4) for n, r in floor}),
        draws=dict(record0=draws_before, overlay=draws_after), mixed_effect_bodies=multi, uv2_bodies=uv2,
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
                     estimate=(r.get('atlas') or {}).get(width), **r.get('baked', {}),
                     **({'error': r['atlas_error']} if r.get('atlas_error') else {}),
                     **({'bake_error': r['bake_error']} if r.get('bake_error') else {}))
                for r in rows],
        summary=summary)
    record_path.parent.mkdir(parents=True, exist_ok=True)
    record_path.write_text(json.dumps(record, indent=1, default=str) + '\n')
    stem = record_path.with_suffix('')
    Path(str(stem) + '-summary.txt').write_text('\n'.join(summary) + '\n')
    Path(str(stem) + '-bodies.txt').write_text(''.join(p['text'] + '\n' for p in plans))
    print(f'record {record_path} (+ -summary.txt, -bodies.txt)')
    if a.dry_run:
        print('dry run: nothing written')
        return 0
    if not plans:
        raise SystemExit('--batch: no eligible body; nothing to write')
    before = archive_digest(game, prev['slot'] if prev else None, a.hash_mode)[1]
    members = [m for p in plans for m in p['members']]
    extra = dict(batch=dict(settings=settings, counts=record['counts'], timing=record['timing'],
                            retired_slot=retire, record=str(record_path)))
    written, moved = commit_outputs(a, game, root, slot, prev['slot'] if prev and retire is None else None, members,
                                    [p['manifest'] for p in plans], before, extra, retire,
                                    [m['path'] for m in orphaned])
    print(f'wrote {", ".join(str(w) for w in written)}; {len(before)} original archive files unchanged'
          + (f'; replaced the installed addon/{slot:02d} overlay' if moved and retire is None else '')
          + (f'; retired addon/{retire:02d}' if retire is not None else ''))
    return 0


def cli():
    try:
        return main()
    except (FileNotFoundError, bob1.FormatError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(cli())
