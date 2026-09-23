# Pause box: what enters it, what leaves it, and where to restrict the exit

2026-09-23. Static study of X3AP.exe, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (the bottle
X3 copy). Nothing was observed at runtime. Function bodies are from Ghidra
headless on `/tmp/x3-ghidra-research/X3Render` (`X3DecompileFunctions`,
`X3XrefsTo`, `X3GrepInsns`, `X3FunctionContext`); every byte, boundary and
table claim is from `i686-w64-mingw32-objdump` on the file bytes and is
re-checked by
[`verify_pause_sites.py`](../../verification/results/pause-dialog-input/verify_pause_sites.py)
(`PASS`, [summary](../../verification/results/pause-dialog-input/verify_pause_sites.json)).
Raw decompiler output stayed local and untracked. Inferences are marked.

**Question.** User report: while the game is paused (Pause key, a box with an
"unpause" button), any key and alt-tabbing away and back unpause it. Which code
enters and leaves the pause, what input does it read, and what is the least
invasive way to accept only the Pause key and the button?

**Answer in one paragraph.** The pause is not a dialog with its own input
handling. `X2_SetPause` (script native 9) sets bit 0 of the game-object flag word
`[*0x0057fc60 + 0x4a0]`; on the next main-loop iteration the in-flight input
routine `0x00404280` sees the bit and **blocks in a tight wait loop**
(`0x004043a0`–`0x004043d6`) that pumps messages and polls DirectInput until
*either* the engine key reader returns a key whose low 12 bits differ from the
previous read, *or* the DirectInput mouse-button word gains a bit. That loop
exit (`0x004043d8`) is the only place in the image that clears the pause bit. The
alt-tab "unpause" is the same test: the Alt (or Command) key-down that begins the
switch is an ordinary DirectInput key; activation messages never touch the
pause bit. The box's button is not hit-tested: any mouse button anywhere
unpauses. A 12-byte in-place rewrite at `0x004043a5` restricts the key exit to
the Pause key and leaves the mouse exit (the "button") as it is.

## 1. Pause state

### 1.1 The flag word

`0x0057fc60` holds a pointer to the game object (the same object whose `+0x418`
is the script host and `+0x4d8`/`+0x4dc` the current game mode and scene script;
[main-loop-input-region.md](main-loop-input-region.md) §1 already calls bit 0
"paused/menu"). The dword at `+0x4a0`:

| Bit | Meaning | Set at | Cleared at |
| --- | --- | --- | --- |
| 0 (`0x1`) | **paused** | `0x0040705d` `or [esi+0x4a0],1` (native 9) | `0x004043e3` only (wait-loop exit); `0x00401a07` zeroes the word once at process init (`0x004019c0`, sole caller `0x00402815`) |
| 1 (`0x2`) | swallow the key that ended the pause | `0x004043e6` `or ecx,2` | `0x004042c8`/`0x004042cb` once a different key or no key is read |
| 2 (`0x4`) | save requested (`X2_Save`, native 3) | `0x00406e35` | `0x00403e07` (then `0x00404530` saves) |

All eleven instructions in the image with a `[reg+0x4a0]` operand on a
non-stack base are listed in the verifier summary; only the three functions
above write it.

### 1.2 Entering the pause

- `0x00406de0` (`__thiscall`, 0x928 bytes) is the engine's native-command
  dispatcher for the X2 script host. It is registered at `0x00403587`–`0x0040359b`
  (`push 0x57c8f0` name table, `mov edx,0x406dc0` thunk, `call 0x004ab0a0`; the
  host handle is stored at `[*0x0057fc60+0x418]`). The switch is
  `movzx eax,[eax+0x004077a8]; jmp [eax*4+0x0040770c]` on `index-3`. The name
  table at `0x0057c8f0` gives index 9 = **`X2_SetPause`**, which resolves to
  `0x0040705d`:

  ```
  0040705d  83 8e a0 04 00 00 01   or   DWORD PTR [esi+0x4a0],0x1   ; esi = *0x0057fc60
  00407064  e8 47 12 09 00         call 0x004982b0                  ; silence looping sound channels
  00407069  e9 40 ff ff ff         jmp  0x00406fae                  ; push 0 as the script result
  ```

  The native takes no argument into account: it can only set the pause.
  (Index 3 `X2_Save` → `0x00406e28`, the bit-2 writer, is the table cross-check.)
