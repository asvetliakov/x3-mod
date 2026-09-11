# Portable managed-buffer upload evidence

The production finite-evidence path uses public Direct3D 9 and COM contracts.
It has no Wine object layout, heap pointer, map-count field, module address,
export RVA or DLL fingerprint. The former pinned Preview qualifier is retained
only as historical verification code; it is not linked into the proxy.
Native Windows execution remains a separate verification requirement; current
runtime results below are from CrossOver Preview.

## Readable native allocation

With finite capture enabled, an eligible new MANAGED VB/IB requested with
WRITEONLY is created with that bit removed. Length, FVF/format, pool and all
other usage flags are forwarded. DEFAULT, shared, zero/oversized and DYNAMIC
requests are not converted. Failed readable creation or failed metadata/atlas
admission releases the provisional allocation and retries the original request.
The chosen native call's HRESULT, output semantics, outgoing computational FP
state and LastError are preserved; a retry receives the original incoming state.

Microsoft specifies that WRITEONLY prohibits reads, and its performance effect
applies to DEFAULT buffers. MANAGED and DYNAMIC are incompatible. This conversion
therefore creates a legally readable backing for the existing managed game
buffers instead of reading WRITEONLY memory on an assumed Wine heap route.
See [D3DUSAGE](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dusage).
Invalid application attempts to read an originally WRITEONLY resource are outside
this adaptation's compatibility promise; removing the native restriction may
change the result of that invalid usage.

Immutable requested Usage is held in the allocation's private-IUnknown metadata.
Wrapped GetDesc first calls the actual native GetDesc and changes only Usage on
success. Other fields and failed-output behavior remain native-owned. This
metadata remains available when the atlas is retired or disabled, after Reset,
and after application wrapper destruction/recreation. It holds no native COM
reference and cannot create a buffer/device ownership cycle. A canonical
IUnknown numeric identity locates the sidecar; every descriptor check receives
the current wrapper-held or lease-held native interface. No persisted typed
interface pointer is dereferenced after its original wrapper dies.

## Existing application mapping

The observer reads only the exact pointer returned by an already successful
application Lock on the readable backing. It requires ordinary flags 0 or
NOSYSLOCK, exactly one observed pending map, the same thread, revision and device
generation, unchanged wrapper Lock/Unlock dispatch and a bounded requested
window. Only (0,0) means the whole allocation. Nonzero-offset/zero-size requests
are refused even if a runtime accepts them. No extra Lock/Unlock, buffer mirror,
GPU readback or private backend pointer is introduced.

The pre-Unlock observer issues MFENCE before integer classification, accounting
for same-thread non-temporal stores. It stages summaries, calls the original
Unlock and publishes only after successful matching closure and revalidation
of the observed wrapper transaction. Nested maps remain ambiguous; failed
Unlock, reset, loss, metadata tamper or inconsistent descriptors cannot publish.
Multiple locks may be valid D3D calls, so refusing evidence does not replace
those native calls or invent failure HRESULTs. See
[VB Lock](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dvertexbuffer9-lock)
and [IB Lock](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dindexbuffer9-lock).

Public D3D9 has no native buffer map-count query. Consequently closure is an
observed-wrapper contract, not a claim that arbitrary native bypasses can be
discovered. Every trusted internal native mutation outside the wrapper must call
`invalidate_native_buffer_evidence` first and serialize its whole interval
against evidence queries and replay. Notification advances the observed storage
revision and invalidates finite summaries. Native closure alone cannot restore
them; later observed writes may rebuild the covered cells. The borrowed native
getter permits trusted read-only inspection by contract, although its C++ pointer
is technically mutable. Unannounced native writes and arbitrary private-GUID
replacement are unsupported. Normal driver/runtime work implementing observed
D3D calls is not a bypass.

## COM lifetime and performance

Geometry leases retain their actual native VB/IB through standard AddRef/Release,
including legitimate forwarding interceptors. Numeric refcount return values are
not used as proof. The immutable draw request, canonical allocation identity,
private-IUnknown authentication, descriptor, revision, layout and finite/index
bounds are rechecked. Known wrapper forwarding loss still refuses evidence.
Metadata authentication uses the documented GetPrivateData AddRef callback;
foreign POD bytes are never treated as callable IUnknown pointers. See
[GetPrivateData](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-getprivatedata)
and [IUnknown lifetime](https://learn.microsoft.com/en-us/windows/win32/api/unknwn/nf-unknwn-iunknown-addref).

The compact atlas and bounded query cache are unchanged. Public descriptor and
identity calls replace the previous repeated module/export/VirtualQuery checks.
The [same-work geometry benchmark](geometry-performance.md) measures the new
query/lease path. A separate [36-case upload benchmark](managed-upload-performance.md)
compares native WRITEONLY/readable backing and wrapped off/finite creation,
mapping and classification. Previous pinned implementation numbers are historical.

## Verification status

The expanded finite-upload fixture passes **534 checks** on Preview, with
independent source and artifact review accepted. Creation and sidecar-attachment
failure controls verify exact retry and selected native FP/LastError; descriptor
controls cover failed outputs, recreation, Reset, permanent evidence disable
and repeated foreign-IUnknown tampering. Native DLL digests are recorded for
before/after provenance without an allowlist. This is not native-Windows runtime
validation or live-game enablement. The installed DLL remains unchanged.
See [the observer report](../../verification/results/finite-upload-summary.json).
