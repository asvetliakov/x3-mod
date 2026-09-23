#!/usr/bin/env python3
"""Static re-check of docs/reverse-engineering/pause-dialog-input.md.

Reads the bottle X3AP.exe (read-only) and prints/writes only derived facts:
addresses, short instruction byte patterns, table resolutions and counts.
Usage: python3 verify_pause_sites.py [path/to/X3AP.exe] [--json out.json] [--key CODE]
Needs i686-w64-mingw32-objdump on PATH for the decode checks and a host C++
compiler (clang++/c++) for the DLL-byte checks: the production encoder in
src/proxy/pause_key_only_core.h is compiled and its window, original bytes and
replacement (default key 0x1b5, plus --key CODE) are compared with the image
and decoded at the site.
"""
import hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CORE = ROOT / "src/proxy/pause_key_only_core.h"
DEFAULT_KEY, MAX_KEY = 0x1B5, 0x1FFF
WINDOW_VA, WINDOW_LEN = 0x004043A0, 79
INSTALL_RE = re.compile(r"\bpause_key_only patched=(?P<patched>[01]) key=(?P<key>0x[0-9a-f]+) reason=(?P<reason>[a-z_]+) "
                        r"requested=(?P<requested>[01]) site=(?P<site>0x[0-9a-f]{8}) write=(?P<write>none|atomic|plain)\s*$")

EXE_SHA = "fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab"
DEFAULT = os.path.expanduser("~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe")
OBJDUMP = "i686-w64-mingw32-objdump"

# Site bytes the note documents (VA -> expected bytes).
SITES = {
    "set_pause_native9_or_bit0": (0x0040705D, "83 8e a0 04 00 00 01"),   # or [esi+0x4a0],1
    "set_pause_native9_sound":   (0x00407064, "e8 47 12 09 00"),         # call 0x004982b0
    "native_table_register":     (0x00403591, "68 f0 c8 57 00 ba c0 6d 40 00"),
    "main_loop_input_call":      (0x00403db8, "e8 c3 04 00 00"),         # call 0x00404280
    "wait_entry_test_bit0":      (0x004042a0, "a8 01"),
    "wait_entry_jne":            (0x004042a9, "0f 85 dd 00 00 00"),     # -> 0x0040438c
    "wait_head":                 (0x004043a0, "66 85 f6 74 0c"),         # test si,si; je 0x004043b1
    "wait_key_test":             (0x004043a5, "8b ce 33 cb f7 c1 ff 0f 00 00 75 27"),
    "wait_mouse_test":           (0x004043b1, "8b d5 0b d7 3b d5 75 1f"),
    "wait_pump_call":            (0x004043b9, "e8 f2 f0 0c 00"),         # call 0x004d34b0
    "wait_key_call":             (0x004043ce, "e8 8d f7 0c 00"),         # call 0x004d3b60
    "wait_exit_clear_bit0":      (0x004043dd, "8b 88 a0 04 00 00 83 e1 fe 83 c9 02 89 88 a0 04 00 00"),
    "wait_exit_clock_resync":    (0x004043ef, "e8 fc d9 0c 00"),         # call 0x004d1df0
    "wndproc_activate":          (0x004d36a5, "e8 a6 12 00 00"),         # WM_ACTIVATE -> 0x004d4950
    "wndproc_activate_inactive": (0x004d36b1, "89 9e 84 04 00 00 89 1d dc 8a 60 00"),
    "wndproc_activateapp":       (0x004d36fe, "e8 4d 12 00 00"),         # WM_ACTIVATEAPP -> 0x004d4950
    "dik_pause_to_0x1b5":        (0x004d6b30, "b8 b5 01 00 00 c3"),
}
PATCH_VA, PATCH_OLD = 0x004043A5, "8b ce 33 cb f7 c1 ff 0f 00 00 75 27"
PATCH_NEW = "66 81 fe b5 01 75 05 66 39 de 75 27"   # cmp si,0x1b5; jne 4043b1; cmp si,bx; jne 4043d8

