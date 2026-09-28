#!/bin/bash
# Stock-view check (2026-09-29): the fog tool's full-install dry run must not change when
# sector_fog_census.Assets gains the pinned `catalogues=` view; the stock palette regression and
# the terran_spp_panel body test must run (not skip) on the stock layers of the modded install.
# Usage: bash verification/results/fog-family-data/stock_view_check.sh BEFORE_DRY_RUN.txt
# BEFORE_DRY_RUN.txt is `python3 tools/analysis/fog_families.py --dry-run` output from the parent commit.
set -e
before=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$(dirname "$0")/../../.."
after=$(mktemp)
python3 tools/analysis/fog_families.py --dry-run > "$after" 2>&1
# the summary line carries the volatile wall_seconds: it is left out of the diff and its counts printed below
if diff <(grep -v '"wall_seconds"' "$before") <(grep -v '"wall_seconds"' "$after") > /dev/null; then echo 'dry run: rows identical'; else echo 'dry run: DIFFERS'; fi
tail -1 "$after" | python3 -c 'import json, sys; d = json.loads(sys.stdin.read()); print(d["families"], d["counts"], d["tbackgrounds"])'
rm -f "$after"
PYTHONPATH=verification/probe:tools/analysis python3 -m unittest -v \
  verification.analysis.test_fog_families.StockPaletteRegression verification.analysis.test_lod_recipes.RealBody 2>&1 | grep -E '\.\.\. |^Ran|^OK|FAIL'
