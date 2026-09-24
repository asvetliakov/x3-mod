"""Q4: per burst frame of run315: camera rotation/cut_median_px from motion_output_frame; hdr_1 luma thin-line detail
(fraction of pixels whose |L - 3x3 mean| > 0.25*mean and L>0.02, i.e. 1-2 px lines), and mean |dL|/L to the previous burst frame
over those detail pixels (frame-to-frame instability). usage: burst_luma_detail.py (reads /tmp/x3-bottleX3-run315)"""
import glob, re, numpy as np
D = '/tmp/x3-bottleX3-run315'; W, H = 5120, 1440
log = glob.glob(f'{D}/session-*.log')[0]; mo = {}
fr = [*range(4443, 4451), *range(4984, 4992)]
for l in open(log, errors='replace'):
    if l.startswith('motion_output_frame '):
        f = int(re.search(r' frame=(\d+)', l).group(1))
        if f in fr: mo[f] = (re.search(r'camera_rotation_deg=(\S+)', l).group(1), re.search(r'cut_median_px=(\S+)', l).group(1))
prev = None
for f in fr:
    a = np.fromfile(f'{D}/hdr_1_{f}.rgba16f', np.float16).reshape(H, W, 4)[..., :3].astype(np.float32)
    L = np.nan_to_num(0.2126*a[..., 0] + 0.7152*a[..., 1] + 0.0722*a[..., 2]).clip(0, 64)
    p = np.pad(L, 1, mode='edge'); m = sum(p[1+dy:1+dy+H, 1+dx:1+dx+W] for dy in (-1, 0, 1) for dx in (-1, 0, 1)) / 9
    det = (np.abs(L - m) > 0.25*m) & (L > 0.02)
    s = f'frame={f} rot_deg={mo.get(f,("-","-"))[0]} cut_median_px={mo.get(f,("-","-"))[1]} meanL={L.mean():.4f} detail_frac={det.mean():.4f}'
    if prev is not None and f - 1 in (prev[0],):
        dm = det | prev[2]; s += f' dL/L_on_detail={(np.abs(L-prev[1])[dm]/(np.maximum(L,prev[1])[dm]+1e-4)).mean():.4f} dL/L_all={(np.abs(L-prev[1])/(np.maximum(L,prev[1])+1e-4)).mean():.4f}'
    print(s); prev = (f, L, det)
