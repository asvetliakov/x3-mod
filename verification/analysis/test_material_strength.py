"""Host oracle of the merged-LOD overlay material strength, proxy side (docs/architecture/overlay-material-strength.md
"option B", sections 4-5 and 7).

Twin transformer (linear_material_strength_twin_pixel_variant, verification/probe/material_strength_twin_structure.cpp
over the local shader corpus): for every reviewed hull original and each of the 14 original-shading forms the proxy
can bind (motion, fill, gained, widened, share, share gained, share gained widened, the engine-light twin of each, and
the sun-lane motion form smotion that the fade owner binds) the twin is the parent byte for byte plus
exactly two insertions: `add rT.w, rS.w, rS.w` right after the specular texld (stage 1 DEFAULT / 2 BUMPMAP, from the
program's CTAB SpecularTexSampler) and the join rewrite at the exposure seed's MAD (product: `mul rT.lanes, a, b ; mad
d, rT, rT.w, c`; addend: `mul rT.lanes, c, rT.w ; mad d, a, b, rT`), rT one above the parent's highest temporary. The
texel-lane survey (verification/results/outpost-lod-material/texel_lanes.py rules) must find lane .x alone read of
the specular texel for every program with a twin, and the eight asteroid / glass programs (no specular stage) get
none. Byte-exact oracle on 5f82ecac (XT BUMPMAP, product join). The native validator run over the same twins is in
verification/results/overlay-material-strength/native_validate_twins.json (SysWOW64 d3d9 through WSL interop; rerun
with native_validate_twins.sh) and is checked here when that interop is available.

Hook side: the DDS mark parser and the pointer table (src/proxy/lod_texture_mark.h) through
verification/probe/lod_texture_mark_check.cpp. No game assets bundled; the corpus is the user's local extraction.
"""
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/hull-widen-native-validator'))
from shader_constants import parse_ctab
import sm3_rules

SEEDS = {int(h, 16): (int(at), int(lanes), prod == 'true') for h, at, lanes, prod in re.findall(
    r'\{0x([0-9a-f]{16})ull,(\d+),(\d+),(true|false)\}',
    (ROOT / 'src/renderer/material_exposure_profiles_inc.h').read_text())}
ADD, MAD, MUL, TEXLD = 2, 4, 5, 66
KINDS = ('motion', 'fill', 'gain', 'widen', 'share', 'sgain', 'swiden', 'emotion', 'efill', 'egain', 'ewiden', 'eshare', 'esgain', 'eswiden',
         'smotion')
NO_SPECULAR = {'ps_517540ae6d5e5410', 'ps_550c2a4d4d3ed70f', 'ps_7a0c3388065bb08d', 'ps_d44db87778a43b61',
               'ps_9d49f288800f898d', 'ps_a66fb1981ba755b2', 'ps_ebc9b2b3f1564e9a', 'ps_f31c9e2701c8eee4'}
MOTION_ONLY = {'ps_6aaaa2cb27e92cc8', 'ps_c002d0727537c5de'}  # a motion row, no reviewed original-shading form
# texel_lanes.py's read rules (per-component, xyz, dp2add, replicate, texture, else all four)
PER_COMPONENT = {1, 2, 3, 4, 5, 18, 23, 11, 10, 12, 13, 36, 16, 17}
XYZ = {8, 36, 33}
REPL = {6, 7, 14, 15, 32, 37}
TEX = {66, 93, 95}
IF_OPS = {40, 41}
ELSE, ENDIF = 42, 43


def words(path):
    data = Path(path).read_bytes()
    return list(struct.unpack(f'<{len(data) // 4}I', data))


def rtype(t):
    return ((t >> 28) & 7) | ((t >> 8) & 0x18)


def reads(op, dst, srcs):
    out = []
    mask = (dst >> 16) & 0xf if dst is not None else 0xf
    for i, s in enumerate(srcs):
        swz = [(s >> (16 + 2 * k)) & 3 for k in range(4)]
        if op in PER_COMPONENT or op in (25, 26):
            lanes = {swz[k] for k in range(4) if mask >> k & 1}
        elif op in XYZ:
            lanes = {swz[0], swz[1], swz[2]}
        elif op == 90:
            lanes = {swz[0], swz[1]} if i < 2 else {swz[0]}
        elif op in REPL:
            lanes = {swz[0]}
        elif op in TEX:
            lanes = {swz[0], swz[1], swz[2], swz[3]} if i == 0 else set()
        else:
            lanes = {swz[0], swz[1], swz[2], swz[3]}
        out.append((rtype(s), s & 0x7ff, lanes))
    return out


