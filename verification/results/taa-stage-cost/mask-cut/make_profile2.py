"""Cost-only profile variants for mask_bench.cpp (not output-preserving; never shipped). Reads cur/ (the shader after
the tests-draw class precompute, centre reuse and composition outer-tap skip, before the class carriage) and writes
one directory per variant beside it. Rows in bench_out.txt, "profile".
"""
import os, shutil
os.chdir(os.path.dirname(os.path.abspath(__file__)))
cur = open('cur/line_mask_ps.hlsl').read()
cam = open('cur/line_mask_camera_ps.hlsl').read()
variants = {}
# tests draw without the 16-byte motion texel (unrouted everywhere; cost only)
a = '            float4 motion = tex2Dlod(motionOverride, float4(uv, 0, 0));\n'
assert a in cur
variants['nomotion'] = cur.replace(a, '            float4 motion = float4(uv, 0, -1);\n')
# composition without its two class fetches (every pixel treated as the sentinel class; cost only)
b = '''            [branch] if (sentinelDepth(tex2Dlod(ownDepth, float4(uv, 0, 0)).r) && options.x > 0.5) {
                float alpha = tex2Dlod(motionOverride, float4(uv, 0, 0)).w;
                sentinelClass = alpha >= -1 && alpha <= -1;
            }'''
assert b in cur
variants['noclassfetch'] = cur.replace(b, '            sentinelClass = options.x > 0.5;')
# composition with only the 4-byte own-depth fetch (class = sentinel depth; cost only)
variants['depthonly'] = cur.replace(b, '            sentinelClass = sentinelDepth(tex2Dlod(ownDepth, float4(uv, 0, 0)).r) && options.x > 0.5;')
for name, text in variants.items():
    os.makedirs(name, exist_ok=True)
    open(name + '/line_mask_ps.hlsl', 'w').write(text)
    open(name + '/line_mask_camera_ps.hlsl', 'w').write(cam)
    print(name, len(text))
