// Detached distance measurement for the docking-port shader pair
// 4944d81dfe531b37 / 64bac8bb307eb896 (docs/reverse-engineering/
// station-material-distance.md). The original programs and the three original
// DDS images stay outside the repository: both directories are arguments and a
// missing file is a clear failure, never a silent substitution.
//
// The quad carries the port material's whole 1024^2 UV domain and is drawn at
// three camera distances whose on-screen widths are 170 / 85 / 42.5 px at
// 1280x768, i.e. isotropic levels 2.59 / 3.59 / 4.59. Nothing but the camera
// distance changes between the three draws of a configuration, so the ratio of
// the mean luminance is exactly the shader's response to minification.
//
// Two passes per case: `composite` uses the captured source-over state over a
// black target (COLORWRITEENABLE = 7, so the target alpha is not written) and
// `raw` disables blending with a full write mask, which exposes oC0.a. Over
// black the two are related by composite.rgb = raw.rgb * raw.a.
#define WIN32_LEAN_AND_MEAN
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d9.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {

void api(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::printf("API_FAIL %s %08lx\n", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(what);
    }
}
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p) p->Release();
    }
    T* operator->() const { return p; }
};

std::vector<unsigned char> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "missing file: " + path);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    require(!bytes.empty(), "empty file: " + path);
    return bytes;
}

float from_half(unsigned short h) {
    const unsigned sign = (h >> 15) & 1, exponent = (h >> 10) & 0x1f, mantissa = h & 0x3ff;
    float value;
    if (exponent == 0)
        value = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        value = mantissa ? std::nanf("") : HUGE_VALF;
    else
        value = std::ldexp(static_cast<float>(mantissa + 1024), int(exponent) - 25);
    return sign ? -value : value;
}

// ---------------------------------------------------------------- vector math
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 operator*(Vec3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalize(Vec3 a) {
    const float length = std::sqrt(dot(a, a));
    require(length > 1e-12f, "normalize of a degenerate vector");
    return a * (1.f / length);
}
// Minimal rotation carrying `from` onto `to` (Rodrigues); both must be unit.
Vec3 rotate_like(Vec3 from, Vec3 to, Vec3 v) {
    const float c = std::max(-1.f, std::min(1.f, dot(from, to)));
    Vec3 axis = cross(from, to);
    const float s = std::sqrt(dot(axis, axis));
    if (s < 1e-6f) return c > 0 ? v : v * -1.f; // parallel; the antiparallel case is unused
    axis = axis * (1.f / s);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.f - c));
}

struct Mat4 {
    float m[4][4] = {};
};
Mat4 multiply(const Mat4& a, const Mat4& b) { // row-vector convention: v*a*b
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double sum = 0;
            for (int k = 0; k < 4; ++k) sum += double(a.m[i][k]) * b.m[k][j];
            r.m[i][j] = float(sum);
        }
    return r;
}
// The VS reads every matrix as columns through dp4, so publish columns.
void column(const Mat4& m, int j, float out[4]) {
    for (int i = 0; i < 4; ++i) out[i] = m.m[i][j];
}

// ------------------------------------------------------------------ DDS input
struct Dds {
    unsigned width = 0, height = 0, levels = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    unsigned block_bytes = 0;
    std::vector<unsigned char> bytes;
};

Dds load_dds(const std::string& path) {
    Dds dds;
    dds.bytes = read_file(path);
    require(dds.bytes.size() > 128 && std::memcmp(dds.bytes.data(), "DDS ", 4) == 0, "not a DDS file: " + path);
    auto dword = [&](unsigned offset) {
        std::uint32_t value = 0;
        std::memcpy(&value, dds.bytes.data() + offset, 4);
        return value;
    };
    dds.height = dword(12);
    dds.width = dword(16);
    dds.levels = std::max(1u, dword(28));
    const std::uint32_t four_cc = dword(84);
    if (four_cc == 0x35545844u) { // 'DXT5'
        dds.format = D3DFMT_DXT5;
        dds.block_bytes = 16;
    } else if (four_cc == 0x31545844u) { // 'DXT1'
        dds.format = D3DFMT_DXT1;
        dds.block_bytes = 8;
    } else
        require(false, "unsupported DDS FourCC in " + path);
    return dds;
}