def specular_lanes(code, stage):
    """(texld index, destination register, lanes read) of the specular texld of a ps_3_0 program, texel_lanes.py's walk."""
    ins = [(at, op, p) for at, op, p in sm3_rules.parse(code) if op not in (81, 31, 'end')]
    body = list(enumerate(ins))
    depth, depth_at = 0, []
    for idx, (at, op, p) in body:
        if op in (ENDIF, ELSE):
            depth -= 1
        depth_at.append(depth)
        if op in IF_OPS or op == ELSE:
            depth += 1
    for idx, (at, op, p) in body:
        if op != TEXLD or rtype(p[0]) != 0 or (p[2] & 0x7ff) != stage or rtype(p[2]) != 10:
            continue
        dreg = p[0] & 0x7ff
        live, used = {0, 1, 2, 3}, set()
        for jdx in range(idx + 1, len(body)):
            _, (at2, op2, p2) = body[jdx]
            has_dst = op2 not in sm3_rules.NO_DST and p2
            d2 = p2[0] if has_dst else None
            for t, n, lanes in reads(op2, d2, p2[1:] if has_dst else p2):
                if t == 0 and n == dreg:
                    used |= lanes & live
            if has_dst and rtype(d2) == 0 and (d2 & 0x7ff) == dreg and depth_at[jdx] <= depth_at[idx]:
                live -= {k for k in range(4) if (d2 >> 16) & (1 << k)}
            if not live:
                break
        return idx, dreg, used
    return None


def instruction_offsets(code):
    """DWORD offset of every instruction token (comments skipped, END included)."""
    out, at = [], 1
    while at < len(code):
        t = code[at]
        if t == 0xffff:
            out.append(at)
            break
        n = (t >> 16) & 0x7fff if (t & 0xffff) == 0xfffe else (t >> 24) & 15
        if (t & 0xffff) != 0xfffe:
            out.append(at)
        at += n + 1
    return out


def reg(kind, number, extra=0):
    return 0x80000000 | ((kind & 7) << 28) | ((kind & 24) << 8) | number | extra


def expected_twin(parent, original_hash, stage):
    """The twin's words from the parent's alone: texld on `stage`, the seed's MAD matched byte for byte, rT = highest + 1."""
    at_seed, lanes, product = SEEDS[original_hash]
    offsets = instruction_offsets(parent)
    highest = 0
    for at in offsets:
        t = parent[at]
        if t == 0xffff:
            break
        n = (t >> 24) & 15
        op = t & 0xffff
        if op in (31, 81):
            continue
        for q in range(1, n + 1):
            if rtype(parent[at + q]) == 0:
                highest = max(highest, (parent[at + q] & 0x7ff) + 1)
    texel = [at for at in offsets if parent[at] & 0xffff == TEXLD and rtype(parent[at + 3]) == 10 and parent[at + 3] & 0x7ff == stage]
    assert len(texel) == 1, texel
    (texel_at,) = texel
    rs = parent[texel_at + 1] & 0x7ff
    rt = highest
    return texel_at, rs, rt, at_seed, lanes, product


def build_twin(parent, original, original_hash, stage):
    texel_at, rs, rt, at_seed, lanes, product = expected_twin(parent, original_hash, stage)
    join_words = original[at_seed:at_seed + 5]
    joins = [at for at in instruction_offsets(parent) if parent[at:at + 5] == join_words]
    assert len(joins) == 1, joins
    (join_at,) = joins
    dst, a, b, c = parent[join_at + 1], parent[join_at + 2], parent[join_at + 3], parent[join_at + 4]
    lane_w = lambda r: reg(0, r, 0xff << 16)                   # .wwww
    ident = lambda r: reg(0, r, 0xe4 << 16)
    add = [(3 << 24) | ADD, reg(0, rt, 8 << 16), lane_w(rs), lane_w(rs)]
    if product:
        join = [(3 << 24) | MUL, reg(0, rt, lanes << 16), a, b, (4 << 24) | MAD, dst, ident(rt), lane_w(rt), c]
    else:
        join = [(3 << 24) | MUL, reg(0, rt, lanes << 16), c, lane_w(rt), (4 << 24) | MAD, dst, a, b, ident(rt)]
    out = []
    at = 0
    while at < len(parent):
        if at == join_at:
            out += join
            at += 5
            continue
        t = parent[at]
        n = 0 if at == 0 or t == 0xffff else ((t >> 16) & 0x7fff if (t & 0xffff) == 0xfffe else (t >> 24) & 15)
        out += parent[at:at + n + 1]
        if at == texel_at:
            out += add
        at += n + 1
    return out, dict(texel_at=texel_at, join_at=join_at, rs=rs, rt=rt, product=product)


