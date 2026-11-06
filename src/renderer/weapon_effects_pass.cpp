#include "weapon_effects_pass.h"
#include "ps3_program_slots.h"
#include "../proxy/cpu_state.h"
#include "../proxy/sse_scalar.h"
#include "../proxy/weapon_effects_core.h"
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <utility>
namespace x3m::renderer {
namespace {
namespace wcore = x3m::weapon_effects::core;
template <class T> void drop(T*& value) noexcept {
    if (value) {
        value->Release();
        value = nullptr;
    }
}
bool lost(HRESULT hr) noexcept {
    return hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET;
}
enum Slot : unsigned {
    GetDirect3D = 6, GetCreationParameters = 9, SetRenderState = 57, SetTexture = 65, SetSamplerState = 69,
    DrawPrimitiveUP = 83, DrawIndexedPrimitiveUP = 84, CreateVertexDeclaration = 86, SetVertexDeclaration = 87,
    CreateVertexShader = 91, SetVertexShader = 92, SetVertexShaderConstantF = 94, CreatePixelShader = 106,
    SetPixelShader = 107, SetPixelShaderConstantF = 109
};
using D = IDirect3DDevice9*;
using GetD3DFn = HRESULT(WINAPI*)(D, IDirect3D9**);
using GetCreationFn = HRESULT(WINAPI*)(D, D3DDEVICE_CREATION_PARAMETERS*);
using SetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD);
using SetTextureFn = HRESULT(WINAPI*)(D, DWORD, IDirect3DBaseTexture9*);
using SetSamplerFn = HRESULT(WINAPI*)(D, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using DrawUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using DrawIndexedUpFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);
using CreateDeclarationFn = HRESULT(WINAPI*)(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
using SetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9*);
using CreateVsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DVertexShader9**);
using SetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9*);
using CreatePsFn = HRESULT(WINAPI*)(D, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9*);
using SetConstantsFn = HRESULT(WINAPI*)(D, UINT, const float*, UINT);
// Only our authored shaders are embedded (provenance: verification/results/weapon-{shell,decal}-{vertex,pixel}-program.json).
constexpr std::uint32_t shell_vs_words[] = {
#include "weapon_shell_vertex_program_inc.h"
};
constexpr std::uint32_t shell_ps_words[] = {
#include "weapon_shell_pixel_program_inc.h"
};
constexpr std::uint32_t bubble_ps_words[] = {
#include "weapon_bubble_pixel_program_inc.h"
};
constexpr std::uint32_t decal_vs_words[] = {
#include "weapon_decal_vertex_program_inc.h"
};
constexpr std::uint32_t decal_ps_words[] = {
#include "weapon_decal_pixel_program_inc.h"
};
// Step 2 (provenance: verification/results/weapon-{sprite,glow}-{vertex,pixel}-program.json).
constexpr std::uint32_t sprite_vs_words[] = {
#include "weapon_sprite_vertex_program_inc.h"
};
constexpr std::uint32_t sprite_ps_words[] = {
#include "weapon_sprite_pixel_program_inc.h"
};
constexpr std::uint32_t glow_vs_words[] = {
#include "weapon_glow_vertex_program_inc.h"
};
constexpr std::uint32_t glow_ps_words[] = {
#include "weapon_glow_pixel_program_inc.h"
};
constexpr D3DVERTEXELEMENT9 sprite_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 32, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    {0, 44, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0}, D3DDECL_END()};
static_assert(sizeof(WeaponSpriteVertex) == 48, "the sprite declaration follows the vertex");
constexpr D3DVERTEXELEMENT9 glow_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 28, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1}, D3DDECL_END()};
static_assert(sizeof(WeaponGlowVertex) == 40, "the glow declaration follows the vertex");
template <std::size_t N> std::uint32_t vs3_slots(const std::uint32_t (&words)[N]) noexcept {
    std::uint32_t copy[N];
    std::memcpy(copy, words, sizeof copy);
    if (copy[0] != 0xfffe0300u) return 0;
    copy[0] = ps3_version_token;
    return ps3_program_slots(copy, N);
}
constexpr D3DVERTEXELEMENT9 shell_elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                                D3DDECL_END()};
static_assert(sizeof(WeaponEffectsPass::CellVertex) == 16, "the patch declaration follows the vertex");
constexpr D3DVERTEXELEMENT9 decal_elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                                {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                                D3DDECL_END()};
struct DecalVertex {
    float p[4];      // view-space centre, radius
    float shade[4];  // corner x, y, soft radius, alpha
};
static_assert(sizeof(DecalVertex) == 32, "the decal declaration follows the vertex");
bool finite(float v) noexcept {
    return v == v && v <= 3.4e38f && v >= -3.4e38f;
}
bool finite_all(const float* v, unsigned n) noexcept {
    for (unsigned i = 0; i < n; ++i)
        if (!finite(v[i])) return false;
    return true;
}
// The box index list (attach time): corner c = (c & 1 ? max : min) u, (c & 2 ? max : min) v, (c & 4 ? max : min) a.
// Every triangle is wound with its outward normal by the right-hand rule in the face frame (u, v, a); run() culls the
// faces turned to the eye (the winding's sense on screen follows the sign of det(U, V, A) in view space), so each
// pixel is shaded once per box, the eye inside it too.
bool build_indices(std::uint16_t* idx) noexcept {
    static constexpr std::uint16_t box[36] = {0, 2, 3, 0, 3, 1,  // -a
                                              4, 5, 7, 4, 7, 6,  // +a
                                              0, 1, 5, 0, 5, 4,  // -v
                                              2, 6, 7, 2, 7, 3,  // +v
                                              0, 4, 6, 0, 6, 2,  // -u
                                              1, 3, 7, 1, 7, 5}; // +u
    std::memcpy(idx, box, sizeof box);
    return true;
}
// Step 2's batched index list (attach time): quad q's corners 4q..4q+3 (a streak's tail-left, tail-right, head-left,
// head-right; a flash's or a glow rectangle's corners in the same order) as two triangles (no culling: a streak's
// winding depends on its direction on screen).
void build_batch_indices(std::uint16_t* quads) noexcept {
    for (unsigned q = 0; q < weapon_sprite_quads; ++q) {
        const std::uint16_t b = std::uint16_t(4 * q);
        const std::uint16_t t[6] = {b, std::uint16_t(b + 1), std::uint16_t(b + 2), std::uint16_t(b + 2),
                                    std::uint16_t(b + 1), std::uint16_t(b + 3)};
        std::memcpy(quads + 6 * q, t, sizeof t);
    }
}
static_assert(weapon_sprite_quads * 4 <= 0x10000, "16-bit batch indices");
} // namespace

