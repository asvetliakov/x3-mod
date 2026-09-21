# Local owned-media package and deployment

**Retired from production (2026-09-21).** The user accepts missing ID2 animated
textures after the Run55 LAVVideo crash. The owned playback implementation and
its runtime/build prerequisites have been removed. The description below is
historical; `--media-package` is no longer a launcher/install option. New managed
installs retire the active selection while retaining the previous installation
and provider files for rollback. See the [media ledger](../verification/media-cues.md#id2-video-omission-and-owned-playback-retirement-2026-09-21).

`tools/prepare_media_package.py` imports the ratified nine-module strict LAV
cohort and generated-timestamp MPEG1 Matroska asset from the existing accepted
records. It performs no download, build, remux, registration or launch. The
import record digests select the bounded local qualification input; delivery
hashes are not runtime renderer API prerequisites. A changed provider or timeline
requires its own behavioral qualification.

```
python3 tools/prepare_media_package.py \
  --graph-provider-record <accepted graph-provider.json> \
  --derived-record <accepted derived-record-v2.json> \
  --game-dir <original X3 directory> --output <fresh non-game staging>
python3 tools/manage.py install --game-dir <game> \
  --dll-source <reviewed frozen proxy> --media-package <staging/package.json>
```

Preparation verifies the original and derived identities, recorded payload and
ordinal evidence, x86 PE files, static imports against local exports, both COM
assembly manifests and the strict build/configuration records. It imports the
existing bytes unchanged. The nine binaries total 110,019,880 bytes; the derived
asset is 534,031,204 bytes. All large reads occur offline. These checks do not
repeat or replace the accepted decode/seek tests. Existing COPYING and README,
and strict FFmpeg LICENSE.md accompany the provider. The strict configuration
has GPL and version3 enabled and nonfree disabled. Public redistribution remains
unqualified: source/build/patch references are provenance, not a complete source
and third-party notice bundle. The derived game asset is a local user cache.

The root staging `package.json` has schema1, kind `x3-owned-media-package`,
`layout: staged`, `path_base: package_root`, `scope: local_qualification`,
`architecture: x86`, profile `lav081-strict-mpeg1-rgb32-v1` and
`distribution_qualified: false`. Provider file rows are an object keyed by exact
basenames, with `path`, `bytes`, `sha256`, `role`, `origin`. There are nine PE rows,
two manifest rows and three `notices/` rows. Runtime files outside that set are
refused. Paths use slash-separated portable components; traversal, Windows aliases,
case collisions and symlink/reparse destinations are refused.

The installed provider is immutable under
`x3-modern-media/providers/<package_id>/`. Its package record changes to
`layout: installed`, `path_base: media_root`; provider paths resolve against
`<game>/x3-modern-media`. The shared source cache is
`x3-modern-media/sources/<original-sha256>/00002.mkv`, alongside `source.json`.
The source record is schema1, kind `x3-owned-media-source`, layout `installed`,
path_base `game_root`. Both that source record and the package's source row carry
the same game-root-relative `asset`, original relative path `mov/00002.dat`,
original/asset sizes and hashes, source ID2, effective flags8, `codec: mpeg1video`,
`timeline: generated_timestamps`, and `derivation_record_sha256`. ID is the
engine source key. Only this source/profile is admitted by this local importer.

The existing installation manifest becomes schema2, retaining project, proxy
hash and source-commit provenance. Its optional `media` object contains
`package_id`, game-root-relative `package_record_relative` and digest, and one
source selection containing ID, flags, game-root-relative `source_record_relative`
and digest. A missing media object preserves DLL-only behavior; installing just
a replacement proxy preserves an existing media selection. The runtime reader
uses the proxy directory; process CWD, Wine paths and environment variables are
not path bases. The Python launch preflight verifies the selected files. This full offline check
reads at least 1.287 GB including the PE inspection pass; the recorded local
revalidation took 1.246 seconds. It is outside the C++ rendering hot path.

Mutation and launch use one advisory installer lock and a fail-closed game process
check. The launcher holds the lock from its final journal/selection validation
through the complete `launch_teed` child lifetime, including process creation.
Dry-run takes the same lock for validation and reporting, creates no child and
does not require the game to be closed; it can create the coordination lock file.
The legacy `tools/media_transcode.py` original-file swapping route (H.264 into
`mov/*.dat`) was retired with owned playback and its tool removed in the
2026-09-22 cleanup (batch 2). If such a transcode is already present, preflight
still rejects its original hash through `media_package.guard_legacy`.
No EXE, original MOV, CAT/DAT or bottle configuration write is performed.

`x3-modern-transaction.json` records a verified old snapshot, new snapshot and
phase (`prepared`, `dll`, `manifest`). The proxy replacement precedes the atomic
manifest commit. Any journal blocks launch and further mutations. `manage.py
recover` restores the verified pre-transaction pair, including after a completed
manifest replacement whose journal remains. Recovery refuses unknown current
bytes. `rollback` selects the one retained prior pair using the same protocol;
its `previous` pointer can name only an owned rollback UUID directory. Rollback
keeps the former current pair as the new previous. Snapshots are exact DLL and
manifest bytes. Immutable provider/source data is retained across updates;
unchanged source caches are reused, and there is no automatic orphan cache GC.

`uninstall` removes exact unchanged manifest-owned payload files and reports
retained changed payloads or foreign additions. Changed installation, package or
source control records abort uninstall before mutation because ownership cannot
be established from them. Captures and originals remain untouched. Recovery
of an interrupted uninstall restores the old pair even if media files were
already changed; normal launch validation still refuses those changed bytes.
Abrupt interruption during pre-journal staging can leave an unselected immutable
cache or snapshot directory, but cannot change the active DLL/manifest pair.
Native Python uses documented Windows task listing and file locking; native
Windows execution and relocated runtime cohort observation remain unverified.
The launcher itself still requires CrossOver Preview.