def encode_patch(key):
    """The replacement the DLL writes at 0x004043a5 (Python twin of core::encode_site)."""
    if not 1 <= key <= MAX_KEY or not key & 0xFFF:
        raise ValueError("key out of range: %r" % key)
    return bytes([0x66, 0x81, 0xFE, key & 0xFF, key >> 8, 0x75, 0x05, 0x66, 0x39, 0xDE, 0x75, 0x27])

def parse_install_line(line):
    """The DLL's one pause_key_only line as a dict, or None."""
    m = INSTALL_RE.search(line)
    if not m:
        return None
    g = m.groupdict()
    return {"patched": g["patched"] == "1", "key": int(g["key"], 16), "reason": g["reason"], "requested": g["requested"] == "1",
            "site": int(g["site"], 16), "write": g["write"]}

DLL_HARNESS = r"""
#include "pause_key_only_core.h"
#include <cstdio>
#include <cstdlib>
using namespace x3m::pause_key_only::core;
static void hex(const unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; ++i) std::printf("%02x", p[i]); std::printf("\n"); }
int main(int argc, char** argv) {
    hex(window, window_length); hex(original, site_length);
    std::printf("%lx %lx %u\n", (unsigned long)window_va, (unsigned long)site_va, site_offset);
    for (int i = 1; i < argc; ++i) { unsigned char out[site_length]; const char* r = plan(window, std::uint32_t(std::strtoul(argv[i], nullptr, 0)), out); if (r) std::printf("%s\n", r); else hex(out, site_length); }
    return 0;
}
"""

def dll_core(keys):
    """Compiles the production core with the host compiler; returns (window, original, (window_va, site_va, offset), {key: bytes|reason})."""
    compiler = shutil.which("clang++") or shutil.which("c++")
    if not compiler:
        return None
    with tempfile.TemporaryDirectory(prefix="x3-pause-core-") as d:
        src, exe = Path(d) / "h.cpp", Path(d) / "h"
        src.write_text(DLL_HARNESS)
        b = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(CORE.parent), str(src), "-o", str(exe)], capture_output=True, text=True)
        if b.returncode:
            return None
        r = subprocess.run([str(exe)] + [hex(k) for k in keys], capture_output=True, text=True)
    lines = r.stdout.split()
    if r.returncode or len(lines) != 5 + len(keys):
        return None
    wva, sva, off = int(lines[2], 16), int(lines[3], 16), int(lines[4])
    out = {}
    for k, v in zip(keys, lines[5:]):
        out[k] = bytes.fromhex(v) if re.fullmatch(r"[0-9a-f]+", v) else v
    return bytes.fromhex(lines[0]), bytes.fromhex(lines[1]), (wva, sva, off), out

def decode_at(raw, va):
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(raw); p = f.name
    d = subprocess.run([OBJDUMP, "-D", "-b", "binary", "-m", "i386", "-M", "intel", "--adjust-vma=" + hex(va), p], capture_output=True, text=True).stdout
    os.unlink(p)
    rows = [l for l in d.splitlines() if re.match(r"^\s+[0-9a-f]+:\t", l)]
    return [hex(int(l.split(":")[0], 16)) for l in rows], [" ".join(l.split("\t")[-1].split()) for l in rows]

def sections(b):
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    n = struct.unpack_from("<H", b, pe + 6)[0]
    opt = struct.unpack_from("<H", b, pe + 20)[0]
    out = []
    for i in range(n):
        o = pe + 24 + opt + 40 * i
        vs, va, rs, ra = struct.unpack_from("<IIII", b, o + 8)
        out.append((b[o:o + 8].rstrip(b"\0").decode(), va + 0x400000, vs, ra, rs))
    return out

