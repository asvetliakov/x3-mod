// Fault injection into the production FogPass's long-distance-fade paths (docs/architecture/fog-visible-distance.md §6):
// the horizon level's SYSTEMMEM staging and DEFAULT atlas creation (also after a Reset), the staging LockRect, the
// UpdateSurface of a horizon rectangle, the device refusing the distance programs, and fog_distance 1; and the family
// field's persistent SYSTEMMEM upload texture (prepare_field: reuse, a smaller field, Reset, detach, the per-call
// fallback). The real
// FogPass, DensityCache (worker thread) and generator run against a scripted device: a native vtable of the slots
// prepare_density uses, fake COM textures, surfaces and shaders. No D3D runtime, no window, no GPU, no game.
// Build: i686-w64-mingw32-g++ (the proxy's flags) with -DX3M_FOG_PASS_FIXTURE; run natively (WSL interop) or under Wine.
// Prints one line per scenario and "fog_pass_horizon_fault scenarios=N failures=0".
#include "../../src/renderer/fog_pass.h"
#include "../../src/fog/fog_density_cache.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
// The family field's asset decoder is not linked: no compiled profile unless a field scenario lists one (then a
// small field of its size, decoded as a ramp).
namespace field_stub {
x3m::renderer::fog_field::ProfileInfo infos[3]{};
unsigned count = 0;
} // namespace field_stub
namespace x3m::renderer::fog_field {
const ProfileInfo* profile_info(Profile profile) noexcept {
    for (unsigned i = 0; i < field_stub::count; ++i)
        if (field_stub::infos[i].profile == profile) return &field_stub::infos[i];
    return nullptr;
}
Result decode_from_resource(void*, Profile profile, std::vector<std::uint16_t>& output) noexcept {
    const ProfileInfo* info = profile_info(profile);
    if (!info) return {};
    output.assign(std::size_t(info->width) * info->height * 4, 0);
    for (std::size_t i = 0; i < output.size(); ++i) output[i] = std::uint16_t(i & 0x3fffu);
    return {Status::Ok, 0, info->decoded_bytes};
}
const FamilyTable* family_table() noexcept { return nullptr; }
bool family_info(Profile, ProfileInfo&) noexcept { return false; }
Result decode_family(Profile, std::vector<std::uint16_t>&) noexcept { return {}; }
} // namespace x3m::renderer::fog_field
using namespace x3m;
using namespace x3m::renderer;
namespace {
constexpr DWORD march_distance_q4[] = {
#include "../../src/renderer/fog_density_march_distance_q4_program_inc.h"
};
constexpr DWORD march_look_q4[] = {
#include "../../src/renderer/fog_density_march_look_q4_program_inc.h"
};
unsigned failures = 0, scenarios = 0;
void need(bool ok, const char* scenario, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s: %s\n", scenario, what);
    }
}

// --- scripted COM objects --------------------------------------------------------------------------------------
enum Kind { Texture, Surface, Shader, Api, Device };
struct Obj {
    void** vtbl;
    long refs = 1;
    Kind kind;
    unsigned index = 0; // CreateTexture order (textures and their surfaces)
    D3DPOOL pool = D3DPOOL_DEFAULT;
    UINT width = 0, height = 0;
    std::vector<unsigned char> bits;
    Obj* level = nullptr;
};
std::vector<Obj*> objects;
long live() {
    long n = 0;
    for (Obj* o : objects) n += o->kind != Device && o->refs > 0;
    return n;
}
struct Faults {
    unsigned create_fail_at = 0; // the Nth CreateTexture fails
    HRESULT create_hr = E_OUTOFMEMORY;
    unsigned lock_fail_texture = 0; // LockRect of the texture created Nth fails
    unsigned update_fail_texture = 0; // UpdateSurface into the texture created Nth fails
    bool refuse_distance = false;   // CreatePixelShader refuses the distance programs
} faults;
unsigned creates = 0, locks_failed = 0, updates_failed = 0, updates[8]{}, shaders_distance = 0, shaders_look = 0;
unsigned update_textures = 0, systemmem_creates = 0; // the field: UpdateTexture calls, SYSTEMMEM CreateTexture calls
RECT last_update_rect{};
Obj* last_update_source = nullptr;

