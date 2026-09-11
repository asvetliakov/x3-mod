# Bounded finite-sidecar index

The portable geometry benchmark exposed repeated linear allocation-list lookup:
700 leases using many small buffers cost about 11.4 ms for acquisition plus
inspection, versus about 2.5 ms with one shared buffer pair. The ownership layer
now locates each allocation through a bounded intrusive hash index. It still
performs canonical IUnknown identity lookup, public descriptor validation,
private-IUnknown authentication and observed revision checks.

## Storage and lifetime

Each FiniteOwner embeds 2,048 weak bucket heads, costing 8 KiB on x86. At most
64 owners can exist, so bucket storage is bounded by 512 KiB. The existing
4,096-sidecar global cap and 32 MiB payload cap are unchanged. The three new pointer fields occupy 12 bytes on x86; compiler layout padding
changes the complete sidecar size from 272 to 280 bytes with the current i686
toolchain. `metadata_bytes` includes complete sidecar objects and the fixed owner
bucket storage. Control-block/allocator overhead remains
separate from reported payload bytes. Index creation uses the existing owner
allocation; failure follows its existing E_OUTOFMEMORY/refusal path. No lookup,
insertion, unlink or identity replacement allocates memory.

Canonical pointer bits are mixed before selecting a bucket because native
addresses are aligned and clustered. Exact canonical pointer equality identifies
a match. Expected lookup cost is constant; deliberately colliding keys can still
form a chain bounded by the 4,096-sidecar cap. This is not a worst-case constant
time claim. Intrusive back-links provide constant-time removal from a bucket and
from the owner list. The owner list remains available for complete reset/loss
retirement, which necessarily visits the affected allocations.

All index/list operations execute under the existing registry mutex. A sidecar
whose last reference is released beneath a native runtime lock still queues for
deferred destruction if the registry is busy. Until drain, its index entry is
present but positive-reference CAS cannot resurrect it. Drain unlinks the object
before freeing it. No index link owns a COM or sidecar reference.

If a new native allocation reuses an old numeric canonical identity while an
external reference retains the old CPU sidecar, insertion first detaches the old
index entry and clears its allocation key. Its evidence retires under the
existing policy. The old object's later destructor cannot erase the new entry.
Immutable requested Usage, authentication-failure latches and allocation metadata
survive ordinary atlas retirement as before.

## Verification

The focused fixture adds 18 deterministic controls using distinct synthetic
numeric identities forced into one bucket; those pointers are never called.
Controls cover tail/middle/head lookup and removal, missing/null keys, back-link
consistency, metadata charges, identity replacement with a surviving old CPU
sidecar, immutable Usage/refusal state after payload retirement, and a real
worker-thread zero-reference release while the registry is held. That deferred
entry must refuse resurrection and drain without damaging colliding survivors.
Existing native observer controls independently cover wrapper recreation,
private-IUnknown authentication, upload/Reset/loss/refusal and actual native
callback contention.

The fresh native observer fixture passes **552 checks**, including all 18 new
index controls, on the recorded Preview runtime. Independent source, artifact and timing review is accepted; the
[retained manifest](../../verification/results/finite-upload-summary.json) matches
the fresh executable and source inputs. The unchanged geometry fixture passes
421 checks and the same-work benchmark passes 831,397 checks across 84 samples.

For 700 leases over many small buffers, median acquisition plus inspection fell
from 11.4072 to 2.5374 ms; the shared-small profile stayed approximately steady
at 2.4762 versus 2.4346 ms. At 4,096 many-small leases, acquisition/inspection
fell from 26.1380/25.8433 to 7.1757/7.5590 ms. These are synthetic Preview CPU
timings with buffers created and uploaded before measurement, not game FPS or
native-Windows results. The [geometry report](geometry-performance.md) retains
the exact historical/current source maps and all samples.

The index
does not broaden supported mappings, enable live replay or change the installed
DLL. See [finite upload evidence](finite-upload-observer.md) and
[geometry performance](geometry-performance.md).
