#pragma once
#include "clone_upload_core.h"
#include <d3d9.h>
struct ID3DXMesh;
namespace x3m::ownership {
// Manual diagnostic only. Caller owns Store until successful disarm. Arming
// retains the recognized device; no environment option or game seam uses it.
HRESULT arm_clone_upload(clone_upload::Store*, IDirect3DDevice9*, const D3DVERTEXELEMENT9*) noexcept;
HRESULT disarm_clone_upload(clone_upload::Store*) noexcept;
HRESULT clone_mesh_upload(ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**);
bool copy_clone_upload(unsigned, clone_upload::Buffer, IDirect3DVertexBuffer9*, IDirect3DIndexBuffer9*, void*,
                       std::size_t) noexcept;
// Flight coordinator keeps Store storage alive through a Deferred close. None
// of these operations adopts an unrecognized device or resurrects a weak key.
struct CloneUploadArmToken {
    std::uint64_t serial = 0, owner = 0;
};
struct CloneUploadPinView {
    CloneUploadArmToken arm;
    std::uint64_t generation = 0;
    unsigned references = 0; // this observer's wrapper pin only: 0 or 1
    bool closing = false, scope_active = false;
};
HRESULT query_clone_upload_pin(IDirect3DDevice9*, CloneUploadPinView*) noexcept;
enum class CloneUploadClose { None, Deferred, Released };
// S_OK + Deferred is NOT permission to free Store. May Release outside registry.
HRESULT close_clone_upload(IDirect3DDevice9*, CloneUploadArmToken, CloneUploadClose*) noexcept;
struct CloneUploadPairRequest {
    CloneUploadArmToken arm;
    unsigned slot = 0;
    std::uint64_t generation = 0;
    IDirect3DVertexBuffer9* vertex_key = nullptr;
    IDirect3DIndexBuffer9* index_key = nullptr;
    clone_upload::Identity vertex, index;
};
enum class CloneUploadPairStatus {
    None,
    Copied,
    Unarmed,
    Closing,
    ActiveScope,
    Selector,
    Missing,
    Duplicate,
    StaleArm,
    Device,
    Binding,
    Revision,
    OpenMapping,
    Dispatch,
    Capacity
};
struct CloneUploadPairResult {
    clone_upload::Record record{};
    CloneUploadPairStatus status = CloneUploadPairStatus::None;
    bool binding_revision_match_at_observation = false;
};
// Caller releases its getter refs first, retaining only the observed keys/IDs.
// All supplied spans must be writable, overflow-free and mutually disjoint,
// and separate from request and Store. Aliasing is E_INVALIDARG, no writes;
// ordinary refusal clears result and leaves both payload buffers untouched.
HRESULT copy_clone_upload_pair(IDirect3DDevice9*, const CloneUploadPairRequest&, void*, std::size_t, void*, std::size_t,
                               CloneUploadPairResult*) noexcept;
#ifdef X3M_LATTICE_UPLOAD_FIXTURE
enum class CloneUploadFixtureEvent {
    Created,
    BeforeStage,
    AfterStageQualifiers,
    BeforeFinal,
    AfterFinal,
    BeforeOriginal,
    StageRegistryAcquired,
    StageCopyCompleted,
    ResetEntry,
    PairRegistryAcquired,
    PairCopyCompleted
};
using CloneUploadFixtureHook = void (*)(CloneUploadFixtureEvent, IUnknown*);
void clone_upload_fixture_hook(CloneUploadFixtureHook) noexcept;
bool clone_upload_fixture_scope_active() noexcept;
// Inject the exhausted-owner sentinel only on an unarmed, otherwise unreferenced device.
bool clone_upload_fixture_owner_serial(IDirect3DDevice9*, std::uint64_t, std::uint64_t*) noexcept;
#endif
}
