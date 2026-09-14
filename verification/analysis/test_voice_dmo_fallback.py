"""Host checks for the voice DMO fallback hook (src/proxy/voice_dmo_fallback.cpp).

Pins the GUID constants and the failure condition the hook keys on, the
environment gate and its launcher delivery, the SiteSpec against the verifier's
independent ledger, and (when the installed EXE is present) the read-only
instruction/ABI qualification of the site. No Wine, no game.
"""
import json
import unittest
from pathlib import Path

import verify_voice_dmo_site as probe
from verification.analysis.test_voice_decoder_launch import VoiceDecoderLaunchOption

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'src/proxy/voice_dmo_fallback.cpp').read_text()
GUID_RE = '{0x%s,0x%s,0x%s,{%s}}'


def guid_pattern(text):
    """dmodshow-style GUID text -> the initializer the source must contain."""
    a, b, c, d, e = text.split('-')
    tail = ','.join('0x' + (d + e)[i:i + 2] for i in range(0, 16, 2))
    return GUID_RE % (a, b, c, tail)


class Constants(unittest.TestCase):
    def test_guids_match_the_documented_values(self):
        for name, text in (('kIID_IDMOWrapperFilter', '52d6f586-9f0f-4824-8fc8-e32ca04930c2'),
                           ('kCLSID_CWMADecMediaObject', '2eeb4adf-4578-4d10-bca7-bb955f56320a'),
                           ('kDMOCATEGORY_AUDIO_DECODER', '57f2db8b-e6bb-4513-9d43-dcd2a6593125')):
            self.assertIn('constexpr GUID ' + name + '=' + guid_pattern(text) + ';', SOURCE)
        self.assertNotIn('874131cb', SOURCE.lower().replace('874131cb-4ecc-443b-8948-746b89595d20', ''), 'the speech CLSID is never retried')

    def test_condition_slot_and_site_spec(self):
        self.assertIn('kClassNotRegistered=0x80040154', SOURCE)
        self.assertIn('kDecoderSlot=0x9c', SOURCE)
        self.assertIn('if(regs[7]!=kClassNotRegistered)return;', SOURCE)
        self.assertIn('regs[7]=r.init_hr;', SOURCE)
        specs = probe.common.parse_source_specs(SOURCE)
        self.assertEqual(specs, [dict(name=probe.SITE.name, va=probe.SITE.va, bytes=probe.SITE.expected,
                                      length=len(probe.SITE.expected), rel32_offset=0, rel32_target=0)])
        self.assertEqual(probe.SITE.va, 0x4cfd46)
        self.assertEqual(probe.SITE.expected, bytes.fromhex('8bf081fe0e000780'))

    def test_env_gate_and_install_guards(self):
        self.assertIn('L"X3M_VOICE_DMO_FALLBACK"', SOURCE)
        for guard in ('install_window_open()', 'executable_verified()', 'verify_bytes(kSite.address', 'engine_patch::restore(patch)'):
            self.assertIn(guard, SOURCE)
        self.assertIn('x3m::PreserveCpuState cpu;', SOURCE)
        self.assertIn('force_align_arg_pointer', SOURCE)


class LauncherGate(VoiceDecoderLaunchOption):
    def test_the_gate_travels_only_with_the_decoder(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = self.plugin_tree(directory)
            self.assertNotIn('X3M_VOICE_DMO_FALLBACK', json.loads(self.launch(directory)[1])['env'])
            code, output, error = self.launch(directory, '--voice-decoder', str(root))
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_VOICE_DMO_FALLBACK'], '1')


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed X3AP.exe unavailable')
class InstalledSite(unittest.TestCase):
    def test_site_qualifies(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['checks'])
        self.assertEqual([i['mnemonic'] for i in report['site']['instructions']], ['mov', 'cmp'])


if __name__ == '__main__':
    unittest.main()
