#!/bin/sh
# The amendment-13 fixture evidence (docs/verification/ship-steering.md, "Capital gate transit"): the host fixture's capital
# checks alone, with the calibrated engine model (slip on, the acceptance) and without the slip term (what slip does to them).
# The full fixture (the acceptance: `checks PASS`) is verification.analysis.test_ship_steering; its stdout is
# verification/results/ship-steering/ship_steering_host.txt.
set -e
cd "$(dirname "$0")/../../../.."
c++ -std=c++17 -O2 -Wall -Wextra -Werror verification/probe/ship_steering_host.cpp -o /tmp/ship_steering_host_fixture
/tmp/ship_steering_host_fixture --capital > verification/results/ship-steering/capital-gate-transit/fixture_capital.txt 2>&1
/tmp/ship_steering_host_fixture --capital-noslip > verification/results/ship-steering/capital-gate-transit/fixture_capital_noslip.txt 2>&1
/tmp/ship_steering_host_fixture > verification/results/ship-steering/ship_steering_host.txt
