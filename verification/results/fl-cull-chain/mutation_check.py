#!/usr/bin/env python3
"""Mutation check of the FL cull/LOD fixture (verification/analysis/test_fl_cull_chain.py build_and_run): each mutation of
src/engine/fl/cull_small_parts_fl.cpp, cull_small_parts_sites_fl.h or lod_patch_sites_fl.h is written in place (the
original restored in a finally block), built into the native fixture and must make it fail, fail its stub audit, or fail
to build (a static_assert). Prints one JSON line; exit 1 when a mutation survives.

usage: python3 verification/results/fl-cull-chain/mutation_check.py > verification/results/fl-cull-chain/mutation_check.json
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'verification/analysis'), str(ROOT / 'verification/probe')]
import test_fl_cull_chain as t  # noqa: E402

PROVIDER = ROOT / 'src/engine/fl/cull_small_parts_fl.cpp'
SITES = ROOT / 'src/engine/fl/cull_small_parts_sites_fl.h'
LOD = ROOT / 'src/engine/fl/lod_patch_sites_fl.h'
HANDLER = ROOT / 'src/proxy/engine_far_jets.cpp'
MUTATIONS = {
    # The stub (cull_small_parts_sites_fl.h encode_stub).
    'stub_ap_s_slot': (SITES, 'constexpr unsigned s_slot = 0x10;', 'constexpr unsigned s_slot = 0x2c;'),
    'stub_clobbers_ecx': (SITES, "    b(0x51);                                                 // 120 save the measure\n",
                          "    b(0x90);                                                 // 120 save the measure\n"),
    'stub_no_ecx_restore': (SITES, "    b(0x59);                                                 // 132\n",
                            "    b(0x90);                                                 // 132\n"),
    'stub_ap_model_offset': (SITES, 'threshold_1d8_offset = 0x1d8;\nconstexpr unsigned s_slot',
                             'threshold_1d8_offset = 0x1d8;\nconstexpr unsigned model_offset_ap = 0x140;\nconstexpr unsigned s_slot'),
    'stub_no_cmovg_replay': (SITES, '    b(0x0f); b(0x4f); b(0xd0);\n', '    b(0x90); b(0x90); b(0x90);\n'),
    'stub_skips_engine_and': (SITES, 'd(cull_target - (at + stub_exempt));', 'd(cull_target + 7 - (at + stub_exempt));'),
    'stub_far_args_swapped': (SITES, '    b(0x53); b(0x51); b(0x56);', '    b(0x51); b(0x53); b(0x56);'),
    'stub_upper_inclusive': (SITES, '    b(0x7d); rel8(stub_continue);                            // 18',
                             '    b(0x7f); rel8(stub_continue);                            // 18'),
    'stub_threshold_inclusive': (SITES, '    b(0x7c); rel8(stub_small);', '    b(0x7e); rel8(stub_small);'),
    'stub_marker_never_set': (SITES, 'b(0xf7); b(0x86); d(flags130_offset); d(small::projectile_flag);',
                              'b(0xf7); b(0x86); d(flags130_offset); d(0);'),
    'stub_far_bit0_unchecked': (SITES, 'b(static_cast<unsigned char>(small::jet_flag_low));', 'b(0);'),
    # The install (cull_small_parts_fl.cpp).
    'no_late_window': (PROVIDER, '    if (!engine_patch::install_window_open()) return done("late_claim");\n', ''),
    'no_window_check': (PROVIDER, '    if (const char* refused = S::plan(window)) return done(refused);\n', ''),
    'no_readback': (PROVIDER, 'if (!engine_patch::read_code(spec.address, now, 5) || std::memcmp(now, site_.patched, 5))',
                    'if (now[0] == 0xe9 && false)'),
    'chain_failure_not_rolled_back': (PROVIDER, 'return done(roll_back("chain_failed"));', 'return done("chain_failed");'),
    'far_writer_not_pinned': (PROVIDER, 'if (far_jets && !bytes_match(base + S::jet_writer_rva, S::jet_writer, S::jet_writer_length)) {',
                              'if (false) {'),
    'far_never_requested': (PROVIDER, 'applied = install_base(base, exempt, far_jets);',
                            'applied = install_base(base, exempt, false);'),
    # The far-jet handler (engine_far_jets.cpp, FL read set 2026-11-03).
    'handler_ap_model_offset': (HANDLER, 'constexpr unsigned model_at = fl_layout::model_offset;',
                                'constexpr unsigned model_at = core::model_offset;'),
    'handler_scale_unbound': (HANDLER, 'node_context && node_context == r.context ? word(', 'node_context ? word('),

    'marker_not_pinned': (PROVIDER, 'bytes_match(base + S::marker_or_rva, S::marker_or, S::marker_or_length)', 'true'),
    # The one-byte patches (lod_patch_sites_fl.h).
    'occlusion_fixup_unchecked': (LOD, '    if (w.fixup_offset && u32(current + w.fixup_offset) != std::uint32_t(base + w.fixup_rva)) return "bytes_mismatch";\n', '    (void)base;\n'),
    'terran_patches_ecx': (LOD, '0x16, 0xc1, 0xc0, 0x0e8e0d62', '0x16, 0xc1, 0xc8, 0x0e8e0d62'),
    'one_byte_no_rollback_write': (LOD, '        ops.write(at, &w.original, 1, &ignored);\n        reprotected', '        (void)ignored;\n        reprotected'),
    'one_byte_no_late_window': (LOD, '    if (!window_open) return "late_claim";\n', '    (void)window_open;\n'),
}


def main():
    results = {}
    for name, (target, old, new) in MUTATIONS.items():
        text = target.read_text()
        assert text.count(old) == 1, name
        mutated = text.replace(old, new)
        if name == 'stub_ap_model_offset':   # the dock compare reads AP's +0x140
            mutated = mutated.replace('b(0x8b); b(0x86); d(model_offset);', 'b(0x8b); b(0x86); d(model_offset_ap);')
        try:
            target.write_text(mutated)
            with tempfile.TemporaryDirectory(prefix='x3-fl-cull-mut-') as tmp:
                try:
                    run, audit = t.build_and_run(tmp)
                except subprocess.CalledProcessError:
                    results[name] = dict(killed=True, by='build')
                    continue
            failed = [l[5:] for l in run.stdout.splitlines() if l.startswith('FAIL ')]
            results[name] = dict(killed=run.returncode != 0 or audit.returncode != 0,
                                 by='fixture' if run.returncode else 'audit' if audit.returncode else 'none',
                                 fixture_failures=len(failed), first=[f.split(' ')[0] for f in failed[:3]])
        finally:
            target.write_text(text)
    survived = [k for k, v in results.items() if not v['killed']]
    print(json.dumps(dict(mutations=len(results), killed=len(results) - len(survived), survived=survived,
                          results=results)))
    return 1 if survived else 0


if __name__ == '__main__':
    sys.exit(main())
