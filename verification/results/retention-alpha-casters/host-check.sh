#!/bin/sh
# Host evidence of alpha-tested caster retention (docs/architecture/shadow-caster-retention.md,
# "Alpha-tested casters retained (2026-11-04)"). Run from the repository root; prints the unittest tails,
# the retention module's test count, the store driver's RESULT line (executed CHECKs) and the two
# editions' build state. No Wine.
set -e
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_shadow_retention \
    verification.analysis.test_shadow_replay_candidates verification.analysis.test_shadow_alpha_casters \
    verification.analysis.test_fl_object_lifetime 2>&1 | tail -n 3
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_shadow_replay_depth \
    verification.analysis.test_engine_effects 2>&1 | tail -n 3
echo "retention tests: $(grep -c '    def test_' verification/analysis/test_shadow_retention.py)"
python3 verification/results/retention-alpha-casters/driver_result.py
for edition in ap fl; do
    if [ -d build-$edition ]; then
        echo "build-$edition: $(cmake --build build-$edition -j6 2>&1 | grep -ci warning) warning lines"
    fi
done
