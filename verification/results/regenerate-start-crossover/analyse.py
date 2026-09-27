"""Summarise the regenerate-start traces and the xtajit64 disassembly facts.
Usage: python3 analyse.py <trace>...   (traces are local, untracked)
Prints, per trace: FEX AppConfig paths opened, and whether c000001d was raised.
With no args, checks the xtajit64.dll instruction facts only."""
import re, subprocess, sys
from collections import Counter
DLL = "/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/wine/aarch64-windows/xtajit64.dll"
for path in sys.argv[1:]:
    text = open(path, errors="replace").read()
    names = Counter(re.findall(r'AppConfig/[^"]*', text))
    print(path.rsplit("/", 1)[-1], "trap" if "c000001d" in text else "no-trap", dict(names))
def dis(lo, hi):
    out = subprocess.run(["objdump", "-d", "--no-show-raw-insn", f"--start-address={lo:#x}",
                          f"--stop-address={hi:#x}", DLL], capture_output=True, text=True).stdout
    return [l.strip() for l in out.splitlines() if re.match(r"\s*18[0-9a-f]+:", l)]
base = 0x180000000
checks = {
    "1710e8 hlt #0x1 (FEX trap)": any("hlt" in l for l in dis(base + 0x1710e8, base + 0x1710ec)),
    "17a73c-17a744 invalid-JSON error then trap": [l.split(":", 1)[1].strip() for l in dis(base + 0x17a73c, base + 0x17a748)],
    "3058 operator delete of path buffer": dis(base + 0x3058, base + 0x305c),
    "30c4 memcpy of filename from freed buffer": dis(base + 0x30c4, base + 0x30c8),
}
for k, v in checks.items():
    print(k, "->", v)
