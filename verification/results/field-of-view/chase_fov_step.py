"""First-frame chase boom response to a FOV step at the compiled defaults (pos_lag_clamp 0.10).
Builds verification/probe/chase_camera_host.cpp with the host compiler and prints, per new
half_vfov_tan, the distance before the step, one frame after, the target, the lag and the clamp."""
import math, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/analysis'))
from test_chase_camera import Driver
with tempfile.TemporaryDirectory() as d:
    exe = Path(d) / 'host'
    subprocess.run([shutil.which('c++'), '-std=c++17', '-O2', '-ffp-contract=off', '-o', str(exe),
                    str(ROOT / 'verification/probe/chase_camera_host.cpp')], check=True)
    drv = Driver(exe)
    defaults = drv.defaults()
    for new in (0.75, 0.62, 0.60):
        drv.tunables(**defaults); drv.reset()
        first = drv.frame(1 / 60, half_vfov_tan=0.5625)
        step = drv.frame(1 / 60, half_vfov_tan=new)
        target = 1.05 * math.hypot(40, 200) * 0.75 / new
        print(f'0.5625->{new}: before={first["distance"]:.2f} next={step["distance"]:.2f} target={target:.2f} '
              f'lag={step["pos_lag"]:.2f} clamp={0.10 * target:.2f} snapped={int(step["snapped"])}')
    drv.close()
