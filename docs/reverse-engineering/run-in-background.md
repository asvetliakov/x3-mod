# Run in background: the `-runinbg` flag and the DLL's `run_in_background` setting

2026-09-27. Static reading of `X3AP.exe` (objdump, checked by `verification/probe/verify_run_in_background_site.py`,
result `verification/results/run-in-background/verify_run_in_background_site.json`, 17/17 checks PASS, measured).
Implementation `src/proxy/run_in_background{.h,.cpp,_sites.h}`; setting `run_in_background` / `X3M_RUN_IN_BACKGROUND`
(default 1), launcher `--run-in-background on|off`. Why it matters: a CrossOver shortcut starts the game without
arguments, and the alt-tab stall of `docs/verification/window-and-cursor.md` ("2026-09-27: alt-tab freeze") follows.

## 1. The flag

Bit `0x4000` (RunInBackground) of the input flags word, dword `+0x000` of the input block `*0x00606f3c` (0x4fc bytes,
allocated and zeroed by `0x004d2d10`, stored at `0x004d2d76`, called from the init routine at `0x0040337f`; layout in
[pause-dialog-input.md](pause-dialog-input.md), "Input block"). Readers (every load of `0x00606f3c` followed by a
test of the bit, measured by a scan of the disassembly):

| VA | Reader | Effect of the bit |
| --- | --- | --- |
| `0x004d34cf` | message pump `0x004d34b0` | with the window inactive (`[0x00608adc] == 0`) and the bit clear, loops in a blocking `GetMessageA` until the window is active again: the whole game loop stops; with the bit set it drains messages without blocking and returns |
| `0x004d14fa` | media status query `0x004d14e0` | with the window inactive and the bit clear, answers "ended" without looking ([music-restart.md](music-restart.md) §2, Patch D) |
| `0x0040768c` | script `X2_IsRunInBackground` | returns the bit |
| `0x0040764d` / `0x00407657` | script `X2_SetRunInBackground` | sets / clears it and stores registry value `RunInBackground` (`0x004b82f0`) |

The InputFlags registry value (`0x00562ccc`, read `0x004b74bf`, written `0x004b7e9c`) is the settings block's
`[*0x00606f34+8]`; it seeds this word (`0x0040337f` passes it to `0x004d2d10`, stored at `0x004d2d87`), and the window
below then sets or clears bit 0x4000 unconditionally, so InputFlags never decides the bit. The DLL writes only the
input word, never the settings field or the registry.

## 2. How the argument sets it

Init routine `0x00402780` (called from `0x0040273c`), argument loop from `0x0040288a`, each token compared with
`_stricmp`-like `0x00517b7b`:

| VA | Instruction | Meaning |
| --- | --- | --- |
| `0x004027a4`, `0x004027ba` | `or ebx,-1`; `mov [esp+0x18],ebx` (one push outstanding) | local `[esp+0x14]` = -1: no argument |
| `0x00402be7` / `0x00402c00` | `push 0x00555514` "/runinbg" / `push 0x00555520` "-runinbg", `je 0x00402d16` | |
| `0x00402c19` / `0x00402c32` | `push 0x0055552c` "/noruninbg" / `push 0x00555538` "-noruninbg", `je 0x00402d09` | |
| `0x00402d16` | `c7 44 24 14 01 00 00 00` `mov dword [esp+0x14],1` | `-runinbg` |
| `0x00402d09` | `c7 44 24 14 00 00 00 00` `mov dword [esp+0x14],0` | `-noruninbg` |

The local has exactly one reader, `0x00403398`, the second instruction of the window below (every instruction of
`0x00402780..0x0040383c` naming `[esp+0x14]`: `0x00402d09`, `0x00402d16`, `0x00403398`; measured). The argument
therefore does one thing: it decides bit 0x4000. With neither argument the window reads registry value
`RunInBackground` (`0x004b8510`: `RegOpenKeyExA(HKCU, …, KEY_READ)`, `RegQueryValueExA`, `RegCloseKey`; a pure
read) and uses it instead.

