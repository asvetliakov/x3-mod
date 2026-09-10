// Fault-injection replacement linked only into a disposable verification DLL.
// Forces the real loader's documented native fallback without an environment
// backdoor or a production allocator hook. The incoming owned ref is untouched.
#include "../../src/ownership/d3d9_ownership.h"
namespace x3m::ownership {
HRESULT wrap_factory(IDirect3D9*,IDirect3D9** out,const Options&) noexcept {
    if(out)*out=nullptr;
    return E_OUTOFMEMORY;
}
IDirect3DDevice9* borrowed_native_device(IDirect3DDevice9*) noexcept {return nullptr;}
IDirect3DVertexBuffer9* borrowed_native_buffer_for_lock_contract(IDirect3DVertexBuffer9*) noexcept {return nullptr;}
IDirect3DIndexBuffer9* borrowed_native_buffer_for_lock_contract(IDirect3DIndexBuffer9*) noexcept {return nullptr;}
HRESULT get_copy_depth_view(IDirect3DDevice9*,CopyDepthView*) noexcept {return E_INVALIDARG;}
HRESULT copy_auto_depth(IDirect3DDevice9*) noexcept {return E_INVALIDARG;}
HRESULT get_buffer_content_view(IDirect3DResource9*,BufferContentView*) noexcept {return E_INVALIDARG;}
HRESULT get_finite_position_view(IDirect3DVertexBuffer9*,const FinitePositionRequest&,FinitePositionView*) noexcept {return E_INVALIDARG;}
HRESULT get_index_range_view(IDirect3DIndexBuffer9*,const IndexRangeRequest&,IndexRangeView*) noexcept {return E_INVALIDARG;}
HRESULT get_finite_upload_statistics(IDirect3DDevice9*,FiniteUploadStatistics*) noexcept {return E_INVALIDARG;}
const char* finite_evidence_reason_name(FiniteEvidenceReason) noexcept {return "unavailable";}
}
