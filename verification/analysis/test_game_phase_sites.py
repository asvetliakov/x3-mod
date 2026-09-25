"""Independent admission/refusal checks for the 47 native marker contracts."""
import dataclasses
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import verify_game_phase_sites as probe
from verification.analysis.test_chase_lead import extract_named_function
from verification.analysis.test_chase_lead_sites import PatchedImage


ROOT=Path(__file__).resolve().parents[2]


COUNTING_EMITTER = r'''
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
using std::uint32_t;
using std::uintptr_t;
struct Emission { unsigned reserve=0, used=0; } last;
namespace engine_patch {
class Emitter {
public:
 explicit Emitter(unsigned reserve):reserve_(reserve){last.reserve=reserve;}
 bool ok() const{return good_;}
 void* here(){return good_?storage+cursor_:nullptr;}
 void byte(unsigned char){if(cursor_<reserve_)++cursor_;else good_=false;}
 void bytes(const void*,unsigned n){for(unsigned i=0;i<n;++i)byte(0);}
 void dword(uint32_t v){for(unsigned i=0;i<4;++i)byte(static_cast<unsigned char>(v>>(8*i)));}
 void rel32(const void*){dword(0);}
 void* finish(){if(!good_)return nullptr;last.used=(cursor_+3)&~3u;return storage;}
private:
 alignas(16) static unsigned char storage[512];
 unsigned reserve_=0,cursor_=0;bool good_=true;
};
alignas(16) unsigned char Emitter::storage[512]{};
}
extern "C" void x3m_resource_read_entry(){}
extern "C" void x3m_game_phase_enter(){}
extern "C" void x3m_pass_phase_enter(){}
extern "C" void x3m_chase_camera_enter(){}
extern "C" void x3m_chase_transition_enter(){}
extern "C" void x3m_chase_lead_enter(){}
extern "C" void x3m_chase_aim_enter(){}
extern "C" void x3m_chase_fire_enter(){}
extern "C" void x3m_voice_dmo_fallback_enter(){}
extern "C" void x3m_probe_enter(){}
extern "C" void x3m_probe_exit(){}
'''


def restore_filter_count():
    """The production constant the chase_transition stub's prefilter is sized by."""
    header=(ROOT/'src/proxy/chase_transition_restore_core.h').read_text()
    match=re.search(r'constexpr unsigned restore_filter_count=(\d+)',header)
    if match is None:raise ValueError('restore_filter_count not found')
    return int(match.group(1))


def emitter_fixture_source():
    # Since 123f98d (chase view restore ticket) chase_transition::emit is a thin
    # wrapper over the shared register-saving emit_stub, so the stub is extracted
    # with it and the one constant it reads is mirrored from its own header.
    emitters=(
        ('resource_reader','resource_reader.cpp','emit_reader_stub','emit_reader_stub(&next)','',()),
        ('game_phases','game_phases.cpp','emit','emit(0,&next)','',()),
        # The pass-phase and loop-phase stamps share the lean stub (no x87 save).
        ('pass_phases','lean_stub.cpp','emit','emit(nullptr,0,&next)','constexpr unsigned reserve=160;',()),
        ('loop_phases','lean_stub.cpp','emit','emit(nullptr,0,&next)','constexpr unsigned reserve=160;',()),
        ('residual_phases','lean_stub.cpp','emit','emit(nullptr,0,&next)','constexpr unsigned reserve=160;',()),
        # The submit-phase stamps use the context variant (pushad frame passed to the handler).
        ('submit_phases','lean_stub.cpp','emit_context','emit_context(nullptr,0,&next)','constexpr unsigned context_reserve=176;',()),
        # The media-cue gate: two arms plus the return trampoline in one block.
        ('media_cue','media_cue.cpp','emit_gate','emit_gate(nullptr,nullptr,&next,nullptr)','',()),
        ('chase_camera','chase_camera.cpp','emit_stub','emit_stub(&next)','',()),
        ('chase_transition','chase_transition.cpp','emit','emit(0,&next)',
         f'namespace detail {{ constexpr unsigned restore_filter_count={restore_filter_count()}; }}',
         ('emit_stub',)),
        ('chase_lead','chase_lead.cpp','emit','emit(0,&next)','',()),
        ('chase_aim_trace','chase_aim_trace.cpp','emit','emit(0,&next)','',()),
        ('chase_fire','chase_fire.cpp','emit','emit(&next)','',()),
        ('voice_dmo_fallback','voice_dmo_fallback.cpp','emit','emit(&next)','',()),
        ('loading_probes','loading_probes.cpp','emit_entry_stub','emit_entry_stub(0,&next)','',()),
        ('loading_probes_exit','loading_probes.cpp','emit_exit_stub','emit_exit_stub()','',()),
    )
    chunks=[COUNTING_EMITTER]
    calls=[]
    for label,filename,name,call,prelude,helpers in emitters:
        source=(ROOT/'src/proxy'/filename).read_text()
        if filename in ('game_phases.cpp','pass_phases.cpp','loop_phases.cpp'):
            source=source.replace('void* emit(unsigned index,void*** next_out);','')
        bodies=[extract_named_function(source,helper) for helper in helpers]
        bodies.append(extract_named_function(source,name))
        body='\n'.join(bodies)
        chunks.append(f'namespace {label} {{\n{prelude}\n{body}\n}}')
        next_check='||!next' if '&next' in call else ''
        calls.append(
            f'next=nullptr;last={{}};if(!{label}::{call}{next_check})return 2;'
            f'std::printf("{label} %u %u\\n",last.reserve,last.used);')
    chunks.append('int main(){void** next=nullptr;'+''.join(calls)+'return 0;}')
    return '\n'.join(chunks)


