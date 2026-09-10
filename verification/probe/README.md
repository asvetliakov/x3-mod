# Standalone Windows graphics verification

These disposable 32-bit programs are separate from production source. Build with
`sh verification/probe/build.sh` using MinGW. They open a small temporary window,
close it automatically, and do not launch the game or modify its configuration.

Run from the project root with the actual installed **CrossOver Preview** runtime:

```sh
"/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine" \
  --bottle Steam --no-update --workdir "$PWD/verification/probe/build" \
  "$PWD/verification/probe/build/capability_probe.exe" \
  > verification/results/graphics-capabilities.txt \
  2> verification/results/graphics-capabilities-wine.log
```

`capability_probe.exe` checks D3D9 shader/render-target/depth capabilities and
creates an FP16 RT. It separately creates a D3D11 device and FP16 flip-discard
DXGI swapchain, queries output metadata and scRGB support, selects scRGB, and
presents. It prints loaded Windows module paths. Module paths alone do not
identify native host translation libraries.

`d3d9_smoke.exe` exercises drawing and presentation, texture ownership, COM
identity, pixel shader creation/bytecode/ownership, state block capture/application, device reset, destruction, and a
second device. It exits nonzero on failed checks. To test a proxy, copy this
executable and the proxy DLL into an isolated test directory and pass the
process-local `--dll d3d9=n,b` option to Preview Wine. Keep baseline and proxy
reports separate; do not replace the bottle's system DLL or its settings.

## Observed 2026-09-10, Preview Steam bottle

- D3D9 exposes shader model 3, four MRTs, FP16 RT/filter/blend and INTZ depth
  texture support. FP16 texture allocation, clear and present succeeded.
- D3D9 reports an emulated NVIDIA GeForce 8800 GTX adapter and loads
  `wined3d.dll`; this is not the host's physical GPU identity.
- D3D11 exposes feature level 11.0 on Apple M5 Pro. RGBA16F supports render
  targets, blending and sampling.
- Output6 reports two attached 10-bit outputs in PQ/Rec.2020 color space
  (enum 12), with maximum luminance metadata of 400 and 1600 nits respectively.
- FP16 flip-discard creation, scRGB present-support query, scRGB selection and
  presentation all return success. Wine stderr contains a Metal cache message.

These are runtime/API capability observations. The probe does **not** measure
physical display brightness, validate an HDR image, test interop between D3D9
and D3D11, recover scene-linear game lighting, establish depth availability in
X3, or establish that a game frame can be re-presented by the DXGI path. Reported
display luminance is metadata, not a measurement. Physical HDR and game
integration remain separate verification tasks.

## Proxy smoke result

The baseline backend and app-local production proxy both completed with zero
failures. This included two sequential devices, pixel shader creation with
bytecode preservation and COM ownership, resource/device/API identity checks,
state blocks, drawing, presentation and reset. The proxy test used
`X3M_CAPTURE_START=1 X3M_CAPTURE_FRAMES=1` and rendered twice before reset so
that frame 1 was captured. The snapshot contains the expected pixel shader hash
`c294f234bcdafae7`, shader constants and draw state. The tested proxy was statically
linked with no companion MinGW runtime DLL. The reports are stored in
`verification/results/d3d9-smoke-*.txt` and `d3d9-smoke-proxy-capture.log`.

This smoke test does not validate every D3D9 interface, concurrent rendering,
X3's rendering behavior, image equivalence or GPU performance.
