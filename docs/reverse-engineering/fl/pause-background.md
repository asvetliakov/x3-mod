# Farnham's Legacy pause-key and background policy sites

2026-10-29. Bounded offline reconstruction of clean
`C:\X3FL-orig\X3FL.exe` (`/mnt/c/X3FL-orig/X3FL.exe`), SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
This establishes FL equivalents of the AP contracts in
[pause-dialog-input.md](../pause-dialog-input.md) and
[run-in-background.md](../run-in-background.md). No production code was changed;
no game, Wine or native execution occurred. The candidate rewrites below are
offline proposals, not implemented or game-qualified features.

## Recommendation and edition boundary

Share configuration/key validation and desired behavior. Keep byte encoders,
site fingerprints, relocation handling and installation in edition adapters.
Resolve FL addresses as module base plus RVA, using the existing profile and
[post-cookie startup owner](../../architecture/farnhams-legacy-install-boundary-decision.md).
Do not transplant AP absolute addresses, its 12-byte pause encoder or its
background CALL thunk into FL.

The simplest FL implementation uses two inline changes installed during that
existing startup transaction:

| Feature, enabled setting only | Preferred VA / RVA | Original behavior | Candidate |
| --- | --- | --- | --- |
| Pause key only | `0x47880c` / `0x7880c`, 13 bytes | Exit on any changed nonzero key, comparing its low 12 bits | Compare SI with configured key, then ESI with the previous zero-extended key DWORD; exit only when both tests allow it |
| Run in background | `0x4776ec` / `0x776ec`, 2 bytes | JE to the native clear-bit instruction when argument/registry choice is zero | Replace JE with two NOPs; execute the existing OR-bit path for every choice |

With either setting off, leave its bytes native. In particular,
`run_in_background=0` does **not** clear the bit: command-line and registry
choice remain authoritative, exactly as with the AP setting off. With it on,
`-noruninbg` is overridden at initialization, as in AP. The later native
`X2_SetRunInBackground` still changes the live bit and registry normally.

No runtime policy callback, input pointer cache, polling, worker, allocation or
new installation mechanism is needed. Keep the current pinned startup ownership
and refuse late attachment. Ordinary rollback belongs before startup publication;
do not invent a live shutdown writer for these multi-instruction patches.

## 1. Pause entry, key loop and button behavior

The FL game context is `*0x6243cc`, with the same `+0x4a0` flag word as AP:
bit 0 paused, bit 1 swallow the key ending pause, bit 2 save requested.
Constructor `0x475e70` zeroes this field at `0x475ec9`. The native name table
at `0x5a5d60` contains index 9 → name `X2_SetPause` at `0x58bd54`.
Dispatcher `0x47b560` loads EDX=1 at `0x47b569`, loads context into ESI,
and dispatches `index-3` through byte table `0x47bedc` and DWORD table
`0x47be4c`. Index 9 resolves directly to `0x47b7c4`, which ORs EDX into
`[esi+0x4a0]` and calls sound-silencing helper `0x4b4210`.
Thus the register operand is still a constant bit-0 setter; it is not a new
script argument or toggle. Native registration is at `0x4778d3..0x4778dd`.
The whole-text direct-offset census finds eleven non-stack `+0x4a0` uses:
constructor initialization, main-loop tests/save-bit clear, input/swallow and
pause-exit operations, and save/pause native setters. Within that measured
census, the wait exit is the sole bit-0 clearer after construction.

In-flight input function `0x478690..0x4788aa` has one measured direct caller,
`0x478150`. It takes no stack arguments, aligns its own stack to eight bytes,
saves EBX/EBP/ESI/EDI and returns with plain RET. Its internal loop deliberately
reuses EBP as a button snapshot, so an adapter must not assume EBP remains a
frame pointer there. Entry loads previous key from word `0x62ddf0` into EBX
and saves it as DWORD `[esp+0x14]` at `0x4786a4`. Key reader `0x4eccd0`
returns AX; MOVZX into ESI at `0x4786b3` establishes the zero-extended key.
Test of paused bit 0 at `0x4786c3` branches to `0x4787ee`.