class SourceAndReplay(unittest.TestCase):
    def test_exact_production_order_and_spec_fields(self):
        self.assertEqual(len(probe.SITES),33)
        self.assertEqual(probe.PHASE_COUNT,33)
        self.assertEqual(probe.SITES[-1].va,0x498f5a)
        self.assertTrue(probe.source_checks(probe.SOURCE.read_text()))

    def test_ret_pop_and_relocation_fields_cannot_swap(self):
        text=probe.SOURCE.read_text()
        self.assertIn(',5,0,1}',text)
        self.assertFalse(probe.source_checks(text.replace(',5,0,1}',',5,1,0}',1)))
        self.assertIn('"game_phase_publisher_begin",0x00425a10',text)
        self.assertIn('},5,4,0}',text)
        self.assertIn('"game_phase_publisher_end",0x00425c79',text)
        self.assertIn('},7,4,0}',text)
        self.assertFalse(probe.source_checks(text.replace('},5,4,0}','},5,0,0}',1)))
        self.assertFalse(probe.source_checks(text.replace('},7,4,0}','},7,0,0}',1)))

    def test_missing_reordered_or_duplicated_site_refused(self):
        text=probe.SOURCE.read_text()
        lines=text.splitlines()
        entries=[i for i,line in enumerate(lines) if '{"game_phase_' in line]
        for mode in ('missing','reorder','duplicate'):
            changed=list(lines)
            if mode=='missing':del changed[entries[0]]
            elif mode=='duplicate':changed.insert(entries[0],changed[entries[0]])
            else:changed[entries[0]],changed[entries[1]]=changed[entries[1]],changed[entries[0]]
            self.assertFalse(probe.source_checks('\n'.join(changed)),mode)

    def test_every_rel32_arena_replay_preserves_destination_and_opcode(self):
        for site in probe.SITES:
            for arena in (0x10000000,0x71000000,0xf1000000):
                with self.subTest(site=site.name,arena=hex(arena)):
                    code=probe.relocated_bytes(site,arena)
                    self.assertEqual(len(code),len(site.expected))
                    self.assertEqual(code[0],site.expected[0])
                    if site.va in probe.TARGETS:
                        offset=probe.RELATIVE[site.va][0]
                        self.assertEqual(code[:offset],site.expected[:offset])
                        target=(arena+offset+4+probe.struct.unpack_from('<i',code,offset)[0])&0xffffffff
                        self.assertEqual(target,probe.TARGETS[site.va])
                    else:self.assertEqual(code,site.expected)

    def test_combined_hook_arena_footprint_and_admission(self):
        compiler=shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler,'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-phase-arena-') as temporary:
            cpp=Path(temporary)/'count.cpp';exe=Path(temporary)/'count'
            cpp.write_text(emitter_fixture_source())
            build=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',
                                  str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        emitted={}
        for line in run.stdout.splitlines():
            name,reserve,used=line.split();emitted[name]=(int(reserve),int(used))
        # chase_transition reserves 320 since 123f98d (chase view restore):
        # emit() shares emit_stub with the filtered restore path, whose
        # prefilter is sized into the same reservation. Emitted bytes unchanged.
        self.assertEqual(emitted,{
            'resource_reader':(48,32),'game_phases':(192,128),'pass_phases':(160,124),'loop_phases':(160,124),'residual_phases':(160,124),'submit_phases':(176,128),'media_cue':(320,300),
            'chase_camera':(160,124),'chase_transition':(320,128),
            'chase_lead':(192,128),'chase_aim_trace':(176,128),
            'chase_fire':(176,124),'voice_dmo_fallback':(192,124),
            'loading_probes':(40,28),'loading_probes_exit':(32,24),
        })

        def lengths(filename):
            return [row['length'] for row in
                    probe.common.parse_source_specs((ROOT/'src/proxy'/filename).read_text())]
        families={
            'game_phases':lengths('game_phase_sites.h'),
            # The ten frame-phase stamps use the game-phase emitter (indices
            # after the phase group) and claim through the same tail shape.
            'frame_phases':lengths('frame_phase_sites.h'),
            'pass_phases':lengths('pass_phase_sites.h'),
            'loop_phases':lengths('loop_phase_sites.h'),
            'residual_phases':lengths('residual_phase_sites.h'),
            'submit_phases':lengths('submit_phase_sites.h'),
            'media_cue':lengths('media_cue_sites.h'),
            'chase_transition':lengths('chase_transition.cpp'),
            'chase_lead':lengths('chase_lead.cpp'),
            'chase_aim_trace':lengths('chase_aim_trace.cpp'),
            'chase_fire':lengths('chase_fire.cpp'),
            'voice_dmo_fallback':lengths('voice_dmo_fallback.cpp'),
            'loading_probes':lengths('loading_probes.cpp'),
        }
        camera=(ROOT/'src/proxy/chase_camera.cpp').read_text()
        camera_match=re.search(r'SiteSpec site_spec\s*=\s*\{"cockpit_update_pose",\s*site_va,\s*\{[^}]+\},\s*(\d+),',camera)
        self.assertIsNotNone(camera_match)
        families['chase_camera']=[int(camera_match.group(1))]
        reader=(ROOT/'src/proxy/resource_reader.cpp').read_text()
        match=re.search(r'SiteSpec spec\{"resource_read",reader_va,\{\},(\d+),0,0\}',reader)
        self.assertIsNotNone(match)
        families['resource_reader']=[int(match.group(1))]
        # chase_transition carries 16 rows since 123f98d added the seven
        # byte-verified chase view restore sites.
        self.assertEqual({name:len(value) for name,value in families.items()},
                         {'resource_reader':1,'game_phases':33,'frame_phases':10,'pass_phases':4,'loop_phases':6,'residual_phases':2,'submit_phases':22,'media_cue':1,'chase_camera':1,
                          'chase_transition':16,'chase_lead':9,'chase_aim_trace':4,'chase_fire':1,'voice_dmo_fallback':1,
                          'loading_probes':12})
        lead_rows=probe.common.parse_source_specs((ROOT/'src/proxy/chase_lead.cpp').read_text())
        self.assertEqual([row['name'] for row in lead_rows],[
            'chase_lead_gate','chase_lead_publish','chase_lead_final_fov',
            'chase_central_hud_gate','chase_native_solver_begin','chase_native_solver_end',
            'chase_native_distance_begin','chase_native_distance_end','chase_native_central_end'])

        engine=(ROOT/'src/proxy/engine_patch.cpp').read_text()
        claim=extract_named_function(engine,'claim')
        compact=re.sub(r'\s+','',claim)
        self.assertIn('Emittere(spec.length+5+4+6+8);',compact)
        self.assertIn('e.bytes(displaced,spec.length);e.byte(0xe9);e.rel32(',compact)
        self.assertIn('site.entry=static_cast<void**>(e.here());e.dword(',compact)
        self.assertIn('site.dispatcher=e.here();e.byte(0xff);e.byte(0x25);e.dword(',compact)
        engine_compact=re.sub(r'\s+','',engine)
        self.assertIn('arena_cursor+reserve>arena_size',engine_compact)
        capacity=int(re.search(r'constexprunsignedarena_size=(\d+);',engine_compact).group(1))
        self.assertEqual(capacity,24576)

        # claim() emits the stolen span, a five-byte tail jump, alignment,
        # a four-byte chain head and a six-byte indirect dispatcher.
        claim_used=lambda length: (((length+5+3)&~3)+4+6+3)&~3
        operations=[]
        for name in ('resource_reader','game_phases','frame_phases','pass_phases','chase_camera','chase_transition',
                     'chase_lead','chase_aim_trace'):
            reserve,stub_used=emitted['game_phases' if name=='frame_phases' else name]
            for length in families[name]:
                operations.extend(((length+23,claim_used(length)),(reserve,stub_used)))

        def admit(limit,selected=operations):
            cursor=0
            for reserve,used in selected:
                if cursor+reserve>limit:return False,cursor
                cursor+=used
            return True,cursor

        accepted,used=admit(capacity)
        # 13908 with the 47-site phase group, the frame and pass stamps and the
        # chase set; the 14 audio witnesses (X3M_AUDIO_SITES, removed
        # 2026-09-25) took 2144 of it, so 11764 now.
        self.assertTrue(accepted);self.assertEqual(used,11764)
        self.assertEqual(capacity-used,12812)
        self.assertGreaterEqual(capacity-used,max(reserve for reserve,_ in operations))
        self.assertEqual(admit(8192)[0],False)
        # Every optional group on: the six loop stamps (claims of 6/6/6/5/6/5
        # bytes, 6 * 24, plus 6 * 124) with X3M_LOOP_PHASES=1, the two residual
        # stamps (claims of 8/9 bytes, 2 * 28, plus 2 * 124) with
        # X3M_RESIDUAL_PHASES=1, the twenty-two submit stamps (claims of 5-10
        # bytes, 22 * 24 or 28, plus 22 * 128 for the context stub) with
        # X3M_SUBMIT_PHASES=1, the media-cue gate (one 5-byte claim, 24, plus
        # the 300-byte two-arm stub with its return trampoline) with
        # X3M_MEDIA_CUE_TRACE/CACHE, the chase cursor admission site, the voice
        # DMO fallback site and the twelve loading probes (one shared exit stub
        # plus an entry stub per site). The remaining headroom is what a future
        # group can claim; chase_transition's 320-byte reserve is the largest
        # single reservation.
        everything=list(operations)
        for name in ('loop_phases','residual_phases','submit_phases','media_cue','chase_fire','voice_dmo_fallback'):
            reserve,stub_used=emitted[name]
            for length in families[name]:
                everything.extend(((length+23,claim_used(length)),(reserve,stub_used)))
        everything.append(emitted['loading_probes_exit'])
        for length in families['loading_probes']:
            everything.extend(((length+23,claim_used(length)),emitted['loading_probes']))
        accepted_all,used_all=admit(capacity,everything)
        self.assertTrue(accepted_all);self.assertEqual(used_all,17608)
        self.assertEqual(capacity-used_all,6968)
        self.assertEqual(sum(used for _,used in everything)-sum(used for _,used in operations),5844)
        # 6,968 B left since the audio witnesses went (2026-09-25; 4,824 B
        # before). The arena grew by one page for the media-cue gate and by
        # another with the residual group as headroom for later groups;
        # 24,576 holds two further six-site lean groups with a second gate each.
        self.assertGreaterEqual(capacity-used_all,max(reserve for reserve,_ in everything))
        self.assertGreaterEqual(capacity-used_all,2*(6*(24+124)+(24+300))+max(reserve for reserve,_ in everything))
        self.assertLess(16384-used_all,max(reserve for reserve,_ in everything))
        old_game=23
        old_operations=[]
        for name in ('resource_reader','game_phases','chase_camera','chase_transition',
                     'chase_lead','chase_aim_trace'):
            reserve,stub_used=emitted[name]
            selected=families[name][:old_game] if name=='game_phases' else families[name]
            for length in selected:
                old_operations.extend(((length+23,claim_used(length)),(reserve,stub_used)))
        self.assertEqual(admit(16384,old_operations),(True,8124))
        self.assertEqual(admit(8192,old_operations),(True,8124))
        # The production '>' guard admits an exact fit and rejects one byte over.
        largest=max(reserve for reserve,_ in operations)
        self.assertEqual((8192-largest)+largest,8192)
        self.assertGreater((8192-largest+1)+largest,8192)


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(),'installed X3AP.exe unavailable')
class NativeSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=probe.common.Image(probe.DEFAULT_EXE.read_bytes())
        cls.decoded=probe.decode()
        cls.source=probe.SOURCE.read_text()

    def report(self,image=None,decoded=None):
        return probe.inspect(image or self.image,decoded or self.decoded,self.source)

    def test_actual_executable_and_all_33_spans(self):
        report=probe.verify()
        self.assertEqual(report['result'],'PASS',report['checks'])
        self.assertEqual(len(report['sites']),33)

    def test_corrupted_byte_refused_at_every_site(self):
        for site in probe.SITES:
            with self.subTest(site=site.name):
                image=PatchedImage(self.image,[(site.va,bytes([site.expected[0]^1]))])
                report=self.report(image=image)
                row=next(row for row in report['sites'] if row['name']==site.name)
                self.assertFalse(row['bytes_ok'])
                self.assertFalse(row['ok'])

    def test_interior_entry_from_other_routine_refused_at_every_site(self):
        for site in probe.SITES:
            with self.subTest(site=site.name):
                decoded={bounds:list(value) for bounds,value in self.decoded.items()}
                other=probe.PRESENT if site.function_start!=probe.PRESENT[0] else probe.MAIN
                decoded[other].append(probe.common.Instruction(0x100,b'\xe9\0\0\0\0','jmp',hex(site.va+1)))
                row=next(row for row in self.report(decoded=decoded)['sites'] if row['name']==site.name)
                self.assertFalse(row['no_interior_branch'])
                self.assertFalse(row['ok'])

    def test_incoming_at_span_start_is_allowed(self):
        decoded={bounds:list(value) for bounds,value in self.decoded.items()}
        for site in probe.SITES:
            decoded[probe.MAIN].append(probe.common.Instruction(0x100,b'\xe9\0\0\0\0','jmp',hex(site.va)))
        report=self.report(decoded=decoded)
        self.assertTrue(report['checks']['sites'])
        self.assertTrue(all(row['no_interior_branch'] for row in report['sites']))

    def test_start_or_end_inside_instruction_refused(self):
        for site in probe.SITES:
            with self.subTest(site=site.name):
                bounds=(site.function_start,site.function_end)
                for boundary in ('start','end'):
                    decoded=dict(self.decoded)
                    decoded[bounds]=[i for i in decoded[bounds] if
                        (i.va!=site.va if boundary=='start' else i.end!=site.end)]
                    row=next(row for row in self.report(decoded=decoded)['sites'] if row['name']==site.name)
                    self.assertFalse(row['whole_instructions'])

    def test_all_relative_decoded_destinations_must_match(self):
        for site in probe.SITES:
            if site.va not in probe.TARGETS:continue
            with self.subTest(site=site.name):
                decoded=dict(self.decoded)
                bounds=(site.function_start,site.function_end)
                # The relative branch is the last instruction of the span (a
                # call/jmp is the whole span; a Jcc follows the flag setter).
                decoded[bounds]=[dataclasses.replace(i,operands='0x400000')
                                 if site.va<=i.va<site.end and probe.common._is_direct_control(i) is not None else i
                                 for i in decoded[bounds]]
                row=next(row for row in self.report(decoded=decoded)['sites'] if row['name']==site.name)
                self.assertFalse(row['relative_contract'])
                self.assertFalse(row['ok'])

    def test_jump_table_interior_entry_refused(self):
        image=PatchedImage(self.image,[(0x425c80,probe.struct.pack('<I',0x425bad))])
        self.assertFalse(self.report(image=image)['checks']['publisher_jump_table'])

    def test_mov_jump_table_target_and_site_interior_are_refused(self):
        # Command 6 is table slot six. A different valid case target loses the
        # command contract; an interior target additionally violates patch safety.
        image=PatchedImage(self.image,[(0x499aec+6*4,probe.struct.pack('<I',0x499803))])
        checks=self.report(image=image)['checks']
        self.assertFalse(checks['mov_jump_table'])
        self.assertFalse(checks['mov_command6_switch'])
        image=PatchedImage(self.image,[(0x499aec+6*4,probe.struct.pack('<I',0x49984a))])
        self.assertFalse(self.report(image=image)['checks']['mov_jump_table'])

    def test_publisher_mode3_switch_and_ret4_epilogue_are_exact(self):
        image=PatchedImage(self.image,[(0x425c80+3*4,probe.struct.pack('<I',0x425ba2))])
        checks=self.report(image=image)['checks']
        self.assertFalse(checks['publisher_jump_table'])
        self.assertFalse(checks['publisher_mode3_switch'])
        decoded=dict(self.decoded)
        decoded[probe.PUBLISHER]=[
            dataclasses.replace(i,raw=b'\xc3') if i.va==0x425c7d else i
            for i in decoded[probe.PUBLISHER]
        ]
        checks=self.report(decoded=decoded)['checks']
        self.assertFalse(checks['publisher_ret4_epilogue'])

    def test_shared_join_source_sets_are_exact(self):
        for bounds,source in ((probe.MAIN,0x403b10),(probe.MAIN,0x403d84),
                              (probe.PUBLISHER,0x425c1f),(probe.STREAM,0x498ee0)):
            with self.subTest(source=hex(source)):
                decoded={key:list(value) for key,value in self.decoded.items()}
                decoded[bounds]=[
                    dataclasses.replace(i,operands='0x400000') if i.va==source else i
                    for i in decoded[bounds]
                ]
                checks=self.report(decoded=decoded)['checks']
                self.assertFalse(checks['join_edges'])
                self.assertFalse(checks['shared_join_sources'])

        decoded={key:list(value) for key,value in self.decoded.items()}
        decoded[probe.MAIN].append(
            probe.common.Instruction(0x100,b'\xe9\0\0\0\0','jmp',hex(0x498f00)))
        self.assertFalse(self.report(decoded=decoded)['checks']['shared_join_sources'])

    def test_safe_join_addresses_skip_cleanup_instructions(self):
        addresses={site.va for site in probe.SITES}
        self.assertNotIn(0x425c76,addresses)
        self.assertIn(0x425c79,addresses)
        self.assertNotIn(0x498efd,addresses)
        self.assertIn(0x498f00,addresses)

    def test_context_corruptions_refused(self):
        for name,(va,raw) in probe.WITNESSES.items():
            with self.subTest(context=name):
                image=PatchedImage(self.image,[(va,bytes([bytes.fromhex(raw)[0]^1]))])
                self.assertFalse(self.report(image=image)['checks']['abi_context'])

    def test_conditional_acquisition_cannot_escape_endpoint(self):
        decoded=dict(self.decoded)
        decoded[probe.PUBLISHER]=[dataclasses.replace(i,operands='0x425c79') if i.va==0x425bd6 else i
                                  for i in decoded[probe.PUBLISHER]]
        checks=self.report(decoded=decoded)['checks']
        self.assertFalse(checks['acquisition_normal_endpoint'])
        self.assertFalse(checks['join_edges'])

    def test_missing_routine_refused(self):
        for bounds in self.decoded:
            with self.subTest(bounds=bounds):
                decoded=dict(self.decoded)
                del decoded[bounds]
                self.assertEqual(self.report(decoded=decoded)['result'],'FAIL')


if __name__=='__main__':unittest.main()
