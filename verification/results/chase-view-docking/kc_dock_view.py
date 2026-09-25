#!/usr/bin/env python3
"""Static evidence for docs/reverse-engineering/chase-view-docking.md.

Read-only. Extracts L/x3story.obj from the installed addon/04 catalogue in memory (XOR 0x33),
decodes STRG/CLAS with the loader contract of 0x0049d030 and the instruction widths of 0x0049e1a0
(selection-native-vm.md), reads X3AP.exe for the native CanLand call and the cockpit command table,
and prints only derived facts: CODE offsets, method names, return offsets, stack depths and
instruction bytes at named native sites. No bytecode or game bytes are written.

usage: python3 kc_dock_view.py [X3 install dir]
"""
import bisect, hashlib, os, re, struct, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    "~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3")
STORY_SHA = "ed5786a0c603802d5735fb36e0332d7c732b981d2b64dcee4174890284faff7a"

def load_story():
    cat = open(os.path.join(ROOT, "addon", "04.cat"), "rb").read()
    text = bytes(c ^ ((0xdb + i) & 0xff) for i, c in enumerate(cat)).decode("latin1").split("\n")
    off = 0
    for line in text[1:]:
        if not line.strip():
            continue
        name, size = line.rsplit(" ", 1); size = int(size)
        if name.replace("\\", "/").lower() == "l/x3story.obj":
            with open(os.path.join(ROOT, "addon", "04.dat"), "rb") as f:
                f.seek(off); return bytes(x ^ 0x33 for x in f.read(size))
        off += size
    raise SystemExit("L/x3story.obj not found")

B = load_story()
assert hashlib.sha256(B).hexdigest() == STORY_SHA, "unexpected x3story.obj"
CH, o = {}, 12
while o + 8 <= len(B):
    tag = B[o:o + 4].decode(); n = struct.unpack(">I", B[o + 4:o + 8])[0]; CH[tag] = B[o + 8:o + 8 + n]; o += 8 + n
CODE, ENC = CH["CODE"], CH["STRG"]
STR = bytes([(~ENC[0]) & 255] + [((~ENC[i]) - ENC[i - 1]) & 255 for i in range(1, len(ENC))])
def sname(v):
    return "?%x" % v if v == 0 or v >= len(STR) else STR[v:STR.index(b"\0", v)].decode("latin1")
C, p = CH["CLAS"], 0
def u():
    global p
    v = struct.unpack_from(">I", C, p)[0]; p += 4; return v
METH, PARENT = {}, {}
for _ in range(u()):
    cid, parent, _f = u(), u(), u(); PARENT[cid] = parent
    for _m in range(u()):
        e, n, _l, _a = u(), u(), u(), u(); METH.setdefault(e, []).append((cid, sname(n)))
    for _v in range(u() * 4):
        u()
assert p == len(C)
ENTRIES = sorted(METH)
W = {op: 2 for op in (0x05, 0x47, 0x4c)}
W.update({op: 3 for op in (0x06, 0x0d, 0x0e, 0x0f, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1a,
                           0x1b, 0x1c, 0x1d, 0x1f, 0x20, 0x21, 0x23, 0x28, 0x29, 0x2c, 0x30, 0x48, 0x4d, 0x6e)})
W.update({op: 5 for op in (0x07, 0x0b, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x49, 0x4e,
                           0x82, 0x84, 0x85, 0x86, 0x87, 0x88)})
W[0x0a] = 9
def width(pc):
    op = CODE[pc]
    if op == 0x78: return 3 + (struct.unpack_from(">H", CODE, pc + 1)[0] + 2) * 4
    if op == 0x79:
        a, c = struct.unpack_from(">HH", CODE, pc + 1); return 5 + ((a + c) * 2 + 1) * 4
    return W.get(op, 1)
def owner(pc):
    e = ENTRIES[bisect.bisect_right(ENTRIES, pc) - 1]; return "%x::%s" % METH[e][0]
def find(cls, name):
    return [e for e, ms in METH.items() if (cls, name) in ms][0]
def end_of(entry):
    k = bisect.bisect_right(ENTRIES, entry); return ENTRIES[k] if k < len(ENTRIES) else len(CODE)
