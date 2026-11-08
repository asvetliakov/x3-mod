"""Contact sheets per weapon: <tag>__bolt.png (2x5 cells) and <tag>__muzzle.png (2x3). Side-on cells are 3x centre crops."""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont
D = os.path.dirname(os.path.abspath(__file__))
meta = json.load(open(f'{D}/meta.json'))
info = {}
for line in open(f'{D}/shots.jsonl'):
    r = json.loads(line); info[r['name']] = r
font = ImageFont.load_default(size=14)
CW, CH, LB = 720, 405, 22
def cell(name, crop=0):
    im = Image.open(f'{D}/{name}.png').convert('RGB')
    if im.size != (CW, CH): im = im.resize((CW, CH))
    if crop:
        cw, ch = CW // crop, CH // crop
        x0, y0 = (CW - cw) // 2, (CH - ch) // 2
        im = im.crop((x0, y0, x0 + cw, y0 + ch)).resize((CW, CH), Image.LANCZOS)
    return im
def sheet(tag, rows, out):
    W = CW * 2; H = (CH + LB) * len(rows)
    S = Image.new('RGB', (W, H), (20, 20, 20)); dr = ImageDraw.Draw(S)
    for r, row in enumerate(rows):
        for c, (name, crop, label) in enumerate(row):
            if name not in info: continue
            x, y = c * CW, r * (CH + LB)
            S.paste(cell(name, crop), (x, y + LB))
            det = info[name]['detail'][:95]
            dr.text((x + 4, y + 3), f"{label}{' [3x centre crop]' if crop else ''} | t {info[name]['t']:.0f} ms | {det}", fill=(230, 230, 230), font=font)
    S.save(out)
tags = sys.argv[1:] or list(meta)
for tag in tags:
    m = meta[tag]; p = lambda s: f'{tag}__{s}'
    sheet(tag, [[(p('b_side_a'), 3, f'side-on {m["D"]} m'), (p('b_side_b'), 3, f'side-on {m["D"]} m, earlier')],
                [(p('b_hull'), 3, f'side-on over lit hull {m["D"]} m'), (p('b_seta6'), 3, 'side-on SETA 6')],
                [(p('b_own_a'), 0, 'own fire, chase'), (p('b_own_b'), 0, 'own fire, chase, later')],
                [(p('b_in300'), 0, 'incoming cockpit, 300 m'), (p('b_in60'), 0, 'incoming cockpit, 60 m')],
                [(p('b_stream'), 0, 'stream 10/s over lit hull 1 km'), (p('b_stream'), 3, 'stream, crop')]], f'{D}/{tag}__bolt.png')
    sheet(tag, [[(p('m_chase_a'), 0, f'own chase, {m["hz"]} shots/s, 20 ms after shot'), (p('m_chase_b'), 0, 'own chase, half a period later')],
                [(p('m_cockpit'), 0, 'own cockpit'), (p('m_side'), 0, 'own guns across the view (cone)')],
                [(p('m_range'), 0, 'capital at 600 m'), (p('m_seta6'), 0, 'own chase SETA 6')]], f'{D}/{tag}__muzzle.png')
print('sheets for', len(tags))
