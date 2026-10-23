"""Reader for src/proxy/story_builds.h (the per-story-build table, docs/architecture/story-builds.md) and for the story
objects on disk. Read-only; no copyrighted bytes are written anywhere.

    from story_builds_table import entries, other_objects, row_errors, story_objects, loaded_story
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/proxy/story_builds.h'
# The Build fields in declaration order, as each builds[] row spells them from its prefix.
FIELDS = ('build', 'seta_stop_ip', 'seta_gate_return', 'warp_method_entry', 'warp_fork_offset', 'warp_continue_offset',
          'warp_window_memory', 'warp_patch_memory', 'kc_play_stop_ip', 'kc_fade_stop_ip', 'kc_play_ip', 'kc_loop_music_return',
          'kc_change_track_return')
_ROW = re.compile(r'\{"(\w+)", (\w+)_build, ([^}]*)\},')


def _text(header=HEADER):
    return Path(header).read_text()


def _scalars(text):
    out = {}
    for name, value in re.findall(r'\b(\w+) = (0x[0-9a-fA-F]+|\d+)u?\b', text):
        out.setdefault(name, int(value, 0))
    return out


def _array(text, name):
    m = re.search(r'\b%s\[[\w ]*\] = \{(.*?)\};' % re.escape(name), text, re.S)
    if not m:
        return None
    return m.group(1)


def entries(header=HEADER):
    """One dict per builds[] row: name, prefix and every field (the two windows as bytes)."""
    text = _text(header)
    table = text[text.index('inline constexpr Build builds[] = {'):]
    table = table[:table.index('};') + 2]
    scal = _scalars(text)
    out = []
    for name, prefix, _rest in _ROW.findall(table):
        e = {'name': name, 'prefix': prefix}
        for f in FIELDS:
            if f.startswith('warp_') and f.endswith('_memory'):
                body = _array(text, '%s_%s' % (prefix, f))
                e[f] = bytes(int(v, 0) for v in re.findall(r'0x[0-9a-fA-F]+', body)) if body is not None else None
            else:
                e[f] = scal.get('%s_%s' % (prefix, f))
        out.append(e)
    return out


def row_errors(header=HEADER):
    """Rows of builds[] that do not spell every field from their own prefix in declaration order (table completeness)."""
    text = _text(header)
    table = text[text.index('inline constexpr Build builds[] = {'):]
    table = table[:table.index('};') + 2]
    bad = []
    for name, prefix, rest in _ROW.findall(table):
        want = ', '.join('%s_%s' % (prefix, f) for f in FIELDS[1:])
        if re.sub(r'\s+', ' ', rest.strip()) != want:
            bad.append(name)
    return bad


def other_objects(header=HEADER):
    text = _text(header)
    body = text[text.index('inline constexpr Object other_objects[] = {'):]
    body = body[:body.index('};')]
    return [(int(b), n) for b, n in re.findall(r'\{(\d+)u, "(\w+)"\}', body)]


def story_objects(x3):
    """Every L/x3story*.obj on disk: [(source, build, raw)] for the catalogues (base, then addon) and loose files."""
    sys.path.insert(0, str(ROOT / 'tools/analysis'))
    from inspect_x3 import read_catalogue
    x3 = Path(x3)
    out = []
    for d in (x3, x3 / 'addon'):
        for cat in sorted(d.glob('*.cat')):
            for e in read_catalogue(cat):
                p = e['path'].lower()
                if p.startswith('l/x3story') and p.endswith('.obj'):
                    with open(cat.with_suffix('.dat'), 'rb') as f:
                        f.seek(e['offset'])
                        raw = bytes(b ^ 0x33 for b in f.read(e['size']))
                    out.append(('%s:%s' % (cat.relative_to(x3), e['path']), struct.unpack_from('>I', raw, 8)[0], raw))
    for p in sorted(list(x3.glob('L/x3story*.obj')) + list(x3.glob('l/x3story*.obj'))):
        raw = p.read_bytes()
        out.append((str(p.relative_to(x3)), struct.unpack_from('>I', raw, 8)[0], raw))
    return out


def kc_objects(x3, catalogues=('addon/04.cat', 'addon/07.cat')):
    """Build ids of every L/*.obj KC object in the named catalogues: [(catalogue, member path, build)]."""
    sys.path.insert(0, str(ROOT / 'tools/analysis'))
    from inspect_x3 import read_catalogue
    x3 = Path(x3)
    out = []
    for name in catalogues:
        cat = x3 / name
        if not cat.is_file():
            continue
        for e in read_catalogue(cat):
            if e['path'].lower().startswith('l/') and e['path'].lower().endswith('.obj'):
                with open(cat.with_suffix('.dat'), 'rb') as f:
                    f.seek(e['offset'])
                    head = bytes(b ^ 0x33 for b in f.read(12))
                if head[:4] == b'STOR':
                    out.append((name, e['path'], struct.unpack_from('>I', head, 8)[0]))
    return out


def loaded_story(x3, objects=None):
    """The object the game opens (gate-queue-script.md section 8): a loose L\\x3story*.obj, else the newest addon catalogue."""
    objects = story_objects(x3) if objects is None else objects
    loose = [o for o in objects if ':' not in o[0]]
    if loose:
        return loose[0]
    addon = [o for o in objects if o[0].startswith('addon/')]
    return addon[-1] if addon else None
