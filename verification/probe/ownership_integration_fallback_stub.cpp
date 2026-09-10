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
HRESULT get_depth_view(IDirect3DDevice9*,DepthView*) noexcept {return E_INVALIDARG;}
}