| FL address | Established instruction/dataflow |
| --- | --- |
| `0x4787ee..0x478806` | Load input `*0x5a8478`; ECX/EBX snapshot `+0x454` mouse buttons, EBP/EDI snapshot `+0x450` joystick buttons; save current mouse word at `[esp+0x10]` |
| `0x478807`, `0x47880a` | TEST SI,SI; JE `0x478819` when no key |
| `0x47880c`, `0x47880e`, `0x478812`, `0x478817` | MOV EAX,ESI; XOR EAX,[esp+0x14]; TEST EAX,0xfff; JNE `0x47885e` |
| `0x478819..0x47881f` | Mouse rising-bit test `(ECX \| EBX) != EBX`; JNE `0x47885e` |
| `0x478821..0x478827` | **Additional FL joystick test** `(EBP \| EDI) != EDI`; JNE `0x47885e` |
| `0x478829` | CALL pump `0x4ec620`, which polls input before processing messages |
| `0x47882e..0x47884c` | Reload input pointer/buttons, advance prior snapshots, save MOVZX(SI) at `[esp+0x14]` |
| `0x478850..0x47885c` | CALL key reader; MOVZX ESI,AX; JMP `0x478807` |
| `0x47885e..0x478875` | Reload context; write `[context+0x4a0] = (old & ~1) \| 2` |
| `0x478876..0x4788aa` | Clock resync, publish snapshots and key, restore frame and return 1 |

FL therefore preserves AP's any-mouse-button-anywhere behavior and adds a
joystick-button exit. There is no coordinate, UI object or button hit test
between the key comparison and either exit. Calling this the visible box's
button is UI shorthand; changing hit testing would require a separate feature.
`+0x450` is supported by `joyGetPos` IAT `0x55030c`, called in the input poll
at `0x4ec18c/0x4ec1a0`, and the merge into `+0x450` at `0x4ec1bb`.
The DirectInput joystick state path also assembles its button mask there
(`0x4ef5e9..0x4ef65a`). Preserve both existing tests.

Key conversion `0x4efb70` dispatches DIK `0xc5` through byte table `0x4f0134`
and pointer table `0x4efef0` to `0x4efe4f`, returning **0x1b5**. The key reader
ORs 0x1000 for Shift (`0x4ecd83..0x4ecd93`). The default exact key is therefore
0x1b5; Shift+Pause requires configured 0x11b5, matching AP's exact-key policy.
The no-DirectInput fallback maps VK_PAUSE 0x13 through `0x4ecec4/0x4ece64` to
`0x4ecdcb`, returning 0x100, also shared with other virtual keys. The default
DirectInput code cannot distinguish that fallback; retain the configurable key
and document the limitation rather than inventing a second input system.

The proposed 13-byte encoding is:
`66 81 fe KK KK 75 06 3b 74 24 14 75 45` — CMP SI,KEY;
JNE `0x478819`; CMP ESI,[ESP+0x14]; JNE `0x47885e`.
Both ESI and the stored prior key are zero-extended 16-bit values, so the
DWORD comparison implements the same exact-key policy without another byte.
Do not use AP's BX comparison: FL has repurposed EBX as the mouse snapshot.

EAX is dead on both successors: the mouse successor first MOVs EAX,EBX;
the pause exit MOVs EAX from the context flag word before using it. ECX is
**live** for the mouse test and is untouched by this proposal. ESI, all other
registers, ESP and stack memory remain unchanged. The proposal's arithmetic
flags are consumed by its own conditional branches, then overwritten by mouse
OR/CMP or the exit's AND/OR before another consumer. DF/control flags remain
unchanged. There are no added calls, LastError writes or new faulting addresses;
the stack operand is the same already used by the original comparison.

Activation remains separate: WM_ACTIVATE calls `0x4edb50` at `0x4ec80d`,
sets input `+0x484` and global active `0x5aaf34`; WM_ACTIVATEAPP calls the same
helper at `0x4ec867`. That helper clears keyboard state, mouse and joystick
words, unacquires/reacquires devices, and flushes keyboard events. These sites
do not write the context pause bit. Rejecting Alt/Windows keys removes those
key exits; a fresh mouse/joystick bit can still exit after focus changes.
No claim of complete alt-tab behavior or cursor acceptance follows from static
code. The box drawing and script key-to-native binding were not re-traced in
FL story bytecode; the EXE native and actual waiting loop are established.

## 2. Background argument, registry and input lifetime

Startup `0x476bc0` initializes `[ebp+0x60]` to -1 at `0x476bfd`.
The ascending argument loop begins `0x476cf0`, increments at `0x477252`,
and loops at `0x477259`; later matching arguments overwrite earlier choices.
Each alias is compared with CRT case-insensitive comparator `0x53744d`:

| String / address | Compare's JE | Store reached |
| --- | --- | --- |
| `/runinbg` / `0x58b854` | `0x477032` | `0x477124`: local = 1 |
| `-runinbg` / `0x58b860` | `0x47704a` | `0x477124`: local = 1 |
| `/noruninbg` / `0x58b86c` | `0x477062` | `0x477118`: local = 0 |
| `-noruninbg` / `0x58b878` | `0x47707a` | `0x477118`: local = 0 |

