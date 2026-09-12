# The script signature check `0x004cabc0`

Scope: the CryptoAPI signature verification that costs 76.6 % of the save-load
"script/XML" stall on the X3/FEX bottle (run B: stall B 16.758 s, of which the
`signature_check` probe `0x004cabc0` = 844 calls / 12.835 s, 99.9 % of it inside
the hooked ADVAPI32 imports; `CryptAcquireContextA` 2,532 calls = exactly 3.00
per check, 844 of them failing, 10.346 s = 4.086 ms each = 61.7 % of the stall).
This note establishes the exact call sequence, its arguments, where the public
key comes from and what a failure means to the callers, from a read-only
decompile of `0x004cabc0`, `0x004cae90` and the script VM dispatcher `0x004ab880`
(decompiler output stayed under `/tmp/x3-crypt-study`, untracked). Addresses are
preferred VAs, image base `0x00400000`. The mitigation built on it is
`X3M_CRYPT_CACHE=1` ([docs/verification/crypt-cache.md](../verification/crypt-cache.md)).

## 1. Signature

```
char* 0x004cabc0(EAX = const BYTE* text,          // the signed bytes (script text)
                 DWORD text_length,               // [esp+4]
                 const char* signature_base64,    // [esp+8]
                 unsigned signature_base64_length,// [esp+0xc]
                 const BYTE* public_key_blob,     // [esp+0x10]  PUBLICKEYBLOB
                 DWORD public_key_blob_length,    // [esp+0x14]  always 0x114 = 276
                 int* out_length)                 // [esp+0x18]
```

Custom convention: the text pointer arrives in `EAX`, the rest on the stack.
Every argument is validated up front (non-null, lengths > 0) and `*out_length`
is zeroed; a bad argument returns 0 without touching CryptoAPI. The return value
is a game-heap buffer (`0x004b89f0`, the tracked allocator) holding the
**base64 of the MD5 digest** of `text`, `*out_length` its length — or 0 when
the signature does not verify (or anything before the verify fails).

## 2. The exact call sequence

| # | Call | Arguments | Result the game expects |
| --- | --- | --- | --- |
| 1 | `CryptAcquireContextA` | `&prov, "X2EgosoftCSPContainer", "Microsoft Base Cryptographic Provider v1.0", PROV_RSA_FULL (1), CRYPT_DELETEKEYSET (0x10)` | **return value ignored.** In steady state the container does not exist (call 10 of the previous check removed it), so this fails with `NTE_BAD_KEYSET` (0x80090016). This is the one failing acquire per check the probes counted (844 of 2,532). |
| 2 | `CryptAcquireContextA` | same container/provider/type, `CRYPT_NEWKEYSET (8)` | must succeed (else return 0). Creates the named container: registry/keystore work in every CSP, ~4 ms under Wine. |
| 3 | `CryptImportKey` | `prov, blob, 0x114, hPubKey=0, flags=0, &key` | RSA-2048 `PUBLICKEYBLOB` (8-byte `BLOBHEADER` + 12-byte `RSAPUBKEY` + 256-byte modulus). |
| 4 | `CryptCreateHash` | `prov, CALG_MD5 (0x8003), hKey=0, 0, &hash` | |
| 5 | `CryptHashData` | `hash, text, text_length, 0` | the whole script text. |
| 6 | (game) | base64 length arithmetic on `signature_base64` (`=` padding), decode with `0x004c8420` into a game-heap buffer | signature length = 3/4 of the base64 length minus padding; a base64 length that is not a multiple of 4 gives length 0 (the verify then fails). |
| 7 | `CryptVerifySignatureA` | `hash, signature, signature_length, key, sDescription=NULL, 0` | the verdict. |
| 8 | `CryptGetHashParam` ×2 | `HP_HASHSIZE (4)` then `HP_HASHVAL (2)` | only after a successful verify; the digest is base64-encoded with `0x004c82d0` into the returned buffer. |
| 9 | `CryptDestroyHash`, `CryptDestroyKey`, `CryptReleaseContext(prov, 0)` | | unconditional cleanup of whatever was created. |
| 10 | `CryptAcquireContextA` | same, `CRYPT_DELETEKEYSET` | **return value ignored.** Deletes the container again (succeeds in steady state). With `CRYPT_DELETEKEYSET` the CSP does not return a handle, so nothing is released after 1 or 10. |

