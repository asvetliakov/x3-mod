# B2: F8 geometry payload packet — pending parent ratification

**Removed 2026-09-22.** The default-off lattice state capture, geometry packet writer and upload-hook diagnostic described here is no longer in the tree; the crawl it was built to explain is fixed and accepted (`taa-lattice-crawl.md` §32.5–§32.6). The last commit that carries the code is main `59ad2649`. This note is kept as history.

Design only. Inputs: AGENTS.md, `/tmp/x3-lattice-upload-flight-integration.md`, ratified B1 `/tmp/x3-lattice-upload-lifecycle-contract.md`, current `src/proxy/lattice_state_capture.{h,cpp}`, `verification/probe/lattice_state_packet.py`, and collector **`tools/analysis/snapshot_x3_run.py`**. No source changes, build, Wine, game, new ABI or lifecycle proposal. B1 remains the authority for safe atomic paired copying after the final getter Release.

## Recommendation

Retain schema1 writing/reading for state-only requests. An upload-enabled F8 request writes schema2: the same bounded state fields plus two upload-result envelopes and one optional, fixed-layout binary sidecar. Publish binary geometry only when **both** selected pairs qualify and the entire state observation completes with successful native submissions. Otherwise preserve state/refusal JSON only. No partial binary, per-buffer files, embedded base64, pointer-based payload identity, source-mesh substitution or generic format migration.

The parent's “no pointer serialization” requirement applies to **new upload identity/metadata**: no B1 weak keys, mapped addresses, COM pointers, Store pointers or native descriptors containing addresses enter that envelope. Existing schema1-style state fields already include bounded pointer words; their semantics remain unchanged as explicitly agreed by the parent. They are not payload identity. New payload identity uses B1's allocation/revision, owner/generation, arm serial and invocation.

## Storage and call boundary

`Capture` retains its existing state storage limit. Add an optional separately owned fixed-size `GeometryPacket` (one `std::array<unsigned char,466224>` plus small metadata), allocated and zero-initialized **only at the explicit upload-enabled F8 edge** with the Capture's shared CPU owner. A separate allocation keeps state-only Capture within its existing448KiB ceiling. No allocation, resize, hashing, formatting, file access, native Lock/Unlock or COM call is added by the payload copy itself.

If this allocation fails, still produce schema2 JSON identifying `allocation_failure`; state observation can remain complete. Never silently downgrade the requested evidence to an indistinguishable schema1 packet. Existing Capture allocation failure remains the existing log-only failure. Storage belongs to the packet until publication/retirement, not to Store, the device or a file worker.

After the final descriptor/getter/resource Releases and B1's weak-key/view capture, invoke **one B1 `copy_clone_upload_pair` per selected slot** with the exact two destination spans below. Require S_OK, status Copied, valid Record, matching expected slot/byte counts and `binding_revision_match_at_observation=true`. Store the returned Record and the request's B1-confirmed arm serial. No pointer members from the request survive in GeometryPacket. B1 atomically checks/copies metadata plus both CPU buffers; B2 must not substitute two calls to the old single-buffer helper. B2 copies at most466224 bytes per complete armed frame; default state-only captures do no payload work.

## Binary format `x3_lattice_geometry_v1`

File basename: `lattice-geometry-<pid>-<device>-<frame>-<generation>.bin`, where all four decimal components equal the associated `lattice-state-...json` stem and logged writer identity. `generation` in the filename is the existing **capture Reset generation**; B1 ownership generation is distinct and recorded below.

Exactly466224 bytes, no header, trailer, padding, compression or executable/native structure dump. The JSON is the sole versioned directory. Bytes are copied unchanged from the qualified producer observations. INDEX16 is unsigned little endian; VB is the existing stride40 declaration's raw bytes, with its five FLOAT16_4 elements. Do not repack vertices, normalize half values, reorder indices or canonicalize NaNs.

| Slot / range | Offset | Bytes | Expected layout |
| --- | ---: | ---: | --- |
|0 VB|0|387200|9680×40|
|0 IB|387200|22704|3784×3×2|
|1 VB|409904|50680|1267×40|
|1 IB|460584|5640|940×3×2|

These ranges are fixed constants, disjoint, cover the entire file and are independent of selection/draw order. The validator checks exact constants, not merely nonoverlap/capacity. No zero placeholder bytes for an unavailable pair are exported. A file shorter/longer than466224 bytes is invalid even if a supplied hash matches.

## JSON schema2 delta

