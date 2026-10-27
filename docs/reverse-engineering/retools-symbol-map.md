# RETools symbol map of X3AP.exe

`tools/analysis/x3ap_symbols_retools.json` names the function starts of the executable identified in
[executable.md](executable.md) (SHA-256 `fdbf3418…f8ab`, preferred VAs, image base `0x00400000`). That is 3,059
functions; exception funclets (`Catch`/`Unwind` blocks) and import thunks are left out. It is exported from a durable Ghidra database kept with [RETools](https://github.com/DIMOSUS/RETools)
(project `x3ap`).

The file holds addresses, names, sizes, prototypes and a confidence only. It contains no decompiler output,
disassembly or bytes of the game.

## Format

It uses the schema of `x3ap_function_labels.json`, so the profile tooling takes it unchanged:

```sh
python3 tools/analysis/analyze_loading_profile.py <run-dir> --labels tools/analysis/x3ap_symbols_retools.json
```

Each entry under `labels` (key: the function's entry VA) has these fields:

| Field | Meaning |
|---|---|
| `label` | The function's name, `Namespace::Name` (for example `Script::SADispatcher`, `Texture::EnsureLoadedById`). |
| `source` | Always this database. |
| `size` | Bytes from the entry to the end of the body. |
| `prototype` | Placement and types as established from the code. Register arguments are in IDA notation: `__usercall` / `__userpurge` with `@<reg>`, the result register after the name when it is not EAX, and `__userpurge` when the callee pops stack arguments. Otherwise it is a C prototype with its convention. |
| `keeps_registers` | ECX and/or EDX: registers the function never clobbers on any path, its callees included, although every standard convention allows it to. This is common for the image's whole-program-optimised code: a caller may keep a value in them across the call. |
| `confidence` | `high` / `medium` / `low`: the confidence of the name, as its evidence tag in the database states. It is absent for statically linked CRT/STL code named by FID or by its library role, and for untagged entries. |

The image is linked by MSVC linker 8.0 with whole-program optimisation. Many internal functions take arguments in
registers that no standard convention would use, such as `this` in ESI or EAX, or a size in EDX. The prototypes
record that placement. The parameter names are often only register or stack-slot names (`ecx`, `s04`) where the role
of an argument was not settled.

## How the names were established

The database started from this repository's notes: role labels, the sites under `src/proxy/` and the documented
addresses. Every function was then named or checked from the code itself (the statically linked CRT/STL partly
by FID). Calling conventions come from register liveness, the
callers' stores and the `RET` / caller cleanup at the call sites. Floating-point arguments and
results come from the x87 accesses. Indirect calls through COM vtables and function pointers were resolved where the
object's type is proven.

A journal of every change is replayed into a fresh import and compared with the working database (identical). The
database also rebuilds the executable byte for byte from its own export, which checks its code/data split and the
data references it found (a rebuild with code moved by padding changes no data but those references). The rebuild material is not published here, since it is the game's code.

Corrections of this repository's notes that came out of the comparison are in the owning notes and the role table;
the pull request that added this file lists them.
