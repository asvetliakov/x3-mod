# Loading probes: entry-counting trampolines on the engine's loading functions

Probe batch 2 of [script-xml-load-stall.md](script-xml-load-stall.md) §7, implemented
in `src/proxy/loading_probes.cpp` (sites, stubs, report), `src/proxy/engine_patch.cpp`
(byte-verified patches and the code arena) and `src/proxy/loading_trace_light.cpp`
(the handlers). Gate: `X3M_TELEMETRY=1` plus `X3M_LOADING_PROBES=1`
(`tools/manage.py launch --telemetry --loading-probes`). Exact executable only
(`object_trace::executable_verified()`, SHA-256 `fdbf3418…`); every site is
byte-verified and fails closed on its own. Verification evidence and the log
format: [docs/verification/loading-probes.md](../verification/loading-probes.md).

## Sites

Bytes read from the installed `X3AP.exe` (`.text` file offset = VA − 0x401000 +
0x400) and checked against the Ghidra listings (`X3LoadingOrchestration.java
listing`) on 2026-09-12. `length` is the number of bytes displaced (whole
instructions, none of them a relative branch); `ret` is the callee's return form,
which the exit handler needs to find its shadow entry (`ret n` pops `n` bytes of
arguments above the return address).

| # | Name | VA | First bytes (displaced) | Length | ret | Kind | Extras `x0..x3` |
| ---: | --- | --- | --- | ---: | --- | --- | --- |
| 0 | `resource_load` | `0x004e8e10` | `55 8b ec 83 e4 f8` push ebp; mov ebp,esp; and esp,-8 | 6 | `ret` | timed | – |
| 1 | `resource_open` | `0x004e8780` | `53 8b 5c 24 08` push ebx; mov ebx,[esp+8] | 5 | `ret 4` | timed, ESI = file object at entry, `+0x04` after return | loose, catalogue, failed |
| 2 | `name_resolve` | `0x004e7590` | `6a ff 68 1d fa 52 00` push -1; push 0x52fa1d (SEH prolog) | 7 | `ret 0xc` (`c2 0c 00` at the end of the 4,538-byte body) | timed, AL = hit | hit, miss |
| 3 | `resource_read` | `0x004e8880` | `81 ec 54 04 00 00` sub esp,0x454 | 6 | `ret` | timed, EAX = file object at entry; `DAT_00596988` summed at exit | plain, catalogue, gzhandle_or_unopened, null_result |
| 4 | `read_dispatch` | `0x004e9210` | `f6 46 04 01 53` test byte [esi+4],1; push ebx | 5 | `ret 4` | count only (ECX·EAX bytes, branch by ESI flags) | plain, catalogue, gzhandle, unopened |
| 5 | `crt_fgetc` | `0x0050fff5` | `6a 0c 68 40 d0 56 00` push 0xc; push 0x56d040 | 7 | `ret` | count only | – |
| 6 | `sopen_helper` | `0x00527869` | `55 8b ec 83 ec 34` push ebp; mov ebp,esp; sub esp,0x34 | 6 | `leave; ret` | timed | – |
| 7 | `find_wrapper` | `0x004d2950` | `53 55 56 57 bd 38 01 00 00` push ebx/ebp/esi/edi; mov ebp,0x138 | 9 | `ret` | timed, bucketed by return address (8 slots) | – |
| 8 | `signature_check` | `0x004cabc0` | `83 ec 1c 53 8b 5c 24 34` sub esp,0x1c; push ebx; mov ebx,[esp+0x34] | 8 | `ret` | timed | – |
| 9 | `texture_body` | `0x004dd2c0` | `6a ff 68 50 fd 52 00` | 7 | `ret` | timed | – |
| 10 | `texture_loader` | `0x004dc540` | `6a ff 68 70 fd 52 00` | 7 | `ret` | timed | – |
| 11 | `mesh_body` | `0x004bc680` | `83 ec 10 53 55` sub esp,0x10; push ebx; push ebp | 5 | `ret` | timed | – |

The `test` at `0x004e9210` sets the flags that the `jne` at `0x004e921c` reads;
the entry stub restores EFLAGS (`popfd`) before the displaced `test` runs, and
the jump back is a plain `jmp`, so the original branch still sees them.

## Patch mechanics (`engine_patch`)

1. `claim(site, spec)`: the `length` bytes at the VA must equal `expected`
   (`bytes_mismatch` otherwise, nothing written). A **tail** block = the displaced
   bytes + `jmp VA+length`, an **entry word** initialised to the tail, and a
   **dispatcher** `jmp [entry]` (`FF 25`) are emitted into an 8 KB
   `VirtualAlloc` arena (PAGE_EXECUTE_READ except while emitting); then the
   site's first five bytes become `jmp dispatcher` (`E9 rel32`) with
   `VirtualProtect`/`FlushInstructionCache` and a rollback on failure, the same
   sequence as `scene_hook.cpp`.
2. `push_front(site, stub)`: sets the entry word to a generated stub and returns
   the previous head, which the stub stores as its continuation. A second hook
   on the same function (the resource reader on `0x004e8880`) chains the same
   way, so the order is site → dispatcher → probe stub → reader stub → tail.
3. `restore(site)`: original bytes back; the arena and the tails stay valid for a
   thread that is still inside a stub.

