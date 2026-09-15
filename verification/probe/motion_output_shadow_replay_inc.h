// Cascade-0 depth replay script ("shadowreplay" mode; docs/architecture/
// shadow-replay-gates.md, "Implemented: cascade-0 depth replay fixture").
// The seam camera script with N managed casters (X3M_FIXTURE_SHADOW_CASTERS,
// default 2) drawn every frame at fixed rows under a fixed world sun
// direction uploaded as LightDir_Dir0 (PS c4); the DLL replays the quiet
// candidates into its private map at the scene end (the bloom copy, with or
// without the resolve). Frame 2 issues a READONLY Lock/Unlock of caster 1
// between its draw and the scene end (lease proof fails: the frame's replay is
// refused and the map keeps frame 1's content); a Reset precedes frame 4 (the
// map is recreated); frame 5 writes no LightDir_Dir0 (refused no_sun); frame 7
// draws caster 0 with a two-stream declaration (refused multistream). After
// every Present the map is read back through the
// seam export and written beside the executable (shadow_<frame>.r32f) with the
// projection basis, for the runner's CPU projection of the same geometry. The
// presented frames are written as in every mode for the byte-identical twins.
namespace {
constexpr float shadow_tri_a[3][2] = {{-1, 1}, {3, 1}, {-1, -3}}, shadow_tri_b[3][2] = {{-.9f, .9f}, {-.3f, .9f}, {-.9f, .3f}};
constexpr float shadow_sun[4] = {0.30151134f, 0.90453403f, -0.30151134f, 0.f}; // (1, 3, -1) / sqrt(11): unit, object -> light, world space
constexpr unsigned shadow_frames = 8, shadow_lock_frame = 2, shadow_reset_before = 4, shadow_no_sun_frame = 5, shadow_multistream_frame = 7;
struct ShadowCaster { Object object{""}; float t = 0, p = 0, zo = 0; Com<IDirect3DVertexBuffer9> buffer; };
using ShadowReadbackFn = HRESULT (*)(IDirect3DDevice9*, float*, unsigned, unsigned*, unsigned*, float*, unsigned);
// The application's bloom source when the script runs without the resolve.
void shadow_ensure_bloom(Fixture& f) {
    if (f.bloom.p) return;
    api(f.d->CreateTexture(Fixture::W, Fixture::H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &f.bloom.p, nullptr), "CreateTexture bloom");
    api(f.bloom->GetSurfaceLevel(0, &f.bloom_surface.p), "bloom level");
}
// The scene end without a resolve: the game's depth unbind and bloom copy.
// The replay runs inside the copy's hook; every watched state and the main
// target must come out unchanged.
void shadow_bloom_copy(Fixture& f) {
    api(f.d->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface null");
    const auto before_image = f.color_image();
    const Snapshot before = f.snapshot();
    api(f.d->StretchRect(f.back.p, nullptr, f.bloom_surface.p, nullptr, D3DTEXF_NONE), "StretchRect bloom copy");
    f.compare(before, f.snapshot(), "boundary");
    require(f.color_image() == before_image, "the scene-end copy without a resolve leaves the main target byte-identical");
    require(f.color_image(f.bloom_surface.p) == before_image, "the application's bloom copy receives the main target");
}
void shadow_frame_end(Fixture& f) {
    if (f.taa) f.boundary(); else shadow_bloom_copy(f);
    api(f.d->EndScene(), "EndScene");
    const auto image = f.color_image();
    std::printf("COLOR frame=%llu hash=%016llx\n", f.frame, static_cast<unsigned long long>(Fixture::color_hash(image)));
    f.write_presented(image);
    if (!f.taa) f.verify_coverage(image);
    f.previous_presented = image;
    f.verify_motion();
    api(f.d->SetDepthStencilSurface(f.depth.p), "SetDepthStencilSurface rebind");
    api(f.d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
    ++f.frame; ++f.frames_since_reset;
}
// The map after the Present: the seam export's floats and basis, or
// available=0 when nothing has been replayed yet (option off, before the
// first replay, or an unreadable fallback format).
void shadow_readback(Fixture& f, ShadowReadbackFn readback, unsigned long long frame) {
    if (!readback) { std::printf("SHADOW_MAP frame=%llu available=0 reason=no_export\n", frame); return; }
    unsigned w = 0, h = 0; float params[16]{};
    HRESULT hr = readback(f.d.p, nullptr, 0, &w, &h, params, 16);
    if (hr == D3DERR_NOTFOUND || hr == D3DERR_NOTAVAILABLE) { std::printf("SHADOW_MAP frame=%llu available=0 reason=%s\n", frame, hr == D3DERR_NOTFOUND ? "no_map" : "unreadable"); return; }
    require(hr == D3DERR_MOREDATA && w == h && w >= 64, "the map readback reports its size");
    std::vector<float> map(std::size_t(w) * h);
    api(readback(f.d.p, map.data(), unsigned(map.size()), &w, &h, params, 16), "shadow map readback");
    char name[64]; std::snprintf(name, sizeof name, "shadow_%llu.r32f", frame);
    FILE* file = std::fopen(name, "wb"); require(file != nullptr, "shadow map written");
    std::fwrite(map.data(), 4, map.size(), file); std::fclose(file);
    std::printf("SHADOW_MAP frame=%llu available=1 width=%u height=%u right=%.9g,%.9g,%.9g up=%.9g,%.9g,%.9g forward=%.9g,%.9g,%.9g center=%.9g,%.9g,%.9g extent=%.9g depth_half=%.9g valid=%g\n",
                frame, w, h, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8], params[9], params[10], params[11], params[12], params[13], params[15]);
}
void run_shadow_replay_integration(Fixture& f) {
    require(f.seam && f.camera && f.enabled, "shadowreplay runs on the seam DLL with the route and the rotating camera");
    char setting[16]{};
    const bool depth_on = GetEnvironmentVariableA("X3M_SHADOW_REPLAY_DEPTH", setting, sizeof setting) == 1 && setting[0] == '1';
    unsigned casters = 2;
    if (GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_CASTERS", setting, sizeof setting) > 0) { const int n = std::atoi(setting); if (n >= 2 && n <= 64) casters = unsigned(n); }
    const auto readback = symbol<ShadowReadbackFn>(f.runtime, "x3m_shadow_replay_fixture_readback", false);
    std::printf("SHADOW_MODE depth=%u casters=%u taa=%u export=%u\n", depth_on, casters, f.taa, readback != nullptr);
    // Casters: A and B, then copies of their shapes in their own managed
    // buffers (distinct route keys) with their own scope identities.
    std::vector<ShadowCaster> extra(casters > 2 ? casters - 2 : 0);
    for (unsigned i = 2; i < casters; ++i) {
        auto& c = extra[i - 2]; const bool b = i & 1; const auto& tri = b ? shadow_tri_b : shadow_tri_a;
        api(f.d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &c.buffer.p, nullptr), "CreateVertexBuffer caster");
        void* dst = nullptr; api(c.buffer->Lock(0, 0, &dst, 0), "Lock caster");
        for (UINT v = 0; v < 3; ++v) {
            unsigned short data[12] = {half(tri[v][0]), half(tri[v][1]), half(.5f), half(7), half(float(v & 1)), half(float(v >> 1)), 0, half(7), 0, 0, half(1), half(7)};
            std::memcpy(static_cast<char*>(dst) + v * 24, data, 24);
        }
        api(c.buffer->Unlock(), "Unlock caster");
        c.object.name = b ? "B" : "A"; c.object.vb = c.buffer.p; c.object.covers = b ? covers_b : covers_a;
        c.object.scope = b ? f.b.scope : f.a.scope;
        c.object.scope.node_serial = 11 + i; c.object.scope.node = 0x1000 + 0x100 * i; c.object.scope.mesh = 0x4000 + 0x100 * i;
        c.object.scope.node_handle = 7 + i; c.object.scope.model = 0x11 + i;
    }
    auto caster = [&](unsigned i) -> Object& { return i == 0 ? f.a : i == 1 ? f.b : extra[i - 2].object; };
    // Every caster shares the tilt p, so the planes are parallel and never
    // cross: the front-most draw of a texel is unambiguous for both oracles
    // (crossing planes put every intersection line on the sub-texel snap).
    auto rows_of = [&](unsigned i, float& t, float& p, float& zo) {
        const bool b = i & 1; const float k = float(i / 2);
        t = (b ? -.05f : .8f) - .1f * k; p = .125f; zo = .05f * float(i);
    };
    // A declaration that also reads stream 1 (TEXCOORD1, unused by the
    // reviewed program): the route keys the draw; the replay must refuse it.
    Com<IDirect3DVertexDeclaration9> two_streams;
    {
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 8, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            {0, 16, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
            {1, 8, D3DDECLTYPE_FLOAT16_4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1}, D3DDECL_END()};
        api(f.d->CreateVertexDeclaration(elements, &two_streams.p), "CreateVertexDeclaration two streams");
    }
    shadow_ensure_bloom(f);
    for (unsigned frame = 0; frame < shadow_frames; ++frame) {
        if (frame == shadow_reset_before) { f.reset(); shadow_ensure_bloom(f); }
        const bool no_sun = frame == shadow_no_sun_frame, multistream = frame == shadow_multistream_frame;
        f.skip_c4 = no_sun;
        f.frame_begin();
        f.skip_c4 = false;
        if (!no_sun) api(f.d->SetPixelShaderConstantF(4, shadow_sun, 1), "SetPixelShaderConstantF LightDir_Dir0");
        const auto& cam = f.camera_current;
        std::printf("SHADOW_CAMERA frame=%llu m00=%.9g m11=%.9g r=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g t=%.9g,%.9g,%.9g\n", f.frame, cam.m00, cam.m11,
                    cam.r[0], cam.r[1], cam.r[2], cam.r[3], cam.r[4], cam.r[5], cam.r[6], cam.r[7], cam.r[8], cam.t[0], cam.t[1], cam.t[2]);
        std::printf("SHADOW_SUN frame=%llu direction=%.9g,%.9g,%.9g\n", f.frame, shadow_sun[0], shadow_sun[1], shadow_sun[2]);
        const bool matched = f.frames_since_reset > 0;
        for (unsigned i = 0; i < casters; ++i) {
            float t, p, zo; rows_of(i, t, p, zo);
            std::printf("SHADOW_DRAW frame=%llu caster=%u shape=%s t=%.9g p=%.9g zo=%.9g\n", f.frame, i, (i & 1) ? "B" : "A", t, p, zo);
            const bool other_declaration = multistream && i == 0;
            if (other_declaration) {
                api(f.d->SetStreamSource(1, f.vb_a.p, 0, 24), "SetStreamSource stream 1");
                api(f.d->SetVertexDeclaration(two_streams.p), "SetVertexDeclaration two streams");
            }
            // A new declaration is a new route key: that draw has no history (unmatched).
            f.draw(caster(i), t, p, zo, true, true, matched && !other_declaration, Alter::None, casters <= 8);
            if (other_declaration) {
                api(f.d->SetVertexDeclaration(f.declaration.p), "SetVertexDeclaration restore");
                api(f.d->SetStreamSource(1, nullptr, 0, 0), "SetStreamSource stream 1 unbound");
            }
        }
        const bool lock = frame == shadow_lock_frame;
        if (lock) {
            // The lease proof: a READONLY Lock between the draw and the scene
            // end moves the attempt serial (no revision, so the route's
            // history is unaffected); the counter refuses the record.
            void* data = nullptr;
            api(f.vb_b->Lock(0, 0, &data, D3DLOCK_READONLY), "READONLY Lock of caster 1 after its draw");
            api(f.vb_b->Unlock(), "Unlock of caster 1");
        }
        std::printf("SHADOW_EXPECT frame=%llu casters=%u lease_refused=%u after_reset=%u no_sun=%u multistream=%u\n", f.frame, casters, lock, frame == shadow_reset_before, no_sun, multistream);
        const unsigned long long ended = f.frame;
        shadow_frame_end(f);
        shadow_readback(f, readback, ended);
    }
    require(frames_verified == shadow_frames, "every frame verified against the oracle");
    if (f.taa) require(taa_frames == shadow_frames && taa_reference_frames == shadow_frames, "every frame ran the boundary and compared against the reference resolve");
    for (auto& c : extra) c.buffer.reset();
}
} // namespace
