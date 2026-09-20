"""Frame authority and authored profile policy, independent of D3D/Wine."""
import csv
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include "fog_sector_policy.h"
#include <cassert>
#include <cstring>
#include <limits>
using namespace x3m;
int main() {
    sector_background::Sample sample;
    sample.status=sector_background::Status::Ready;sample.row_valid=sample.name_valid=true;
    sample.sector=0x1000;sample.table=0x2000;sample.record=0x2044;sample.index=3;sample.dust=8;
    std::strcpy(sample.family,"bluewell");
    auto select=[&](float strength=.02f,bool enabled=true,bool force=false){return fog_sector_frame(sample,42,7,strength,enabled,force);};
    auto blue=select();assert(blue.profile==1&&blue.current(42)&&!blue.current(43)&&blue.density_scale==1.f&&!blue.forced);
    assert(!select(0).enabled&&!select(.02f,false).enabled&&!select(-1).enabled&&!select(.11f).enabled);
    assert(!select(std::numeric_limits<float>::quiet_NaN()).enabled);
    for (float s : {.005f,.01f,.02f,.03f,.05f,.1f})assert(select(s).density_scale==s/.02f);
    std::strcpy(sample.family,"foggreenoutlands");auto green=select();assert(green.profile==2&&!green.same_key(blue));
    // Row density metadata never scales the authored field.
    sample.dust=50;sample.fog_near=500;sample.fog_far=800;sample.stars=99;
    assert(select().density_scale==1.f&&select().same_key(green));
    for(const auto& family : renderer::fog_field::family_profiles) {
        std::strcpy(sample.family,family.family);
        auto selected=select();assert(selected.enabled&&selected.profile==unsigned(family.profile)&&!selected.forced);
        assert(!std::strcmp(selected.reason,family.family));
        assert(select(.02f,true,true).profile==selected.profile&&!select(.02f,true,true).forced);
        sample.dust=0;assert(!select().enabled&&select().profile==0);sample.dust=-1;assert(!select().enabled);sample.dust=8;
        sample.row_valid=false;assert(!select().enabled);sample.row_valid=true;
        sample.name_valid=false;assert(!select().enabled);sample.name_valid=true;
        sample.camera_check=sector_background::Check::Mismatch;assert(!select().enabled);sample.camera_check=sector_background::Check::Match;
        sample.anchor_check=sector_background::Check::Mismatch;assert(!select().enabled);sample.anchor_check=sector_background::Check::Match;
    }
    // Missing-asset positive definitions and unknown names stay native.
    for(const char* family : {"xtmgreenring","earth","unknown","Bluewell",""}) {
        std::strcpy(sample.family,family);assert(!select().enabled&&select().profile==0);
        assert(select(.02f,true,true).profile==1&&select(.02f,true,true).forced);
    }
    std::strcpy(sample.family,"bluewell");sample.dust=0;
    assert(!select().enabled&&!std::strcmp(select().reason,"clear"));sample.dust=8;
    sample.name_valid=false;assert(!select().enabled);sample.name_valid=true;
    sample.row_valid=false;assert(!select().enabled);sample.row_valid=true;
    sample.camera_check=sector_background::Check::Mismatch;assert(!select().enabled);sample.camera_check=sector_background::Check::Match;
    sample.anchor_check=sector_background::Check::Mismatch;assert(!select().enabled);sample.anchor_check=sector_background::Check::Match;
    for(auto status : {sector_background::Status::ReadFailure,sector_background::Status::ForeignExecutable,
        sector_background::Status::NoCockpit,sector_background::Status::NoSector,sector_background::Status::Loading,
        sector_background::Status::Malformed,sector_background::Status::BadIndex,sector_background::Status::CameraMismatch,
        sector_background::Status::AnchorMismatch}) {sample.status=status;assert(!select().enabled);}
    sample.status=sector_background::Status::Ready;
    assert(select().same_key(blue));
    auto key=blue;++key.sector;assert(!key.same_key(blue));key=blue;++key.table;assert(!key.same_key(blue));
    key=blue;++key.record;assert(!key.same_key(blue));key=blue;++key.index;assert(!key.same_key(blue));
    key=blue;++key.field_generation;assert(!key.same_key(blue));
    key=blue;++key.generation;assert(!key.same_key(blue));key=blue;++key.recipe;assert(!key.same_key(blue));
}
'''
class FogSectorPolicyTests(unittest.TestCase):
    def test_profiles_disables_keys_and_tuning(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'test.cpp';exe=Path(tmp)/'test'
            source.write_text('#include <initializer_list>\n'+DRIVER)
            subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/proxy'),str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_recipe_and_native_table_cover_every_mapped_positive_family(self):
        spec=importlib.util.spec_from_file_location('fog_recipe',ROOT/'tools/fog_field_recipe.py')
        recipe=importlib.util.module_from_spec(spec);spec.loader.exec_module(recipe)
        with (ROOT/'docs/reverse-engineering/sector-fog-census.csv').open() as stream:
            rows=list(csv.DictReader(stream))
        families={row['family'] for row in rows if int(row['dust'])>0}
        self.assertEqual(len(families),11)
        self.assertEqual(sum(int(row['dust'])>0 for row in rows),35)
        self.assertEqual(set(recipe.PROFILES),families | {"fogblue", "fogkhaak", "khaakhive"})
        # Compile an independently census-derived inventory, checking both name
        # and persistent ID against the public routing table used by the policy.
        checks=''.join(f'assert(!std::strcmp(family_profiles[{i}].family,"{name}"));assert(unsigned(family_profiles[{i}].profile)=={p["id"]});' for i,(name,p) in enumerate(recipe.PROFILES.items()))
        source='#include "fog_field_assets.h"\n#include <cassert>\n#include <cstring>\n#include <iterator>\nusing namespace x3m::renderer::fog_field;int main(){static_assert(std::size(family_profiles)==14);'+checks+'}'
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'coverage.cpp';exe=Path(tmp)/'coverage';cpp.write_text(source)
            subprocess.run([shutil.which('clang++') or shutil.which('g++'),'-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/renderer'),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
