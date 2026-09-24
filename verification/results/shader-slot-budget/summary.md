# Shader instruction-slot budget

Bottle: X3 bottle, WineArch=arm64, FEX_X87REDUCEDPRECISION=1 WINEMSYNC=1. Adapter: NVIDIA_GeForce_8800_GTX. d3d9: C:\windows\system32\d3d9.dll, C:\windows\system32\wined3d.dll. d3dx9_37 sha256 c2ccb84c.
Produced by `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_shader_slot_budget.py` (fixture `verification/probe/shader_slot_budget_fixture.cpp` sha256 29d58586); exit 0, 558.6 s. Full record: `verification/results/bottle-X3/shader-slot-budget.json`.

## Caps

| field | value |
| --- | --- |
| hr | 00000000 |
| vs_version | 0300 |
| ps_version | 0300 |
| max_ps30_slots | 512 |
| max_vs30_slots | 512 |
| max_ps_executed | 65535 |
| max_vs_executed | 65535 |
| ps20_slots | 512 |
| ps20_dyn_flow | 24 |
| ps20_temps | 32 |
| ps20_static_flow | 4 |
| ps20_caps | 0000001f |
| vs20_dyn_flow | 24 |
| vs20_temps | 32 |
| vs20_static_flow | 4 |
| vs20_caps | 00000001 |
| max_vs_consts | 256 |

## Cases

N = mads in the chain (`x x L` = [loop] of L iterations). Executed = (g - 1) * 65536 read back at the centre pixel / expected. ms = per full-screen draw at 512x512 A32B32G32R32F (event-query sync); raw chains read only constants, so their value is uniform and their draw time is not per-pixel ALU cost. `asm` = D3DXAssembleShader, created only. Slots = D3DXDisassembleShader "approximately N instruction slots used". First draw = draw + sync of the first draw with the program, dominated by the backend shader compile: "cold" from the first run (`--first-run-log`, the first time the backend met these programs), "warm" from this run of identical bytecode (backend cache, inferred).

