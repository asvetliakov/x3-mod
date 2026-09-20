"""Focused guard runner and real extracted fixture dispatch lifetime checks."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from lattice_observer_guard_run import check
from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


def output(lazy):
    lines = []
    for cycle in range(2):
        for mode in range(3):
            for dropped in range(2):
                callback = int(bool(mode and dropped))
                broken = bool(lazy and mode == 1 and dropped)
                lines.append(f'LATTICE_GUARD cycle={cycle} mode={mode} dropped={dropped} lazy={int(lazy)} '
                             f'query_releases={callback} query_restores={int(mode == 1 and bool(dropped))} '
                             f'sample_releases=0 native_calls=1 depth={-1 if broken else .5} broken={int(broken)}')
        for action in (1, 2, 4, 5):
            lines.append(f'LATTICE_GUARD_NESTED cycle={cycle} action={action} result=8876086c frame=9 reset=1')
    lines += ['LATTICE_GUARD_FINAL routed=0 retired=1 pins=1 result=8876086c',
              'LATTICE_GUARD_FINAL routed=1 retired=1 pins=1 result=00000000', 'RESULT PASS checks=100']
    return '\n'.join(lines)


class LatticeObserverGuard(unittest.TestCase):
    def test_checker_matrix(self):
        for lazy in (False, True):
            result = check(output(lazy), lazy)
            self.assertEqual(result['status'], 'pass')
            self.assertFalse(result['historical_run193_causality_proved'])

    def test_checker_rejects_missing_or_false_witnesses(self):
        mutations = [
            ('sample_releases=0', 'sample_releases=1'),
            ('native_calls=1', 'native_calls=0'),
            ('retired=1', 'retired=0'),
            ('depth=-1', 'depth=0.5'),
            ('RESULT PASS checks=100', 'RESULT FAIL incomplete'),
            ('query_releases=1', 'query_releases=0'),
        ]
        for before, after in mutations:
            with self.subTest(before=before):
                with self.assertRaises(ValueError):
                    check(output(True).replace(before, after, 1), True)

    def test_owned_dispatch_does_not_write_native_table_and_survives_retirement(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        functions = '\n'.join(extract_function(source, signature) for signature in (
            'extern "C" __declspec(dllexport) HRESULT x3m_lattice_observer_fixture_arm(',
            'extern "C" __declspec(dllexport) HRESULT x3m_lattice_observer_fixture_disarm('))
        functions = functions.replace('__declspec(dllexport)', '')
        # The source under test operates on a real read-only mapped table. A
        # regression to writing ctx.original[82] faults instead of passing a mock.
        code = r'''
#include <sys/mman.h>
#include <unistd.h>
#include <memory>
#include <vector>
#include <map>
#include <cassert>
#include <cstdint>
using HRESULT=int; constexpr HRESULT S_OK=0,D3DERR_INVALIDCALL=-1;
struct IDirect3DDevice9 {};
namespace x3m {
struct CpuCallBoundary{};struct CaptureLock{};
namespace lattice_state {struct Capture {void arm(unsigned,unsigned,unsigned,bool){}};}
struct Device {void** original;std::vector<void*> table;unsigned id=1,frame=2,reset_generation=3;
 std::shared_ptr<lattice_state::Capture> lattice_state;
 template<class F> F get(unsigned slot){return reinterpret_cast<F>(original[slot]);}};
using FixtureDip=void(*)();void fixture_guard_native(){}void fixture_guard_declaration(){}
struct Result {unsigned mode=0,action=0;};
struct Observer {IDirect3DDevice9* device=nullptr;FixtureDip native=nullptr;bool active=false;
 void** borrowed_original=nullptr;void* declaration_entry=nullptr;std::vector<void*> saved_dispatch;Result result;} fixture_observer;
std::map<IDirect3DDevice9*,std::shared_ptr<Device>> devices;
}
void native_dip(){}void native_declaration(){}
''' + functions + r'''
int main(){
 const auto bytes=static_cast<size_t>(sysconf(_SC_PAGESIZE));
 auto* memory=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);
 auto** native=static_cast<void**>(memory);native[82]=reinterpret_cast<void*>(native_dip);native[88]=reinterpret_cast<void*>(native_declaration);
 assert(mprotect(memory,bytes,PROT_READ)==0);
 IDirect3DDevice9 device,other;
 for(unsigned retired=0;retired<2;++retired){
  auto owner=std::make_shared<x3m::Device>();owner->original=native;owner->table.assign(native,native+119);x3m::devices[&device]=owner;
  assert(x3m_lattice_observer_fixture_arm(&device,2,0)==S_OK);
  auto** dispatch=owner->original;assert(dispatch!=native&&dispatch==x3m::fixture_observer.saved_dispatch.data());
  assert(native[82]==reinterpret_cast<void*>(native_dip)&&dispatch[82]==reinterpret_cast<void*>(x3m::fixture_guard_native));
  assert(x3m::fixture_observer.native==native_dip);
  assert(x3m_lattice_observer_fixture_arm(&device,1,0)==S_OK&&owner->original==dispatch);
  assert(x3m_lattice_observer_fixture_arm(&other,2,0)==D3DERR_INVALIDCALL);
  if(retired){x3m::devices.erase(&device);owner.reset();}
  assert(x3m_lattice_observer_fixture_disarm(&device)==S_OK);
  assert(!x3m::fixture_observer.active&&x3m::fixture_observer.saved_dispatch.empty());
  assert(native[82]==reinterpret_cast<void*>(native_dip));
  if(owner){assert(owner->original==native&&owner->table[88]==native[88]);x3m::devices.erase(&device);}
 }
 assert(munmap(memory,bytes)==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(code)
            run = subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2', '-Wall', '-Wextra',
                                  '-Werror', '-Wno-unused-variable', str(path / 'test.cpp'), '-o', str(path / 'test')],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            run = subprocess.run([str(path / 'test')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
