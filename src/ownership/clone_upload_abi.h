#pragma once
#include <d3d9.h>
#include <cstddef>
struct ID3DXMesh;
namespace x3m::ownership::clone_upload_abi {
struct Arguments {
    ID3DXMesh* source;
    DWORD options;
    const D3DVERTEXELEMENT9* declaration;
    IDirect3DDevice9* device;
    ID3DXMesh** output;
};
struct alignas(16) Context { unsigned char bytes[512]; };
struct Observer {
    void (*prepare)(Context&, const Arguments&) noexcept;
    void (*before_original)(Context&) noexcept;
    void (*finish)(Context&, HRESULT, ID3DXMesh*) noexcept;
    void (*abort)(Context&) noexcept;
};
extern const Observer observer;
}
namespace x3m::ownership {
HRESULT clone_mesh_upload(ID3DXMesh*, DWORD, const D3DVERTEXELEMENT9*,
                          IDirect3DDevice9*, ID3DXMesh**);
}
