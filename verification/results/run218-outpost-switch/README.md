# Run 218 outpost band/opaque switch (AP native, dev DLL 9be4c8a4, commit 793a2443)

Session 20261102-005506-32412, log `/mnt/c/X3/x3m.log`. F8 bursts this session: 3571 (distant, band),
4148 (closer, opaque), 15021 (distant, band), 16134 (closer, opaque); first frame of each burst used.
Station: node 3d8c0290 model 5c09 LOD 1 `stations\sadiablo\freighter_base`, VS 37c34a74, PS 5f82ecac (15749 prims),
f1b0e820 (4081), f1b0e820 atest (188).

    LC_ALL=C grep -E 'frame=(3571|4148|15021|16134)( |$)' /mnt/c/X3/x3m.log > frames.txt   # scratch, not kept
    python3 -I station_rows.py frames.txt > station_rows.txt   # route rows, widen rows, frame counters
    python3 -I regions.py   > regions.txt     # HDR luminance: station / upper / lower / top-2% / rock, by RT2 share lane
    python3 -I share_bins.py > share_bins.txt # opaque frames: station luminance by RT2.g share bin

Result: band rows are fade_arm=1 routed (permille 998/999), RT2.g = -1 on every station pixel (no cascade apply);
opaque rows are receivers (RT2.g 0..0.95). Band: top-2 % +14..18 %, p90 +16..33 %, upper-half p50 -20 %, lower p50 equal.
Per-draw bound program (fill/gain/owner form), engine-light admission and material strength are not logged per draw.
