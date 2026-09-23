#!/usr/bin/env python3
"""Fog route step C (docs/architecture/fog-gpu-cost.md): every fog program against the base commit 638b19ad (step_b_programs'
table: bytecode identical or changed, slots / texture instructions / loops / rep counts; NEW for the programs step C adds), and
each quarter-resolution program against its scale-2 default (the program matrix: 4 march, 4 repair, 2 composite, 2 census).
Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_c_programs.py"""
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('step_b', Path(__file__).resolve().parent / 'step_b_programs.py')
step_b = importlib.util.module_from_spec(spec); spec.loader.exec_module(step_b)
step_b.BASE = '638b19ad'
slots = step_b.slots

if __name__ == '__main__':
    step_b.main()
    for name, header in slots.Q4_PROGRAMS.items():
        base = name.replace('_q4', '')
        a, b = slots.count(slots.words_of(slots.PROGRAMS[base])), slots.count(slots.words_of(header))
        print(f"{name:34s} vs {base}: slots {a['slots']} -> {b['slots']} tex {a['texture_instructions']} -> {b['texture_instructions']} "
              f"words {a['words']} -> {b['words']} rep {step_b.rep_counts(slots.words_of(slots.PROGRAMS[base]))} -> {step_b.rep_counts(slots.words_of(header))}")
