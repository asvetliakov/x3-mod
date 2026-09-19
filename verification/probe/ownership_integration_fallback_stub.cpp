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
HRESULT get_buffer_lock_view(IDirect3DResource9*,BufferLockView* out) noexcept {
    if(out){*out={};out->status=E_INVALIDARG;}
    return E_INVALIDARG;
}
HRESULT get_buffer_lock_view_light(IDirect3DResource9*,BufferLockView* out) noexcept {
    if(out){*out={};out->status=E_INVALIDARG;}
    return E_INVALIDARG;
}
HRESULT get_locked_prefix_view(IDirect3DResource9*,std::uint32_t,bool,LockedPrefixView* out) noexcept {
    if(out){*out={};out->status=E_INVALIDARG;}
    return E_INVALIDARG;
}
void get_locked_prefix_statistics(LockedPrefixStatistics* out) noexcept {if(out)*out={};}
void set_surface_lock_observer(SurfaceLockObserver) noexcept {}
HRESULT invalidate_native_buffer_evidence(IUnknown*) noexcept {return E_INVALIDARG;}
HRESULT get_finite_position_view(IDirect3DVertexBuffer9*,const FinitePositionRequest&,FinitePositionView*) noexcept {return E_INVALIDARG;}
HRESULT get_index_range_view(IDirect3DIndexBuffer9*,const IndexRangeRequest&,IndexRangeView*) noexcept {return E_INVALIDARG;}
HRESULT get_finite_upload_statistics(IDirect3DDevice9*,FiniteUploadStatistics*) noexcept {return E_INVALIDARG;}
const char* finite_evidence_reason_name(FiniteEvidenceReason) noexcept {return "unavailable";}
HRESULT get_execution_view(IDirect3DDevice9*,ExecutionView* out) noexcept {
    if(out)*out={}; // In particular, never claim known/query-idle on native fallback.
    return E_INVALIDARG;
}
HRESULT invalidate_execution_state(IDirect3DDevice9*) noexcept {return E_INVALIDARG;}
HRESULT observe_native_result(IDirect3DDevice9*,HRESULT hr) noexcept {return hr;}
HRESULT begin_geometry_frame(IDirect3DDevice9*,GeometryFrameHandle* out) noexcept {
    if(out)*out={};
    return E_INVALIDARG;
}
HRESULT acquire_geometry_lease(GeometryFrameHandle,IDirect3DVertexBuffer9*,
    IDirect3DIndexBuffer9*,const GeometryLeaseRequest&,GeometryLeaseHandle* out) noexcept {
    if(out)*out={};
    return E_INVALIDARG;
}
HRESULT inspect_geometry_lease(GeometryFrameHandle,GeometryLeaseHandle,GeometryLeaseView* out) noexcept {
    if(out){*out={};out->status=E_INVALIDARG;}
    return E_INVALIDARG;
}
HRESULT release_geometry_lease(GeometryFrameHandle,GeometryLeaseHandle) noexcept {return E_INVALIDARG;}
HRESULT end_geometry_frame(GeometryFrameHandle) noexcept {return E_INVALIDARG;}
}
