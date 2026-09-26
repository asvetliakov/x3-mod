# Review 36: shader compiler provenance

Independent Sol/xhigh review approved the offline generator correction with no
findings. The input list appends expanded shader includes after the compiler
DLL. Recording its final element as `compiler_sha256` therefore mislabeled the
RCAS include hash as the compiler identity in the two sharpen manifests.
The generator now indexes the named compiler path used by the actual command.

All ten authored shaders were recompiled through the serialized Wine wrapper.
Their bytecode and generated headers are unchanged. Every manifest now records
the actual compiler DLL SHA-256
`c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8`;
all ten also refresh the changed generator's source hash. This corrects current
provenance, not the historical observation recorded by earlier checkpoints.
The compiler remains an offline tool; its hash is not a renderer admission gate.

The host regression uses a fake external compiler to isolate manifest assembly
with an included shader. It passes with the fix and fails on the old expression
with the expected hash assertion (no setup errors). It does not claim shader
execution coverage. Independent review additionally verified all ten old/new
bytecode/header identities and the actual local DLL hash.

Evidence: [correction record](../../verification/results/shader-provenance-correction.json).
Old complete manifests remain local at `/tmp/x3-shader-provenance-before.json`;
the bounded native compiler log is `/tmp/x3-shader-provenance-regenerate.log`.
No renderer binary, installation or gameplay behavior changed.