- The Pause key itself is handled by script, not by the EXE (**inference**, see
  §5): `0x00404280` passes every key it reads to `0x00402670`, which calls the
  script function `"Input"` of scene script `0x96` through `0x0049f570`
  (`[*0x0057fc60+0x418]`, args `key,key`). No EXE code tests the Pause key code;
  bit 0 has exactly one setter, native 9, so the script's Pause handler must
  call `X2_SetPause`.
- The pause box is drawn by that script through the ordinary UI path and is
  rendered by the normal frame routine `0x00471f50` (main-loop call
  `0x00403f34`) in the same iteration in which the native ran. The next
  iteration blocks (§2.1) before any further frame, so the screen stays frozen
  on that frame. There is no EXE function dedicated to the box.

### 1.3 Where the paused state is consumed

Main loop `0x00403840` (single caller path per [main-loop-input-region.md](main-loop-input-region.md)):

| Site | Test | Effect while paused |
| --- | --- | --- |
| `0x00403ac5` | `test byte [esi+0x4a0],1` | resync the game clock (`0x004d1df0`) and the `0x00609104/08` snapshots |
| `0x00403b09` | same | skip the whole `input_part=0` simulation region |
| `0x00403b3a` | `cmp [esi+0x4d8],1` / `jne 0x00403db8` | mode ≠ 1 (in flight) → `call 0x00404280` at `0x00403db8`; mode 1 feeds keys/mouse to the scene script instead and never reaches the wait loop (**inference:** mode 1 is the menu/start mode; its meaning was not traced) |

## 2. Unpause triggers

### 2.1 The wait loop in `0x00404280` (the only exit)

`0x00404280`–`0x00404421` (0x1a2 bytes, cdecl, no arguments, one caller
`0x00403db8`; gap-free decode, 140 instructions). Entry: `ebx = word
[0x0060903c]` (previous key), `call 0x004d3b60` (key reader), `esi = movzx ax`,
store the new key to `0x0060903c`, test bit 0 at `0x004042a0`, `jne 0x0040438c`.

```
0040438c  mov  eax,[0x606f3c] ; edi = ebp = [eax+0x454]          ; mouse-button word
004043a0  66 85 f6               test si,si
004043a3  74 0c                  je   004043b1                    ; no key -> mouse test
004043a5  8b ce                  mov  ecx,esi
004043a7  33 cb                  xor  ecx,ebx
004043a9  f7 c1 ff 0f 00 00      test ecx,0xfff
004043af  75 27                  jne  004043d8                    ; EXIT: key differs from the previous read
004043b1  8b d5 0b d7 3b d5      mov edx,ebp; or edx,edi; cmp edx,ebp
004043b7  75 1f                  jne  004043d8                    ; EXIT: a mouse button bit appeared
004043b9  e8 f2 f0 0c 00         call 004d34b0                    ; pump (polls DirectInput first)
004043be..004043cb               ebp = edi; edi = [ctx+0x454]; ebx = movzx si
004043ce  e8 8d f7 0c 00         call 004d3b60                    ; next key
004043d3  0f b7 f0 / eb c8       esi = movzx ax; jmp 004043a0
004043d8..004043e9               [obj+0x4a0] = (x & ~1) | 2        ; unpause, arm the swallow bit
004043ef  e8 fc d9 0c 00         call 004d1df0                    ; clock resync
004043f4..00404421               publish 0x00609104/08, key -> 0x0060903c, return 1 (Input not called)
```

`ctx` is `*0x00606f3c`, the engine input block (0x4fc bytes; layout in §4).
The loop has no sleep: while paused the main thread spins on
`PeekMessage` + DirectInput polls (**inference** from `0x004d34b0`'s active
branch, which has no wait).

Accepted set, per test:

| Exit | Condition | Accepts |
| --- | --- | --- |
| key (`0x004043af`) | `si != 0` and `(si ^ previous) & 0xfff != 0` | **every** key the reader produces (all DIK codes except the two Shift keys, §2.2) as soon as it differs from the last value read; a key held since before the pause produces the same code every poll and is ignored until released and pressed again |
| mouse (`0x004043b7`) | `([ctx+0x454] | previous) != previous` | any DirectInput mouse button 0–7 press anywhere on screen; the wheel is not in this word |

### 2.2 The "any key" input path: DirectInput, not window messages

