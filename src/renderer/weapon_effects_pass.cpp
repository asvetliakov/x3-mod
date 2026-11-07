#include "weapon_effects_pass.h"
#include "ps3_program_slots.h"
#include "../proxy/cpu_state.h"
#include "../proxy/sse_scalar.h"
#include "../proxy/weapon_effects_core.h"
#include <cstddef>
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
    GetDirect3D = 6, GetCreationParameters = 9, CreateVertexBuffer = 26, CreateIndexBuffer = 27, GetRenderState = 58,
    DrawIndexedPrimitive = 82, GetVertexDeclaration = 88, GetVertexShader = 93, SetStreamSource = 100,
    GetStreamSource = 101, GetStreamSourceFreq = 103, SetIndices = 104, GetIndices = 105, GetPixelShader = 108,
    GetPixelShaderConstantF = 110, SetRenderState = 57, SetTexture = 65, SetSamplerState = 69,
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
using GetConstantsFn = HRESULT(WINAPI*)(D, UINT, float*, UINT);
using GetRsFn = HRESULT(WINAPI*)(D, D3DRENDERSTATETYPE, DWORD*);
using CreateVbFn = HRESULT(WINAPI*)(D, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*);
using CreateIbFn = HRESULT(WINAPI*)(D, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9**, HANDLE*);
using DrawIndexedFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using GetDeclarationFn = HRESULT(WINAPI*)(D, IDirect3DVertexDeclaration9**);
using GetVsFn = HRESULT(WINAPI*)(D, IDirect3DVertexShader9**);
using GetPsFn = HRESULT(WINAPI*)(D, IDirect3DPixelShader9**);
using SetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
using GetStreamFn = HRESULT(WINAPI*)(D, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
using GetStreamFreqFn = HRESULT(WINAPI*)(D, UINT, UINT*);
using SetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9*);
using GetIndicesFn = HRESULT(WINAPI*)(D, IDirect3DIndexBuffer9**);
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
// The fireball impostor (provenance: verification/results/weapon-fireball-{vertex,pixel}-program.json).
constexpr std::uint32_t fireball_vs_words[] = {
#include "weapon_fireball_vertex_program_inc.h"
};
constexpr std::uint32_t fireball_ps_words[] = {
#include "weapon_fireball_pixel_program_inc.h"
};
constexpr D3DVERTEXELEMENT9 fireball_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 28, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    {0, 44, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
    {0, 60, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3},
    {0, 76, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4}, D3DDECL_END()};
static_assert(sizeof(WeaponFireballVertex) == 80, "the fireball declaration follows the vertex");
// The hull classes' shaped glows (provenance: verification/results/weapon-{shape-vertex,ring-pixel,filament-pixel}-
// program.json).
constexpr std::uint32_t shape_vs_words[] = {
#include "weapon_shape_vertex_program_inc.h"
};
constexpr std::uint32_t ring_ps_words[] = {
#include "weapon_ring_pixel_program_inc.h"
};
constexpr std::uint32_t filament_ps_words[] = {
#include "weapon_filament_pixel_program_inc.h"
};
// The ion's lightning segments (provenance: verification/results/weapon-lightning-vertex-program.json; the pixel
// program is the filament's).
constexpr std::uint32_t lightning_vs_words[] = {
#include "weapon_lightning_vertex_program_inc.h"
};
constexpr D3DVERTEXELEMENT9 lightning_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    {0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2}, D3DDECL_END()};
static_assert(sizeof(WeaponLightningVertex) == 64, "the lightning declaration follows the vertex");
constexpr D3DVERTEXELEMENT9 shape_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 28, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    {0, 40, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
    {0, 56, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3},
    {0, 68, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4},
    {0, 80, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5}, D3DDECL_END()};
static_assert(sizeof(WeaponShapeVertex) == 92, "the shape declaration follows the vertex");
constexpr D3DVERTEXELEMENT9 sprite_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 32, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    {0, 44, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0}, D3DDECL_END()};
static_assert(sizeof(WeaponSpriteVertex) == 48, "the sprite declaration follows the vertex");
constexpr D3DVERTEXELEMENT9 glow_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 12, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 28, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    {0, 40, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2}, D3DDECL_END()};
static_assert(sizeof(WeaponGlowVertex) == 48, "the glow declaration follows the vertex");
template <std::size_t N> std::uint32_t vs3_slots(const std::uint32_t (&words)[N]) noexcept {
    std::uint32_t copy[N];
    std::memcpy(copy, words, sizeof copy);
    if (copy[0] != 0xfffe0300u) return 0;
    copy[0] = ps3_version_token;
    return ps3_program_slots(copy, N);
}
// Step 4, the bolt capsule (provenance: verification/results/weapon-bolt-{vertex,pixel}-program.json and
// weapon-bolt_flag-pixel-program.json, the far flag's MRT variant of the same source).
constexpr std::uint32_t bolt_vs_words[] = {
#include "weapon_bolt_vertex_program_inc.h"
};
constexpr std::uint32_t bolt_ps_words[] = {
#include "weapon_bolt_pixel_program_inc.h"
};
constexpr std::uint32_t bolt_flag_ps_words[] = {
#include "weapon_bolt_flag_pixel_program_inc.h"
};
// Step 3: the death shock (provenance: verification/results/weapon-shock-pixel-program.json).
constexpr std::uint32_t shock_ps_words[] = {
#include "weapon_shock_pixel_program_inc.h"
};
// Step 3: the death body's halo (provenance: verification/results/weapon-halo-pixel-program.json).
constexpr std::uint32_t halo_ps_words[] = {
#include "weapon_halo_pixel_program_inc.h"
};
// Section 13.1: the death fire wave (provenance: verification/results/weapon-wave-pixel-program.json).
constexpr std::uint32_t wave_ps_words[] = {
#include "weapon_wave_pixel_program_inc.h"
};
// Section 13.2: the composed death body (provenance: verification/results/weapon-compose-pixel-program.json).
constexpr std::uint32_t compose_ps_words[] = {
#include "weapon_compose_pixel_program_inc.h"
};
static_assert(sizeof(WeaponComposeConstants) == (1 + 4 * weapon_compose_spheres) * 16, "c14 .. c42 of weapon_compose_ps.hlsl");
static_assert(offsetof(WeaponWaveConstants, spare) == 2 * 16 && offsetof(WeaponWaveConstants, quads) == 3 * 16,
              "c13 .. c15 of weapon_wave_ps.hlsl, then the quad count");