def target(pc):
    op = CODE[pc]; v = struct.unpack_from(">I", CODE, pc + 1)[0]
    if op in (0x82, 0x85): return sname(v)
    if op in (0x86, 0x88): return "%x::%s" % METH[v][0] if v in METH else hex(v)
def calls(entry, names=None):
    pc, out = entry, []
    while pc < end_of(entry):
        if CODE[pc] in (0x82, 0x85, 0x86, 0x88):
            t = target(pc)
            if names is None or any(t.endswith(n) for n in names): out.append((pc, pc + 5, t))
        elif CODE[pc] in (0x6f, 0x70): out.append((pc, None, "FORK" if CODE[pc] == 0x6f else "ENDFORK"))
        pc += width(pc)
    return out
def callers(name):
    out, pc = [], 1
    while pc < len(CODE):
        if CODE[pc] in (0x85, 0x86, 0x88) and (target(pc) or "").split("::")[-1] == name: out.append("%05x %s" % (pc, owner(pc)))
        pc += width(pc)
    return out
# Stack depth (cells pushed by the method body) before each instruction. LOADL k (handler 0x004a27f8)
# reads the k-th cell from the top, so at depth d the body's own pushes are k <= d and the argument
# of a one-argument method is k = d + 4 (context, return, count, argument).
IMM = {0x01: 0, 0x02: 1, 0x03: 2, 0x04: 3}
def depths(entry):
    end = end_of(entry); prev = {}; pc = entry; last = None
    while pc < end:
        prev[pc] = last; last = pc; pc += width(pc)
    def imm(q):
        return IMM.get(CODE[q], CODE[q + 1] if CODE[q] == 0x05 else None) if q is not None else None
    def value_start(q):  # first instruction of the single-value expression ending at q (object operand)
        if CODE[q] in (0x86,): return value_start(prev[prev[q]]) if False else None
        return q
    D, work = {entry: 0}, [entry]
    while work:
        pc = work.pop(); d = D[pc]; op = CODE[pc]; nxt = [pc + width(pc)]
        if op in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0b, 0x0d, 0x0e, 0x0f, 0x2e, 0x6f): d += 1
        elif op in (0x24, 0x10) or 0x46 <= op <= 0x63: d -= 1
        elif op == 0x17: d -= 2
        elif op in (0x33, 0x34): d -= 1; nxt.append(struct.unpack_from(">I", CODE, pc + 1)[0])
        elif op == 0x32: nxt = [struct.unpack_from(">I", CODE, pc + 1)[0]]
        elif op == 0x23: d -= struct.unpack_from(">H", CODE, pc + 1)[0]
        elif op in (0x82, 0x84, 0x87, 0x88): d -= imm(prev[pc])
        elif op == 0x86: d -= imm(prev[prev[pc]]) + 1
        elif op == 0x85:
            q = prev[pc]  # object operand: one pushed value, possibly a call result
            n = imm(prev[q]) if CODE[q] in (0x0d, 0x0e, 0x0f, 0x2e) else None
            if n is None: continue  # this path is not needed for the reported sites
            d -= n + 1
        elif op in (0x83, 0x70): nxt = []
        for q in nxt:
            if q < end and q not in D: D[q] = d; work.append(q)
    return D

print("x3story.obj sha256", STORY_SHA)
print("class chains: 7e0", "->".join("%x" % c for c in (0x7e0, PARENT[0x7e0])), "| 7f1 ->", "%x" % PARENT[0x7f1])
for title, cls, name, keep in (
        ("7d4::CanLand (player station branch)", 0x7d4, "CanLand", ("SA_SetPlayerLand", "StartPlayerTrade", "Signal_Docked", "MoveTo")),
        ("7e0::StartPlayerTrade", 0x7e0, "StartPlayerTrade", None),
        ("7e0::RunPlayerTrade", 0x7e0, "RunPlayerTrade", ("StopAllMonitors", "RestartAllMonitors", "StartPlayerShip", "SetCockpitNumber", "SetTracking", "IsDocked")),
        ("7e0::StartPlayerShip", 0x7e0, "StartPlayerShip", ("__StartInHangar",)),
        ("280::Undock", 0x280, "Undock", None),
        ("7d4::__StartInHangar", 0x7d4, "__StartInHangar", ("SE_IsClass", "SA_StartInHangar", "StopAllMonitors", "RestartAllMonitors", "SetCockpitNumber", "SetTracking")),
        ("25d::StopAllMonitors", 0x25d, "StopAllMonitors", ("StopMonitor", "TI_Interrupt")),
        ("25d::RestartAllMonitors", 0x25d, "RestartAllMonitors", ("SelectMode", "OpenLayout")),
        ("96::LandPlayerShipAt", 0x96, "LandPlayerShipAt", None),
        ("96::DockPlayerShipTo", 0x96, "DockPlayerShipTo", None)):
    e = find(cls, name)
    print("\n== %s entry %05x" % (title, e))
    for pc, ret, t in calls(e, keep):
        print("  %05x %-44s return %s" % (pc, t, "%05x" % ret if ret else "-"))
