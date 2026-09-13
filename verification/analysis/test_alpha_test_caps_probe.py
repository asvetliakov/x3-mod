"""Numeric eligibility and actual public-query arguments; no Wine or D3D device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from verification.analysis.test_capture_bloom_lifetime import extract_function
ROOT=Path(__file__).resolve().parents[2]
PREFIX=r'''
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>
#include <array>
using DWORD=std::uint32_t;using HRESULT=std::int32_t;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define D3DSHADER_VERSION_MAJOR(x) (((x)>>8)&255)
constexpr DWORD D3DADAPTER_DEFAULT=0,D3DDEVTYPE_HAL=1,D3DRTYPE_TEXTURE=3;
constexpr DWORD D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING=0x80000,D3DPMISCCAPS_INDEPENDENTWRITEMASKS=0x4000,D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS=0x40000;
constexpr DWORD D3DPCMPCAPS_GREATEREQUAL=0x40,D3DUSAGE_RENDERTARGET=1,D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING=0x80000;
constexpr DWORD D3DFMT_A16B16G16R16F=113,D3DFMT_A32B32G32R32F=116,D3DFMT_R32F=114;
struct D3DADAPTER_IDENTIFIER9{DWORD VendorId=0,DeviceId=0;};
struct D3DDISPLAYMODE{DWORD Format=0;};
struct D3DCAPS9{DWORD NumSimultaneousRTs=4,PrimitiveMiscCaps=0xc4000,AlphaCmpCaps=0x40,MaxVertexShaderConst=256,VertexShaderVersion=0xfffe0300,PixelShaderVersion=0xffff0300;};
struct IDirect3D9{
 D3DCAPS9 caps;int failure=-1,format_failure=-1;unsigned queries=0;bool wrong=false;
 HRESULT GetAdapterIdentifier(DWORD a,DWORD f,D3DADAPTER_IDENTIFIER9* o){wrong|=a!=0||f!=0;o->VendorId=1;o->DeviceId=2;return failure==0?-1:0;}
 HRESULT GetDeviceCaps(DWORD a,DWORD t,D3DCAPS9* o){wrong|=a!=0||t!=1;*o=caps;return failure==1?-1:0;}
 HRESULT GetAdapterDisplayMode(DWORD a,D3DDISPLAYMODE* o){wrong|=a!=0;o->Format=22;return failure==2?-1:0;}
 HRESULT CheckDeviceFormat(DWORD a,DWORD t,DWORD adapter,DWORD usage,DWORD type,DWORD format){
 const DWORD formats[]={113,116,114};unsigned n=queries++;
 wrong|=n>=6||a!=0||t!=1||adapter!=22||type!=3||format!=formats[n/2]||usage!=(n%2?0x80001u:1u);
 return int(n)==format_failure?-1:0;}
};
'''
MAIN=r'''
int main(){
 for(unsigned n=0;n<15;++n){IDirect3D9 d;
  if(n<6)d.format_failure=int(n);
  else if(n<9)d.failure=int(n-6);
  else if(n==9)d.caps.PrimitiveMiscCaps&=~0x80000u;
  else if(n==10)d.caps.PrimitiveMiscCaps&=~0x4000u;
  else if(n==11)d.caps.PrimitiveMiscCaps&=~0x40000u;
  else if(n==12)d.caps.AlphaCmpCaps=0x80;
  else if(n==13)d.caps.NumSimultaneousRTs=2;
  std::printf("HOST_CASE id=%u\n",n);
  bool complete=alpha_test_caps(&d);
  if(d.wrong||complete!=(n<6||n>=9)||d.queries!=((n>=6&&n<9)?0u:6u))return 1;
 }
}
'''
class AlphaTestCapsProbeTests(unittest.TestCase):
    def test_actual_queries_and_numeric_refusals(self):
        compiler=shutil.which('clang++') or shutil.which('c++');self.assertIsNotNone(compiler)
        source=(ROOT/'verification/probe/capability_probe.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='x3-alpha-caps-') as temp:
            path=Path(temp);(path/'probe.cpp').write_text(PREFIX+extract_function(source,'static bool alpha_test_caps(')+MAIN)
            result=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(path/'probe.cpp'),'-o',str(path/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(path/'probe')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            cases=result.stdout.split('HOST_CASE id=')[1:];self.assertEqual(len(cases),15)
            for n,body in enumerate(cases):
                complete=not 6<=n<9
                self.assertIn(f'ALPHA_TEST_RESULT complete={int(complete)} eligible={int(n==14)} formats={3 if complete else 0} queries={6 if complete else 0}',body)
                self.assertEqual(body.count('ALPHA_TEST_FORMAT '),3 if complete else 0)

    def test_mode_is_headless_and_build_is_selected(self):
        source=(ROOT/'verification/probe/capability_probe.cpp').read_text()
        mode=extract_function(source,'static int alpha_test_caps_mode(')
        self.assertNotIn('CreateWindow',mode);self.assertNotIn('CreateDevice',mode);self.assertNotIn('dx11',mode)
        main=extract_function(source,'int main(')
        self.assertLess(main.index('alpha_test_caps_mode()'),main.index('CreateWindowA'))
        build=(ROOT/'verification/probe/build.sh').read_text()
        self.assertLess(build.index('[ "${1:-}" != --capability-only ] || exit 0'),build.index('d3d9_smoke.cpp'))
