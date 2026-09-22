"""Run251 captures: horizontal luminance profile across the pale column above the ship
(rows 330-460, median over rows to reject stars), 10-90% rise width on each flank.
No pass-off capture exists in this session; widths are absolute, not an A/B."""
import numpy as np
D = "/tmp/x3-bottleX3-run251"
for f in (9772, 11400):
    h = np.fromfile(f"{D}/hdr_1_{f}.rgba16f", np.float16).astype(np.float32).reshape(768, 1280, 4)
    lum = 0.2126*h[..., 0] + 0.7152*h[..., 1] + 0.0722*h[..., 2]
    prof = np.median(lum[330:460, 400:960], axis=0)
    prof = np.convolve(prof, np.ones(5)/5, mode="same")
    pk = int(np.argmax(prof[40:-40])) + 40
    out = [f"frame {f} peak x={pk+400} lum={prof[pk]:.4f}"]
    for side, seg in (("left", prof[:pk+1]), ("right", prof[pk:][::-1])):
        lo = float(seg[:60].min()); hi = float(seg[-1]); amp = hi - lo
        i10 = int(np.argmax(seg >= lo + .1*amp)); i90 = int(np.argmax(seg >= lo + .9*amp))
        out.append(f"{side}: base={lo:.4f} contrast={amp/lo*100:.1f}% width10-90={i90-i10}px")
    print(" | ".join(out))
