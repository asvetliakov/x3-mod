# Run 217 outpost-dark triage (AP native, 1.8.4 DLL 2f278784)

Frames: 5845 (distant/dark, first of F8 burst 5845-5852) and 7063 (closer/lit, burst 7063-7070).
Captures `/mnt/c/X3/x3-modern-captures/*_1_{5845,7063}.*` (untracked). Log `/mnt/c/X3/x3m.log` (session 20261102-002044).
Station: target root 4bb6dfb8 (object_target target_id=1931), child node 4bb6a2f8 model 5c09 LOD 1,
body `stations\sadiablo\freighter_base` (cull_census), draws VS 37c34a7478544c14 + PS 5f82ecacd39529cd / f1b0e820c7b488c3.

    LC_ALL=C grep -E 'frame=(5845|7063)( |$)' /mnt/c/X3/x3m.log > frames.txt   # scratch, not kept
    python3 -I routes.py frames.txt [node]      # per node/ps motion_route aggregate
    grep -E '^motion_route device=1 frame=(5845 index=10[123]|7063 index=11[789]) ' frames.txt
    LC_ALL=C grep -E '^fade_route_frame .*frame=(584[4-6]|706[2-4]) ' /mnt/c/X3/x3m.log
    python3 -I regions.py          > regions.txt          # depth-covered pixels, share-lane split
    python3 -I regions_nodepth.py  > regions_nodepth.txt  # covered pixels without RT2 depth
    python3 -I dark_fraction.py    > dark_fraction.txt

Result: at 5845 the three station draws (index 101-103) are in the engine fade-band state (blend 1, SRCALPHA/
INVSRCALPHA, zwrite 0), routed=0 gate=4 unmatched=overlay_node (x2) / no_zwrite (188-prim sub-mesh), node=0 in the row;
at 7063 the same draws (117-119) are opaque (zwrite 1, blend 0), routed=1 gate 0. Same LOD 1 both frames.
VS 37c34a74 has no fade_route registers row (fade_route_core.h vertex_programs, deliberately excluded as "damage"),
so fade_route_pair=0 and the draw falls to the overlay arm, which refuses it (no routed draw of the same node just
before): native draw, no fill. fade_owner_forms rows exist for both PS (fill=1 gain=1 widen=1) but are never reached.

Fix (2026-11-02): VS 37c34a74 is fade_route row 10 (c40/c44, `fog_branch`: the arm's only with b0 TRUE, else the overlay
arm as before); ledger `docs/verification/fill-light.md` "XT BUMP stations in the fade band (Run 217)".

    python3 -I fade_state_census.py /mnt/c/X3/x3m.log /mnt/c/X3/x3m.prev.log > fade_state_census.txt  # b0 evidence
    python3 fade_arm_ab.py [b1ec6b0f] > fade_arm_ab.txt   # host fixture fade_arm_selection, working tree vs base
    python3 ../farnhams-legacy-build/cmp_objs.py <clean b1ec6b0f build-fl> <candidate build-fl> > fl_object_compare.json
    # capture.cpp.obj: objdump -d both, sed -E 's/0x[0-9a-f]+\(/D(/g', diff: only the MotionOutput size 0x16ae00 -> 0x16ae08