| stage | kind | N | compile hr | slots | create hr | draw hr | executed | ms/draw | first draw ms cold | first draw ms warm |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ps_3_0 | hlsl | 256 | 00000000 | 257 | 00000000 | 00000000 | 256/256 | 0.044 | 79.5 | 19.7 |
| ps_3_0 | hlsl | 500 | 00000000 | 501 | 00000000 | 00000000 | 500/500 | 0.044 | 41.7 | 10.6 |
| ps_3_0 | hlsl | 505 | 00000000 | 506 | 00000000 | 00000000 | 505/505 | 0.041 | 41.2 | 10.3 |
| ps_3_0 | hlsl | 506 | 00000000 | 507 | 00000000 | 00000000 | 506/506 | 0.042 | 41.2 | 10.3 |
| ps_3_0 | hlsl | 507 | 00000000 | 508 | 00000000 | 00000000 | 507/507 | 0.041 | 39.7 | 10.2 |
| ps_3_0 | hlsl | 508 | 00000000 | 509 | 00000000 | 00000000 | 508/508 | 0.037 | 39.6 | 10.7 |
| ps_3_0 | hlsl | 509 | 00000000 | 510 | 00000000 | 00000000 | 509/509 | 0.041 | 38.9 | 10.1 |
| ps_3_0 | hlsl | 510 | 00000000 | 511 | 00000000 | 00000000 | 510/510 | 0.041 | 39.0 | 10.1 |
| ps_3_0 | hlsl | 511 | 00000000 | 512 | 00000000 | 00000000 | 511/511 | 0.040 | 39.7 | 10.0 |
| ps_3_0 | hlsl | 512 | 00000000 | 513 | 00000000 | 00000000 | 512/512 | 0.036 | 39.2 | 10.1 |
| ps_3_0 | hlsl | 513 | 00000000 | 514 | 00000000 | 00000000 | 513/513 | 0.040 | 39.5 | 10.1 |
| ps_3_0 | hlsl | 600 | 00000000 | 601 | 00000000 | 00000000 | 600/600 | 0.045 | 46.9 | 11.7 |
| ps_3_0 | hlsl | 1024 | 00000000 | 1025 | 00000000 | 00000000 | 1024/1024 | 0.058 | 88.0 | 21.1 |
| ps_3_0 | hlsl | 2048 | 00000000 | 2049 | 00000000 | 00000000 | 2048/2048 | 0.102 | 218.9 | 55.2 |
| ps_3_0 | hlsl | 4096 | 00000000 | 4097 | 00000000 | 00000000 | 4096/4096 | 0.166 | 650.8 | 141.2 |
| ps_3_0 | hlsl | 8192 | 00000000 | 8193 | 00000000 | 00000000 | 8192/8192 | 0.571 | 2225.2 | 474.7 |
| ps_3_0 | hlsl | 16384 | 00000000 | 16385 | 00000000 | 00000000 | 16384/16384 | 0.645 | 8591.8 | 1784.3 |
| ps_3_0 | hlsl | 10 x16 | 00000000 | 18 | 00000000 | 00000000 | 160/160 | 0.015 | 10.1 | 2.4 |
| ps_3_0 | hlsl | 500 x64 | 00000000 | 508 | 00000000 | 00000000 | 32000/32000 | 0.942 | 62.9 | 27.2 |
| ps_3_0 | hlsl | 500 x128 | 00000000 | 508 | 00000000 | 00000000 | 64000/64000 | 6.288 | - | 117.1 |
| ps_3_0 | hlsl | 500 x132 | 00000000 | 508 | 00000000 | 00000000 | 66000/66000 | 6.302 | - | 70.3 |
| ps_3_0 | hlsl | 500 x255 | 80004005 `error_X3531:_Can't_unroll_loops_marked_with_loop_attribute` | -1 | - | - | - | - | - | - |
| ps_3_0 | raw | 509 | 00000000 | 511 | 00000000 | 00000000 | 509/509 | 0.016 | 51.3 | 11.5 |
| ps_3_0 | raw | 510 | 00000000 | 512 | 00000000 | 00000000 | 510/510 | 0.022 | 45.8 | 11.0 |
| ps_3_0 | raw | 511 | 00000000 | 513 | 00000000 | 00000000 | 511/511 | 0.021 | 49.7 | 11.0 |
| ps_3_0 | raw | 512 | 00000000 | 514 | 00000000 | 00000000 | 512/512 | 0.020 | 46.6 | 11.0 |
| ps_3_0 | raw | 1024 | 00000000 | 1026 | 00000000 | 00000000 | 1024/1024 | 0.021 | 126.8 | 24.2 |
| ps_3_0 | raw | 4096 | 00000000 | 4098 | 00000000 | 00000000 | 4096/4096 | 0.029 | 1022.4 | 169.5 |
| ps_3_0 | raw | 32766 | 00000000 | 32768 | 00000000 | 00000000 | 32766/32766 | 0.247 | 45778.1 | 7278.1 |
| ps_3_0 | raw | 32767 | 00000000 | 32769 | 00000000 | 00000000 | 32767/32767 | 0.249 | 46003.1 | 7258.6 |
| ps_3_0 | raw | 32768 | 00000000 | 32770 | 00000000 | 00000000 | 32768/32768 | 0.248 | 45974.9 | 7121.9 |
| ps_3_0 | raw | 65536 | 00000000 | 65538 | 00000000 | 80004005 | - | - | - | - |
| ps_3_0 | raw | 262144 | 00000000 | 262146 | 00000000 | - | - | - | - | - |
| ps_3_0 | asm | 510 | 00000000 | 512 | 00000000 | - | - | - | - | - |
| ps_3_0 | asm | 511 | 00000000 | 513 | 00000000 | - | - | - | - | - |
| ps_3_0 | asm | 4096 | 00000000 | 4098 | 00000000 | - | - | - | - | - |
| ps_3_0 | asm | 32765 | 00000000 | 32767 | 00000000 | - | - | - | - | - |
| ps_3_0 | asm | 32766 | 00000000 | 32768 | 00000000 | - | - | - | - | - |
| ps_3_0 | asm | 32767 | 00000000 | 32769 | 00000000 | - | - | - | - | - |
| ps_3_0 | asm | 32768 | 00000000 | 32770 | 00000000 | - | - | - | - | - |
| vs_3_0 | hlsl | 500 | 00000000 | 502 | 00000000 | 00000000 | 500/500 | - | - | 57109.0 |
| vs_3_0 | hlsl | 600 | 00000000 | 602 | 00000000 | 00000000 | 600/600 | - | - | 62.2 |
| vs_3_0 | hlsl | 4096 | 00000000 | 4098 | 00000000 | 00000000 | 4096/4096 | - | - | 1126.6 |
| vs_3_0 | asm | 510 | 00000000 | 513 | 00000000 | - | - | - | - | - |
| vs_3_0 | asm | 511 | 00000000 | 514 | 00000000 | - | - | - | - | - |
| vs_3_0 | asm | 32765 | 00000000 | 32768 | 00000000 | - | - | - | - | - |
| vs_3_0 | asm | 32766 | 00000000 | 32769 | 00000000 | - | - | - | - | - |
| vs_3_0 | raw | 600 | 00000000 | 603 | 00000000 | 00000000 | 600/600 | - | - | 60.5 |
| vs_3_0 | raw | 32766 | 00000000 | 32769 | 00000000 | - | - | - | - | - |
| vs_3_0 | raw | 32767 | 00000000 | 32770 | 00000000 | - | - | - | - | - |
| vs_3_0 | raw | 65536 | 00000000 | 65539 | 00000000 | - | - | - | - | - |

Retries with D3DXSHADER_SKIPVALIDATION after a compile failure:

- ps_3_0 N=500 loop=255: hr 80004005, `error_X3531:_Can't_unroll_loops_marked_with_loop_attribute`

First run log sha256 a979ec63: 28 cases matched, mismatched in words/slots/HRESULTs/executed: none.
