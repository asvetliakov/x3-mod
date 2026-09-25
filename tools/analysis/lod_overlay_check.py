#!/usr/bin/env python3
"""Launch-time state of the merged-LOD overlay: one `lod overlay:` report line (standard library only).

docs/architecture/lod-overlay-mods.md section 2. Shared by `tools/manage.py launch` (the line beside
`fog families:`), `tools/analysis/lod_overlay.py --check` and this module's own CLI; it also owns the
archive helpers the baker uses for its originals check (original_archives, fingerprint_files,
originals_digest), so both sides compute them one way.

What it reads: the x3m-lod markers (addon/NN.x3m-lod.json, addon/mods/*.x3m-lod.json), stat of every
NN.cat/.dat and addon/NN.cat/.dat, sha256 of the small .cat files, the catalogue index (.cat) of a
catalogue above the overlay and of a selected package, and the one registry value
HKCU\\Software\\EGOSOFT\\X3AP\\ModName (CrossOver: a plain text scan of the bottle's user.reg; native
Windows: winreg). It never opens a .dat, never reads a body, never writes, never blocks.

States (the first word after `lod overlay:`), worst wins: orphaned (an overlay slot or a derived package
copy no longer matches its marker: a mod overwrote it), source_missing (a derived package's source
addon/mods/<name>.cat is gone), stale (a catalogue was added above the overlay, an original catalogue
changed, the selected package has no derived copy although it shadows overlay bodies, or the derived copy's
source package changed), ok, none (no marker at all). "Intact" for our own files = the marker's cat
sha256 and dat size match (the dat is not hashed at launch; --hash-archives does). A marker with
originals_fingerprints (written by the baker since 2026-09-25) names the changed original; an older one is
compared by its originals_sha256 digest and says only "sources changed since the bake".

  python3 tools/analysis/lod_overlay_check.py [--game DIR] [--registry user.reg] [--hash-archives]
"""
import argparse
import hashlib
import json
import os
import shlex
import sys
from pathlib import Path


