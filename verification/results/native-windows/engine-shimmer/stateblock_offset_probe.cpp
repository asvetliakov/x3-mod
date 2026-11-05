// Native D3D9 state-block stream capture probe (2026-11-05; docs/verification/engine-effects.md, "Native stream offset").
// Which stream-0 fields IDirect3DStateBlock9::Capture refreshes for a recorded block and a D3DSBT_ALL block, across a
// plain overwrite and DrawPrimitiveUP; frequency and stream 1 too; the explicit Get/Set tuple as the control.
// Prints ADAPTER / ADAPTER_DESC / ADAPTER_DRIVER / D3D9_MODULE rows for verification/probe/native_host.py identity().
// Built and run on the native Windows host by stateblock_offset_probe.py beside it (see there).
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
static IDirect3DDevice9* d;
static IDirect3DVertexBuffer9 *vb, *vb2;
static void show(const char* tag) {
    IDirect3DVertexBuffer9* b = nullptr; UINT o = 99, s = 99;
    HRESULT hr = d->GetStreamSource(0, &b, &o, &s);
    std::printf("%-44s hr=%08lx vb=%s offset=%u stride=%u\n", tag, hr, b == vb ? "vb" : b == vb2 ? "vb2" : b ? "other" : "null", o, s);
    if (b) b->Release();
}
static void caller() { d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE); d->SetStreamSource(0, vb, 16, 24); }
static IDirect3DStateBlock9* recorded(IDirect3DVertexBuffer9* pb, UINT po, UINT ps) {
    IDirect3DStateBlock9* b = nullptr;
    d->BeginStateBlock(); d->SetStreamSource(0, pb, po, ps); d->EndStateBlock(&b); return b;
}
struct V { float x, y, z, w; DWORD c; float pad; };
static void drawup() {
    V v[3] = {{0,0,0,1,0,0},{10,0,0,1,0,0},{0,10,0,1,0,0}};
    d->BeginScene(); HRESULT hr = d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, 24); d->EndScene();
    std::printf("  drawup hr=%08lx\n", hr);
}
int main() {
    WNDCLASSA wc{}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "sbp";
    RegisterClassA(&wc);
    HWND w = CreateWindowA("sbp", "sbp", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    IDirect3D9* api = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.hDeviceWindow = w;
    HRESULT hr = api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &d);
    std::printf("device hr=%08lx\n", hr); if (FAILED(hr)) return 1;
    {
        D3DADAPTER_IDENTIFIER9 id{};
        if (SUCCEEDED(api->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id))) {
            std::printf("ADAPTER vendor=0x%04lx device=0x%04lx subsys=0x%08lx revision=%lu whql=%lu driver_version=%u.%u.%u.%u\n",
                        id.VendorId, id.DeviceId, id.SubSysId, id.Revision, id.WHQLLevel,
                        unsigned(HIWORD(id.DriverVersion.HighPart)), unsigned(LOWORD(id.DriverVersion.HighPart)),
                        unsigned(HIWORD(id.DriverVersion.LowPart)), unsigned(LOWORD(id.DriverVersion.LowPart)));
            id.Description[sizeof id.Description - 1] = 0;
            id.Driver[sizeof id.Driver - 1] = 0;
            std::printf("ADAPTER_DESC %s\nADAPTER_DRIVER %s\n", id.Description, id.Driver);
        }
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(GetModuleHandleA("d3d9.dll"), path, MAX_PATH)) std::printf("D3D9_MODULE %s\n", path);
    }
    d->CreateVertexBuffer(96, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb, nullptr);
    d->CreateVertexBuffer(96, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb2, nullptr);
    caller(); show("A0 caller set");
    { IDirect3DStateBlock9* b = recorded(nullptr, 0, 0); show("A1 after recording (null,0,0)");
      b->Capture(); d->SetStreamSource(0, vb2, 32, 12); show("A2 overwritten");
      b->Apply(); show("A3 recorded(null) Capture/Apply"); b->Release(); }
    caller();
    { IDirect3DStateBlock9* b = recorded(vb2, 8, 12);
      b->Capture(); d->SetStreamSource(0, vb2, 32, 12); b->Apply(); show("B recorded(vb2,8,12) Capture/Apply"); b->Release(); }
    caller();
    { IDirect3DStateBlock9* b = recorded(nullptr, 0, 0); b->Capture(); drawup(); show("C1 after DrawPrimitiveUP");
      b->Apply(); show("C2 recorded Capture/DrawUP/Apply"); b->Release(); }
    caller();
    { IDirect3DStateBlock9* b = nullptr; d->CreateStateBlock(D3DSBT_ALL, &b); show("D0 after CreateStateBlock(ALL)");
      d->SetStreamSource(0, vb2, 32, 12); b->Apply(); show("D1 ALL created/Apply");
      d->SetStreamSource(0, vb, 40, 24); b->Capture(); d->SetStreamSource(0, vb2, 32, 12); b->Apply(); show("D2 ALL Capture(vb,40)/Apply");
      caller(); b->Capture(); drawup(); b->Apply(); show("D3 ALL Capture/DrawUP/Apply"); b->Release(); }
    caller();
    { IDirect3DStateBlock9* b = nullptr; d->CreateStateBlock(D3DSBT_VERTEXSTATE, &b);
      d->SetStreamSource(0, vb2, 32, 12); b->Apply(); show("E VERTEXSTATE created/Apply"); b->Release(); }
    caller();
    { IDirect3DStateBlock9* b = recorded(nullptr, 0, 0); b->Capture(); d->SetStreamSource(0, nullptr, 0, 0);
      b->Apply(); show("F recorded Capture/(null)/Apply");
      IDirect3DVertexDeclaration9* decl = nullptr; d->GetVertexDeclaration(&decl);
      std::printf("  decl=%p\n", (void*)decl);
      if (decl) { d->SetVertexDeclaration(decl); decl->Release(); show("F2 after SetVertexDeclaration(saved)"); }
      d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE); show("F3 after SetFVF"); b->Release(); }
    caller();
    { IDirect3DVertexBuffer9* sb = nullptr; UINT so = 0, ss = 0; d->GetStreamSource(0, &sb, &so, &ss);
      drawup(); d->SetStreamSource(0, sb, so, ss); if (sb) sb->Release(); show("G explicit Get/DrawUP/Set"); }
    { UINT f0=0; IDirect3DStateBlock9* b = nullptr; d->SetStreamSourceFreq(0, 1);
      d->BeginStateBlock(); d->SetStreamSourceFreq(0, 1); d->EndStateBlock(&b);
      d->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 3); b->Capture(); d->SetStreamSourceFreq(0, 1); b->Apply();
      d->GetStreamSourceFreq(0, &f0); std::printf("H recorded freq Capture(idx|3)/Apply          freq=%08x\n", f0); b->Release();
      d->SetStreamSourceFreq(0, 1); d->CreateStateBlock(D3DSBT_ALL, &b);
      d->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 3); b->Capture(); d->SetStreamSourceFreq(0, 1); b->Apply();
      d->GetStreamSourceFreq(0, &f0); std::printf("I ALL freq Capture(idx|3)/Apply               freq=%08x\n", f0);
      d->SetStreamSourceFreq(0, 1);
      d->SetStreamSource(0, vb2, 0, 12); b->Capture(); d->SetStreamSource(0, vb, 16, 24); b->Apply(); show("J ALL(created vb,16,24) Capture(vb2,0,12)/Apply");
      d->SetStreamSource(1, vb2, 20, 12); b->Capture(); d->SetStreamSource(1, nullptr, 0, 0); b->Apply();
      { IDirect3DVertexBuffer9* x=nullptr; UINT o=0,st=0; d->GetStreamSource(1,&x,&o,&st); std::printf("K ALL stream1 Capture(vb2,20,12)/Apply vb=%s offset=%u stride=%u\n", x==vb2?"vb2":x?"other":"null",o,st); if(x)x->Release(); }
      b->Release(); }
    d->Release(); api->Release(); return 0;
}
