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
PREVIOUS_PIXELS = set(PROFILES)
PROFILES.update({
    '39f3b4d5b6a5aaed': ('24bc4ff303d1a1d49a9d35de3b49ecddb2767a155c26ffe91e5f88b54a82eafc',62,41,54,58,False,True),
    '47e15e20d63b0e93': ('2f5e160e96ceb661b1ec43424f1d9ebd3091b191b85395b7ab06d4651b55e561',62,41,54,58,False,True),
    '846c5c1a549f9491': ('4e9a03b5af938ffe0a1c69fa88cfe0768a39a85247800aea1bb759a5ce7bf698',1108,1069,1100,1104,True,True),
    'c6dacb8f74b65c97': ('59b16b900bd09f05aa464634cca5019c0bd7b2deaff729afadc50d20ec5fdc2d',1108,1069,1100,1104,True,True),
    'f0c91793a75e1203': ('bf30c26d78fcf6f781edd5d6dfdfb0ecf973b924d3ae13bd8e4eea593fbef038',1108,1069,1100,1104,True,True),
})
PS2X = {'47e15e20d63b0e93','c6dacb8f74b65c97','f0c91793a75e1203'}
# Complete additional group, including INSTANCE uses of already supported PS.
NEW_PAIR_OCCURRENCES = {
    ('5b7a3ccd9e7df00a','9975b706e5a1c999'):16,
    ('5b7a3ccd9e7df00a','ff2473e73a6bdfa1'):16,
    ('6435a84d8ac5908e','39f3b4d5b6a5aaed'):8,
    ('6435a84d8ac5908e','47e15e20d63b0e93'):4,
    ('6435a84d8ac5908e','846c5c1a549f9491'):8,
    ('6435a84d8ac5908e','c6dacb8f74b65c97'):4,
    ('89193868c61c3846','8360f422de08b5bd'):112,
    ('89193868c61c3846','f0c91793a75e1203'):16,
    ('a520be365951c9dc','8559522220507d5e'):4,
    ('a520be365951c9dc','875e780adb131b16'):4,
    ('cfb2c31707d545bc','39f3b4d5b6a5aaed'):8,
    ('cfb2c31707d545bc','47e15e20d63b0e93'):4,
    ('cfb2c31707d545bc','846c5c1a549f9491'):8,
    ('cfb2c31707d545bc','c6dacb8f74b65c97'):4,
    ('d5e1c75351ed3f04','f0c91793a75e1203'):16,
}
PREVIOUS_PAIRS = {('d5e1c75351ed3f04','8360f422de08b5bd'),
                  ('32e75459998d0388','9975b706e5a1c999'),('32e75459998d0388','ff2473e73a6bdfa1'),
                  ('089091aab2d5eb13','8559522220507d5e'),('089091aab2d5eb13','875e780adb131b16')}

VERTICES = {
    'd5e1c75351ed3f04': (253,'a1db7ff10b8a6c81a830a095417133e37571a055674914df09136cc91f454114'),
    '32e75459998d0388': (253,'9ae32f514a79ca8675fe876731b72207f4449d4e34d89529114e9bacb62c3798'),
    '089091aab2d5eb13': (125,'2a3270292daf99a296801100e52c052967b4c80fee3b31216c8908dfb3233835'),
}
VERTICES.update({
    '5b7a3ccd9e7df00a':(221,'ac01616c5abfc5db1a9d8631de9149d99d242ed7e066613f203cfd1b7798ecd9'),
    '6435a84d8ac5908e':(221,'ad5209f409b152f1aa211e41d699ba53f439fcb1fa9bb1f98737bf9a69f1d6e9'),
    '89193868c61c3846':(221,'83e106b5cef3e814d095909adb55816f9fdac98723ee11bea209f2772557c8eb'),
    'a520be365951c9dc':(94,'39f272c33da47ec1fd75ce483949dfc7450d9fbd768669228fb429dfc5a38dac'),
    'cfb2c31707d545bc':(253,'cd60b84218aadc678b5d6c0cd9b0d151f8727d3c84f93c23e1b69c0ca2c11a99'),
})
GAINS = (0, .25, 1, 4, 16)


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


