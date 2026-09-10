# Native buffer content revision diagnostics

The opt-in ownership tracker is verified by `verification/probe/buffer_content.cpp`
against builtin D3D9 in the Steam bottle of **CrossOver Preview.app**. Run:

```sh
python3 verification/probe/run_buffer_content.py
```

The runner freshly compiles the fixture plus production ownership source with
`-Werror`, records source and executable SHA-256 before/after, uses the process-local
`WINEDLLOVERRIDES=d3d9=b`, and enforces a 90-second child-process timeout. It creates
a hidden standalone window, closes it on exit, and never launches or installs into
the game. Build products stay under ignored `verification/probe/build`.

The current result is in `verification/results/buffer-content-summary.json` and
`buffer-content.txt`. Coverage includes:

- Native, ownership-off-tracking and ownership-on-tracking Lock failure output
  parity, including unchanged versus cleared caller pointers, and exact argument
  forwarding. Tracking off makes no metadata calls.
- Normal hardware and pure hardware devices; VB/IB writable/READONLY locks,
  DISCARD/NOOVERWRITE, pending counters and flags. Writable events advance revision
  before return; READONLY does not.
- Actual nested Lock success on this backend, sticky ambiguity after both Unlocks,
  injected failed Unlock and subsequent recovery, and both closed/pending metadata
  surviving wrapper release/recreation through native bindings.
- Actual successful ProcessVertices into a buffer with nonzero output FVF on normal
  and pure devices; only the destination revision advances. Injected failure leaves
  revision unchanged. Its output-buffer requirement follows Microsoft's
  [ProcessVertices contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-processvertices).
- Metadata Get/Set failure, successful reserved-GUID removal/replacement, native
  untagged-resource adoption, metadata initialization failure preserving the newly
  created application buffer, and device-wide uncertainty through wrapper recreation,
  new buffers and Reset. Known managed-buffer metadata also survives Reset.

Failure injection replaces only the disposable fixture object's native vtable and
restores it before destruction. Reports distinguish the injected failure contracts
from naturally observed nested locks and ProcessVertices success. No buffer payload
is captured, hashed or copied by production; the fixture uploads only its own small
synthetic vertex positions to exercise ProcessVertices.

All fixture calls are serialized. The public API requires serialization of buffer
operations, views and draw snapshots; metadata updates are not transactional with
concurrent native operations. No concurrent-write correctness is claimed.

These revisions describe writes observed at the ownership boundary. They do not
prove immutable content or establish engine object/asset identity. Direct native
writes bypassing the wrapper remain outside the contract. Baseline ownership,
copied-depth and loss fixtures are separate regression gates after source freeze.

## Borrowed buffer endpoint inspection

The current fixture passes **698 checks**, including the original 530 tracking
checks and 168 additional endpoint checks. It builds our x86 code with SSE2 and
four-byte incoming stack realignment. The source and executable hashes are
recorded in the summary above; no game process is launched.

Both typed `borrowed_native_buffer_for_lock_contract` overloads are exercised
with tracking enabled and disabled. They return the exact separately acquired
native pointer without changing logical/native reference counts or content
metadata. Null, unknown, native, nonbuffer-wrapper and wrong-buffer-kind inputs
are rejected. Each replaced Lock/Unlock slot in a copied table is rejected;
restoring the slots accepts the object. A shared original-table Unlock
replacement is also rejected, proving that a changed table cannot redefine the
pristine expected method. An unrelated slot mutation is deliberately accepted:
this API certifies only our Lock/Unlock forwarding endpoints. Retired wrapper
addresses are rejected without dereference; a later canonical wrapper recreation
is accepted. The fixture also checks LastError preservation around inspection.

The helper makes no backend calls. Existing tests verify exact forwarded failed
Lock/Unlock HRESULTs and failure-output behavior. Native endpoint success and
native D3DX adjacency lock behavior are separate cache-integration evidence;
this helper does not certify either. In particular extra READONLY acquisition
changes diagnostic `last_lock_flags`, while skipping a native writable lock
would also omit a revision event. Full cache tracking parity must be established
with the actual native mesh call, not inferred from unchanged mesh bytes.
