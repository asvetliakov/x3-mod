"""Fused-copy evidence gates; Wine execution is replaced only in runner tests."""
from pathlib import Path
import contextlib
import io
import json
import re
import struct
import tempfile
import unittest
from unittest.mock import patch
import run_linear_emission as r
from verification.analysis.test_linear_emission_pass_report import report as pass_report


def report(cases):
    text,data=pass_report(cases)
    lines=text.replace('PASS_TIMING','FUSED_TIMING').splitlines()[:-1]
    lines += [f"FUSED_CASE id={c['id']} rgba_depth_mask_exact=1 sources={len(c['ops'])}" for c in cases]
    lines.append(f'FUSED_RESULT pass cases={len(cases)}')
    return '\n'.join(lines)+'\n',data


class FusedReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases=r.pass_cases();cls.text,cls.data=report(cls.cases)

    def validate(self,text=None,data=None):
        return r.validate_coverage_report(self.text if text is None else text,self.data if data is None else data,self.cases,True,True)

    def test_all_twins_and_paired_cost_summary(self):
        result=self.validate()
        self.assertEqual(result['fused_image_twins'],60)
        self.assertEqual(result['fused_exact_channels'],261120)
        self.assertEqual(result['indexed_source_draws'],85)
        self.assertEqual(result['component_comparison_channels'],245760)
        self.assertTrue(result['native_reset_passed'])
        for timing in result['timings']:
            self.assertEqual(timing['samples'],8)
            self.assertEqual(timing['paired_delta_median_ms'],.0625)
            self.assertEqual(timing['paired_delta_range_ms'],[.0625,.0625])
            self.assertEqual([x['baseline_first'] for x in timing['pairs']],[i%2==0 for i in range(8)])
            self.assertTrue(all(x['delta_ms']==x['fused_ms']-x['baseline_ms'] for x in timing['pairs']))

    def test_exact_image_proof_rows_required(self):
        first=re.search(r'^FUSED_CASE .*$',self.text,re.M)[0]
        edits=[self.text.replace(first+'\n','',1),self.text.replace(first,first+'\n'+first,1),
               self.text.replace('FUSED_CASE id=0','FUSED_CASE id=1',1),
               self.text.replace('rgba_depth_mask_exact=1','rgba_depth_mask_exact=0',1),
               re.sub(r'(FUSED_CASE id=0 .*sources=)\d+',r'\g<1>999',self.text),
               self.text.replace('FUSED_RESULT pass cases=60','FUSED_RESULT pass cases=59'),
               self.text+'unexpected\n']
        for text in edits:
            with self.subTest(edit=text[-100:]),self.assertRaises(AssertionError):self.validate(text)

    def test_pair_order_finite_values_and_exact_row_count(self):
        first=re.search(r'^FUSED_TIMING .*$',self.text,re.M)[0]
        edits=[self.text.replace(first+'\n','',1),self.text.replace(first,first+'\n'+first,1),
               self.text.replace('variant=0 pair=0','variant=1 pair=0',1),
               self.text.replace('pair=0 order=0','pair=1 order=0',1),
               self.text.replace('width=1280 height=768','width=1280 height=769',1)]
        edits += [self.text.replace('completed_ms=1.0','completed_ms='+value,1) for value in ('nan','inf','-0.01')]
        for text in edits:
            with self.subTest(edit=text[-100:]),self.assertRaises(AssertionError):self.validate(text)

    def test_component_fault_caps_reset_gates_retained(self):
        for old,new in [('checks=16','checks=15'),('source_replays=0','source_replays=1'),
                        ('forbidden_calls=0','forbidden_calls=1'),('transaction=1','transaction=0'),
                        ('allocations=0','allocations=1')]:
            with self.subTest(old=old),self.assertRaises(AssertionError):self.validate(self.text.replace(old,new,1))

    def test_numeric_oracles_still_reject_corrupted_color_depth_energy_and_mask(self):
        for offset in (4,16,4+256*4*4,4+256*9*4,4+256*13*4):
            data=bytearray(self.data);value=struct.unpack_from('<f',data,offset)[0];struct.pack_into('<f',data,offset,value+1)
            with self.subTest(offset=offset),self.assertRaises(AssertionError):self.validate(data=data)

    def run_mocked(self,mutation=None):
        with tempfile.TemporaryDirectory(prefix='x3-fused-runner-host-') as directory:
            tmp=Path(directory);exe=tmp/'prebuilt.exe';exe.write_bytes(b'prebuilt')
            programs=tmp/'programs';programs.mkdir();raw=tmp/'raw';results=tmp/'results'
            for stage,names in (('vs',r.ORIGINAL_VS),('ps',r.ORIGINAL_PS)):
                for name in names:(programs/f'{stage}_{name}.bin').write_bytes(b'authored-test-input')
            def execute(command,*,stdout,stderr,env,timeout):
                self.assertIn('--mrt-fused',command)
                self.assertEqual(command[-2:],[f'Z:{programs.resolve()}',f'Z:{(raw/"variants").resolve()}'])
                self.assertEqual((raw/'cases.bin').read_bytes(),r.binary_cases(self.cases))
                stdout.write(self.text);(raw/'pixels.bin').write_bytes(self.data)
                for name in r.ORIGINAL_PS:
                    for gain in range(5):
                        for suffix in ('','-coverage'):(raw/'variants'/f'ps_{name}-{gain}{suffix}.bin').write_bytes(b'authored-transformed-output')
                if mutation=='exe':exe.write_bytes(b'changed')
                if mutation=='cases':(raw/'cases.bin').write_bytes(b'changed')
                if mutation=='original':next(programs.iterdir()).write_bytes(b'changed')
                return type('Completed',(),{'returncode':0})()
            argv=['runner','--mode','mrt-fused','--exe',str(exe),'--raw-dir',str(raw),'--programs',str(programs)]
            with patch.object(r.sys,'argv',argv),patch.object(r.bottle,'BOTTLE','X3'),patch.object(r.bottle,'describe',return_value={'name':'X3'}),patch.object(r.bottle,'results_dir',return_value=results),patch.object(r,'game_running',return_value=False),patch.object(r.subprocess,'run',side_effect=execute) as native,contextlib.redirect_stdout(io.StringIO()):
                if mutation:
                    with self.assertRaises(AssertionError):r.main()
                else:r.main()
                self.assertEqual(native.call_count,1)
            record=raw/'failed-result.json' if mutation else results/'linear-emission-mrt-fused-gpu.json'
            return json.loads(record.read_text())

    def test_runner_consumes_prebuilt_and_binds_all_inputs(self):
        result=self.run_mocked()
        self.assertTrue(result['passed'])
        self.assertEqual(result['cases'],60)
        self.assertEqual(len(result['original_sha256']),8)
        self.assertEqual(len(result['coverage_transformed_sha256']),25)
        self.assertIn('src/renderer/linear_emission_copy_clear_inc.h',result['code_sha256'])
        self.assertEqual(len(result['cases_sha256']),64)
        self.assertEqual(len(result['executable_sha256']),64)

    def test_runner_rejects_replaced_exe_cases_and_originals(self):
        for mutation in ('exe','cases','original'):
            with self.subTest(mutation=mutation):
                result=self.run_mocked(mutation)
                self.assertFalse(result['passed']);self.assertIn('changed during run',result['error'])


if __name__=='__main__':unittest.main()