IDirect3DTexture9* create_from_dds(IDirect3DDevice9* device, const Dds& dds, const char* label) {
    IDirect3DTexture9* texture = nullptr;
    api(device->CreateTexture(dds.width, dds.height, dds.levels, 0, dds.format, D3DPOOL_MANAGED, &texture, nullptr),
        "CreateTexture");
    const unsigned char* source = dds.bytes.data() + 128;
    const unsigned char* end = dds.bytes.data() + dds.bytes.size();
    for (unsigned level = 0; level < dds.levels; ++level) {
        const unsigned w = std::max(1u, dds.width >> level), h = std::max(1u, dds.height >> level);
        const unsigned rows = (h + 3) / 4, row_bytes = ((w + 3) / 4) * dds.block_bytes;
        if (source + std::size_t(rows) * row_bytes > end) {
            texture->Release();
            require(false, std::string("DDS data truncated at level ") + std::to_string(level) + " of " + label);
        }
        D3DLOCKED_RECT lock{};
        api(texture->LockRect(level, &lock, nullptr, 0), "LockRect");
        for (unsigned row = 0; row < rows; ++row)
            std::memcpy(static_cast<unsigned char*>(lock.pBits) + std::size_t(row) * lock.Pitch,
                        source + std::size_t(row) * row_bytes, row_bytes);
        api(texture->UnlockRect(level), "UnlockRect");
        source += std::size_t(rows) * row_bytes;
    }
    std::printf("TEXTURE label=%s width=%u height=%u levels=%u format=%s "
                "bytes=%zu consumed=%zu\n",
                label, dds.width, dds.height, dds.levels, dds.format == D3DFMT_DXT5 ? "DXT5" : "DXT1", dds.bytes.size(),
                std::size_t(source - dds.bytes.data()));
    return texture;
}

// ----------------------------------------------------------- captured setup
// PS float constants of draw index 216, frame 2071 of run 39 (the registers the
// program declares; c12/c13 are def-ed inside the bytecode and shadow the host).
const float ps_grading[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}; // c0..c2, identity, zero column 4
const float ps_enable_glow[4] = {0, 0, 0, 0};                              // c3
const float light_dir0[4] = {-0.304886f, 0.455627f, -0.836319f, 0};        // c4
const float light_color0[4] = {0.664062f, 0.781250f, 0.585938f, 0};        // c5
const float light_dir1[4] = {0.945480f, 0.230774f, 0.229752f, 0};          // c6
const float light_color1[4] = {0.128906f, 0.257812f, 0.214844f, 0};        // c7
const float mat_specular_strength[4] = {3, 0, 0, 0};                       // c8
const float mat_specular_power[4] = {6, 0, 0, 0};                          // c9
const float mat_reflection_strength[4] = {1, 0, 0, 0};                     // c10
const float mat_diffuse_strength[4] = {0.5f, 0, 0, 0};                     // c11

// Projection of the captured draw (1280x768): m00 0.8, m11 4/3, near 6.
Mat4 captured_projection() {
    Mat4 p;
    auto bits = [](std::uint32_t v) {
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    };
    p.m[0][0] = bits(0x3f4cccccu);
    p.m[1][1] = bits(0x3faaaaaau);
    p.m[2][2] = bits(0x3f800019u);
    p.m[2][3] = 1.f;
    p.m[3][2] = bits(0xc0c00026u);
    return p;
}

struct Configuration {
    const char* name;
    float view_degrees;  // angle between the surface normal and the view ray
    float light_degrees; // signed offset of light 0 from the normal, same plane
};
// head_on/mirror/off_peak follow the design note's three geometries
// (docs/architecture/specular-antialiasing.md). The quad is tilted about the
// world X axis; the captured light pair is carried rigidly by the minimal
// rotation that puts light 0 at the requested offset, so the two captured
// colours and their 112 degree separation are preserved exactly.
const Configuration configurations[] = {{"head_on", 0.f, 30.f}, {"mirror", 45.f, 45.f}, {"off_peak", 60.f, -20.f}};