MARKER_SUFFIX = '.x3m-lod.json'
PACKAGE_SUFFIX = '-x3m-lod'            # derived package copy: addon/mods/<name>-x3m-lod.cat/.dat
BODY_EXTENSIONS = ('.pbb', '.bob', '.pbd', '.bod')
LIVE_MARKERS = ('valid', 'legacy')
OUR_SLOTS = LIVE_MARKERS + ('retired',)
STATE_ORDER = ('none', 'ok', 'stale', 'source_missing', 'orphaned')
DEFAULT_GAME = Path(os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'))
REGISTRY_KEY = 'Software\\EGOSOFT\\X3AP'
REBAKE = 'python3 tools/analysis/lod_overlay.py --batch --sync --install'


# --- archive helpers (also used by lod_overlay.py) -------------------------------------------

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


def hash_files(paths):
    out = {}
    for p in paths:
        h = hashlib.sha256()
        with p.open('rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                h.update(block)
        out[str(p)] = h.hexdigest()
    return out


def stat_fingerprint(path):
    st = path.stat()
    return f'size:{st.st_size}:mtime_ns:{st.st_mtime_ns}'


def fingerprint_files(paths, full=False):
    """{path: fingerprint}: sha256 of every .cat (small) and, with full, of every .dat; otherwise
    a .dat is 'size:<bytes>:mtime_ns:<ns>' (mod trees are gigabytes; --hash-archives hashes them)."""
    out = {}
    for p in paths:
        if full or p.suffix.lower() == '.cat':
            out.update(hash_files([p]))
        else:
            out[str(p)] = stat_fingerprint(p)
    return out


def originals_digest(hashes):
    return hashlib.sha256(json.dumps(sorted(hashes.items())).encode()).hexdigest()


def originals_fingerprints(game, paths):
    """{game-relative posix path: 'size:N:mtime_ns:M'} of the original archives (the marker field
    originals_fingerprints): lets the launch line name the changed file without hashing anything."""
    return {p.relative_to(game).as_posix(): stat_fingerprint(p) for p in paths}


def body_key(path):
    """Canonical body stem of a member path or body name: objects/<...> lower case, no extension, no addon/
    (the light twin of lod_batch_census.body_key / bob1.body_stem)."""
    stem = str(path).replace('\\', '/').strip('/').lower()
    for ext in BODY_EXTENSIONS:
        if stem.endswith(ext):
            stem = stem[:-len(ext)]
            break
    stem = stem.removeprefix('addon/')
    return stem if stem.startswith('objects/') else 'objects/' + stem


def catalogue_names(cat):
    """Member paths of a CAT index (the rolling-XOR decode of inspect_x3.read_catalogue, without its dat size
    check: the launch line stats nothing but the listed files)."""
    raw = Path(cat).read_bytes()
    lines = bytes(v ^ ((0xdb + i) & 255) for i, v in enumerate(raw)).decode('utf-8').splitlines()
    if not lines or not lines[0].endswith('.dat'):
        raise ValueError(f'unexpected catalogue header in {cat}')
    return [line.rsplit(' ', 1)[0] for line in lines[1:] if line]


def catalogue_body_keys(cat):
    return {body_key(p) for p in catalogue_names(cat) if p.lower().endswith(BODY_EXTENSIONS)}


# --- selected package (registry) --------------------------------------------------------------

def bottle_registry(game):
    """The CrossOver bottle's user.reg for a game directory under <bottle>/drive_c/..., else None."""
    game = Path(game)
    for parent in [game] + list(game.parents):
        if parent.name == 'drive_c':
            reg = parent.parent / 'user.reg'
            return reg if reg.is_file() else None
    return None


def _reg_string(text):
    """Value of a user.reg string entry ('"..."' or 'str(2):"..."'); None for any other type."""
    if text.startswith('str(2):'):
        text = text[len('str(2):'):]
    if len(text) < 2 or text[0] != '"' or text[-1] != '"':
        return None
    out, i, body = [], 0, text[1:-1]
    while i < len(body):
        if body[i] == '\\' and i + 1 < len(body):
            out.append(body[i + 1])
            i += 2
        else:
            out.append(body[i])
            i += 1
    return ''.join(out)


def parse_mod_name(text):
    """ModName from the text of a Wine user.reg: the [Software\\\\EGOSOFT\\\\X3AP] block, the "ModName" line.
    '' when the key or value is absent (no package), None when the value is not a string."""
    want = REGISTRY_KEY.replace('\\', '\\\\').lower()
    inside = False
    for line in text.splitlines():
        if line.startswith('['):
            inside = line[1:line.find(']')].lower() == want if ']' in line else False
            continue
        if inside and line[:10].lower() == '"modname"=':
            return _reg_string(line[10:].strip())
    return ''


def read_mod_name(registry=None, game=None):
    """(ModName, where): registry is a user.reg path (CrossOver); without one the bottle's user.reg is found
    from game, and on native Windows winreg reads HKCU\\Software\\EGOSOFT\\X3AP. ModName is '' for no package
    and None when it cannot be read."""
    reg = Path(registry) if registry else (bottle_registry(game) if game else None)
    if reg is not None:
        try:
            with reg.open(encoding='utf-8', errors='replace') as f:
                return parse_mod_name(f.read()), str(reg)
        except OSError:
            return None, f'{reg} unreadable'
    if sys.platform == 'win32':
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, REGISTRY_KEY) as key:
                value, kind = winreg.QueryValueEx(key, 'ModName')
            return (value if kind in (winreg.REG_SZ, winreg.REG_EXPAND_SZ) else None), 'HKCU'
        except FileNotFoundError:
            return '', 'HKCU'
        except OSError:
            return None, 'HKCU unreadable'
    return None, 'no user.reg'


def valid_package_name(name):
    """The engine loads addon\\mods\\%s.cat: an ASCII stem without a dot or a path separator."""
    return bool(name) and name.isascii() and not any(c in name for c in './\\:') and name.strip() == name


def derived_name(package):
    return package + PACKAGE_SUFFIX


def package_files(game, derived):
    mods = Path(game) / 'addon' / 'mods'
    return mods / f'{derived}.cat', mods / f'{derived}.dat', mods / f'{derived}{MARKER_SUFFIX}'


def package_fingerprint(cat, full=False):
    """Source package fingerprint recorded in a derived package's marker: cat sha256, dat bytes and the dat's
    size:mtime (sha256 under full)."""
    dat = cat.with_suffix('.dat')
    return dict(cat=hash_files([cat])[str(cat)], dat=hash_files([dat])[str(dat)] if full else stat_fingerprint(dat),
                dat_bytes=dat.stat().st_size, mode='sha256' if full else 'fingerprint')


def package_status(game, marker_path, manifest, full=False):
    """(status, reason) of a derived package copy: orphaned (its own cat/dat are gone or do not match the marker:
    cat sha256 plus dat size, or the dat sha256 with full), source_missing (addon/mods/<package>.cat gone),
    stale (the source package's fingerprint differs: the mod was updated) or valid."""
    derived = marker_path.name[:-len(MARKER_SUFFIX)]
    cat, dat, _ = package_files(game, derived)
    own = manifest.get('overlay_sha256') if isinstance(manifest, dict) else None
    if not isinstance(own, dict) or not cat.is_file() or not dat.is_file():
        return 'orphaned', f'{derived}.cat/.dat missing or no hashes in the marker'
    if hash_files([cat])[str(cat)] != own.get('cat') or dat.stat().st_size != manifest.get('overlay_dat_bytes'):
        return 'orphaned', f'{derived}.cat/.dat do not match the marker'
    if full and hash_files([dat])[str(dat)] != own.get('dat'):
        return 'orphaned', f'{derived}.dat does not match the marker'
    package = manifest.get('package')
    src = Path(game) / 'addon' / 'mods' / f'{package}.cat'
    if not package or not src.is_file() or not src.with_suffix('.dat').is_file():
        return 'source_missing', f'addon/mods/{package}.cat missing'
    want = manifest.get('package_fingerprint') or {}
    if hash_files([src])[str(src)] != want.get('cat'):
        return 'stale', f'{package}.cat changed'
    sdat = src.with_suffix('.dat')
    if want.get('mode') == 'sha256' and not full:
        same = sdat.stat().st_size == want.get('dat_bytes')
    else:
        same = (hash_files([sdat])[str(sdat)] if want.get('mode') == 'sha256' else stat_fingerprint(sdat)) == want.get('dat')
    return ('valid', '') if same else ('stale', f'{package}.dat changed')


def package_markers(game, full=False):
    """[dict(path, derived, package, manifest, status, reason)] for every addon/mods/*.x3m-lod.json."""
    out = []
    for path in sorted((Path(game) / 'addon' / 'mods').glob('*' + MARKER_SUFFIX)):
        derived = path.name[:-len(MARKER_SUFFIX)]
        m = dict(path=path, derived=derived, package=None, manifest=None, status='unreadable', reason='unreadable')
        try:
            manifest = json.loads(path.read_text())
            if not isinstance(manifest, dict) or not manifest.get('package'):
                raise ValueError('not a package marker')
        except (ValueError, OSError):
            out.append(m)
            continue
        m.update(package=manifest['package'], manifest=manifest)
        m['status'], m['reason'] = package_status(game, path, manifest, full)
        out.append(m)
    return out


# --- numbered overlay ------------------------------------------------------------------------

def slot_dat_bytes(manifest, slot):
    """Recorded dat size of a slot: overlay_dat_bytes (since 2026-09-25), else the batch slot row."""
    if isinstance(manifest.get('overlay_dat_bytes'), int):
        return manifest['overlay_dat_bytes']
    for row in (manifest.get('batch') or {}).get('slots') or ():
        if isinstance(row, dict) and row.get('slot') == slot:
            return row.get('bytes')
    return None


def slot_markers(game, full=False):
    """[dict(path, slot, manifest, status, overlay_slots)] for addon/NN.x3m-lod.json, the launch-cost twin of
    lod_overlay.installed_markers: valid/retired when the cat sha256 and the dat size (the dat sha256 with full)
    match, legacy for a marker without overlay hashes (its member proof reads the dat: not checked here),
    orphaned or unreadable otherwise."""
    out = []
    for path in sorted((Path(game) / 'addon').glob('[0-9][0-9]' + MARKER_SUFFIX)):
        m = dict(path=path, slot=None, manifest=None, status='unreadable', overlay_slots=None)
        try:
            manifest = json.loads(path.read_text())
            slot = int(manifest['slot'])
            if path.name != f'{slot:02d}{MARKER_SUFFIX}':
                raise ValueError('marker name does not match its slot')
            group = manifest.get('overlay_slots', [slot])
            if not (isinstance(group, list) and group and all(type(s) is int for s in group) and slot in group
                    and group == list(range(group[0], group[0] + len(group)))):
                raise ValueError('bad overlay_slots')
        except (ValueError, KeyError, TypeError, OSError, AttributeError):
            out.append(m)
            continue
        m.update(slot=slot, manifest=manifest, overlay_slots=group)
        cat = path.with_name(f'{slot:02d}.cat')
        dat = cat.with_suffix('.dat')
        expected = manifest.get('overlay_sha256')
        if not cat.is_file() or not dat.is_file():
            m['status'] = 'orphaned'
        elif not isinstance(expected, dict):
            m['status'] = 'legacy'
        else:
            size = slot_dat_bytes(manifest, slot)
            same = (hash_files([cat])[str(cat)] == expected.get('cat')
                    and (size is None or dat.stat().st_size == size)
                    and (not full or hash_files([dat])[str(dat)] == expected.get('dat')))
            m['status'] = ('retired' if manifest.get('retired') else 'valid') if same else 'orphaned'
        out.append(m)
    return out


def slot_text(slots):
    return '/'.join(f'{s:02d}' for s in sorted(slots))


def gb(n):
    return f'{n / 1e9:.2f} GB' if n >= 1e9 else f'{n / 1e6:.1f} MB'


def rebake_command(game, package=None):
    cmd = REBAKE
    if Path(game).resolve() != DEFAULT_GAME.resolve():
        cmd += f' --game {shlex.quote(str(game))}'
    return cmd + (f' --mod {package}' if package else '')


def sources_clause(game, marker, exclude, above, full=False):
    """(changed: bool, text) of the originals check for one live overlay marker; the catalogues in above are
    reported by their own clause and left out here."""
    paths = [p for p in original_archives(game, exclude) if p.with_suffix('.cat') not in above]
    stored = marker.get('originals_fingerprints')
    if isinstance(stored, dict):
        current = originals_fingerprints(game, paths)
        above_rel = {p.relative_to(game).as_posix() for c in above for p in (c, c.with_suffix('.dat'))}
        changed = sorted(k for k in stored if k not in above_rel and current.get(k) != stored[k])
        added = sorted(k for k in current if k not in stored)
        names = [('-' if k not in current else '') + k for k in changed] + ['+' + k for k in added]
        if names:
            return True, f'sources changed since the bake ({", ".join(names[:6])}{", ..." if len(names) > 6 else ""})'
        return False, 'sources unchanged'
    mode = marker.get('originals_mode', 'sha256')
    if mode == 'sha256' and not full:
        return False, 'sources not checked at launch (marker hashed every dat; lod_overlay.py --check --hash-archives)'
    digest = originals_digest(fingerprint_files(paths, full=mode == 'sha256'))
    if digest == marker.get('originals_sha256'):
        return False, 'sources unchanged'
    return True, 'sources changed since the bake (the marker predates originals_fingerprints: no file named)'


def overlay_state(game, registry=None, full=False, mod_name=Ellipsis, mod_where='given'):
    """dict(state, clauses, rebake): the numbered overlay, catalogues above it, the originals and the selected
    package. mod_name overrides the registry read (Ellipsis = read it; None = unknown, reported with mod_where
    and treated as no package)."""
    game = Path(game).resolve()
    clauses, states, rebake_pkg = [], [], None
    markers = slot_markers(game, full)
    live = [m for m in markers if m['status'] in LIVE_MARKERS]
    exclude = {m['slot'] for m in markers if m['slot'] and m['status'] in OUR_SLOTS}
    overlay_keys = set()
    for m in markers:
        if m['status'] == 'orphaned':
            states.append('orphaned')
            clauses.append(f'slot {m["slot"]:02d} overwritten by a mod (marker orphaned; the catalogue is a mod source now)')
        elif m['status'] == 'unreadable':
            states.append('orphaned')
            clauses.append(f'marker {m["path"].name} unreadable')
    if live:
        slots = [m['slot'] for m in live]
        bodies = sum(len(m['manifest'].get('bodies') or ()) for m in live)
        size = sum((game / 'addon' / f'{s:02d}.dat').stat().st_size for s in slots)
        for m in live:
            for b in m['manifest'].get('bodies') or ():
                if isinstance(b, dict) and (b.get('member') or b.get('name')):
                    overlay_keys.add(body_key(b.get('member') or b.get('name')))
        legacy = [m['slot'] for m in live if m['status'] == 'legacy']
        clauses.insert(0, f'{bodies} bodies in slots {slot_text(slots)}, {gb(size)}'
                       + (f'; legacy marker(s) {slot_text(legacy)} not verified at launch' if legacy else ''))
        top = max(slots)
        above = [c for c in sorted((game / 'addon').glob('[0-9][0-9].cat')) if int(c.stem) > top
                 and int(c.stem) not in exclude]
        for c in above:
            try:
                hit = len(catalogue_body_keys(c) & overlay_keys)
                clauses.append(f'addon/{c.name} above the overlay holds {hit} overlay bodies')
            except (ValueError, OSError) as exc:
                clauses.append(f'addon/{c.name} above the overlay (unreadable: {exc})')
            states.append('stale')
        changed, text = sources_clause(game, live[0]['manifest'], exclude, set(above), full)
        clauses.append(text)
        if changed:
            states.append('stale')
        states.append('ok')
    # selected package
    if mod_name is Ellipsis:
        name, where = read_mod_name(registry, game)
    else:
        name, where = mod_name, mod_where
    packages = package_markers(game, full)
    by_derived = {p['derived']: p for p in packages}
    if name is None:
        clauses.append(f'ModName unknown ({where})')
    elif name == '':
        clauses.append('no package: ModName empty')
    elif name.endswith(PACKAGE_SUFFIX):
        p = by_derived.get(name)
        if p is None:
            states.append('orphaned')
            clauses.append(f'package {name} selected but it has no x3m-lod marker (not ours)')
        else:
            src, st = p['package'], p['status']
            rebake_pkg = src
            if st == 'valid':
                clauses.append(f'package {name} ({len(p["manifest"].get("bodies") or ())} bodies, from {src})')
                states.append('ok')
            elif st == 'stale':
                clauses.append(f'package {name} selected, stale ({p["reason"]})')
                states.append('stale')
            elif st == 'source_missing':
                clauses.append(f'package {name} selected, {p["reason"]} (remove the copy with lod_overlay.py'
                               f' --remove-package {src})')
                states.append('source_missing')
            else:
                clauses.append(f'package {name} selected, {p["reason"]}')
                states.append('orphaned')
    else:
        cat = game / 'addon' / 'mods' / f'{name}.cat'
        if not cat.is_file():
            clauses.append(f'package {name} selected but addon/mods/{name}.cat is missing')
        else:
            try:
                hit = len(catalogue_body_keys(cat) & overlay_keys)
            except (ValueError, OSError) as exc:
                hit = None
                clauses.append(f'package {name} selected (unreadable: {exc})')
            if hit is not None:
                d = by_derived.get(derived_name(name))
                if d is not None and d['status'] in ('valid', 'stale'):
                    clauses.append(f'package {name} selected instead of its derived {derived_name(name)}:'
                                   f' {hit} overlay bodies shadowed (select {derived_name(name)} in the start menu)')
                else:
                    clauses.append(f'package {name} selected (no derived package: {hit} overlay bodies shadowed)')
                    if hit:
                        states.append('stale')
                        rebake_pkg = name
    for p in packages:                     # derived copies that are not the selection
        if p['derived'] == name:
            continue
        if p['status'] in ('orphaned', 'unreadable'):
            states.append('orphaned')
            clauses.append(f'addon/mods/{p["derived"]}: {p["reason"]}')
        elif p['status'] == 'source_missing':
            states.append('source_missing')
            clauses.append(f'addon/mods/{p["derived"]}: {p["reason"]} (lod_overlay.py --remove-package {p["package"]})')
        elif p['status'] == 'stale':
            clauses.append(f'addon/mods/{p["derived"]} (not selected) stale: {p["reason"]}')
    state = max(states, key=STATE_ORDER.index) if states else 'none'
    if state == 'none':
        clauses.insert(0, 'no overlay installed')
    return dict(state=state, clauses=clauses, mod_name=name,
                rebake=rebake_command(game, rebake_pkg) if state in ('stale', 'orphaned') else None)


def overlay_line(game, registry=None, full=False, mod_name=Ellipsis, mod_where='given'):
    """The one `lod overlay:` report line (module notes). Never raises: a failure is reported in the line."""
    try:
        s = overlay_state(game, registry, full, mod_name, mod_where)
    except Exception as exc:                          # a report line must never block a launch
        return f'lod overlay: check failed ({type(exc).__name__}: {exc})'
    line = f'lod overlay: {s["state"]} ({"; ".join(s["clauses"])})'
    if s['rebake']:
        line += f'; rebake with `{s["rebake"]}`'
    return line


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', type=Path, default=DEFAULT_GAME)
    ap.add_argument('--registry', type=Path, help='user.reg to read ModName from (default: the bottle\'s, found'
                                                  ' from --game; native Windows: HKCU)')
    ap.add_argument('--hash-archives', action='store_true', help='also hash our overlay dats (slow: gigabytes)')
    a = ap.parse_args(argv)
    print(overlay_line(a.game, a.registry, a.hash_archives))
    return 0


if __name__ == '__main__':
    sys.exit(main())