def main():
    args = [a for a in sys.argv[1:]]
    out_json = None
    if "--json" in args:
        i = args.index("--json"); out_json = args[i + 1]; del args[i:i + 2]
    keys = [DEFAULT_KEY]
    while "--key" in args:
        i = args.index("--key"); keys.append(int(args[i + 1], 0)); del args[i:i + 2]
    exe = args[0] if args else DEFAULT
    b = open(exe, "rb").read()
    secs = sections(b)
    def rd(va, n):
        for _, sva, vs, ra, rs in secs:
            if sva <= va < sva + rs:
                return b[va - sva + ra: va - sva + ra + n]
        return None
    res = {"exe": os.path.basename(exe), "sha256": hashlib.sha256(b).hexdigest()}
    res["sha256_ok"] = res["sha256"] == EXE_SHA
    res["sites"] = {k: rd(va, len(bytes.fromhex(h))) == bytes.fromhex(h) for k, (va, h) in SITES.items()}

    # Native command dispatcher 0x00406de0: case = param-3 via byte map 0x004077a8, dword table 0x0040770c.
    def native_case(case):
        idx = rd(0x004077A8 + case - 3, 1)[0]
        return struct.unpack("<I", rd(0x0040770C + idx * 4, 4))[0]
    def cstr(va):
        return rd(va, 64).split(b"\0")[0].decode()
    names = [cstr(struct.unpack("<I", rd(0x0057C8F0 + 4 * i, 4))[0]) for i in range(0x3D)]
    res["native9_name"] = names[9]
    res["native9_target"] = hex(native_case(9))
    res["native3_name"], res["native3_target"] = names[3], hex(native_case(3))

    # Key converter 0x004d6850: eax-1 indexes byte map 0x004d6e14, dword table 0x004d6bd0.
    def dik(code):
        idx = rd(0x004D6E14 + code - 1, 1)[0]
        tgt = struct.unpack("<I", rd(0x004D6BD0 + idx * 4, 4))[0]
        ins = rd(tgt, 5)
        return hex(struct.unpack("<I", ins[1:5])[0]) if ins[0] == 0xB8 else "none(" + hex(tgt) + ")"
    res["dik_map"] = {"DIK_PAUSE(0xc5)": dik(0xC5), "DIK_LMENU(0x38)": dik(0x38), "DIK_RMENU(0xb8)": dik(0xB8),
                      "DIK_LWIN(0xdb)": dik(0xDB), "DIK_RWIN(0xdc)": dik(0xDC), "DIK_ESCAPE(0x01)": dik(0x01),
                      "DIK_TAB(0x0f)": dik(0x0F)}

    # Every instruction with a +0x4a0 displacement on the game-object pointer, and all writers.
    text = [s for s in secs if s[0] == ".text"][0]
    dis = subprocess.run([OBJDUMP, "-d", "-M", "intel", "--no-show-raw-insn", exe], capture_output=True, text=True).stdout
    rows = [l for l in dis.splitlines() if re.search(r"\[(e[a-d]x|e[sd]i|ebp)\+0x4a0\]", l)]
    res["reg_plus_0x4a0_insns"] = [l.split(":")[0].strip() + " " + " ".join(l.split()[1:]) for l in rows]
    # Only 0x004019c0 (init), 0x00404280 and 0x00406de0 write the word; bit 0 is cleared only at 0x004043e3.
    res["bit0_clear_sites"] = [l.split(":")[0].strip() for l in dis.splitlines() if re.search(r"and\s+e[a-d]x,0xfffffffe$", l) and 0x404280 <= int(l.split(":")[0], 16) < 0x404422]

    # Gap-free decode of 0x00404280..0x00404422 (instruction boundaries).
    d = subprocess.run([OBJDUMP, "-d", "-M", "intel", "--start-address=0x404280", "--stop-address=0x404422", exe], capture_output=True, text=True).stdout
    starts = [int(m.group(1), 16) for m in re.finditer(r"^\s+([0-9a-f]+):", d, re.M)]
    res["wait_fn_insn_count"] = len(starts)
    res["wait_fn_bad_opcode"] = "(bad)" in d
    res["patch_boundaries_orig"] = [hex(s) for s in starts if PATCH_VA <= s < PATCH_VA + 12]

    # Raw sweep: any rel8/rel32 branch encoding anywhere in .text landing strictly inside the patch span.
    _, tva, tvs, tra, trs = text
    t = b[tra: tra + min(tvs, trs)]
    lo, hi = PATCH_VA + 1, PATCH_VA + 12
    hits = []
    for i in range(len(t) - 6):
        va, op = tva + i, t[i]
        if op in (0xE8, 0xE9):
            tg = va + 5 + struct.unpack_from("<i", t, i + 1)[0]
        elif op == 0x0F and 0x80 <= t[i + 1] <= 0x8F:
            tg = va + 6 + struct.unpack_from("<i", t, i + 2)[0]
        elif 0x70 <= op <= 0x7F or op == 0xEB or 0xE0 <= op <= 0xE3:
            tg = va + 2 + struct.unpack_from("<b", t, i + 1)[0]
        else:
            continue
        if lo <= tg < hi:
            hits.append(hex(va))
    res["raw_branch_encodings_into_patch_interior"] = hits
    res["aligned_dword_refs_into_patch_span"] = sum(len(re.findall(re.escape(struct.pack("<I", v)), b)) for v in range(PATCH_VA, PATCH_VA + 12))

    # Decode the proposed replacement at its VA.
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(bytes.fromhex(PATCH_NEW)); p = f.name
    d2 = subprocess.run([OBJDUMP, "-D", "-b", "binary", "-m", "i386", "-M", "intel", "--adjust-vma=" + hex(PATCH_VA), p], capture_output=True, text=True).stdout
    os.unlink(p)
    res["patch_new_decode"] = [" ".join(l.split("\t")[-1].split()) for l in d2.splitlines() if re.match(r"^\s+[0-9a-f]+:\t", l)]
    res["patch_old_matches"] = rd(PATCH_VA, 12) == bytes.fromhex(PATCH_OLD)
    res["patch_new_boundaries"] = [hex(int(m.group(1), 16)) for m in re.finditer(r"^\s+([0-9a-f]+):", d2, re.M)]

    # The bytes the DLL writes: the production core, compiled, against the image and the decoder.
    core = dll_core(keys)
    dll = {"core_compiled": core is not None}
    if core:
        window, original, (wva, sva, off), written = core
        dll["window_matches_image"] = window == rd(WINDOW_VA, WINDOW_LEN) and len(window) == WINDOW_LEN and wva == WINDOW_VA
        dll["original_matches_note"] = original == bytes.fromhex(PATCH_OLD) and sva == PATCH_VA and wva + off == PATCH_VA
        dll["default_key_bytes_match_note"] = written.get(DEFAULT_KEY) == bytes.fromhex(PATCH_NEW)
        dll["keys"] = {}
        for k in keys:
            w = written.get(k)
            if not isinstance(w, bytes):
                dll["keys"][hex(k)] = {"refused": w, "ok": False}
                continue
            starts, insns = decode_at(w, PATCH_VA)
            dll["keys"][hex(k)] = {"bytes": w.hex(" "), "python_twin": w == encode_patch(k), "boundaries": starts, "decode": insns,
                                   "ok": w == encode_patch(k) and starts == ["0x4043a5", "0x4043aa", "0x4043ac", "0x4043af"]
                                   and insns == ["cmp si," + hex(k), "jne 0x4043b1", "cmp si,bx", "jne 0x4043d8"]}
    res["dll_patch"] = dll
    dll_ok = (core is not None and dll["window_matches_image"] and dll["original_matches_note"] and dll["default_key_bytes_match_note"]
              and all(v.get("ok") for v in dll["keys"].values()))

    ok = (dll_ok and res["sha256_ok"] and all(res["sites"].values()) and res["native9_name"] == "X2_SetPause"
          and res["native9_target"] == "0x40705d" and res["dik_map"]["DIK_PAUSE(0xc5)"] == "0x1b5"
          and res["bit0_clear_sites"] == ["4043e3"] and not res["wait_fn_bad_opcode"]
          and not hits and res["aligned_dword_refs_into_patch_span"] == 0 and res["patch_old_matches"]
          and res["patch_new_boundaries"] == ["0x4043a5", "0x4043aa", "0x4043ac", "0x4043af"])
    res["verdict"] = "PASS" if ok else "FAIL"
    s = json.dumps(res, indent=1)
    if out_json:
        open(out_json, "w").write(s + "\n")
    print(s)
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
