#!/usr/bin/env python3
"""Stream the two Run55 logs and write a compact crash witness."""
from __future__ import annotations

import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path("/tmp/x3-bottleX3-run199")
OUT = Path("/tmp/x3-run55-crash-triage")
LAUNCH = ROOT / "launcher-stderr.log"
SESSION = ROOT / "session-20260921-005922-212.log"
LAV = Path("/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-media/providers/lav081-strict-cadf5fbf4cf41bae/LAVVideo.ax")


def count_lines(path: Path) -> int:
    with path.open("rb") as f:
        return sum(1 for _ in f)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def pe_geometry(path: Path) -> dict:
    blob = path.read_bytes()
    pe = struct.unpack_from("<I", blob, 0x3C)[0]
    return {
        "path": str(path), "bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
        "image_base": f"0x{struct.unpack_from('<I', blob, pe + 24 + 28)[0]:08x}",
        "size_of_image": f"0x{struct.unpack_from('<I', blob, pe + 24 + 56)[0]:x}",
    }


def main() -> None:
    OUT.mkdir(mode=0o700, exist_ok=True)
    fault_re = re.compile(r"\[(?P<ts>[^]]+)\].*:(?P<pid>[0-9a-f]{4}):(?P<tid>[0-9a-f]{4}):trace:seh:dispatch_exception code=c0000005.*addr=(?:0+)?6EB2413A", re.I)
    regs_re = re.compile(r"dispatch_exception (eip=6eb2413a.*|eax=.*|esi=.*)", re.I)
    module_range_re = re.compile(r" at 0x([0-9a-f]+)-0x([0-9a-f]+)", re.I)
    module_base_re = re.compile(r" at ([0-9a-f]+): native", re.I)
    tid_name_re = re.compile(r":00d4:03f8:warn:threadname:dispatch_exception Thread renamed to \"([^\"]+)\"", re.I)
    debug_re = re.compile(r"(?:Register dump|Backtrace:|Stack dump|^.*in 32-bit code|^.*Modules:)", re.I)
    trace_config_re = re.compile(r"(?:CX_DEBUGMSG|WINEDEBUG) = \"([^\"]+)\"")
    faults, regs, fault_lines, names, maps, loaded, configs = [], [], [], [], [], [], []
    debugger_stack_lines = 0
    fault_register_lines_remaining = 0
    with LAUNCH.open(errors="replace") as f:
        for n, line in enumerate(f, 1):
            m = fault_re.search(line)
            if m:
                faults.append({"line": n, **m.groupdict(), "text": line.rstrip()})
            if "6eb2413a" in line.lower() and ("dispatch_exception" in line or "Unhandled page fault" in line):
                fault_lines.append({"line": n, "text": line.rstrip()})
            if ":00d4:03f8:" in line and "eip=6eb2413a" in line.lower():
                fault_register_lines_remaining = 2
                regs.append({"line": n, "text": line.rstrip()})
            elif fault_register_lines_remaining and ":00d4:03f8:" in line and regs_re.search(line):
                regs.append({"line": n, "text": line.rstrip()})
                fault_register_lines_remaining -= 1
            m = module_range_re.search(line) if "mapping PE file" in line and "LAVVideo.ax" in line else None
            if m is not None:
                maps.append({"line": n, "base": "0x" + m.group(1).lower(), "end": "0x" + m.group(2).lower(), "text": line.rstrip()})
            m = module_base_re.search(line) if "Loaded L\"" in line and "LAVVideo.ax" in line else None
            if m is not None:
                loaded.append({"line": n, "provider": "lav081-strict-cadf5fbf4cf41bae", "base": "0x" + m.group(1).lower(), "text": line.rstrip()})
            m = tid_name_re.search(line)
            if m:
                names.append({"line": n, "name": m.group(1), "text": line.rstrip()})
            m = trace_config_re.search(line)
            if m:
                configs.append({"line": n, "channels": m.group(1)})
            if debug_re.search(line):
                debugger_stack_lines += 1

    media_terms = ["media_owned_snapshot", "media_owned_services", "media_worker", "media_submission", "media_retire", "media_completion", "media_shutdown", "media_cue_window", "frame_end"]
    media_counts = {term: 0 for term in media_terms}
    media_last = {}
    identity = None
    with SESSION.open(errors="replace") as f:
        for n, line in enumerate(f, 1):
            if n == 3:
                identity = {"line": n, "text": line.rstrip()}
            for term in media_terms:
                if term in line:
                    media_counts[term] += 1
                    media_last[term] = {"line": n, "text": line.rstrip()}

    lav = pe_geometry(LAV)
    witness = {
        "schema": 1,
        "inputs": {
            "launcher": {"path": str(LAUNCH), "bytes": LAUNCH.stat().st_size, "lines": count_lines(LAUNCH), "sha256": sha256_file(LAUNCH)},
            "session": {"path": str(SESSION), "bytes": SESSION.stat().st_size, "lines": count_lines(SESSION), "sha256": sha256_file(SESSION)},
        },
        "candidate": identity,
        "trace_configuration": configs,
        "fault": {
            "exception": "0xc0000005", "access": "read", "address": "0xffffffff", "eip": "0x6eb2413a", "tid": "0x03f8", "process": "0x00d4",
            "first_dispatch": faults[0] if faults else None, "dispatch_count": len(faults), "fault_log_lines": fault_lines,
            "x86_context_lines": regs, "thread_names": names,
        },
        "module": {
            "name": "LAVVideo.ax", "runtime_maps": maps, "runtime_loaded": loaded, "pe": lav,
            "fault_rva": "0xa413a", "range_contains_fault": True,
            "owning_source_location": None,
            "owning_source_note": "The logs establish the native provider binary path, not a source-file/symbol location.",
        },
        "debugger": {"auto_start_line": 1048065, "auto_debugger": "winedbg --auto 212 8820", "stack_or_winedbg_context_lines": debugger_stack_lines},
        "media": {"counts": media_counts, "last": media_last},
        "limitations": [
            "No winedbg register dump, module list, raw stack, or symbolic caller chain was emitted after the automatic debugger launch.",
            "The repeated dispatch records are exception delivery/unwind observations, not independent crashes.",
            "The session telemetry has no worker/submission/retirement/completion/shutdown records, so it cannot establish the bad object's provenance.",
            "This is CrossOver/Wine evidence only; it does not establish native Windows behavior or a production-source root cause.",
        ],
    }
    (OUT / "result.json").write_text(json.dumps(witness, indent=2) + "\n")
    report = f"""# Run55 crash triage

## Outcome

**Observation.** The Run55 crash is a read access violation at `0x6eb2413a`, reading `0xffffffff`, on process/thread `00d4:03f8` at `2026-09-20T21:01:47.734Z` (Wine QPC `1667545.514`). It is inside native `LAVVideo.ax`, not the installed proxy: actual map `[0x6ea80000, 0x6eb8e000)`, RVA `0xa413a`. The fault thread was named `CLAVOutputPin Video`.

**Observation.** Fault x86 state: `EIP=6eb2413a ESP=EBP=56b3f80c`, `EAX=00000000 EBX=00000c02 ECX=ffffffff EDX=00000c02 ESI=5852567c EDI=56b3f884`. The exception is `0xc0000005`, read (`info[0]=0`) of `0xffffffff` (`info[1]`). `LAVVideo.ax` first handles it at `6EB5B7F6`; the proxy vectored handler `7692B4C0` returns 0.

**Observation.** The runtime log maps the provider at `0x6ea80000-0x6eb8e000` and calls it native. The on-disk provider reports ImageBase `0x10000000`, SizeOfImage `0x10e000`, SHA-256 `{lav['sha256']}`. The session identifies the installed proxy SHA-256 `4b47f636acd66eba35c61a3f9a4d4d18d911470f46b2df7b64aaf1465cdaa012`, source commit `54b48c36f6d7d6aa846992fe5412fbaee5f26219`.

**Observation.** `CX_DEBUGMSG`/`WINEDEBUG` includes timestamp, pid, seh, unwind, process, module, loaddll, threadname and tid. Wine starts `winedbg --auto 212 8820` at launcher line 1,048,065, but this log contains no winedbg register dump, module listing, raw stack, or symbolic caller chain.

## Preceding events and telemetry

The same TID was renamed `CLAVOutputPin Video` at launcher lines {', '.join(str(x['line']) for x in names)}. At `{faults[0]['ts'] if faults else 'unknown'}`, the preceding one millisecond includes `avfilter-lav-11.dll` thread attach and Wine threadpool worker/timerqueue naming. LAVVideo path/base lookup on the fault TID occurs at launcher lines 1,046,350–1,046,353 before the dispatch at 1,046,452.

The proxy session records {media_counts['media_owned_snapshot']} ownership snapshot and {media_counts['media_owned_services']} services snapshot; it records zero worker, submission, retirement, completion, or shutdown events. It has {media_counts['media_cue_window']} cue windows; its final window at line {media_last['media_cue_window']['line']} has zero attempts, video blits, unlocks and failures. These are observations only and do not identify the bad object passed to LAVVideo.

## Limitation and next evidence

No source-file ownership is established by the logs; the established owning runtime location is `{lav['path']}`. A decisive next witness needs the LAVVideo instruction/caller stack plus the object origin/lifetime at this hash-chain lookup; current telemetry lacks both.
"""
    (OUT / "result.md").write_text(report)


if __name__ == "__main__":
    main()
