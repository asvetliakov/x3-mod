"""Root identity and call-graph coverage for the linked light-hook audit."""
import contextlib
import io
import json
import unittest
from unittest.mock import patch

import check_no_x87 as audit


DRAW = '__ZN3x3m12_GLOBAL__N_1L12draw_indexedEP16IDirect3DDevice917_D3DPRIMITIVETYPEijjjj@28'
# Real enclosing spelling (DRAW, from `nm` over the linked DLL) spliced into the
# real `call_preserved` lambda-thunk grammar that build shows, e.g.
# __ZZN3x3m14call_preservedIZNS_3logEPKczEUlvE_EEvOT_ENUlPvE_4_FUNES6_. These two
# names are constructed, not copied: no light hook currently encloses such a
# lambda (the lattice observer that did was removed on 2026-09-22). The guard
# they exercise - an enclosed lambda thunk must never be reported as the hook
# root - still has to hold for the next hook that gains one.
ENCLOSED = '__ZZN3x3m14call_preservedIZZN3x3m12_GLOBAL__N_1L12draw_indexedEP16IDirect3DDevice917_D3DPRIMITIVETYPEijjjjEUlvE%s_EEvOT_ENUlPvE_4_FUNES6_'
THUNKS = [ENCLOSED % '', ENCLOSED % '0']


def roots():
    result = {}
    for names, namespace in [(audit.LIGHT_HOOKS, '3x3m'),
                             (audit.GZ_HOOKS + audit.CRYPT_HOOKS, '3x3m13loading_trace')]:
        for name in names:
            result[name] = f'__ZN{namespace}12_GLOBAL__N_1L{len(name)}{name}Ev'
    for name in audit.LIGHT_LOADING_ROWS:
        result['light::' + name] = f'{audit.LIGHT_NAMESPACE}{len(name)}{name}Ev'
    for name in audit.CRYPT_FUNCTIONS:
        result['crypt_cache::' + name] = f'{audit.CRYPT_NAMESPACE}{len(name)}{name}Ev'
    result.update((name, name) for name in audit.EXTERN_ROOTS)
    result['ownership::get_buffer_lock_view_light'] = audit.OWNERSHIP_LIGHT + 'v'
    return result


def run_audit(functions):
    out = io.StringIO()
    with patch.object(audit, 'disassemble', return_value=functions), \
            patch.object(audit.sys, 'argv', ['check_no_x87.py', 'synthetic.dll']), \
            contextlib.redirect_stdout(out):
        result = audit.main()
    return result, json.loads(out.getvalue())


class NoX87RootTests(unittest.TestCase):
    def test_real_hook_not_enclosing_name_in_lambda_thunks(self):
        self.assertEqual(audit.hook_symbol(dict.fromkeys(THUNKS + [DRAW]), 'draw_indexed'), [DRAW])
        self.assertEqual(audit.hook_symbol(dict.fromkeys(THUNKS), 'draw_indexed'), [])

    def test_every_anonymous_root_and_older_internal_linkage_spelling(self):
        names = roots()
        for hook in audit.LIGHT_HOOKS + audit.GZ_HOOKS + audit.CRYPT_HOOKS:
            with self.subTest(hook=hook):
                name = names[hook]
                self.assertEqual(audit.hook_symbol({name: []}, hook), [name])
                older = name.replace('_GLOBAL__N_1L', '_GLOBAL__N_1')
                self.assertEqual(audit.hook_symbol({older: []}, hook), [older])
                wrong_scope = name.replace('3x3m', '5other')
                self.assertEqual(audit.hook_symbol({wrong_scope: []}, hook), [])

    def test_actual_overload_or_clone_ambiguity_still_fails(self):
        names = roots()
        for other in [DRAW, names['draw_indexed'] + '.constprop.0']:
            functions = dict.fromkeys(names.values(), [])
            functions[other] = []
            result, summary = run_audit(functions)
            self.assertEqual(result, 1)
            self.assertIn('draw_indexed: 2 symbols', summary['error'])

    def test_nested_thunks_cannot_supply_missing_root(self):
        names = roots()
        functions = dict.fromkeys(names.values(), [])
        del functions[names['draw_indexed']]
        functions.update(dict.fromkeys(THUNKS, []))
        result, summary = run_audit(functions)
        self.assertEqual(result, 1)
        self.assertIn('draw_indexed: 0 symbols', summary['error'])

    def test_all_roots_and_directly_reachable_nested_thunks_still_walked(self):
        names = roots()
        functions = dict.fromkeys(names.values(), [])
        functions[names['draw_indexed']] = [f' 100: call 200 <{THUNKS[0]}>']
        functions[THUNKS[0]] = [f' 200: jmp 300 <{THUNKS[1]}>']
        functions[THUNKS[1]] = [' 300: fadd %st(1),%st']
        result, summary = run_audit(functions)
        self.assertEqual(result, 1)
        self.assertEqual(summary['roots'], names)
        self.assertEqual(summary['reachable_functions'], len(names) + 2)
        self.assertEqual(summary['violations'], {THUNKS[1]: ['300: fadd %st(1),%st']})
        functions[THUNKS[1]] = [' 300: ret']
        result, summary = run_audit(functions)
        self.assertEqual(result, 0)
        self.assertEqual(summary['result'], 'PASS')


if __name__ == '__main__':
    unittest.main()
