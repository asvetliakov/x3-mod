"""Production transfer arrays equal the previously qualified fixture programs."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]


def function(source,name):
    start=source.index(name+'(');start=source.rfind('\n',0,start)+1
    brace=source.index('{',start);depth=1;i=brace+1
    while depth:
        depth+=(source[i]=='{')-(source[i]=='}');i+=1
    return source[start:i]


def qualified_programs():
    source=(ROOT/'verification/probe/linear_emission_fixture.cpp').read_text()
    names=('reg','dst','src','bits','ins','def','gamma','shader','selective_composite')
    code='#include <cstdint>\n#include <cstring>\n#include <vector>\n#include <initializer_list>\n#include <iostream>\nusing DWORD=std::uint32_t;using Words=std::vector<DWORD>;\nconstexpr unsigned D3DSIO_MAX=11,D3DSIO_IFC=41,D3DSPC_GT=1,D3DSIO_ENDIF=43;\n'
    code+='\n'.join(function(source,name) for name in names)
    code+='\nint main(){for(const auto& p:{shader(5),selective_composite(false)}){for(auto w:p)std::cout<<std::hex<<w<<" ";std::cout<<"\\n";}}'
    with tempfile.TemporaryDirectory(prefix='x3-emission-pass-words-') as d:
        cpp=Path(d)/'words.cpp';exe=Path(d)/'words';cpp.write_text(code)
        subprocess.run(['clang++','-std=c++17','-O2',str(cpp),'-o',str(exe)],check=True,capture_output=True,text=True)
        return [[int(x,16) for x in line.split()] for line in subprocess.check_output([str(exe)],text=True).splitlines()]


class EmissionPassProgramTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.qualified=qualified_programs()

    def test_copy_and_composite_are_byte_exact_qualified_programs(self):
        for name,wanted in zip(('copy','composite'),self.qualified):
            text=(ROOT/f'src/renderer/linear_emission_{name}_inc.h').read_text()
            got=[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{8})u',text)]
            self.assertEqual(got,wanted,name)
            self.assertEqual(got[0],0xffff0300);self.assertEqual(got[-1],0xffff)


if __name__=='__main__':unittest.main()
