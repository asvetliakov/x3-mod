import struct, sys, subprocess, os
sys.path.insert(0, sys.argv[1] + '/tools/analysis')
import fog_families as ff
FNV_OFFSET, FNV_PRIME, MASK = 14695981039346656037, 1099511628211, (1 << 64) - 1
TEXELS = 1560 * 1430
def packet(profile, seed, literal=4):
    texels = b''.join(struct.pack('<H', 0x3000 + ((seed + t) & 0x3ff)) for t in range(literal * 4))
    c = FNV_OFFSET
    for b in texels: c = ((c ^ b) * FNV_PRIME) & MASK
    c = (c * pow(FNV_PRIME, (TEXELS - literal) * 8, 1 << 64)) & MASK
    payload = struct.pack('<I', 0x80000000 | literal) + texels + struct.pack('<I', TEXELS - literal)
    header = ff.PACKET_HEADER.pack(b'X3FOGPK', 1, 56, profile, 1, 1560, 1430, 8, TEXELS * 8, 2, c, 0)
    return dict(bytes=header + payload, decoded_fnv1a=c, profile_id=profile, decoded_sha256='00' * 32)
# row 0: name zza, bad sigma (disabled); row 1: name zza, valid -> same profile id
rows = [dict(name='zza', profile_id=ff.profile_id('zza'), packet=0, base_sigma=1e-3, occupancy=.12, chroma=[.2,.4,1.], colours=[[.5,.5,1.]]*4, flags=0),
        dict(name='zza', profile_id=ff.profile_id('zza'), packet=1, base_sigma=2.5e-6, occupancy=.12, chroma=[.2,.4,1.], colours=[[.5,.5,1.]]*4, flags=0)]
data = ff.build_file(rows, [packet(ff.profile_id('zza'), 7, 4), packet(ff.profile_id('zza'), 38, 5)])
open('dup.bin', 'wb').write(data)
r = ff.read_file('dup.bin')
print('python:', r['status'], r['reason'], [(x['name'], x['disabled']) for x in r['rows']])
