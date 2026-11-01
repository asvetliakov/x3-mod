# FL UI render exclusion: native window and scene callbacks

Offline study, 2026-10-28. Extends the reviewed
[load-readiness contract](ui-load-readiness.md),
[scene lifetime contract](ui-camera-lifetime.md) and
[startup contract](startup.md). Preferred EXE VAs and file KC CODE
offsets throughout. No production edits, game/Wine execution, build, install or
commit. The implementation/activation decision remains with the port owner.

## Result

The inspected native window callbacks do **not synchronously invoke load or
reenter the outer game loop**. Mode/load requests publish next-location fields;
the existing outer loop must return before startup dispatch consumes them. The
registered audio completion callbacks publish results/ready state rather than
executing a new game loop. These are concrete native endpoints, not a demand to
exclude arbitrary injected calls to private addresses.

There is nevertheless a concrete **returning scene-destruction callback**:
SC_CLOSE invokes KC `NotifyLeave`, whose SetLeave branch can flush an active
cutscene and free its native scene/cameras before returning successfully. If this
message is dispatched during a scene render, native borrowed pointers can become
invalid. Scene-role retirement does not pin those pointers. This is a conditional
code-path witness; no game run or particular driver's message-pumping timing was
observed.

The smallest additional interception is the SC_CLOSE callback call at
**0x4ec8ed**, deferring the original no-argument `0x476250` invocation until the
**complete outermost native scene-render pass has returned**, or at the normal
load-exit/denied-render service boundaries below when both native depths are zero.
Preserve pending
request multiplicity, existing KC coordinates and normal return semantics. Do
not defer by returning false from a load or by dropping the close request.

No installed native target for scene+0x54 was found. New/restored scene creation
and the audited scene mutators leave it null; the renderer's virtual call is
inert for those qualified flows. The exact alias-proof limit is stated below;
no vtable or target is invented for an unpopulated extension field.

## 1. Native owner thread and queued location changes

The reviewed startup path constructs the display and window synchronously on
the startup/game thread. At `0x4f3abc` it supplies WndProc `0x4ec790`, then calls
RegisterClassA at `0x4f3b67` and CreateWindowExA at `0x4f3ba3`. Its ordinary
message pump `0x4ec620` uses GetMessageA, TranslateMessage and DispatchMessageA
(`0x5502b4/0x5502cc/0x5502b8`), plus PeekMessageA (`0x5502d4`). Windows dispatches
cross-thread sent messages on the receiving window's thread, and same-thread
SendMessage can invoke the procedure immediately. Thus owner-thread execution
does not imply non-reentrancy. This uses the documented
[SendMessage contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagea),
not Wine-private behavior or a worker-thread inference from the sender.

Native `X2_SetNextLocation` is positively resolved: name table entry 10 at
`0x5a5d88`, byte-map entry `0x47bedc+7`, jump-table entry 5 at `0x47be4c+20`,
handler **0x47b7d4**. It copies the three argument-record values into
owner+0x4e4, +0x4e8 and +0x4ec at `0x47b7da/0x47b7e3/0x47b7ec`, then jumps to
ordinary VM result publication at `0x47b71e`; it has no calls in that handler.
It does not copy immediately into current +0x4d8/+0x4dc/+0x4e0 or invoke loading.

Startup calls outer loop `0x477ba0` at `0x477a9a`. **After that call returns**,
`0x477a9f/0x477aa5` read the owner and next-location fields; `0x477ad1/0x477add/
0x477ae3` install them as current state, then `0x477aec` returns to state
dispatch. The prior full CFG proves the render continuation cannot loop back
to load within the same outer-loop invocation. The current census again finds
only `0x477c52 → 0x479430` and `0x477a9a → 0x477ba0`; neither private target has
been identified as a native callback registration.

Consequently, a native callback which reaches X2_SetNextLocation queues a future
transition. It is not a synchronous load writer that must wait on an active
render-reader token. The supported native owner-thread route can use the
load/pass admission protocol without inventing a cross-thread waiting policy.
Deliberate private-address calls by foreign code are outside this profile.

## 2. Window callback endpoints

WndProc takes HWND, message, WPARAM and LPARAM on the stack and returns with
`ret 0x10`. It uses the real input context or a temporary zeroed fallback if
the global input pointer is null. Its input/mouse/character branches update
input fields or the small character queue; they do not dispatch the outer loop.
Unhandled messages call DefWindowProcA at `0x4ecbd9`. Direct WM_CLOSE has no
special game-VM branch here; do not confuse it with the SC_CLOSE path below.