- **Key reader `0x004d3b60`** (0x164 bytes, five callers including `0x0040428c`
  and `0x004043ce`): clears `[ctx+0x434]`, pops one entry from a 3-slot ring
  (read index word `0x00578488`, write index word `0x0057847c`, entries of 12
  bytes at `0x00595ee8`: `+0` DIK byte, `+4` WM code, `+8` "is virtual key"),
  returns 0 when empty. When DirectInput and its keyboard exist
  (`[0x00608b74] != 0 && [0x00608b7c] != 0`, `0x004d3bd5`–`0x004d3be5`) it
  converts the DIK byte through `0x004d6850` (register-argument switch, byte map
  `0x004d6e14`, table `0x004d6bd0`) and ORs `0x1000` when either Shift is held
  (`[0x00608cf4]`). Engine codes of interest: **DIK_PAUSE `0xc5` → `0x1b5`**
  (`0x004d6b30 b8 b5 01 00 00`), LMENU `0x38` → `0x199`, RMENU → `0x19a`,
  LWIN `0xdb` → `0x1b2`, RWIN → `0x1b3`, ESC → `0x1b`, TAB → `0x9`.
- **Producer `0x004d5a90`** (DirectInput poll, called from `0x004d2fc0`, which
  `0x004d34b0` runs *before* draining messages): `GetDeviceData`
  (vtable `+0x28`) on the keyboard device `[0x00608b7c]` with
  `sizeof(DIDEVICEOBJECTDATA)=0x14`, buffer count 1 when `[*0x00606f34+0x728]
  > 10` else 4 (**inference:** that field is the frame-rate figure the
  benchmark text prints); applies each event to the persistent 256-byte state at
  `ctx+0x210`; then for **every DIK whose state is down** pushes one ring entry
  and sets the per-poll table `ctx+0x10[code]`, skipping only LSHIFT `0x2a` and
  RSHIFT `0x36`. Tab is cleared while either Alt is down (`0x004d5a90`, the
  `+0x21f`/`+0x248`/`+0x2c8` test), but **Alt itself is pushed**. Mouse:
  `GetDeviceData` on `[0x00608b78]`, buttons `DIMOFS_BUTTON0..7` into
  `ctx+0x454` (pressed this poll) and `ctx+0x458` (held); `+0x454 = held |
  pressed`; an overflow (`DI_BUFFEROVERFLOW`) falls back to `GetDeviceState`.
- **Window procedure `0x004d3620`** (the `RegisterClassA` class, sole
  `DefWindowProcA` user): `WM_KEYDOWN`/`WM_CHAR`/`WM_DEADCHAR` feed the same
  ring **only when DirectInput or its keyboard is absent** (the
  `[0x00608b74]`/`[0x00608b7c]` test precedes them). `WM_SYSKEYDOWN` is not
  handled. In that fallback the reader maps `VK_PAUSE` (`0x13`) to `0x100`, the
  same code as Print/Snapshot/Help/NumLock/Scroll (decompiler reading of the
  `0x004d3c0d` switch).

So in the normal configuration nothing the game does while paused depends on a
Win32 key message; the key that unpauses is a DirectInput buffered event.

### 2.3 Activation (alt-tab) path

- `WM_ACTIVATE` (`0x004d3697`–`0x004d36f2`): `call 0x004d4950(LOWORD(wParam)==0)`;
  on deactivation `[ctx+0x484]=0`, `[0x00608adc]=0` (application-active flag),
  `call 0x004982b0`; on activation both set to 1. Returns 0, no `DefWindowProc`.
- `WM_ACTIVATEAPP` (`0x004d36f5`–`0x004d370e`): `call 0x004d4950(wParam==0)`.
- `WM_SETFOCUS`/`WM_KILLFOCUS` fall through to `DefWindowProcA`.
- `0x004d4950` (0x14f bytes): zeroes `ctx+0x10`…, the key state `ctx+0x210`
  (0x200 bytes), `ctx+0x454`/`+0x458` and the joystick words; deactivation
  `Unacquire`s mouse and keyboard; activation `Acquire`s both and flushes the
  **keyboard** buffer (`GetDeviceData(0x14, NULL, &INFINITE, 0)`); the mouse
  buffer is not flushed.
- `0x004d34b0` (pump): while `[0x00608adc]==0` and RunInBackground (`ctx+0` bit
  `0x4000`, native `0x26`) is off it sits in a blocking `GetMessageA` loop until
  reactivated.

None of these touch `[obj+0x4a0]`. The alt-tab unpause therefore goes through
§2.1: pressing Alt (Command on macOS, delivered by Wine as a key) before Tab is a
DirectInput key-down, pushed as `0x199`/`0x1b2` (or whatever Wine maps Command
to), which differs from the previous read and exits the loop while the window is
still active (**inference** about the order at runtime; the statically certain
part is that no activation handler can clear bit 0). A click that reactivates
the window can also exit through the mouse test if DirectInput reports its
button-down after re-`Acquire` (runtime-dependent; not established).

