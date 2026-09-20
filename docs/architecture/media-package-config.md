# Media package configuration reader

2026-09-20. The preparation-time reader supplies an immutable provider manifest
and source mapping for owned media workers. Its host/parser and synthetic Win32
file-adapter qualification are recorded in the
[media ledger](../verification/media-cues.md#media-package-reader-file-lifetime-checkpoint-2026-09-20)
and [compact result](../../verification/results/media-package-config-2026-09-20.json).
Real provider/decoder opens with retained configuration are now qualified by the
[canonical worker fixtures](media-lav-worker.md#package-integration-and-measured-scope).
Native Windows execution and enabled engine playback remain separate work.

## Configuration and lifetime

[`load_package_config`](../../src/media/package_config.h) receives the pinned
proxy HMODULE and resolves its module directory with documented Windows APIs.
Discovery does not depend on CWD, environment variables or a temporary-directory
convention. Invoke it during asynchronous service preparation, before graph work;
engine constructors and pumps must consume the prepared result.

The reader accepts installation schema 2, package/source schema 1 with explicit
installed layouts and path bases, the x86 `lav081-strict-mpeg1-rgb32-v1` profile,
the two qualified CLSIDs, and exactly source ID 2 with effective flags 8. Provider
records contain the nine named PE files, two manifests and three notice files.
Paths are confined to the selected app-local media layout; absolute paths,
traversal, Windows device components, unknown runtime rows and reparse points are
rejected. SHA256 equality binds the two small package/source records to the
installation selection. Provider and media hashes remain installation provenance;
the reader does not hash their contents or impose a binary-version hash prerequisite.

Missing installation/media selection returns `disabled`; unresolved transaction
journals, missing files and invalid selected records/paths return explicit errors. Failure leaves
the caller's output unchanged. Public entry points contain C++ exceptions, and
the Windows entry preserves LastError. Successful publication exposes UTF-16
manifest/source paths through `shared_ptr<const PackageConfig>`. The integration
must retain that shared owner through both workers' context, graph and source
lifetimes, including any retained unsafe worker. Copying only the path strings
would release the configuration's file handles too early.

The Win32 adapter retains selected file handles opened with `GENERIC_READ` and
`FILE_SHARE_READ`. Directory handles request
`FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES`, share read/write and omit delete
sharing. It checks type/reparse attributes and normalized final handle paths
while resolving components. These are documented access/share contracts; no
Wine-specific export or layout is used. Supported installation also refuses
mutation while the game is running. This is not a guarantee against arbitrary
host-side namespace changes that bypass Windows sharing, or hostile changes to
ancestors outside the selected directory tree.

## Directory access correction and measured scope

The first actual file-adapter fixture passed 41 checks, then failed its provider
directory rename denial at check 42: `MoveFileExW` succeeded while configuration
handles remained alive. Its old diagnostic printed stale LastError 32 and did
not check restoration; the frozen executable and failed log are retained in the
compact result. That failure remains a failed qualification.

The cause was the adapter's attribute-only directory access. The
[CreateFileW sharing contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
exempts attribute/extended-attribute access from sharing restrictions, while
omitting `FILE_SHARE_DELETE` prevents subsequent delete access, including rename.
[`FILE_LIST_DIRECTORY`](https://learn.microsoft.com/en-us/windows/win32/fileio/file-access-rights-constants)
adds directory read/list access. The focused correction adds that access to both
root and traversed directory handles, retaining existing sharing flags and all
file policies. The fixture now logs the actual rename BOOL, source/target paths
and existence, uses no stale success error, and checks restoration if a supposedly
forbidden rename succeeds.

The corrected fixture passed **150/150 checks, zero unsupported cases**, on X3
under CrossOver Preview. Both provider-directory rename attempts returned false
with error 32; six selected file rename attempts did likewise. Read sharing worked,
write access stayed denied through the final shared configuration owner, and actual
byte-preserving writes and renames succeeded after release. File and directory
symlink escapes were created and refused; they were not skipped as unsupported.
The fixture also checks unrelated CWD, Unicode/space relocation, exact eligibility,
LastError, missing selections/files, journals, oversized records and digest mismatch.

Only **6,983 bytes** of synthetic record/file data are prepared; named provider
and source files are dummy bytes and cannot serve as decoder inputs. The fixture
uses the self EXE's pinned HMODULE to exercise the real production Win32 adapter.
Its result therefore establishes the tested file API behavior on CrossOver, with
native Windows still unverified. Actual two-worker COM/decoder opens with the
same retained owner are separately qualified by the canonical worker fixtures.

## Bounds and cost

The shared production parser limits each JSON document to 128 KiB, depth 16 and
4096 values. Its numeric subset accepts unsigned integers only, matching the
installer's current records, including provenance. Strict UTF-8, duplicate keys,
integer overflow and malformed JSON are rejected. A valid selection reads three
small records and performs 16 cached file-resolution calls; the repeated provider
manifest lookup reuses its retained handle. SHA256 runs only over the small
records. This work allocates during preparation and does no provider probing,
loading, COM, remuxing or decoding. No per-frame work or game-performance benefit
is established by this checkpoint.