### Activation and audio completion publish results

Activation branches call `0x4edb50` to clear/reacquire input state; it operates
on the DirectInput devices. Deactivation also calls `0x4b4210`, which completes
pending media requests through the VM result callback slot at
`VM + type*24 + 0x34`. The multimedia notification branch (`message=0x3b9`,
successful notification, matching registered media IDs) calls audio-manager+0x10
at `0x4ec8b7`.

That audio pointer is installed explicitly as **0x4b81a0** at `0x4b5da8`.
It checks pending type/token and invokes the same VM result callback slot at
`0x4b81de`, then clears its pending fields. The fixed native callback table
writers establish two non-null result targets:

| Publication | Result target and effect |
| --- | --- |
| VM construction `0x4b8d44` | `0x4c1a40`: resolves the suspended task, stores a result and calls `0x4c1880`, which links it to VM+0x12d8 ready list. It does not run the interpreter. |
| Sole direct registration `0x4778dd → 0x4c7e10`; argument at `0x4778ce` | `0x478910`: stores completion/result in a native task record or owner+0x490/+0x498 and returns. |
| Registration variant `0x4c7e99` | Writes zero to the result callback field. |

`0x4c7e2d` installs the second callback argument from stack+0x10 after its
single saved-register push. This is a 24-byte type record, not a guessed C++
vtable. These two completion bodies call only result/ready-list and value-release
helpers. The inspected value-release helpers recurse through VM arrays/tables,
remove their registry entries and free storage; no KC method callback is
dispatched from those release bodies. A ready-list insertion is not synchronous
execution. This closes the actual audio/deactivation completion frontier for
the fixed native registrations.

### SC_CLOSE executes NotifyLeave synchronously

`message==0x112` selects WM_SYSCOMMAND. The handler masks WPARAM with 0xfff0 at
`0x4ec8c0` and compares against SC_CLOSE=0xf060 at `0x4ec8c5`; equality reaches
**0x4ec8ed → 0x476250**. This call has no stack arguments. HWND/WPARAM/LPARAM
are not passed to the helper. On a normal helper return, WndProc zeroes EAX at
`0x4ec8f2`, restores its frame and returns **LRESULT=0, ret 0x10** at `0x4ec8fa`.

`0x476250` looks up `NotifyLeave` (string `0x58b278`) on the current class. If
present, its call at `0x47628c` invokes `0x4bb5b0` with six caller-cleaned words:
`owner+0x418, 0, owner+0x4dc, NotifyLeave, 1, 0`. The final zero is the argument
count; no message payload is forwarded. The wrapper enters KC synchronously
through `0x4bb65b → 0x4bb470 → 0x4bed90` (interpreter call at
`0x4bb4f0`). A successful native callback, completed result
flag owner+0x490==1, and true returned value permit the ordinary return at
`0x4762b8`. Otherwise `0x4762bb` enters fatal cleanup `0x4762d0`.

The fatal branch's reviewed normal ExitProcess path does not return to stale
render locals. It must remain distinct from a callback that returns normally;
foreign unwind/nonlocal escape from fatal cleanup retains the earlier exception
qualification limit and is not made safe by a scene-role token.

## 3. Returning cutscene-destruction witness

The fixed FL story has one `NotifyLeave` method, class 150 at KC `0x16eb4`.
With its guard clear and leave not already set, it calls `150::SetLeave` at
`0x16ed3`, then X2_SetNextLocation at `0x16edd`, then returns true. SetLeave can
take its ordinary non-DID branch, optionally start its fade, set leave state and
call `625::Flush` at `0x16d82`.

The exact typed chain is:

| KC edge | Effect |
| --- | --- |
| `0x2bffc/0x2c00f`, class 625 Flush | Enumerate the cutscene table and call StopCutscene. |
| `0x2bfbb/0x2bfc5`, class 625 StopCutscene | Remove the entry and invoke its Destruct method. |
| `0x2bf0f/0x2bf13`, class 625 StartPlayingCutscene | The stored object is created as **class 624**, not class 626 (the latter is the XML loader). |
| `0x2c3ab`, class 624 Destruct | Call FreeCameraB3DAssets; that helper calls B3D_CameraFree at `0x30740/0x3075d/0x3077a` and B3D_SceneFree at `0x30797`. |
| `0x2c3c3/0x2c3cd`, class 624 Destruct | Disable/free the cutscene's main scene. Native B3D_SceneFree reaches common destructor `0x4b0170 → 0x4a5460`. |