So the three acquires per check are: **delete (fails, ignored) → create
(must succeed) → … → delete (succeeds, ignored)**. The container is a scratch
object that exists only for the duration of one check. Nothing is ever stored
in it: the key is imported from a blob and the hash is keyless. The first
acquire is not a probe the game reads; its failure is simply the steady state
of a "delete whatever is left" call. Both deletes are `CRYPT_DELETEKEYSET`
with no other flag (no `CRYPT_MACHINE_KEYSET`, no `CRYPT_VERIFYCONTEXT`), so
they are per-user registry operations (`HKCU\Software\Microsoft\Cryptography\
UserKeys\X2EgosoftCSPContainer` on Windows; Wine's `rsaenh` keeps the same
layout in the bottle registry).

Cost model (run B, X3/FEX): 2,532 acquires, 10.346 s — the delete and create
each pay the registry round trip, the failing delete included; the remaining
2.5 s of the 12.835 s is `CryptImportKey` (RSA-2048 public key parse),
`CryptVerifySignatureA` (one modular exponentiation) and the MD5 of the text.
A keyed provider handle would be equally valid for every check: the
verification depends only on the imported blob, the text and the signature.

## 3. Where the public key comes from

Both call sites pass the same 276-byte blob:

* `0x004cae90` (two calls) builds it on the stack: a 0x114-byte obfuscated
  constant is written byte by byte, then decoded in place with a rolling
  scheme (each output byte XORs the running pair difference with the
  characters of the string at `DAT_005638cc`, cycling; mirrored write from both
  ends of the 0x115-byte output). The result is deterministic — the same blob
  for every call — and is `_free`'d at the end of the function (`-0x115` on the
  tracked heap counters `DAT_006085f4`/`DAT_006085f8`).
* `0x004af494` inside the script VM dispatcher `0x004ab880` (opcode case
  `0x95`) receives the blob from the VM's argument conversion (`0x004aa260`,
  `0x004a9fd0` per byte) — i.e. from the caller's data, which in practice is
  the same key material the engine ships; the fixture and the cache do not
  assume it is: the cache keys the imported key on the blob bytes.

Because the blob is constant and `CryptImportKey` has no side effect on the
provider beyond creating the key object, the imported `HCRYPTKEY` can be kept
across checks on the same provider handle.

## 4. Failure semantics

* Inside `0x004cabc0`: any failure of calls 2–7 skips the digest and returns 0;
  the cleanup of step 9 still runs for whatever was created, and the final
  delete always runs. A failing verify is not special: it returns 0 exactly
  like a failing import.
* Script VM site `0x004af494` (opcode `0x95`, three operands: a string/array
  value, a string and a third value, all type-checked before the call): the VM
  copies the operand bytes into a game-heap buffer, converts the signature
  string, calls `0x004cabc0`, frees the buffer, and — when the result is
  non-null with `*out_length > 0` — calls `0x004a4830(context)` (push the
  true/digest result) and returns 1; otherwise it falls to the shared
  `0x004a47f0(context, 0)` path (push 0). **The game does not refuse the
  script**: the verification verdict becomes the script command's return
  value. 844 checks per save load = one per script document (~790) plus the
  story/mission scripts that verify twice.
* `0x004cae90` (called for the story object): verifies the whole text, then
  locates the `val="L\x3story.obj" /><sval type="string" val="` marker and
  verifies that section with a second call; returns 1 only if the second
  result is non-null, else 0 after freeing everything. The caller of
  `0x004cae90` decides what a 0 means; nothing in the two functions writes to
  disk or changes global state beyond the tracked heap counters.

## 5. Consequences for the cache

The observable contract of the sequence is: the return values of calls 2–8
(and the digest bytes), plus, for the two ignored deletes, nothing at all. A
cache that keeps the provider handle from call 2 alive across checks, answers
call 2 from it, turns `CryptReleaseContext` into a mark and emulates the two
deletes (fail while "deleted", succeed after a release) reproduces every value
the game reads — and, for the fixture's stricter comparison, also the
`GetLastError` of the failing delete, recorded from the first real failure.
The imported key is cached per (provider handle, flags, blob bytes) and its
`CryptDestroyKey` suppressed; hash creation, hashing, verification and the
hash parameters are not touched, so the verdict is computed by the CSP exactly
as before. Expected saving from run B: the 10.346 s of acquires and most of
the 2.5 s of imports — 11.3–12.8 s of the 16.758 s stall.

Related: [script-xml-load-stall.md](script-xml-load-stall.md) §6 item 4 (the
measurement request this note answers), [loading-probes.md](loading-probes.md)
(the `signature_check` probe row and the ADVAPI32 import rows).
