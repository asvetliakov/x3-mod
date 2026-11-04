#!/usr/bin/env python3
"""Read-only check for docs/reverse-engineering/sector-identity.md (both editions).

EXE side: pins the byte windows of the sector-identity read chain in X3AP.exe and X3FL.exe (player root, parent
sector test, SA_AllocObject sector branch, SA_SetEventObject / SA_GetEventObject at object +0x94, the KC object
lookup over [VM+0x12d0], the KC ldmem / this / new-object handlers, the class binary search, +0x94 init and save
restore) and lists the HIGHLOW relocations inside each FL window. KC side: decodes L/x3story.obj of AP+Mayhem
(addon/07.cat), stock AP (addon/04.cat) and FL (addon2/03.cat) and checks 2001::GetX/GetY members, the only writers
of those members, 2001::Activate's SA_AllocObject -> member -> SA_SetEventObject(this, member) sequence and the
music member. Map side: x/y range, duplicate coordinates and music-id sharing per loaded universe map.
Prints a small JSON summary; no bytes are written, no game is run.

    python3 -I verification/results/sector-identity/verify_sector_identity.py [AP dir] [FL dir] > .../summary.json
Exit status 0 only on PASS.
"""
import collections, hashlib, json, struct, sys
import xml.etree.ElementTree as ET
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
from kc_obj import Obj, load_member  # noqa: E402
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402

