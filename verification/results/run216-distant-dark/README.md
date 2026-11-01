# Run 216 distant-dark triage (AP native, 1.8.3 DLL 73508a5c)

Captures: `/mnt/c/X3/x3-modern-captures/*_1_{12898,15576}.*` (untracked). Log `/mnt/c/X3/x3m.log`.

    LC_ALL=C grep -E 'frame=(12898|15576)( |$)' /mnt/c/X3/x3m.log > frames.txt
    LC_ALL=C grep -E '^sun_shadow_apply_params .*frame=(12898|15576) ' /mnt/c/X3/x3m.log > params.txt
    python3 -I regions.py            # per-region view depth, HDR luminance, present luma
    python3 cascade_regions.py params.txt   # cascade twin: owner cascade, receiver validity, map margin
    python3 -I share_classes.py      # HDR luminance split by RT2 share lane (receiver vs fade band)
    grep -E '^motion_route .*node=4a9e8058' frames.txt   # big asteroid: fade band (12898) vs opaque (15576)

Result: big asteroid node 4a9e8058 (model 509e, LOD 0, PS d44db87778a43b61 in both frames) is drawn
in the engine's fade band at 12898 (blend SRCALPHA/INVSRCALPHA, zwrite 0, fade_arm 1, permille 997)
and opaque at 15576. Fade-band draws skip the original fill / sun lane (motion_output.cpp:7683, :7716):
night side HDR L p50 0.0000 (far) vs 0.0701 (near, share lane 0 = no sun term). EV identical (1.18961).
