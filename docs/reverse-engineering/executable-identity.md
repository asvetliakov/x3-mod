# X3AP.exe identity: structure plus verified sites

Rule (2026-09-23): the proxy accepts X3AP.exe when its **structure** is the known
image and every **site** it touches holds the expected bytes. File hashes are
provenance: logged, recorded and compared with the known list as INFO, never a
gate. The LARGE_ADDRESS_AWARE bit (0x20 in `IMAGE_FILE_HEADER.Characteristics`)
and the optional-header `CheckSum` are free, so the file with the "4GB patch"
verifies exactly like the shipped one.

## Structure

`src/proxy/executable_identity.h` (C++) and `verification/probe/exe_identity.py`
(host mirror; `test_exe_identity` compares the two tables):

| Field | Value |
|---|---|
| Base / ImageBase | `0x00400000` (the module's actual base must equal it) |
| Machine, optional header | i386, PE32 (`0x10b`), SizeOfOptionalHeader `0xe0` |
| TimeDateStamp | `0x5a1d70ad` |
| Characteristics without bit 0x20 | `0x0103` (relocations stripped, executable, 32-bit) |
| SizeOfImage / entry RVA | `0x002f5000` / `0x00112ead` |
| Sections | `.text`, `.rdata`, `.data`, `.rsrc`: name, VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData, Characteristics all equal |
| File size | 2,153,984 bytes (kept: every site the proxy uses is byte-checked) |

## Sites

* **Patched sites**: every module that writes code checks its own whole
  instructions first: `engine_patch::claim`/`claim_call` (expected bytes), the
  stamp groups' `verify_bytes` preflight, or a module `memcmp` window (lod_scale,
  point_light_admission, pause_key_only, scene_hook, object_trace's call bytes,
  object_lifetime's six region fingerprints). Unchanged by this rule.
* **Engine globals**: 41 anchors, one per global the proxy reads. Each anchor
  is a whole `.text` instruction whose absolute operand is that global
  (prefix + le32(global) [+ imm8]), chosen from objdump's linear sweep; the
  verifier re-confirms every anchor VA is an instruction start. No anchor
  overlaps a patched site (host test); `0x00607ce8` is anchored at `0x00445a3a`
  because its only other reference, `0x004074de`, is a `chase_aim_trace` site;
  `0x00606f44` (media record list head, read by music_keep) at `0x004971d3`, a
  branch target outside the music patch sites (`0x004982b0`, `0x004982db`,
  `0x00498810`, `0x00498c90`, `0x00498d54`).
  Every data-range literal under `src/proxy` is anchored except five that are
  not reads (two comment range ends, three d3dx9_37 addresses in
  `loading_trace.cpp`); `test_exe_identity` enforces this.
* **Engine call targets**: the bodies of fixed engine callees a module calls
  or redirects to are each module's responsibility and are not covered by the
  anchors; collide_memo, resource_reader and music_keep already hash theirs.

Module audit (every caller of `object_trace::executable_verified()`):

| Modules | Own site check | Globals |
|---|---|---|
| chase_camera, chase_aim_trace, chase_fire, chase_lead, chase_transition, collide_box_cull, collide_memo, collide_narrow_census, collide_query_phases, collide_sat_sse2, cull_census, cull_small_parts, frame/game/light/loop/pass/residual/submit phases, loading_probes, lod_scale, media_cue, pause_key_only, point_light_admission, resource_reader, scene_hook, sun_occlusion, voice_dmo_fallback | yes (engine_patch or memcmp) | anchored in the gate |
| camera_state, capture (sector background, config reads), sun_light_poll, motion_output own-ship (`motion_output_shadow_adaptive_inc.h`) | none: read only | anchored in the gate (the check these modules gained) |
| music_keep | yes: the 26-byte seek head and the 0x57-byte pause body | anchored in the gate (`0x00606f44`) |
| object_lifetime (own gate) | six region fingerprints | same structure + anchors + size |

Enforced in `object_trace::verified_image()` (cached once per process; ~41 short
reads and one `GetFileSizeEx`, no 2 MB hash) and `object_lifetime::initialize()`.
Host verifiers (`verify_*_site(s).py`, `verify_pause_sites.py`,
`verify_music_restart_sites.py`, `inspect_object_lifetimes.py`,
`build_collide_memo.py`) check `exe_identity.identity_ok()` plus their sites and
report `exe_info` (raw hash, known label, LAA, CheckSum).

## Known files

| File | Raw SHA-256 | LAA | CheckSum | Status |
|---|---|---|---|---|
| Shipped (installed in bottle X3) | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` | set | 0 | measured |
| LAA cleared | `9d8ddf43f06031f4fb21e5e700d55a2df8881c76bd8c5ce4a9af0866d6dc38c8` | clear | 0 | measured on a scratch copy |
| NTCore `4gb_patch.exe` output | `5d6b741e7269b40e2a7df850fc274d593d0b39ac647e0b0205b64737c93190c3` | set | `0x0021bd66` | inferred (tool behaviour below; checksum algorithm reproduces the stored CheckSum of all 4 game DLLs that carry one) |

The masked digest (LAA cleared, CheckSum zeroed) is `9d8ddf43…` for all three
(`exe_identity.identity_digest`, reported as `known_image`).

**The shipped X3AP.exe is already large address aware** (Characteristics
`0x0123`, measured; `executable.md` lists the same). No patch is needed:
`apply_laa.py` is unnecessary and must not be run with `--apply` on the bottle,
and `4gb_patch.exe` must not be run either (it would only rewrite CheckSum and
change the hash). The proxy logs `exe_laa=` (the bit in the mapped headers) and
`exe_max_app=` (`GetSystemInfo().lpMaximumApplicationAddress`, documented as
`7ffeffff` without LAA and `fffeffff` with it under WOW64) so the next session log
measures the limit the process actually got.

`4gb_patch.exe` (Daniel Pistelli, NTCore 2007, 45,056 bytes in the game
directory), from its disassembly (`0x004010f8`–`0x00401148`) and imports: copies
the target to `<name>.Backup` (`CopyFileW`), reads the first min(size, 0x8000)
bytes, `or word [nt+0x16], 0x20` unconditionally, writes the block back, calls
`imagehlp!MapFileAndCheckSumW` and on success stores the result at `nt+0x58`
(`OptionalHeader.CheckSum`) and writes again. So it does recompute CheckSum, and
on this already-LAA file it changes only CheckSum 0 → `0x0021bd66`.
`tools/analysis/apply_laa.py --exe PATH [--apply]` exists for an LAA-cleared
copy only (on the shipped file it stops with `already_set`): reports the bit and both hashes; with `--apply` it sets only the
bit (CheckSum untouched), only on the known image, only when the bit is clear and
the game is not running (`game_guard.py`), keeping `X3AP.exe.x3m-pre-laa`.

## Large address space under Wine and Windows

Documented behaviour, not measured in this project:

* Windows: a 32-bit image with IMAGE_FILE_LARGE_ADDRESS_AWARE running under
  WOW64 on 64-bit Windows gets a 4 GB user address space (2 GB without it);
  Microsoft's `/LARGEADDRESSAWARE` and "Memory Limits for Windows Releases"
  documentation.
* Wine: `dlls/ntdll/unix/virtual.c`, `virtual_set_large_address_space()`, runs
  once the main image is mapped. For a WoW64 process (CrossOver's arm64 Wine
  runs X3AP.exe this way, through FEX) it sets the 32-bit user limit to just
  under 4 GB when the main image is large address aware and to 2 GB otherwise;
  on a 32-bit Wine it releases the reserved area above `0x80000000` only for an
  LAA image. Read from Wine's source as recalled, not checked against the
  CrossOver Preview build here.

## Evidence and tools

* `verification/results/executable-identity/run_verifiers.py`: all 23
  identity-carrying verifiers on the shipped, LAA-cleared, 4GB-patch and
  unknown-hash variants (PASS each), a different-build copy (link stamp flipped:
  every verifier FAIL, identity false) and two site-corrupted copies (FAIL with
  the identity still passing); output `verifiers.json` beside it, with the anchor
  count and the source commit.
* `verification/results/executable-identity/pe_checksum_check.py`: the CheckSum
  reimplementation against the game DLLs and the 4GB-patch prediction.
* Launcher: `tools/manage.py` install records `executable` (sha256, bytes, laa,
  checksum, known, identity_ok) in `x3-modern-install.json`; `launch --dry-run`
  prints the same record. It never refuses: a failed identity warns that the
  structural gate disables every hook module; an unknown hash with a passing
  identity is an `Info:` line.
* Session log: `exe_sha256= exe_bytes= exe_hash_us= exe_laa= exe_max_app=` on the
  `proxy_identity` line; `attach_us` keeps its old scope (the executable hash is
  timed separately as `exe_hash_us`).

## Known, accepted limitations

* `object_lifetime::initialize()` repeats the structure/anchor/size check instead
  of calling `object_trace::executable_verified()` (its fixture builds without
  object_trace); both use the same inline functions of `executable_identity.h`.
* The host test that no anchor overlaps a patched site matches site addresses
  written as literals in `src/proxy` (SiteSpec rows and `*site_va =` constants);
  a site computed at run time would not be seen.
