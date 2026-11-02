#!/usr/bin/env python3
"""Mutation check of the FL engine-effects fixture (verification/analysis/test_fl_engine_effects.py build_and_run):
each mutation of src/engine/fl/engine_effects_fl.cpp (or its sites header) is written to a temporary sibling file,
built into the native fixture and must make it fail (or the stub audit). Prints one JSON line; exit 1 when a mutation
survives.

usage: python3 verification/results/fl-engine-effects/mutation_check.py > verification/results/fl-engine-effects/mutation_check.json
"""
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'verification/analysis'), str(ROOT / 'verification/probe')]
import test_fl_engine_effects as t  # noqa: E402

PROVIDER = ROOT / 'src/engine/fl/engine_effects_fl.cpp'
SITES = ROOT / 'src/engine/fl/engine_effects_sites_fl.h'
MUTATIONS = {
    'a_ap_offset': (PROVIDER, 'mov eax, dword ptr [esp+0xc]\n', 'mov eax, dword ptr [esp+0x14]\n'),
    'b_offset': (PROVIDER, 'mov eax, dword ptr [esp+0x10]\n', 'mov eax, dword ptr [esp+0xc]\n'),
    'a_clobbers_ecx': (PROVIDER, '    jmp dword ptr [_x3m_engine_effects_continue_a]',
                       '    xor ecx, ecx\n    jmp dword ptr [_x3m_engine_effects_continue_a]'),
    'a_clobbers_edx': (PROVIDER, '    jmp dword ptr [_x3m_engine_effects_continue_a]',
                       '    xor edx, edx\n    jmp dword ptr [_x3m_engine_effects_continue_a]'),
    'b_plain_ret': (PROVIDER, '    xor eax, eax\n    ret 0x10', '    xor eax, eax\n    ret'),
    'a_ret_pops': (PROVIDER, '    xor eax, eax\n    ret\n', '    xor eax, eax\n    ret 0x20\n'),
    'a_class_10_skipped': (PROVIDER, 'cmp word ptr [eax+0x48], 7\n    pop eax\n    je .Lx3m_fl_engine_effects_skip_a',
                           'cmp word ptr [eax+0x48], 10\n    pop eax\n    je .Lx3m_fl_engine_effects_skip_a'),
    'no_b_rollback': (PROVIDER, 'const bool b_back = engine_patch::restore_call(site_[1]);', 'const bool b_back = true;'),
    'no_late_window': (PROVIDER, 'if (!engine_patch::install_window_open()) return done("late_claim", "late_claim", "late_claim", false);', ''),
    'fixup_unchecked': (SITES, 'if (rel32_at(current + window_b_fixup_offset) != std::uint32_t(base + config_slot_rva)) return "bytes_mismatch_b";', '(void)base;'),
}


def main():
    source = PROVIDER.read_text()
    mutated = PROVIDER.with_name('.mutation_engine_effects_fl.cpp')
    header = SITES.with_name('.mutation_engine_effects_sites_fl.h')
    results = {}
    for name, (target, old, new) in MUTATIONS.items():
        text = target.read_text()
        assert text.count(old) == 1, name
        try:
            if target == SITES:
                header.write_text(text.replace(old, new))
                mutated.write_text(source.replace('#include "engine_effects_sites_fl.h"',
                                                  '#include ".mutation_engine_effects_sites_fl.h"'))
            else:
                mutated.write_text(text.replace(old, new))
            with tempfile.TemporaryDirectory(prefix='x3-fl-ee-mut-') as tmp:
                run, audit = t.build_and_run(tmp, mutated.relative_to(ROOT).as_posix())
            failed = [l[5:] for l in run.stdout.splitlines() if l.startswith('FAIL ')]
            results[name] = dict(killed=run.returncode != 0 or audit.returncode != 0, fixture_failures=len(failed),
                                 audit_exit=audit.returncode, first=[f.split(' ')[0] for f in failed[:3]])
        finally:
            mutated.unlink(missing_ok=True)
            header.unlink(missing_ok=True)
    survived = [k for k, v in results.items() if not v['killed']]
    print(json.dumps(dict(mutations=len(results), killed=len(results) - len(survived), survived=survived,
                          results=results), sort_keys=True))
    return 1 if survived else 0


if __name__ == '__main__':
    sys.exit(main())