AP_DIR = Path(sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3')
FL_DIR = Path(sys.argv[2] if len(sys.argv) > 2 else '/mnt/c/X3FL')
SHA = {'ap': 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab',
       'fl': 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'}

WINDOWS = {
    'ap': [
        ('player_root', 0x42c890, 'a10c85600083ec08535556578b7838'),
        ('parent_is_sector', 0x42c8a9, '8b475485c00f84b10000006683784801'),
        ('sa_alloc_sector', 0x46070d, '6880010000e8296308008bf083c4048974241033c03bf0898424ec03000074c58bc3e89c1dffffeb'),
        ('sa_set_event_object', 0x46078b, '8b75188b5601a10c856000e8559dfdff85c0742f8a4e0580f90175178b4e06898894000000'
                                          '8b15e48560006a0052e93b26000080f90a750b8b56068b0a898894000000'),
        ('sa_get_event_object', 0x4607dc, '8b45188b5001a10c856000e8049dfdff85c00f8411ffffff8b809400000050e9f2250000'),
        ('kc_object_lookup', 0x49f1e0, '85c07d3283c9ff2bc88b86d01200008b1085d2741a8b400483e80123c18b048285c0740b3948'
                                       '0474098b0085c075f533c0c38b4008c38b561c508b4620e8ce14010083c404c3'),
        ('kc_ldmem', 0x4a283c, '0fbf078b4c24208d048003410c83eb058bf3895c2414e8995c000083c702'),
        ('kc_this', 0x4a3052, '8b4c24208b0183eb05c60301894301'),
        ('kc_new_object_id', 0x4a2e0f, '8b5424188b74242c8b82d012000056e89dce040083c9ff2bc883c404890e'),
        ('kc_class_bsearch', 0x4b06f0, '85d2538b5c240856578bf8762a8d49008bcad1e98d04cd000000002bc18b34c73bde8d04c77c16'
                                       '7e108d783883c8ff2bc103d085d277d933c05f5e5bc38bd1ebf2'),
        ('object_init_event_zero', 0x44004f, '899d80000000899d94000000899d9800000066c7859c0000'),
        ('restore_event_object', 0x430130, '898780000000e8e5920b00898794000000'),
        ('vm_create_store', 0x4031e8, 'e8237e0a003bc6a3e4856000'),
    ],
    'fl': [
        ('player_root', 0x42f913, 'a1ec825a005355578b5838'),
        ('parent_is_sector', 0x42f92e, '8b435485c00f849601000066837848010f858b010000'),
        ('sa_alloc_sector', 0x46a71e, '6880010000e8f8e9060083c4048945acc745fc0000000085c0740a568bc8e8cf19ffff'),
        ('sa_set_event_object', 0x46a7c1, '8b73188b0dec825a00ff7601e85e95fdff8bd085d274328a46053c01751c8b4e066a00898a94'
                                          '0000008b0d74a15a0057e84a710500e9a39500003c0a750b8b46068b008982940000008b0d74'
                                          'a15a006a0057e828710500e981950000'),
        ('sa_get_event_object', 0x46a81d, '8b43188b0dec825a00ff7001e80295fdff85c00f841affffff8b80940000008b0d74a15a0050'
                                          '57e8f7700500e950950000'),
        ('kc_object_lookup', 0x4bb1b0, '8b54240485d279318b81d0120000f7d28b0885c974188b40044823c28b048185c0740b395004'
                                       '740b8b0085c075f533c0c204008b4008c20400528b511c8b4920e88b1f010083c404c20400'),
        ('kc_ldmem', 0x4bef32, '0fbf0683ef058b4de0897de8897de48d048003410c8bcf50e821650000'),
        ('kc_this', 0x4bf9eb, '8b45e083ef058b00c60701894701e9d3f3ffff'),
        ('kc_new_object_id', 0x4bf7b2, '8b45ec8bf18b7de88bd68b88d0120000e819d8040083c9ff2bc8890e'),
        ('kc_class_bsearch', 0x4cd180, '538bd9565785d274328b7c24100f1f008bcad1e98d04cd000000002bc18b34c38d04c33bfe7c0e'
                                       '7e148d583883c8ff2bc103d0eb028bd185d275d533c05f5e5bc3'),
        ('restore_event_object', 0x434658, '8bcb6a010bd08d4424146a0450899794000000'),
    ],
}


class PE:
    def __init__(self, path):
        d = self.d = path.read_bytes()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        n, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
        self.base = struct.unpack_from('<I', d, pe + 52)[0]
        self.secs = []
        for i in range(n):
            o = pe + 24 + opt + i * 40
            vs, va, rs, rp = struct.unpack_from('<IIII', d, o + 8)
            self.secs.append((self.base + va, vs, rp, rs))
        self.relocs = set()
        rva, size = struct.unpack_from('<II', d, pe + 24 + 96 + 5 * 8)
        if size:
            p, end = self.off(self.base + rva), self.off(self.base + rva) + size
            while p < end:
                page, block = struct.unpack_from('<II', d, p)
                for k in range((block - 8) // 2):
                    e = struct.unpack_from('<H', d, p + 8 + 2 * k)[0]
                    if e >> 12 == 3: self.relocs.add(self.base + page + (e & 0xfff))
                p += block

    def off(self, va):
        for v, vs, rp, rs in self.secs:
            if v <= va < v + rs: return rp + va - v
        raise ValueError(hex(va))

    def rd(self, va, n): return self.d[self.off(va):self.off(va) + n]


def kc_facts(obj):
    o = obj
    def body(cid, nm):
        es = o.find(cid, nm)
        assert len(es) == 1, (cid, nm, es)
        return [(o.code[pc], o.operand(pc)) for pc in o.insns(es[0])]
    gx, gy = body(2001, 'GetX'), body(2001, 'GetY')
    x_member = int(gx[1][1]) if gx[1][0] == 0x0f and gx[2][0] == 0x83 else None
    y_member = int(gy[1][1]) if gy[1][0] == 0x0f and gy[2][0] == 0x83 else None
    create = body(2001, 'Create')
    create_ok = create[1:7] == [(0x0d, '4'), (0x16, str(x_member)), (0x24, ''), (0x0d, '5'), (0x16, str(y_member)), (0x24, '')]
    # every store to the x/y members inside class-2001 methods (KC stmem only targets `this`)
    import bisect
    writers = collections.Counter()
    music = body(2001, 'GetMusicTrack')
    music_member = int(music[1][1]) if music[1][0] == 0x0f else None
    music_writers = collections.Counter()
    for pc in o.all_insns():
        if o.code[pc] != 0x16: continue
        e = o.entries[bisect.bisect_right(o.entries, pc) - 1]
        if not any(c == 2001 for c, _, _ in o.meth[e]): continue
        m = int(o.operand(pc))
        names = '/'.join(n for c, n, _ in o.meth[e] if c == 2001)
        if m in (x_member, y_member): writers[(names, m)] += 1
        if m == music_member: music_writers[names] += 1
    act = body(2001, 'Activate')
    native_member = None
    for k in range(len(act) - 5):
        if act[k] == (0x82, 'SA_AllocObject') and act[k + 1][0] == 0x16 and act[k + 3][0] == 0x2e and \
                act[k + 4] == (0x0f, act[k + 1][1]) and act[k + 6] == (0x82, 'SA_SetEventObject'):
            native_member = int(act[k + 1][1]); break
    deact = body(2001, 'Deactivate')
    frees = any(deact[k] == (0x0f, str(native_member)) and any(d == (0x82, 'SA_FreeObject') for d in deact[k:k + 3])
                for k in range(len(deact)))
    return dict(build=o.build, x_member=x_member, y_member=y_member, create_writes_xy_from_args=create_ok,
                xy_writers=sorted('%s:m%d=%d' % (n, m, c) for (n, m), c in writers.items()),
                native_id_member=native_member, deactivate_frees_native=frees,
                music_member=music_member, music_writers=dict(music_writers),
                member_count=len(o.classes[2001]['members']), parent=o.parent[2001])


def map_facts(assets):
    data, src = assets.logical('maps/x3_universe', ('.pck', '.xml'))
    secs = [e for e in ET.fromstring(data).iter('o') if e.get('t') == '1']
    xs, ys = [int(e.get('x')) for e in secs], [int(e.get('y')) for e in secs]
    xy = collections.Counter(zip(xs, ys)); m = collections.Counter(e.get('m') for e in secs)
    return dict(source=src['source'] + ':' + src['member'], decoded_sha256=src['decoded_sha256'], sectors=len(secs),
                x=[min(xs), max(xs)], y=[min(ys), max(ys)], duplicate_xy=sum(v > 1 for v in xy.values()),
                distinct_m=len(m), m_values_shared=sum(v > 1 for v in m.values()),
                sectors_with_shared_m=sum(v for v in m.values() if v > 1), m_zero=m.get('0', 0))


out, fails = {}, []
for ed, path in (('ap', AP_DIR / 'X3AP.exe'), ('fl', FL_DIR / 'X3FL.exe')):
    pe = PE(path)
    sha = hashlib.sha256(pe.d).hexdigest()
    if sha != SHA[ed]: fails.append(ed + ' sha256')
    rows = []
    for name, va, hx in WINDOWS[ed]:
        want = bytes.fromhex(hx); got = pe.rd(va, len(want))
        rel = sorted(r - va for r in pe.relocs if va <= r < va + len(want))
        ok = got == want
        if not ok: fails.append('%s %s' % (ed, name))
        rows.append(dict(name=name, va=hex(va), length=len(want), match=ok, highlow_reloc_offsets=rel))
    out[ed] = dict(exe=str(path), sha256=sha, relocatable=bool(pe.relocs), windows=rows)

kc = {'ap_mayhem': (AP_DIR / 'addon/07.cat', 'L/x3story.obj'), 'ap_stock': (AP_DIR / 'addon/04.cat', 'L/x3story.obj'),
      'fl': (FL_DIR / 'addon2/03.cat', 'addon2/L/x3story.obj')}
for label, (cat, member) in kc.items():
    f = kc_facts(Obj(load_member(cat, member)))
    out['kc_' + label] = f
    if (f['x_member'], f['y_member']) != (0, 1) or not f['create_writes_xy_from_args'] or \
            sorted(f['xy_writers']) != ['Create:m0=1', 'Create:m1=1'] or f['native_id_member'] is None:
        fails.append('kc ' + label)

out['map_ap_loaded'] = map_facts(Assets(AP_DIR, edition='ap'))
out['map_ap_stock'] = map_facts(Assets(AP_DIR, catalogues=STOCK_AP_CATALOGUES))
out['map_fl_loaded'] = map_facts(Assets(FL_DIR, edition='fl'))
for k in ('map_ap_loaded', 'map_ap_stock', 'map_fl_loaded'):
    if out[k]['duplicate_xy']: fails.append(k + ' duplicate xy')
out['result'] = 'PASS' if not fails else 'FAIL'
out['failures'] = fails
print(json.dumps(out, indent=1))
sys.exit(0 if not fails else 1)