Keep existing root selector, status, device/frame/generation, scope/candidate/match/timing fields and the two records/fields unchanged. Add root `pid` (positive integer matching filename/log); keep `draw_input_coherence:"unqualified"`. For schema2 only, `payload_copy_valid` is a **boolean** true iff the entire published geometry attachment is complete; schema1 retains exactly the historical string `"not_attempted"`. This is an explicit versioned type change, never a truthiness check.

Add one root object:

```json
"geometry": {
  "format": "x3_lattice_geometry_v1",
  "scope": "producer_uploads_bound_at_observation",
  "status": "complete",
  "file": "lattice-geometry-123-1-6401-0.bin",
  "bytes": 466224,
  "sha256": "<64 lowercase hex digits>",
  "copy_ticks": 0
}
```

`geometry.status` is `complete` or `unavailable`. Unavailable always has `file:null`, `bytes:0`, `sha256:null`, `payload_copy_valid:false`; it never references an orphan sidecar. `copy_ticks` is a nonnegative uint64 count under existing `qpc_frequency`, summed for the at-most-two B1 calls; it excludes Present hashing/I/O. `complete` requires top-level state status complete, matches[1,1], both successful submitted records, and valid slot envelopes. Unqualified draw coherence is mandatory in every state.

Each of the two records gains `upload`:

```json
"upload": {
  "pair_status": "copied",
  "attachment_status": "valid",
  "producer_payload_valid": true,
  "binding_revision_match_at_observation": true,
  "arm_serial": "0000000000000001",
  "owner": "0000000000000001",
  "generation": "0000000000000001",
  "invocation": "0000000000000001",
  "vertex": {
    "allocation": "0000000000000011", "revision": "0000000000000001",
    "offset": 0, "bytes": 387200
  },
  "index": {
    "allocation": "0000000000000012", "revision": "0000000000000001",
    "offset": 387200, "bytes": 22704
  }
}
```

All new64-bit identifiers use **exactly16 lowercase hexadecimal digits**, decoded as unsigned integers without floating conversion. Successful identities are nonzero; vertex/index allocations differ. Both records have equal arm serial, owner and ownership generation and distinct nonzero upload invocations and distinct allocation pairs. The byte counts/offsets must match the fixed slot table. Capture frame/generation are not compared numerically to owner/generation: they are different namespaces. Do not add raw pointers, inferred texture identity or a draw-coherence flag that can become true.

`pair_status` is `not_attempted` or the stable lowercase spelling of the actual B1 result (`copied`, `unarmed`, `closing`, `active_scope`, `selector`, `missing`, `duplicate`, `stale_arm`, `device`, `binding`, `revision`, `open_mapping`, `dispatch`, `capacity`; malformed API storage becomes explicit `api_error`, not Copied). Use the finalized B1 enum spelling if its accepted header differs; do not invent a separate alternate ownership classifier.

`attachment_status` is the B2 export verdict: `valid`, `not_selected`, `allocation_failure`, `copy_refused`, `packet_invalid`, `submission_failed`, `sibling_refused` or `export_failed`. `packet_invalid` carries `invalidated_by` equal to the existing terminal state status (`reset`, `ambiguous`, `partial`, `unavailable`, `capacity`, `submission_failed` or `no_match`). Other attachment statuses use `invalidated_by:null`. On any non-valid attachment, both boolean flags are false and all identity/range members (`arm_serial` through `index`) are **absent**. Preserve `pair_status` as attempt history (e.g. Copied then Reset), never a stale successful Record. This gives precise per-slot B1 refusal plus explicit packet-wide invalidation without exposing invalidated bytes.

With only one B1 refusal, retain that slot's `copy_refused` and its precise reason; the successful sibling becomes `sibling_refused`, with its attempt status Copied but no exported identity/range. Both pairs must be valid before either is exported. Top-level state `status:"complete"` plus unavailable geometry is legitimate: state completeness and payload availability are different results.

The top-level boolean means only “valid published producer observations with B1 binding/revision matches at the observation point.” The constant scope and `draw_input_coherence:"unqualified"` delimit that claim. It is not an immutable simultaneous VB/IB/texture submission snapshot, later-writer proof or historical Run177 identity.

## Invalidation and publication eligibility

