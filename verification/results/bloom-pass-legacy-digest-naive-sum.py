#!/usr/bin/env python3
"""bloom-pass-legacy-digest.py with builtins.sum replaced by plain left-to-right addition.

Python 3.12 made sum() of floats compensated (Neumaier); Python <= 3.11 (macOS /usr/bin/python3 is 3.9) adds left to
right. If this prints the recorded digest on Python >= 3.12 while the plain script does not, the drift is sum().
"""
import builtins
import runpy
import sys
from pathlib import Path

_sum = builtins.sum


def naive_sum(iterable, start=0):
    total = start
    for value in iterable:
        total = total + value
    return total


builtins.sum = naive_sum
print('python', sys.version.split()[0])
runpy.run_path(str(Path(__file__).with_name('bloom-pass-legacy-digest.py')), run_name='__main__')
