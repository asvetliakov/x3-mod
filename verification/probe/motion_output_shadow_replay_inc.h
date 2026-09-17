// Cascade-0 depth replay script ("shadowreplay" mode; docs/architecture/
// shadow-replay-gates.md, "Implemented: cascade-0 depth replay fixture").
// The seam camera script with N managed casters (X3M_FIXTURE_SHADOW_CASTERS,
// default 2) drawn every frame at fixed rows under a fixed world sun
// direction uploaded as LightDir_Dir0 (PS c4); the DLL replays the quiet
// candidates into its private map at the scene end (the bloom copy, with or
// without the resolve). Frame 2 issues a READONLY Lock/Unlock of a caster
// between its draw and the scene end (caster 0 since 2026-09-17: the cap drops the last caster; lease proof fails: the frame's replay is
// refused and the map keeps frame 1's content); a Reset precedes frame 4 (the
// map is recreated); frame 5 writes another unit direction to LightDir_Dir0
// for that one frame (every sample disagrees with the latched sun: refused
// sun_changing, the latch keeps the sector's sun; before the run-38 fix this
// frame wrote no c4 and was refused no_sun, but the device register still
// holds the sun then, and the per-program sample now sees it); frame 7
// draws caster 0 with a two-stream declaration (refused multistream). With
// X3M_FIXTURE_SHADOW_SUN_PROGRAMS=<directory> two more reviewed pairs are
// drawn first on every frame (docs/verification/directional-shadows.md, "Run
// 38 A (run111) diagnosis", cause 1): G, a glow-style pair whose c4 is
// g_EnableGlow = (1, 0, 0, 0) and whose LightDir_Dir0 is c5, then D, a
// detail-style pair whose c4 is p_DetailMapBlendWeight and whose
// LightDir_Dir0 is c0. Both lie where F lies (outside every box, invisible);
// they are routed z-writers, so they feed the frame's sun: the latch takes the
// true sun from c5 on frame 0 and the map's basis never follows c4. Two
// bounds objects join the casters every frame (casters by bounds): L, drawn
// first, a large triangle whose origin lies 256 units to the camera's right
// (outside the origin rule's 250 units, the run-36 station case) but whose
// vertices cross the 8-unit box, and F, drawn last, a small triangle whose
// origin lies 300 units away and whose vertices lie 225 units away (outside
// both rules; X3M_FIXTURE_SHADOW_FAR_T moves it: t = 720 puts the origin at
// 900 units and the vertices at 825, outside the default 250-unit box and
// inside a 1000-unit one). Neither shows on the presented frame: L lies beyond the far
// plane (clip z / w > 1, its tilt .004 keeps its plane from crossing the
// casters' planes inside their triangles), F behind the camera (w < 0). The
// DLL learns every extent at frame 0's scene end; frame 0 admits the casters
// alone (origin rule), later frames L and the casters by bounds, and the
// per-frame cap X3M_SHADOW_REPLAY_CAP = casters drops the last caster on
// those frames (capped=1). After every Present the map is read back through the
// seam export and written beside the executable (shadow_<frame>.r32f) with the
// projection basis, for the runner's CPU projection of the same geometry. The
// presented frames are written as in every mode for the byte-identical twins.
// X3M_FIXTURE_SHADOW_POLL (cascades; docs/verification/directional-shadows.md,
// "Sun at finite distance"): the fixture's own render-context block stands in
// for *0x00608518 (x3m_sun_light_poll_fixture_install): a light-candidate
// array of a bright point light, the sun (directional, luma 187), a dimmer
// directional fill and a forced-directional node at the origin (the 0x00420260
// hazard), slot count 8. `agree`: the sun node sits 100,000 units along the
// script's sun from the world origin and every draw uploads LightDir_Dir0 =
// normalize(light - its own origin), as the engine does: the DLL validates the
// poll against those constants and builds every cascade's basis from
// normalize(light - cascade centre). `disagree`: the node sits elsewhere while
// the draws keep the script's constant sun: the poll is refused (disagrees,
// then cooldown) and the latch stays the source. `null`: the context pointer is
// null (before the engine's constructor): unavailable, the latch stays.
// `refusals`: the block changes before every frame: slot count 7, no
// terminator, an entry without the light bit (three layout refusals), an
// unreadable node pointer, no directional light, then a null context.
namespace {
struct ShadowPollContext {
    std::vector<unsigned char> context = std::vector<unsigned char>(0x6290, 0);
    std::vector<unsigned char> nodes[4];
    float record[0x70 / 4]{};
    std::uint32_t slot = 0;
    static void put32(std::vector<unsigned char>& b, std::size_t at, std::uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
    static void put16(std::vector<unsigned char>& b, std::size_t at, std::int16_t v) { std::memcpy(b.data() + at, &v, 2); }
    void node(unsigned i, std::uint32_t flags, const double world[3], int r, int g, int b) {
        nodes[i].assign(0x170, 0);
        put32(nodes[i], 0x12c, flags);
        for (unsigned k = 0; k < 3; ++k) put32(nodes[i], 0xb0 + 4 * k, std::uint32_t(std::int32_t(std::llround(world[k] * 100.))));
        put16(nodes[i], 0x150, std::int16_t(r)); put16(nodes[i], 0x152, std::int16_t(g)); put16(nodes[i], 0x154, std::int16_t(b));
        put32(context, 0x5e8c + 4 * i, std::uint32_t(reinterpret_cast<std::uintptr_t>(nodes[i].data())));
    }
    void build(const double light[3], bool null_context) {
        const double lamp[3] = {3., 2., 1.}, fill[3] = {-40000., 90000., 20000.}, origin[3] = {0., 0., 0.};
        node(0, 0x400004u, lamp, 255, 255, 255);   // a point light: brighter, never directional
        node(1, 0x800004u, light, 190, 190, 160);  // the sun
        node(2, 0x800004u, fill, 60, 60, 80);      // a second directional light
        node(3, 0x800004u, origin, 128, 128, 128); // the secondary scene's forced-directional node
        for (unsigned k = 0; k < 3; ++k) record[0x34 / 4 + k] = float(light[k]);
        put32(nodes[1], 0x16c, std::uint32_t(reinterpret_cast<std::uintptr_t>(record)));
        put32(context, 0x6288, 8);
        slot = null_context ? 0u : std::uint32_t(reinterpret_cast<std::uintptr_t>(context.data()));
    }
    // The refusals script: one defect per frame on a freshly built block.
    const char* refuse(unsigned frame, const double light[3]) {
        std::fill(context.begin(), context.end(), static_cast<unsigned char>(0));
        build(light, frame >= 5);
        switch (frame) {
        case 0: put32(context, 0x6288, 7); return "layout";                                                   // not the context this reader knows
        case 1: for (unsigned i = 0; i < 255; ++i) put32(context, 0x5e8c + 4 * i, std::uint32_t(reinterpret_cast<std::uintptr_t>(nodes[1].data()))); return "layout"; // no terminator
        case 2: put32(nodes[3], 0x12c, 0x800000u); return "layout";                                           // an entry that is not a light
        case 3: put32(context, 0x5e8c, 0x10u); return "unreadable";                                           // a node pointer into the null page
        case 4: for (unsigned i = 1; i < 4; ++i) put32(nodes[i], 0x12c, 0x400004u); return "no_directional";  // point lights only (range 0)
        default: return "null_context";
        }
    }
};
constexpr double shadow_poll_distance = 100000.;
constexpr float shadow_tri_a[3][2] = {{-1, 1}, {3, 1}, {-1, -3}}, shadow_tri_b[3][2] = {{-.9f, .9f}, {-.3f, .9f}, {-.9f, .3f}};
constexpr float shadow_tri_l[3][2] = {{-214, 8}, {-214, -8}, {-195, 0}}, shadow_tri_f[3][2] = {{-60, 4}, {-60, -4}, {-56, 0}};
constexpr float shadow_t_l = 204.8f, shadow_p_l = .004f; // L: origin at view x = 204.8 / m00 (0.8) = 256 units; vertices at view x -11.5..12.25, w .14..0.22
constexpr float shadow_t_f = 240.f;                    // F: origin at 300 units; vertices at view x 225..230, w -6.5 (behind the camera)
float shadow_t_far = shadow_t_f;                       // F's row offset this run (X3M_FIXTURE_SHADOW_FAR_T, 240..4000)
// Coverage on the presented frame: the triangle test at the raster sample's
// object point. L's points have z / w > 1 (the oracles' depth test rejects
// them as the far plane does); F's inverse-mapped points lie behind the
// camera (w = 1 + .125 x <= 0), where the GPU clips.
bool covers_l(double ox, double oy) { return ox >= -214 && oy <= 8 - 8 * (ox + 214) / 19 && oy >= -8 + 8 * (ox + 214) / 19; }
bool covers_f(double ox, double oy) { return 1 + .125 * ox > 0 && ox >= -60 && oy <= 4 - (ox + 60) && oy >= -4 + (ox + 60); }
// The own-ship hulls of the adaptive cascade-0 cases (X3M_FIXTURE_OWN_SHIP;
// docs/architecture/shadow-cascade-extents.md, section 5): H1, a fighter whose
// AABB is +-1 x +-1.5 (radius about 1.7 through the rows: x / m00, y / m11,
// the .125 tilt), and H2, a capital at +-20 x +-30 (about 34). Both share the
// casters' tilt (parallel planes, never crossing) and sit near the camera
// (origins at 1.1 units); their zo = 4 puts every point with w > 0 beyond the
// far plane (z / w = 4.5 / w > 1 for w <= 3.5), so neither shows on the
// presented frame, while the replay's linear rows put them on every map they meet.
constexpr float shadow_tri_h1[3][2] = {{-1, -1.5f}, {1, -1.5f}, {0, 1.5f}}, shadow_tri_h2[3][2] = {{-20, -30}, {20, -30}, {0, 30}};
constexpr float shadow_t_h1 = .4f, shadow_t_h2 = -.4f, shadow_zo_h = 4.f;
bool covers_tri(const float (&tri)[3][2], double ox, double oy) {
    if (!(1 + .125 * ox > 0)) return false;
    double sign = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const double ax = tri[i][0], ay = tri[i][1], bx = tri[(i + 1) % 3][0], by = tri[(i + 1) % 3][1];
        const double cross = (bx - ax) * (oy - ay) - (by - ay) * (ox - ax);
        if (cross == 0) continue;
        if (sign == 0) sign = cross; else if ((cross > 0) != (sign > 0)) return false;
    }
    return true;
}
bool covers_h1(double ox, double oy) { return covers_tri(shadow_tri_h1, ox, oy); }
bool covers_h2(double ox, double oy) { return covers_tri(shadow_tri_h2, ox, oy); }
constexpr float shadow_sun[4] = {0.30151134f, 0.90453403f, -0.30151134f, 0.f}; // (1, 3, -1) / sqrt(11): unit, object -> light, world space
constexpr float shadow_sun_flip[4] = {1.f, 0.f, 0.f, 0.f};                     // the one-frame disagreement of the sun-changing frame
constexpr float shadow_glow_c4[4] = {1.f, 0.f, 0.f, 0.f}, shadow_detail_c4[4] = {.3f, .2f, .7f, 0.f}; // what c4 means to the glow and detail programs
constexpr float shadow_t_g = 260.f, shadow_t_d = 280.f;                        // G and D: F's geometry at their own rows
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
// Cascades on (X3M_SHADOW_CASCADES): every cascade's map through the per-cascade
// seam export with the basis the DLL retained for it (valid=0 while absent:
// not replayed since the last Reset or refusal, or skipped by the budget before
// any replay) and the frame that basis is from.
using ShadowCascadeReadbackFn = HRESULT (*)(IDirect3DDevice9*, unsigned, float*, unsigned, unsigned*, unsigned*, float*, unsigned);
void shadow_cascade_readback(Fixture& f, ShadowCascadeReadbackFn readback, unsigned cascades, unsigned long long frame) {
    for (unsigned c = 0; c < cascades; ++c) {
        unsigned w = 0, h = 0; float params[19]{};
        HRESULT hr = readback(f.d.p, c, nullptr, 0, &w, &h, params, 19);
        if (hr == D3DERR_NOTFOUND || hr == D3DERR_NOTAVAILABLE) { std::printf("SHADOW_CASCADE_MAP frame=%llu cascade=%u available=0 valid=0\n", frame, c); continue; }
        require(hr == D3DERR_MOREDATA && w == h && w >= 64, "the cascade map readback reports its size");
        std::vector<float> map(std::size_t(w) * h);
        api(readback(f.d.p, c, map.data(), unsigned(map.size()), &w, &h, params, 19), "cascade map readback");
        char name[64]; std::snprintf(name, sizeof name, "shadow_%llu_c%u.r32f", frame, c);
        FILE* file = std::fopen(name, "wb"); require(file != nullptr, "cascade map written");
        std::fwrite(map.data(), 4, map.size(), file); std::fclose(file);
        std::printf("SHADOW_CASCADE_MAP frame=%llu cascade=%u available=1 width=%u height=%u right=%.9g,%.9g,%.9g up=%.9g,%.9g,%.9g forward=%.9g,%.9g,%.9g center=%.9g,%.9g,%.9g extent=%.9g depth_light=%.9g depth_behind=%.9g valid=%g replayed_frame=%g\n",
                    frame, c, w, h, params[0], params[1], params[2], params[3], params[4], params[5], params[6], params[7], params[8], params[9], params[10], params[11], params[12], params[16], params[17], params[15], params[18]);
    }
}
// The at-rest A/B (docs/architecture/comparison-hotkeys.md, "Sun shadows at
// rest"): X3M_FIXTURE_SHADOW_TOGGLE=<off frame>,<on frame> presses the seam
// that the production Ctrl+Shift+F12 path reaches, at the same frame boundary
// (after the previous Present, before this frame's first draw). The frames in
// between must show no replay at all, and the frame back on must replay every
// cascade, the far one included.
using ShadowToggleFn = int (*)(IDirect3DDevice9*);
// The own-ship seam (X3M_FIXTURE_OWN_SHIP=small|big|swap): H1 and H2 are drawn
// every frame with their own scope nodes; the seam names H1 (small), H2 (big)
// or H1 until frame shadow_own_swap_frame and H2 from it (swap) as the player
// ship before each frame's draws, the way the registry walk would.
using ShadowOwnShipFn = int (*)(IDirect3DDevice9*, std::uintptr_t, std::uint32_t);
constexpr unsigned shadow_own_swap_frame = 5;
void run_shadow_replay_integration(Fixture& f) {
    require(f.seam && f.camera && f.enabled, "shadowreplay runs on the seam DLL with the route and the rotating camera");
    char setting[16]{};
    const bool depth_on = GetEnvironmentVariableA("X3M_SHADOW_REPLAY_DEPTH", setting, sizeof setting) == 1 && setting[0] == '1';
    unsigned casters = 2;
    if (GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_CASTERS", setting, sizeof setting) > 0) { const int n = std::atoi(setting); if (n >= 2 && n <= 64) casters = unsigned(n); }
    shadow_t_far = shadow_t_f;
    if (GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_FAR_T", setting, sizeof setting) > 0) { const float t = std::strtof(setting, nullptr); if (t >= shadow_t_f && t <= 4000.f) shadow_t_far = t; }
    const auto readback = symbol<ShadowReadbackFn>(f.runtime, "x3m_shadow_replay_fixture_readback", false);
    const auto cascade_readback = symbol<ShadowCascadeReadbackFn>(f.runtime, "x3m_shadow_replay_fixture_cascade_readback", false);
    unsigned cascades = 0; // the configured cascade count: the commas of X3M_SHADOW_CASCADES plus one ("0" or unset: none)
    { char list[128]{}; const DWORD n = GetEnvironmentVariableA("X3M_SHADOW_CASCADES", list, sizeof list);
      if (n > 0 && n < sizeof list && !(n == 1 && list[0] == '0')) { cascades = 1; for (const char* c = list; *c; ++c) cascades += *c == ','; } }
    require(!cascades || cascade_readback != nullptr, "the seam DLL exports the per-cascade readback");
    // The at-rest A/B seam: the frames the toggle is pressed on, alternating
    // off, on, off, on ... from the first.
    unsigned toggle_frames[shadow_frames]{}, toggle_count = 0;
    ShadowToggleFn toggle = nullptr;
    { char list[64]{};
      if (GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_TOGGLE", list, sizeof list) > 0) {
          for (const char* c = list; *c;) {
              char* stop = nullptr;
              const unsigned long v = std::strtoul(c, &stop, 10);
              require(stop != c && toggle_count < shadow_frames && v < shadow_frames && (!toggle_count || v > toggle_frames[toggle_count - 1]),
                      "X3M_FIXTURE_SHADOW_TOGGLE is increasing frame numbers inside the script, off first");
              toggle_frames[toggle_count++] = unsigned(v);
              c = *stop == ',' ? stop + 1 : stop;
          }
          require(toggle_count > 0, "X3M_FIXTURE_SHADOW_TOGGLE lists at least one press");
          toggle = symbol<ShadowToggleFn>(f.runtime, "x3m_sun_shadow_fixture_toggle", false);
          require(toggle != nullptr, "the seam DLL exports the sun-shadow A/B toggle");
          std::printf("SHADOW_TOGGLE_SCRIPT presses=%u first=%u\n", toggle_count, toggle_frames[0]);
      } }
    // The sun-position poll seam.
    char poll_mode[16]{};
    const bool poll = GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_POLL", poll_mode, sizeof poll_mode) > 0;
    const bool poll_agree = poll && !std::strcmp(poll_mode, "agree"), poll_null = poll && !std::strcmp(poll_mode, "null"), poll_refusals = poll && !std::strcmp(poll_mode, "refusals");
    static ShadowPollContext poll_context; // the DLL reads it until the process ends
    double poll_light[3] = {0., 0., 0.};
    if (poll) {
        require(GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_SUN_PROGRAMS", nullptr, 0) == 0, "the poll seam runs without the sun-register pairs");
        require(cascades != 0 && (poll_agree || poll_null || poll_refusals || !std::strcmp(poll_mode, "disagree")), "the poll seam runs with cascades in one of its four modes");
        const auto install = symbol<void (*)(const std::uint32_t*)>(f.runtime, "x3m_sun_light_poll_fixture_install", false);
        require(install != nullptr, "the seam DLL exports the sun-position poll seam");
        const double elsewhere[3] = {-shadow_sun[0], shadow_sun[1], -shadow_sun[2]}; // 35 degrees from the script's sun
        for (unsigned k = 0; k < 3; ++k) poll_light[k] = shadow_poll_distance * (poll_agree || poll_null || poll_refusals ? double(shadow_sun[k]) : elsewhere[k]);
        poll_context.build(poll_light, poll_null);
        install(&poll_context.slot);
        std::printf("SHADOW_POLL mode=%s light=%.9g,%.9g,%.9g candidates=4 directional=3\n", poll_mode, poll_light[0], poll_light[1], poll_light[2]);
    }
    // The sun-register programs: two more reviewed pairs from the local dumps.
    Com<IDirect3DVertexShader9> glow_vs, detail_vs; Com<IDirect3DPixelShader9> glow_ps, detail_ps;
    char programs[260]{};
    const bool sun_programs = GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_SUN_PROGRAMS", programs, sizeof programs) > 0;
    if (sun_programs) {
        const std::string directory = std::string(programs) + "\\";
        const auto create = [&](const char* vs_name, std::uint64_t vs_hash, const char* ps_name, std::uint64_t ps_hash, Com<IDirect3DVertexShader9>& vs, Com<IDirect3DPixelShader9>& ps) {
            const Words vs_words = load((directory + vs_name).c_str()), ps_words = load((directory + ps_name).c_str());
            require(fnv(vs_words.data(), vs_words.size() * 4) == vs_hash && fnv(ps_words.data(), ps_words.size() * 4) == ps_hash, "reviewed sun-register pair identity");
            api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(vs_words.data()), &vs.p), "CreateVertexShader sun-register pair");
            api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(ps_words.data()), &ps.p), "CreatePixelShader sun-register pair");
        };
        create("vs_494fe349b8bc12ec.bin", 0x494fe349b8bc12ecull, "ps_fffdabd910793aba.bin", 0xfffdabd910793abaull, glow_vs, glow_ps);
        create("vs_b0602757fce6e870.bin", 0xb0602757fce6e870ull, "ps_517540ae6d5e5410.bin", 0x517540ae6d5e5410ull, detail_vs, detail_ps);
    }
    // The own-ship seam.
    char own_mode[16]{};
    const bool own_ship = GetEnvironmentVariableA("X3M_FIXTURE_OWN_SHIP", own_mode, sizeof own_mode) > 0;
    ShadowOwnShipFn own_install = nullptr;
    if (own_ship) {
        require(cascades != 0 && (!std::strcmp(own_mode, "small") || !std::strcmp(own_mode, "big") || !std::strcmp(own_mode, "swap")), "X3M_FIXTURE_OWN_SHIP is small, big or swap, with cascades");
        own_install = symbol<ShadowOwnShipFn>(f.runtime, "x3m_shadow_own_ship_fixture_install", false);
        require(own_install != nullptr, "the seam DLL exports the own-ship seam");
    }
    std::printf("SHADOW_MODE depth=%u casters=%u taa=%u export=%u far_t=%.9g far_origin=%.9g sun_programs=%u own_ship=%s\n", depth_on, casters, f.taa, readback != nullptr, double(shadow_t_far), double(shadow_t_far / .8f), sun_programs, own_ship ? own_mode : "none");
    // Casters: A and B, then copies of their shapes in their own managed
    // buffers (distinct route keys) with their own scope identities; then the
    // two bounds objects L (index casters) and F (index casters + 1).
    std::vector<ShadowCaster> extra(casters + 4); // ... G, D (indices casters + 2, casters + 3) with F's shape, and the hulls H1, H2 (casters + 4, casters + 5)
    for (unsigned i = 2; i < casters + 6; ++i) {
        auto& c = extra[i - 2]; const bool b = i & 1;
        const bool large = i == casters, distant = i >= casters + 1 && i < casters + 4, hull1 = i == casters + 4, hull2 = i == casters + 5; // (`far` is a Win16 macro)
        const auto& tri = hull1 ? shadow_tri_h1 : hull2 ? shadow_tri_h2 : large ? shadow_tri_l : distant ? shadow_tri_f : b ? shadow_tri_b : shadow_tri_a;
        api(f.d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &c.buffer.p, nullptr), "CreateVertexBuffer caster");
        void* dst = nullptr; api(c.buffer->Lock(0, 0, &dst, 0), "Lock caster");
        for (UINT v = 0; v < 3; ++v) {
            unsigned short data[12] = {half(tri[v][0]), half(tri[v][1]), half(.5f), half(7), half(float(v & 1)), half(float(v >> 1)), 0, half(7), 0, 0, half(1), half(7)};
            std::memcpy(static_cast<char*>(dst) + v * 24, data, 24);
        }
        api(c.buffer->Unlock(), "Unlock caster");
        c.object.name = hull1 ? "H1" : hull2 ? "H2" : large ? "L" : i == casters + 2 ? "G" : i == casters + 3 ? "D" : distant ? "F" : b ? "B" : "A"; c.object.vb = c.buffer.p;
        c.object.covers = hull1 ? covers_h1 : hull2 ? covers_h2 : large ? covers_l : distant ? covers_f : b ? covers_b : covers_a;
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
        if (i == casters) { t = shadow_t_l; p = shadow_p_l; } // L: origin beyond the origin rule, geometry across the box
        else if (i == casters + 1) t = shadow_t_far;          // F: origin and geometry outside both rules (or inside a wide box)
        else if (i == casters + 2) t = shadow_t_g;            // G and D: as F at 240, outside every box of the cases that draw them
        else if (i == casters + 3) t = shadow_t_d;
        else if (i == casters + 4) { t = shadow_t_h1; zo = shadow_zo_h; } // the hulls: near the camera, beyond the far plane on screen
        else if (i == casters + 5) { t = shadow_t_h2; zo = shadow_zo_h; }
    };
    // The hulls' scope nodes: what the seam names as the player ship.
    const std::uintptr_t hull_node[2] = {extra[casters + 2].object.scope.node, extra[casters + 3].object.scope.node};
    const std::uint32_t hull_handle[2] = {extra[casters + 2].object.scope.node_handle, extra[casters + 3].object.scope.node_handle};
    // Submission order: L, the casters, F (the cap drops the last submitted).
    auto order_of = [&](unsigned k) { return k == 0 ? casters : k <= casters ? k - 1 : casters + 1; };
    // poll agree: the engine's law, LightDir_Dir0 = normalize(light - the draw's own origin); the origin of rows
    // (t, p, zo) is clip (t, 0, zo, 1), view (t / m00, 0, 1), world (view - cam.t) R^T.
    auto sun_at = [&](float t, float out[4]) {
        const auto& cam = f.camera_current;
        const double v[3] = {double(t) / cam.m00 - double(cam.t[0]), -double(cam.t[1]), 1. - double(cam.t[2])};
        double d[3], n = 0.;
        for (unsigned i = 0; i < 3; ++i) { d[i] = poll_light[i] - (v[0] * cam.r[i * 3] + v[1] * cam.r[i * 3 + 1] + v[2] * cam.r[i * 3 + 2]); n += d[i] * d[i]; }
        n = std::sqrt(n);
        for (unsigned i = 0; i < 3; ++i) out[i] = float(d[i] / n);
        out[3] = 0.f;
    };
    const char* shape_of[] = {"A", "B"};
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
        for (unsigned t = 0; t < toggle_count; ++t) if (frame == toggle_frames[t]) {
            const int state = toggle(f.d.p);
            require(state == int(t & 1u), "the toggle reports the state it switched to (off first)");
            std::printf("SHADOW_TOGGLE frame=%llu state=%d\n", f.frame, state);
        }
        if (poll_refusals) std::printf("SHADOW_POLL_REFUSAL frame=%u expect=%s\n", frame, poll_context.refuse(frame, poll_light));
        if (own_ship) {
            const unsigned which = !std::strcmp(own_mode, "big") || (!std::strcmp(own_mode, "swap") && frame >= shadow_own_swap_frame) ? 1u : 0u;
            require(own_install(f.d.p, hull_node[which], hull_handle[which]) == 0, "the own-ship seam accepts the device");
            std::printf("SHADOW_OWN_SHIP frame=%u hull=H%u node=%llx\n", frame, which + 1, static_cast<unsigned long long>(hull_node[which]));
        }
        f.frame_begin();
        const float* frame_sun = no_sun ? shadow_sun_flip : shadow_sun; // the sun-changing frame: one frame of another direction
        if (sun_programs) {
            // G first: c4 is its g_EnableGlow, its LightDir_Dir0 is c5. Then D: c4 is its blend weight, LightDir_Dir0 is c0.
            float saved[8][4]; // what material_state wrote to c0 and c5 goes back before the casters
            api(f.d->GetPixelShaderConstantF(0, saved[0], 8), "GetPixelShaderConstantF c0-7");
            const bool matched = f.frames_since_reset > 0;
            float t, p, zo;
            api(f.d->SetPixelShaderConstantF(4, shadow_glow_c4, 1), "glow c4"); api(f.d->SetPixelShaderConstantF(5, frame_sun, 1), "glow LightDir_Dir0 c5");
            rows_of(casters + 2, t, p, zo);
            std::printf("SHADOW_DRAW frame=%llu caster=%u shape=G t=%.9g p=%.9g zo=%.9g\n", f.frame, casters + 2, t, p, zo);
            f.draw(caster(casters + 2), t, p, zo, true, true, matched, Alter::None, casters <= 8, 24, glow_vs.p, glow_ps.p);
            api(f.d->SetPixelShaderConstantF(4, shadow_detail_c4, 1), "detail c4"); api(f.d->SetPixelShaderConstantF(0, frame_sun, 1), "detail LightDir_Dir0 c0");
            rows_of(casters + 3, t, p, zo);
            std::printf("SHADOW_DRAW frame=%llu caster=%u shape=D t=%.9g p=%.9g zo=%.9g\n", f.frame, casters + 3, t, p, zo);
            f.draw(caster(casters + 3), t, p, zo, true, true, matched, Alter::None, casters <= 8, 24, detail_vs.p, detail_ps.p);
            api(f.d->SetPixelShaderConstantF(0, saved[0], 8), "restore c0-7");
        }
        api(f.d->SetPixelShaderConstantF(4, frame_sun, 1), "SetPixelShaderConstantF LightDir_Dir0");
        const auto& cam = f.camera_current;
        std::printf("SHADOW_CAMERA frame=%llu m00=%.9g m11=%.9g r=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g t=%.9g,%.9g,%.9g\n", f.frame, cam.m00, cam.m11,
                    cam.r[0], cam.r[1], cam.r[2], cam.r[3], cam.r[4], cam.r[5], cam.r[6], cam.r[7], cam.r[8], cam.t[0], cam.t[1], cam.t[2]);
        float first_sun[4] = {shadow_sun[0], shadow_sun[1], shadow_sun[2], 0.f};
        if (poll_agree) { float t, p, zo; rows_of(order_of(0), t, p, zo); sun_at(t, first_sun); } // the latch takes the first routed draw's constant
        std::printf("SHADOW_SUN frame=%llu direction=%.9g,%.9g,%.9g\n", f.frame, first_sun[0], first_sun[1], first_sun[2]);
        const bool matched = f.frames_since_reset > 0;
        for (unsigned k = 0; k < casters + 2; ++k) {
            const unsigned i = order_of(k);
            float t, p, zo; rows_of(i, t, p, zo);
            const char* shape = i == casters ? "L" : i == casters + 1 ? "F" : shape_of[i & 1];
            std::printf("SHADOW_DRAW frame=%llu caster=%u shape=%s t=%.9g p=%.9g zo=%.9g\n", f.frame, i, shape, t, p, zo);
            const bool other_declaration = multistream && i == 0;
            if (poll_agree && !no_sun) { float own[4]; sun_at(t, own); api(f.d->SetPixelShaderConstantF(4, own, 1), "SetPixelShaderConstantF LightDir_Dir0 of this draw"); }
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
        if (own_ship) for (unsigned h = 0; h < 2; ++h) { // the hulls after the script's objects: both drawn every frame, one of them the player ship
            const unsigned i = casters + 4 + h;
            float t, p, zo; rows_of(i, t, p, zo);
            std::printf("SHADOW_DRAW frame=%llu caster=%u shape=H%u t=%.9g p=%.9g zo=%.9g\n", f.frame, i, h + 1, t, p, zo);
            f.draw(caster(i), t, p, zo, true, true, matched, Alter::None, casters <= 8);
        }
        const bool lock = frame == shadow_lock_frame;
        if (lock) {
            // The lease proof: a READONLY Lock between the draw and the scene
            // end moves the attempt serial (no revision, so the route's
            // history is unaffected); the counter refuses the record.
            void* data = nullptr;
            api(f.vb_a->Lock(0, 0, &data, D3DLOCK_READONLY), "READONLY Lock of caster 0 after its draw");
            api(f.vb_a->Unlock(), "Unlock of caster 0");
        }
        std::printf("SHADOW_EXPECT frame=%llu casters=%u bounds_objects=%u lease_refused=%u after_reset=%u sun_changing=%u multistream=%u\n", f.frame, casters, sun_programs ? 4u : 2u, lock, frame == shadow_reset_before, no_sun, multistream);
        const unsigned long long ended = f.frame;
        shadow_frame_end(f);
        shadow_readback(f, readback, ended);
        if (cascades) shadow_cascade_readback(f, cascade_readback, cascades, ended);
    }
    require(frames_verified == shadow_frames, "every frame verified against the oracle");
    if (f.taa) require(taa_frames == shadow_frames && taa_reference_frames == shadow_frames, "every frame ran the boundary and compared against the reference resolve");
    for (auto& c : extra) c.buffer.reset();
}
} // namespace