D = depths(find(0x7d4, "__StartInHangar"))
print("\n__StartInHangar depths: SE_IsClass(0x7f1, arg) at b75d5 d=%d -> body cell #3; restart gate b797c LOADL %d at d=%d, b7990 LOADL %d at d=%d"
      % (D[0xb75d5], struct.unpack_from(">H", CODE, 0xb797d)[0], D[0xb797c], struct.unpack_from(">H", CODE, 0xb7991)[0], D[0xb7990]))
print("  arg check b75d1 LOADL %d at d=%d (= d+4: the hangar argument)" % (struct.unpack_from(">H", CODE, 0xb75d2)[0], D[0xb75d1]))
print("\ncallers SelectMode:", len(callers("SelectMode")), "; StartPlayerShip:", callers("StartPlayerShip"))
print("callers CanLand:", callers("CanLand"), "; StartPlayerTrade:", callers("StartPlayerTrade"))
stores = []
pc = 1
while pc < len(CODE):
    if CODE[pc] == 0x16 and struct.unpack_from(">H", CODE, pc + 1)[0] == 0 and owner(pc).startswith("25e::"): stores.append("%05x %s" % (pc, owner(pc)))
    pc += width(pc)
print("25e cell0 stores:", stores)
print("RestartAllMonitors SelectMode literal:", CODE[0xedc85:0xedc91].hex(" "))

# ---- X3AP.exe ----
D2 = open(os.path.join(ROOT, "X3AP.exe"), "rb").read()
pe = struct.unpack_from("<I", D2, 0x3c)[0]; nsec = struct.unpack_from("<H", D2, pe + 6)[0]; osz = struct.unpack_from("<H", D2, pe + 20)[0]
SECS = [struct.unpack_from("<IIII", D2, pe + 24 + osz + 40 * i + 8) for i in range(nsec)]
def va2off(v):
    for vs, va, rs, ra in SECS:
        if 0x400000 + va <= v < 0x400000 + va + max(vs, rs): return v - 0x400000 - va + ra
def off2va(o):
    for vs, va, rs, ra in SECS:
        if ra <= o < ra + rs: return 0x400000 + va + o - ra
s = off2va(D2.index(b"\0CanLand\0") + 1)
print("\nEXE sha256", hashlib.sha256(D2).hexdigest())
print("string CanLand at 0x%08x; push sites:" % s, ["0x%08x" % off2va(m.start()) for m in re.finditer(re.escape(b"\x68" + struct.pack("<I", s)), D2)])
print("0x0045d8a0..0x0045d8c5 bytes:", D2[va2off(0x45d8a0):va2off(0x45d8c5)].hex(" "))
tbl = 0x57aef0
for case in (0x18, 0x2c, 0x2e, 0x30):
    name_va = struct.unpack_from("<I", D2, va2off(tbl + 4 * case))[0]; no = va2off(name_va)
    tgt = struct.unpack_from("<I", D2, va2off(0x42f064 + 4 * case))[0]
    print("cockpit command 0x%02x %s -> case 0x%08x" % (case, D2[no:D2.index(b"\0", no)].decode(), tgt))
print("0x0042e742 bytes:", D2[va2off(0x42e742):va2off(0x42e748)].hex(" "), "| 0x004a3ffd bytes:", D2[va2off(0x4a3ffd):va2off(0x4a4003)].hex(" "))