[[noreturn]] void trap(int slot) {
    std::printf("TRAP slot %d\n", slot);
    std::fflush(stdout);
    std::exit(3);
}
template <int N> HRESULT WINAPI trap_slot() { trap(N); }
template <int... N> void fill_traps(void** table, std::integer_sequence<int, N...>) {
    ((table[N] = reinterpret_cast<void*>(&trap_slot<N>)), ...);
}
HRESULT WINAPI qi(Obj*, const IID&, void** out) {
    *out = nullptr;
    return E_NOINTERFACE;
}
ULONG WINAPI add_ref(Obj* o) { return ULONG(++o->refs); }
ULONG WINAPI release(Obj* o) {
    if (o->refs <= 0) trap(-2);
    return ULONG(--o->refs);
}
Obj* make(Kind kind, void** vtbl) {
    Obj* o = new Obj;
    o->vtbl = vtbl;
    o->kind = kind;
    objects.push_back(o);
    return o;
}
void* common[32], *texture_vt[32], *surface_vt[32], *api_vt[32], *device_vt[119];
// IDirect3DTexture9: GetLevelDesc 17, GetSurfaceLevel 18, LockRect 19, UnlockRect 20.
HRESULT WINAPI level_desc(Obj*, UINT, D3DSURFACE_DESC* d) {
    *d = {};
    d->Format = D3DFMT_A16B16G16R16F;
    d->Width = fog::kAtlasWidth;
    d->Height = fog::kAtlasHeight;
    return S_OK;
}
HRESULT WINAPI surface_level(Obj* t, UINT, IDirect3DSurface9** out) {
    if (!t->level) {
        t->level = make(Surface, surface_vt);
        t->level->index = t->index;
        t->level->refs = 0;
    }
    ++t->level->refs;
    *out = reinterpret_cast<IDirect3DSurface9*>(t->level);
    return S_OK;
}
HRESULT WINAPI lock_rect(Obj* t, UINT, D3DLOCKED_RECT* r, const RECT*, DWORD) {
    if (t->index == faults.lock_fail_texture) {
        ++locks_failed;
        return E_FAIL;
    }
    if (t->pool != D3DPOOL_SYSTEMMEM) trap(-3);
    r->pBits = t->bits.data();
    r->Pitch = INT(t->width * 8u); // the density staging: fog::kAtlasPitch
    return S_OK;
}
HRESULT WINAPI unlock_rect(Obj*, UINT) { return S_OK; }
HRESULT WINAPI check_format(Obj*, UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT) { return D3D_OK; }
// The device (slots of fog_pass.cpp's Slot enum).
using D = IDirect3DDevice9*;
HRESULT WINAPI get_d3d(D, IDirect3D9** out) {
    *out = reinterpret_cast<IDirect3D9*>(make(Api, api_vt));
    return S_OK;
}
HRESULT WINAPI creation(D, D3DDEVICE_CREATION_PARAMETERS* p) {
    *p = {};
    return S_OK;
}
HRESULT WINAPI create_texture(D, UINT w, UINT h, UINT, DWORD, D3DFORMAT, D3DPOOL pool, IDirect3DTexture9** out, HANDLE*) {
    ++creates;
    if (creates == faults.create_fail_at) return faults.create_hr;
    Obj* t = make(Texture, texture_vt);
    t->index = creates;
    t->pool = pool;
    t->width = w;
    t->height = h;
    systemmem_creates += pool == D3DPOOL_SYSTEMMEM;
    if (pool == D3DPOOL_SYSTEMMEM) t->bits.assign(std::size_t(w) * h * 8, 0);
    *out = reinterpret_cast<IDirect3DTexture9*>(t);
    return S_OK;
}
HRESULT WINAPI update_texture(D, IDirect3DBaseTexture9* source, IDirect3DBaseTexture9* target) {
    const Obj *s = reinterpret_cast<const Obj*>(source), *t = reinterpret_cast<const Obj*>(target);
    if (s->pool != D3DPOOL_SYSTEMMEM || t->pool != D3DPOOL_DEFAULT || s->width != t->width || s->height != t->height)
        trap(-5); // the documented contract: SYSTEMMEM to DEFAULT, equal dimensions
    ++update_textures;
    return S_OK;
}
HRESULT WINAPI update_surface(D, IDirect3DSurface9* src, const RECT* rect, IDirect3DSurface9* dst, const POINT*) {
    last_update_rect = rect ? *rect : RECT{};
    last_update_source = reinterpret_cast<Obj*>(src);
    const Obj* s = reinterpret_cast<const Obj*>(dst);
    if (s->index == faults.update_fail_texture) {
        ++updates_failed;
        return E_FAIL;
    }
    ++updates[s->index < 8 ? s->index : 0];
    return S_OK;
}
HRESULT WINAPI create_vs(D, const DWORD*, IDirect3DVertexShader9** out) {
    *out = reinterpret_cast<IDirect3DVertexShader9*>(make(Shader, common));
    return S_OK;
}
HRESULT WINAPI create_decl(D, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9** out) {
    *out = reinterpret_cast<IDirect3DVertexDeclaration9*>(make(Shader, common));
    return S_OK;
}
HRESULT WINAPI create_ps(D, const DWORD* words, IDirect3DPixelShader9** out) {
    *out = nullptr;
    const bool distance = !std::memcmp(words, march_distance_q4, sizeof march_distance_q4);
    shaders_distance += distance;
    shaders_look += !std::memcmp(words, march_look_q4, sizeof march_look_q4);
    if (distance && faults.refuse_distance) return D3DERR_INVALIDCALL;
    *out = reinterpret_cast<IDirect3DPixelShader9*>(make(Shader, common));
    return S_OK;
}
void tables() {
    fill_traps(common, std::make_integer_sequence<int, 32>{});
    common[0] = reinterpret_cast<void*>(&qi);
    common[1] = reinterpret_cast<void*>(&add_ref);
    common[2] = reinterpret_cast<void*>(&release);
    std::memcpy(texture_vt, common, sizeof common);
    std::memcpy(surface_vt, common, sizeof common);
    std::memcpy(api_vt, common, sizeof common);
    texture_vt[17] = reinterpret_cast<void*>(&level_desc);
    texture_vt[18] = reinterpret_cast<void*>(&surface_level);
    texture_vt[19] = reinterpret_cast<void*>(&lock_rect);
    texture_vt[20] = reinterpret_cast<void*>(&unlock_rect);
    api_vt[10] = reinterpret_cast<void*>(&check_format);
    fill_traps(device_vt, std::make_integer_sequence<int, 119>{});
    device_vt[0] = reinterpret_cast<void*>(&qi);
    device_vt[1] = reinterpret_cast<void*>(&add_ref);
    device_vt[2] = reinterpret_cast<void*>(&release);
    device_vt[6] = reinterpret_cast<void*>(&get_d3d);
    device_vt[9] = reinterpret_cast<void*>(&creation);
    device_vt[23] = reinterpret_cast<void*>(&create_texture);
    device_vt[30] = reinterpret_cast<void*>(&update_surface);
    device_vt[31] = reinterpret_cast<void*>(&update_texture);
    device_vt[86] = reinterpret_cast<void*>(&create_decl);
    device_vt[91] = reinterpret_cast<void*>(&create_vs);
    device_vt[106] = reinterpret_cast<void*>(&create_ps);
}
D3DCAPS9 caps() {
    D3DCAPS9 c{};
    c.PixelShaderVersion = D3DPS_VERSION(3, 0);
    c.VertexShaderVersion = D3DVS_VERSION(3, 0);
    c.MaxPixelShader30InstructionSlots = 32768;
    c.MaxTextureWidth = c.MaxTextureHeight = 8192;
    c.NumSimultaneousRTs = 4;
    c.MaxStreams = 16;
    c.MaxVertexShaderConst = 256;
    c.TextureFilterCaps = D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFLINEAR;
    c.DevCaps2 = D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES;
    return c;
}