// Every drawn wave's quad count in 1 .. weapon_wave_quads (its vertices 4 x weapon_wave_quads a wave).
static bool wave_quads_valid(const WeaponEffectsFrame& f) noexcept {
    for (unsigned k = 0; f.wave_constants && k < f.wave_boxes && k < weapon_wave_boxes; ++k)
        if (f.wave_constants[k].quads < 1 || f.wave_constants[k].quads > weapon_wave_quads) return false;
    return true;
}
static_assert(wcore::look::fireball_column_pow == 0.25f, "weapon_compose_ps.hlsl takes column^0.25 as two square roots");
constexpr D3DVERTEXELEMENT9 bolt_elements[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    {0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
    {0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
    {0, 64, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3},
    {0, 80, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 4},
    {0, 96, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 5},
    {0, 112, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 6}, D3DDECL_END()};
static_assert(sizeof(WeaponBoltVertex) == 128, "the bolt declaration follows the vertex");
constexpr UINT bolt_vb_bytes = UINT(weapon_bolt_quads * 4 * sizeof(WeaponBoltVertex));
constexpr UINT bolt_ib_bytes = UINT(weapon_bolt_quads * 6 * sizeof(std::uint16_t));
static_assert(weapon_bolt_quads * 4 <= 0x10000, "16-bit bolt indices");
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
    drop(fireball_vs_);
    drop(fireball_ps_);
    drop(fireball_declaration_);
    drop(shape_vs_);
    drop(ring_ps_);
    drop(filament_ps_);
    drop(shape_declaration_);
    drop(lightning_vs_);
    drop(lightning_declaration_);
    drop(shock_ps_);
    drop(halo_ps_);
    drop(wave_ps_);
    drop(compose_ps_);
}
void WeaponEffectsPass::release_bolt_objects() noexcept {
    drop(bolt_vs_);
    drop(bolt_ps_);
    drop(bolt_flag_ps_);
    drop(bolt_declaration_);
    drop(bolt_vb_);
    drop(bolt_ib_);
}
void WeaponEffectsPass::release_objects() noexcept {
    release_bolt_objects();
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
    return sprite_vs_ && sprite_ps_ && glow_vs_ && glow_ps_ && sprite_declaration_ && glow_declaration_ && fireball_vs_ &&
           fireball_ps_ && fireball_declaration_ && shape_vs_ && ring_ps_ && filament_ps_ && shape_declaration_ &&
           lightning_vs_ && lightning_declaration_;
}
bool WeaponEffectsPass::bolt_objects_ready() const noexcept {
    return bolt_vs_ && bolt_ps_ && bolt_flag_ps_ && bolt_declaration_ && bolt_vb_ && bolt_ib_;
}
bool WeaponEffectsPass::resources_ready() const noexcept {
    return device_ && caps_.enabled && !reset_pending_ && shell_vs_ && shell_ps_ && bubble_ps_ && decal_vs_ && decal_ps_ &&
           shell_declaration_ && decal_declaration_ && (!caps_.hull_enabled || hull_objects_ready()) &&
           (!caps_.bolt_enabled || bolt_objects_ready()) && (!caps_.boom_enabled || halo_ps_) &&
           (!caps_.shock_enabled || shock_ps_) && (!caps_.wave_enabled || wave_ps_) && (!caps_.compose_enabled || compose_ps_);
}
unsigned WeaponEffectsPass::references() const noexcept {
    unsigned n = 0;
    for (const void* p : {static_cast<const void*>(shell_vs_), static_cast<const void*>(shell_ps_),
                          static_cast<const void*>(bubble_ps_), static_cast<const void*>(decal_vs_), static_cast<const void*>(decal_ps_),
                          static_cast<const void*>(shell_declaration_), static_cast<const void*>(decal_declaration_),
                          static_cast<const void*>(sprite_vs_), static_cast<const void*>(sprite_ps_),
                          static_cast<const void*>(glow_vs_), static_cast<const void*>(glow_ps_),
                          static_cast<const void*>(sprite_declaration_), static_cast<const void*>(glow_declaration_),
                          static_cast<const void*>(fireball_vs_), static_cast<const void*>(fireball_ps_),
                          static_cast<const void*>(fireball_declaration_), static_cast<const void*>(shape_vs_),
                          static_cast<const void*>(ring_ps_), static_cast<const void*>(filament_ps_),
                          static_cast<const void*>(shape_declaration_), static_cast<const void*>(lightning_vs_),
                          static_cast<const void*>(lightning_declaration_), static_cast<const void*>(bolt_vs_),
                          static_cast<const void*>(bolt_ps_), static_cast<const void*>(bolt_flag_ps_),
                          static_cast<const void*>(bolt_declaration_), static_cast<const void*>(bolt_vb_),
                          static_cast<const void*>(bolt_ib_), static_cast<const void*>(shock_ps_),
                          static_cast<const void*>(halo_ps_), static_cast<const void*>(wave_ps_),
                          static_cast<const void*>(compose_ps_)})
        n += p != nullptr;
    return n;
}
HRESULT WeaponEffectsPass::attach(D d, void* const* native, const D3DCAPS9& caps, D3DFORMAT format, bool hull,
                                  bool bolts, bool booms) noexcept {
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
    // The lightning's lane normal (weapon_lightning_vs.hlsl): vertex texture fetch of the lane's two formats, point
    // filtered (documented D3D9: CheckDeviceFormat with D3DUSAGE_QUERY_VERTEXTEXTURE, VertexTextureFilterCaps).
    caps_.lightning_vtf_four = caps_.lightning_vtf_r32f = false;
    if (SUCCEEDED(hr) && (caps.VertexTextureFilterCaps & D3DPTFILTERCAPS_MINFPOINT) &&
        (caps.VertexTextureFilterCaps & D3DPTFILTERCAPS_MAGFPOINT)) {
        caps_.lightning_vtf_four = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, format,
                                                          D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE,
                                                          D3DFMT_A32B32G32R32F) == D3D_OK;
        caps_.lightning_vtf_r32f = api->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, format,
                                                          D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE,
                                                          D3DFMT_R32F) == D3D_OK;
    }
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
        caps_.fireball_vs_slots = vs3_slots(fireball_vs_words);
        caps_.fireball_ps_slots = ps3_program_slots(fireball_ps_words, std::size(fireball_ps_words));
        caps_.shape_vs_slots = vs3_slots(shape_vs_words);
        caps_.ring_ps_slots = ps3_program_slots(ring_ps_words, std::size(ring_ps_words));
        caps_.filament_ps_slots = ps3_program_slots(filament_ps_words, std::size(filament_ps_words));
        caps_.lightning_vs_slots = vs3_slots(lightning_vs_words);
        // The scorches' multiply blend (hull-impact-classes.md 3.7): ZERO / INVSRCCOLOR; without it the scorches are
        // not drawn (the glue builds none), everything else is.
        caps_.scorch_enabled = (caps.SrcBlendCaps & D3DPBLENDCAPS_ZERO) && (caps.DestBlendCaps & D3DPBLENDCAPS_INVSRCCOLOR);
        if (!caps_.sprite_vs_slots || !caps_.glow_vs_slots || !caps_.sprite_ps_slots || !caps_.glow_ps_slots ||
            !caps_.fireball_vs_slots || !caps_.fireball_ps_slots || !caps_.shape_vs_slots || !caps_.ring_ps_slots ||
            !caps_.filament_ps_slots || !caps_.lightning_vs_slots)
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
    // Step 3 (the explosions modes, with the hull): the shock's program beside the hull's; a refusal keeps every death
    // native (boom_enabled false, boom_reason) and the hull drawing. Its slots are logged; none is refused for its count.
    caps_.boom_reason = "not_requested";
    if (booms) {
        caps_.shock_ps_slots = ps3_program_slots(shock_ps_words, std::size(shock_ps_words));
        caps_.halo_ps_slots = ps3_program_slots(halo_ps_words, std::size(halo_ps_words));
        caps_.wave_ps_slots = ps3_program_slots(wave_ps_words, std::size(wave_ps_words));
        caps_.compose_ps_slots = ps3_program_slots(compose_ps_words, std::size(compose_ps_words));
        if (!caps_.hull_enabled)
            caps_.boom_reason = "boom_needs_hull";
        else if (!caps_.halo_ps_slots)
            caps_.boom_reason = "boom_compiled_program";
        else if (FAILED(create_boom_objects()))
            caps_.boom_reason = "boom_program_create";
        else {
            caps_.boom_enabled = true;
            caps_.boom_reason = "ok";
        }
        // the shock: optional (a refusal keeps the deaths and drops only the shells)
        caps_.shock_reason = !caps_.boom_enabled ? caps_.boom_reason
                             : !caps_.shock_ps_slots ? "shock_compiled_program"
                             : shock_ps_ ? "ok"
                                         : "shock_program_create";
        caps_.shock_enabled = caps_.boom_enabled && shock_ps_ != nullptr;
        // the fire wave: optional likewise (a refusal keeps the deaths and drops only the waves)
        caps_.wave_reason = !caps_.boom_enabled ? caps_.boom_reason
                            : !caps_.wave_ps_slots ? "wave_compiled_program"
                            : wave_ps_ ? "ok"
                                       : "wave_program_create";
        caps_.wave_enabled = caps_.boom_enabled && wave_ps_ != nullptr;
        caps_.compose_reason = !caps_.boom_enabled ? caps_.boom_reason
                               : !wcore::boom::compose_enabled ? "off"
                               : !caps_.compose_ps_slots ? "compose_compiled_program"
                               : compose_ps_ ? "ok"
                                             : "compose_program_create";
        caps_.compose_enabled = caps_.boom_enabled && compose_ps_ != nullptr;
    }
    // Step 4 (shields,hull,bolts only): the capsule programs, the MRT variant (independent write masks: the far flag's RT2
    // GREEN lane while RT1 stays masked), the quads' index limits, the multiply blend of the rim pass and the buffers. A
    // refusal leaves the game's bullets (bolt_enabled false, bolt_reason); the hull and shields keep drawing. The program
    // slots are logged (bolt_vs_slots / bolt_ps_slots); none is refused for its count.
    caps_.bolt_reason = "not_requested";
    if (bolts) {
        caps_.bolt_vs_slots = vs3_slots(bolt_vs_words);
        caps_.bolt_ps_slots = ps3_program_slots(bolt_ps_words, std::size(bolt_ps_words));
        caps_.bolt_flag_ps_slots = ps3_program_slots(bolt_flag_ps_words, std::size(bolt_flag_ps_words));
        if (!caps_.bolt_vs_slots || !caps_.bolt_ps_slots || !caps_.bolt_flag_ps_slots)
            caps_.bolt_reason = "bolt_compiled_program";
        else if (caps.MaxPrimitiveCount < 2u * weapon_bolt_quads || caps.MaxVertexIndex < 4u * weapon_bolt_quads - 1u ||
                 caps.NumSimultaneousRTs < 3u)
            caps_.bolt_reason = "bolt_limits";
        else if (!(caps.SrcBlendCaps & D3DPBLENDCAPS_ZERO) || !(caps.DestBlendCaps & D3DPBLENDCAPS_INVSRCCOLOR))
            caps_.bolt_reason = "bolt_blend_caps";
        else if (FAILED(create_bolt_objects()))
            caps_.bolt_reason = "bolt_program_create";
        else {
            caps_.bolt_enabled = true;
            caps_.bolt_reason = "ok";
            caps_.bolt_rim_undo = (caps.SrcBlendCaps & D3DPBLENDCAPS_DESTCOLOR) != 0;
        }
    }
    return S_OK;
}
HRESULT WeaponEffectsPass::create_bolt_objects() noexcept {
    D d = device_;
    HRESULT hr = (faults_ & 32u) ? E_FAIL : S_OK; // fixture fault bit 5: the bolt programs refused
    if (SUCCEEDED(hr) && !bolt_vs_)
        hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(bolt_vs_words), &bolt_vs_);
    if (SUCCEEDED(hr) && !bolt_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(bolt_ps_words), &bolt_ps_);
    if (SUCCEEDED(hr) && !bolt_flag_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(bolt_flag_ps_words), &bolt_flag_ps_);
    if (SUCCEEDED(hr) && !bolt_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, bolt_elements, &bolt_declaration_);
    if (SUCCEEDED(hr) && !bolt_vb_)
        hr = call<CreateVbFn>(CreateVertexBuffer)(d, bolt_vb_bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                                  D3DPOOL_DEFAULT, &bolt_vb_, nullptr);
    if (SUCCEEDED(hr) && !bolt_ib_) {
        hr = call<CreateIbFn>(CreateIndexBuffer)(d, bolt_ib_bytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT,
                                                 &bolt_ib_, nullptr);
        void* bits = nullptr;
        if (SUCCEEDED(hr)) hr = bolt_ib_->Lock(0, 0, &bits, 0);
        if (SUCCEEDED(hr)) {
            auto* idx = static_cast<std::uint16_t*>(bits);
            for (unsigned q = 0; q < weapon_bolt_quads; ++q) {
                const std::uint16_t b = std::uint16_t(4 * q);
                const std::uint16_t six[6] = {b, std::uint16_t(b + 1), std::uint16_t(b + 2), std::uint16_t(b + 2),
                                              std::uint16_t(b + 1), std::uint16_t(b + 3)};
                std::memcpy(idx + 6 * q, six, sizeof six);
            }
            hr = bolt_ib_->Unlock();
        }
    }
    if (SUCCEEDED(hr) && !bolt_objects_ready()) hr = E_FAIL;
    if (FAILED(hr)) release_bolt_objects();
    return hr;
}
HRESULT WeaponEffectsPass::create_boom_objects() noexcept {
    D d = device_;
    HRESULT hr = (faults_ & 256u) ? E_FAIL : S_OK; // fixture fault bit 8: the death programs refused
    if (SUCCEEDED(hr) && !halo_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(halo_ps_words), &halo_ps_);
    if (FAILED(hr)) {
        drop(shock_ps_);
        drop(halo_ps_);
        drop(wave_ps_);
        drop(compose_ps_);
        return hr;
    }
    // the shock is optional: a refusal (or fixture fault bit 9) leaves it null and the set enabled
    if (!shock_ps_ && !(faults_ & 512u) && caps_.shock_ps_slots &&
        FAILED(call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(shock_ps_words), &shock_ps_)))
        drop(shock_ps_);
    // the fire wave likewise (fixture fault bit 10)
    if (!wave_ps_ && !(faults_ & 1024u) && caps_.wave_ps_slots &&
        FAILED(call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(wave_ps_words), &wave_ps_)))
        drop(wave_ps_);
    // the composed body likewise (fixture fault bit 11)
    // (not created while boom::compose_enabled is false: the kill, explosion-look.md D46; reason "off")
    if (wcore::boom::compose_enabled && !compose_ps_ && !(faults_ & 2048u) && caps_.compose_ps_slots &&
        FAILED(call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(compose_ps_words), &compose_ps_)))
        drop(compose_ps_);
    return hr;
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
    if (SUCCEEDED(hr) && !fireball_vs_)
        hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(fireball_vs_words), &fireball_vs_);
    if (SUCCEEDED(hr) && !fireball_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(fireball_ps_words), &fireball_ps_);
    if (SUCCEEDED(hr) && !fireball_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, fireball_elements, &fireball_declaration_);
    if (SUCCEEDED(hr) && !shape_vs_)
        hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(shape_vs_words), &shape_vs_);
    if (SUCCEEDED(hr) && !ring_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(ring_ps_words), &ring_ps_);
    if (SUCCEEDED(hr) && !filament_ps_)
        hr = call<CreatePsFn>(CreatePixelShader)(d, reinterpret_cast<const DWORD*>(filament_ps_words), &filament_ps_);
    if (SUCCEEDED(hr) && !shape_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, shape_elements, &shape_declaration_);
    if (SUCCEEDED(hr) && !lightning_vs_)
        hr = call<CreateVsFn>(CreateVertexShader)(d, reinterpret_cast<const DWORD*>(lightning_vs_words), &lightning_vs_);
    if (SUCCEEDED(hr) && !lightning_declaration_)
        hr = call<CreateDeclarationFn>(CreateVertexDeclaration)(d, lightning_elements, &lightning_declaration_);
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
    if (FAILED(hr)) return hr;
    if (!caps_.hull_enabled) {
        if (caps_.bolt_enabled && FAILED(create_bolt_objects())) {
            caps_.bolt_enabled = false;
            caps_.bolt_reason = "bolt_program_create";
        }
        return hr;
    }
    // The hull programs after a Reset: a refusal now turns the hull off (the shields keep drawing).
    if (FAILED(create_hull_objects())) {
        caps_.hull_enabled = false;
        caps_.hull_reason = "hull_program_create";
    }
    // The bolt objects and buffers after a Reset: a refusal turns the bolts off (the game's bullets draw).
    if (caps_.bolt_enabled && FAILED(create_bolt_objects())) {
        caps_.bolt_enabled = false;
        caps_.bolt_reason = "bolt_program_create";
    }
    // The death programs after a Reset (with the hull's): a refusal turns the deaths native (Present's boom latch).
    if (caps_.boom_enabled && (!caps_.hull_enabled || FAILED(create_boom_objects()))) {
        caps_.boom_enabled = false;
        caps_.boom_reason = "boom_program_create";
    }
    if (caps_.shock_enabled && (!caps_.boom_enabled || !shock_ps_)) {
        caps_.shock_enabled = false;
        caps_.shock_reason = "shock_program_create";
    }
    if (caps_.wave_enabled && (!caps_.boom_enabled || !wave_ps_)) {
        caps_.wave_enabled = false;
        caps_.wave_reason = "wave_program_create";
    }
    if (caps_.compose_enabled && (!caps_.boom_enabled || !compose_ps_)) {
        caps_.compose_enabled = false;
        caps_.compose_reason = wcore::boom::compose_enabled ? "compose_program_create" : "off";
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
        (f.glow_boxes && !f.glows) || f.sprite_quads > weapon_sprite_quads || f.glow_boxes > weapon_glow_boxes ||
        (f.fireball_boxes && !f.fireballs) || f.fireball_boxes > weapon_fireball_boxes || (f.ring_quads && !f.rings) ||
        f.ring_quads > weapon_ring_quads || ((f.fireball_boxes || f.smoke_boxes) && (f.fireball_octaves < 2u || f.fireball_octaves > 4u)) ||
        (f.smoke_boxes && !f.smokes) || f.smoke_boxes > weapon_smoke_boxes ||
        (f.filament_boxes && !f.filaments) || f.filament_boxes > weapon_filament_boxes ||
        (f.scorch_boxes && !f.scorches) || f.scorch_boxes > weapon_scorch_boxes || (f.shock_boxes && !f.shocks) ||
        f.shock_boxes > weapon_shock_boxes || (f.halo_boxes && !f.halos) || f.halo_boxes > weapon_halo_boxes ||
        (f.wave_boxes && (!f.waves || !f.wave_constants)) || f.wave_boxes > weapon_wave_boxes || !wave_quads_valid(f) ||
        (f.composed_boxes && !f.compose) || f.composed_first + f.composed_boxes > f.fireball_boxes)
        return refuse(WeaponEffectsStep::Validate, E_INVALIDARG);
    // Without the hull programs the sprites, glows, fireballs, rings, filaments and scorches are not drawn (the glue
    // builds none then); without the multiply blend no scorch.
    const unsigned sprite_quads = caps_.hull_enabled ? f.sprite_quads : 0u, glow_boxes = caps_.hull_enabled ? f.glow_boxes : 0u;
    const unsigned fireball_boxes = caps_.hull_enabled ? f.fireball_boxes : 0u, ring_quads = caps_.hull_enabled ? f.ring_quads : 0u;
    const unsigned filament_boxes = caps_.hull_enabled ? f.filament_boxes : 0u;
    const unsigned scorch_boxes = caps_.hull_enabled && caps_.scorch_enabled ? f.scorch_boxes : 0u;
    const unsigned smoke_boxes = caps_.hull_enabled && caps_.scorch_enabled ? f.smoke_boxes : 0u;
    const unsigned shock_boxes = caps_.hull_enabled && caps_.shock_enabled ? f.shock_boxes : 0u;
    const unsigned halo_boxes = caps_.hull_enabled && caps_.boom_enabled ? f.halo_boxes : 0u;
    const unsigned wave_boxes = caps_.hull_enabled && caps_.wave_enabled ? f.wave_boxes : 0u;
    // the composed bodies with their program, else (refused: the stage builds none) drawn in the batch
    const unsigned composed_boxes = caps_.hull_enabled && caps_.compose_enabled ? f.composed_boxes : 0u;
    if (!f.patch_count && !f.decal_count && !sprite_quads && !glow_boxes && !fireball_boxes && !ring_quads &&
        !filament_boxes && !scorch_boxes && !smoke_boxes && !shock_boxes && !halo_boxes && !wave_boxes)
        return finish(S_FALSE);
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
    // The scorches first (hull-impact-classes.md 3.7: the only non-additive elements, before every additive layer): the
    // glow programs under ZERO / INVSRCCOLOR, the scene x (1 - s w scorch_colour), then the bursts' smoke (the fireball
    // programs, hull-burst-look.md section 5) in the same state; then ONE / ONE again.
    const float glow_limits[4] = {f.near_z, 0.f, 0.f, 0.f};
    // weapon_glow_ps.hlsl c2, c3 (the ring program reads the same two): the projection, the screen, an ember's lobes
    const float glow_ps[8] = {f.m00, f.m11, f.m20, f.m21, 2.f / W, 2.f / H, wcore::look::fireball_lobes,
                              wcore::hull::ember_lobe_amp};
    // weapon_fireball_ps.hlsl c2..c13: the projection, the screen and the frame's third-octave weight (0 past the budget's
    // step 1), the noise and erosion law, the ramp.
    namespace lk = wcore::look;
    float fb[12 * 4] = {f.m00, f.m11, f.m20, f.m21, 2.f / W, 2.f / H, f.near_z, float(f.fireball_octaves) - 2.f,
                        lk::fireball_octave_freq[0], lk::fireball_octave_freq[1], lk::fireball_octave_freq[2], lk::fireball_lobes,
                        lk::fireball_octave_w[0], lk::fireball_octave_w[1], lk::fireball_octave_w[2], lk::fireball_rim_t,
                        lk::fireball_erode_0, lk::fireball_erode_t, lk::fireball_erode_r, lk::fireball_erode_soft,
                        lk::fireball_column_pow, lk::fireball_lobe_amp, lk::fireball_lobe_soft, lk::fireball_heat_soft};
    for (unsigned k = 0; k < 5; ++k)
        for (unsigned c = 0; c < 3; ++c) fb[24 + 4 * k + c] = lk::heat_ramp[k][c];
    fb[44] = lk::fireball_erode_rt;
    fb[45] = wcore::boom::compose_soft_add; // c13.y: the composed body's soft add (the fireball program skips it)
    fb[46] = lk::fireball_octave_freq4;      // c13.zw: the fourth octave (both programs)
    fb[47] = lk::fireball_octave_w4;
    if (SUCCEEDED(hr) && (scorch_boxes || smoke_boxes)) {
        step(WeaponEffectsStep::Scorches, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Scorches, call<SetRsFn>(SetRenderState)(d, D3DRS_SRCBLEND, DWORD(D3DBLEND_ZERO)));
        step(WeaponEffectsStep::Scorches, call<SetRsFn>(SetRenderState)(d, D3DRS_DESTBLEND, DWORD(D3DBLEND_INVSRCCOLOR)));
        step(WeaponEffectsStep::Scorches, call<SetDeclarationFn>(SetVertexDeclaration)(d, glow_declaration_));
        step(WeaponEffectsStep::Scorches, call<SetVsFn>(SetVertexShader)(d, glow_vs_));
        step(WeaponEffectsStep::Scorches, call<SetPsFn>(SetPixelShader)(d, glow_ps_));
        step(WeaponEffectsStep::Scorches, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, glow_limits, 1));
        step(WeaponEffectsStep::Scorches, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, glow_ps, 2));
        if (SUCCEEDED(hr) && scorch_boxes) {
            const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                d, D3DPT_TRIANGLELIST, 0, scorch_boxes * 4, scorch_boxes * 2, sprite_indices_, D3DFMT_INDEX16, f.scorches,
                UINT(sizeof(WeaponGlowVertex))));
            if (step(WeaponEffectsStep::Scorches, draw)) {
                r.scorch_boxes = scorch_boxes;
                r.drew = true;
            }
        }
        if (SUCCEEDED(hr) && smoke_boxes) {
            step(WeaponEffectsStep::Smokes, call<SetDeclarationFn>(SetVertexDeclaration)(d, fireball_declaration_));
            step(WeaponEffectsStep::Smokes, call<SetVsFn>(SetVertexShader)(d, fireball_vs_));
            step(WeaponEffectsStep::Smokes, call<SetPsFn>(SetPixelShader)(d, fireball_ps_));
            step(WeaponEffectsStep::Smokes, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, fb, 12));
            if (SUCCEEDED(hr)) {
                const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                    d, D3DPT_TRIANGLELIST, 0, smoke_boxes * 4, smoke_boxes * 2, sprite_indices_, D3DFMT_INDEX16, f.smokes,
                    UINT(sizeof(WeaponFireballVertex))));
                if (step(WeaponEffectsStep::Smokes, draw)) {
                    r.smoke_boxes = smoke_boxes;
                    r.drew = true;
                }
            }
        }
        step(WeaponEffectsStep::Scorches, call<SetRsFn>(SetRenderState)(d, D3DRS_SRCBLEND, DWORD(D3DBLEND_ONE)));
        step(WeaponEffectsStep::Scorches, call<SetRsFn>(SetRenderState)(d, D3DRS_DESTBLEND, DWORD(D3DBLEND_ONE)));
    }
    // The glows and embers (look 2.10's order): one batched draw of their screen rectangles (no cull).
    if (SUCCEEDED(hr) && glow_boxes) {
        const float* limits = glow_limits;
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
    // The surface rings and the filament glows (the glow primitive with a profile, one vertex program; no cull).
    if (SUCCEEDED(hr) && (ring_quads || filament_boxes)) {
        step(WeaponEffectsStep::Rings, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Rings, call<SetDeclarationFn>(SetVertexDeclaration)(d, shape_declaration_));
        step(WeaponEffectsStep::Rings, call<SetVsFn>(SetVertexShader)(d, shape_vs_));
        step(WeaponEffectsStep::Rings, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, glow_limits, 1));
        step(WeaponEffectsStep::Rings, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, glow_ps, 2));
        if (SUCCEEDED(hr) && ring_quads) {
            // The shock front's terms (weapon_ring_ps.hlsl c4, c5; weapon_effects_core.h hull::ring_*): the trailing
            // band's colour over the leading edge's x its gain, 1 / the band's span; the noise, the span; c6 the seed's
            // axis and the noise's window, c7 the front's window.
            namespace wh = wcore::hull;
            float lead[3], tail[3];
            wcore::heat_colour(wh::ring_lead_t, lead);
            wcore::heat_colour(wh::ring_trail_t, tail);
            float ring_front[16] = {};
            for (unsigned c = 0; c < 3; ++c) ring_front[c] = lead[c] > 1e-6f ? wh::ring_trail_gain * tail[c] / lead[c] : 0.f;
            ring_front[3] = 1.f / wh::ring_trail_span;
            ring_front[4] = wh::ring_noise_freq;
            ring_front[5] = wh::ring_noise_contrast;
            ring_front[6] = wh::ring_noise_depth;
            ring_front[7] = wh::ring_trail_span;
            for (unsigned c = 0; c < 3; ++c) ring_front[8 + c] = wh::ring_seed_axis[c];
            ring_front[11] = wh::ring_noise_window;
            ring_front[12] = wh::ring_front_window;
            step(WeaponEffectsStep::Rings, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 4, ring_front, 4));
            step(WeaponEffectsStep::Rings, call<SetPsFn>(SetPixelShader)(d, ring_ps_));
            if (SUCCEEDED(hr)) {
                const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                    d, D3DPT_TRIANGLELIST, 0, ring_quads * 4, ring_quads * 2, sprite_indices_, D3DFMT_INDEX16, f.rings,
                    UINT(sizeof(WeaponShapeVertex))));
                if (step(WeaponEffectsStep::Rings, draw)) {
                    r.ring_quads = ring_quads;
                    r.drew = true;
                }
            }
        }
        if (SUCCEEDED(hr) && filament_boxes) {
            // The lightning (weapon_lightning_vs.hlsl c1 the viewport, c2 the near plane; weapon_filament_ps.hlsl c4 the
            // core's colour, c5 the corona's, c6.x the corona's half width over the core's; weapon_effects_core.h
            // hull::ion_*): the core white mixed ion_core_white towards ion_colour, the corona ion_colour x its share.
            // The vertex program (c0 the projection, c1 the viewport, c2 the near plane, the corona's half width over the
            // core's and the lane fetch's switch, c3 the lane's form, c4 its sizes; s0 = D3DVERTEXTEXTURESAMPLER0 the
            // lane, point sampled) turns each hit's plane onto the lane's normal at the contact pixel; without vertex
            // texture fetch of the lane's format (or fixture fault bit 5) the stage's plane.
            namespace wh = wcore::hull;
            float ion[12] = {};
            for (unsigned c = 0; c < 3; ++c) {
                ion[c] = wh::ion_colour[c] + (1.f - wh::ion_colour[c]) * wh::ion_core_white;
                ion[4 + c] = wh::ion_colour[c] * wh::ion_corona_share;
            }
            ion[8] = wh::ion_corona_x;
            const bool vtf = !(faults_ & 32u) && (f.four_channel ? caps_.lightning_vtf_four : caps_.lightning_vtf_r32f);
            const float ion_vs[12] = {f.near_z, wh::ion_corona_x, vtf ? 1.f : 0.f, 0.f, lane[4], lane[5], lane[6], lane[7],
                                      lane[0], lane[1], lane[2], lane[3]};
            step(WeaponEffectsStep::Filaments, call<SetDeclarationFn>(SetVertexDeclaration)(d, lightning_declaration_));
            step(WeaponEffectsStep::Filaments, call<SetVsFn>(SetVertexShader)(d, lightning_vs_));
            step(WeaponEffectsStep::Filaments, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, ion_vs, 3));
            step(WeaponEffectsStep::Filaments, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 4, ion, 3));
            step(WeaponEffectsStep::Filaments, call<SetPsFn>(SetPixelShader)(d, filament_ps_));
            if (vtf) {
                step(WeaponEffectsStep::Filaments, call<SetTextureFn>(SetTexture)(d, D3DVERTEXTEXTURESAMPLER0, f.lane));
                for (auto s : {std::pair{D3DSAMP_MINFILTER, DWORD(D3DTEXF_POINT)}, std::pair{D3DSAMP_MAGFILTER, DWORD(D3DTEXF_POINT)},
                               std::pair{D3DSAMP_MIPFILTER, DWORD(D3DTEXF_NONE)}, std::pair{D3DSAMP_ADDRESSU, DWORD(D3DTADDRESS_CLAMP)},
                               std::pair{D3DSAMP_ADDRESSV, DWORD(D3DTADDRESS_CLAMP)}})
                    step(WeaponEffectsStep::Filaments,
                         call<SetSamplerFn>(SetSamplerState)(d, D3DVERTEXTEXTURESAMPLER0, s.first, s.second));
            }
            if (SUCCEEDED(hr)) {
                const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                    d, D3DPT_TRIANGLELIST, 0, filament_boxes * 4, filament_boxes * 2, sprite_indices_, D3DFMT_INDEX16,
                    f.filaments, UINT(sizeof(WeaponLightningVertex))));
                if (step(WeaponEffectsStep::Filaments, draw)) {
                    r.filament_boxes = filament_boxes;
                    r.drew = true;
                }
            }
            // the lane leaves the vertex sampler (it is a render target again later in the frame)
            if (vtf) step(WeaponEffectsStep::Filaments, call<SetTextureFn>(SetTexture)(d, D3DVERTEXTEXTURESAMPLER0, nullptr));
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
    // The fireballs (the hull hits' bursts; look 2.10: after the patches, before the sprite batch): one batched draw of
    // their screen rectangles (no cull); weapon_fireball_ps.hlsl c2..c13 (fb above).
    // Section 13.2: the composed death bodies first, one draw each with its table (weapon_compose_ps.hlsl c2..c13 of fb,
    // c14..c42 its WeaponComposeConstants), then the rest of the fireballs batched.
    if (SUCCEEDED(hr) && composed_boxes) {
        const float limits[4] = {f.near_z, 0.f, 0.f, 0.f};
        step(WeaponEffectsStep::Composed, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Composed, call<SetDeclarationFn>(SetVertexDeclaration)(d, fireball_declaration_));
        step(WeaponEffectsStep::Composed, call<SetVsFn>(SetVertexShader)(d, fireball_vs_));
        step(WeaponEffectsStep::Composed, call<SetPsFn>(SetPixelShader)(d, compose_ps_));
        step(WeaponEffectsStep::Composed, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, limits, 1));
        step(WeaponEffectsStep::Composed, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, fb, 12));
        for (unsigned k = 0; k < composed_boxes && SUCCEEDED(hr); ++k) {
            step(WeaponEffectsStep::Composed, call<SetConstantsFn>(SetPixelShaderConstantF)(
                                                  d, 14, f.compose[k].head, 1 + 4 * weapon_compose_spheres));
            if (SUCCEEDED(hr)) {
                const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                    d, D3DPT_TRIANGLELIST, 0, 4, 2, sprite_indices_, D3DFMT_INDEX16, f.fireballs + 4 * (f.composed_first + k),
                    UINT(sizeof(WeaponFireballVertex))));
                if (step(WeaponEffectsStep::Composed, draw)) {
                    r.fireball_boxes = fireball_boxes;
                    r.drew = true;
                }
            }
        }
    }
    if (SUCCEEDED(hr) && fireball_boxes > composed_boxes) {
        const float limits[4] = {f.near_z, 0.f, 0.f, 0.f};
        step(WeaponEffectsStep::Fireballs, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Fireballs, call<SetDeclarationFn>(SetVertexDeclaration)(d, fireball_declaration_));
        step(WeaponEffectsStep::Fireballs, call<SetVsFn>(SetVertexShader)(d, fireball_vs_));
        step(WeaponEffectsStep::Fireballs, call<SetPsFn>(SetPixelShader)(d, fireball_ps_));
        step(WeaponEffectsStep::Fireballs, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, limits, 1));
        step(WeaponEffectsStep::Fireballs, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, fb, 12));
        // the boxes before the composed range (the muzzles') and after it (the secondaries, the hull bursts)
        const unsigned first = composed_boxes ? f.composed_first : 0u, after = first + composed_boxes;
        const unsigned ranges[2][2] = {{0u, first}, {after, fireball_boxes - after}};
        for (const auto& range : ranges) {
            if (!range[1] || FAILED(hr)) continue;
            const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                d, D3DPT_TRIANGLELIST, 0, range[1] * 4, range[1] * 2, sprite_indices_, D3DFMT_INDEX16,
                f.fireballs + 4 * range[0], UINT(sizeof(WeaponFireballVertex))));
            if (step(WeaponEffectsStep::Fireballs, draw)) {
                r.fireball_boxes = fireball_boxes;
                r.drew = true;
            }
        }
    }
    // Step 3: the death shocks (explosion-look.md 2.3; after the fireballs, additive): one batched draw of their screen
    // rectangles with the fireball's vertex program and declaration and weapon_shock_ps.hlsl (c2, c3 of fb above).
    if (SUCCEEDED(hr) && shock_boxes) {
        const float limits[4] = {f.near_z, 0.f, 0.f, 0.f};
        step(WeaponEffectsStep::Shocks, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Shocks, call<SetDeclarationFn>(SetVertexDeclaration)(d, fireball_declaration_));
        step(WeaponEffectsStep::Shocks, call<SetVsFn>(SetVertexShader)(d, fireball_vs_));
        step(WeaponEffectsStep::Shocks, call<SetPsFn>(SetPixelShader)(d, shock_ps_));
        step(WeaponEffectsStep::Shocks, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, limits, 1));
        step(WeaponEffectsStep::Shocks, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, fb, 2));
        if (SUCCEEDED(hr)) {
            const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                d, D3DPT_TRIANGLELIST, 0, shock_boxes * 4, shock_boxes * 2, sprite_indices_, D3DFMT_INDEX16, f.shocks,
                UINT(sizeof(WeaponFireballVertex))));
            if (step(WeaponEffectsStep::Shocks, draw)) {
                r.shock_boxes = shock_boxes;
                r.drew = true;
            }
        }
    }
    // Section 13.1: the death fire waves (explosion-look.md 13.1; after the shocks, additive): their quads with the
    // fireball's vertex program and declaration and weapon_wave_ps.hlsl, one draw a wave: c2, c3 of fb above, c4..c12 the
    // front's profile, break, tongues, the material's noise, erosion and floor (boom::wave_*, v7 D52), c13..c15 the wave's
    // normal, age and cooling, c16..c20 the heat ramp, c21 the temperatures, c22 the rim.
    if (SUCCEEDED(hr) && wave_boxes) {
        namespace wb = wcore::boom;
        const float s_end = wb::wave_sheet_enabled ? wb::wave_sheet_span / wb::wave_trail_span : 1.f;
        const float wv[11 * 4] = {fb[0], fb[1], fb[2], fb[3], fb[4], fb[5], fb[6], 0.f,
                                  1.f / wb::wave_trail_span, s_end, wb::wave_light, wb::wave_m_bias,
                                  wb::wave_window_in, wb::wave_window_out, wb::wave_m_floor, wb::wave_m_contrast,
                                  wb::wave_n_cells[0], wb::wave_n_cells[1], wb::wave_n_w[0], wb::wave_n_w[1],
                                  wb::wave_tongue * wb::wave_trail_span, wb::wave_tongue_contrast, wb::wave_lobe_amp,
                                  wb::wave_lobe_soft,
                                  wb::wave_noise_cells[0], wb::wave_noise_cells[1], wb::wave_noise_cells[2], wb::wave_advect,
                                  wb::wave_noise_radial[0], wb::wave_noise_radial[1], wb::wave_noise_radial[2], wb::wave_lead,
                                  wb::wave_noise_w[0], wb::wave_noise_w[1], wb::wave_noise_w[2], wb::wave_break_floor,
                                  wb::wave_erode_0, wb::wave_erode_t, wb::wave_erode_r, wb::wave_erode_soft,
                                  wb::wave_erode_rt, wb::wave_heat_soft, wb::wave_floor_a, wb::wave_floor_t};
        float ramp[7 * 4] = {};
        for (unsigned k = 0; k < 5; ++k)
            for (unsigned c = 0; c < 3; ++c) ramp[4 * k + c] = lk::heat_ramp[k][c];
        const float tail[8] = {wb::wave_t_edge, wb::wave_t_band0, wb::wave_t_band_end, wb::wave_t_sheet_end,
                               wb::wave_ahead, wb::wave_rim_noise, 0.f, 0.f};
        for (unsigned k = 0; k < 8; ++k) ramp[20 + k] = tail[k];
        const float limits[4] = {f.near_z, 0.f, 0.f, 0.f};
        step(WeaponEffectsStep::Waves, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Waves, call<SetDeclarationFn>(SetVertexDeclaration)(d, fireball_declaration_));
        step(WeaponEffectsStep::Waves, call<SetVsFn>(SetVertexShader)(d, fireball_vs_));
        step(WeaponEffectsStep::Waves, call<SetPsFn>(SetPixelShader)(d, wave_ps_));
        step(WeaponEffectsStep::Waves, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, limits, 1));
        step(WeaponEffectsStep::Waves, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, wv, 11));
        step(WeaponEffectsStep::Waves, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 16, ramp, 7));
        for (unsigned k = 0; k < wave_boxes && SUCCEEDED(hr); ++k) {
            step(WeaponEffectsStep::Waves, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 13, f.wave_constants[k].normal, 3));
            if (SUCCEEDED(hr)) {
                const UINT q = f.wave_constants[k].quads;
                const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                    d, D3DPT_TRIANGLELIST, 0, 4 * q, 2 * q, sprite_indices_, D3DFMT_INDEX16,
                    f.waves + 4 * weapon_wave_quads * k, UINT(sizeof(WeaponFireballVertex))));
                if (step(WeaponEffectsStep::Waves, draw)) {
                    r.wave_boxes = k + 1;
                    r.drew = true;
                }
            }
        }
    }
    // Step 3: the death bodies' halos (the glow's vertex program and declaration, weapon_halo_ps.hlsl: the light in air,
    // occlusion class 1; c2, c3 the glow's, c4.x the soft cut's share of R).
    if (SUCCEEDED(hr) && halo_boxes) {
        const float halo_ps[12] = {f.m00, f.m11, f.m20, f.m21, 2.f / W, 2.f / H, 0.f, 0.f, wcore::boom::halo_soft, 0.f,
                                   0.f, 0.f};
        step(WeaponEffectsStep::Halos, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Halos, call<SetDeclarationFn>(SetVertexDeclaration)(d, glow_declaration_));
        step(WeaponEffectsStep::Halos, call<SetVsFn>(SetVertexShader)(d, glow_vs_));
        step(WeaponEffectsStep::Halos, call<SetPsFn>(SetPixelShader)(d, halo_ps_));
        step(WeaponEffectsStep::Halos, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, glow_limits, 1));
        step(WeaponEffectsStep::Halos, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, halo_ps, 3));
        if (SUCCEEDED(hr)) {
            const HRESULT draw = draw_fault(call<DrawIndexedUpFn>(DrawIndexedPrimitiveUP)(
                d, D3DPT_TRIANGLELIST, 0, halo_boxes * 4, halo_boxes * 2, sprite_indices_, D3DFMT_INDEX16, f.halos,
                UINT(sizeof(WeaponGlowVertex))));
            if (step(WeaponEffectsStep::Halos, draw)) {
                r.halo_boxes = halo_boxes;
                r.drew = true;
            }
        }
    }
    // The sprite batch last (flashes, streaks and sparks): one draw, both triangle windings (no cull).
    if (SUCCEEDED(hr) && sprite_quads) {
        // c2 the look, c3 c4 the star's arms and width (hull-burst-look.md section 4)
        const float sprite_ps[12] = {wcore::look::soft_core, wcore::look::soft_halo, wcore::look::halo_max,
                                     wcore::look::halo_fold, wcore::hull::star_arm_len[0], wcore::hull::star_arm_len[1],
                                     wcore::hull::star_arm_len[2], wcore::hull::star_arm_len[3], wcore::hull::star_width,
                                     0.f, 0.f, 0.f};
        // The streak expansion's constants (weapon_sprite_vs.hlsl c2, c3; c0, c1 the projection and viewport above).
        const float sprite_vs[8] = {f.near_z, wcore::look::floor_lit_px, wcore::hull::spark_rest_w, wcore::look::floor_len_px,
                                    wcore::look::streak_cap, 0.f, 0.f, 0.f};
        step(WeaponEffectsStep::Sprites, call<SetRsFn>(SetRenderState)(d, D3DRS_CULLMODE, DWORD(D3DCULL_NONE)));
        step(WeaponEffectsStep::Sprites, call<SetDeclarationFn>(SetVertexDeclaration)(d, sprite_declaration_));
        step(WeaponEffectsStep::Sprites, call<SetVsFn>(SetVertexShader)(d, sprite_vs_));
        step(WeaponEffectsStep::Sprites, call<SetPsFn>(SetPixelShader)(d, sprite_ps_));
        step(WeaponEffectsStep::Sprites, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 2, sprite_ps, 3));
        step(WeaponEffectsStep::Sprites, call<SetConstantsFn>(SetVertexShaderConstantF)(d, 2, sprite_vs, 2));
        if (SUCCEEDED(hr) && sprite_quads) {
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
// Step 4: the in-place bolt draw (weapon_effects_pass.h). The saved state is read before the first setter; a failed read
// refuses the draw before anything is set (the caller forwards the game's own bullets). The restore runs whatever
// happened after the first setter and returns its first failure in report.restored.
namespace {
constexpr D3DRENDERSTATETYPE bolt_states[] = {
    D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_CULLMODE, D3DRS_ALPHATESTENABLE, D3DRS_COLORWRITEENABLE,
    D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2, D3DRS_FOGENABLE, D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE,
    D3DRS_SRGBWRITEENABLE, D3DRS_FILLMODE, D3DRS_CLIPPLANEENABLE};
constexpr unsigned bolt_state_count = sizeof bolt_states / sizeof bolt_states[0];
// The capsule pass's values (the rim pass changes the blend pair and the two extra masks): additive ONE / ONE, depth
// tested LESSEQUAL without writes, both windings, colour into RT0 (the masks of RT1 / RT2 are the caller's).
constexpr DWORD bolt_values[bolt_state_count] = {
    TRUE, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD, FALSE, D3DZB_TRUE, FALSE, D3DCMP_LESSEQUAL, D3DCULL_NONE, FALSE,
    D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA, 0, 0,
    FALSE, FALSE, FALSE, FALSE, D3DFILL_SOLID, 0};
constexpr unsigned bolt_mask1 = 11, bolt_mask2 = 12; // the two indices the capsule pass leaves at their saved values
} // namespace
HRESULT WeaponEffectsPass::draw_bolts(const WeaponBoltDraw& f, WeaponBoltReport* output) noexcept {
    PreserveCpuState guard;
    WeaponBoltReport local{};
    WeaponBoltReport& r = output ? *output : local;
    r = WeaponBoltReport{};
    calls_ = 0;
    const unsigned total = f.lines + f.orbs + f.waves;
    auto done = [&](HRESULT hr) {
        r.operation = hr;
        r.calls = calls_;
        return hr;
    };
    if (!device_ || !caps_.enabled || !caps_.bolt_enabled || total > weapon_bolt_quads || f.rim_lines > f.lines ||
        f.rim_orbs > f.orbs || (total && !f.quads)) {
        r.failed = WeaponBoltStep::Validate;
        return done(E_INVALIDARG);
    }
    if (!total) return done(S_FALSE);
    // After a Reset the stage's run() recreates the objects at the resolve, later in the frame than this draw: recreate
    // them here (resource creation is legal inside a scene), so the first frame after a Reset draws ours.
    if (!reset_pending_ && !bolt_objects_ready()) ensure_resources();
    if (reset_pending_ || !bolt_objects_ready()) {
        r.failed = WeaponBoltStep::Resources;
        return done(D3DERR_INVALIDCALL);
    }
    D d = device_;
    // Save: every value set below, read first (documented Get*; the proxy keeps the device non-pure).
    DWORD saved[bolt_state_count]{};
    IDirect3DVertexShader9* old_vs = nullptr;
    IDirect3DPixelShader9* old_ps = nullptr;
    IDirect3DVertexDeclaration9* old_decl = nullptr;
    IDirect3DVertexBuffer9* old_vb = nullptr;
    IDirect3DIndexBuffer9* old_ib = nullptr;
    UINT old_offset = 0, old_stride = 0, frequency = 1;
    float old_constants[16]{};
    HRESULT hr = S_OK;
    for (unsigned i = 0; i < bolt_state_count && SUCCEEDED(hr); ++i)
        hr = call<GetRsFn>(GetRenderState)(d, bolt_states[i], &saved[i]);
    if (SUCCEEDED(hr)) hr = call<GetVsFn>(GetVertexShader)(d, &old_vs);
    if (SUCCEEDED(hr)) hr = call<GetPsFn>(GetPixelShader)(d, &old_ps);
    if (SUCCEEDED(hr)) hr = call<GetDeclarationFn>(GetVertexDeclaration)(d, &old_decl);
    if (SUCCEEDED(hr)) hr = call<GetStreamFn>(GetStreamSource)(d, 0, &old_vb, &old_offset, &old_stride);
    if (SUCCEEDED(hr)) hr = call<GetStreamFreqFn>(GetStreamSourceFreq)(d, 0, &frequency);
    if (SUCCEEDED(hr)) hr = call<GetIndicesFn>(GetIndices)(d, &old_ib);
    if (SUCCEEDED(hr)) hr = call<GetConstantsFn>(GetPixelShaderConstantF)(d, 0, old_constants, 4);
    if (SUCCEEDED(hr) && frequency != 1) hr = D3DERR_INVALIDCALL; // an instanced stream 0 is not the bullet batch's
    auto release_saved = [&] {
        drop(old_vs);
        drop(old_ps);
        drop(old_decl);
        drop(old_vb);
        drop(old_ib);
    };
    if (FAILED(hr)) {
        release_saved();
        r.failed = WeaponBoltStep::Save;
        if (lost(hr)) reset_pending_ = true;
        return done(hr);
    }
    // The quads into the dynamic buffer (one DISCARD lock; four vertices a quad, the constant interpolants repeated).
    void* bits = nullptr;
    hr = (faults_ & 64u) ? E_FAIL : bolt_vb_->Lock(0, UINT(total * 4 * sizeof(WeaponBoltVertex)), &bits, D3DLOCK_DISCARD);
    faults_ &= ~64u;
    if (SUCCEEDED(hr)) {
        auto* v = static_cast<WeaponBoltVertex*>(bits);
        for (unsigned q = 0; q < total; ++q) {
            const WeaponBoltQuad& src = f.quads[q];
            for (unsigned c = 0; c < 4; ++c, ++v) {
                std::memcpy(v->clip, src.clip[c], sizeof v->clip);
                std::memcpy(v->p, src.p, sizeof v->p);
            }
        }
        hr = bolt_vb_->Unlock();
    }
    if (FAILED(hr)) {
        release_saved();
        r.failed = WeaponBoltStep::Lock;
        if (lost(hr)) reset_pending_ = true;
        return done(hr);
    }
    // Set, then draw: the rim pass first (its prefixes), then the capsules.
    HRESULT first = S_OK;
    auto step = [&](WeaponBoltStep at, HRESULT h) {
        if (SUCCEEDED(first) && FAILED(h)) {
            first = h;
            r.failed = at;
        }
        return SUCCEEDED(first);
    };
    step(WeaponBoltStep::State, call<SetDeclarationFn>(SetVertexDeclaration)(d, bolt_declaration_));
    step(WeaponBoltStep::State, call<SetVsFn>(SetVertexShader)(d, bolt_vs_));
    step(WeaponBoltStep::State, call<SetPsFn>(SetPixelShader)(d, f.far_flag ? bolt_flag_ps_ : bolt_ps_));
    step(WeaponBoltStep::State,
         call<SetStreamFn>(SetStreamSource)(d, 0, bolt_vb_, 0, UINT(sizeof(WeaponBoltVertex))));
    step(WeaponBoltStep::State, call<SetIndicesFn>(SetIndices)(d, bolt_ib_));
    for (unsigned i = 0; i < bolt_state_count && SUCCEEDED(first); ++i)
        if (i != bolt_mask1 && i != bolt_mask2)
            step(WeaponBoltStep::State, call<SetRsFn>(SetRenderState)(d, bolt_states[i], bolt_values[i]));
    auto draw_range = [&](WeaponBoltStep at, unsigned start, unsigned count) {
        if (!count || FAILED(first)) return;
        HRESULT h = E_FAIL;
        if (at == WeaponBoltStep::Capsules && (faults_ & 128u))
            faults_ &= ~128u; // fixture fault bit 7: the capsule draw refused (nothing drawn)
        else
            h = call<DrawIndexedFn>(DrawIndexedPrimitive)(d, D3DPT_TRIANGLELIST, 0, 0, total * 4, start * 6, count * 2);
        if (step(at, h)) ++r.draws;
    };
    float look[16];
    std::memcpy(look, f.look, sizeof look);
    if (f.rim && (f.rim_lines || f.rim_orbs) && SUCCEEDED(first)) {
        look[6] = 1.f; // c1.z: the rim pass
        step(WeaponBoltStep::Rim, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 0, look, 4));
        step(WeaponBoltStep::Rim, call<SetRsFn>(SetRenderState)(d, D3DRS_SRCBLEND, D3DBLEND_ZERO));
        step(WeaponBoltStep::Rim, call<SetRsFn>(SetRenderState)(d, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR));
        step(WeaponBoltStep::Rim, call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE1, 0));
        step(WeaponBoltStep::Rim, call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE2, 0));
        look[12] = 0.f; // c3.x: the line kind
        step(WeaponBoltStep::Rim, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 0, look, 4));
        draw_range(WeaponBoltStep::Rim, 0, f.rim_lines);
        look[12] = 1.f;
        if (f.rim_orbs) step(WeaponBoltStep::Rim, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 0, look, 4));
        draw_range(WeaponBoltStep::Rim, f.lines, f.rim_orbs);
        r.rim_quads = f.rim_lines + f.rim_orbs;
        step(WeaponBoltStep::Capsules, call<SetRsFn>(SetRenderState)(d, D3DRS_SRCBLEND, D3DBLEND_ONE));
        step(WeaponBoltStep::Capsules, call<SetRsFn>(SetRenderState)(d, D3DRS_DESTBLEND, D3DBLEND_ONE));
        step(WeaponBoltStep::Capsules, call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE1, saved[bolt_mask1]));
        step(WeaponBoltStep::Capsules, call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE2, saved[bolt_mask2]));
    }
    const bool rim_drew = SUCCEEDED(first) && r.rim_quads;
    look[6] = 0.f;
    const unsigned ranges[3][2] = {{0, f.lines}, {f.lines, f.orbs}, {f.lines + f.orbs, f.waves}};
    for (unsigned kind = 0; kind < 3; ++kind) {
        if (!ranges[kind][1]) continue;
        look[12] = float(kind);
        step(WeaponBoltStep::Capsules, call<SetConstantsFn>(SetPixelShaderConstantF)(d, 0, look, 4));
        draw_range(WeaponBoltStep::Capsules, ranges[kind][0], ranges[kind][1]);
    }
    // The capsule pass failed after the rim darkened the scene (review 2026-11-07, note 14): the rim's band is divided
    // back out where the target can (a lost device presents nothing: no undo): the same ranges again with the program's
    // undo output s / (1 - s) under DESTCOLOR / ONE, dest (1 + s / (1 - s)) = dest / (1 - s); RT1 / RT2 stay masked.
    // DESTCOLOR as the source factor: D3DPBLENDCAPS_DESTCOLOR, checked; without it the rim's band stays for this frame.
    if (rim_drew && FAILED(first) && r.failed == WeaponBoltStep::Capsules && !lost(first) &&
        caps_.bolt_rim_undo) {
        HRESULT u = call<SetRsFn>(SetRenderState)(d, D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
        if (SUCCEEDED(u)) u = call<SetRsFn>(SetRenderState)(d, D3DRS_DESTBLEND, D3DBLEND_ONE);
        if (SUCCEEDED(u)) u = call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE1, 0);
        if (SUCCEEDED(u)) u = call<SetRsFn>(SetRenderState)(d, D3DRS_COLORWRITEENABLE2, 0);
        look[6] = 2.f;
        const unsigned undo_ranges[2][3] = {{0, f.rim_lines, 0}, {f.lines, f.rim_orbs, 1}};
        for (const auto& ur : undo_ranges) {
            if (!ur[1] || FAILED(u)) continue;
            look[12] = float(ur[2]);
            u = call<SetConstantsFn>(SetPixelShaderConstantF)(d, 0, look, 4);
            if (SUCCEEDED(u))
                u = call<DrawIndexedFn>(DrawIndexedPrimitive)(d, D3DPT_TRIANGLELIST, 0, 0, total * 4, ur[0] * 6, ur[1] * 2);
        }
        r.rim_undone = SUCCEEDED(u);
    }
    if (SUCCEEDED(first)) {
        r.quads = total;
        r.drew = true;
    }
    // Restore in reverse order: render states, constants, indices, stream, declaration, programs.
    HRESULT back = S_OK;
    auto undo = [&](HRESULT h) {
        if (SUCCEEDED(back) && FAILED(h)) back = h;
    };
    for (unsigned i = bolt_state_count; i-- > 0;) undo(call<SetRsFn>(SetRenderState)(d, bolt_states[i], saved[i]));
    undo(call<SetConstantsFn>(SetPixelShaderConstantF)(d, 0, old_constants, 4));
    undo(call<SetIndicesFn>(SetIndices)(d, old_ib));
    undo(call<SetStreamFn>(SetStreamSource)(d, 0, old_vb, old_offset, old_stride));
    undo(call<SetDeclarationFn>(SetVertexDeclaration)(d, old_decl));
    undo(call<SetPsFn>(SetPixelShader)(d, old_ps));
    undo(call<SetVsFn>(SetVertexShader)(d, old_vs));
    release_saved();
    r.restored = SUCCEEDED(back);
    if (FAILED(back) && SUCCEEDED(first)) {
        first = back;
        r.failed = WeaponBoltStep::Restore;
    }
    if (FAILED(first) && lost(first)) reset_pending_ = true;
    return done(FAILED(first) ? first : S_OK);
}
} // namespace x3m::renderer
