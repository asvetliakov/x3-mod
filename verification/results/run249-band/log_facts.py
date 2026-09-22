#!/usr/bin/env python3
"""Run249: the resolved strict/band settings, from the first motion_output_mode row of the session log.
Usage: log_facts.py [log]"""
import re, sys
LOG = sys.argv[1] if len(sys.argv) > 1 else "/tmp/x3-bottleX3-run249/session-20260922-225116-212.log"
with open(LOG, errors="replace") as fh:
    for line in fh:
        if line.startswith("motion_output_mode "):
            print("motion_output_mode", " ".join(m.group(0) for m in re.finditer(r"\bsky_history\w*=\S+", line)))
            break