WeaponLook default_weapon_look() noexcept {
    WeaponLook l{};
    l.ripple_speed = wcore::ripple_speed;
    l.ring_width = wcore::ring_width;
    l.ring_gain = wcore::ring_gain;
    l.centre_gain = wcore::centre_gain;
    l.centre_fill = wcore::centre_fill;
    l.fade_s = float(wcore::fade_ms) * 1e-3f;
    l.stagger = wcore::stagger;
    l.edge_width = wcore::edge_width;
    l.fill_ratio = wcore::fill_ratio;
    l.cell_gain = wcore::cell_gain;
    l.bubble_near_gain = 1.f;
    l.bubble_far_gain = wcore::bubble_far_gain;
    return l;
}
WeaponEffectsPass::~WeaponEffectsPass() {
    detach();
}
void WeaponEffectsPass::release_hull_objects() noexcept {
    drop(sprite_vs_);
    drop(sprite_ps_);
    drop(glow_vs_);
    drop(glow_ps_);
    drop(sprite_declaration_);
    drop(glow_declaration_);
}
void WeaponEffectsPass::release_objects() noexcept {
    drop(shell_vs_);
    drop(shell_ps_);
    drop(bubble_ps_);
    drop(decal_vs_);
    drop(decal_ps_);
    drop(shell_declaration_);
    drop(decal_declaration_);
    release_hull_objects();
}
void WeaponEffectsPass::detach() noexcept {
    PreserveCpuState guard;
    release_objects();
    device_ = nullptr;
    vtable_ = nullptr;
    caps_ = {};
    reset_pending_ = false;
}
void WeaponEffectsPass::before_reset() noexcept {
    PreserveCpuState guard;
    release_objects();
    reset_pending_ = device_ != nullptr;
}
void WeaponEffectsPass::after_reset(HRESULT hr) noexcept {
    if (SUCCEEDED(hr)) reset_pending_ = false;
}
bool WeaponEffectsPass::hull_objects_ready() const noexcept {
    return sprite_vs_ && sprite_ps_ && glow_vs_ && glow_ps_ && sprite_declaration_ && glow_declaration_;
}
bool WeaponEffectsPass::resources_ready() const noexcept {
    return device_ && caps_.enabled && !reset_pending_ && shell_vs_ && shell_ps_ && bubble_ps_ && decal_vs_ && decal_ps_ &&
           shell_declaration_ && decal_declaration_ && (!caps_.hull_enabled || hull_objects_ready());
}
unsigned WeaponEffectsPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(shell_vs_), static_cast<const void*>(shell_ps_),
                          static_cast<const void*>(bubble_ps_), static_cast<const void*>(decal_vs_), static_cast<const void*>(decal_ps_),
                          static_cast<const void*>(shell_declaration_), static_cast<const void*>(decal_declaration_),
                          static_cast<const void*>(sprite_vs_), static_cast<const void*>(sprite_ps_),
                          static_cast<const void*>(glow_vs_), static_cast<const void*>(glow_ps_),
                          static_cast<const void*>(sprite_declaration_), static_cast<const void*>(glow_declaration_)})
        n += p != nullptr;
    return n;
}
HRESULT WeaponEffectsPass::attach(D d, void* const* native, const D3DCAPS9& caps, D3DFORMAT format, bool hull) noexcept {
    PreserveCpuState guard;
    detach();
    auto refuse = [&](const char* reason, HRESULT hr = D3DERR_NOTAVAILABLE) {
        release_objects();
        device_ = nullptr;
        vtable_ = nullptr;
        caps_.enabled = false;
        caps_.reason = reason;
        return hr;
    };
    if (!d || !native) return refuse("device_native_table", E_INVALIDARG);
    device_ = d;
    vtable_ = native;
    if (caps.PixelShaderVersion < D3DPS_VERSION(3, 0) || caps.VertexShaderVersion < D3DVS_VERSION(3, 0))
        return refuse("shader_model3");
    caps_.shell_vs_slots = vs3_slots(shell_vs_words);
    caps_.decal_vs_slots = vs3_slots(decal_vs_words);
    caps_.shell_ps_slots = ps3_program_slots(shell_ps_words, std::size(shell_ps_words));
    caps_.decal_ps_slots = ps3_program_slots(decal_ps_words, std::size(decal_ps_words));
    caps_.bubble_ps_slots = ps3_program_slots(bubble_ps_words, std::size(bubble_ps_words));
    if (!caps_.shell_vs_slots || !caps_.decal_vs_slots || !caps_.shell_ps_slots || !caps_.decal_ps_slots ||
        !caps_.bubble_ps_slots)
        return refuse("compiled_program");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) || !(caps.SrcBlendCaps & D3DPBLENDCAPS_ONE) ||
        !(caps.DestBlendCaps & D3DPBLENDCAPS_ONE))
        return refuse("blend_caps");
    if (!(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLNONE) || !(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLCW) ||
        !(caps.PrimitiveMiscCaps & D3DPMISCCAPS_CULLCCW))
        return refuse("cull_caps");
    if (caps.MaxPrimitiveCount < patch_triangles || caps.MaxVertexIndex < patch_vertices || caps.MaxStreams < 1u)
        return refuse("limits");
    IDirect3D9* api = nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    HRESULT hr = call<GetD3DFn>(GetDirect3D)(d, &api);
    if (SUCCEEDED(hr) && !api) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = call<GetCreationFn>(GetCreationParameters)(d, &creation);
    if (SUCCEEDED(hr))
        hr = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, format,
                                    D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE,
                                    D3DFMT_A16B16G16R16F);
    drop(api);
    caps_.fp16_blending = hr;
    if (faults_ & 1u) {
        caps_.fp16_blending = D3DERR_NOTAVAILABLE;
        hr = D3DERR_NOTAVAILABLE;
    }
    if (hr != D3D_OK) return refuse("fp16_blending", D3DERR_NOTAVAILABLE);
    if (!indices_built_) {
        indices_built_ = build_indices(indices_);
        if (!indices_built_) return refuse("box_mesh", E_FAIL);
        build_batch_indices(sprite_indices_);
    }
    hr = create_objects();
    if (FAILED(hr)) return refuse("program_create", hr);
    caps_.enabled = true;
    caps_.reason = "ok";
    // Step 2 (shields,hull only): the sprite batch and the glow, their batch limits (2 x weapon_sprite_quads triangles
    // over 4 x as many vertices) and programs. A device refusing any of them keeps the shield effect: the pass stays
    // enabled with hull_enabled false (hull_reason), and the glue's latch keeps the hull hits native.
    caps_.hull_reason = "not_requested";
    if (hull) {
        caps_.sprite_vs_slots = vs3_slots(sprite_vs_words);
        caps_.glow_vs_slots = vs3_slots(glow_vs_words);
        caps_.sprite_ps_slots = ps3_program_slots(sprite_ps_words, std::size(sprite_ps_words));
        caps_.glow_ps_slots = ps3_program_slots(glow_ps_words, std::size(glow_ps_words));
        if (!caps_.sprite_vs_slots || !caps_.glow_vs_slots || !caps_.sprite_ps_slots || !caps_.glow_ps_slots)
            caps_.hull_reason = "hull_compiled_program";
        else if (caps.MaxPrimitiveCount < 2u * weapon_sprite_quads || caps.MaxVertexIndex < 4u * weapon_sprite_quads - 1u)
            caps_.hull_reason = "hull_limits";
        else if (FAILED(create_hull_objects()))
            caps_.hull_reason = "hull_program_create";
        else {
            caps_.hull_enabled = true;
            caps_.hull_reason = "ok";
        }
    }
    return S_OK;
}
HRESULT WeaponEffectsPass::create_objects() noexcept {
    D d = device_;
    HRESULT hr = (faults_ & 8u) ? E_FAIL : S_OK; // fixture fault bit 3: program creation refused
    if (SUCCEEDED(hr) && !shell_vs_) hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(shell_vs_words), &shell_vs_);
    if (SUCCEEDED(hr) && !shell_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(shell_ps_words), &shell_ps_);
    if (SUCCEEDED(hr) && !bubble_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(bubble_ps_words), &bubble_ps_);
    if (SUCCEEDED(hr) && !decal_vs_)
        hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(decal_vs_words), &decal_vs_);
    if (SUCCEEDED(hr) && !decal_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(decal_ps_words), &decal_ps_);
    if (SUCCEEDED(hr) && !shell_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, shell_elements, &shell_declaration_);
    if (SUCCEEDED(hr) && !decal_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, decal_elements, &decal_declaration_);
    if (SUCCEEDED(hr) &&
        !(shell_vs_ && shell_ps_ && bubble_ps_ && decal_vs_ && decal_ps_ && shell_declaration_ && decal_declaration_))
        hr = E_FAIL;
    if (FAILED(hr)) release_objects();
    return hr;
}
HRESULT WeaponEffectsPass::create_hull_objects() noexcept {
    D d = device_;
    HRESULT hr = (faults_ & 16u) ? E_FAIL : S_OK; // fixture fault bit 4: the hull programs refused
    if (SUCCEEDED(hr) && !sprite_vs_)
        hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(sprite_vs_words), &sprite_vs_);
    if (SUCCEEDED(hr) && !sprite_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(sprite_ps_words), &sprite_ps_);
    if (SUCCEEDED(hr) && !glow_vs_)
        hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(glow_vs_words), &glow_vs_);
    if (SUCCEEDED(hr) && !glow_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(glow_ps_words), &glow_ps_);
    if (SUCCEEDED(hr) && !sprite_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, sprite_elements, &sprite_declaration_);
    if (SUCCEEDED(hr) && !glow_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, glow_elements, &glow_declaration_);
    if (SUCCEEDED(hr) && !hull_objects_ready()) hr = E_FAIL;
    if (FAILED(hr)) release_hull_objects();
    return hr;
}
HRESULT WeaponEffectsPass::ensure_resources() noexcept {
    if (!device_ || !caps_.enabled) return E_INVALIDARG;
    if (reset_pending_) return E_FAIL;
    if (resources_ready()) return S_OK;
    PreserveCpuState guard;
    const HRESULT hr = create_objects();
    if (FAILED(hr) || !caps_.hull_enabled) return hr;
    // The hull programs after a Reset: a refusal now turns the hull off (the shields keep drawing).
    if (FAILED(create_hull_objects())) {
        caps_.hull_enabled = false;
        caps_.hull_reason = "hull_program_create";
    }
    return S_OK;
}
namespace {
// The pixel constants of one draw (src/effects/weapon_hex_patch.hlsl): c2..c18 lattice, colour, patch, timing, gains,
// shape, anchor frame, the ring hits and their points (hex_block, the decals' whole set); patches add c19..c25
// (shell_block, weapon_shell_ps.hlsl: the projection, the screen and footprint terms, the owner's axes, centre and the
// eye in the owner frame; a ship's bubble draw bubble_block, weapon_bubble_ps.hlsl) and the cell map (map_block): c26..c57 four packed keys per register in ascending order
// (unused -1), c58..c89 their clocks + 1 (negative for a hit's centre cell, 0 not lit yet), c90..c93 the key range
// [first, last] of each group of sixteen keys (unused: never matched), c94..c95 the box (min, max in the face frame). Returns false for a degenerate draw (skipped,
// never drawn wrong).
constexpr unsigned hex_head = 17, shell_tail = 7, map_blocks = weapon_patch_cells / 4, map_groups = map_blocks / 4;
constexpr unsigned keys_at = 2 + hex_head + shell_tail, clocks_at = keys_at + map_blocks, ranges_at = clocks_at + map_blocks;
static_assert(weapon_patch_cells % 16 == 0 && ranges_at + map_groups / 2 + 2 <= 224, "the cell map fits ps_3_0's constants");
bool hex_block(const WeaponLook& l, float cell, float radius, float reach, float seed, float surface2,
               const float colour[3], const float* anchor, const float* anchor_u, const float* anchor_v, unsigned hits,
               const float (*hit_cell)[2], const float (*point)[2], const float* ages, float* out) noexcept {
    if (!(cell > 0.f) || !(radius > 0.f) || !(reach >= 0.f) || !finite(cell) || !finite(radius) || !finite(reach) ||
        !finite(seed) || !finite(surface2) || !finite_all(colour, 3) || hits > wcore::hit_slots)
        return false;
    const float v = l.ripple_speed * radius;
    const float block[9 * 4] = {
        1.f / cell, cell, seed, surface2,                                        // c2 lattice
        colour[0], colour[1], colour[2], 0.f,                                    // c3 colour
        radius, 1.f / radius, 1.f / v, v,                                        // c4 patch
        1.f / l.fade_s, l.stagger, 0.f, l.edge_width,                            // c5 timing
        l.cell_gain, l.fill_ratio, l.ring_gain, l.centre_gain,                   // c6 gains
        // c7 shape: the reach with a tolerance of 1e-3 cell (a footprint centre lies exactly on it)
        0.5f - l.edge_width, 1.f / (l.ring_width * radius), reach + 1e-3f * cell, l.centre_fill,
        anchor[0], anchor[1], anchor[2], 0.f,                                    // c8..c10 the anchor frame
        anchor_u[0], anchor_u[1], anchor_u[2], 0.f,
        anchor_v[0], anchor_v[1], anchor_v[2], 0.f};
    std::memcpy(out, block, sizeof block);
    for (unsigned k = 0; k < 4; ++k) {
        float* h = out + 36 + 4 * k;     // c11..c14
        float* p = out + 52 + 4 * k;     // c15..c18
        const bool on = k < hits;
        h[0] = on ? hit_cell[k][0] : 0.f;
        h[1] = on ? hit_cell[k][1] : 0.f;
        h[2] = on ? ages[k] : 0.f;
        h[3] = on ? 1.f : 0.f;
        p[0] = on ? point[k][0] : 0.f;
        p[1] = on ? point[k][1] : 0.f;
        p[2] = p[3] = 0.f;
        if (on && (!finite(h[0]) || !finite(h[1]) || !finite(h[2]) || !finite(p[0]) || !finite(p[1]))) return false;
    }
    return finite_all(out, hex_head * 4);
}
// The cell map of a patch: all 32 key registers in ascending key order (unused -1), the clock registers of its groups
// of sixteen keys (*sent) and the eight groups' key ranges (an unused group's range is empty), so the program scans
// only the group whose range holds the pixel's key; false for a cell the program cannot carry. O(cells log cells): a
// heap sort of the indices (sort_by_key, no allocation, review F5).
bool map_block(const WeaponPatch& p, float* keys, float* clocks, float* ranges, unsigned* sent) noexcept {
    if (p.cells > weapon_patch_cells || !p.cells) return false;
    constexpr float key_max = float((2 * weapon_cell_key_bias) * 4096);
    std::uint8_t order[weapon_patch_cells];
    for (unsigned i = 0; i < p.cells; ++i) {
        if (!(p.cell_key[i] >= 0.f) || !(p.cell_key[i] < key_max) || !finite(p.cell_since[i])) return false;
        order[i] = std::uint8_t(i);
    }
    sort_by_key(order, p.cells, p.cell_key);
    const unsigned used = (p.cells + 15) / 16 * 4; // registers of the groups in use
    for (unsigned i = 0; i < weapon_patch_cells; ++i) {
        const bool on = i < p.cells;
        const unsigned c = on ? order[i] : 0u;
        keys[i] = on ? p.cell_key[c] : -1.f;
        if (i < used * 4)
            clocks[i] = on && p.cell_since[c] >= 0.f ? (p.cell_centre[c] ? -1.f : 1.f) * (p.cell_since[c] + 1.f) : 0.f;
    }
    for (unsigned g = 0; g < map_groups; ++g) {
        const unsigned first = 16 * g, last = 16 * g + 15 < p.cells ? 16 * g + 15 : p.cells - 1;
        const bool on = first < p.cells;
        ranges[2 * g] = on ? keys[first] : 1e30f;
        ranges[2 * g + 1] = on ? keys[last] : -1e30f;
    }
    *sent = used;
    return true;
}
// The box's corners (CellVertex) from the patch's box; false for a degenerate box.
bool write_box(const WeaponPatch& p, WeaponEffectsPass::CellVertex* v) noexcept {
    if (!finite_all(p.box, 6) || !(p.box[3] > p.box[0]) || !(p.box[4] > p.box[1]) || !(p.box[5] > p.box[2])) return false;
    for (unsigned c = 0; c < WeaponEffectsPass::patch_vertices; ++c) {
        v[c].corner[0] = p.box[c & 1u ? 3 : 0];
        v[c].corner[1] = p.box[c & 2u ? 4 : 1];
        v[c].corner[2] = p.box[c & 4u ? 5 : 2];
        v[c].corner[3] = 1.f;
    }
    return true;
}
// The patch tail c19..c25 (weapon_shell_ps.hlsl): the frame's jittered projection (m00, m11, m20, m21); 2 / W, 2 / H,
// unused and the triplanar sharpness; the owner's axes in view (L_k, owner_frame) and its
// centre (view), so the program takes a view point to the owner frame as p_k = (v - centre) . L_k; and the eye in the
// owner frame (p of the view origin), for the normal's side, with the triplanar floor. False for a degenerate frame.
bool shell_block(const WeaponEffectsFrame& f, const WeaponPatch& p, float out[shell_tail * 4]) noexcept {
    if (!(f.m00 != 0.f) || !(f.m11 != 0.f) || !f.width || !f.height) return false;
    const float block[shell_tail * 4] = {
        f.m00, f.m11, f.m20, f.m21,
        2.f / float(f.width), 2.f / float(f.height), 0.f, wcore::triplanar_sharpness,
        p.axes[0][0], p.axes[0][1], p.axes[0][2], 0.f,
        p.axes[1][0], p.axes[1][1], p.axes[1][2], 0.f,
        p.axes[2][0], p.axes[2][1], p.axes[2][2], 0.f,
        p.centre[0], p.centre[1], p.centre[2], 0.f,
        -(p.centre[0] * p.axes[0][0] + p.centre[1] * p.axes[0][1] + p.centre[2] * p.axes[0][2]),
        -(p.centre[0] * p.axes[1][0] + p.centre[1] * p.axes[1][1] + p.centre[2] * p.axes[1][2]),
        -(p.centre[0] * p.axes[2][0] + p.centre[1] * p.axes[2][1] + p.centre[2] * p.axes[2][2]),
        wcore::triplanar_floor};
    std::memcpy(out, block, sizeof block);
    return finite_all(out, shell_tail * 4);
}
// The bubble's tail c19..c25 (weapon_bubble_ps.hlsl), the shell's register count, so the cell map keeps c26..c95: the
// view ray of raster r is ((2 r.x / W - 1 - m20) / m00, (1 - 2 r.y / H - m21) / m11, 1) (view z = t along it); taken
// to the owner frame (the axes L_k) and scaled by 1 / e it is r.x X + r.y Y + D0 (X, Y, D0 with the far and near
// gains and the triplanar sharpness in .w); the eye's normalised point (-centre . L_k - c_k) / e_k with the near z;
// 1 / e with the triplanar floor; e * u and e * v with c . u and c . v (the lattice coordinates of a normalised point).
// In double (a far owner's eye point is large against the bubble). False for a degenerate frame or bubble.
bool bubble_block(const WeaponEffectsFrame& f, const WeaponPatch& p, float out[shell_tail * 4]) noexcept {
    if (!(f.m00 != 0.f) || !(f.m11 != 0.f) || !f.width || !f.height || !finite_all(p.bubble_c, 3) ||
        !finite_all(p.bubble_e, 3) || !(p.bubble_e[0] > 0.f) || !(p.bubble_e[1] > 0.f) || !(p.bubble_e[2] > 0.f))
        return false;
    const double sx = 2.0 / double(int(f.width)), sy = 2.0 / double(int(f.height)); // signed: cvtsi2sd
    const double r0[3] = {(-1.0 - double(f.m20)) / double(f.m00), (1.0 - double(f.m21)) / double(f.m11), 1.0};
    const double rx = sx / double(f.m00), ry = -sy / double(f.m11);
    double X[3], Y[3], D0[3], O[3], ie[3], U[3], V[3], cu = 0.0, cv = 0.0;
    for (unsigned k = 0; k < 3; ++k) {
        const double L[3] = {double(p.axes[k][0]), double(p.axes[k][1]), double(p.axes[k][2])};
        ie[k] = 1.0 / double(p.bubble_e[k]);
        X[k] = rx * L[0] * ie[k];
        Y[k] = ry * L[1] * ie[k];
        D0[k] = (r0[0] * L[0] + r0[1] * L[1] + r0[2] * L[2]) * ie[k];
        const double eye = -(double(p.centre[0]) * L[0] + double(p.centre[1]) * L[1] + double(p.centre[2]) * L[2]);
        O[k] = (eye - double(p.bubble_c[k])) * ie[k];
        U[k] = double(p.bubble_e[k]) * double(p.anchor_u[k]);
        V[k] = double(p.bubble_e[k]) * double(p.anchor_v[k]);
        cu += double(p.bubble_c[k]) * double(p.anchor_u[k]);
        cv += double(p.bubble_c[k]) * double(p.anchor_v[k]);
    }
    const float block[shell_tail * 4] = {
        float(X[0]), float(X[1]), float(X[2]), f.look.bubble_far_gain,
        float(Y[0]), float(Y[1]), float(Y[2]), f.look.bubble_near_gain,
        float(D0[0]), float(D0[1]), float(D0[2]), wcore::triplanar_sharpness,
        float(O[0]), float(O[1]), float(O[2]), f.near_z,
        float(ie[0]), float(ie[1]), float(ie[2]), wcore::triplanar_floor,
        float(U[0]), float(U[1]), float(U[2]), float(cu),
        float(V[0]), float(V[1]), float(V[2]), float(cv)};
    std::memcpy(out, block, sizeof block);
    return finite_all(out, shell_tail * 4);
}
// The draw's box (face frame u, v, a) as bounds of the bubble's normalised owner coordinates q = (x - c) / e (the
// pixel program's test; the face's axes are signed owner axes, so each maps to one owner axis): out[0..2] min, out[4..6]
// max (.w 0).
void bubble_box_normalised(const WeaponPatch& p, float out[8]) noexcept {
    const float* dirs[3] = {p.anchor_u, p.anchor_v, p.anchor};
    for (unsigned k = 0; k < 3; ++k) {
        float lo = -1e30f, hi = 1e30f;
        for (unsigned f = 0; f < 3; ++f) {
            const float d = dirs[f][k];
            if (!(d > 0.5f || d < -0.5f)) continue;
            const float a = d > 0.f ? p.box[f] : -p.box[3 + f], b = d > 0.f ? p.box[3 + f] : -p.box[f];
            lo = (a - p.bubble_c[k]) / p.bubble_e[k];
            hi = (b - p.bubble_c[k]) / p.bubble_e[k];
        }
        out[k] = lo;
        out[4 + k] = hi;
    }
    out[3] = out[7] = 0.f;
}
} // namespace
HRESULT WeaponEffectsPass::run(const WeaponEffectsFrame& f, WeaponEffectsReport* output) noexcept {
    PreserveCpuState guard;
    WeaponEffectsReport r{};
    const unsigned initial = calls_;
    auto finish = [&](HRESULT hr) {
        r.operation = hr;
        r.calls = calls_ - initial;
        if (output) *output = r;
        return hr;
    };
    auto refuse = [&](WeaponEffectsStep step, HRESULT hr) {
        r.failed = step;
        if (lost(hr)) reset_pending_ = true;
        return finish(hr);
    };
    if (!device_ || !caps_.enabled) return refuse(WeaponEffectsStep::Validate, E_INVALIDARG);
    if (reset_pending_) return refuse(WeaponEffectsStep::Validate, E_FAIL);
    if (!f.width || !f.height || !f.lane || (f.patch_count && !f.patches) || (f.decal_count && !f.decals) ||
        f.patch_count > wcore::max_patches || f.decal_count > wcore::max_decals || !(f.m00 > 0.f) || !(f.m11 > 0.f) ||
        !finite(f.m20) || !finite(f.m21) || !(f.near_z > 0.f) || (f.sprite_quads && !f.sprites) ||
        (f.glow_boxes && !f.glows) || f.sprite_quads > weapon_sprite_quads || f.glow_boxes > weapon_glow_boxes)
        return refuse(WeaponEffectsStep::Validate, E_INVALIDARG);
    // Without the hull programs the sprites and glows are not drawn (the glue builds none then).
    const unsigned sprite_quads = caps_.hull_enabled ? f.sprite_quads : 0u, glow_boxes = caps_.hull_enabled ? f.glow_boxes : 0u;
    if (!f.patch_count && !f.decal_count && !sprite_quads && !glow_boxes) return finish(S_FALSE);
    const WeaponLook& l = f.look;
    if (!(l.ripple_speed > 0.f) || !(l.ring_width > 0.f) || !finite(l.centre_gain) || !finite(l.centre_fill) ||
        !(l.fade_s > 0.f) || !(l.edge_width > 0.f) || !(l.edge_width < 0.5f) ||
        !(l.stagger >= 0.f) || !(l.stagger < 2.f) || !finite(l.ring_gain) ||
        !finite(l.cell_gain) || !finite(l.fill_ratio) || !(l.bubble_near_gain >= 0.f) || !finite(l.bubble_near_gain) ||
        !(l.bubble_far_gain >= 0.f) || !finite(l.bubble_far_gain))
        return refuse(WeaponEffectsStep::Validate, E_INVALIDARG);
    HRESULT hr = ensure_resources();
    if (FAILED(hr)) return refuse(WeaponEffectsStep::Resources, hr);
    D d = device_;
    auto step = [&](WeaponEffectsStep at, HRESULT value) {
        if (FAILED(value) && SUCCEEDED(hr)) {
            hr = value;
            r.failed = at;
        }
        return SUCCEEDED(hr);
    };
    auto draw_fault = [&](HRESULT draw) {
        if (faults_ & 6u) {
            faults_ &= ~2u;
            return HRESULT(E_FAIL);
        }
        return draw;
    };
    const float W = float(f.width), H = float(f.height);
    const float lane[8] = {W, H, 1.f / W, 1.f / H, f.four_channel ? 1.f : 0.f, f.m22, f.m32, 0.f};
    const float projection[4] = {f.m00, f.m11, f.m20, f.m21};
    const float viewport[4] = {W * .5f, H * .5f, 2.f / W, 2.f / H};
    step(WeaponEffectsStep::State, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 0, lane, 2));
    step(WeaponEffectsStep::State, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 0, projection, 1));
    step(WeaponEffectsStep::State, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 1, viewport, 1));
    step(WeaponEffectsStep::State, call<SetTextureFn>(SetTexture)(d, 0, f.lane));
    for (auto s : {std::pair{D3DSAMP_MINFILTER, DWORD(D3DTEXF_POINT)}, std::pair{D3DSAMP_MAGFILTER, DWORD(D3DTEXF_POINT)},
                   std::pair{D3DSAMP_MIPFILTER, DWORD(D3DTEXF_NONE)}, std::pair{D3DSAMP_ADDRESSU, DWORD(D3DTADDRESS_CLAMP)},
                   std::pair{D3DSAMP_ADDRESSV, DWORD(D3DTADDRESS_CLAMP)}, std::pair{D3DSAMP_SRGBTEXTURE, DWORD(FALSE)}})
        step(WeaponEffectsStep::State, call<SetSamplerFn>(SetSamplerState)(d, 0, s.first, s.second));
    for (auto s : {std::pair{D3DRS_CLIPPING, DWORD(TRUE)}, std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)},
                   std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_ONE)}, std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_ONE)},
                   std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_ADD)},
                   std::pair{D3DRS_ZENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_SEPARATEALPHABLENDENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)}, std::pair{D3DRS_SRGBWRITEENABLE, DWORD(FALSE)},
                   std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}})
        step(WeaponEffectsStep::State, call<SetRsFn>(SetRenderState)(d, s.first, s.second));
    // The glows and embers first (look 2.10's order): one batched draw of their screen rectangles (no cull).
    if (SUCCEEDED(hr) && glow_boxes) {
        const float limits[4] = {f.near_z, 0.f, 0.f, 0.f};
        const float glow_ps[8] = {f.m00, f.m11, f.m20, f.m21, 2.f / W, 2.f / H, 0.f, 0.f};
        step(WeaponEffectsStep::Glows, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Glows, call<SetDeclarationFn>(SetVertexDeclaration)(d, glow_declaration_));
        step(WeaponEffectsStep::Glows, call<SetVsFn>(SetVertexShader)(d, glow_vs_));
        step(WeaponEffectsStep::Glows, call<SetPsFn>(SetPixelShader)(d, glow_ps_));
        step(WeaponEffectsStep::Glows, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, limits, 1));
        step(WeaponEffectsStep::Glows, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, glow_ps, 2));
        if (SUCCEEDED(hr)) {
            const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                d, D3DPT_TRIANGLELIST, 0, glow_boxes * 4, glow_boxes * 2, sprite_indices_, D3DFMT_INDEX16, f.glows,
                UINT(sizeof(WeaponGlowVertex))));
            if (step(WeaponEffectsStep::Glows, draw)) {
                r.glow_boxes = glow_boxes;
                r.drew = true;
            }
        }
    }
    if (SUCCEEDED(hr) && f.patch_count) {
        step(WeaponEffectsStep::Patches, call<SetDeclarationFn>(SetVertexDeclaration)(d, shell_declaration_));
        step(WeaponEffectsStep::Patches, call<SetVsFn>(SetVertexShader)(d, shell_vs_));
        IDirect3DPixelShader9* bound = nullptr; // the hull decal's or the bubble's program, set when it changes
        for (unsigned i = 0; i < f.patch_count && SUCCEEDED(hr); ++i) {
            const WeaponPatch& p = f.patches[i];
            bool ok = finite_all(p.centre, 3) && finite_all(p.view_u, 3) && finite_all(p.view_v, 3) &&
                      finite_all(p.view_a, 3) && finite_all(p.anchor, 3) && finite_all(p.anchor_u, 3) &&
                      finite_all(p.anchor_v, 3) && finite_all(&p.axes[0][0], 9) && p.cells > 0;
            float ps[(hex_head + shell_tail + map_blocks) * 4], clocks[map_blocks * 4], ranges[map_groups * 2 + 8];
            unsigned map_sent = 0;
            ok = ok && hex_block(l, p.cell, p.radius, p.reach, p.seed, 0.f, p.colour, p.anchor, p.anchor_u, p.anchor_v,
                                 p.hits, p.hit_cell, p.hit, p.age_s, ps) &&
                 write_box(p, vertices_) &&
                 map_block(p, ps + (hex_head + shell_tail) * 4, clocks, ranges, &map_sent);
            if (!ok) continue; // a degenerate patch is skipped, never drawn wrong
            if (!(p.bubble ? bubble_block(f, p, ps + hex_head * 4) : shell_block(f, p, ps + hex_head * 4))) continue; // c19..c25
            IDirect3DPixelShader9* program = p.bubble ? bubble_ps_ : shell_ps_;
            if (program != bound) {
                if (!step(WeaponEffectsStep::Patches, call<SetPsFn>(SetPixelShader)(d, program))) break;
                bound = program;
            }
            const float vs[4 * 4] = {p.centre[0], p.centre[1], p.centre[2], f.near_z,
                                     p.view_u[0], p.view_u[1], p.view_u[2], 0.f,
                                     p.view_v[0], p.view_v[1], p.view_v[2], 0.f,
                                     p.view_a[0], p.view_a[1], p.view_a[2], 0.f};
            // Only the faces turned away from the eye draw: right-hand outward triangles face the eye clockwise on
            // screen when (U, V, A) keeps its sense in view space (det > 0), else counter-clockwise.
            const float det = p.view_a[0] * (p.view_u[1] * p.view_v[2] - p.view_u[2] * p.view_v[1]) +
                              p.view_a[1] * (p.view_u[2] * p.view_v[0] - p.view_u[0] * p.view_v[2]) +
                              p.view_a[2] * (p.view_u[0] * p.view_v[1] - p.view_u[1] * p.view_v[0]);
            step(WeaponEffectsStep::Patches,
                 call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(det > 0.f ? D3DCULL_CW : D3DCULL_CCW)));
            step(WeaponEffectsStep::Patches, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, vs, 4));
            step(WeaponEffectsStep::Patches,
                 call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, ps, hex_head + shell_tail + map_blocks));
            step(WeaponEffectsStep::Patches, call<SetConstantsFn>(SetPixelShaderConstantF)(d, clocks_at, clocks, map_sent));
            float box4[8] = {p.box[0], p.box[1], p.box[2], 0.f, p.box[3], p.box[4], p.box[5], 0.f};
            if (p.bubble) bubble_box_normalised(p, box4); // c94..c95: the bubble program tests its points in q
            std::memcpy(ranges + map_groups * 2, box4, sizeof box4);
            step(WeaponEffectsStep::Patches,
                 call<SetConstantsFn>(SetPixelShaderConstantF)(d, ranges_at, ranges, map_groups / 2 + 2));
            if (SUCCEEDED(hr)) {
                const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                    d, D3DPT_TRIANGLELIST, 0, patch_vertices, patch_triangles, indices_,
                    D3DFMT_INDEX16, vertices_, UINT(sizeof vertices_[0])));
                if (step(WeaponEffectsStep::Patches, draw)) {
                    ++r.patches;
                    r.drew = true;
                }
            }
        }
    }
    if (SUCCEEDED(hr) && f.decal_count) {
        const float limits[4] = {f.near_z, 0.f, 0.f, 0.f};
        step(WeaponEffectsStep::Decals, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Decals, call<SetDeclarationFn>(SetVertexDeclaration)(d, decal_declaration_));
        step(WeaponEffectsStep::Decals, call<SetVsFn>(SetVertexShader)(d, decal_vs_));
        step(WeaponEffectsStep::Decals, call<SetPsFn>(SetPixelShader)(d, decal_ps_));
        step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, limits, 1));
        static constexpr float corners[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {-1.f, 1.f}, {1.f, 1.f}};
        static constexpr float origin[1][2] = {{0.f, 0.f}};
        static constexpr float frame_a[3] = {0.f, 0.f, 1.f}, frame_u[3] = {1.f, 0.f, 0.f}, frame_v[3] = {0.f, 1.f, 0.f};
        for (unsigned i = 0; i < f.decal_count && SUCCEEDED(hr); ++i) {
            const WeaponDecal& c = f.decals[i];
            float ps[hex_head * 4];
            bool ok = finite(c.radius) && c.radius > 0.f && finite(c.age_s) && finite(c.soft) && c.soft > 0.f &&
                      finite_all(c.centre, 3) &&
                      hex_block(l, c.cell, c.radius, c.reach, float(12), 0.f, c.colour, frame_a, frame_u, frame_v, 1,
                                origin, origin, &c.age_s, ps);
            if (!ok) continue;
            // The quad's half size: the footprint's outermost cell centres plus a cell's circumradius (1 / sqrt 3).
            const float half = c.reach + 0.5773503f * c.cell;
            ps[52 + 2] = half; // c15.z: the decal program's corner scale
            DecalVertex v[4];
            for (unsigned k = 0; k < 4; ++k)
                v[k] = DecalVertex{{c.centre[0], c.centre[1], c.centre[2], half},
                                   {corners[k][0], corners[k][1], c.soft, 1.f}};
            step(WeaponEffectsStep::Decals, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, ps, hex_head));
            if (SUCCEEDED(hr)) {
                const HRESULT draw =
                    draw_fault(call<DrawUpFn>(DrawPrimitiveUP)(d, D3DPT_TRIANGLESTRIP, 2, v, UINT(sizeof(DecalVertex))));
                if (step(WeaponEffectsStep::Decals, draw)) {
                    ++r.decals;
                    r.drew = true;
                }
            }
        }
    }
    // The sprite batch last (flashes and sparks): one draw, both triangle windings (no cull).
    if (SUCCEEDED(hr) && sprite_quads) {
        const float sprite_ps[4] = {wcore::look::soft_core, wcore::look::soft_halo, wcore::look::halo_max,
                                    wcore::look::halo_fold};
        // The streak expansion's constants (weapon_sprite_vs.hlsl c2, c3; c0, c1 the projection and viewport above).
        const float sprite_vs[8] = {f.near_z, wcore::look::floor_lit_px, wcore::hull::spark_rest_w, wcore::look::floor_len_px,
                                    wcore::look::streak_cap, 0.f, 0.f, 0.f};
        step(WeaponEffectsStep::Sprites, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Sprites, call<SetDeclarationFn>(SetVertexDeclaration)(d, sprite_declaration_));
        step(WeaponEffectsStep::Sprites, call<SetVsFn>(SetVertexShader)(d, sprite_vs_));
        step(WeaponEffectsStep::Sprites, call<SetPsFn>(SetPixelShader)(d, sprite_ps_));
        step(WeaponEffectsStep::Sprites, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, sprite_ps, 1));
        step(WeaponEffectsStep::Sprites, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, sprite_vs, 2));
        if (SUCCEEDED(hr)) {
            const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                d, D3DPT_TRIANGLELIST, 0, sprite_quads * 4, sprite_quads * 2, sprite_indices_, D3DFMT_INDEX16,
                f.sprites, UINT(sizeof(WeaponSpriteVertex))));
            if (step(WeaponEffectsStep::Sprites, draw)) {
                r.sprite_quads = sprite_quads;
                r.drew = true;
            }
        }
    }
    if (FAILED(hr) && lost(hr)) reset_pending_ = true;
    return finish(hr);
}
} // namespace x3m::renderer
