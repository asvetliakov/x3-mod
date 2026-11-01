# FL scaler overflow fix

**Bug.** X3FL.exe's three-component fixed-point scaler at RVA 0x12d10 (`T(n,d)`,
[gate-motion.md](../reverse-engineering/fl/gate-motion.md) section 2) computes `out[i] = c ? (int64)a[i] * b / c : 0`
with `imul edx; idiv ecx`; a quotient outside int32 makes the `idiv` raise c0000095. FL run 3
([ledger](../verification/farnhams-legacy.md), "FL run 3"; `verification/results/fl-run3-crash/`) crashed at
RVA 0x12dbf, the third `idiv`: the cockpit update (0x22d0a–0x22dd4) computes camera velocity Δ × 1000 / dt with
Δ = (1366700, −692544, 4460464), dt = 2, so z = 2,230,232,000 > INT32_MAX.

**ABI** (`tools/analysis/fl_scale_overflow_constants.py`; capstone witness
`verification/results/fl-scale-overflow/abi_witness.py`): ECX = `const int a[3]`, EDX = `const int *b`,
`[esp+4]` = `int out[3]`, `[esp+8]` = `const int *c`; plain RET, caller cleanup; EAX = out. EBX untouched,
ESI/EDI/EBP restored; on exit ECX = c, EDX = the last remainder (ECX = a[2], EDX = 0 when c == 0). c, b, a[i] are
re-read per component; component 2's b is read before out[1] is stored. The eleven direct callers (0x170f6,
0x22dd4, 0x22e2a, 0x4fc00, 0x4fd07, 0x64075, 0x64f3d, 0x84805, 0x84870, 0xa4d55, 0xa4da5) form ECX/EDX as pointers,
push &c then out, write EFLAGS before reading it and load the result through `[eax]`. No base relocation in the
0xbc-byte body, no relocated word into it, no E9 to it; the rel8/rel32 byte scan into its interior finds only
0x12ddc, the low byte of the relocated imm32 of `push 0x54cae0` at 0x12ddb.

**Fix** (`src/engine/fl/scale_overflow_fix_fl.cpp`, FL only). Gate: `fl::verified()`, the 16 entry bytes and an
FNV-1a of the whole body (generated `scale_overflow_fix_fl_inc.h`); then install window, pin, `engine_patch::claim`
(6 displaced bytes, 5-byte entry JMP, one `lock cmpxchg8b`, rollback) and `push_front` of the replacement (a failed
head store restores the entry; the tail is the original). A naked stub plus a cdecl body reproduce the helper's
reads, writes, EAX, ECX and EDX whenever it does not trap; otherwise the quotient is clamped to INT32_MAX/INT32_MIN
by sign (`|a·b| ≥ (limit+1)·|c|` in 64-bit integers; fitting quotients use the same `idiv`, so truncation toward
zero is the engine's). Built `-fno-exceptions -mno-sse -mno-mmx -mfpmath=387`: no x87, XMM or libgcc call per call
(`cpu_state_audit.py`); the QPC check every 256 calls holds an XmmSave and keeps LastError, the row runs under
`call_preserved`.

**Per-caller safety of a clamped result.** 0x22dd4/0xa4d55 store it as a velocity at object +0x100,
0x22e2a/0xa4da5 at object +0x350; 0x170f6, 0x64075, 0x64f3d, 0x84805 and 0x84870 add it to a position with a
plain `add ecx,eax` (wraps, never traps); 0x4fc00/0x4fd07 pass it to 0x12b80 (0x4fc00 then converts with
`cvtdq2ps`). None uses it as an index or size within the call windows read; later consumers are not traced.
**Open:** the sibling 16.16 divide ending at RVA 0x12d08 (`shl/sar 16; idiv; ret 8`) has the same trap class
and is not patched.

**Why the helper, not the caller.** The overflow belongs to the helper and is reachable from all eleven callers
(gate-motion position integration, camera velocity, the 0x4fc00/0x84805 families). One entry patch under a
whole-body hash covers them all without per-caller frame knowledge and changes no non-trapping result; a guard at
0x22d0a would fix one caller and need its own ABI proof.

**Settings and rows.** `fl_scale_overflow_fix` (`X3M_FL_SCALE_OVERFLOW_FIX`, DLL default on, `0` off),
`fl_scale_overflow_log` (0..3600 s, default 10, 0 off). Once: `fl.scale_fix status=active base=0x… site=0x…
entry_ok=1 body_ok=1 write=atomic`, or a refusal (`disabled`, `executable_mismatch`, `entry_mismatch`,
`body_mismatch`, `late_claim`, `chain_failed`, an engine_patch status). At the first elapsed period (the
call rate, even `saturated=0`), then when a period elapsed and the clamp count changed:
`fl.scale_fix_sample calls=N saturated=M last_site=0x<RVA>` (M clamped components since install;
`last_site` the call of the last clamp, 0x22dd4 for the camera velocity). Evidence: `verification/results/fl-scale-overflow/`, `verification/analysis/test_fl_scale_overflow.py` (host, native via WSL interop); not yet run in a game.
