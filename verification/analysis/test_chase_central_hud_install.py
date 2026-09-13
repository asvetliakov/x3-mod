"""Exercise the actual optional central gate transaction independently of lead."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from verification.analysis.test_chase_lead import extract_named_function
from verification.analysis.test_chase_native_timing_install import PREFIX

ROOT=Path(__file__).resolve().parents[2]
HUD_PREFIX = PREFIX.replace('bool timing=true;', '''engine_patch::Site hud_site;
engine_patch::SiteSpec hud_spec{0};
std::atomic<bool> hud_enabled{false};
bool timing=true;''').replace('int index=int(&s-timing_sites);', 'int index=0;').replace(
    'if(index!=unsigned(current+4))return nullptr;', 'if(index!=3)return nullptr;')
HUD_SUFFIX = r'''
int main() {
 unsigned checks=0,failures=0;
 auto check=[&](bool v){++checks;if(!v)++failures;};
 auto reset=[&]{hud_site={};hud_enabled=true;fail_index=fail_stage=restore_fail=-1;
  claims=0;restored.clear();
  for(unsigned i=0;i<3;++i)timing_sites[i]={true,"old_lead_active",&original};};
 auto old_lead=[&]{check(existing_lead);
  for(unsigned i=0;i<3;++i){check(timing_sites[i].patched_in);
   check(timing_sites[i].entry==&original);check(!std::strcmp(timing_sites[i].status,"old_lead_active"));}};
 const char* status=nullptr;
 reset();check(!install_central_hud(false,status));check(!hud_enabled);check(claims==0);
 check(restored.empty());check(!std::strcmp(status,"lead_unavailable"));old_lead();
 reset();check(install_central_hud(true,status));check(hud_enabled);check(claims==1);
 check(hud_site.patched_in);check(restored.empty());old_lead();
 for(int stage=0;stage<4;++stage){
  reset();fail_index=0;fail_stage=stage;
  check(!install_central_hud(true,status));check(!hud_enabled);check(claims==1);
  check(restored.size()==(stage?1u:0u));check(!hud_site.patched_in);
  check(!std::strcmp(status,stage?"stub_chain_failed":"claim_failed"));old_lead();
 }
 reset();fail_index=0;fail_stage=1;restore_fail=0;
 check(!install_central_hud(true,status));check(!hud_enabled);check(claims==1);
 check(restored.size()==1);check(hud_site.patched_in);
 check(!std::strcmp(status,"rollback_failed_disabled"));old_lead();
 std::printf("central_hud_install checks=%u failures=%u\n",checks,failures);
 return failures?1:0;
}
'''

class CentralHudInstall(unittest.TestCase):
    def test_optional_transaction_and_old_lead_preservation(self):
        source=(ROOT/'src/proxy/chase_lead.cpp').read_text()
        body=extract_named_function(source,'install_central_hud')
        with tempfile.TemporaryDirectory(prefix='x3-central-hud-install-') as tmp:
            cpp=Path(tmp)/'fixture.cpp';exe=Path(tmp)/'fixture'
            cpp.write_text(HUD_PREFIX+body+HUD_SUFFIX)
            build=subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('failures=0',run.stdout)
