# scene luma (Rec.709, pre-resolve hdr_1) on the plant crop: share of routed pixels with Y >= 1 (emissive vote threshold E = 1, thin_emissive=1.000)
import numpy as np
L=np.array([0.2126,0.7152,0.0722],np.float32)
for f in (4443,4447):
    h=np.fromfile(f"/tmp/x3-bottleX3-run315/hdr_1_{f}.rgba16f",dtype=np.float16).reshape(1440,5120,4)[285:925,2300:2880,:3].astype(np.float32)
    a=np.fromfile(f"/tmp/x3-bottleX3-run315/depth_1_{f}.rgba32f",dtype=np.float32).reshape(1440,5120,4)[285:925,2300:2880]
    Y=h@L; routed=a[...,0]!=-1
    print(f"frame {f} routed plant-crop pixels {routed.sum()}: Y p50 {np.median(Y[routed]):.3f} p99 {np.percentile(Y[routed],99):.3f} max {Y[routed].max():.3f} share Y>=1 {np.mean(Y[routed]>=1):.4f}")
