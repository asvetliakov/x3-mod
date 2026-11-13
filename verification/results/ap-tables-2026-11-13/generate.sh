#!/bin/sh
# AP weapon tables at schema 3 (2026-11-13), the Mayhem tree at C:\X3 and the stock catalogue view of the same tree.
# Outputs went to the session scratchpad; summary.json holds the counts and provenance.
python3 tools/effects/weapon_effects_table.py --game-root /mnt/c/X3 --edition ap --out "$1/mayhem.json"
python3 tools/effects/weapon_effects_table.py --game-root /mnt/c/X3 --edition ap --stock-only --out "$1/stock.json"