All payload flags are initialized false at F8. A B1 failure erases its entire output pair and leaves no valid header. A Reset (including a reentrant Reset refused by the existing query guard), selector duplicate/ambiguity, capacity failure, unavailable required state, failed or suppressed original draw invalidates the payload export. Erase admitted packet ranges and strip Record metadata; retain only attempt/refusal history. No later result callback, successful second slot or `Policy::finish` can restore that invalidated attachment.

At Present, calculate final state eligibility **before hashing any payload**. This terminal check is mandatory even if immediate invalidation helpers already ran, so a direct `policy_.refuse` call cannot accidentally leave a serializable geometry success. If state is partial/no-match or either pair is unavailable, erase any remaining sibling bytes and publish JSON-only. Existing reentrant `publish` busy refusal stays intact and must invalidate payload too. Do not repopulate from Store at Present, retry a failed pair or fall back to a previous frame's bytes.

B2 invalidates only its packet. It does not reset/rearm B1, clear duplicate Store slots, alter the hook gate or modify device-pin policy. Those remain B1 responsibilities.

## Present writer ordering and failure behavior

1. Freeze the packet's completed CPU-only view under the existing capture publication boundary; no COM survives into file operations. Validate final state and both attachments. Only admitted bytes reach hashing/file output.
2. Compute SHA-256 over exactly466224 bytes using documented Windows CryptoAPI (the project already uses SHA-256 in `proxy_identity.cpp`); no new crypto library, backend API or per-buffer hash is needed. Hashing failure makes geometry unavailable/export_failed; do not produce a successful sidecar.
3. Write the binary temporary file in the capture directory; require exact write length, no stream error and successful close. Rename it to the exact final basename. Prefer no-replace publication for schema2 artifacts so an unexpected same-stem collision is a refusal, not an old metadata/new binary mixture. Track ownership of created temporary/final files; never delete pre-existing files on failure.
4. Only after the binary is finalized, write JSON temporary containing that basename, exact length/hash and valid envelopes. Enforce the existing1MiB JSON cap, require successful close, then finalize JSON **last**. Schema1's existing one-file writer remains supported. No `file_ok=1` log until all required final files exist.
5. Emit one successful `lattice_state` record. On binary/hash failure, best-effort publish a JSON-only refusal (`export_failed`) with no sidecar reference; `file_ok=1` then means that refusal metadata was saved, not payload success. On JSON write/finalize failure, report `file_ok=0`; remove only the current writer's orphan binary if safe, otherwise leave it unreferenced. The collector must never discover it by directory glob. Do not retry/reuse the packet under the same filename as though it had succeeded.

No crash-durability claim is made for an OS/power failure between renames. Manifest-last, size/hash checks and log authorization ensure incomplete bundles cannot validate as payload. Additional full-volume flushing is not required for this diagnostic.

Extend the existing log row with `schema=2 payload_copy_valid=0|1 payload_file=<basename|-> payload_bytes=<466224|0> payload_sha256=<hex|->`. Existing `pid/device/frame/generation/file/bytes/file_ok/status/matches` remain. For schema1 the collector accepts the historical row without these fields. A schema2 success row's new fields must agree with its JSON; all four identity components must agree across log/JSON/basenames. Do not emit a separate binary success record before final JSON publication.

## Validator and collector behavior

`verification/probe/lattice_state_packet.py`: split version dispatch explicitly. Preserve schema1 requirements and all existing complete-state checks. Schema2 validates the same state contract plus strict types, allowed enums, bounds, no unexpected upload identity members/pointers, boolean evidence claims, exact fixed ranges, equality of arm/owner/generation and pair completeness. Reject duplicate JSON keys as today. Reject booleans where numeric IDs/offsets/ticks are required. Structural `validate(packet)` cannot claim file integrity; it validates metadata only.

`load(path, require_complete=False, require_payload=False)` additionally validates every referenced schema2 sidecar's basename, same directory, ordinary regular file/non-symlink status, exact size and SHA-256 with stable file identity/stat checks. Use a no-follow open; do not follow absolute/relative paths from JSON. A missing/corrupt referenced sidecar makes `load` fail even when `require_payload` is false. A JSON-only refusal can load normally; `--require-payload` refuses it and also refuses schema1. Preserve `--require-complete` as **state completeness** for existing consumers; it must not silently become a payload requirement. Add `--require-payload` for downstream geometry use. Decoder reads only after file/schema validation and checks INDEX16 ranges against the fixed vertex counts before replay. Nonfinite or unusual unused VB attributes are not normalized or silently dropped by the preservation layer.

