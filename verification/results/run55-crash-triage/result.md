# Run55 crash triage

## Outcome

**Observation.** The Run55 crash is a read access violation at `0x6eb2413a`, reading `0xffffffff`, on process/thread `00d4:03f8` at `2026-09-20T21:01:47.734Z` (Wine QPC `1667545.514`). It is inside native `LAVVideo.ax`, not the installed proxy: actual map `[0x6ea80000, 0x6eb8e000)`, RVA `0xa413a`. The fault thread was named `CLAVOutputPin Video`.

**Observation.** Fault x86 state: `EIP=6eb2413a ESP=EBP=56b3f80c`, `EAX=00000000 EBX=00000c02 ECX=ffffffff EDX=00000c02 ESI=5852567c EDI=56b3f884`. The exception is `0xc0000005`, read (`info[0]=0`) of `0xffffffff` (`info[1]`). `LAVVideo.ax` first handles it at `6EB5B7F6`; the proxy vectored handler `7692B4C0` returns 0.

**Observation.** The runtime log maps the provider at `0x6ea80000-0x6eb8e000` and calls it native. The on-disk provider reports ImageBase `0x10000000`, SizeOfImage `0x10e000`, SHA-256 `84ac9e2f4da06d52518557cb8c3e04315c0364f01f822bde761e284d0b8d2cdd`. The session identifies the installed proxy SHA-256 `4b47f636acd66eba35c61a3f9a4d4d18d911470f46b2df7b64aaf1465cdaa012`, source commit `54b48c36f6d7d6aa846992fe5412fbaee5f26219`.

**Observation.** `CX_DEBUGMSG`/`WINEDEBUG` includes timestamp, pid, seh, unwind, process, module, loaddll, threadname and tid. Wine starts `winedbg --auto 212 8820` at launcher line 1,048,065, but this log contains no winedbg register dump, module listing, raw stack, or symbolic caller chain.

## Preceding events and telemetry

The same TID was renamed `CLAVOutputPin Video` at launcher lines 1045691, 1045818. At `2026-09-20T21:01:47.734Z`, the preceding one millisecond includes `avfilter-lav-11.dll` thread attach and Wine threadpool worker/timerqueue naming. LAVVideo path/base lookup on the fault TID occurs at launcher lines 1,046,350–1,046,353 before the dispatch at 1,046,452.

The proxy session records 1 ownership snapshot and 1 services snapshot; it records zero worker, submission, retirement, completion, or shutdown events. It has 19 cue windows; its final window at line 2480892 has zero attempts, video blits, unlocks and failures. These are observations only and do not identify the bad object passed to LAVVideo.

## Limitation and next evidence

No source-file ownership is established by the logs; the established owning runtime location is `/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-media/providers/lav081-strict-cadf5fbf4cf41bae/LAVVideo.ax`. A decisive next witness needs the LAVVideo instruction/caller stack plus the object origin/lifetime at this hash-chain lookup; current telemetry lacks both.
