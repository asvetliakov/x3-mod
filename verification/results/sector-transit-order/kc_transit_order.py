#!/usr/bin/env python3
"""Static call order of the sector-transit script path (docs/reverse-engineering/sector-transit-order.md).

Read-only. Extracts L/x3story.obj from the installed addon/04 catalogue into memory (XOR 0x33),
decodes STRG/CLAS with the loader contract of 0x0049d030 and the instruction widths of 0x0049e1a0
(selection-native-vm.md), and prints only derived facts: CODE offsets, method names, native command
names and fork markers. No bytecode or game bytes are written.

usage: python3 kc_transit_order.py [X3 install dir]
"""
import bisect, hashlib, os, struct, sys

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
        name, size = line.rsplit(" ", 1)
        size = int(size)
        if name.replace("\\", "/").lower() == "l/x3story.obj":
            with open(os.path.join(ROOT, "addon", "04.dat"), "rb") as f:
                f.seek(off)
                return bytes(x ^ 0x33 for x in f.read(size))
        off += size
    raise SystemExit("L/x3story.obj not found in addon/04.cat")

B = load_story()
assert hashlib.sha256(B).hexdigest() == STORY_SHA, "unexpected x3story.obj"
CH = {}
o = 12
while o + 8 <= len(B):
    tag = B[o:o + 4].decode(); n = struct.unpack(">I", B[o + 4:o + 8])[0]
    CH[tag] = B[o + 8:o + 8 + n]; o += 8 + n
CODE, ENC = CH["CODE"], CH["STRG"]
STR = bytes([(~ENC[0]) & 255] + [((~ENC[i]) - ENC[i - 1]) & 255 for i in range(1, len(ENC))])

def sname(v):
    if v == 0 or v >= len(STR):
        return "?%x" % v
    return STR[v:STR.index(b"\0", v)].decode("latin1")

C = CH["CLAS"]; p = 0
def u():
    global p
    v = struct.unpack_from(">I", C, p)[0]; p += 4; return v
METH = {}
for _ in range(u()):
    cid, _parent, _f = u(), u(), u()
    for _m in range(u()):
        e, n, _l, _a = u(), u(), u(), u()
        METH.setdefault(e, []).append((cid, sname(n)))
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
    if op == 0x78:
        return 3 + (struct.unpack_from(">H", CODE, pc + 1)[0] + 2) * 4
    if op == 0x79:
        a, c = struct.unpack_from(">HH", CODE, pc + 1); return 5 + ((a + c) * 2 + 1) * 4
    return W.get(op, 1)

def owner(pc):
    e = ENTRIES[bisect.bisect_right(ENTRIES, pc) - 1]; return e, METH[e]

def events(entry, lo=None, hi=None):
    pc = entry
    while pc < len(CODE):
        if pc != entry and pc in METH:
            break
        op = CODE[pc]
        if (lo is None or pc >= lo) and (hi is None or pc <= hi):
            if op == 0x82:
                yield pc, "native", sname(struct.unpack_from(">I", CODE, pc + 1)[0])
            elif op in (0x86, 0x88):
                v = struct.unpack_from(">I", CODE, pc + 1)[0]
                yield pc, "call", "%d::%s" % METH[v][0] if v in METH else hex(v)
            elif op == 0x85:
                yield pc, "dyncall", sname(struct.unpack_from(">I", CODE, pc + 1)[0])
            elif op in (0x6f, 0x70):
                yield pc, "fork" if op == 0x6f else "endfork", ""
        pc += width(pc)

def find(cls, name):
    return [e for e, ms in METH.items() if (cls, name) in ms][0]

def sites(native):
    out, pc = [], 1
    while pc < len(CODE):
        if CODE[pc] == 0x82 and sname(struct.unpack_from(">I", CODE, pc + 1)[0]) == native:
            e, ms = owner(pc); out.append("%05x in %d::%s" % (pc, ms[0][0], ms[0][1]))
        pc += width(pc)
    return out

print("x3story.obj sha256", STORY_SHA, "CODE", len(CODE), "methods", len(METH))
SKIP = {"SE_TableNext", "SE_TableSize", "SE_Random", "SE_IsClass", "SE_GetExprType"}
for title, cls, name, lo, hi in (
        ("150::WarpToSector critical section", 150, "WarpToSector", 0x16605, 0x1672c),
        ("150::QuickWarp", 150, "QuickWarp", None, None),
        ("2001::Activate (sector)", 2001, "Activate", None, 0x8b100),
        ("2001::Deactivate (sector)", 2001, "Deactivate", None, None),
        ("402::EnterSector", 402, "EnterSector", None, None),
        ("402::WarpEnterSector", 402, "WarpEnterSector", None, None),
        ("605::RestartAllMonitors", 605, "RestartAllMonitors", None, None),
        ("606::StartMonitor (cockpit alloc/sector space)", 606, "StartMonitor", None, 0xf0210)):
    e = find(cls, name)
    print("\n== %s  entry %05x" % (title, e))
    for pc, kind, what in events(e, lo, hi):
        if kind == "native" and what in SKIP:
            continue
        print("  %05x %-8s %s" % (pc, kind, what))
print()
for n in ("INS_CockpitSetSectorSpace", "INS_CockpitAlloc", "INS_SetActiveControlCockpit", "SA_SetSectorBackgroundType",
          "SA_SetSpaceScene", "SA_FreeAllBodies", "SA_CleanUpObjects"):
    print("sites", n, sites(n))
def callers_of(entry):
    out, pc = [], 1
    while pc < len(CODE):
        if CODE[pc] in (0x86, 0x88) and struct.unpack_from(">I", CODE, pc + 1)[0] == entry:
            ms = owner(pc)[1]; out.append("%05x in %d::%s" % (pc, ms[0][0], ms[0][1]))
        pc += width(pc)
    return out
for cls, name in ((150, "WarpToSector"), (150, "QuickWarp"), (150, "StartGateWarp"), (150, "StartSectorWarp")):
    print("callers", "%d::%s" % (cls, name), callers_of(find(cls, name)))
# SA_AllocObject main/subtype: the last pushed argument is the main type (0x00460630 case 0 reads it
# at args+1), the one before it the subtype. Opcodes 01..04 push 0..3, 05 pushes a byte.
PUSH = {0x01: "0", 0x02: "1", 0x03: "2", 0x04: "3"}
for pc_s in sites("SA_AllocObject"):
    pc = int(pc_s.split()[0], 16); e = owner(pc)[0]; q = e; prev = []
    while q < pc:
        prev.append(q); q += width(q)
    def arg(q):
        op = CODE[q]
        if op in PUSH: return PUSH[op]
        if op == 0x05: return str(CODE[q + 1])
        if op == 0x0f: return "member%d" % struct.unpack_from(">H", CODE, q + 1)[0]
        if op == 0x0d: return "local%d" % struct.unpack_from(">H", CODE, q + 1)[0]
        return "expr"
    print("SA_AllocObject", pc_s, "main=%s sub=%s" % (arg(prev[-2]), arg(prev[-3])))