struct Run {
    FogPass pass;
    Obj* device = make(Device, device_vt);
    FogDensityConfig config{};
    std::uint64_t frame = 0;
    double camera[3] = {1000., 2000., -3000.};
    HRESULT last = S_OK;
    bool any_oom = false;
    HRESULT upload = S_FALSE; // the proxy's device-attach creation of the field upload texture (field scenarios)
    explicit Run(float distance, bool field_upload = false) {
        config.enabled = true;
        config.sector_key = 5;
        config.recipe = 1;
        config.sigma = 1e-6f;
        config.distance = distance;
        config.handover_step = config.handover_coldfill = true;
        if (field_upload) upload = pass.create_field_upload(reinterpret_cast<D>(device), device_vt, 8192, 8192);
        if (FAILED(pass.attach(reinterpret_cast<D>(device), device_vt, caps(), D3DFMT_X8R8G8B8))) trap(-4);
    }
    HRESULT prepare() {
        last = pass.prepare_density(config, camera, ++frame);
        any_oom = any_oom || last == E_OUTOFMEMORY || last == D3DERR_OUTOFVIDEOMEMORY;
        return last;
    }
    // Frames until `done` holds (the real worker fills in the background), at most `seconds`.
    template <class F> bool until(F done, double seconds = 60) {
        const auto begin = std::chrono::steady_clock::now();
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() < seconds) {
            prepare();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
    const fog::DensityCache* cache() const { return pass.fixture_density_cache(); }
    bool horizon_freed() const { return cache() && !cache()->horizon() && cache()->cache_bytes(fog::kHorizonLevel) == nullptr; }
};
void reset_counters(const Faults& f) {
    faults = f;
    creates = locks_failed = updates_failed = shaders_distance = shaders_look = update_textures = systemmem_creates = 0;
    std::memset(updates, 0, sizeof updates);
}
void report(const char* name, Run& r) {
    const auto& s = r.pass.density_status();
    std::printf("SCENARIO %s result=%08lx available=%d cap=%.0f horizon_refused=%s distance_refused=%s horizon=%d "
                "ready=%.3f,%.3f,%.3f creates=%u lock_failed=%u update_failed=%u horizon_updates=%u\n",
                name, (unsigned long)r.last, int(s.available), double(s.distance_cap),
                s.horizon_refused ? s.horizon_refused : "-", s.distance_refused ? s.distance_refused : "-",
                int(r.cache() && r.cache()->horizon()), double(s.ready_far), double(s.ready_fine), double(s.ready_horizon),
                creates, locks_failed, updates_failed, updates[6]);
    ++scenarios;
}
bool refused(Run& r, const char* reason) {
    const char* why = r.pass.density_status().horizon_refused;
    return why && !std::strcmp(why, reason);
}

void scenario_baseline() {
    reset_counters({});
    Run r(3.f);
    const bool ready = r.until([&] { return r.pass.density_status().ready_horizon >= 1.f; });
    report("baseline", r);
    need(ready && r.last == S_OK && creates == 6 && updates[6] > 0 && r.pass.density_distance_programs() &&
             r.pass.density_status().distance_cap == 337500.f && !r.pass.density_status().horizon_refused,
         "baseline", "three levels ready at the 67.5 km cap");
}
// A creation failure of the horizon staging (5th) or atlas (6th): the fallback, never a refusal or an OOM result.
void scenario_create(unsigned at, const char* reason, HRESULT hr) {
    reset_counters({at, hr});
    Run r(3.f);
    const bool ready = r.until([&] { return r.pass.density_status().ready_fine >= 1.f && r.horizon_freed(); });
    report(reason, r);
    need(ready && r.last == S_OK && !r.any_oom && refused(r, reason) && r.pass.density_status().distance_cap == 195000.f &&
             !r.pass.fixture_density_atlas(fog::kHorizonLevel) && r.pass.density_distance_programs() &&
             r.pass.density_status().available && updates[6] == 0,
         reason, "39 km fallback, horizon cache freed, fog drawable");
}
void scenario_reset() {
    reset_counters({});
    Run r(3.f);
    r.until([&] { return r.pass.density_status().ready_horizon >= 1.f; });
    r.pass.before_reset();
    r.pass.after_reset(S_OK);
    faults.create_fail_at = creates + 3; // after a Reset only the DEFAULT atlases are created again: fine, far, horizon
    const bool ready = r.until([&] { return r.pass.density_status().ready_far >= 1.f && r.horizon_freed(); });
    report("reset_atlas", r);
    need(ready && r.last == S_OK && !r.any_oom && refused(r, "atlas") && r.pass.density_status().distance_cap == 195000.f &&
             r.pass.fixture_density_atlas(0) && r.pass.fixture_density_atlas(1),
         "reset_atlas", "the horizon atlas lost at a Reset falls back, far and fine come back");
}
void scenario_lock() {
    reset_counters({0, E_OUTOFMEMORY, 5});
    Run r(3.f);
    const bool fell = r.until([&] { return locks_failed > 0 && r.horizon_freed(); });
    r.until([&] { return r.pass.density_status().ready_fine >= 1.f; });
    report("lock", r);
    need(fell && r.last == S_OK && !r.any_oom && refused(r, "lock") && r.pass.density_status().ready_far >= 1.f &&
             r.pass.density_status().ready_fine >= 1.f && !r.pass.fixture_density_atlas(fog::kHorizonLevel),
         "lock", "a failed horizon LockRect falls back, far and fine upload on");
}
void scenario_update() {
    reset_counters({0, E_OUTOFMEMORY, 0, 6});
    Run r(3.f);
    const bool fell = r.until([&] { return updates_failed > 0 && r.horizon_freed(); });
    r.until([&] { return r.pass.density_status().ready_fine >= 1.f; });
    report("upload", r);
    need(fell && updates_failed == 1 && r.last == S_OK && !r.any_oom && refused(r, "upload") &&
             r.pass.density_status().ready_far >= 1.f && r.pass.density_status().ready_fine >= 1.f,
         "upload", "a failed horizon UpdateSurface falls back once, far and fine upload on");
}
void scenario_lost() {
    reset_counters({5, D3DERR_DEVICELOST});
    Run r(3.f);
    r.prepare();
    report("lost", r);
    need(r.last == D3DERR_DEVICELOST && r.pass.reset_pending() && !r.pass.density_status().horizon_refused, "lost",
         "a lost device on the horizon staging is a Reset, not a fallback");
}
void scenario_program() {
    reset_counters({0, E_OUTOFMEMORY, 0, 0, true});
    Run r(3.f);
    const bool ready = r.until([&] { return r.pass.density_status().ready_fine >= 1.f; });
    report("program", r);
    const char* why = r.pass.density_status().distance_refused;
    need(ready && why && !std::strcmp(why, "program") && !r.pass.density_distance_programs() && shaders_distance == 1 &&
             shaders_look == 1 && creates == 4 && r.cache() && !r.cache()->horizon() &&
             r.cache()->cache_bytes(fog::kHorizonLevel) == nullptr && r.pass.density_status().distance_cap == 0.f,
         "program", "refused distance programs draw today's set and never allocate the horizon level");
}
void scenario_distance_one() {
    reset_counters({});
    Run r(1.f);
    const bool ready = r.until([&] { return r.pass.density_status().ready_fine >= 1.f; });
    report("distance_1", r);
    need(ready && shaders_distance == 0 && shaders_look == 1 && creates == 4 && !r.cache()->horizon() &&
             r.cache()->cache_bytes(fog::kHorizonLevel) == nullptr && r.pass.density_status().distance_cap == 0.f &&
             !r.pass.density_status().horizon_refused,
         "distance_1", "today's programs, two levels, no horizon allocation");
}
// The family field's persistent SYSTEMMEM upload texture: one creation before attach (the proxy's device attach) at
// the largest compiled field (64x32), kept by attach and reused for every field; a smaller field (32x16) copies its
// rectangle with UpdateSurface; Reset keeps it; detach releases it. Then a failed creation: the per-call upload
// texture (step field_upload); a field larger than the persistent texture takes the per-call path too; a failed lock
// on the persistent texture drops it and that prepare and later ones go per-call.
Obj* object_at(unsigned index, D3DPOOL pool) {
    for (Obj* o : objects)
        if (o->kind == Texture && o->index == index && o->pool == pool && o->refs > 0) return o;
    return nullptr;
}
void field_infos(unsigned count) {
    using fog_field::Profile;
    field_stub::infos[0] = {Profile::Bluewell, 1, 64, 32, 8, 64 * 32 * 8, 1e-6f, 0, 0};
    field_stub::infos[1] = {Profile::Fogred, 1, 32, 16, 8, 32 * 16 * 8, 1e-6f, 0, 0};
    field_stub::infos[2] = {Profile::Fogblue, 1, 96, 40, 8, 96 * 40 * 8, 1e-6f, 0, 0};
    field_stub::count = count;
}
void scenario_field_persistent() {
    using fog_field::Profile;
    field_infos(2);
    reset_counters({});
    unsigned attach_refs = 0;
    bool ok = false, reuse = false, smaller = false, reset = false, released = false;
    {
        Run r(3.f, true);
        Obj* upload = object_at(1, D3DPOOL_SYSTEMMEM);
        attach_refs = r.pass.references();
        ok = r.upload == S_OK && creates == 1 && upload && upload->width == 64 && upload->height == 32 && r.pass.field_upload_result() == S_OK &&
             r.pass.field_upload_width() == 64 && r.pass.field_upload_height() == 32;
        reuse = r.pass.prepare_field(nullptr, Profile::Bluewell) == S_OK && creates == 2 && systemmem_creates == 1 &&
                update_textures == 1 && !std::strcmp(r.pass.last_step(), "ok") && upload->refs == 1 &&
                upload->bits[8] == 4; // row 0, texel 1, red: the ramp's 4th half
        smaller = r.pass.prepare_field(nullptr, Profile::Fogred) == S_OK && creates == 3 && systemmem_creates == 1 &&
                  update_textures == 1 && updates[3] == 1 && last_update_source == upload->level &&
                  last_update_rect.right == 32 && last_update_rect.bottom == 16 && !last_update_rect.left &&
                  !last_update_rect.top && upload->bits[64 * 8] == std::uint8_t((32 * 4) & 0xff) && upload->refs == 1 &&
                  (!upload->level || upload->level->refs == 0);
        r.pass.before_reset();
        const bool kept = upload->refs == 1 && r.pass.references() == attach_refs;
        r.pass.after_reset(S_OK);
        reset = kept && r.pass.prepare_field(nullptr, Profile::Bluewell) == S_OK && creates == 4 && systemmem_creates == 1 &&
                update_textures == 2 && upload->refs == 1;
        r.pass.detach();
        released = upload->refs == 0 && r.pass.references() == 0 && r.pass.field_upload_result() == S_FALSE;
    }
    std::printf("SCENARIO field_persistent attach=%d reuse=%d smaller=%d reset=%d released=%d creates=%u systemmem=%u "
                "update_textures=%u attach_refs=%u\n",
                int(ok), int(reuse), int(smaller), int(reset), int(released), creates, systemmem_creates, update_textures,
                attach_refs);
    ++scenarios;
    need(ok && reuse && smaller && reset && released, "field_persistent",
         "one attach-time SYSTEMMEM upload texture, reused, kept over Reset, released at detach");
    field_infos(0);
}
void scenario_field_fallback() {
    using fog_field::Profile;
    field_infos(2);
    reset_counters({1, E_OUTOFMEMORY});
    bool attach = false, per_call = false, failed = false, larger = false, lock = false;
    {
        Run r(3.f, true);
        attach = r.upload == E_OUTOFMEMORY && r.pass.caps().enabled && r.pass.field_upload_width() == 0;
        per_call = r.pass.prepare_field(nullptr, Profile::Bluewell) == S_OK && creates == 3 && systemmem_creates == 1 &&
                   update_textures == 1 && !object_at(2, D3DPOOL_SYSTEMMEM);
        faults.create_fail_at = creates + 1; // the per-call upload texture of the next field
        failed = r.pass.prepare_field(nullptr, Profile::Fogred) == E_OUTOFMEMORY &&
                 !std::strcmp(r.pass.last_step(), "field_upload") && r.pass.field_profile() == Profile::None;
    }
    // A field larger than the persistent texture (64x32) takes the per-call upload texture.
    field_infos(2);
    reset_counters({});
    {
        Run r(3.f, true);
        field_infos(3);
        larger = r.pass.prepare_field(nullptr, Profile::Fogblue) == S_OK && creates == 3 && systemmem_creates == 2 &&
                 update_textures == 1 && !object_at(2, D3DPOOL_SYSTEMMEM) && object_at(1, D3DPOOL_SYSTEMMEM);
    }
    // A failed LockRect on the persistent texture (created 1st): dropped, this prepare goes per-call (2nd, atlas 3rd),
    // the one-shot drop flag, and the next field per-call as well.
    field_infos(2);
    reset_counters({0, E_OUTOFMEMORY, 1});
    {
        Run r(3.f, true);
        Obj* persistent = object_at(1, D3DPOOL_SYSTEMMEM);
        const unsigned refs = r.pass.references();
        lock = persistent && r.pass.prepare_field(nullptr, Profile::Bluewell) == S_OK && locks_failed == 1 &&
               persistent->refs == 0 && r.pass.take_field_upload_dropped() && !r.pass.take_field_upload_dropped() &&
               r.pass.field_upload_result() == E_FAIL && r.pass.field_upload_width() == 0 && creates == 3 &&
               systemmem_creates == 2 && update_textures == 1 && r.pass.references() == refs && // -upload +atlas
               r.pass.prepare_field(nullptr, Profile::Fogred) == S_OK && creates == 5 && systemmem_creates == 3 &&
               update_textures == 2 && !r.pass.take_field_upload_dropped();
    }
    std::printf("SCENARIO field_fallback attach=%d per_call=%d failed=%d larger=%d lock=%d creates=%u systemmem=%u\n",
                int(attach), int(per_call), int(failed), int(larger), int(lock), creates, systemmem_creates);
    ++scenarios;
    need(attach && per_call && failed && larger && lock, "field_fallback",
         "a failed creation, a larger field and a failed lock use the per-call upload texture");
    field_infos(0);
}
} // namespace

int main() {
    tables();
    scenario_baseline();
    scenario_create(5, "staging", E_OUTOFMEMORY);
    scenario_create(6, "atlas", D3DERR_OUTOFVIDEOMEMORY);
    scenario_reset();
    scenario_lock();
    scenario_update();
    scenario_lost();
    scenario_program();
    scenario_distance_one();
    scenario_field_persistent();
    scenario_field_fallback();
    std::printf("fog_pass_horizon_fault scenarios=%u failures=%u live_objects=%ld\n", scenarios, failures, live());
    return failures ? 1 : 0;
}
