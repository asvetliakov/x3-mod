#pragma once
#include "clone_upload_core.h"
#include <d3d9.h>
struct ID3DXMesh;
namespace x3m::ownership {
// Manual diagnostic only. Caller owns Store until successful disarm. Arming
// retains the recognized device; no environment option or game seam uses it.
HRESULT arm_clone_upload(clone_upload::Store*,IDirect3DDevice9*,const D3DVERTEXELEMENT9*) noexcept;
HRESULT disarm_clone_upload(clone_upload::Store*) noexcept;
HRESULT clone_mesh_upload(ID3DXMesh*,DWORD,const D3DVERTEXELEMENT9*,IDirect3DDevice9*,ID3DXMesh**);
bool copy_clone_upload(unsigned,clone_upload::Buffer,IDirect3DVertexBuffer9*,IDirect3DIndexBuffer9*,void*,std::size_t) noexcept;
#ifdef X3M_LATTICE_UPLOAD_FIXTURE
enum class CloneUploadFixtureEvent { Created, BeforeStage, AfterStageQualifiers, BeforeFinal, AfterFinal, BeforeOriginal,
    StageRegistryAcquired, StageCopyCompleted, ResetEntry };
using CloneUploadFixtureHook=void(*)(CloneUploadFixtureEvent,IUnknown*);
void clone_upload_fixture_hook(CloneUploadFixtureHook) noexcept;
bool clone_upload_fixture_scope_active() noexcept;
#endif
}