The complete startup decode has exactly six named uses of this local:
initializer, those two stores, read `0x4776c7`, registry-result store
`0x4776df`, reread `0x4776e7`. Input initialization at `0x4776b1` precedes
the background decision at `0x4776c2..0x477700`:

1. Load input in EAX; OR `[eax]` with 0x1000; compare choice with -1.
2. If -1, call `0x4d8850` with ECX=`0x58b9e8` ("RunInBackground"); store
   result in the local, reload input and choice. This helper takes its name
   in ECX, returns DWORD EAX and takes no stack arguments. It formats
   `Software\EGOSOFT\%s\Values`, opens HKCU with KEY_READ, queries the DWORD
   and closes the key on its successful path. The failure paths return 0;
   this helper performs no registry write.
3. TEST ECX,ECX at `0x4776ea`; JE `0x4776ec` → `0x4776f6` clears bit 0x4000.
   Otherwise OR `[eax],0x4000` at `0x4776ee` and JMP `0x4776f4` → `0x4776fc`.
4. CALL `0x500590` at `0x4776fc`, returning to `0x477701`. It takes no stack
   arguments and immediately CALLs `0x500480`, whose first SUB ESP,0x410
   overwrites arithmetic flags before any read of incoming flags.

Replacing only the JE with two NOPs forces the original OR path. Registers,
stack, return address, all call instructions and registry path remain native.
Only the intended input flag changes when choice was zero. Its derived
arithmetic flags have no consumer before `0x500480` overwrites them; DF is
unchanged. No handler runs, so no additional LastError preservation machinery
is needed. A hypothetical API-based installer must still obey the startup
owner's LastError and complete machine-state restoration contract.

The AP-equivalent CALL seam is `0x4776fc` → `0x500590` (one measured direct
caller); unlike AP, its five bytes and rel32 cross an aligned eight-byte word.
There is no reason to add a FL thunk or rely on the AP atomic-word claim when
the two-byte branch change expresses the same policy.

The input pointer is `*0x5a8478`, flags at +0, allocation size 0x4fc.
That slot and the context pointer slot are in the PE `.data` zero-fill tail;
they begin null before startup creates their allocations.
Initializer `0x4ebef0` takes seed flags in ECX, preserves them in EBX,
allocates at `0x4ebf05`, publishes at `0x4ebf0a`, and stores seed flags at
`0x4ebf1c`; failure returns null and startup takes fatal cleanup before the
decision. `InputFlags` registry reading at `0x4d7953..0x4d7969` populates
settings `[*0x5a83b0+8]`, passed into the initializer. Its bit 0x4000 is
then overwritten unconditionally by the argument/RunInBackground choice,
as in AP. Never cache the input pointer or write either settings or registry.
Normal destruction `0x4ebfd0` tears down devices, frees the input allocation
and zeroes the slot at `0x4ec004`; fatal cleanup has the equivalent free/zero
sequence `0x4769d3..0x476a10`. These process/input lifetimes are independent
of a D3D Reset. A persistent inline policy needs no input-lifetime observer.
The direct store census finds exactly the allocation publication and these
two zero stores; it is not an alias/computed-writer proof.

The unchanged bit consumers are message pump `0x4ec620`, testing at
`0x4ec645`, and media query `0x4ff580`, testing at `0x4ff598`.
The inactive-window/bit-clear pump blocks in GetMessageA until activation;
bit-set uses the nonblocking drain path. Native **0x25** (FL)
`X2_SetRunInBackground` resolves to `0x47bd7e`, OR/ANDs the bit at
`0x47bd94/0x47bda4`, then writes the registry via `0x4d8640`.
Native **0x26** `X2_IsRunInBackground` resolves to `0x47bdb6`, reading the
bit by SHR 14 and AND 1. Do not assume AP native indices. These paths remain
untouched, so a script can switch background processing off after startup.

## 3. Boundaries, ownership, relocation and failure paths

Measured whole-.text raw rel8/rel32 scans find no direct incoming branch to
either candidate span, including their first bytes. Both enter by normal
fall-through. No raw DWORD in the file names an interior candidate address;
no HIGHLOW relocation overlaps either span. The clear-bit instruction has
exactly one raw incoming edge, the JE proposed for removal. The existing
OR path joins the unchanged CALL at `0x4776fc`. These are file/direct-edge
facts; they do not prove absence of arbitrary computed jumps, foreign engine
writers or nonlocal exception transfers.

