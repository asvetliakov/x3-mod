# Archive-wide exact shader lookup

The production registry covers the complete reviewed installation, rather than
only shaders encountered during captured scenes. Exact whole-program FNV-1a-64
and DWORD length select immutable metadata; unknown or altered programs reject.
No shader code is distributed in these tables. A shader match supplies a narrow
arithmetic/coverage fact and does not authorize replay or temporal history.

| API / outcome | Archive programs | Meaning |
| --- | ---: | --- |
| `find_rigid_position` | 234 VS | POSITION0.xyz with forced W=1, four submitted row dots |
| `classify_vertex_position`: direct XYZW | 18 VS | Original converted POSITION0.xyzw copied to clip output |
| Direct XYZ/W=1 | 2 VS | Input XYZ preserved, homogeneous W forced to one |
| View/XY expansion/projection | 2 VS | Billboard path requires its separate input/history contract |
| `find_pixel_coverage` | 494 PS | No shader discard or explicit/legacy shader depth write |
| Pixel coverage unproved | 1 PS | Malformed destination syntax; deliberately omitted |

All **256 VS** are classified. The 234 row-dot entries contain 97 c0, 70 c6 and
67 c24 matrix layouts; 224 have a WorldViewProjection name hint and 10 a
ViewProjection hint. Names never establish input space or temporal rigidity.
The [archive position proof](archive-position-paths.md) and
[captured input evidence](rigid-position-profiles.md) retain their original
reports. The extra 218 rigid entries are backed by the same independently
reviewed structural proof, without assuming that unseen live draws meet the
input, state or history requirements.

The row-dot metadata also retains the original shader version, position output
issue order and homogeneous constructor. These distinguish replay contracts that
the finite-input algebra alone collapses:

| Source model | XYZW order | WXYZ order |
| --- | ---: | ---: |
| VS 1.1 | 124 | 0 |
| VS 2.0 | 48 | 6 |
| VS 2.1 | 24 | 0 |
| VS 3.0 | 32 | 0 |

All 234 use `MAD temp, input.xyzx, literal.xxxy, literal.yyyx`, with local
literal X exactly +1 and Y exactly +0. Original DP4 operands are temporary/row.
Literal Z/W and stored POSITION W are unused by this constructor. The generator
derives issue order from the pinned proof's original instruction offsets after
reproducing the proof from raw bytes. Unsupported model/order/constructor values
reject; a hand-written four-field legacy profile defaults its added fields to
zero/Unknown. None of these fields grants exceptional-payload, raster or temporal
equivalence. The initial token-exact replay investigation targets the 32 SM3
contracts; legacy shader-model qualification remains separate.

## Generation and provenance

[`generate_shader_profiles.py`](../../tools/analysis/generate_shader_profiles.py)
requires the exact reviewed archive-position and shader-sweep SHA-256 digests.
It validates every local program's SHA-256/FNV, reproduces all 256 position proofs,
and derives the 495 pixel coverage decisions from the same full bytes and
explicit semantic evidence. The generated
[registry manifest](../../verification/results/shader-profile-registry.json)
records all 751 identities, decisions, tool hashes and include hashes. Tables are
sorted by numeric full-program FNV with duplicate hashes rejected across stages.
Changing the pinned inventories requires renewed review, not automatic discovery
promotion. Raw programs stay under the local `/tmp/x3-shader-sweep/programs` tree.

```sh
python3 tools/analysis/generate_shader_profiles.py --check
python3 -m unittest verification.analysis.test_shader_profile_generation \
  verification.analysis.test_pixel_coverage
python3 verification/probe/run_rigid_position_profiles.py
```

Generation is deterministic. Its ten original-metadata tests cover stage
bounds, layout/category rejection, duplicate/invalid fingerprints, missing
coverage evidence and pinned digest mismatch, as well as original issue-order
derivation and rejection of missing/unknown replay metadata. The coverage proof has 29 original
token tests. The existing 16 position-proof tests remain unchanged.

## Pixel coverage proof and limits

[`inspect_pixel_coverage.py`](../../tools/analysis/inspect_pixel_coverage.py)
requires explicit inventory stage/model, instruction count, opcode histogram,
discard/depth-write index arrays and syntax warnings. It independently walks
complete raw instruction boundaries, including legacy SM1 forms, 2.1 and SM3.
Comments and local definition literals are skipped as data. Its raw opcode
histogram and count must match the inventory. Missing evidence never means
"no discard".

The gate rejects TEXKILL (65), legacy TEXM3x2DEPTH/TEXDEPTH (84/87), depth-output
register destinations, unknown opcode/framing/destination forms, predication,
relative addressing, missing END and trailing data. Other known control flow
can be accepted: both branch bodies are scanned for coverage-changing work.
This proves absence of shader discard/depth output, not all shader semantics or
validity of every possible source operand. The sole rejected installed PS,
`d66dd16fc0a6c3a3`, has an unresolved destination register type; removing its
inventory warning still causes the raw destination check to reject it.

Render-state alpha testing, stencil, blend policy, clipping, multisampling,
viewport/scissor, targets/depth surface, topology and vertex path remain separate
coverage gates. A pixel lookup does not prove finite vertex data, lifetime,
buffer retention, matching current/previous draws, exclusive final-color pass
ownership, or safe GPU replay. Nor does it imply shading or lighting equivalence.
The 67 c24 vertex profiles still require the documented light-index guard for
constant-row jitter; that condition is not part of the lookup.

## Bounded runtime and executable verification

The API uses no COM calls or allocations. It first rejects null, impossible
lengths and lengths absent from its table, then computes the existing bytewise
little-endian FNV and uses binary lookup. Maximum complete program sizes,
including comments and END, are **769 VS DWORDs** and **1883 PS DWORDs**; a
4096-DWORD caller scratch buffer therefore covers the reviewed installation.
Caller memory must still hold the complete readable span supplied to the API.
The fingerprint is an exact-profile identifier in this trusted local workflow,
not a cryptographic authenticity boundary.

The fresh x86 Win32 fixture validates all 751 local programs through the actual
production functions: all 256 position categories, 234 row registers/name hints,
shader models, constructor semantics and original write orders,
and 494 pixel coverage positives agree. Every DWORD's low bit is independently
flipped; all **547,927 mutations** reject. Baseline tests also reject null, zero,
SIZE_MAX, over-cap, truncated and appended lengths. Source files, executable,
manifest and original program SHA-256 hashes are checked before and after the
run; output is hashed from exact bytes. The
[lookup summary](../../verification/results/rigid-position-lookup-summary.json)
and [log](../../verification/results/rigid-position-lookup-wine.log) retain only
derived facts. This is CPU-only verification: no D3D device, game launch or
production installation occurs.

Independent review reproduced deterministic generation and the 55 synthetic
checks, verified every raw-program identity and retained Win32 provenance, and
accepted the production lookup/coverage gates. The runner now invalidates an
older PASS before reading any source; the complete Win32 sweep was rerun after
that hardening with the same 751 classifications and 547,927 rejected mutations.
The replay-metadata extension was independently checked against version words,
MAD operands/literals and original DP4 tokens at pinned proof offsets; fixture
expectations do not import the production generator. Its fresh sweep retains
the same coverage totals and explicitly verifies unknown aggregate defaults.