def fnv1a64(data):
    value = 0xcbf29ce484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return value


class StrengthTwinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.corpus = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        if not (cls.corpus / 'ps_5f82ecacd39529cd.bin').is_file():
            reason = f'local shader corpus unavailable under {cls.corpus} (X3M_SHADER_PROGRAM_DIRECTORY)'
            if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
                raise AssertionError(reason)
            raise unittest.SkipTest(reason)
        compiler = shutil.which('g++') or shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-material-strength-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        executable = cls.directory / 'twins'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/material_strength_twin_structure.cpp'),
                                str(ROOT / 'src/renderer/linear_material.cpp'), str(ROOT / 'src/renderer/material_motion.cpp'),
                                '-o', str(executable)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        out = cls.directory / 'out'
        out.mkdir()
        run = subprocess.run([str(executable), str(cls.corpus), str(out)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.report = json.loads(run.stdout)
        cls.out = out
        cls.rows = cls.report['rows']

    def test_every_reviewed_hull_program_gets_a_twin_in_every_form(self):
        by_program = {}
        for r in self.rows:
            by_program.setdefault(r['name'], {})[r['kind']] = r
        # Motion-table programs that are not reviewed hull originals have the sun-lane motion form alone; they have no
        # specular stage, so the proxy keeps no words for them and they are refused (unsupported) like the eight below.
        motion_only = {n for n, kinds in by_program.items() if set(kinds) == {'smotion'}}
        self.assertEqual(motion_only, MOTION_ONLY)
        for n in motion_only:
            self.assertEqual((by_program[n]['smotion']['refusal'], by_program[n]['smotion']['stage']), ('unsupported', 0))
            del by_program[n]
        twinned = {n for n, kinds in by_program.items() if all(k['status'] == 0 for k in kinds.values())}
        refused = {n for n, kinds in by_program.items() if any(k['status'] != 0 for k in kinds.values())}
        self.assertEqual(len(twinned), 100, sorted(by_program)[:5])
        self.assertEqual(refused, NO_SPECULAR)
        for n in refused:
            self.assertTrue(all(k['refusal'] == 'unsupported' and k['stage'] == 0 for k in by_program[n].values()), n)
        self.assertEqual({r['kind'] for r in self.rows if r['status'] == 0}, set(KINDS))
        self.assertTrue(all(r.get('twin_slots', 0) <= 32768 for r in self.rows))
        self.assertTrue(all(r['twin_words'] == r['parent_words'] + 8 for r in self.rows if r['status'] == 0))

    def test_specular_texel_lanes_and_stage_from_the_ctab(self):
        """Every twinned program reads lane .x alone of the specular texel (the baker owns .w), and the stage the
        transformer used is the CTAB's SpecularTexSampler register (1 DEFAULT, 2 BUMPMAP)."""
        seen = set()
        for r in self.rows:
            if r['status'] != 0 or r['name'] in seen:
                continue
            seen.add(r['name'])
            code = (self.corpus / f"{r['name']}.bin").read_bytes()
            samplers = {c['register']: c['name'] for c in parse_ctab(code) if c.get('register_set') == 3}
            self.assertEqual(samplers.get(r['stage']), 'SpecularTexSampler', (r['name'], r['stage'], samplers))
            idx, dreg, used = specular_lanes(words(self.corpus / f"{r['name']}.bin"), r['stage'])
            self.assertEqual(used, {0}, (r['name'], used))
            self.assertEqual(r['lanes_read'], 1)
        self.assertEqual(len(seen), 100)

    def test_twin_is_the_parent_plus_exactly_the_two_insertions(self):
        for r in self.rows:
            if r['status'] != 0:
                continue
            parent = words(self.out / f"{r['name']}-{r['kind']}-parent.bin")
            twin = words(self.out / f"{r['name']}-{r['kind']}-twin.bin")
            original = words(self.corpus / f"{r['name']}.bin")
            expected, info = build_twin(parent, original, int(r['name'][3:], 16), r['stage'])
            self.assertEqual(twin, expected, (r['name'], r['kind']))
            self.assertEqual((r['texel_at'], r['join_at'], r['source_register'], r['scratch_register'], r['product']),
                             (info['texel_at'], info['join_at'], info['rs'], info['rt'], info['product']), r['name'])
            # the join's register never appears elsewhere in the parent, and no instruction of the twin reads two c#
            self.assertFalse(any(rtype(w) == 0 and (w & 0x7ff) == info['rt'] for at in instruction_offsets(parent)
                                 for w in parent[at + 1:at + 1 + ((parent[at] >> 24) & 15)]
                                 if parent[at] != 0xffff and (parent[at] & 0xffff) not in (31, 81)))
            for at in instruction_offsets(twin):
                t = twin[at]
                if t == 0xffff or (t & 0xffff) in (31, 81):
                    continue
                constants = {w & 0x7ff for w in twin[at + 2:at + 1 + ((t >> 24) & 15)] if rtype(w) == 2}
                self.assertLessEqual(len(constants), 1, (r['name'], at))

    def test_sun_motion_twin_is_the_motion_twin_with_the_invalid_share(self):
        """Kind 7 (smotion, motion_output_material_strength_inc.h): the sun-lane motion form is the motion variant with
        a leading `def c221, -1, 0, 0, 0` and a trailing `mov oC2.g, c221.x` (material_motion_invalid_sun_share), so its
        twin is the motion twin with the same two additions, word for word, for every program that has the form."""
        smotion = [r for r in self.rows if r['kind'] == 'smotion' and r['name'] not in NO_SPECULAR | MOTION_ONLY]
        self.assertEqual(len(smotion), 100)
        for r in smotion:
            self.assertEqual(r['status'], 0, r['name'])
            motion = words(self.out / f"{r['name']}-motion-twin.bin")
            twin = words(self.out / f"{r['name']}-smotion-twin.bin")
            self.assertEqual(twin, [motion[0], 0x05000051, 0xa00f00dd, 0xbf800000, 0, 0, 0] + motion[1:-1] +
                             [0x02000001, 0x80020802, 0xa00000dd, 0x0000ffff], r['name'])

    def test_5f82ecac_byte_exact_oracle(self):
        """XT BUMPMAP 5f82ecac (freighter_base's distant draw): specular on s2 into r2, the product join
        `mad r1.xyz, r1, c12.x, r4` (DWORD 1636 of the original); the plain motion twin's insertions are
        `add rT.w, r2.w, r2.w` right after that texld and `mul rT.xyz, r1, c12.x ; mad r1.xyz, rT, rT.w, r4`."""
        name = 'ps_5f82ecacd39529cd'
        r = next(r for r in self.rows if r['name'] == name and r['kind'] == 'motion')
        self.assertEqual((r['status'], r['stage'], r['source_register'], r['product']), (0, 2, 2, True))
        parent = words(self.out / f'{name}-motion-parent.bin')
        twin = words(self.out / f'{name}-motion-twin.bin')
        original = words(self.corpus / f'{name}.bin')
        self.assertEqual(SEEDS[0x5f82ecacd39529cd], (1636, 7, True))
        self.assertEqual(original[1636], (4 << 24) | MAD)
        self.assertEqual((original[1637] & 0x7ff, (original[1637] >> 16) & 15), (1, 7))          # r1.xyz
        self.assertEqual((rtype(original[1639]), original[1639] & 0x7ff, (original[1639] >> 16) & 0xff), (2, 12, 0))  # c12.x
        rt = r['scratch_register']
        t = r['texel_at']
        n = (parent[t] >> 24) & 15
        self.assertEqual(twin[t + n + 1:t + n + 5], [(3 << 24) | ADD, reg(0, rt, 8 << 16), reg(0, 2, 0xff << 16), reg(0, 2, 0xff << 16)])
        j = r['join_at'] + 4   # the add shifted the join by four words
        self.assertEqual(twin[j:j + 9], [(3 << 24) | MUL, reg(0, rt, 7 << 16), parent[r['join_at'] + 2], parent[r['join_at'] + 3],
                                         (4 << 24) | MAD, parent[r['join_at'] + 1], reg(0, rt, 0xe4 << 16),
                                         reg(0, rt, 0xff << 16), parent[r['join_at'] + 4]])
        self.assertEqual(len(twin), len(parent) + 8)
        self.assertEqual(twin[:t + n + 1], parent[:t + n + 1])
        self.assertEqual(twin[t + n + 5:j], parent[t + n + 1:r['join_at']])
        self.assertEqual(twin[j + 9:], parent[r['join_at'] + 5:])

    def test_native_validator_accepts_every_twin(self):
        """The recorded native validator run (SysWOW64 d3d9, WSL interop) covers every generated twin with end S_OK and
        no message; when the interop and the MinGW compiler are available the run is repeated on this session's twins."""
        recorded = json.loads((ROOT / 'verification/results/overlay-material-strength/native_validate_twins.json').read_text())
        twins = [r for r in self.rows if r['status'] == 0]
        self.assertEqual(recorded['twins'], len(twins))
        self.assertEqual({(t['kind'], t['end'], t['messages']) for t in recorded['tally']},
                         {(k, '00000000', 0) for k in KINDS})
        self.assertEqual(sum(t['programs'] for t in recorded['tally']), len(twins))
        mingw = shutil.which('i686-w64-mingw32-g++')
        if not mingw or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
            self.skipTest('native validator needs i686-w64-mingw32-g++ and WSL interop')
        exe = self.directory / 'native_validate.exe'
        build = subprocess.run([mingw, '-O1', '-static',
                                str(ROOT / 'verification/results/hull-widen-native-validator/native_validate.cpp'), '-o', str(exe)],
                               capture_output=True, text=True)
        self.assertEqual(build.returncode, 0, build.stderr)
        ends, total = [], 0
        for kind in KINDS:  # one run per form: the Windows command line holds about 32 KB
            names = sorted(p.name for p in self.out.glob(f'*-{kind}-twin.bin'))
            total += len(names)
            run = subprocess.run([str(exe)] + names, cwd=self.out, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr[:500])
            ends += re.findall(r'end=(\w+) messages=(\d+)', run.stdout)
        self.assertEqual(len(ends), total)
        self.assertEqual(total, len(twins))
        self.assertEqual(set(ends), {('00000000', '0')})


class TextureMarkTests(unittest.TestCase):
    def test_parser_and_table(self):
        compiler = shutil.which('g++') or shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='x3-texture-mark-') as folder:
            exe = Path(folder) / 'check'
            build = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/lod_texture_mark_check.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            report = json.loads(run.stdout)
            self.assertEqual((run.returncode, report['ok']), (0, True), report)
            self.assertGreaterEqual(report['checks'], 20)

    def test_baker_mark_matches_the_parser_constants(self):
        """The baker's bytes (lod_atlas.strength_mark) and the proxy's constants agree: magic, version, slot ids, lane w,
        K 2, payload 1."""
        import lod_atlas
        header = (ROOT / 'src/proxy/lod_texture_mark.h').read_text()
        magic = int(re.search(r'magic = 0x([0-9a-f]+)u', header).group(1), 16)
        self.assertEqual(struct.pack('<I', magic), lod_atlas.DDS_MARK)
        self.assertEqual(int(re.search(r'version = (\d+);', header).group(1)), lod_atlas.DDS_MARK_VERSION)
        self.assertEqual(int(re.search(r'lane_w = (\d+)', header).group(1)), lod_atlas.LANE_W)
        self.assertEqual(int(re.search(r'strength_k = (\d+)', header).group(1)), lod_atlas.STRENGTH_K)
        self.assertEqual(int(re.search(r'payload_strength = (\d+)', header).group(1)), lod_atlas.PAYLOAD_STRENGTH)
        self.assertEqual(int(re.search(r'mark_offset = (\d+)', header).group(1)), lod_atlas.DDS_MARK_OFFSET)
        mark = lod_atlas.strength_mark('specular')
        self.assertEqual(struct.unpack('<III', mark), (magic, 1, 3 | 3 << 8 | 2 << 16 | 1 << 24))
        self.assertEqual(lod_atlas.SLOT_IDS, {'diffuse': 0, 'light': 1, 'bump': 2, 'specular': 3})


if __name__ == '__main__':
    unittest.main()
