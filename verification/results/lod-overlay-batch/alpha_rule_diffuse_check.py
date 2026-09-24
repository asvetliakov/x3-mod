"""Which diffuse each record-0 face group of C samples, under the old alpha rule (flags only) and the new one
(lod_overlay.alpha_materials with assets, 2026-09-24), for every body whose alpha set shrinks and that is eligible
after (census.txt of two lod_batch_census.py --out directories). Atlas groups sample their own diffuse (a tile of
it); a group in the alpha class samples the part's dominant alpha material's diffuse (lod_atlas.rewrite_record; the
synthesized copy keeps the dominant's textures). "own" compares the resolved texture members (lod_atlas.Textures).
Counts faces and groups per class (eligible before and after; newly eligible, whose "before" is the vanilla LOD with every group on its own diffuse): own before and not after (new borrowing) must be 0. Bottle X3 read-only.

    python3 verification/results/lod-overlay-batch/alpha_rule_diffuse_check.py <census before> <census after> \
        > verification/results/lod-overlay-batch/alpha_rule_diffuse_check_out.txt
"""
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[2] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import body_materials  # noqa: E402
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402
from alpha_rule_census_compare import rows  # noqa: E402

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'


def sampled(mats, r0, alpha, textures):
    """[(faces, own diffuse member, sampled diffuse member)] per visible record-0 group."""
    def member(mi):
        name = body_materials.slots(mats[mi]).get('diffuse') if 0 <= mi < len(mats) else None
        src = textures.source(name) if name is not None else None
        return src['member'] if src else repr(name)
    out = []
    for p in r0['parts']:
        if p['flags'] & lod_atlas.HIDDEN_PART:
            continue
        dom = lod_atlas.dominant([g for g in p['groups'] if g['material'] in alpha])
        for g in p['groups']:
            m = g['material']
            out.append((len(g['faces']), member(m), member(dom if m in alpha else m)))
    return out


def main(before, after):
    b, a = rows(before), rows(after)
    names = [n for n in sorted(a) if a[n]['alpha'] < b[n]['alpha'] and a[n]['eligible']]
    assets, _ = lod_overlay.original_assets(GAME)
    textures = lod_atlas.Textures(assets)
    tot = {'eligible before and after': Counter(), 'newly eligible': Counter()}
    bodies = {k: Counter() for k in tot}
    for n in names:
        cls = 'eligible before and after' if b[n]['eligible'] else 'newly eligible'
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, n)), lod_overlay.MAX_TRAILING)
        mats = bob1.materials(tree)
        r0 = bob1.lods(tree)[0]
        r0 = lod_atlas.animated_record(mats, r0, assets)[0]
        old = sampled(mats, r0, lod_overlay.alpha_materials(mats), textures)
        new = sampled(mats, r0, lod_overlay.alpha_materials(mats, assets, record=r0), textures)
        c = Counter()
        for (f, own, s0), (_, _, s1) in zip(old, new):
            if cls == 'newly eligible':     # before: no overlay, the vanilla LOD draws every group with its own diffuse
                key = 'own -> ' + ('own' if s1 == own else 'other')
            else:
                key = ('own' if s0 == own else 'other') + ' -> ' + ('own' if s1 == own else 'other')
            c[key + ' faces'] += f
            c[key + ' groups'] += 1
        tot[cls].update(c)
        bodies[cls]['new borrowing' if c['own -> other groups'] else 'regains own' if c['other -> own groups'] else
                    'unchanged'] += 1
        if c['own -> other groups']:
            print(f'  NEW BORROWING {n}: {dict(c)}')
    for cls in tot:
        print(f'{cls}: bodies {sum(bodies[cls].values())} (alpha set shrinks, eligible after): {dict(bodies[cls])}')
        print('  record-0 groups and faces by sampled diffuse, before -> after:', dict(sorted(tot[cls].items())))


if __name__ == '__main__':
    main(*sys.argv[1:3])
