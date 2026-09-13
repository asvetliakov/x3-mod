"""Independent admission/refusal checks for the 23 native marker contracts."""
import dataclasses
import unittest
import verify_game_phase_sites as probe
from verification.analysis.test_chase_lead_sites import PatchedImage


class SourceAndReplay(unittest.TestCase):
    def test_exact_production_order_and_spec_fields(self):
        self.assertEqual(len(probe.SITES),23)
        self.assertTrue(probe.source_checks(probe.SOURCE.read_text()))

    def test_ret_pop_and_relocation_fields_cannot_swap(self):
        text=probe.SOURCE.read_text()
        self.assertIn(',5,0,1}',text)
        self.assertFalse(probe.source_checks(text.replace(',5,0,1}',',5,1,0}',1)))

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
                        target=(arena+5+probe.struct.unpack_from('<i',code,1)[0])&0xffffffff
                        self.assertEqual(target,probe.TARGETS[site.va])
                    else:self.assertEqual(code,site.expected)


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(),'installed X3AP.exe unavailable')
class NativeSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image=probe.common.Image(probe.DEFAULT_EXE.read_bytes())
        cls.decoded=probe.decode()
        cls.source=probe.SOURCE.read_text()

    def report(self,image=None,decoded=None):
        return probe.inspect(image or self.image,decoded or self.decoded,self.source)

    def test_actual_executable_and_all_23_spans(self):
        report=probe.verify()
        self.assertEqual(report['result'],'PASS',report['checks'])
        self.assertEqual(len(report['sites']),23)

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
        self.assertEqual(self.report(decoded=decoded)['result'],'PASS')

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
                decoded[bounds]=[dataclasses.replace(i,operands='0x400000') if i.va==site.va else i
                                 for i in decoded[bounds]]
                row=next(row for row in self.report(decoded=decoded)['sites'] if row['name']==site.name)
                self.assertFalse(row['relative_contract'])
                self.assertFalse(row['ok'])

    def test_jump_table_interior_entry_refused(self):
        image=PatchedImage(self.image,[(0x425c80,probe.struct.pack('<I',0x425bad))])
        self.assertFalse(self.report(image=image)['checks']['publisher_jump_table'])

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
