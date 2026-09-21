// These standalone fixtures deliberately arm Capture with upload_requested=false.
// Link only in their state-only executables, never an ownership/geometry fixture.
// If a future edit reaches any upload operation, fail the process immediately;
// returning a fake refusal could conceal unintended geometry-path execution.
#ifndef X3M_LATTICE_STATE_ONLY_FIXTURE
#error State-only ownership stubs require the explicit standalone fixture gate.
#endif
#include "../../src/ownership/clone_upload_observer.h"
#include "../../src/ownership/d3d9_ownership.h"
#include <cstdio>

namespace {
[[noreturn]] void unexpected_upload(const char* operation) noexcept {
    std::fprintf(stderr,"FAIL state-only fixture reached upload operation: %s\n",operation);
    std::fflush(stderr);
    ExitProcess(97);
}
}
namespace x3m::ownership {
HRESULT query_clone_upload_pin(IDirect3DDevice9*,CloneUploadPinView*) noexcept {
    unexpected_upload("query_clone_upload_pin");
}
HRESULT get_buffer_lock_view(IDirect3DResource9*,BufferLockView*) noexcept {
    unexpected_upload("get_buffer_lock_view");
}
HRESULT copy_clone_upload_pair(IDirect3DDevice9*,const CloneUploadPairRequest&,
    void*,std::size_t,void*,std::size_t,CloneUploadPairResult*) noexcept {
    unexpected_upload("copy_clone_upload_pair");
}
}