Both patch spans and surrounding reviewed regions have complete instruction
decodes. The reference decode from known `.text` entry `0x401000` validates
22 pairs of region/span endpoints. One explicit exception is key converter
entry `0x4efb70`: neighboring inline data desynchronizes that linear reference;
the real direct CALL at `0x4ecd7e` independently identifies its entry and its
complete selected prefix is checked. The verifier does not disguise that
linear-decode limitation as full CFG coverage.

FL supports relocation. The 63-byte background context window
`0x4776c2..0x477701` has HIGHLOWs at `0x4776c3/0x4776d6/0x4776e3`;
pause loop `0x4787ee..0x478876` has HIGHLOWs at
`0x4787ef/0x47882f/0x478860`. Candidate bytes and their relative branches
need no relocation. Normalize context fingerprints using their actual fixups,
then encode/resolve at the admitted module base. Positive offline rebases
to `0x01000000` and `0x07100000` each apply 38,327 HIGHLOW fixups and check
the relocated input operand, candidate invariance and branch destinations,
then normalize/recheck the reviewed regions. Injected candidate relocations
are independently refused.

Installation belongs after the cookie and before game CRT initialization,
through the current admitted initial static-load startup owner. At that
boundary neither startup decision nor pause loop has run; no engine/input
heap read is necessary. The later D3D probe is not an interchangeable
quiescence proof ([startup](startup.md)). The 13-byte pause
rewrite spans multiple atomic words; preserve existing exact-byte claims,
all-or-nothing group rollback, protection/cache/readback checks and refusal
of late writes. Runtime pause pumping can reenter the window procedure;
these inline changes add no reentrant callback or mutable policy state.
Preserve the engine's existing SEH and call/return paths. Unrecoverable
installer rollback follows existing startup-owner policy; no new exception
handler is justified. Pin/retain ownership for process lifetime and restore
only during qualified prepublication rollback. Inline runtime code has no
proxy target to dangle, but that does not authorize unsafe concurrent unpatch.

## 4. Reproduction, checks and performance limits

Producer: [verify_fl_pause_background.py](../../../verification/probe/verify_fl_pause_background.py).
Compact record and wrapper are under
`verification/results/fl-port/pause-background/{summary.json,reproduce.py}`;
raw disassembly stays in ignored `raw-disassembly-local.txt` and must not be
tracked. Run:

```sh
python3 verification/results/fl-port/pause-background/reproduce.py
# Optional private listings:
python3 verification/probe/verify_fl_pause_background.py --raw
```

Measured result: 21 reviewed regions, 1,864 distinct instructions, 63 semantic
anchors, 131,712 pause interpreter cases and 70 background cases; 16 negative
witnesses and two positive rebases. The fail-closed host interpreter consumes
Capstone-decoded original/candidate bytes and follows encoded branches; it
does not execute native game code. Cases cover all 8,192 key values in `0..0x1fff`
with four configurations/prior-key choices, unchanged registers/memory,
all eight mouse and all 32 joystick bits (rising/held/lost/absent), and
zero/nonzero signed argument or registry choices with both setting states.
Negatives bypass image/region hashes to test independent operand, branch,
button, native table/setter, conversion, direct-edge, interior-pointer,
relocation, boundary and invalid-key refusals. Summary binds the actual
producer SHA-256 and measured command duration. Hashes are evidence
provenance, not production private-DLL prerequisites.

Static performance pass: pause adds no instructions on the accepted-key
comparison path (four original versus four proposed), and unwanted nonzero
keys use two proposed comparison/branch instructions before the existing
mouse test, instead of four original instructions. Zero-key, button, polling
and input producer paths remain native. Background adds only two startup
NOPs and chooses an existing OR/JMP path, with no per-frame work. No new
allocation, lock, validation or LastError API occurs in either runtime path.
These instruction counts are measured from encodings; they are not game
timing or FPS claims. Native ABI/installer failure injection and user-run
FL pause, joystick, background, alt-tab and double-cursor acceptance remain
future implementation checks. No extra flight cycle is requested by this RE.

## Independent review — 2026-10-29

Independent review accepted this bounded RE and reproduced the offline semantic,
negative and relocation checks (13.078 seconds). An additional raw LOOP/JECXZ
incoming-edge scan found zero entries into either proposed span; the reused
branch scanner does not cover those encodings. This is source-byte evidence,
not native execution or live control-flow coverage. Production admission,
rollback and user runtime gates above remain open.
