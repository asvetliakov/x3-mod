# Execution observer synchronization review

The execution observer now synchronizes its state and marks native transitions
in flight from before dispatch through result publication. A snapshot during that
interval is unavailable. Overlapping calls permanently invalidate observation;
successful completion cannot reconstruct their actual ordering. This fixes the
observer's data races and publication gap, not replay-versus-application exclusion.

Independent review required two additional fixes: an unacknowledged or
exception-unwound ticket must invalidate observation, and device loss arriving
during Reset must not disappear when that Reset returns success. Both have
deterministic regression controls. Query owner identity is atomic and fixed for
the token lifetime; foreign observers reject before touching fields protected by
another observer's mutex. No observer mutex spans native dispatch or COM cleanup.

New observation helpers preserve incoming and native outgoing CPU state through
their bookkeeping. Whole Query::Release entry-to-return preservation is not
claimed: its existing registry, deletion and parent-dispatch work extends beyond
the helper. Ordinary non-loss HRESULT forwarding avoids observer locking, and
the disabled option bypasses the new instrumentation.

The [execution verification](../verification/execution-state.md) records 61,408 checks each in
optimized, ASan/UBSan and ThreadSanitizer host runs, plus 184 checks across 23
labeled Windows fixture cases on Preview. Independent review verified current
source, native-runtime, executable and report hashes, barriers, failure controls,
disabled behavior and generator reproducibility. The measured Preview overhead
was about 0.53 microseconds per empty BeginScene/EndScene pair; this is a small
dispatch benchmark, not game or native-Windows performance evidence.

This is a component checkpoint. Full DLL integration will be refreshed after the
ongoing portable-buffer and deferred-retirement work. The last full DLL evidence
remains at `5196f31`. No game launch or installation occurred; live execution
tracking and motion replay remain disabled. Production synchronization uses
standard C++/Win32 facilities rather than Wine-private APIs.
