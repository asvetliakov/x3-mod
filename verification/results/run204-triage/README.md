Run 204 A triage (2026-10-25). Log /mnt/c/X3/x3m.log, captures /mnt/c/X3/x3-modern-captures (not copied).
- pass1.py LOG OUT.json (copy of run203-triage), spikes.py OUT.json 30 (twin set = Run 204 creation frames) -> spikes_out.txt.
- Rows: grep -aE "^lod_strength_(session|variant|variant_frame)" x3m.log; no_twin sum over lod_strength_frame:
  grep -a "^lod_strength_frame" x3m.log | awk '{for(i=2;i<=NF;i++) if($i~/^no_twin=/){split($i,a,"=");s+=a[2]}} END{print s,NR}'  -> 0 25
- Twin-frame phases: grep -aE "^frame_phases_slow .*frame=(5810|6100|6535|24532|641) " x3m.log
- Outpost draws: grep -a "^object_bounds device=1 frame=1503 .*node=47e0a000" x3m.log (3 draws, index 24 alpha_tested=1)
- crop.py CAPDIR FRAME x0 x1 y0 y1 SCALE OUT.png; region_stats.py CAPDIR -> region_stats_out.txt (boxes re-picked; view differs from Run 203).
