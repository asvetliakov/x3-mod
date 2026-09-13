"""Host-only original PS2 emission augmentation proof; no game assets bundled.

The tiny arithmetic evaluator covers only the newly authored emission tail.
It does not emulate original partial precision, interpolation or GPU execution.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import inspect_motion_output_profiles as shader
from run_linear_emission import decode, sanitize

# SHA256, DWORD count, first declaration, pre-fade copy boundary, native MOV,
# affine and fade. These are derived original contracts, never shader words.
PROFILES = {
    '8360f422de08b5bd': ('ce26e0a8fca24b311324ca1c6d210cf6a7757af44c07525e51435dbaf4a36c7d',1108,1069,1100,1104,True,True),
    '9975b706e5a1c999': ('4330e9cb7260e6bea28962d8e8a1670d825b584c9d6ae51d10d22ea1c7418d2a',1108,1069,1100,1104,True,True),
    'ff2473e73a6bdfa1': ('70f619052110e03e9ce2e8b053b10e99f7d2102844f750535b7796c5206b8100',62,41,54,58,False,True),
    '8559522220507d5e': ('2f80daf9e908678a0e36a49d0fbaaba446e687e71cb654e85a1756c9cdd8c34d',1101,1069,1097,1097,True,False),
    '875e780adb131b16': ('c31728d81b42e58a809b401a174186217c9549c24a0809807ba8b097ddd445a1',55,41,51,51,False,False),
}
VERTICES = {
    'd5e1c75351ed3f04': (253,'a1db7ff10b8a6c81a830a095417133e37571a055674914df09136cc91f454114'),
    '32e75459998d0388': (253,'9ae32f514a79ca8675fe876731b72207f4449d4e34d89529114e9bacb62c3798'),
    '089091aab2d5eb13': (125,'2a3270292daf99a296801100e52c052967b4c80fee3b31216c8908dfb3233835'),
}
GAINS = (0, .25, 1, 4, 16)


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


def f32(value):
    try:
        return struct.unpack('<f', struct.pack('<f', value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def emission_math(words, items, native_end, rgb, fade):
    """Evaluate only authored DEF/tail operations; original RGB is an input.

    Ordered comparisons intentionally model the selected DX9 MAX/MIN operand
    semantics. Float32 rounding is an analytical host witness, not GPU parity.
    """
    registers = {(0,2): [f32(x) for x in rgb] + [math.nan], (1,0): [f32(fade),0.,0.,0.]}

    def source(token):
        values = registers[shader.register_of(token)]
        result = [values['xyzw'.index(lane)] for lane in shader.swizzle_of(token)]
        modifier = (token >> 24) & 15
        if modifier not in (0,1):
            raise AssertionError('unreviewed source modifier')
        return [-v for v in result] if modifier else result

    for item in items:
        opcode = item['opcode']
        if opcode == shader.DEF:
            registers[shader.register_of(item['words'][0])] = list(struct.unpack('<4f',struct.pack('<4I',*item['words'][1:])))
            continue
        if item['dword'] < native_end:
            continue
        destination, *tokens = item['words']
        values = [source(token) for token in tokens]
        if opcode == 1:
            computed = values[0]
        elif opcode == 5:
            computed = [a*b for a,b in zip(*values)]
        elif opcode == 11:
            computed = [a if a >= b else b for a,b in zip(*values)]
        elif opcode == 10:
            computed = [a if a <= b else b for a,b in zip(*values)]
        elif opcode == 32:
            computed = [math.pow(a,b) for a,b in zip(*values)]
        elif opcode == 88:
            computed = [b if a >= 0 else c for a,b,c in zip(*values)]
        else:
            raise AssertionError('unreviewed authored emission operation')
        target = registers.setdefault(shader.register_of(destination), [math.nan]*4)
        for component in shader.mask_of(destination):
            lane = 'xyzw'.index(component)
            target[lane] = f32(computed[lane])
    return registers[8,1]


class LinearEmissionTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY','/tmp/x3-shader-sweep/programs'))
        if not all((cls.originals / f'ps_{key}.bin').is_file() for key in PROFILES):
            raise unittest.SkipTest('local five-original PS2 corpus unavailable')
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-linear-emission-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        executable = cls.directory / 'structure'
        build = subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',
                                str(ROOT/'verification/probe/linear_emission_structure.cpp'),
                                '-o',str(executable)],capture_output=True,text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(executable),str(cls.originals),str(cls.directory)],capture_output=True,text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.driver = json.loads(run.stdout)

    def each(self):
        for key, profile in PROFILES.items():
            for g, gain in enumerate(GAINS):
                words, items, _ = shader.instructions((self.directory/f'ps_{key}-{g}.bin').read_bytes())
                yield key, profile, gain, words, items

    def test_all_five_originals_gains_aliases_and_failure_guards(self):
        self.assertEqual((self.driver['programs'],self.driver['pairs'],self.driver['variants']), (5,5,25))
        self.assertGreaterEqual(self.driver['checks'],243)

    def test_local_original_identities_and_untouched_vs2_contract(self):
        for key, profile in PROFILES.items():
            data = (self.originals/f'ps_{key}.bin').read_bytes()
            self.assertEqual((hashlib.sha256(data).hexdigest(),len(data)//4,shader.fnv1a64(data)), (profile[0],profile[1],key))
        for key, (count,digest) in VERTICES.items():
            data = (self.originals/f'vs_{key}.bin').read_bytes()
            self.assertEqual((len(data)//4,hashlib.sha256(data).hexdigest(),shader.fnv1a64(data)), (count,digest,key))
            self.assertEqual(struct.unpack_from('<I',data)[0],0xfffe0200)
            # The pure API produces pixel variants only; no new VS is emitted.
            self.assertFalse(list(self.directory.glob(f'vs_{key}*.bin')))

    def test_every_original_byte_comment_and_native_output_is_retained(self):
        for key, profile, _, words, items in self.each():
            _, count, declaration, copy, native, _, _ = profile
            original, _, _ = shader.instructions((self.originals/f'ps_{key}.bin').read_bytes())
            # Three precise insertion intervals partition the entire original,
            # including both preshader/CTAB comment payloads and final END.
            reconstructed = (words[:declaration] + words[declaration+12:copy+12] +
                             words[copy+15:count-1+15] + words[-1:])
            self.assertEqual(reconstructed, original)
            native_mov = next(i for i in items if i['dword'] == native+15)
            destination, sources = shader.split_operands(native_mov,2)
            self.assertEqual((native_mov['opcode'],destination['name'],destination['mask'],destination['modifiers']),
                             (1,'oC0','xyzw',['partial_precision']))
            self.assertEqual((sources[0]['name'],sources[0]['swizzle']), ('r0','xyzw'))

    def test_copy_precedes_original_fade_and_never_touches_raw_alpha(self):
        for _, profile, _, words, items in self.each():
            _, _, _, copy, _, _, fade = profile
            inserted = next(i for i in items if i['dword'] == copy+12)
            d,sources = shader.split_operands(inserted,2)
            self.assertEqual((inserted['opcode'],d['name'],d['mask'],d['modifiers']), (1,'r2','xyz',[]))
            self.assertEqual((sources[0]['name'],sources[0]['swizzle']), ('r0','xyzw'))
            following = next(i for i in items if i['dword'] == copy+15)
            self.assertEqual(following['opcode'], 5 if fade else 1)
            if fade:
                d,sources = shader.split_operands(following,2)
                self.assertEqual((d['name'],d['mask'],d['modifiers']), ('r0','xyz',['partial_precision']))
                self.assertEqual((sources[1]['name'],sources[1]['swizzle']), ('v0','xxxx'))

    def test_full_precision_output_cap_order_resources_and_weighted_ps2_limits(self):
        expected_arithmetic = {'8360f422de08b5bd':29,'9975b706e5a1c999':29,'ff2473e73a6bdfa1':24,
                               '8559522220507d5e':27,'875e780adb131b16':22}
        for key, profile, gain, _, items in self.each():
            _, count, _, _, _, affine, fade = profile
            definitions, arithmetic, texture, outputs = {}, 0, 0, []
            appended = [i for i in items if i['dword'] >= count-1+15]
            self.assertEqual([i['opcode'] for i in appended],
                             [11,10,11,32,32,32,88,11,10]+([5] if fade else [])+[5,11,10,1,1])
            for item in items:
                if item['opcode'] == shader.DEF:
                    index = shader.register_of(item['words'][0])[1]
                    definitions[index] = struct.unpack('<4f',struct.pack('<4I',*item['words'][1:]))
                    continue
                if item['opcode'] == shader.DCL:
                    continue
                d,sources = shader.split_operands(item,2)
                if item['opcode'] == 66:
                    texture += 1
                else:
                    self.assertIn(item['opcode'], (1,5,9,10,11,32,88))
                    arithmetic += 3 if item['opcode'] == 32 else 1
                for operand in [d]+sources:
                    if operand['register_type'] == 0:
                        self.assertLess(operand['register'],12)
                    if operand['register_type'] == 2:
                        self.assertLess(operand['register'],32)
                self.assertFalse(any(s['register_type'] == 8 for s in sources), 'color outputs are write-only')
                if d['register_type'] == 8:
                    outputs.append((item['opcode'],d['name'],d['mask'],d['modifiers']))
            for item in appended:
                d,_ = shader.split_operands(item,2)
                self.assertEqual(d['modifiers'],[])
                self.assertIn(d['name'], ('r2','r3','oC1'))
            self.assertEqual(outputs, [(1,'oC0','xyzw',['partial_precision']),(1,'oC1','xyzw',[])])
            self.assertEqual((arithmetic,texture), (expected_arithmetic[key],1))
            self.assertLessEqual(arithmetic,64)
            self.assertLessEqual(texture,32)
            self.assertEqual(set(definitions), {3,30,31} if affine else {30,31})
            self.assertEqual(definitions[30], (f32(2.2),0.,65504.,f32(1e-10)))
            self.assertEqual(definitions[31], (gain,0.,0.,0.))
            # All channels are initialized before the one legal full MOV oC1.
            d,sources = shader.split_operands(appended[-2],2)
            self.assertEqual((d['name'],d['mask'],sources[0]['name'],sources[0]['swizzle']), ('r2','w','c30','yyyy'))
            _,sources = shader.split_operands(appended[-1],2)
            self.assertEqual((sources[0]['name'],sources[0]['swizzle']), ('r2','xyzw'))

    def test_high_input_quarter_gain_distinguishes_decoded_cap_before_fade(self):
        for key, profile, gain, words, items in self.each():
            if gain != .25:
                continue
            fade = .5 if profile[-1] else 1.
            result = emission_math(words,items,profile[1]-1+15,[256.,128.,64.],fade)
            expected = [sanitize(decode(x)*fade*gain) for x in (256.,128.,64.)]
            for got,want in zip(result[:3],expected):
                self.assertAlmostEqual(got,want,delta=max(1e-6,abs(want)*2e-6))
            self.assertEqual(result[0],8188. if profile[-1] else 16376.)
            wrong = sanitize(256.**2.2*fade*gain)
            self.assertGreater(abs(result[0]-wrong),1000.,key)
            self.assertEqual(struct.pack('<f',result[3]),b'\0\0\0\0')

    def test_new_arithmetic_model_retains_linear_fade_gain_and_finite_endpoints(self):
        cases = ([.25,.5,1.], [0.,-0.,-1.], [math.nan,-math.inf,math.inf], [1e-12,256.,65504.])
        for _, profile, gain, words, items in self.each():
            fade = .5 if profile[-1] else 1.
            for rgb in cases:
                result = emission_math(words,items,profile[1]-1+15,rgb,fade)
                for got,code in zip(result[:3],rgb):
                    wanted = sanitize(decode(code)*fade*gain)
                    self.assertTrue(math.isfinite(got) and 0 <= got <= 65504)
                    self.assertAlmostEqual(got,wanted,delta=max(1e-7,abs(wanted)*2e-6))
                    if wanted == 0:
                        self.assertEqual(struct.pack('<f',got),b'\0\0\0\0')
                self.assertEqual(struct.pack('<f',result[3]),b'\0\0\0\0')


if __name__ == '__main__':
    unittest.main()