## 3. General dialog dismiss or pause-specific?

Pause-specific. The blocking loop runs only when bit 0 is set and the game mode
`[obj+0x4d8] != 1`; it is private to `0x00404280`, consumes keys without passing
them to the script (the unpausing key is swallowed: `return 1` skips `Input`,
and bit 1 drops its auto-repeat on the next iteration), and has no knowledge of
the box. Other menus and dialogs receive keys through the script `"Input"` call
and dismiss themselves in script. The "button" is not a separate path: the
loop exits on any mouse button, so clicking the button works only because it is
a click.

## 4. Fix options

### 4.1 Recommended: 12-byte in-place rewrite of the key test at `0x004043a5`

| | Bytes | Decode |
| --- | --- | --- |
| original | `8b ce 33 cb f7 c1 ff 0f 00 00 75 27` | `mov ecx,esi; xor ecx,ebx; test ecx,0xfff; jne 0x004043d8` |
| patched | `66 81 fe b5 01 75 05 66 39 de 75 27` | `cmp si,0x1b5; jne 0x004043b1; cmp si,bx; jne 0x004043d8` |

Behaviour: a key read exits the pause only if it is `0x1b5` (DIK_PAUSE, no
Shift) and differs from the previous read; every other key is read, dropped,
and the loop falls into the unchanged mouse test. The mouse exit (the button,
and any other click) is untouched. Because the loop still pops the ring every
iteration, ignored keys are consumed and never reach the script. The existing
swallow logic (bit 1) keeps the unpausing Pause press from re-pausing through
`Input`. The alt-tab trigger disappears with the key trigger because it *is* a
key. The immediate `0x01b5` sits at `0x004043a8`–`0x004043a9` and can be
configured if the Pause binding differs (the binding lives in the script's
control matrix, `X2_Get/SetControlMatrix`; not traced).

Hook-site validation (mechanised in the verifier):

- **Boundaries.** Old boundaries `0x004043a5/a7/a9/af`, new `0x004043a5/aa/ac/af`;
  the last instruction (`75 27` at `0x004043af`) and the span end `0x004043b1`
  are unchanged. Only 10 bytes (`0x004043a5`–`0x004043ae`) change.
- **Incoming edges.** The span is entered only by fall-through from
  `0x004043a3` (`je 0x004043b1` not taken); `0x004043b1`, the `je` target, is
  outside the span. A raw rel8/rel32 sweep of all `.text` offsets finds no
  encoding that lands in `0x004043a6`–`0x004043b0`, and no aligned dword in the
  file refers to a span byte.
- **Registers and flags.** `esi` = current key (zero-extended word), `ebx` =
  previous key (zero-extended word, `movzx ebx,si` at `0x004043cb`, or the
  `movzx ebx,word [0x0060903c]` at entry). The patch reads `si`/`bx`, writes only
  flags, which are consumed by its own `jne`s. The original wrote `ecx`; `ecx` is
  dead on both successors (`0x004043b1` does not read it, `0x004043dd` reloads
  it, the pump call clobbers it). `ebp`/`edi` (mouse words) are untouched.
