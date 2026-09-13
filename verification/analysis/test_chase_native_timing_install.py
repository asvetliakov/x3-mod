"""Exercise the actual five-site diagnostic transaction with injected failures."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from verification.analysis.test_chase_lead import extract_named_function

ROOT = Path(__file__).resolve().parents[2]

PREFIX = r'''
#include <atomic>
#include <cstring>
#include <vector>
#include <cstdio>
namespace engine_patch {
struct Site { bool patched_in=false; const char* status="idle"; void** entry=nullptr; };
struct SiteSpec { int index; };
}
engine_patch::Site timing_sites[5];
engine_patch::SiteSpec timing_specs[5]={{0},{1},{2},{3},{4}};
bool timing=true;
std::uint64_t frequency=10000000;
std::atomic<bool> native_timing_enabled{false};
bool existing_lead=true, existing_hud=true;
int fail_index=-1,fail_stage=-1,restore_fail=-1,current=-1,claims=0;
void* original=reinterpret_cast<void*>(1);
void* next_pointer=nullptr;
std::vector<int> restored;
namespace engine_patch {
bool claim(Site& s,const SiteSpec& spec) {
 current=spec.index;++claims;
 if(current==fail_index&&fail_stage==0){s.status="claim_failed";return false;}
 s.patched_in=true;s.entry=&original;s.status="active";return true;
}
bool store_pointer(void** at,void* value) {
 if(current==fail_index&&fail_stage==2)return false;
 *at=value;return true;
}
bool push_front(Site&,void*) {return !(current==fail_index&&fail_stage==3);}
bool restore(Site& s) {
 int index=int(&s-timing_sites);restored.push_back(index);
 if(index==restore_fail)return false;
 s.patched_in=false;return true;
}
}
void* emit(unsigned index,void*** next) {
 if(index!=unsigned(current+4))return nullptr;
 if(current==fail_index&&fail_stage==1)return nullptr;
 *next=&next_pointer;return reinterpret_cast<void*>(2);
}
'''
SUFFIX = r'''
int main() {
 unsigned checks=0,failures=0;
 auto check=[&](bool v){++checks;if(!v)++failures;};
 auto reset=[&]{for(auto& s:timing_sites)s={};timing=true;frequency=10000000;native_timing_enabled=false;
  fail_index=fail_stage=restore_fail=-1;claims=0;restored.clear();};
 const char* status=nullptr;
 reset();timing=false;
 check(!install_native_timing(true,status));check(claims==0);check(!std::strcmp(status,"telemetry_off"));
 reset();check(!install_native_timing(false,status));check(claims==0);
 check(!std::strcmp(status,"central_gate_unavailable"));
 reset();frequency=0;
 check(!install_native_timing(true,status));check(!native_timing_enabled);check(claims==0);
 check(!std::strcmp(status,"clock_unavailable"));
 reset();check(install_native_timing(true,status));check(native_timing_enabled);check(claims==5);
 check(restored.empty());for(auto& s:timing_sites)check(s.patched_in);
 for(int index=0;index<5;++index)for(int stage=0;stage<4;++stage) {
  reset();fail_index=index;fail_stage=stage;
  check(!install_native_timing(true,status));check(!native_timing_enabled);
  check(claims==index+1);check(existing_lead&&existing_hud);
  const int last=stage==0?index-1:index;
  check(restored.size()==unsigned(last+1));
  for(int j=0;j<=last;++j)check(restored[j]==last-j);
  for(auto& s:timing_sites)check(!s.patched_in);
  check(!std::strcmp(status,stage==0?"claim_failed":"stub_chain_failed"));
 }
 reset();fail_index=4;fail_stage=1;restore_fail=1;
 check(!install_native_timing(true,status));check(!native_timing_enabled);
 check(!std::strcmp(status,"rollback_failed_disabled"));check(timing_sites[1].patched_in);
 check(restored.size()==5);check(existing_lead&&existing_hud);
 std::printf("native_timing_install checks=%u failures=%u\n",checks,failures);
 return failures?1:0;
}
'''

class NativeTimingInstall(unittest.TestCase):
    def test_all_five_sites_and_failure_stages(self):
        source = (ROOT/'src/proxy/chase_lead.cpp').read_text()
        body = extract_named_function(source, 'install_native_timing')
        with tempfile.TemporaryDirectory(prefix='x3-native-timing-install-') as tmp:
            cpp=Path(tmp)/'fixture.cpp';exe=Path(tmp)/'fixture'
            cpp.write_text(PREFIX+body+SUFFIX)
            build=subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('failures=0',run.stdout)