`tools/analysis/snapshot_x3_run.py`: retain the current pinned directory FDs, no-follow source leaf opens, regular-file/size/time-window checks, unchanged-source stat recheck, immutable copied session log and exclusive destination creation. Extend `references` only for schema2 `lattice_state` rows; authorize the binary solely from a successful metadata row with matching bounded basename, bytes and SHA-256. `file_ok=0` must revoke both same-stem files from prior rows, consistent with existing last-write-wins authorization. A JSON-only refusal authorizes only JSON. Never scan the capture directory for lattice binaries or copy `.tmp` files.

Add an optional expected SHA-256 parameter to `copy_file` for this binary, computing it while the existing bounded streaming copy runs. On mismatch/change remove only that newly created destination, as today. After both files are copied, cross-check the schema2 JSON's sidecar reference/identity/length/hash against the authorizing log and use the validator for the copied bundle. Preserve malformed/refused JSON for diagnosis and record an issue; do not count its binary as qualified or leave a binary authorized only by mismatched JSON. Missing binary may leave preserved JSON plus an issue; the parser then refuses payload. Existing schema1 collection must remain byte-preserving, including explicitly refused packets and legacy rows.

## Exact implementation files and bounded acceptance

- `src/proxy/lattice_state_capture.{h,cpp}`: optional fixed GeometryPacket, per-slot result envelopes, final eligibility/invalidation and Present publication. `src/proxy/capture.cpp`: allocate at F8/request handoff only and consume B1 after-final-Release paired API per the existing lifecycle contract. Do not reopen B1 pin/ABI code in this checkpoint.
- `verification/probe/lattice_state_packet.py`, `tools/analysis/snapshot_x3_run.py`: schema2/bundle validation, strict log authorization and streaming binary hash. Extend `verification/analysis/test_lattice_state_capture.py` and `test_snapshot_x3_run.py`; a small isolated packet-policy/layout helper test may be added if needed to exercise the actual production packet helper without D3D.
- Reuse current state fixture coverage; add an extracted/helper case that feeds accepted B1 pair results and invalidations, checks exact packet bytes/flags, and asserts no allocation/hash/file/native Lock occurs in attachment. No live D3DX/Wine rerun or full suite belongs to this B2 design task.

Focused tests must demonstrate:

1. Existing schema1 complete and all refusal fixtures still validate/collect unchanged; schema1 cannot satisfy `require_payload`.
2. Exact schema2 positive: both slots, reversed draw arrival order, all four fixed byte ranges, exact total/hash, uint64 identifiers above2^53, state and payload requirements separated. Binary/layout helper uses canaries and fails short second capacity without a partial success.
3. Every B1 refusal has JSON-only geometry with precise slot status; successful sibling bytes/headers cannot leak. Copy-then-Reset, duplicate, partial frame, unavailable state, suppressed/failed draw and late reentrant invalidation all prohibit binary publication and cannot be revived by subsequent success.
4. Missing/extra/overlapping/wrong offsets, truncated/oversized binary, changed byte/hash, duplicate JSON keys, bool-as-number, malformed IDs, mismatched arm/owner/generation, false qualification/coherence and unexpected pointer keys all fail. INDEX16 out-of-range is refused before replay, not repaired.
5. Hash/write/close/rename failures at each writer stage, existing-name collision and JSON-finalize failure: no complete manifest names an unavailable binary; owned temporary cleanup only; successful refusal JSON remains collectible; no success row before final metadata.
6. Collector rejects path traversal/absolute paths, wrong PID/device/frame/generation, symlink/nonregular files, stale capture-window files, source mutation, size/hash mismatch, unlogged/orphan/temp binary and log-versus-JSON mismatch. Failed later writer row revokes prior same-stem binary authorization. The copied log/source files remain untouched.

Run only affected host modules for implementation: `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_lattice_state_capture verification.analysis.test_snapshot_x3_run` plus the actual packet helper test if separate. Parent retains B1 runtime evidence and owns the later integrated candidate/full-suite checkpoint.

## Cost and native behavior

One additional466224-byte CPU array per armed upload F8 request, small metadata, at most two guarded pair copies, one466224-byte SHA-256 at Present, and one binary write. No per-draw allocation or new native resource access. Existing state getter timing stays separate from payload copy timing and Present I/O. Public Windows CryptoAPI/file operations provide native behavior; host parser/collector tests and cross-compilation do not claim native runtime execution. The only open dependency is the finalized B1 enum/API names and its accepted atomic-copy semantics; do not duplicate or weaken them in B2.
