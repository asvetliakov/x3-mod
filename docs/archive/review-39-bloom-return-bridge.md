# Review 39: compositor return bridge prototype

2026-09-13. Independent Sol high review approved the verification-only
[return bridge](../architecture/bloom-return-bridge.md), after fixing one
medium verification finding. There is no production hook integration.

The runner previously accepted any reported check count with a zero-failure
terminal line. It now requires exactly 240 checks and the complete case
inventory, zero exit status, no failure lines, one terminal record and stable
before/after source and executable hashes. Six independent host controls
reject under/over counts, malformed/duplicate records and other false passes.

Refreshed Steam and X3/FEX runs each passed 240 checks, with the same binary
and stable inputs. The [scoped summary](../../verification/results/bloom-return-bridge-summary.json)
binds the retained records and logs. The reviewer independently checked both
records/current hashes and reproduced the generated SEH assembly and linked
code/data sections in a separate cross-build.

Code review confirmed immediate original-output capture, restoration ordering,
four-byte stack handling and compiler-generated SEH cleanup across normal,
exceptional and continued returns. No remaining CPU/SEH correctness finding
was identified. Fixed stack storage adds no heap allocation, lock or per-draw
work; actual compositor overhead is still unmeasured.

Compiler/linker packaging, production imports and SafeSEH treatment require
integration review. Native Windows has not run. Synthetic CPU checks do not
qualify the game's transitive calling behavior, device ownership, Reset
revocation or GPU state/image recovery. These remain implementation gates.
