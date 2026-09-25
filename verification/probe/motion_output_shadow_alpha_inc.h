// Alpha-tested caster script ("shadowalpha" mode; docs/architecture/
// shadow-replay-gates.md, "Alpha-tested casters"; X3M_SHADOW_ALPHA_CASTERS).
// The production ShadowReplayPass linked into the fixture and driven directly,
// as the "sunapply" mode does, through one-map cascade transactions (the
// single-map attach/execute were removed on 2026-09-25): identity light rows
// (c0-c2 of each issue over the pass's c3; the map is the draws' own x, y and
// depth z), one managed vertex buffer of four quads under a
// POSITION FLOAT3 + TEXCOORD0 FLOAT2 declaration:
//   O1 opaque (depth .25), H alpha-tested with a texture whose alpha is 255 for
//   u < .5 and 0 beyond (depth .5), Z alpha-tested with an all-zero alpha
//   (depth .5), O2 opaque after them (depth .75, the depth-only pair bound back).
// Checks: the alpha pass casts H's left half only (a half shadow), nothing of
// Z, all of O1 and O2; the native state calls of the issue path (program pair
// once, sampler 0 once, texture per change, threshold per change); hostile
// caller state (stage-0 texture, sampler 0, PS c0, programs, alpha test)
// restored exactly; the pass without the alpha programs byte-identical on the
// opaque draws and refusing an alpha draw at validation (nothing touched); the
// cascade transaction with the same draws (alpha issues after the opaque ones);
// a Reset between two identical transactions byte-identical; a refused alpha
// program (CreatePixelShader failing through a copied table) leaving the pass
// enabled without them, no reference held. Output: ALPHA_* lines and RESULT.
namespace {
constexpr unsigned alpha_map_size = 256, alpha_tex_size = 64;
struct AlphaQuad { float x0, x1, y0, y1, z; bool alpha; };
constexpr AlphaQuad alpha_quads[4] = {
    {-.9f, -.6f, -.9f, .9f, .25f, false},  // O1
    {-.5f, .5f, -.5f, .5f, .5f, true},     // H: the half-alpha texture
    {.6f, .9f, -.9f, -.6f, .5f, true},     // Z: the zero-alpha texture
    {.6f, .9f, .6f, .9f, .75f, false}};    // O2
// Map texel (i, j) samples screen (i, j) (the D3D9 convention, shadow_replay_depth.py SAMPLE_OFFSET 0).
double alpha_screen_x(double x) { return (x + 1.) * .5 * alpha_map_size; }
double alpha_screen_y(double y) { return (1. - y) * .5 * alpha_map_size; }
struct AlphaRegion { unsigned covered = 0, total = 0; bool depth_ok = true; };
// Texels at least `guard` inside [x0, x1) x [y0, y1) of the map (screen units).
AlphaRegion alpha_region(const std::vector<float>& map, double sx0, double sx1, double sy0, double sy1, double guard, float depth) {
    AlphaRegion r{};
    for (unsigned j = 0; j < alpha_map_size; ++j) for (unsigned i = 0; i < alpha_map_size; ++i) {
        if (!(i >= sx0 + guard && i <= sx1 - guard && j >= sy0 + guard && j <= sy1 - guard)) continue;
        ++r.total;
        const float v = map[std::size_t(j) * alpha_map_size + i];
        if (v < 1.f) { ++r.covered; if (std::fabs(v - depth) > 1e-6f) r.depth_ok = false; }
    }
    return r;
}
std::vector<float> alpha_read_map(Fixture& f, IDirect3DSurface9* map) {
    Com<IDirect3DSurface9> copy;
    api(f.d->CreateOffscreenPlainSurface(alpha_map_size, alpha_map_size, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &copy.p, nullptr), "CreateOffscreenPlainSurface R32F");
    api(f.d->GetRenderTargetData(map, copy.p), "GetRenderTargetData alpha map");
    D3DLOCKED_RECT lock{}; api(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY), "LockRect alpha map");
    std::vector<float> out(std::size_t(alpha_map_size) * alpha_map_size);
    for (unsigned y = 0; y < alpha_map_size; ++y) std::memcpy(out.data() + std::size_t(y) * alpha_map_size, static_cast<const char*>(lock.pBits) + y * lock.Pitch, alpha_map_size * 4);
    api(copy->UnlockRect(), "UnlockRect alpha map");
    return out;
}
// The map's verdict per quad; `alpha_on`: H and Z drawn with their test.
struct AlphaVerdict { AlphaRegion o1, h_left, h_right, z, o2; };
AlphaVerdict alpha_verdict(const std::vector<float>& map) {
    const auto rect = [&](const AlphaQuad& q, double x0, double x1, double guard) {
        return alpha_region(map, alpha_screen_x(x0), alpha_screen_x(x1), alpha_screen_y(q.y1), alpha_screen_y(q.y0), guard, q.z);
    };
    AlphaVerdict v{};
    v.o1 = rect(alpha_quads[0], alpha_quads[0].x0, alpha_quads[0].x1, 1.);
    // H's alpha edge at u = .5 (x = 0): the bilinear blend spans one texture
    // texel either side (two map texels); three texels of guard.
    v.h_left = rect(alpha_quads[1], alpha_quads[1].x0, 0., 3.);
    v.h_right = rect(alpha_quads[1], 0., alpha_quads[1].x1, 3.);
    v.z = rect(alpha_quads[2], alpha_quads[2].x0, alpha_quads[2].x1, 1.);
    v.o2 = rect(alpha_quads[3], alpha_quads[3].x0, alpha_quads[3].x1, 1.);
    return v;
}
void alpha_print(const char* label, const AlphaVerdict& v, const std::vector<float>& map) {
    std::uint64_t h = 14695981039346656037ull;
    const auto* b = reinterpret_cast<const unsigned char*>(map.data());
    for (std::size_t i = 0; i < map.size() * 4; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    unsigned covered = 0; for (float x : map) covered += x < 1.f;
    std::printf("ALPHA_MAP label=%s o1=%u/%u h_left=%u/%u h_right=%u/%u z=%u/%u o2=%u/%u depth_ok=%u covered=%u hash=%016llx\n", label,
                v.o1.covered, v.o1.total, v.h_left.covered, v.h_left.total, v.h_right.covered, v.h_right.total, v.z.covered, v.z.total, v.o2.covered, v.o2.total,
                unsigned(v.o1.depth_ok && v.h_left.depth_ok && v.z.depth_ok && v.o2.depth_ok), covered, static_cast<unsigned long long>(h));
}
// The copied device table's CreatePixelShader: the pass's first program (the
// depth-only one) through the device, every later one refused.
using AlphaCreatePsFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const DWORD*, IDirect3DPixelShader9**);
AlphaCreatePsFn alpha_real_create_ps = nullptr;
unsigned alpha_create_ps_calls = 0;
HRESULT WINAPI alpha_failing_create_ps(IDirect3DDevice9* d, const DWORD* words, IDirect3DPixelShader9** out) {
    if (++alpha_create_ps_calls == 1) return alpha_real_create_ps(d, words, out);
    if (out) *out = nullptr;
    return D3DERR_INVALIDCALL;
}
void run_shadow_alpha(Fixture& f) {
    using namespace x3m::renderer;
    D3DCAPS9 caps{}; api(f.d->GetDeviceCaps(&caps), "GetDeviceCaps");
    std::printf("ALPHA_CAPS ps=%08lx vs=%08lx ps30_slots=%lu\n", static_cast<unsigned long>(caps.PixelShaderVersion), static_cast<unsigned long>(caps.VertexShaderVersion),
                static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots));
    // Geometry: four quads (two triangles each), UV (0,0) top-left .. (1,1) bottom-right.
    Com<IDirect3DVertexBuffer9> vb; Com<IDirect3DVertexDeclaration9> declaration;
    api(f.d->CreateVertexBuffer(4 * 6 * 20, 0, 0, D3DPOOL_MANAGED, &vb.p, nullptr), "CreateVertexBuffer alpha quads");
    { float* v = nullptr; api(vb->Lock(0, 0, reinterpret_cast<void**>(&v), 0), "Lock alpha quads");
      for (const auto& q : alpha_quads) {
          const float corner[6][4] = {{q.x0, q.y1, 0, 0}, {q.x1, q.y1, 1, 0}, {q.x0, q.y0, 0, 1}, {q.x1, q.y1, 1, 0}, {q.x1, q.y0, 1, 1}, {q.x0, q.y0, 0, 1}};
          for (const auto& c : corner) { v[0] = c[0]; v[1] = c[1]; v[2] = q.z; v[3] = c[2]; v[4] = c[3]; v += 5; }
      }
      api(vb->Unlock(), "Unlock alpha quads"); }
    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
    api(f.d->CreateVertexDeclaration(elements, &declaration.p), "CreateVertexDeclaration alpha");
    // Textures (managed, one level): H's half alpha, Z's zero alpha, a hostile one for the caller's stage 0.
    Com<IDirect3DTexture9> half, zero, hostile;
    const auto texture = [&](Com<IDirect3DTexture9>& t, unsigned mode) {
        api(f.d->CreateTexture(alpha_tex_size, alpha_tex_size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr), "CreateTexture alpha");
        D3DLOCKED_RECT lock{}; api(t->LockRect(0, &lock, nullptr, 0), "LockRect alpha");
        for (unsigned y = 0; y < alpha_tex_size; ++y) for (unsigned x = 0; x < alpha_tex_size; ++x) {
            const DWORD a = mode == 0 ? (x < alpha_tex_size / 2 ? 255u : 0u) : mode == 1 ? 0u : 255u;
            reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits) + y * lock.Pitch)[x] = (a << 24) | 0x00808080u;
        }
        api(t->UnlockRect(0), "UnlockRect alpha");
    };
    texture(half, 0); texture(zero, 1); texture(hostile, 2);
    // The draws, the documented GREATEREQUAL ref 1 threshold of the game's passes.
    const float threshold = 1.f / 255.f;
    ShadowReplayDraw draws[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        auto& d = draws[i];
        d.vertex_buffer = vb.p; d.declaration = declaration.p; d.stride = 20; d.topology = D3DPT_TRIANGLELIST; d.first = 6 * i; d.primitives = 2; d.cull_mode = D3DCULL_NONE;
        if (alpha_quads[i].alpha) { d.alpha_texture = i == 1 ? static_cast<IDirect3DBaseTexture9*>(half.p) : static_cast<IDirect3DBaseTexture9*>(zero.p); d.alpha_threshold = threshold; }
    }
    ShadowReplayDraw opaque[2] = {draws[0], draws[3]};
    // One issue per draw in submission order with identity light rows c0-c2 (the pass sets c3 = (0, 0, 0, 1)).
    ShadowReplayIssue identity[4]{};
    for (unsigned i = 0; i < 4; ++i) { identity[i].draw = std::uint16_t(i); for (unsigned k = 0; k < 3; ++k) identity[i].rows[k * 4 + k] = 1.f; }
    // Hostile caller state the transaction must restore exactly.
    const auto hostile_state = [&] {
        api(f.d->SetTexture(0, hostile.p), "hostile texture 0");
        api(f.d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP), "hostile addressu"); api(f.d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR), "hostile addressv");
        api(f.d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT), "hostile minfilter"); api(f.d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT), "hostile magfilter");
        api(f.d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE), "hostile mipfilter"); api(f.d->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 3), "hostile maxmiplevel");
        const float bias = -1.5f; DWORD bias_bits; std::memcpy(&bias_bits, &bias, 4); api(f.d->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, bias_bits), "hostile lod bias");
        api(f.d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, TRUE), "hostile srgb");
        const float c0[4] = {.3f, -2.f, 7.f, 11.f}; api(f.d->SetPixelShaderConstantF(0, c0, 1), "hostile ps c0");
        api(f.d->SetPixelShader(f.flat.p), "hostile ps");
        api(f.d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE), "hostile alpha test"); api(f.d->SetRenderState(D3DRS_ALPHAREF, 200), "hostile alpha ref");
        api(f.d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_LESS), "hostile alpha func");
    };
    // One transaction inside the caller's scene with hostile state, restored exactly; the main target untouched.
    const auto transaction = [&](ShadowReplayPass& pass, const ShadowReplayDraw* list, unsigned count, const char* label, ShadowReplayResult& out) -> HRESULT {
        api(f.d->BeginScene(), "BeginScene alpha");
        hostile_state();
        const auto image = f.color_image();
        const Snapshot before = f.snapshot();
        const ShadowReplayMapList map0{0, identity, count, false};
        const HRESULT hr = pass.execute_cascades(list, count, &map0, 1, true, false, &out);
        f.compare(before, f.snapshot(), label);
        require(f.color_image() == image, "the replay leaves the main target byte-identical");
        api(f.d->EndScene(), "EndScene alpha");
        return hr;
    };
    unsigned checks = 0;
    const auto check = [&](bool condition, const char* what) { require(condition, what); ++checks; };
    // Pass A: the alpha programs requested; pass B: the depth-only pass (option off).
    ShadowReplayPass a, b;
    a.request_alpha_programs(true);
    const unsigned one_map[1] = {alpha_map_size};
    api(a.attach_cascades(f.d.p, nullptr, caps, D3DFMT_X8R8G8B8, one_map, 1), "attach alpha pass");
    api(b.attach_cascades(f.d.p, nullptr, caps, D3DFMT_X8R8G8B8, one_map, 1), "attach depth-only pass");
    check(a.caps().enabled && a.caps().alpha && a.caps().alpha_programs == S_OK && a.caps().readable, "the alpha pass attaches with its programs");
    check(b.caps().enabled && !b.caps().alpha && b.caps().alpha_programs == S_FALSE, "the depth-only pass has no alpha programs");
    check(a.references() == b.references() + 2, "the alpha pair is two more references");
    std::printf("ALPHA_DEVICE alpha=%u programs=%08lx references=%u depth_only_references=%u\n", unsigned(a.caps().alpha), static_cast<unsigned long>(a.caps().alpha_programs), a.references(), b.references());
    // A: the four draws.
    ShadowReplayResult out{};
    api(transaction(a, draws, 4, "alpha_pass", out), "alpha transaction");
    check(out.drawn == 4, "four draws issued");
    // The map's bind (SetRenderTarget, SetViewport) and Clear (3); per issue five calls; H adds the
    // pair (2), sampler 0 (8), its texture and the threshold; Z its texture (same threshold); O2 the
    // depth-only pair back (2).
    std::printf("ALPHA_CALLS state_calls=%u expected=%u\n", out.state_calls_map[0], 3u + 4u * 5u + 12u + 1u + 2u);
    check(out.state_calls_map[0] == 3u + 4u * 5u + 12u + 1u + 2u, "the issue path's native calls (no redundant alpha state)");
    const auto map_a = alpha_read_map(f, a.map_surface());
    const AlphaVerdict va = alpha_verdict(map_a);
    alpha_print("alpha", va, map_a);
    check(va.o1.total && va.o1.covered == va.o1.total && va.o2.total && va.o2.covered == va.o2.total, "the opaque quads cast in full");
    check(va.h_left.total && va.h_left.covered == va.h_left.total, "the alpha quad casts where its alpha is 255");
    check(va.h_right.total && va.h_right.covered == 0, "the alpha quad casts nothing where its alpha is 0 (a half shadow)");
    check(va.z.total && va.z.covered == 0, "the zero-alpha quad casts nothing");
    check(va.o1.depth_ok && va.h_left.depth_ok && va.o2.depth_ok, "every cast texel holds its quad's depth");
    // The alpha pass on the opaque draws alone, and the depth-only pass on the same: byte-identical maps.
    api(transaction(a, opaque, 2, "alpha_pass_opaque", out), "alpha pass, opaque draws");
    check(out.state_calls_map[0] == 3u + 2u * 5u, "no alpha state for opaque draws");
    const auto map_a_opaque = alpha_read_map(f, a.map_surface());
    api(transaction(b, opaque, 2, "depth_only_pass", out), "depth-only pass");
    const auto map_b = alpha_read_map(f, b.map_surface());
    check(map_a_opaque == map_b, "the opaque casters draw byte-identically with and without the alpha programs");
    alpha_print("opaque_alpha_pass", alpha_verdict(map_a_opaque), map_a_opaque);
    alpha_print("opaque_depth_only", alpha_verdict(map_b), map_b);
    // B refuses an alpha draw at validation: nothing touched, the map keeps its content.
    { api(f.d->BeginScene(), "BeginScene refusal"); hostile_state();
      const Snapshot before = f.snapshot();
      const ShadowReplayMapList map0{0, identity, 4, false};
      const HRESULT hr = b.execute_cascades(draws, 4, &map0, 1, true, false, &out);
      f.compare(before, f.snapshot(), "depth_only_refusal");
      api(f.d->EndScene(), "EndScene refusal");
      check(hr == E_INVALIDARG && out.failed == ShadowReplayStage::Validate && out.drawn == 0, "the pass without alpha programs refuses an alpha draw");
      check(alpha_read_map(f, b.map_surface()) == map_b, "a refused transaction leaves the map"); }
    // Cascades: two maps, map 0 [O1, H, Z] and map 1 [H, O2, Z] issued in list order with the
    // transaction's c3 (the pass keeps the pair across maps; bound back for O2).
    {
        ShadowReplayPass c;
        c.request_alpha_programs(true);
        const unsigned sizes[2] = {alpha_map_size, alpha_map_size};
        api(c.attach_cascades(f.d.p, nullptr, caps, D3DFMT_X8R8G8B8, sizes, 2), "attach alpha cascades");
        check(c.caps().alpha, "the cascade pass has the alpha programs");
        ShadowReplayIssue issues[6]{};
        const unsigned order[6] = {0, 1, 2, 1, 3, 2};
        for (unsigned i = 0; i < 6; ++i) { issues[i].draw = std::uint16_t(order[i]); for (unsigned k = 0; k < 3; ++k) issues[i].rows[k * 4 + k] = 1.f; }
        const ShadowReplayMapList lists[2] = {{0, issues, 3, false}, {1, issues + 3, 3, false}};
        api(f.d->BeginScene(), "BeginScene cascades"); hostile_state();
        const Snapshot before = f.snapshot();
        api(c.execute_cascades(draws, 4, lists, 2, true, false, &out), "alpha cascade transaction");
        f.compare(before, f.snapshot(), "alpha_cascades");
        api(f.d->EndScene(), "EndScene cascades");
        check(out.drawn_map[0] == 3 && out.drawn_map[1] == 3, "three issues per map");
        // Map 0: 3 bind + 3x5 + H 12 + Z 1; map 1: 3 bind + 3x5 + H 1 (its texture back) + O2 2 + Z 2 (pair) + 1 (texture).
        std::printf("ALPHA_CASCADE_CALLS map0=%u map1=%u\n", out.state_calls_map[0], out.state_calls_map[1]);
        check(out.state_calls_map[0] == 3u + 15u + 12u + 1u && out.state_calls_map[1] == 3u + 15u + 1u + 2u + 3u, "the cascade issue path's native calls");
        const auto m0 = alpha_read_map(f, c.map_surface(0)), m1 = alpha_read_map(f, c.map_surface(1));
        const AlphaVerdict v0 = alpha_verdict(m0), v1 = alpha_verdict(m1);
        alpha_print("cascade0", v0, m0); alpha_print("cascade1", v1, m1);
        check(v0.o1.covered == v0.o1.total && v0.h_left.covered == v0.h_left.total && v0.h_right.covered == 0 && v0.z.covered == 0 && v0.o2.covered == 0, "cascade 0: O1 and H's left half");
        check(v1.o1.covered == 0 && v1.h_left.covered == v1.h_left.total && v1.h_right.covered == 0 && v1.z.covered == 0 && v1.o2.covered == v1.o2.total, "cascade 1: H's left half and O2");
        c.detach();
        check(c.references() == 0, "the cascade pass releases everything");
    }
    // Reset: the maps and the block go, the programs stay; the same transaction afterwards is byte-identical.
    a.before_reset(); b.before_reset();
    check(a.references() == 4 && a.reset_pending(), "before_reset keeps the four programs only");
    f.reset();
    a.after_reset(S_OK); b.after_reset(S_OK);
    api(transaction(a, draws, 4, "alpha_pass_after_reset", out), "alpha transaction after Reset");
    const auto map_reset = alpha_read_map(f, a.map_surface());
    alpha_print("after_reset", alpha_verdict(map_reset), map_reset);
    check(map_reset == map_a, "the transaction after a Reset is byte-identical");
    // A refused alpha program: the pass stays enabled for the depth-only casters, holding nothing more.
    {
        void* table[119];
        std::memcpy(table, *reinterpret_cast<void* const* const*>(f.d.p), sizeof table);
        std::memcpy(&alpha_real_create_ps, &table[106], sizeof alpha_real_create_ps);
        const AlphaCreatePsFn failing = alpha_failing_create_ps; std::memcpy(&table[106], &failing, sizeof failing);
        alpha_create_ps_calls = 0;
        ShadowReplayPass r;
        r.request_alpha_programs(true);
        api(r.attach_cascades(f.d.p, table, caps, D3DFMT_X8R8G8B8, one_map, 1), "attach with the alpha program refused");
        std::printf("ALPHA_REFUSED enabled=%u alpha=%u programs=%08lx references=%u create_ps_calls=%u\n", unsigned(r.caps().enabled), unsigned(r.caps().alpha),
                    static_cast<unsigned long>(r.caps().alpha_programs), r.references(), alpha_create_ps_calls);
        check(r.caps().enabled && !r.caps().alpha && r.caps().alpha_programs == D3DERR_INVALIDCALL && alpha_create_ps_calls == 2, "a refused alpha program leaves the pass enabled without it");
        check(r.references() == 2, "the refused pair holds no reference (the created alpha vertex program is released)");
        api(transaction(r, opaque, 2, "refused_pass", out), "refused pass, opaque draws");
        check(alpha_read_map(f, r.map_surface()) == map_b, "the refused pass draws the opaque casters as the depth-only pass");
        r.detach();
    }
    a.detach(); b.detach();
    check(a.references() == 0 && b.references() == 0, "detach releases every object");
    api(f.d->SetTexture(0, nullptr), "SetTexture 0 null"); api(f.d->SetPixelShader(nullptr), "SetPixelShader null");
    std::printf("RESULT PASS checks=%u\n", checks);
}
// Route-level alpha-tested caster script ("shadowalpharoute" mode): the real
// capture path of the seam DLL with --shadow-alpha-casters on (X3M_SHADOW_ALPHA_CASTERS=1),
// the ownership wrapper, TAA, the FP16 scene and the sun-share lane (gate 4
// routes an alpha-tested draw only on the lane's tested-opaque arm or the exact
// cutout arm), the rotating camera and the narrowed 8-unit cascade 0. Per frame
// B (opaque control) then A, the shadow-replay script's two casters at their rows;
// A's TEXCOORD0.x runs 0 -> 1 along object x from -1 to 3, so a texture whose alpha
// is 255 for u < .5 keeps the part x < 1 (three quarters of A). Frames:
//   0 A opaque (alpha test off): the pass attaches at this scene end;
//   1, 2 A alpha-tested ALPHAFUNC (GREATEREQUAL, or LESS in the negative twin) ref 128, the half texture (managed);
//   3 the same test with a DEFAULT-pool texture (refused_pool; LESS: refused_function);
//   a Reset precedes frame 4 (the leases retire, the alpha programs stay);
//   4 as 1; 5 the zero-alpha texture (admitted, casts nothing).
// The map is read back after every Present (shadow_<frame>.r32f with SHADOW_MAP
// and SHADOW_CAMERA lines) for the runner's CPU projection; the DLL's per-frame
// shadow_alpha_casters and shadow_replay_candidates lines carry the admission.
constexpr unsigned alpha_route_frames = 6, alpha_route_reset_before = 4;
void run_shadow_alpha_route(Fixture& f) {
    require(f.seam && f.enabled && f.camera && f.taa && f.hdr, "shadowalpharoute runs on the seam DLL with the route, the camera, TAA and the FP16 scene");
    char setting[16]{};
    const bool less = GetEnvironmentVariableA("X3M_FIXTURE_ALPHA_FUNC", setting, sizeof setting) == 4 && !std::strcmp(setting, "less");
    const auto readback = symbol<ShadowReadbackFn>(f.runtime, "x3m_shadow_replay_fixture_readback", false);
    require(readback != nullptr, "the seam DLL exports the map readback");
    // Textures: texel columns 0-31 and 63 at alpha 255 (63 keeps u = 0 above the reference under WRAP), the rest 0.
    Com<IDirect3DTexture9> half, zero, pooled;
    const auto managed = [&](Com<IDirect3DTexture9>& t, bool keep) {
        api(f.d->CreateTexture(alpha_tex_size, alpha_tex_size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t.p, nullptr), "CreateTexture route alpha");
        D3DLOCKED_RECT lock{}; api(t->LockRect(0, &lock, nullptr, 0), "LockRect route alpha");
        for (unsigned y = 0; y < alpha_tex_size; ++y) for (unsigned x = 0; x < alpha_tex_size; ++x)
            reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits) + y * lock.Pitch)[x] = ((keep && (x < alpha_tex_size / 2 || x == alpha_tex_size - 1)) ? 0xff000000u : 0u) | 0x00606060u;
        api(t->UnlockRect(0), "UnlockRect route alpha");
    };
    managed(half, true); managed(zero, false);
    api(f.d->CreateTexture(alpha_tex_size, alpha_tex_size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pooled.p, nullptr), "CreateTexture DEFAULT pool");
    std::printf("ALPHA_ROUTE_MODE func=%s ref=128 frames=%u reset_before=%u\n", less ? "less" : "greaterequal", alpha_route_frames, alpha_route_reset_before);
    shadow_ensure_bloom(f);
    for (unsigned frame = 0; frame < alpha_route_frames; ++frame) {
        if (frame == alpha_route_reset_before) { f.reset(); shadow_ensure_bloom(f); }
        const char* kind = frame == 0 ? "opaque" : frame == 3 ? "default_pool" : frame == 5 ? "zero" : "half";
        IDirect3DBaseTexture9* texture = frame == 0 ? nullptr : frame == 3 ? static_cast<IDirect3DBaseTexture9*>(pooled.p) : frame == 5 ? static_cast<IDirect3DBaseTexture9*>(zero.p) : static_cast<IDirect3DBaseTexture9*>(half.p);
        f.frame_begin();
        api(f.d->SetPixelShaderConstantF(4, shadow_sun, 1), "SetPixelShaderConstantF LightDir_Dir0");
        const auto& cam = f.camera_current;
        std::printf("SHADOW_CAMERA frame=%llu m00=%.9g m11=%.9g r=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g t=%.9g,%.9g,%.9g\n", f.frame, cam.m00, cam.m11,
                    cam.r[0], cam.r[1], cam.r[2], cam.r[3], cam.r[4], cam.r[5], cam.r[6], cam.r[7], cam.r[8], cam.t[0], cam.t[1], cam.t[2]);
        std::printf("SHADOW_SUN frame=%llu direction=%.9g,%.9g,%.9g\n", f.frame, shadow_sun[0], shadow_sun[1], shadow_sun[2]);
        const bool matched = f.frames_since_reset > 0;
        std::printf("SHADOW_DRAW frame=%llu caster=1 shape=B t=%.9g p=%.9g zo=%.9g\n", f.frame, -.05, .125, .05);
        f.draw(f.b, -.05f, .125f, .05f, true, true, matched, Alter::None, false);
        if (texture) {
            api(f.d->SetTexture(0, texture), "SetTexture alpha source");
            api(f.d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE), "alpha test on");
            api(f.d->SetRenderState(D3DRS_ALPHAFUNC, less ? D3DCMP_LESS : D3DCMP_GREATEREQUAL), "alpha func");
            api(f.d->SetRenderState(D3DRS_ALPHAREF, 128), "alpha ref");
        }
        std::printf("ALPHA_ROUTE_DRAW frame=%llu kind=%s alpha_test=%u func=%s\n", f.frame, kind, unsigned(texture != nullptr), less ? "less" : "greaterequal");
        std::printf("SHADOW_DRAW frame=%llu caster=0 shape=A t=%.9g p=%.9g zo=%.9g\n", f.frame, .8, .125, 0.);
        f.draw(f.a, .8f, .125f, 0.f, true, true, matched, Alter::None, false);
        if (texture) {
            api(f.d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE), "alpha test off");
            api(f.d->SetTexture(0, f.textures[0].p), "SetTexture stage 0 back");
        }
        f.decide();
        api(f.d->SetDepthStencilSurface(nullptr), "alpha route scene-end detach depth");
        api(f.d->StretchRect(f.back.p, nullptr, f.bloom_surface.p, nullptr, D3DTEXF_NONE), "alpha route scene end and TAA");
        api(f.d->EndScene(), "alpha route EndScene");
        api(f.d->SetDepthStencilSurface(f.depth.p), "alpha route depth rebind");
        api(f.d->Present(nullptr, nullptr, nullptr, nullptr), "alpha route Present");
        const unsigned long long ended = f.frame;
        ++f.frame; ++f.frames_since_reset; f.camera_history = f.camera_current;
        shadow_readback(f, readback, ended);
        if (frame == 3) pooled.reset(); // DEFAULT pool: gone before the Reset
    }
    std::puts("ALPHA_ROUTE PASS");
}
} // namespace