struct Vertex {
    float position[3], uv[2], normal[3], binormal[3], tangent[3];
};

struct Statistics {
    unsigned pixels = 0;
    double mean_luminance = 0, max_luminance = 0;
    double mean_alpha = 0, min_alpha = 0, max_alpha = 0;
};

double luminance(const float rgba[4]) {
    return 0.2126 * rgba[0] + 0.7152 * rgba[1] + 0.0722 * rgba[2];
}

struct Fixture {
    IDirect3DDevice9* device;
    unsigned width = 1280, height = 768;
    Com<IDirect3DSurface9> back, target, system;
    Com<IDirect3DVertexShader9> vertex_shader;
    Com<IDirect3DPixelShader9> pixel_shader;
    Com<IDirect3DVertexDeclaration9> declaration;
    Com<IDirect3DTexture9> diffuse, bump, specular, lightmap, flat_normal;
    Com<IDirect3DCubeTexture9> cube;
    std::vector<float> pixels; // width*height*4, one readback
    unsigned max_anisotropy = 16;

    explicit Fixture(IDirect3DDevice9* d)
        : device(d) {
        api(device->GetRenderTarget(0, &back.p), "GetRenderTarget");
        api(device->CreateRenderTarget(width, height, D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_NONE, 0, FALSE, &target.p,
                                       nullptr),
            "CreateRenderTarget");
        api(device->CreateOffscreenPlainSurface(width, height, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &system.p,
                                                nullptr),
            "CreateOffscreenPlainSurface");
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            {0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
            {0, 32, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL, 0},
            {0, 44, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0},
            D3DDECL_END()};
        api(device->CreateVertexDeclaration(elements, &declaration.p), "CreateVertexDeclaration");
        pixels.resize(std::size_t(width) * height * 4);
    }
    ~Fixture() {
        for (unsigned stage = 0; stage < 5; ++stage) device->SetTexture(stage, nullptr);
        device->SetRenderTarget(0, back.p);
        device->SetVertexDeclaration(nullptr);
        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);
    }

    void load_programs(const std::string& directory) {
        const auto vs = read_file(directory + "/vs_4944d81dfe531b37.bin");
        const auto ps = read_file(directory + "/ps_64bac8bb307eb896.bin");
        require(vs.size() % 4 == 0 && ps.size() % 4 == 0, "program size");
        api(device->CreateVertexShader(reinterpret_cast<const DWORD*>(vs.data()), &vertex_shader.p),
            "CreateVertexShader");
        api(device->CreatePixelShader(reinterpret_cast<const DWORD*>(ps.data()), &pixel_shader.p), "CreatePixelShader");
        std::printf("PROGRAMS vs_bytes=%zu ps_bytes=%zu\n", vs.size(), ps.size());
    }

    void load_textures(const std::string& directory) {
        const std::string base = directory + "/metal_argon_lattice_windowedgrid_";
        const Dds d = load_dds(base + "diff.dds"), b = load_dds(base + "bump.dds"), s = load_dds(base + "spec.dds");
        require(d.width == 1024 && d.levels == 11 && d.format == D3DFMT_DXT5, "diffuse shape");
        require(b.width == 1024 && b.levels == 11 && b.format == D3DFMT_DXT5, "bump shape");
        require(s.width == 1024 && s.levels == 11 && s.format == D3DFMT_DXT1, "specular shape");
        diffuse.p = create_from_dds(device, d, "diffuse");
        bump.p = create_from_dds(device, b, "bump");
        specular.p = create_from_dds(device, s, "specular");
        // Neutral stand-ins for the two stages the measurement does not vary: the
        // captured lightmap is the shared 32^2 dummy and stage 4 has one level and
        // MIPFILTER NONE, so neither minifies. Both are set to black, which removes
        // their additive and reflective contribution instead of inventing one.
        api(device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &lightmap.p, nullptr),
            "CreateTexture lightmap");
        fill_argb(lightmap.p, 0x00000000u);
        api(device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &flat_normal.p, nullptr),
            "CreateTexture flat normal");
        // AG decode: x = 2*A-1, y = 2*G-1. 128/255 leaves x = y = 0.00392.
        fill_argb(flat_normal.p, 0x80008000u);
        api(device->CreateCubeTexture(1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube.p, nullptr),
            "CreateCubeTexture");
        for (unsigned face = 0; face < 6; ++face) {
            D3DLOCKED_RECT lock{};
            api(cube->LockRect(D3DCUBEMAP_FACES(face), 0, &lock, nullptr, 0), "cube LockRect");
            std::uint32_t black = 0;
            std::memcpy(lock.pBits, &black, 4);
            api(cube->UnlockRect(D3DCUBEMAP_FACES(face), 0), "cube UnlockRect");
        }
    }
    void fill_argb(IDirect3DTexture9* texture, std::uint32_t value) {
        D3DLOCKED_RECT lock{};
        api(texture->LockRect(0, &lock, nullptr, 0), "LockRect 1x1");
        std::memcpy(lock.pBits, &value, 4);
        api(texture->UnlockRect(0), "UnlockRect 1x1");
    }

    void bind_samplers(bool flat) {
        IDirect3DBaseTexture9* stages[5] = {diffuse.p,
                                            flat ? static_cast<IDirect3DBaseTexture9*>(flat_normal.p)
                                                 : static_cast<IDirect3DBaseTexture9*>(bump.p),
                                            specular.p, lightmap.p, cube.p};
        for (unsigned stage = 0; stage < 5; ++stage) api(device->SetTexture(stage, stages[stage]), "SetTexture");
        for (unsigned stage = 0; stage < 4; ++stage) { // captured s0..s3 rows
            api(device->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR), "MAG");
            api(device->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC), "MIN");
            api(device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR), "MIP");
            api(device->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY, max_anisotropy), "ANISO");
            api(device->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, 0), "MAXMIP");
            api(device->SetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, 0), "BIAS");
            api(device->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, 0), "SRGB");
            api(device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP), "ADDRESSU");
            api(device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP), "ADDRESSV");
        }
        api(device->SetSamplerState(4, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR), "cube MAG");
        api(device->SetSamplerState(4, D3DSAMP_MINFILTER, D3DTEXF_LINEAR), "cube MIN");
        api(device->SetSamplerState(4, D3DSAMP_MIPFILTER, D3DTEXF_NONE), "cube MIP");
    }

    void common_state() {
        api(device->SetRenderTarget(0, target.p), "SetRenderTarget");
        api(device->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface");
        D3DVIEWPORT9 viewport{0, 0, width, height, 0, 1};
        api(device->SetViewport(&viewport), "SetViewport");
        for (auto state :
             {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_FOGENABLE, D3DRS_STENCILENABLE,
              D3DRS_DITHERENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
              D3DRS_LIGHTING, D3DRS_CLIPPLANEENABLE, D3DRS_MULTISAMPLEANTIALIAS})
            api(device->SetRenderState(state, FALSE), "render state off");
        api(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "CULLMODE");
        api(device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD), "SHADEMODE");
        api(device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA), "SRCBLEND");
        api(device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA), "DESTBLEND");
        api(device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD), "BLENDOP");
        api(device->SetVertexDeclaration(declaration.p), "SetVertexDeclaration");
        api(device->SetVertexShader(vertex_shader.p), "SetVertexShader");
        api(device->SetPixelShader(pixel_shader.p), "SetPixelShader");
    }

    void set_pixel_constants(Vec3 dir0, Vec3 dir1) {
        for (unsigned i = 0; i < 3; ++i) api(device->SetPixelShaderConstantF(i, ps_grading[i], 1), "ps c0..c2");
        const float d0[4] = {dir0.x, dir0.y, dir0.z, 0}, d1[4] = {dir1.x, dir1.y, dir1.z, 0};
        api(device->SetPixelShaderConstantF(3, ps_enable_glow, 1), "ps c3");
        api(device->SetPixelShaderConstantF(4, d0, 1), "ps c4");
        api(device->SetPixelShaderConstantF(5, light_color0, 1), "ps c5");
        api(device->SetPixelShaderConstantF(6, d1, 1), "ps c6");
        api(device->SetPixelShaderConstantF(7, light_color1, 1), "ps c7");
        api(device->SetPixelShaderConstantF(8, mat_specular_strength, 1), "ps c8");
        api(device->SetPixelShaderConstantF(9, mat_specular_power, 1), "ps c9");
        api(device->SetPixelShaderConstantF(10, mat_reflection_strength, 1), "ps c10");
        api(device->SetPixelShaderConstantF(11, mat_diffuse_strength, 1), "ps c11");
    }

    // Quad of `size` world units at `distance` along the view axis, tilted by
    // `view_degrees` about world X. Camera at the origin looking down +Z.
    void set_vertex_constants(const Mat4& world, const Mat4& world_view_projection) {
        float value[4];
        for (int j = 0; j < 4; ++j) {
            column(world_view_projection, j, value);
            api(device->SetVertexShaderConstantF(24 + j, value, 1), "vs c24..c27");
        }
        for (int j = 0; j < 3; ++j) {
            column(world, j, value);
            api(device->SetVertexShaderConstantF(28 + j, value, 1), "vs c28..c30");
            value[3] = 0; // inverse-transpose of a pure rotation, no translation
            api(device->SetVertexShaderConstantF(31 + j, value, 1), "vs c31..c33");
            float inverse_view[4] = {0, 0, 0, 0};
            inverse_view[j] = 1; // g_mViewInverse column j; .w is the camera position
            api(device->SetVertexShaderConstantF(34 + j, inverse_view, 1), "vs c34..c36");
        }
        const float tex_u[4] = {1, 0, 0, 0}, tex_v[4] = {0, 1, 0, 0}, alpha_value[4] = {1, 0, 0, 0},
                    emissive[4] = {0, 0, 0, 0}, fog_clip[4] = {1, 0, 0, 0};
        api(device->SetVertexShaderConstantF(37, tex_u, 1), "vs c37");
        api(device->SetVertexShaderConstantF(38, tex_v, 1), "vs c38");
        api(device->SetVertexShaderConstantF(39, alpha_value, 1), "vs c39");
        api(device->SetVertexShaderConstantF(40, emissive, 1), "vs c40");
        api(device->SetVertexShaderConstantF(41, fog_clip, 1), "vs c41");
        const BOOL enable_fog = FALSE;
        api(device->SetVertexShaderConstantB(0, &enable_fog, 1), "vs b0");
        const int point_lights[4] = {0, 0, 1, 0};
        api(device->SetVertexShaderConstantI(0, point_lights, 1), "vs i0");
    }

    const float* pixel_at(unsigned x, unsigned y) const { return pixels.data() + (std::size_t(y) * width + x) * 4; }

    void readback() {
        api(device->GetRenderTargetData(target.p, system.p), "GetRenderTargetData");
        D3DLOCKED_RECT lock{};
        api(system->LockRect(&lock, nullptr, D3DLOCK_READONLY), "LockRect readback");
        for (unsigned y = 0; y < height; ++y) {
            const unsigned char* row = static_cast<const unsigned char*>(lock.pBits) + std::size_t(y) * lock.Pitch;
            for (unsigned x = 0; x < width; ++x) {
                unsigned short half[4];
                std::memcpy(half, row + std::size_t(x) * 8, 8);
                float* out = pixels.data() + (std::size_t(y) * width + x) * 4;
                for (unsigned k = 0; k < 4; ++k) out[k] = from_half(half[k]);
            }
        }
        api(system->UnlockRect(), "UnlockRect readback");
    }

    Statistics measure(const std::vector<unsigned char>& mask, bool with_alpha) const {
        Statistics stats;
        double sum_luminance = 0, sum_alpha = 0;
        stats.min_alpha = 1e30;
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                if (!mask[std::size_t(y) * width + x]) continue;
                const float* p = pixel_at(x, y);
                const double l = luminance(p);
                sum_luminance += l;
                stats.max_luminance = std::max(stats.max_luminance, l);
                if (with_alpha) {
                    sum_alpha += p[3];
                    stats.min_alpha = std::min<double>(stats.min_alpha, p[3]);
                    stats.max_alpha = std::max<double>(stats.max_alpha, p[3]);
                }
                ++stats.pixels;
            }
        if (stats.pixels) {
            stats.mean_luminance = sum_luminance / stats.pixels;
            stats.mean_alpha = with_alpha ? sum_alpha / stats.pixels : 0;
        }
        if (!with_alpha || !stats.pixels) stats.min_alpha = 0;
        return stats;
    }
};

