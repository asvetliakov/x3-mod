// Exact native SYSTEMMEM mesh methods; original geometry, hidden standalone device.
#define main previous_mesh_fixture_main
#include "mesh_preparation.cpp"
#undef main
int main() {
    try {
        auto dx = LoadLibraryA("C:\\X3\\d3dx9_37.dll"), d3d = LoadLibraryA("d3d9.dll");
        require(dx && d3d, "modules");
        auto create = symbol<Create>(dx, "D3DXCreateMesh");
        auto factory = symbol<decltype(&Direct3DCreate9)>(d3d, "Direct3DCreate9");
        WNDCLASSA c{};
        c.lpfnWndProc = DefWindowProcA;
        c.hInstance = GetModuleHandleA(nullptr);
        c.lpszClassName = "MeshUnlockProbe";
        RegisterClassA(&c);
        auto w = CreateWindowA(c.lpszClassName, "", 0, 0, 0, 64, 64, nullptr, nullptr, c.hInstance, nullptr);
        Com<IDirect3D9> a;
        a.p = factory(D3D_SDK_VERSION);
        Com<IDirect3DDevice9> d;
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = w;
        ok(a->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &d.p), "device");
        for (DWORD options : {DWORD(D3DXMESH_SYSTEMMEM), DWORD(D3DXMESH_SYSTEMMEM | D3DXMESH_32BIT)}) {
            D3DVERTEXELEMENT9 decl[] = {{0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
            Com<ID3DXMesh> m;
            ok(create(1, 3, options, decl, d.p, &m.p), "mesh");
            auto table = *reinterpret_cast<void***>(m.p);
            for (unsigned i : {9u, 13u, 14u, 15u, 16u, 17u, 18u})
                printf("METHOD options=%08lx slot=%u rva=%08lx\n", options, i,
                       DWORD(reinterpret_cast<uintptr_t>(table[i]) - reinterpret_cast<uintptr_t>(dx)));
            Com<IDirect3DVertexBuffer9> vb;
            Com<IDirect3DIndexBuffer9> ib;
            ok(m->GetVertexBuffer(&vb.p), "vertex buffer");
            ok(m->GetIndexBuffer(&ib.p), "index buffer");
            for (auto buffer : {static_cast<IUnknown*>(vb.p), static_cast<IUnknown*>(ib.p)}) {
                auto t = *reinterpret_cast<void***>(buffer);
                HMODULE owner = nullptr;
                char path[2048]{};
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCSTR>(t[12]), &owner);
                GetModuleFileNameA(owner, path, sizeof path);
                printf("BACKEND path=%s module=%p lock_rva=%08lx unlock_rva=%08lx\n", path, owner,
                       DWORD(reinterpret_cast<uintptr_t>(t[11]) - reinterpret_cast<uintptr_t>(owner)),
                       DWORD(reinterpret_cast<uintptr_t>(t[12]) - reinterpret_cast<uintptr_t>(owner)));
                if (owner) FreeLibrary(owner);
            }
            void* data = nullptr;
            auto lv = m->LockVertexBuffer(D3DLOCK_READONLY, &data);
            auto uv = m->UnlockVertexBuffer();
            auto uv2 = m->UnlockVertexBuffer();
            auto li = m->LockIndexBuffer(D3DLOCK_READONLY, &data);
            auto ui = m->UnlockIndexBuffer();
            auto ui2 = m->UnlockIndexBuffer();
            printf(
                "UNLOCK options=%08lx lock_v=%08lx unlock_v=%08lx extra_unlock_v=%08lx lock_i=%08lx unlock_i=%08lx extra_unlock_i=%08lx\n",
                options, lv, uv, uv2, li, ui, ui2);
        }
        printf("RESULT PASS checks=%u\n", checks);
        DestroyWindow(w);
        return 0;
    } catch (const std::exception& e) {
        printf("RESULT FAIL %s\n", e.what());
        return 1;
    }
}