The native interpreter confirms that these KC method calls do not implicitly
schedule a later task: opcode 0x85 object-call, 0x86 class-call and 0x88 self-call
dispatch to `0x4c094f/0x4c0b83/0x4c0dad`. Their successful paths save the return
offset/current object, switch the same task to callee CODE and jump back to the
same opcode-fetch loop `0x4bedd7` at `0x4c0b7e/0x4c0c0b/0x4c0de4`. This differs
from the audio completion ready-list insertion. A native wait/delay inside a
called method can still suspend; no claim of unconditional completion is needed.

An active cutscene with no delaying child callback and the ordinary return path
is sufficient to exhibit native scene destruction before NotifyLeave returns
true. An empty cutscene table does not take this mutation path. Optional script
work can suspend; the witness does not assert every close completes immediately.
The result is **not a demonstrated load reentry**: the next-location change is
still queued. It is a real returning lifetime mutation which defeats the stronger
claim that window callbacks cannot invalidate render borrows.

If SC_CLOSE is delivered inside a native render call which has retained the
affected scene/camera/node, a role-retirement hook removes metadata but cannot
repair the native renderer's local pointers. The existing projection borrow
contract therefore needs this close deferral. Actual delivery during a driver
call has not been measured; static evidence establishes the callback's effect
and an exact interception for the case where render depth is nonzero.

## 4. Scene+0x54: qualified null flows and alias limit

The normal 0x58-byte scene allocator zeroes the whole object at `0x4a4a9c` and
explicitly sets scene+0x54=0 at `0x4a4b2d`. All six surveyed direct allocation
callers use this allocator: cockpit, target overlay, tooltip, default scene,
scene-resource loading and native script SceneCreate. Renderer restoration
zeroes its 0x58-byte scene at `0x4937a9..0x4937ae` before header publication; the
serialized header restoration does not load +0x54. Scene clear/enable/disable
`0x4a53c0/0x4a5590/0x4a55f0` do not assign that member. Destruction clears it at
`0x4a5531` after the optional virtual destructor.

At `0x48bdce`, ECX=scene+0x54; null branches around the callback at `0x48bdd3`.
Only non-null would read its vtable and call +0x3c at `0x48bdd7`. **No native
non-null installation or corresponding concrete vtable target was found.**

The full linear census contains 147 `mov` destinations with displacement 0x54,
most belonging to unrelated objects or stack frames; that number is not 147
scene writers. The five non-stack `lea base+0x54` candidates are also unrelated:
overlay-entry embedded arrays (`0x42951e/0x4298b3`), an overlay index computation
(`0x42afa3`), static object-data indexing (`0x43dab8`), and a display-side
structure (`0x4fe469`). The related exception cleanup `0x54cdc2` adjusts an
overlay-entry pointer, not a scene. Constant-step loops at `0x49f5d5/0x4a106d`
walk other record arrays. No typed scene-member alias is installed by those
address-taking sites.

This qualifies the null/inert state for the inspected creation, restoration and
scene-API flows; an untyped offset census is **not a formal whole-engine alias
proof**. Computed raw-memory aliases or an additional native extension that
populates +0x54 would require its actual writer/target to be qualified. There is
no concrete such native path to trace in this evidence, and arbitrary foreign
private-memory writes are outside the supported profile. Do not turn this empty
target set into an invented callback reconstruction.

## 5. Smallest deferral contract and exact seam

Extend the complete-pass/load admission state with owner-thread pending close
requests. At `0x4ec8ed`, append each accepted request to the FIFO **before**
attempting service, including an idle arrival when older requests are pending.
Service is permitted only with both native render and load depths zero and no
active drain. With an empty backlog and idle scopes this invokes the original
helper immediately; otherwise it retains the request and returns through
`0x4ec8f2`, preserving zero LRESULT and four-argument cleanup. Recording alone
must not call NotifyLeave, scene destructors or the fatal handler.

Use these concrete owner-thread service triggers in the already proposed wrappers:

- After the **outermost original 0x48b6d0 invocation has returned** and its render
  depth is released. A nested pass or projection return is insufficient.
- After normal return from original load `0x479430` (native return `0x479951`),
  after committing the load result/readiness state and releasing its load depth,
  before resuming the caller at `0x477c57`. Preserve the actual returned EAX and
  all other post-native state around service. This applies even if absent cockpit
  evidence or tracking failure keeps subsequent rendering quarantined.
