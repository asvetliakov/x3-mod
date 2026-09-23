"""Cost-only profile variants for mask_bench.cpp (not output-preserving; never shipped). Reads cur/ (the shader after
the tests-draw class precompute, centre reuse and composition outer-tap skip, before the class carriage) and writes
one directory per variant beside it. Rows in bench_out.txt, "profile".
"""
import os, shutil
os.chdir(os.path.dirname(os.path.abspath(__file__)))
cur = open('cur/line_mask_ps.hlsl').read()
cam = open('cur/line_mask_camera_ps.hlsl').read()

main_start = cur.index('float4 main(float2 uv : TEXCOORD0) : COLOR0')
body = cur[main_start:]
sep_start = body.index('    } else if (options.z > 0.5) {')
tst_start = body.index('    } else {\n        float depth = fetch(uv).r;')
head = body[:body.index('    if (options.z > 1.5 && options.z < 2.5) {')]
sep_branch = body[sep_start + len('    } else if (options.z > 0.5) {'):tst_start]
tst_branch = body[tst_start + len('    } else {'):body.rindex('    return result;')]
tail = '    return result;\n}\n'

variants = {}
variants['sep'] = cur[:main_start] + head + '    {' + sep_branch + '    }\n' + tail
variants['tst'] = cur[:main_start] + head + '    {' + tst_branch + tail
unrolled = cur.replace('[loop] for (int j = 0; j < 10; ++j)', '[unroll] for (int j = 0; j < 10; ++j)').replace('[loop] for (int j = 0; j < 6; ++j)', '[unroll] for (int j = 0; j < 6; ++j)')
variants['unr'] = unrolled
variants['sepunr'] = variants['sep'].replace('[loop] for (int j = 0; j < 10; ++j)', '[unroll] for (int j = 0; j < 10; ++j)').replace('[loop] for (int j = 0; j < 6; ++j)', '[unroll] for (int j = 0; j < 6; ++j)')
# profiling-only cuts of the tests draw (not output-preserving)
frag = cur[cur.index('            float2 own = lineClass(depth);'):cur.index('            [branch] if (emissive.x > 0 && result.b < 0.5)')]
variants['nofrag'] = variants['tst'].replace(frag, '')
variants['nogate'] = variants['tst'].replace('            result.ar = gateOpen(uv, depth, motion);\n', '            result.ar = motion.w;\n')
variants['noemis'] = variants['tst'].replace('[branch] if (emissive.x > 0 && result.b < 0.5)', '[branch] if (emissive.x > 1e30 && result.b < 0.5)')
for name, text in variants.items():
    assert text != cur, name
    os.makedirs(name, exist_ok=True)
    open(name + '/line_mask_ps.hlsl', 'w').write(text)
    open(name + '/line_mask_camera_ps.hlsl', 'w').write(cam)
    print(name, len(text))
