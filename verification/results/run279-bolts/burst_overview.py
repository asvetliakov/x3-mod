# Run 75 B (run279) burst: per-frame luminance stats of the pre-resolve FP16
# scene (hdr_1_<f>.rgba16f), depth lane (depth_1_<f>.rgba32f: r=z/w, g=share,
# b=a=clip w, -1 uncovered) and PNG previews in the scratch dir given as argv[1].
import numpy as np, sys
from PIL import Image
D='/tmp/x3-bottleX3-run279'; W,H=1920,1080
out=sys.argv[1] if len(sys.argv)>1 else None
def hdr(f): return np.fromfile(f'{D}/hdr_1_{f}.rgba16f',dtype=np.float16).reshape(H,W,4).astype(np.float32)
def dep(f): return np.fromfile(f'{D}/depth_1_{f}.rgba32f',dtype=np.float32).reshape(H,W,4)
def lum(a): return 0.2126*a[...,0]+0.7152*a[...,1]+0.0722*a[...,2]
for f in range(6136,6144):
    a=hdr(f); L=lum(a); d=dep(f)
    cov=(d[...,0]>=0)
    print(f, 'lum max %.2f p99.9 %.3f n>1 %d n>2 %d'%(L.max(),np.percentile(L,99.9),(L>1).sum(),(L>2).sum()),
          'depth covered %d zmin %.5f'%(cov.sum(), d[...,0][cov].min() if cov.any() else -1))
    if out:
        t=np.clip(a[...,:3]/(1+a[...,:3]),0,1)**(1/2.2)
        Image.fromarray((t*255).astype(np.uint8)).save(f'{out}/hdr_{f}.png')
        z=d[...,0]; img=np.where(z>=0, np.clip((1-z)*50,0,1), 0)
        Image.fromarray((img*255).astype(np.uint8)).save(f'{out}/depth_{f}.png')