std::vector<unsigned char> erode(const std::vector<unsigned char>& mask, unsigned width, unsigned height,
                                 unsigned radius) {
    std::vector<unsigned char> out = mask;
    for (unsigned pass = 0; pass < radius; ++pass) {
        std::vector<unsigned char> next(out.size(), 0);
        for (unsigned y = 1; y + 1 < height; ++y)
            for (unsigned x = 1; x + 1 < width; ++x) {
                const std::size_t i = std::size_t(y) * width + x;
                next[i] = out[i] && out[i - 1] && out[i + 1] && out[i - width] && out[i + width];
            }
        out.swap(next);
    }
    return out;
}

struct Options {
    std::string pair_dir, texture_dir;
    float size = 100.f;
    unsigned base_width = 170;
};

int run(const Options& options) {
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3PortDistanceFixture";
    require(RegisterClassA(&cls) != 0, "window class");
    HWND window = CreateWindowA(cls.lpszClassName, "Detached port distance", WS_OVERLAPPEDWINDOW, 0, 0, 32, 32, nullptr,
                                nullptr, cls.hInstance, nullptr);
    require(window != nullptr, "window");
    HMODULE runtime = LoadLibraryA("d3d9.dll");
    require(runtime != nullptr, "D3D9 runtime");
    auto raw_export = GetProcAddress(runtime, "Direct3DCreate9");
    IDirect3D9*(WINAPI * create)(UINT) = nullptr;
    std::memcpy(&create, &raw_export, sizeof create);
    require(create != nullptr, "D3D9 factory export");
    {
        Com<IDirect3D9> factory;
        factory.p = create(D3D_SDK_VERSION);
        require(factory.p != nullptr, "factory");
        api(factory->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
                                       D3DFMT_A16B16G16R16F),
            "A16B16G16R16F render target");
        for (D3DFORMAT format : {D3DFMT_DXT5, D3DFMT_DXT1})
            api(factory->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE,
                                           format),
                "compressed texture filtering");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = pp.BackBufferHeight = 32;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        Com<IDirect3DDevice9> device;
        api(factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p),
            "CreateDevice");
        D3DCAPS9 caps{};
        api(device->GetDeviceCaps(&caps), "GetDeviceCaps");
        Fixture fixture(device.p);
        fixture.max_anisotropy = std::min<unsigned>(16, caps.MaxAnisotropy);
        std::printf("CAPS ps=%08lx vs=%08lx max_anisotropy=%lu used_anisotropy=%u "
                    "ps30_slots=%lu\n",
                    static_cast<unsigned long>(caps.PixelShaderVersion),
                    static_cast<unsigned long>(caps.VertexShaderVersion),
                    static_cast<unsigned long>(caps.MaxAnisotropy), fixture.max_anisotropy,
                    static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots));
        require((caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) != 0, "anisotropic minification");
        fixture.load_programs(options.pair_dir);
        fixture.load_textures(options.texture_dir);
        fixture.common_state();

        const Mat4 projection = captured_projection();
        const Vec3 view_direction{0, 0, -1}; // surface to camera, camera at origin
        const Vec3 captured_dir0{light_dir0[0], light_dir0[1], light_dir0[2]};
        const Vec3 captured_dir1{light_dir1[0], light_dir1[1], light_dir1[2]};
        // 512 * size / distance is the on-screen width in pixels at m00 = 0.8.
        const double pixels_per_unit = 0.5 * double(projection.m[0][0]) * fixture.width;
        unsigned cases = 0;

        for (const Configuration& configuration : configurations) {
            const double view_radians = configuration.view_degrees * 3.14159265358979 / 180;
            const double light_radians = (configuration.view_degrees + configuration.light_degrees) * 3.14159265358979 /
                                         180;
            const Vec3 normal{0.f, float(std::sin(view_radians)), float(-std::cos(view_radians))};
            const Vec3 dir0{0.f, float(std::sin(light_radians)), float(-std::cos(light_radians))};
            const Vec3 dir1 = normalize(rotate_like(captured_dir0, dir0, captured_dir1));
            const Vec3 mirror = normal * (2.f * dot(normal, dir0)) - dir0;
            std::printf("CONFIG name=%s view_degrees=%.1f light_degrees=%.1f "
                        "n_dot_l0=%.6f n_dot_l1=%.6f mirror_dot_v=%.6f "
                        "l0_dot_l1=%.6f\n",
                        configuration.name, configuration.view_degrees, configuration.light_degrees, dot(normal, dir0),
                        dot(normal, dir1), dot(mirror, view_direction), dot(dir0, dir1));
            fixture.set_pixel_constants(dir0, dir1);

            const float c = float(std::cos(view_radians)), s = float(std::sin(view_radians));
            for (unsigned step = 0; step < 3; ++step) {
                const double scale = 1 << step; // 1x, 2x, 4x the captured distance
                const double distance = pixels_per_unit * options.size / double(options.base_width) * scale;
                Mat4 world;
                world.m[0][0] = 1;
                world.m[1][1] = c;
                world.m[1][2] = s;
                world.m[2][1] = -s;
                world.m[2][2] = c;
                world.m[3][2] = float(distance);
                world.m[3][3] = 1;
                const Mat4 wvp = multiply(world, projection);
                fixture.set_vertex_constants(world, wvp);

                // Object space: the VS applies g_mWorld / g_mWorldIT itself. Local
                // normal (0,0,-1) faces the camera at the origin; the tangent runs
                // along +u and the binormal along +v (world -Y), a right-handed frame
                // with the normal.
                const float half = options.size * 0.5f;
                Vertex quad[6];
                const float corners[4][2] = {{-1, 1}, {1, 1}, {-1, -1}, {1, -1}};
                Vertex built[4];
                for (unsigned i = 0; i < 4; ++i) {
                    const float lx = corners[i][0] * half, ly = corners[i][1] * half;
                    built[i] = {{lx, ly, 0.f},
                                {corners[i][0] * 0.5f + 0.5f, 0.5f - corners[i][1] * 0.5f},
                                {0.f, 0.f, -1.f},
                                {0.f, -1.f, 0.f},
                                {1.f, 0.f, 0.f}};
                }
                quad[0] = built[0];
                quad[1] = built[1];
                quad[2] = built[2];
                quad[3] = built[2];
                quad[4] = built[1];
                quad[5] = built[3];

                for (unsigned flat = 0; flat < 2; ++flat) {
                    fixture.bind_samplers(flat != 0);
                    // Raw pass: no blending, full write mask, so oC0.a is readable.
                    api(device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "blend off");
                    api(device->SetRenderState(D3DRS_COLORWRITEENABLE, 15), "mask 15");
                    api(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0), "Clear raw");
                    api(device->BeginScene(), "BeginScene raw");
                    api(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(Vertex)), "DrawPrimitiveUP raw");
                    api(device->EndScene(), "EndScene raw");
                    fixture.readback();
                    std::vector<unsigned char> mask(std::size_t(fixture.width) * fixture.height, 0);
                    unsigned min_x = fixture.width, max_x = 0, min_y = fixture.height, max_y = 0;
                    for (unsigned y = 0; y < fixture.height; ++y)
                        for (unsigned x = 0; x < fixture.width; ++x)
                            if (fixture.pixel_at(x, y)[3] > 0.f) {
                                mask[std::size_t(y) * fixture.width + x] = 1;
                                min_x = std::min(min_x, x);
                                max_x = std::max(max_x, x);
                                min_y = std::min(min_y, y);
                                max_y = std::max(max_y, y);
                            }
                    const auto eroded = erode(mask, fixture.width, fixture.height, 2);
                    const Statistics raw_all = fixture.measure(mask, true);
                    const Statistics raw_inner = fixture.measure(eroded, true);
                    require(raw_inner.pixels > 0, "empty quad coverage");

                    // Composite pass: the captured source-over state over black.
                    api(device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "blend on");
                    api(device->SetRenderState(D3DRS_COLORWRITEENABLE, 7), "mask 7");
                    api(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0), "Clear composite");
                    api(device->BeginScene(), "BeginScene composite");
                    api(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(Vertex)),
                        "DrawPrimitiveUP composite");
                    api(device->EndScene(), "EndScene composite");
                    fixture.readback();
                    const Statistics composite_all = fixture.measure(mask, false);
                    const Statistics composite_inner = fixture.measure(eroded, false);

                    std::printf("MEASURE config=%s normal=%s step=%u distance=%.4f "
                                "quad_px_w=%u quad_px_h=%u covered=%u inner=%u "
                                "raw_mean_luma=%.9f raw_max_luma=%.9f raw_mean_alpha=%.9f "
                                "raw_min_alpha=%.9f raw_max_alpha=%.9f "
                                "raw_all_mean_luma=%.9f raw_all_mean_alpha=%.9f "
                                "comp_mean_luma=%.9f comp_max_luma=%.9f comp_all_mean_luma=%.9f\n",
                                configuration.name, flat ? "flat" : "real", step, distance,
                                max_x >= min_x ? max_x - min_x + 1 : 0, max_y >= min_y ? max_y - min_y + 1 : 0,
                                raw_all.pixels, raw_inner.pixels, raw_inner.mean_luminance, raw_inner.max_luminance,
                                raw_inner.mean_alpha, raw_inner.min_alpha, raw_inner.max_alpha, raw_all.mean_luminance,
                                raw_all.mean_alpha, composite_inner.mean_luminance, composite_inner.max_luminance,
                                composite_all.mean_luminance);
                    ++cases;
                }
            }
        }
        std::printf("RESULT PASS cases=%u\n", cases);
    }
    require(DestroyWindow(window) != 0, "destroy window");
    require(FreeLibrary(runtime) != 0, "unload D3D9 runtime");
    require(UnregisterClassA(cls.lpszClassName, cls.hInstance) != 0, "unregister window class");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            auto value = [&]() {
                require(i + 1 < argc, "missing value for " + argument);
                return std::string(argv[++i]);
            };
            if (argument == "--pair-dir")
                options.pair_dir = value();
            else if (argument == "--texture-dir")
                options.texture_dir = value();
            else if (argument == "--quad-size")
                options.size = float(std::atof(value().c_str()));
            else if (argument == "--base-width")
                options.base_width = unsigned(std::atoi(value().c_str()));
            else
                require(false, "unknown argument " + argument);
        }
        require(!options.pair_dir.empty() && !options.texture_dir.empty(), "usage: --pair-dir DIR --texture-dir DIR "
                                                                           "[--quad-size N] [--base-width PX]");
        require(options.base_width >= 8 && options.base_width <= 1024, "base width");
        return run(options);
    } catch (const std::exception& error) {
        std::printf("RESULT FAIL %s\n", error.what());
        return 1;
    }
}
