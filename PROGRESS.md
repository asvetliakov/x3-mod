# cull-small-parts review fixes (scratch, do not commit)
Done: (1) verifier pins prologue + [esp+0x2c] stores (18 checks); (2) docs cascade/every-view/single-m00/878 wording;
(3) camera_state arms only on parse_px+valid_px; (4) manage.py fixed-point .4f, rejects 0<px<0.0001; (5) chain_failed bookkeeping.
Remaining: rerun site verifier + host module; mingw compile of camera_state.cpp/cull_small_parts.cpp; stub bytes unchanged -> no Wine rerun.