**Install window and the write itself (review 27).** Writing a jump over live
code is safe only while no other thread can be executing the patched bytes. All
production claims run on the backend-load path (`loader.cpp load_backend` →
`capture.cpp initialize_log` → `loading_trace::initialize` →
`loading_probes::initialize`, then `resource_reader::initialize`): the thread
that calls `Direct3DCreate9`, before the device exists and before the loading
it drives starts. `capture` closes the window at the first `Present`
(`engine_patch::close_install_window("first_present")`); every later `claim()`
or `claim_call()` is refused with status `late_claim` and the site log line
names the reason (`loading_probes … status=late_claim window_closed_by=`). As
defence in depth the five patch bytes are written with one `lock cmpxchg8b`
whenever they lie inside a single 8-byte-aligned word (`engine_patch::write_code`;
`loading_probe_site … atomic_write=1`): 11 of the 12 sites and the `_fclose`
call site qualify; `crt_fgetc` (`0x0050fff5`) and the `_fopen` call site
(`0x004e87ff`) straddle a qword boundary and take the plain copy, covered by the
window alone. `restore()` writes the same five bytes back the same way (the
displaced remainder was never modified). A failed post-write step rolls the
bytes back (`patch_rolled_back`); when even the rollback's `VirtualProtect`
fails the site stays registered as patched (`rollback_failed`,
`patched_in=true`) so `restore()` still tries at shutdown.

**Register dereferences in the handlers.** `resource_open` (ESI at entry,
`+0x04` after return), `resource_read` (EAX) and `read_dispatch` (ESI) read
the file object's flags word. A register value is dereferenced only when it is
4-aligned, in `0x10000..0x7fff0000`, and its page is committed, readable and
not a guard page: `light::probe_read32` asks `VirtualQuery` once per page and
trusts the answer for 100 ms (16-entry cache of page bases, the same bound
`engine_memory.cpp` uses when no frame advances). No SEH, no `IsBadReadPtr`;
an implausible or unmapped value leaves the call counted but unclassified.

## Shutdown

`loading_probes::shutdown()` (from `loading_trace::shutdown`) restores the
twelve sites and then sets the exit stub to null so no further return address
is hijacked. Three things are deliberately **not** freed while the process
lives: the arena, the per-thread `Shadow` blocks (one 1.5 KB `HeapAlloc` per
thread that hit a timed probe) and the two `TlsAlloc` slots. A frame hijacked
before the restore still returns into the exit stub later, and
`x3m_probe_exit` reads that thread's shadow through its TLS slot; freeing
either would turn a late return into a use-after-free, and a freed TLS index
could be reused by another allocator. The retained amount is logged
(`loading_probes shutdown retained_shadow_blocks=N retained_bytes=`); the OS
reclaims it at process exit, which is the only time the DLL unloads in
production.

Probe entry stub (per site): `pushfd; pushad; mov eax,esp; push eax; push i;
call x3m_probe_enter; add esp,8; popad; popfd; jmp [next]`. The handler sees the
saved registers (`regs[7]` = EAX, `regs[1]` = ESI, `regs[6]` = ECX), the flags
and the return-address slot `regs[9]`. For timed kinds it pushes
`{site, slot address, return address, context, QPC}` on a per-thread shadow
stack (64 entries, `TlsAlloc` slot, `HeapAlloc`ed on first use) and replaces the
slot with the shared exit stub: `sub esp,4; pushfd; pushad; mov eax,esp; push
eax; call x3m_probe_exit; add esp,4; mov [esp+0x24],eax; popad; popfd; ret`.
`x3m_probe_exit` computes the expected slot of the returning frame
(`ESP − 4 − ret_pop`), discards shadow entries whose slot lies *below* it —
frames that were unwound past without returning (SEH/`longjmp`; counted as
`desync`) — pops the matching entry, accounts the inclusive ticks and returns the
original return address for the stub's `ret`. A full shadow stack counts
`overflow` and leaves the return address alone (the call is still counted).

Everything on the hot path (`x3m_probe_enter`/`x3m_probe_exit`, the counters)
lives in the no-SSE unit `loading_trace_light.cpp`: registers and flags are
saved by the stubs, nothing touches x87/XMM, and only the last error is
transported (`docs/verification/loading-probes.md`, objdump proof).

## Output

At install: `loading_probes requested=1 installed=<n> sites=12 …` and one
`loading_probe_site index= name= va= length= timed= kind= status= x0= x1= x2= x3=`
per site (`status=active` or the fail-closed reason: `bytes_mismatch`,
`unreadable`, `arena_full`, `stub_failed`). Per report window (with the
`loading_metric` rows, from `loading_trace::report`):

```
loading_probe site=<name> va=0x… qpc=<end> calls= exits= inclusive_ticks= max_ticks= total_us= max_us= overflow= desync= bytes= x0= x1= x2= x3=
loading_probe_caller site=find_wrapper caller=0x004e7942 calls=<delta>
loading_probe_path op=CreateDirectoryA|DeleteFileA|MoveFileA|MoveFileExA path=<first 16 seen>
```

`tools/analysis/analyze_loading_profile.py` attributes the deltas to each
presentation gap and report stall exactly as it attributes `loading_metric`
(overlapping report windows) and prints an "Engine probes" table per interval
with the extras named from the `loading_probe_site` header.

## The light import rows of the same batch

`X3M_LOADING_PROBES=1` also patches (as light rows, no `CpuCallBoundary`) the
imports §7 asked for: `CryptAcquireContextA`, `CryptReleaseContext`,
`CryptImportKey`, `CryptCreateHash`, `CryptHashData` (bytes = hashed),
`CryptVerifySignatureA`, `CryptGetHashParam`, `CryptDestroyHash`,
`CryptDestroyKey` (ADVAPI32); `inflateInit2_`, `inflateEnd` (zlib1);
`CreateDirectoryA`, `DeleteFileA`, `MoveFileA`, `MoveFileExA`, `WriteFile`
(bytes = written), `GetFileType`, `CloseHandle` (KERNEL32). They appear as
ordinary `loading_metric op=<import>` rows. `xmlFreeDoc`/`xmlDocGetRootElement`
were not added: the executable imports neither name from `libxml2.dll`.
