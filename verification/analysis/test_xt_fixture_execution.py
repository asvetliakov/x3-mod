"""Cross-check appended GPU case equations against actual transformed PS tokens.

This is a finite float64 host check of selected pixel inputs, not GPU evidence.
The existing source driver supplies variants from local untracked game programs.
"""
import unittest

import linear_xt_fixture_reference as fixture
from run_linear_material import cube_sample, fixture_cases
from verification.analysis import test_xt_pixel_execution as execution


class XtFixtureExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        execution.XTPixelExecutionTests.setUpClass()
        cls.addClassCleanup(execution.XTPixelExecutionTests.doClassCleanups)
        cls.driver = execution.XTPixelExecutionTests()

    def test_appended_case_sample_equations(self):
        count=0
        for case in fixture_cases()[3549:]:
            gains=case['gains']
            if len(set(gains))!=1 or gains[0] not in (0,1,4,16):
                continue
            identity=fixture.PAIRS[case['pair']-148][1]
            contract=fixture.xt.CONTRACTS[identity]
            for sample in ((4,4),(12,12)):
                for linear in (False,True):
                    values=fixture.inputs(case,sample,linear=linear,cube_sampler=cube_sample)
                    state,_,_=self.driver.state(contract,values,linear=linear)
                    if not contract.bump:
                        varying=fixture.varyings(case,sample,linear=linear)
                        state.registers['v5']=(varying['shape'],varying['highlight'],0.,0.)
                    path=(self.driver.generated/f'ps_{identity}-0-{int(linear)}-{int(gains[0]) if linear else 1}.bin'
                          if linear or not contract.bump else self.driver.originals/f'ps_{identity}.bin')
                    execution.shader.execute(path.read_bytes(),state)
                    wanted=(fixture.xt.linear_pixel if linear else fixture.xt.native_pixel)(contract,values).output_rgba
                    with self.subTest(case=case['id'],sample=sample,linear=linear):
                        self.driver.assertVectorClose(state.registers['oC0'],wanted,tolerance=1e-5)
                    count+=1
        self.assertGreater(count,1000)


if __name__=='__main__':unittest.main()
