"""Cross-weapon grids: one cell per weapon (a 3x centre crop of the side-on frame, or the chase muzzle), 4 columns."""
import json, sys, os
from PIL import Image, ImageDraw, ImageFont
D = os.path.dirname(os.path.abspath(__file__))
meta = json.load(open(f'{D}/meta.json')); font = ImageFont.load_default(size=13)
info = {json.loads(l)['name']: json.loads(l) for l in open(f'{D}/shots.jsonl')}
def grid(tags, shot, crop, out, cw=480, ch=270):
    cols = 3; rows = (len(tags) + cols - 1) // cols; LB = 18
    S = Image.new('RGB', (cw * cols, (ch + LB) * rows), (20, 20, 20)); dr = ImageDraw.Draw(S)
    for i, tag in enumerate(tags):
        name = f'{tag}__{shot}'; im = Image.open(f'{D}/{name}.png').convert('RGB')
        if crop:
            w, h = im.size; x0, y0 = (w - w // crop) // 2, (h - h // crop) // 2
            im = im.crop((x0, y0, x0 + w // crop, y0 + h // crop))
        im = im.resize((cw, ch), Image.LANCZOS)
        x, y = (i % cols) * cw, (i // cols) * (ch + LB)
        S.paste(im, (x, y + LB)); m = meta[tag]
        dr.text((x + 3, y + 2), f"{tag} {m['style']} {m['size']} {m['hz']}/s | {info[name]['detail'][:70]}", fill=(230, 230, 230), font=font)
    S.save(out)
view = sys.argv[1]; tags = [t for t in meta if t.startswith(view[0] + '_')]
grid(tags, 'b_side_a', 3, f'{D}/grid_{view}_bolt_side.png')
grid(tags, 'm_chase_a', 0, f'{D}/grid_{view}_muzzle_chase.png')
grid(tags, 'b_own_a', 2, f'{D}/grid_{view}_bolt_own.png')
print(len(tags))
for shot, crop in (('m_range', 0), ('m_side', 0), ('b_in60', 0), ('b_seta6', 3), ('b_stream', 3), ('b_hull', 3), ('m_cockpit', 0)):
    grid(tags, shot, crop, f'{D}/grid_{view}_{shot}.png')