The window `0x00403392..0x004033cd` (60 bytes; the site header's `expected_window`):

```
00403392  8b 0d 3c 6f 60 00   mov ecx,[00606f3c]
00403398  8b 44 24 14         mov eax,[esp+0x14]          ; the argument's local
0040339c  81 09 00 10 00 00   or  dword [ecx],0x1000
004033a2  83 f8 ff            cmp eax,-1
004033a5  75 10               jne 004033b7
004033a7  be a8 56 55 00      mov esi,0x005556a8          ; "RunInBackground"
004033ac  e8 5f 51 0b 00      call 004b8510               ; registry read
004033b1  8b 0d 3c 6f 60 00   mov ecx,[00606f3c]
004033b7  85 c0               test eax,eax
004033b9  74 08               je 004033c3
004033bb  81 09 00 40 00 00   or  dword [ecx],0x4000      ; RunInBackground on
004033c1  eb 06               jmp 004033c9
004033c3  81 21 ff bf ff ff   and dword [ecx],0xffffbfff  ; RunInBackground off
004033c9  e8 b2 f1 0c 00      call 004d2580               <- site
```

`0x004d2580` (`51 e8 da fe ff ff`: `push ecx` as a slot, `call 0x004d2460`, returns EAX) takes no argument; the call at
`0x004033c9` is its only direct caller and no raw branch in `.text` lands inside the call's five bytes (measured).

## 3. Mechanism

The only proxy entry before the window is `Direct3DCreate9` (`0x00402edc`, and `0x004dac90` via `0x0040332a`), where
the patches install (`load_backend` → `initialize_log`); at that point `*0x00606f3c` is still null and the window
would overwrite any earlier write. So the DLL redirects the call at `0x004033c9` (rel32 only, opcode and boundaries
unchanged; `engine_patch::claim_call`, one `lock cmpxchg8b`: the five bytes lie in the aligned word
`0x004033c8..0x004033cf`) to `x3m_run_in_background_thunk`:

```
pushfd; pushad; cld; call x3m_run_in_background_apply; popad; popfd; jmp [x3m_run_in_background_continue]  ; = 0x004d2580
```

The C handler takes the one-shot token (`InterlockedExchange`), reads the word through `*0x00606f3c` (which the game has
just written through), and when bit 0x4000 is clear sets it with one `InterlockedOr`; when it is set (the argument or
the registry value) it writes nothing. It writes one row, restores LastError and returns; the `jmp` leaves the stack as
the call left it, so `0x004d2580` returns to `0x004033ce` with every register and EFLAGS as in vanilla (fixture:
identical register record at the callee, DF included). Gates before the write: the structural executable identity
(`object_trace::executable_verified`, no hash), the 60-byte window, the call target, the install window (refused
`late_claim` after the first Present), the module pinned (`GET_MODULE_HANDLE_EX_FLAG_PIN`) because the engine calls
into it. A failed claim is rolled back by `claim_call`; `shutdown()` restores the call on a dynamic unload.

Difference from a real `-runinbg`: with no argument the engine still performs its registry read before the DLL sets
the bit (a read with no side effect). `-noruninbg` is overridden while the setting is on (the bit is set anyway);
`run_in_background = 0` restores the game's own behaviour. A later `X2_SetRunInBackground` changes the bit exactly as
it would after `-runinbg`. No allocation, no per-frame work, nothing on the device: Reset and recovery are not involved.

## 4. Rows

- Install (backend load): `run_in_background site=0x004033c9 status=armed|off|refused reason=ok|unset|setting_off|
  invalid_setting|executable_mismatch|late_claim|bytes_mismatch|target_mismatch|pin_failed|protect_failed|… setting=1|0|-|?
  value_before=- value_after=- write=atomic|plain|none handler=0x…`.
- Site (once, when the game reaches `0x004033c9` during init): `run_in_background site=0x004033c9 status=patched|already
  reason=ok setting=1 value_before=0|1 value_after=1 flags_before=0x… flags_after=0x…`.
- Flight proof: `music_keep_active … run_in_background=1` (the music keep samples the real bit at Present).

A CrossOver shortcut launch with the default setting logs `status=armed`, then `status=patched value_before=0
value_after=1` (the bit was clear), and `music_keep_active frame=0 … run_in_background=1`. A developer launch
(`manage.py --direct` passes `-runinbg`) logs `status=already value_before=1`.

## Reproduce

```sh
python3 verification/probe/verify_run_in_background_site.py --json verification/results/run-in-background/verify_run_in_background_site.json
PYTHONPATH=verification/probe:verification/analysis python3 -m unittest test_run_in_background
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_run_in_background_patch.py
i686-w64-mingw32-objdump -d --start-address=0x402780 --stop-address=0x40383d "$X3AP"   # init routine
```
