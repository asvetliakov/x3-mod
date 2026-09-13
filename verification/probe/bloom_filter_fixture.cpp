// Standalone original-shader numerical fixture. No game/proxy integration.
// run_bloom_filter.py supplies expanded source and an explicitly serialized
// little-endian FP16 case bundle. All D3D operations use documented interfaces.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/temporal/bloom.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
static_assert(sizeof(unsigned) == 4 && sizeof(float) == 4 && sizeof(unsigned short) == 2,
              "case bundle requires Windows x86 scalar widths");
template<class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};
void check(const char* label, HRESULT hr) {
    if (FAILED(hr)) {
        std::printf("API_FAIL name=%s hr=%08lx\n", label, static_cast<unsigned long>(hr));
        throw std::runtime_error(label);
    }
}
void module_path(const char* name, HMODULE module) {
    char path[32768]{};
    DWORD size = GetModuleFileNameA(module, path, sizeof(path));
    if (!size || size >= sizeof(path)) throw std::runtime_error("GetModuleFileName");
    std::printf("MODULE name=%s path=%s\n", name, path);
}
struct Module {
    HMODULE h;
    Module(const char* name, const char* path): h(LoadLibraryA(path)) {
        if (!h) throw std::runtime_error("LoadLibrary");
        module_path(name, h);
    }
    ~Module() { FreeLibrary(h); }
};
template<class T> T symbol(HMODULE module, const char* name) {
    FARPROC address = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    if (!result) throw std::runtime_error(name);
    return result;
}
std::string read_text(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("shader open");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write_bytes(const std::string& path, const void* data, size_t bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!file.write(static_cast<const char*>(data), bytes)) throw std::runtime_error("output write");
}
template<class T> T read_value(std::ifstream& file) {
    T value{};
    if (!file.read(reinterpret_cast<char*>(&value), sizeof(value))) throw std::runtime_error("case truncated");
    return value;
}
struct Case {
    x3::temporal::BloomSize size;
    x3::temporal::BloomParams params;
    x3::temporal::AgxDecode decode;
    float exposure, clamp;
    std::vector<unsigned short> pixels;
};
std::vector<Case> read_cases(const char* path) {
    std::ifstream file(path, std::ios::binary);
    char magic[8]{};
    if (!file.read(magic, 8) || std::memcmp(magic, "X3BLM001", 8)) throw std::runtime_error("case magic");
    const unsigned count = read_value<unsigned>(file);
    if (!count || count > 128) throw std::runtime_error("case count");
    std::vector<Case> cases;
    for (unsigned i = 0; i < count; ++i) {
        Case c{};
        c.size.width = read_value<unsigned>(file); c.size.height = read_value<unsigned>(file);
        c.params.levels = read_value<unsigned>(file);
        unsigned mode = read_value<unsigned>(file);
        if (mode > 2 || !x3::temporal::valid_bloom_size(c.size)
            || static_cast<unsigned long long>(c.size.width) * c.size.height > 65536)
            throw std::runtime_error("case dimensions/mode");
        c.decode = mode == 0 ? x3::temporal::AgxDecode::gamma22
                 : mode == 1 ? x3::temporal::AgxDecode::srgb : x3::temporal::AgxDecode::none;
        c.params.strength = read_value<float>(file); c.params.threshold = read_value<float>(file);
        c.params.knee = read_value<float>(file); c.params.scatter = read_value<float>(file);
        c.exposure = read_value<float>(file); c.clamp = read_value<float>(file);
        x3::temporal::BloomConstants constants;
        if (!x3::temporal::prepare_bloom(constants, c.size, c.size, c.params, c.exposure, c.clamp, c.decode))
            throw std::runtime_error("case constants");
        c.pixels.resize(c.size.width * c.size.height * 4);
        if (!file.read(reinterpret_cast<char*>(c.pixels.data()), c.pixels.size() * sizeof(unsigned short)))
            throw std::runtime_error("case pixels");
        cases.push_back(std::move(c));
    }
    if (file.peek() != std::char_traits<char>::eof()) throw std::runtime_error("case trailing bytes");
    return cases;
}
using Compiler = decltype(&D3DXCompileShader);
void compile(Compiler compiler, const char* path, const char* profile,
             const std::string& retained, ID3DXBuffer** result) {
    std::string text = read_text(path);
    Com<ID3DXBuffer> errors;
    HRESULT hr = compiler(text.data(), static_cast<UINT>(text.size()), nullptr, nullptr,
                         "main", profile, D3DXSHADER_OPTIMIZATION_LEVEL3, result, &errors.p, nullptr);
    if (errors.p) std::printf("COMPILER message=%s\n", static_cast<char*>(errors->GetBufferPointer()));
    check("D3DXCompileShader", hr);
    write_bytes(retained, (*result)->GetBufferPointer(), (*result)->GetBufferSize());
}
struct Texture {
    Com<IDirect3DTexture9> texture;
    Com<IDirect3DSurface9> surface;
    x3::temporal::BloomSize size;
    Texture(IDirect3DDevice9* device, x3::temporal::BloomSize dimensions, bool target,
            const std::vector<unsigned short>* pixels = nullptr): size(dimensions) {
        check("CreateTexture", device->CreateTexture(size.width, size.height, 1,
              target ? D3DUSAGE_RENDERTARGET : 0, D3DFMT_A16B16G16R16F,
              target ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, &texture.p, nullptr));
        check("GetSurfaceLevel", texture->GetSurfaceLevel(0, &surface.p));
        if (!target) {
            D3DLOCKED_RECT lock{};
            check("Lock input", texture->LockRect(0, &lock, nullptr, 0));
            for (unsigned y = 0; y < size.height; ++y) {
                auto* row = static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch;
                if (pixels) std::memcpy(row, pixels->data() + y * size.width * 4, size.width * 8);
                else std::memset(row, 0, size.width * 8);
            }
            check("Unlock input", texture->UnlockRect(0));
        }
    }
};
struct Programs {
    Com<IDirect3DVertexShader9> vertex;
    Com<IDirect3DVertexDeclaration9> declaration;
    Com<IDirect3DPixelShader9> extract[6], down, up;
    Programs(IDirect3DDevice9* device, Compiler compiler, char** paths, const std::string& output) {
        Com<ID3DXBuffer> code;
        compile(compiler, paths[0], "vs_3_0", output + "/quad.cso", &code.p);
        check("CreateVertexShader", device->CreateVertexShader(static_cast<DWORD*>(code->GetBufferPointer()), &vertex.p));
        IDirect3DPixelShader9** shaders[] = {&extract[0].p, &extract[1].p, &extract[2].p,
            &extract[3].p, &extract[4].p, &extract[5].p, &down.p, &up.p};
        const char* names[] = {"extract_gamma", "extract_srgb", "extract_none",
            "extract_even_gamma", "extract_even_srgb", "extract_even_none", "down", "up"};
        for (unsigned i = 0; i < 8; ++i) {
            Com<ID3DXBuffer> pixel;
            compile(compiler, paths[i + 1], "ps_3_0", output + "/" + names[i] + ".cso", &pixel.p);
            check("CreatePixelShader", device->CreatePixelShader(static_cast<DWORD*>(pixel->GetBufferPointer()), shaders[i]));
        }
        const D3DVERTEXELEMENT9 elements[] = {
            {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0}, D3DDECL_END()};
        check("CreateVertexDeclaration", device->CreateVertexDeclaration(elements, &declaration.p));
    }
};
void draw(IDirect3DDevice9* device, const Programs& programs, IDirect3DPixelShader9* shader,
          const Texture& input, const Texture* coarse, const Texture& output,
          const x3::temporal::BloomConstants& constants) {
    check("Unbind s0", device->SetTexture(0, nullptr));
    check("Unbind s1", device->SetTexture(1, nullptr));
    check("SetRenderTarget", device->SetRenderTarget(0, output.surface.p));
    check("SetDepthStencilSurface", device->SetDepthStencilSurface(nullptr));
    D3DVIEWPORT9 viewport{0, 0, output.size.width, output.size.height, 0, 1};
    check("SetViewport", device->SetViewport(&viewport));
    check("SetVertexShader", device->SetVertexShader(programs.vertex.p));
    check("SetVertexDeclaration", device->SetVertexDeclaration(programs.declaration.p));
    check("SetPixelShader", device->SetPixelShader(shader));
    check("SetPixelShaderConstantF", device->SetPixelShaderConstantF(x3::temporal::kBloomFirstRegister,
           constants.source, x3::temporal::kBloomRegisterCount));
    check("SetTexture s0", device->SetTexture(0, input.texture.p));
    check("SetTexture s1", device->SetTexture(1, coarse ? coarse->texture.p : nullptr));
    for (unsigned stage = 0; stage < 2; ++stage) {
        const DWORD filter = stage ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        check("MINFILTER", device->SetSamplerState(stage, D3DSAMP_MINFILTER, filter));
        check("MAGFILTER", device->SetSamplerState(stage, D3DSAMP_MAGFILTER, filter));
        check("MIPFILTER", device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
        check("ADDRESSU", device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
        check("ADDRESSV", device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
        check("SRGBTEXTURE", device->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, FALSE));
    }
    const D3DRENDERSTATETYPE disabled[] = {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE,
        D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_SRGBWRITEENABLE,
        D3DRS_FOGENABLE, D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE};
    for (auto state : disabled) check("Disable render state", device->SetRenderState(state, FALSE));
    check("COLORWRITE", device->SetRenderState(D3DRS_COLORWRITEENABLE, 15));
    check("CULLMODE", device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    struct Vertex { float x, y, z, w, u, v; };
    const float dx = 1.f / output.size.width, dy = 1.f / output.size.height;
    const Vertex vertices[] = {{-1-dx, 1+dy,0,1,0,0}, {1-dx,1+dy,0,1,1,0},
                               {-1-dx,-1+dy,0,1,0,1}, {1-dx,-1+dy,0,1,1,1}};
    check("BeginScene", device->BeginScene());
    check("DrawPrimitiveUP", device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex)));
    check("EndScene", device->EndScene());
}
void readback(IDirect3DDevice9* device, const Texture& texture, const std::string& path) {
    Com<IDirect3DSurface9> staging;
    check("Create readback", device->CreateOffscreenPlainSurface(texture.size.width, texture.size.height,
          D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &staging.p, nullptr));
    check("GetRenderTargetData", device->GetRenderTargetData(texture.surface.p, staging.p));
    D3DLOCKED_RECT lock{};
    check("Lock readback", staging->LockRect(&lock, nullptr, D3DLOCK_READONLY));
    std::vector<unsigned char> bytes(texture.size.width * texture.size.height * 8);
    for (unsigned y = 0; y < texture.size.height; ++y)
        std::memcpy(bytes.data() + y * texture.size.width * 8,
                    static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch, texture.size.width * 8);
    check("Unlock readback", staging->UnlockRect());
    write_bytes(path, bytes.data(), bytes.size());
}
unsigned run_case(IDirect3DDevice9* device, const Programs& programs, const Case& c,
                  unsigned generation, unsigned index, const std::string& directory) {
    x3::temporal::BloomLayout layout;
    if (!x3::temporal::prepare_bloom_layout(layout, c.size, c.params.levels)) throw std::runtime_error("layout");
    x3::temporal::BloomExtractShader extraction;
    if (!x3::temporal::select_bloom_extract(extraction, c.size, c.decode)) throw std::runtime_error("extract selection");
    const unsigned extraction_index = static_cast<unsigned>(extraction);
    const char* extraction_names[] = {"extract_gamma", "extract_srgb", "extract_none",
        "extract_even_gamma", "extract_even_srgb", "extract_even_none"};
    std::printf("CASE generation=%u case=%u extraction=%s levels=%u\n", generation, index,
                extraction_names[extraction_index], layout.count);
    Texture scene(device, c.size, false, &c.pixels);
    std::vector<std::unique_ptr<Texture>> down, up;
    const Texture* input = &scene;
    unsigned passes = 0;
    auto store = [&](const Texture& t, const std::string& stage) {
        const std::string filename = "g" + std::to_string(generation) + "_c" + std::to_string(index) + "_" + stage + ".rgba16f";
        readback(device, t, directory + "/" + filename);
        std::printf("IMAGE generation=%u case=%u stage=%s width=%u height=%u file=%s\n",
                    generation, index, stage.c_str(), t.size.width, t.size.height, filename.c_str());
    };
    for (unsigned i = 0; i < layout.count; ++i) {
        down.emplace_back(new Texture(device, layout.level[i], true));
        x3::temporal::BloomConstants constants;
        if (!x3::temporal::prepare_bloom(constants, input->size, down.back()->size, c.params, c.exposure, c.clamp, c.decode))
            throw std::runtime_error("down constants");
        draw(device, programs, i ? programs.down.p : programs.extract[extraction_index].p,
             *input, nullptr, *down.back(), constants);
        ++passes; store(*down.back(), "d" + std::to_string(i)); input = down.back().get();
    }
    for (unsigned i = layout.count - 1; i > 0; --i) {
        const Texture& fine = *down[i - 1];
        up.emplace_back(new Texture(device, fine.size, true));
        x3::temporal::BloomConstants constants;
        if (!x3::temporal::prepare_bloom(constants, input->size, fine.size, c.params, c.exposure, c.clamp, c.decode))
            throw std::runtime_error("up constants");
        draw(device, programs, programs.up.p, fine, input, *up.back(), constants);
        ++passes; store(*up.back(), "u" + std::to_string(i - 1)); input = up.back().get();
    }
    Texture zero(device, c.size, false), final(device, c.size, true);
    auto params = c.params; params.scatter = 1;
    x3::temporal::BloomConstants constants;
    if (!x3::temporal::prepare_bloom(constants, input->size, c.size, params, c.exposure, c.clamp, c.decode))
        throw std::runtime_error("final constants");
    draw(device, programs, programs.up.p, zero, input, final, constants);
    ++passes; store(final, "final");
    // Release every default-pool fixture resource before Reset, including
    // device-held texture/RT references. This is not a renderer state test.
    Com<IDirect3DSurface9> backbuffer;
    check("GetBackBuffer", device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer.p));
    check("Unbind s0", device->SetTexture(0, nullptr)); check("Unbind s1", device->SetTexture(1, nullptr));
    check("Restore backbuffer", device->SetRenderTarget(0, backbuffer.p));
    return passes;
}
} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSA wc{}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "X3BloomFilterFixture";
    RegisterClassA(&wc);
    HWND window = CreateWindowA(wc.lpszClassName, "Original bloom filter fixture", WS_OVERLAPPEDWINDOW,
        80, 80, 128, 128, nullptr, nullptr, wc.hInstance, nullptr);
    int result = 1;
    try {
        if (argc != 13 || !window) throw std::runtime_error("expected d3dx quad six-extraction-shaders down up cases output-directory");
        const auto cases = read_cases(argv[11]);
        Module d3dx("d3dx", argv[1]), runtime("d3d9", "d3d9.dll");
        auto compiler = symbol<Compiler>(d3dx.h, "D3DXCompileShader");
        auto create = symbol<IDirect3D9* (WINAPI*)(UINT)>(runtime.h, "Direct3DCreate9");
        Com<IDirect3D9> api; api.p = create(D3D_SDK_VERSION);
        if (!api.p) throw std::runtime_error("Direct3DCreate9");
        D3DADAPTER_IDENTIFIER9 adapter{};
        check("GetAdapterIdentifier", api->GetAdapterIdentifier(0, 0, &adapter));
        std::printf("ADAPTER description=%s driver=%s\n", adapter.Description, adapter.Driver);
        D3DCAPS9 caps{}; check("GetDeviceCaps", api->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps));
        std::printf("CAPS ps=%08lx vs=%08lx max_width=%lu max_height=%lu\n",
           static_cast<unsigned long>(caps.PixelShaderVersion), static_cast<unsigned long>(caps.VertexShaderVersion),
           static_cast<unsigned long>(caps.MaxTextureWidth), static_cast<unsigned long>(caps.MaxTextureHeight));
        if (caps.PixelShaderVersion < D3DPS_VERSION(3,0) || caps.VertexShaderVersion < D3DVS_VERSION(3,0))
            throw std::runtime_error("SM3 unsupported");
        unsigned admitted_cases = 0;
        for (unsigned i = 0; i < cases.size(); ++i) {
            const auto& c = cases[i];
            if (c.size.width > caps.MaxTextureWidth || c.size.height > caps.MaxTextureHeight)
                std::printf("SKIP case=%u reason=dimension_caps width=%u height=%u\n", i, c.size.width, c.size.height);
            else ++admitted_cases;
        }
        for (DWORD usage : {DWORD(0), DWORD(D3DUSAGE_RENDERTARGET), DWORD(D3DUSAGE_QUERY_FILTER)}) {
            HRESULT hr = api->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, usage,
                                               D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F);
            std::printf("FORMAT usage=%lu hr=%08lx\n", static_cast<unsigned long>(usage), static_cast<unsigned long>(hr));
            check("FP16 format/filter support", hr);
        }
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window; pp.BackBufferWidth = pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;
        check("CreateDevice", api->CreateDevice(0, D3DDEVTYPE_HAL, window,
             D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p));
        Programs programs(device.p, compiler, argv + 2, argv[12]);
        unsigned passes = 0;
        for (unsigned generation = 0; generation < 2; ++generation) {
            for (unsigned i = 0; i < cases.size(); ++i)
                if (cases[i].size.width <= caps.MaxTextureWidth && cases[i].size.height <= caps.MaxTextureHeight)
                    passes += run_case(device.p, programs, cases[i], generation, i, argv[12]);
            if (!generation) { check("Reset", device->Reset(&pp)); std::puts("RESET PASS"); }
        }
        if (auto backend = GetModuleHandleA("wined3d.dll")) module_path("wined3d", backend);
        std::printf("RESULT PASS cases=%u generations=2 passes=%u\n", admitted_cases, passes);
        result = 0;
    } catch (const std::exception& error) { std::printf("RESULT FAIL reason=%s\n", error.what()); }
    if (window) DestroyWindow(window);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return result;
}