def f32(value):
    try:
        return struct.unpack('<f', struct.pack('<f', value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def emission_math(words, items, native_end, rgb, fade, output=1, native_alpha=0.):
    """Evaluate only authored DEF/tail operations; original RGB is an input.

    Ordered comparisons intentionally model the selected DX9 MAX/MIN operand
    semantics. Float32 rounding is an analytical host witness, not GPU parity.
    """
    registers = {(0,0): [f32(x) for x in rgb] + [f32(native_alpha)], (0,2): [f32(x) for x in rgb] + [math.nan], (1,0): [f32(fade),0.,0.,0.]}

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
    return registers[8,output]


class LinearEmissionTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY','/tmp/x3-shader-sweep/programs'))
        if not all((cls.originals / f'ps_{key}.bin').is_file() for key in PROFILES):
            raise unittest.SkipTest('local ten-original PS2/PS2.x corpus unavailable')
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
                for coverage in (False, True):
                    suffix = '-coverage' if coverage else ''
                    words, items, _ = shader.instructions((self.directory/f'ps_{key}-{g}{suffix}.bin').read_bytes())
                    yield key, profile, gain, coverage, words, items

    def test_all_ten_originals_gains_aliases_and_failure_guards(self):
        self.assertEqual((self.driver['programs'],self.driver['pairs'],self.driver['variants']), (10,20,100))
        self.assertGreaterEqual(self.driver['checks'],500)

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

    def test_complete_sm2_archive_pair_quality_toggle_and_alias_scope(self):
        report=json.loads((ROOT/'verification/results/motion-output-profiles.json').read_text())
        aliases={'effects','effects2s','effects_0000','effects_0001','engine','engine2s','engine_0000','engine_0001'}
        rows={(r['vs'],r['ps']):r for r in report['sm2_pairs'] if aliases.intersection(r['effects']['basenames'])}
        self.assertEqual(set(rows),PREVIOUS_PAIRS|set(NEW_PAIR_OCCURRENCES))
        self.assertEqual(sum(rows[p]['effects']['pass_occurrences'] for p in NEW_PAIR_OCCURRENCES),232)
        self.assertEqual(sum(rows[p]['ps_model']=='2_1' for p in NEW_PAIR_OCCURRENCES),6)
        self.assertEqual(sum(rows[p]['effects']['techniques']==['INSTANCE'] for p in NEW_PAIR_OCCURRENCES),10)
        for pair,occurrences in NEW_PAIR_OCCURRENCES.items():
            row=rows[pair];v,p=pair;e=row['effects'];self.assertEqual(e['pass_occurrences'],occurrences)
            self.assertEqual(row['vs_model'],'2_0');self.assertEqual(row['ps_model'],'2_1' if p in PS2X else '2_0')
            self.assertEqual(e['pass_names'],['P0'])
            expected_aliases=({'engine_0000','engine_0001'} if v=='5b7a3ccd9e7df00a' else
                              {'effects','effects2s','engine','engine2s'} if p=='8360f422de08b5bd' else
                              {'effects','effects2s'} if p=='f0c91793a75e1203' else {'effects_0000','effects_0001'})
            self.assertEqual(set(e['basenames']),expected_aliases)
            profiles=({'2_b'} if p in PS2X else {'3_0'} if v=='a520be365951c9dc' else
                      {'2_0','2_a','2_b','3_0'} if v in {'5b7a3ccd9e7df00a','89193868c61c3846'} else {'2_0','2_a'})
            self.assertEqual(set(e['profile_directories']),profiles)
            toggles=({'(base)','hue_lights_off','hueshift_off','v_lights_off'} if p in {'8360f422de08b5bd','f0c91793a75e1203'} else
                     {'(base)','v_lights_off'} if PROFILES[p][-2] else {'hue_lights_off','hueshift_off'})
            self.assertEqual(set(e['toggle_directories']),toggles)

    def test_native_models_and_untouched_instance_uv_and_fade_outputs(self):
        for key,_,_,_,words,_ in self.each():
            self.assertEqual(words[0],0xffff0201 if key in PS2X else 0xffff0200)
        for key in ('5b7a3ccd9e7df00a','6435a84d8ac5908e','89193868c61c3846','a520be365951c9dc'):
            _,items,_=shader.instructions((self.originals/f'vs_{key}.bin').read_bytes())
            writes=[i for i in items if i['opcode'] not in (shader.DEF,shader.DCL,40,42,43)]
            outputs=[]
            for i in writes:
                d,src=shader.split_operands(i,2)
                if d and d['register_type'] in (4,5,6):outputs.append((i,d,src))
            uv=[(i,d,src) for i,d,src in outputs if d['register_type']==6]
            self.assertEqual(len(uv),1);i,d,src=uv[0]
            self.assertEqual((i['opcode'],d['register'],d['mask'],src[0]['name'],src[0]['swizzle']),(1,0,'xy','v1','xyzw'))
            color=[(i,d,src) for i,d,src in outputs if d['register_type']==5]
            self.assertEqual(len(color),0 if key=='a520be365951c9dc' else 1)
            if color:
                i,d,src=color[0];self.assertEqual((d['register'],d['mask'],src[0]['swizzle']),(0,'xyz','xyyw'))

    def test_all_50_prior_two_and_three_output_variants_remain_exact(self):
        digest=hashlib.sha256();paths=sorted(p for p in self.directory.glob('ps_*.bin') if p.name[3:19] in PREVIOUS_PIXELS)
        self.assertEqual(len(paths),50)
        for path in paths:digest.update(path.name.encode()+b'\0');digest.update(path.read_bytes())
        self.assertEqual(digest.hexdigest(),'0cf24c46942fb5f670c79cca390e583335e8c17f998e602a8d3b907b9f036479')

    def test_every_original_byte_comment_and_native_output_is_retained(self):
        for key, profile, _, _, words, items in self.each():
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
        for _, profile, _, _, words, items in self.each():
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
                               '8559522220507d5e':27,'875e780adb131b16':22,
                               '39f3b4d5b6a5aaed':24,'47e15e20d63b0e93':24,'846c5c1a549f9491':29,
                               'c6dacb8f74b65c97':29,'f0c91793a75e1203':29}
        for key, profile, gain, coverage, _, items in self.each():
            _, count, _, _, _, affine, fade = profile
            definitions, arithmetic, texture, outputs, temporaries = {}, 0, 0, [], set()
            appended = [i for i in items if i['dword'] >= count-1+15]
            self.assertEqual([i['opcode'] for i in appended],
                             [11,10,11,32,32,32,88,11,10]+([5] if fade else [])+[5,11,10,1,1]+([1,1] if coverage else []))
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
                        temporaries.add(operand['register'])
                        self.assertLess(operand['register'],12)
                    if operand['register_type'] == 2:
                        self.assertLess(operand['register'],32)
                self.assertFalse(any(s['register_type'] == 8 for s in sources), 'color outputs are write-only')
                if d['register_type'] == 8:
                    outputs.append((item['opcode'],d['name'],d['mask'],d['modifiers']))
            for item in appended:
                d,_ = shader.split_operands(item,2)
                self.assertEqual(d['modifiers'],[])
                self.assertIn(d['name'], ('r2','r3','oC1','oC2'))
            self.assertEqual(outputs, [(1,'oC0','xyzw',['partial_precision']),(1,'oC1','xyzw',[])]+
                             ([(1,'oC2','xyzw',[])] if coverage else []))
            self.assertEqual((arithmetic,texture), (expected_arithmetic[key]+2*coverage,1))
            self.assertEqual(temporaries, {0,1,2,3} if affine else {0,2,3})
            self.assertLessEqual(arithmetic,64)
            self.assertLessEqual(texture,32)
            self.assertEqual(set(definitions), {3,30,31} if affine else {30,31})
            self.assertEqual(definitions[30], (f32(2.2),0.,65504.,f32(1e-10)))
            self.assertEqual(definitions[31], (gain,float(coverage),0.,0.))
            if coverage:
                d,sources = shader.split_operands(appended[-2],2)
                self.assertEqual((d['name'],d['mask'],sources[0]['name'],sources[0]['swizzle']), ('r3','xyzw','c31','yyyy'))
                _,sources = shader.split_operands(appended[-1],2)
                self.assertEqual((sources[0]['name'],sources[0]['swizzle']), ('r3','xyzw'))
                appended = appended[:-2]
            # All channels are initialized before the one legal full MOV oC1.
            d,sources = shader.split_operands(appended[-2],2)
            self.assertEqual((d['name'],d['mask'],sources[0]['name'],sources[0]['swizzle']), ('r2','w','c30','yyyy'))
            _,sources = shader.split_operands(appended[-1],2)
            self.assertEqual((sources[0]['name'],sources[0]['swizzle']), ('r2','xyzw'))

    def test_all_25_accepted_two_output_variants_remain_byte_exact(self):
        # Captured from accepted two-output source 3c72347 before coverage edits.
        # Canonical digest: sorted basename + NUL + complete shader bytes.
        digest = hashlib.sha256()
        for path in sorted(p for p in self.directory.glob('ps_*.bin') if '-coverage' not in p.name and p.name[3:19] in PREVIOUS_PIXELS):
            digest.update(path.name.encode() + b'\0')
            digest.update(path.read_bytes())
        self.assertEqual(digest.hexdigest(), '2b637e983bae9ef42a59c32851249f2b42f65449edc8911b4fe2ce26e38f481b')
        self.assertEqual(self.driver['max_arithmetic'], [29,31])

    def test_coverage_only_changes_unused_constant_lane_and_adds_final_two_moves(self):
        for key, profile, gain, coverage, words, items in self.each():
            if not coverage:
                continue
            old, old_items, _ = shader.instructions((self.directory/f'ps_{key}-{GAINS.index(gain)}.bin').read_bytes())
            # Same exact instructions and opaque comments through oC1; only the
            # previously unread c31.y literal changes before the coverage tail.
            restored = list(words[:-7]) + list(words[-1:])
            constant = next(i for i in items if i['opcode']==shader.DEF and shader.register_of(i['words'][0])==(2,31))
            restored[constant['dword']+3] = 0
            self.assertEqual(tuple(restored), old)
            for item in old_items:
                if item['opcode'] in (shader.DEF,shader.DCL):
                    continue
                _, sources = shader.split_operands(item,2)
                for source in sources:
                    if source['name']=='c31':
                        self.assertEqual(source['swizzle'],'xxxx')

    def test_constant_positive_coverage_including_zero_rgb_fade_alpha_and_gain(self):
        for _, profile, gain, coverage, words, items in self.each():
            if not coverage:
                continue
            for rgb in ([0.,0.,0.],[-0.,-1.,math.nan],[math.inf,-math.inf,65504.],[.25,.5,1.]):
                for fade in (0.,.5,1.):
                    for alpha in (0.,.5,1.):
                        result = emission_math(words,items,profile[1]-1+15,rgb,fade,output=2,native_alpha=alpha)
                        self.assertEqual(result,[1.,1.,1.,1.])

    def test_high_input_quarter_gain_distinguishes_decoded_cap_before_fade(self):
        for key, profile, gain, _, words, items in self.each():
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
        for _, profile, gain, _, words, items in self.each():
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
