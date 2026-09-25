#!/bin/sh
# Four probe runs of the scratch fixture (run under wine_lock as one command): thin-region and box-half, each without and
# with the in-place 7x7 disabled (X3M_PROBE_NO_INPLACE=1).
S=/private/tmp/claude-501/-Users-asvetl-x3-mod/c01d92c1-4474-4383-946d-4fac988ae47e/scratchpad
R=$S/probe_tree
W=/Users/asvetl/x3-mod/.claude/worktrees/agent-a0c9df61900954b44
cp -R $W/src/temporal/*.hlsl $R/src/temporal/
python3 $S/run_mode.py $R thin-region $S/probe_tr_on.txt
X3M_PROBE_NO_INPLACE=1 python3 $S/run_mode.py $R thin-region $S/probe_tr_off.txt
python3 $S/run_mode.py $R box-half $S/probe_bh_on.txt
X3M_PROBE_NO_INPLACE=1 python3 $S/run_mode.py $R box-half $S/probe_bh_off.txt
