# CPU floating-point instructions

The inspected X3AP executable uses x87 extensively, alongside some SSE2 scalar
double-precision instructions. This is direct static instruction evidence, not
an inference from the executable's age or 32-bit format.

Executable SHA256:
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.

The already identified node-to-world conversion path provides a concrete
rendering example: `0x004be277` converts an integer with `FILD`, then
`0x004be285` multiplies through x87. The same sequence repeats at
`0x004be2a8`/`0x004be2b0`. It follows integer fixed-point multiplication,
rounding and a 16-bit shift. See [node transforms](object-identity.md).
Elsewhere the executable contains x87 loads/stores, arithmetic, and control-word
save/load instructions. SSE2 `ADDSD` is present at `0x005108ab` and `MULSD`
at `0x0052bc20`; their presence does not establish which runtime path is selected.

Reproducing engine transforms therefore requires attention to both fixed-point
conversion and floating-point operation order/rounding. x87 supports extended
precision, but static opcode presence alone does not establish the active
precision-control mode or how long intermediate values remain in registers.
Prefer the actual submitted matrices for temporal history and verify numerical
reconstruction against captures. Do not assume an ordinary modern float matrix
multiply will be bit-identical.

This does not establish an x87 performance bottleneck. Static instruction counts
are not execution counts, and disassembling whole sections may include data or
unreached runtime-library paths. Any optimization needs a measured hot routine,
its calling/control-state contract and output-parity checks. No global FPU-mode
change or instruction replacement is made here. Raw executable disassembly
remains local and untracked.

## Our compilation policy

Per the user's preference, production C++ uses `-msse2 -mfpmath=sse` for its CPU
floating-point arithmetic. SSE2 is the baseline; newer SIMD can be introduced
after runtime support and useful acceleration are established. No global
fast-math policy is enabled. This does not alter original game code, shader
execution or the Windows i686 ABI: an ABI-required scalar return through ST0,
or x87 instructions in linked runtime libraries, may still occur. We do not
promise an x87-free DLL. Tests of engine reconstruction compare results explicitly
instead of silently changing the host FPU control mode to imitate the game.

The explicit stack flags `-mstackrealign -mincoming-stack-boundary=2` retain the
four-byte incoming Win32 contract and align SIMD locals internally. The installed
compiler's default already passed the tested legacy callback entries; these flags
make the contract explicit. See [ABI verification](../verification/sse2-abi.md).
