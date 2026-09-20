# Media startup boundary

`media_startup` implements the narrow ordinary-startup scheduling adapter at the
actual `Direct3DCreate9` export. It is unconfigured by default: this checkpoint
starts no package reader, playback worker or media backend. Integration registers
one callback before the qualified factory returns. The existing factory body,
wrapping, admission, telemetry and returned pointer remain its delegate.

The static ordering proof in `media-record-lifetime.md`, “Early worker scheduling
candidate on ordinary startup” (2026-09-20), identifies the normal setup call
before window/device bring-up and the main loop. Its current local source is
`/tmp/x3-media-reentry-note/docs/reverse-engineering/media-record-lifetime.md`,
qualified by `/tmp/verify_x3_media_worker_startup_boundary.py`; integration of
that separate reverse-engineering note is owned by the parent checkpoint.
It is a bounded normal-control-flow proof, not a general loader-lock detector or
closure over arbitrary computed callers, DLL initializers and exceptional paths.
The adapter uses that exact ordinary context; it is not activated by an arbitrary
first successful factory call.

## Entry, admission and closure

The assembly export records original ESP `E` before any C++ prologue. It safely
copies exactly `[E,E+24)` and requires `[E]=0x4d8494`, `[E+4]=0x20` and
`[E+20]=0x402ee1`. The intervening saved registers are not identity keys. Null,
non-four-byte-aligned, wrapping and unreadable spans refuse scheduling. Production
reads use bounded `VirtualQuery` checks followed by exact `ReadProcessMemory`;
no borrowed stack pointer survives entry capture.

After an ordinary nonnull delegate return, the owner checks executable identity
and five exact instruction anchors at `0x402edc`, `0x4d8488`, `0x4d848f`,
`0x4d8494` and `0x4faedc`. It then atomically claims a process-wide one-shot
request. An unknown caller simply forwards and does not consume the opportunity.
A matching failed factory, missing configuration, failed identity, reentry or
launch failure permanently prevents a later startup retry. Concurrent entry is
treated as reentry; the owner stays active through validation and request claim.

The first statement in capture's `create_device` calls the preserving notification,
which closes this private admission window before existing device work. Closure
during the factory or identity check wins against a later claim. It does not
cancel a request already claimed: bootstrap can finish after device creation.
The existing shared engine-patch window, closed at first Present, is unchanged.
Renderer destruction, Reset and later factory calls never rearm this adapter.

## Ordinary-return CPU contract

The shared assembly header implements both the production export and the fixture
export. Entry and real delegate output receive separate GPR/flags, 108-byte x87,
MXCSR and XMM0–7 save/restore envelopes. Helpers run with empty x87, default
MXCSR, DF clear and four-byte-compatible compiler stack realignment. Each helper
captures LastError first and restores it last. The original stdcall SDK argument,
result and original caller stack are preserved; intentional delegate output is
restored rather than replaced by the incoming seed. There is no displaced engine
instruction, return-slot rewrite or runtime patch transaction to roll back.

This contract covers ordinary returns. The naked stub does not supply C++/SEH
unwind metadata or cleanup for a foreign exception/nonlocal exit through the
factory. If entry context is lost, it stays active and later scheduling refuses;
that fail-closed state is not a guarantee of exception-unwind transparency.

## Integration API and lifetime

1. Call `media_startup::configure(callback, context)` once before the qualified
   factory finishes, for example during its existing initialization. Registration
   publishes a callback and context only; it performs no package/backend work.
   Callback code and context must remain live. The callback is `noexcept`.
2. On the accepted return, the Windows platform acquires a reference to the module
   containing the bootstrap entrypoint with `GetModuleHandleExW(FROM_ADDRESS)`
   **before** `CreateThread` can execute it. Creation failure releases that
   reference and consumes the one-shot. Success closes the thread handle without
   joining or waiting. The added export work performs no DD, COM, HWND or package
   operations.
3. The bootstrap callback receives `{pinned_proxy, requested_qpc}` on that single
   background thread. It may read immutable package configuration and prepare the
   existing canonical runtime. Keep the package reader's `shared_ptr<const
   PackageConfig>` in that runtime through every source/worker use; copying its
   strings alone loses retained file/directory pins. No second playback scheduler
   or worker implementation belongs in this adapter.
4. Returning `true` means preparation was accepted. The canonical service calls
   `service_ready()` when actually ready, either during preparation or later.
   Returning `false` publishes callback failure. The runtime must own each
   longer-lived worker's module/context lifetime independently of bootstrap.
   Bootstrap ends with `FreeLibraryAndExitThread`, with no return through released
   proxy code.

`snapshot()` exposes request, bootstrap begin/end, readiness and first device
attempt QPC values plus status. These are independent atomic observations, not a
transactional snapshot: concurrent readers can briefly see a status before its
timestamp store. `closed` records a first device attempt; other rejection reasons
are represented by status. Readiness must later be compared with actual first
flight/use timing. There is no engine wait, readiness deadline or proof that cold
backend work finishes before flight in this checkpoint.

## Qualification and cost

The [compact record](../../verification/results/media-startup-2026-09-20.json)
and [feature ledger](../verification/media-cues.md#ordinary-startup-scheduling-adapter-2026-09-20)
bind host checks, frozen binaries, both failed mapping witnesses and the accepted
nine-mode run. Host tests exercise the production controller and extracted real
loader/capture bodies. The x86 fixture executes the shared production export and
Windows adapter, with authored engine anchors and a simulated factory/service.
It checks all four incoming stack residues, original output in all modes, input
state in non-reentrant modes, inaccessible cross-page reads, private closure and
real background thread/module APIs. The EXE pins itself; this does not test an
unloadable proxy DLL's final-reference race or production provider lifetime.

All nine modes pass 655 checks on X3/arm64 with FEX. Requested MXCSR `0x3fa5`
immediately reads back as `0x3f80`; full 32-bit represented-state comparisons
remain exact. Requested nonzero sticky-status preservation is unverified. The
fixture's x87 images and seeded SSE registers are represented-state evidence;
it does not close FEX's hidden effective-rounding behavior when x87 and SSE
rounding modes differ. Native Windows runtime and exception behavior remain
unverified despite documented Windows APIs and strict x86 cross-compilation.

Cost is confined to factory calls and device creation attempts, not draw, frame
or media-copy paths. Each export adds two bounded CPU envelopes; admission reads
one 24-byte stack span and, only for the matching successful configured context,
five short anchors plus existing executable identity. One module reference and
one thread are created at most once. There is no per-frame allocation, package
I/O, engine wait or added playback queue. This source-level cost review and
fixture runtimes do not measure game performance or cold provider contention.