- **Re-entrancy/threads.** `0x00404280` has one caller on the main-loop thread;
  the pump inside it dispatches to the window procedure on the same thread and
  nothing calls back into the loop. Install in the existing install window
  (backend load, before the first Present), when the main loop has not started;
  the change straddles the qwords `0x004043a0`/`0x004043a8`, so it is not a
  single `cmpxchg8b` write, which the install window makes irrelevant (same
  situation as `point_light_admission`'s six-byte site). Byte-verify the 12
  original bytes (fail closed) and keep them for `restore()`.
- **Conflicts.** No installed or diagnostic site lies in `0x00404280`–`0x00404421`;
  the nearest diagnostic site is `game_phase_audio_pump_entry` at `0x004d34b0`,
  which the loop calls (it fires once per spin while paused).
- **Risks.** (a) Shift+Pause (`0x11b5`) no longer unpauses (the original masked
  `0xfff`). (b) If DirectInput or its keyboard is unavailable, Pause arrives as
  the shared WM code `0x100` and only a click unpauses; if the mouse is also
  unavailable the game cannot leave the pause except by closing the window
  (`WM_SYSCOMMAND SC_CLOSE` → `0x00401d60` is still pumped). (c) A reactivation
  click can still unpause if DirectInput delivers its button-down after
  `Acquire`. (d) Portability: an EXE byte patch keyed to this hash; nothing
  Wine-specific, so it behaves the same on native Windows.

### 4.2 Proxy-side message filter: not effective

The keyboard never reaches the pause test as a window message while DirectInput
is up (§2.2), the activation messages do not clear the pause (§2.3), and while
paused no `Present` happens, so per-frame proxy code (the `GetAsyncKeyState`
hotkeys in `capture.cpp`) does not run. The proxy has no window-procedure or
DirectInput hook today (`src/proxy` has no `SetWindowLong`/`CallWindowProc`/
`GetDeviceData` use). A proxy-side equivalent would have to wrap
`IDirectInputDevice8A::GetDeviceData` of the keyboard (reached through
`DirectInput8Create`, IAT `0x00532050`) and drop every event except DIK `0xc5`
while `(*(uint32_t*)(*(uint32_t*)0x0057fc60 + 0x4a0) & 1) != 0` — same-thread
and race-free, but a COM wrapper on all game input instead of 10 bytes.

### 4.3 Activation handler patch: not needed

No activation handler clears the pause. Patching `WM_ACTIVATE`/`0x004d4950`
would not change the unpause and would risk DirectInput re-acquisition.

### 4.4 Optional mouse restrictions (not proposed yet)

- *Button-only:* the engine's software cursor is at `ctx+0x410`/`+0x412`
  (signed 16-bit, clamped to the surface size at `*[0x00606f38]+4/+6`), so the
  mouse exit could be limited to a rectangle; the box and button geometry is
  script-defined and unknown.
- *Ignore the reactivation click:* would need a stub at `0x004043b1` (8 bytes,
  too short for in-place logic) that requires one poll with no buttons after
  `[0x00608adc]` becomes 1. Worth doing only if a run shows a focus click
  unpausing after §4.1.

### 4.5 Interplay with the proxy's existing features

The "selection pause" in this project is the frame-time gap after target
selection ([selection-frame-phases.md](selection-frame-phases.md)), not an input
pause, and the proxy filters no input, so there is nothing to coordinate. While
paused the game presents nothing; after the exit the game clock is resynced
(`0x004043ef`), and the chase camera sees one long frame interval that its
`max_dt` clamp already absorbs.

## 5. What this does not establish

- The script side: that the Pause key handler in the story script calls
  `X2_SetPause`, and how it removes the box afterwards. The script object
  (`L/x3story.obj`, newest in `addon/04.cat`) has no plain or single-byte-XOR
  native names; the call was inferred from bit 0 having a single setter.
- Which key Wine/CrossOver delivers for Command during Cmd-Tab, and whether a
  reactivation click reaches DirectInput as a button-down: runtime questions.
  §4.1 covers every key; only the click case would remain.
- The meaning of `[obj+0x4d8]` (mode 1 vs in-flight) and of `[*0x00606f34+0x728]`.
- Nothing was tested in the game; the patch is a proposal with static
  validation only.

## Input block `*0x00606f3c` (fields used here)

| Offset | Type | Use |
| --- | --- | --- |
| `+0x000` | u32 | input flags (`0x4000` RunInBackground, `0x1000` drop ring, `0x2`/`0x8` joystick) |
| `+0x010` | u8[0x200] | pressed-this-poll table by engine key code |
| `+0x210` | u8[0x100] (+pad to 0x200) | DirectInput key state by DIK |
| `+0x410`/`+0x412` | s16 | software cursor x/y |
| `+0x414`/`+0x416`/`+0x418` | s16 | mouse dx/dy/wheel this poll |
| `+0x430` | u32 | DirectInput mouse enabled |
| `+0x434` | u32 | "a key arrived" (set by producers, cleared by the reader) |
| `+0x444` | u32 | a Shift key held |
| `+0x454` | u32 | mouse buttons held or pressed this poll (the pause exit word) |
| `+0x458` | u32 | mouse buttons held |
| `+0x484` | u32 | window active (mirror of `0x00608adc`) |

## Reproduce

```sh
python3 verification/results/pause-dialog-input/verify_pause_sites.py \
  --json verification/results/pause-dialog-input/verify_pause_sites.json
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3DecompileFunctions.java /tmp/out/pause.txt \
  00404280 004d3b60 004d5a90 004d3620 004d34b0 004d4950 00406de0 004d6850
```