- On the denied-render wrapper's return path, after deciding not to enter native
  `0x48b6d0`, provided both depths are zero. This services quarantined cases
  without requiring any successful original render. A denial during an active
  load or nested render cannot service the queue.
- On an idle close arrival, after appending it behind any existing backlog.

Readiness gates scene rendering, **not close servicing**. A load-only close must
not wait forever for a successful render that quarantine prevents. These triggers
all use one service routine which checks the depths and drain guard and consumes
FIFO requests. Do not service during native exception cleanup or infer that an
abnormal/nonlocal exit has released a native scope; that remains the existing
unwind qualification requirement.

Reinvoke `0x476250` with no stack arguments; it obtains the then-current native
owner/class/context just as its implementation specifies. No cached scene, VM
task or message-stack pointer is needed. Preserve each service trigger's actual
native entry/return state. Continue to use load readiness before admitting another
scene-render pass; servicing a close does not reopen quarantined rendering.

Preserve every accepted request in order. **Do not coalesce repeated closes**:
NotifyLeave's already-leaving branch differs from its first SetLeave branch and
can select the normal fatal-exit route. Pending storage/allocation failure must
not silently drop a request or run it early; the implementation owner must choose
and test the storage/error policy. A draining guard must prevent recursive drain
if the helper itself receives another close. It should enqueue that request for
the same safe outer drain, not recurse into scene mutation.

| Seam | Exact ABI/patch evidence |
| --- | --- |
| `0x4ec8ed..0x4ec8f2` | One five-byte relative call to `0x476250`. No outgoing stack arguments; preferred call target must be reconstructed for relocation/trampoline use. The helper has plain ret on its normal paths. |
| Native continuation | `xor eax,eax` at `0x4ec8f2`, then restore EDI/ESI/EBX/ESP/EBP and `ret 0x10` at `0x4ec8fa`. Deferred handling must preserve this result and cleanup. |
| State preservation | Preserve incoming GPRs, stack, FP/SIMD and LastError around pending bookkeeping; use the existing four-byte incoming stack contract. Flags are killed by the native XOR but preserving them is inexpensive here. Deferred callback execution must preserve the actual service-trigger state, including post-load EAX, and respect native exceptions. |

The call site is a verified boundary in the full WndProc body; there are zero
HIGHLOW operands and zero surveyed direct interior edges. It begins at aligned
qword offset five, so a five-byte patch crosses that boundary: quiet group
installation/rollback is still required. The existing complete render-pass hook
and load wrappers supply depths and service boundaries; no per-node/per-projection
hook is added. Cost is an owner-thread state check on SC_CLOSE plus an empty-queue
check at complete-pass returns, normal load returns and denied-render returns. Pending storage is event-time work, not per draw.

No scale factor, virtual bounds, cursor or KC layout changes are involved. This
deferral addresses actual lifetime-mutating native window behavior while the
queued next-location protocol supplies normal load/render ownership. It does not
claim arbitrary callback code may safely mutate renderer objects.

## Verification and acceptance limits

[verify_fl_ui_render_exclusion.py](../../../verification/probe/verify_fl_ui_render_exclusion.py)
binds the reviewed EXE and story hashes, checks the native callback publications,
queued-mode consumer, complete callback/result bodies, script destruction chain
and close seam. The offline run passes **84 instruction/operand anchors, 17
bounded regions (1,325 decoded instructions), five semantic mutation negatives
and nine policy witnesses**. The full-text reference/offset census decodes
428,814 instructions; it is separate from the bounded callback bodies.
Five semantic mutations bypass the image-hash gate and must be
rejected: nonzero scene callback initialization, changed close discriminator,
changed WndProc cleanup, changed VM completion target, and replacing the
synchronous self-call continuation with the ready-list helper. Nine sequential policy
witnesses cover event retention, nested-pass drain exclusion, load-only quarantine
service, denied-render service, FIFO idle arrivals, reentrant drain exclusion and
why retirement does not pin native storage. They do not execute the native callback.

```sh
python3 verification/probe/verify_fl_ui_render_exclusion.py --raw
python3 -m py_compile verification/probe/verify_fl_ui_render_exclusion.py
```

Ignored producer/summary/raw listings live in
`verification/results/fl-port/ui-render-exclusion/`; the story input reuses the
hash-verified local extraction from the graph study. Native deferred-wrapper ABI,
pending-event failure policy, abnormal/nonlocal exit, Windows/CrossOver behavior
and user close-during-cutscene acceptance remain implementation checks. No game
delivery timing, native visual behavior or enabled FL UI feature is claimed.
