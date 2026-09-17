// Sun-shadow cascades script ("sunapply" mode with X3M_FIXTURE_SUNAPPLY_CASCADES=1;
// docs/architecture/shadow-cascades.md, section 4). The production
// ShadowReplayPass (three 256^2 maps, one shared depth attachment, one
// transaction) and SunShadowApplyPass (the cascade program) driven directly
// with the production helpers of shadow_replay_projection.h: the checked
// cascade set 250 / 1500 / 7500, one bounds pass per object
// (shadow_cascade_bounds_mask), the budget policy (shadow_cascade_replays), the
// per-issue light rows, the basis each map was replayed with retained by the
// pass and composed with the current camera for the apply. The scene is the
// apply script's box on its plane, rendered as real geometry (a plane quad
// and boxes in managed buffers) and scaled per case:
//   (a) far plane: scale 100, the box's shadow 450-800 units from the camera,
//       owned by cascade 1;
//   (b) seam: scale 60, the shadow runs through cascade 0's blend band;
//   (c) sun column: scale 40 with a second box 2,000 units towards the light
//       above a receiver inside cascade 0 (inside cascade 0's asymmetric depth
//       range, so its map holds the occluder's true depth);
//   (d) retained far: scale 600, budget 3 < 4 issues: frame 6 (even) replays
//       the far cascade, frame 7 (odd) moves the camera and samples the
//       retained far map through its retained basis;
//   (e) Reset: frame 8 replays, a Reset precedes frame 9 (odd: the far cascade
//       is absent and lit), frame 10 repeats frame 8 byte for byte;
//   (f) pancake: the second box 16,000 units towards the light, beyond every
//       cascade's 15,000-unit light-side range: the bounds test admits it (the
//       light side is open), the replay flattens it onto the near plane
//       (map depth 0) and it still shadows the receiver;
//   (g) half texel: case (a)'s scene eight times with the box moved by k / 8
//       of a cascade-1 texel (11.72 units) along x and 0.61 of that along z,
//       so its shadow edges take eight sub-texel phases against the
//       world-fixed texel grid; the runner sums the shift fit over the eight
//       frames (one frame's edges sit at one phase, which alone is worth up
//       to half a texel either way) and finds the lookup unbiased.
//   (h) sun at finite distance: a point light 24,000 units from the camera, 75
//       degrees to the side of the view direction at 30 degrees elevation, and
//       three boxes on the camera's ground track: A floating, its shadow
//       landing 260 units ahead inside cascade 0; B standing on the plane 550
//       units ahead, 80 units tall, the nearest to cascade 1's centre that
//       cascade 1 owns; C floating 500 units above its shadow 1,200 units
//       ahead, near cascade 1's edge. The
//       production PointSun (src/proxy/shadow_replay_sun_point.h) validates the
//       light against the constants of three draw origins and gives every
//       cascade its own sun, normalize(light - cascade centre): cascade 0's
//       centre lies 128 units ahead, 118 of them across the light, 4.9e-3 rad
//       at this distance, beyond the 1 / 256 rad hold of a 256-texel map, so
//       cascade 0's basis differs from cascade 1's (cascade 2 adopts cascade
//       1's): per-cascade bounds rows, draw rows and apply rows. The runner
//       holds A's and B's shadow edges to one texel of the analytic POINT-light
//       shadow and reports C's residual against h r / D.
// Every frame's RT2, per-cascade map readbacks and target readbacks are
// written beside the executable for the runner's twin
// (verification/probe/sun_shadow_apply.py, expected_factor_cascades).
namespace {
constexpr unsigned cascade_frames = 21, cascade_map = 256, cascade_count = 3;
constexpr double cascade_point_distance = 24000., cascade_point_side_deg = 75., cascade_point_track[3] = {260., 550., 1200.}, cascade_point_lift[3] = {120., 160., 1000.}, cascade_point_half[3] = {8., 40., 80.};
constexpr double cascade_phase_texel = 2. * 1500. / cascade_map; // cascade 1's world texel
struct CascadeScript { char name; double scale; float elevation_deg; double high_box; unsigned budget; double shift[3]; unsigned jitter_index; float jx, jy; float exponent; bool reset_before; unsigned phase = 0; };
constexpr CascadeScript cascade_script[cascade_frames] = {
    {'a', 100., 50.f, 0., 640, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false},
    {'a', 100., 30.f, 0., 640, {0, 0, 0}, 1, .25f, -.125f, 1.f, false},
    {'b', 60., 30.f, 0., 640, {0, 0, 0}, 2, -.375f, .25f, 1.f, false},
    {'b', 60., 50.f, 0., 640, {0, 0, 0}, 3, .125f, .375f, 1.f, false},
    {'c', 40., 50.f, 2000., 640, {0, 0, 0}, 4, 0.f, 0.f, 1.f, false},
    {'c', 40., 70.f, 2000., 640, {0, 0, 0}, 5, -.25f, -.375f, 1.f / 2.2f, false},
    {'d', 600., 50.f, 0., 3, {0, 0, 0}, 6, 0.f, 0.f, 1.f, false},
    {'d', 600., 50.f, 0., 3, {.062, .011, .036}, 7, .25f, .125f, 1.f, false},   // 37 / 6.6 / 21.6 units: no multiple of a texel
    {'e', 600., 50.f, 0., 3, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false},
    {'e', 600., 50.f, 0., 3, {0, 0, 0}, 0, 0.f, 0.f, 1.f, true},                // Reset first: the retained far map is gone
    {'e', 600., 50.f, 0., 3, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false},               // = frame 8
    {'f', 40., 50.f, 16000., 640, {0, 0, 0}, 3, .125f, -.25f, 1.f, false},      // beyond depth_toward_light: pancaked
    {'g', 100., 50.f, 0., 640, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false, 0}, {'g', 100., 50.f, 0., 640, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false, 1},
    {'g', 100., 50.f, 0., 640, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false, 2}, {'g', 100., 50.f, 0., 640, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false, 3},
    {'g', 100., 50.f, 0., 640, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false, 4}, {'g', 100., 50.f, 0., 640, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false, 5},
    {'g', 100., 50.f, 0., 640, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false, 6}, {'g', 100., 50.f, 0., 640, {0, 0, 0}, 0, 0.f, 0.f, 1.f, false, 7},
    {'h', 60., 30.f, 0., 640, {0, 0, 0}, 2, .125f, -.25f, 1.f, false},          // the sun at finite distance (last: it rebuilds the box buffer)
};
struct CascadeBox { double lo[3], hi[3]; };
// Nearest positive hit of the plane y = 0 and the boxes along o + t dir, or a negative value.
double cascade_hit(Vec3 o, Vec3 dir, const CascadeBox* boxes, unsigned count) {
    double best = -1.;
    if (dir.y < 0. && o.y > 0.) best = -o.y / dir.y;
    const double oo[3] = {o.x, o.y, o.z}, dd[3] = {dir.x, dir.y, dir.z};
    for (unsigned b = 0; b < count; ++b) {
        double enter = -1e30, leave = 1e30; bool outside = false;
        for (unsigned k = 0; k < 3 && !outside; ++k) {
            if (std::fabs(dd[k]) < 1e-12) { outside = oo[k] < boxes[b].lo[k] || oo[k] > boxes[b].hi[k]; continue; }
            double t0 = (boxes[b].lo[k] - oo[k]) / dd[k], t1 = (boxes[b].hi[k] - oo[k]) / dd[k];
            if (t0 > t1) std::swap(t0, t1);
            enter = std::max(enter, t0); leave = std::min(leave, t1);
        }
        if (!outside && enter <= leave && leave > 0.) { const double t = enter > 0. ? enter : leave; if (best < 0. || t < best) best = t; }
    }
    return best;
}
struct CascadeObject { Com<IDirect3DVertexBuffer9> buffer; float lo[3], hi[3]; UINT triangles; };
void cascade_make_box(Fixture& f, CascadeObject& o, const CascadeBox& box) {
    const float x[2] = {float(box.lo[0]), float(box.hi[0])}, y[2] = {float(box.lo[1]), float(box.hi[1])}, z[2] = {float(box.lo[2]), float(box.hi[2])};
    static constexpr unsigned char faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}}; // corner = x | y << 1 | z << 2
    std::vector<float> v;
    for (const auto& face : faces) for (unsigned k : {0u, 1u, 2u, 0u, 2u, 3u}) { const unsigned c = face[k]; v.push_back(x[c & 1]); v.push_back(y[(c >> 1) & 1]); v.push_back(z[(c >> 2) & 1]); }
    o.buffer.reset();
    api(f.d->CreateVertexBuffer(UINT(v.size() * 4), 0, 0, D3DPOOL_MANAGED, &o.buffer.p, nullptr), "CreateVertexBuffer box");
    void* dst = nullptr; api(o.buffer->Lock(0, 0, &dst, 0), "Lock box"); std::memcpy(dst, v.data(), v.size() * 4); api(o.buffer->Unlock(), "Unlock box");
    for (unsigned k = 0; k < 3; ++k) { o.lo[k] = float(box.lo[k]); o.hi[k] = float(box.hi[k]); }
    o.triangles = 12;
}
void cascade_make_plane(Fixture& f, CascadeObject& o, float half) {
    const float v[18] = {-half, 0, -half, -half, 0, half, half, 0, half, -half, 0, -half, half, 0, half, half, 0, -half};
    o.buffer.reset();
    api(f.d->CreateVertexBuffer(sizeof v, 0, 0, D3DPOOL_MANAGED, &o.buffer.p, nullptr), "CreateVertexBuffer plane");
    void* dst = nullptr; api(o.buffer->Lock(0, 0, &dst, 0), "Lock plane"); std::memcpy(dst, v, sizeof v); api(o.buffer->Unlock(), "Unlock plane");
    o.lo[0] = o.lo[2] = -half; o.hi[0] = o.hi[2] = half; o.lo[1] = o.hi[1] = 0.f;
    o.triangles = 2;
}
std::vector<float> cascade_read_map(Fixture& f, IDirect3DSurface9* map, IDirect3DSurface9* copy, unsigned size) {
    api(f.d->GetRenderTargetData(map, copy), "GetRenderTargetData cascade map");
    D3DLOCKED_RECT lock{}; api(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY), "LockRect cascade map");
    std::vector<float> out(std::size_t(size) * size);
    for (unsigned y = 0; y < size; ++y) std::memcpy(&out[std::size_t(y) * size], static_cast<const char*>(lock.pBits) + y * lock.Pitch, std::size_t(size) * 4);
    copy->UnlockRect();
    return out;
}
void run_sun_apply_cascades(Fixture& f) {
    namespace r = x3m::renderer;
    SunApplyState s;
    sun_apply_map = cascade_map;
    D3DCAPS9 caps{}; api(f.d->GetDeviceCaps(&caps), "GetDeviceCaps");
    // The production set at fixture map sizes; every other default stands.
    const unsigned sizes[r::shadow_cascade_max] = {cascade_map, cascade_map, cascade_map, cascade_map};
    r::ShadowCascadeSet set{};
    require(r::shadow_cascade_set(r::shadow_cascade_extent_defaults, cascade_count, sizes, nullptr, r::shadow_cascade_budget_default, set), "the default cascade set builds");
    require(set.count == cascade_count && set.cascades[0].forward_offset == r::shadow_replay_forward_offset_default && set.cascades[1].forward_offset == 0.f &&
            set.cascades[0].depth_toward_light == 15000.f && set.cascades[0].depth_behind == 512.f && set.cascades[1].depth_behind == 3000.f && set.cascades[2].depth_behind == 15000.f &&
            set.caps[0] == 128 && set.caps[1] == 512 && set.caps[2] == 1024 && set.budget == 640, "the note's extents, depth ranges, caps and budget are the defaults");
    { r::ShadowCascadeSet bad{}; const float descending[2] = {1500.f, 250.f}; const float tiny[1] = {10.f};
      require(!r::shadow_cascade_set(descending, 2, nullptr, nullptr, 640, bad) && !r::shadow_cascade_set(tiny, 1, nullptr, nullptr, 640, bad) &&
              !r::shadow_cascade_set(r::shadow_cascade_extent_defaults, 5, nullptr, nullptr, 640, bad) && !r::shadow_cascade_set(r::shadow_cascade_extent_defaults, 3, nullptr, nullptr, 0, bad) && bad.count == 0,
              "a descending, out-of-range, oversized or budget-free cascade set is refused"); }
    require(r::shadow_cascade_replays(0, 3, 9999, 1, 1) && r::shadow_cascade_replays(2, 3, 640, 640, 1) && !r::shadow_cascade_replays(2, 3, 641, 640, 1) && r::shadow_cascade_replays(2, 3, 641, 640, 2) &&
            r::shadow_cascade_replays(0, 1, 9999, 1, 1), "only the far cascade of a set yields to the budget, on odd frames");
    // The passes. The apply pass without the cascade program refuses the cascade frame.
    {
        r::SunShadowApplyPass plain; r::SunShadowApplyResult refused{}; r::SunShadowCascadeFrame none{};
        api(plain.attach(f.d.p, nullptr, caps, D3DFMT_X8R8G8B8, D3DFMT_A16B16G16R16F), "plain apply attach");
        require(!plain.caps().cascades && plain.references() == 3 && plain.execute_cascades(none, &refused) == S_FALSE && refused.skipped && !std::strcmp(refused.skipped_reason, "cascades"),
                "a pass attached without the cascade program skips a cascade frame");
        D3DCAPS9 few = caps; few.MaxPixelShader30InstructionSlots = 256; // between the two programs' slot counts
        r::SunShadowApplyPass small;
        require(small.attach(f.d.p, nullptr, few, D3DFMT_X8R8G8B8, D3DFMT_A16B16G16R16F, true) == D3DERR_NOTAVAILABLE && !std::strcmp(small.caps().reason, "cascade_ps_slots") && small.references() == 0,
                "a device with too few ps_3_0 slots refuses the cascade program");
        require(SUCCEEDED(small.attach(f.d.p, nullptr, few, D3DFMT_X8R8G8B8, D3DFMT_A16B16G16R16F)) && small.caps().enabled, "the same device keeps the single-map program");
    }
    const HRESULT attached = s.pass.attach(f.d.p, nullptr, caps, D3DFMT_X8R8G8B8, D3DFMT_A16B16G16R16F, true);
    std::printf("SUNAPPLY_DEVICE attached=%u result=%08lx reason=%s slots=%u cascade_slots=%u references=%u max_texture=%lux%lu ps30_slots=%lu\n", SUCCEEDED(attached), attached, s.pass.caps().reason,
                s.pass.caps().program_slots, s.pass.caps().cascade_slots, s.pass.references(), caps.MaxTextureWidth, caps.MaxTextureHeight, caps.MaxPixelShader30InstructionSlots);
    require(SUCCEEDED(attached) && s.pass.caps().enabled && s.pass.caps().cascades && s.pass.references() == 4, "the apply pass attaches with the cascade program");
    r::ShadowReplayPass replay;
    {   // MaxTextureWidth below the request: every map is halved to fit; below 64 the attach is refused.
        D3DCAPS9 narrow = caps; narrow.MaxTextureWidth = narrow.MaxTextureHeight = 128;
        api(replay.attach_cascades(f.d.p, nullptr, narrow, D3DFMT_X8R8G8B8, sizes, cascade_count), "attach_cascades under a 128-texel limit");
        require(replay.maps() == cascade_count && replay.size(0) == 128 && replay.size(2) == 128 && replay.depth_size() == 128 && replay.caps().halved == cascade_count, "oversized cascade maps are halved to MaxTextureWidth");
        narrow.MaxTextureWidth = narrow.MaxTextureHeight = 32;
        require(replay.attach_cascades(f.d.p, nullptr, narrow, D3DFMT_X8R8G8B8, sizes, cascade_count) == D3DERR_NOTAVAILABLE && !std::strcmp(replay.caps().reason, "size") && replay.maps() == 0, "a limit below 64 texels refuses the cascades");
        const unsigned mixed[3] = {64, 256, 128};
        api(replay.attach_cascades(f.d.p, nullptr, caps, D3DFMT_X8R8G8B8, mixed, 3), "attach_cascades mixed sizes"); api(replay.prepare(), "prepare mixed sizes");
        D3DSURFACE_DESC desc{};
        require(replay.depth_size() == 256 && replay.references() == 9 && SUCCEEDED(replay.map_surface(2)->GetDesc(&desc)) && desc.Width == 128 && replay.map_texture(3) == nullptr, "per-cascade sizes, one attachment of the largest");
    }
    api(replay.attach_cascades(f.d.p, nullptr, caps, D3DFMT_X8R8G8B8, sizes, cascade_count), "ShadowReplayPass attach_cascades");
    std::printf("SUNAPPLY_REPLAY_DEVICE maps=%u depth_size=%u map_format=%u depth_format=%u readable=%u halved=%u\n", replay.maps(), replay.depth_size(), unsigned(replay.caps().map_format),
                unsigned(replay.caps().depth_format), replay.caps().readable, replay.caps().halved);
    require(replay.caps().readable && replay.retained(0) == nullptr, "R32F cascade maps, nothing retained before the first replay");
    sun_apply_create_targets(f, s);
    Com<IDirect3DSurface9> map_copy;
    api(f.d->CreateOffscreenPlainSurface(cascade_map, cascade_map, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &map_copy.p, nullptr), "CreateOffscreenPlainSurface R32F");
    Com<IDirect3DVertexDeclaration9> declaration;
    { const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
      api(f.d->CreateVertexDeclaration(elements, &declaration.p), "CreateVertexDeclaration position"); }
    r::SunShadowBias biases[cascade_count]{};
    const double bias_units = r::sun_shadow_bias_units_default, clamp_texels = 4.; // the detached fixture's clamp (a 256^2 map's texel is 16 x the production one)
    for (unsigned c = 0; c < cascade_count; ++c)
        require(r::sun_shadow_apply_bias(bias_units, clamp_texels, set.cascades[c].half_extent, set.cascades[c].depth_half(), set.cascades[c].size, biases[c]), "the world-unit bias resolves per cascade");
    std::printf("SUNAPPLY_CONFIG cascades=%u map_size=%u extents=%.9g,%.9g,%.9g depth_light=%.9g depth_behind=%.9g,%.9g,%.9g caps=%u,%u,%u bias_units=%.9g clamp_texels=%.9g margin=%.9g band=%.9g\n",
                cascade_count, cascade_map, double(set.cascades[0].half_extent), double(set.cascades[1].half_extent), double(set.cascades[2].half_extent), double(set.cascades[0].depth_toward_light),
                double(set.cascades[0].depth_behind), double(set.cascades[1].depth_behind), double(set.cascades[2].depth_behind), set.caps[0], set.caps[1], set.caps[2], bias_units, clamp_texels,
                double(r::shadow_cascade_select_margin), double(r::shadow_cascade_blend_band));
    s.rt2_data.resize(std::size_t(sun_apply_w) * sun_apply_h * 2);
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    CascadeObject plane, box, high, third;
    std::vector<unsigned char> reset_reference_before, reset_reference_after;
    std::uint64_t map_frames[cascade_count] = {~0ull, ~0ull, ~0ull};
    double built_scale = 0.; unsigned built_phase = 0;
    for (unsigned frame = 0; frame < cascade_frames; ++frame) {
        const CascadeScript& script = cascade_script[frame];
        if (script.reset_before) {
            sun_apply_release_targets(s); map_copy.reset();
            s.pass.before_reset(); replay.before_reset();
            require(s.pass.reset_pending() && s.pass.references() == 4 && replay.reset_pending() && replay.references() == 2, "before_reset releases the blocks, the maps and the attachment, keeps the programs");
            for (unsigned c = 0; c < cascade_count; ++c) require(replay.retained(c) == nullptr && replay.map_texture(c) == nullptr, "before_reset voids every retained basis");
            r::SunShadowApplyResult refused{}; r::SunShadowCascadeFrame pending{};
            require(s.pass.execute_cascades(pending, &refused) == S_FALSE && refused.skipped && !std::strcmp(refused.skipped_reason, "reset_pending"), "the cascade quad is refused while the Reset is pending");
            f.reset();
            s.pass.after_reset(S_OK); replay.after_reset(S_OK);
            sun_apply_create_targets(f, s);
            api(f.d->CreateOffscreenPlainSurface(cascade_map, cascade_map, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &map_copy.p, nullptr), "CreateOffscreenPlainSurface R32F after Reset");
        }
        // The scene at this frame's scale.
        const double scale = script.scale;
        const bool point = script.name == 'h';
        CascadeBox boxes[3]; unsigned box_count = 1;
        for (unsigned k = 0; k < 3; ++k) { boxes[0].lo[k] = sun_apply_box_min[k] * scale; boxes[0].hi[k] = sun_apply_box_max[k] * scale; }
        { const double phase = script.phase / 8. * cascade_phase_texel; // case g: the box at eight sub-texel phases
          boxes[0].lo[0] += phase; boxes[0].hi[0] += phase; boxes[0].lo[2] += .61 * phase; boxes[0].hi[2] += .61 * phase; }
        const double az = sun_apply_azimuth_deg * 3.14159265358979323846 / 180., el = double(script.elevation_deg) * 3.14159265358979323846 / 180.;
        float sun[4] = {float(std::sin(az) * std::cos(el)), float(std::sin(el)), float(std::cos(az) * std::cos(el)), 0.f};
        if (script.high_box > 0.) {
            // Towards the light above the ground point P, 30 units across.
            const Vec3 p{.75 * scale, 0., -2.5 * scale}, centre = p + Vec3{sun[0], sun[1], sun[2]} * script.high_box;
            const double c[3] = {centre.x, centre.y, centre.z};
            for (unsigned k = 0; k < 3; ++k) { boxes[1].lo[k] = c[k] - 15.; boxes[1].hi[k] = c[k] + 15.; }
            box_count = 2;
        }
        // The plane quad stays inside every cascade's light-side range from every camera of the script.
        if (built_scale != scale || built_phase != script.phase) { cascade_make_plane(f, plane, 12000.f); cascade_make_box(f, box, boxes[0]); built_scale = scale; built_phase = script.phase; }
        if (script.high_box > 0.) cascade_make_box(f, high, boxes[1]);
        // Camera.
        const Vec3 look = Vec3{sun_apply_look[0], sun_apply_look[1], sun_apply_look[2]} * scale;
        s.position = (Vec3{sun_apply_camera[0], sun_apply_camera[1], sun_apply_camera[2]} + Vec3{script.shift[0], script.shift[1], script.shift[2]}) * scale;
        s.forward = normalize(look - s.position);
        s.right = normalize(cross(Vec3{0, 1, 0}, s.forward)); s.up = cross(s.forward, s.right);
        const double fov_half = 25. * 3.14159265358979323846 / 180., near_z = 1. * scale, far_z = 50. * scale;
        s.camera = {}; s.camera.valid = true; s.camera.m00 = s.camera.m11 = float(1. / std::tan(fov_half));
        s.m22 = float(far_z / (far_z - near_z)); s.m32 = float(-near_z * far_z / (far_z - near_z));
        const Vec3 axes[3] = {s.right, s.up, s.forward};
        for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j) { const Vec3 a = axes[j]; s.camera.r[i * 3 + j] = float(i == 0 ? a.x : i == 1 ? a.y : a.z); }
        for (unsigned j = 0; j < 3; ++j) s.camera.t[j] = float(-dot(axes[j], s.position));
        // The application's clip rows of world-space geometry (x, y and w are what the helpers read).
        float clip[16]{};
        for (unsigned j = 0; j < 3; ++j) {
            const double scale_j = j == 0 ? double(s.camera.m00) : j == 1 ? double(s.camera.m11) : 1.;
            const unsigned row = j == 2 ? 3 : j;
            clip[row * 4] = float(axes[j].x * scale_j); clip[row * 4 + 1] = float(axes[j].y * scale_j); clip[row * 4 + 2] = float(axes[j].z * scale_j);
            clip[row * 4 + 3] = float(-dot(axes[j], s.position) * scale_j);
        }
        for (unsigned k = 0; k < 4; ++k) clip[8 + k] = clip[12 + k] * s.m22 + (k == 3 ? s.m32 : 0.f);
        // Every cascade's sun: the frame's one direction, or (h) the production PointSun's per-cascade directions.
        float suns[r::shadow_cascade_max * 4]{};
        for (unsigned c = 0; c < r::shadow_cascade_max; ++c) std::memcpy(suns + c * 4, sun, sizeof sun);
        Vec3 light{0, 0, 0}; double point_agreement = -1.; unsigned point_rederived = 0;
        if (point) {
            const Vec3 ahead = normalize(Vec3{s.forward.x, 0., s.forward.z}), side = cross(Vec3{0, 1, 0}, ahead);
            const double phi = cascade_point_side_deg * 3.14159265358979323846 / 180.;
            const Vec3 to_light = ahead * (-std::cos(phi) * std::cos(el)) + Vec3{0, 1, 0} * std::sin(el) + side * (std::sin(phi) * std::cos(el));
            light = s.position + to_light * cascade_point_distance;
            sun[0] = float(to_light.x); sun[1] = float(to_light.y); sun[2] = float(to_light.z); // the scene record's nominal direction (at the camera)
            const Vec3 ground{s.position.x, 0., s.position.z};
            for (unsigned b = 0; b < 3; ++b) {
                const Vec3 landing = ground + ahead * cascade_point_track[b];
                const Vec3 centre = b == 1 ? landing + Vec3{0, 1, 0} * cascade_point_half[b] : landing + normalize(light - landing) * cascade_point_lift[b]; // B stands on the plane (its top: lift x sin 30)
                const double c[3] = {centre.x, centre.y, centre.z};
                for (unsigned k = 0; k < 3; ++k) { boxes[b].lo[k] = c[k] - cascade_point_half[b]; boxes[b].hi[k] = c[k] + cascade_point_half[b]; }
            }
            box_count = 3;
            cascade_make_box(f, box, boxes[0]); cascade_make_box(f, high, boxes[1]); cascade_make_box(f, third, boxes[2]);
            // The production decision: the light as the engine stores it (integers x 0.01), validated against the
            // LightDir_Dir0 the engine would upload at three draw origins, then one held direction per cascade.
            x3m::shadow_replay::PointSun ps;
            ps.begin_frame();
            const std::int32_t native[3] = {std::int32_t(std::llround(light.x * 100.)), std::int32_t(std::llround(light.y * 100.)), std::int32_t(std::llround(light.z * 100.))};
            ps.set_poll(native, x3m::shadow_replay::PointSunReason::Point);
            light = Vec3{native[0] * .01, native[1] * .01, native[2] * .01};
            require(!ps.decide(true, s.camera, set) && ps.reason == x3m::shadow_replay::PointSunReason::Unchecked, "an unvalidated light position is not a sun source");
            ps.begin_frame(); ps.set_poll(native, x3m::shadow_replay::PointSunReason::Point);
            for (unsigned b = 0; b < 3; ++b) {
                const Vec3 origin{.5 * (boxes[b].lo[0] + boxes[b].hi[0]), .5 * (boxes[b].lo[1] + boxes[b].hi[1]), .5 * (boxes[b].lo[2] + boxes[b].hi[2])}, d = normalize(light - origin);
                const float constant[4] = {float(d.x), float(d.y), float(d.z), 0.f}; const double o[3] = {origin.x, origin.y, origin.z};
                require(ps.check(constant, o), "the engine's constant at a draw origin agrees with the polled light");
            }
            require(ps.decide(true, s.camera, set) && ps.rederived == cascade_count && ps.checks == 3 && ps.disagreements == 0 && ps.agreement_degrees() < .01, "the validated light is the frame's sun source");
            std::memcpy(suns, ps.suns, sizeof suns); point_agreement = ps.agreement_degrees(); point_rederived = ps.rederived;
            require(std::memcmp(suns, suns + 4, 12) != 0 && std::memcmp(suns + 4, suns + 8, 12) == 0, "cascade 0 holds its own direction, cascade 2 adopts cascade 1's");
            ps.end_frame();
            // The next frame: the same camera holds every direction bit for bit without a new check (validation carried);
            // a light inside the cascades' volume, a disagreeing constant and a missing light fall back with their reasons.
            ps.begin_frame(); ps.set_poll(native, x3m::shadow_replay::PointSunReason::Point);
            require(ps.decide(true, s.camera, set) && ps.rederived == 0 && std::memcmp(suns, ps.suns, sizeof suns) == 0, "held directions are bit-stable under a carried validation");
            ps.end_frame();
            { auto inside = ps; inside.begin_frame(); const std::int32_t close[3] = {std::int32_t(std::llround(s.position.x * 100.)), std::int32_t(std::llround((s.position.y + 20000.) * 100.)), std::int32_t(std::llround(s.position.z * 100.))};
              inside.set_poll(close, x3m::shadow_replay::PointSunReason::Point); const float up[4] = {0, 1, 0, 0}; const double o[3] = {s.position.x, s.position.y, s.position.z};
              require(inside.check(up, o) && !inside.decide(true, s.camera, set) && inside.reason == x3m::shadow_replay::PointSunReason::Near && inside.sun(0) == nullptr, "a light inside the cascades' volume is refused (near)"); }
            { auto wrong = ps; wrong.begin_frame(); wrong.set_poll(native, x3m::shadow_replay::PointSunReason::Point); const float up[4] = {0, 1, 0, 0}; const double o[3] = {0, 0, 0};
              require(!wrong.check(up, o) && !wrong.decide(true, s.camera, set) && wrong.reason == x3m::shadow_replay::PointSunReason::Disagrees, "a disagreeing constant refuses the poll");
              wrong.end_frame(); wrong.begin_frame(); wrong.set_poll(native, x3m::shadow_replay::PointSunReason::Point);
              require(!wrong.decide(true, s.camera, set) && wrong.reason == x3m::shadow_replay::PointSunReason::Cooldown && wrong.frames_latch[unsigned(x3m::shadow_replay::PointSunReason::Disagrees)] == 1, "the refusal cools down, counted by reason"); }
            { auto none = ps; none.begin_frame(); none.set_poll(nullptr, x3m::shadow_replay::PointSunReason::NoLight);
              require(!none.decide(true, s.camera, set) && none.reason == x3m::shadow_replay::PointSunReason::NoLight, "no directional light: the latch stays"); }
        }
        // One bounds pass per object: the cascade mask, under the per-cascade caps.
        r::ShadowCascadeBounds bounds{};
        require(r::shadow_cascade_bounds_suns(s.camera, suns, set, bounds) && bounds.shared == !point, "the frame's cascade boxes build");
        if (!point) { r::ShadowCascadeBounds one{}; require(r::shadow_cascade_bounds(s.camera, sun, set, one) && one.count == bounds.count && one.shared && !std::memcmp(one.rows, bounds.rows, sizeof one.rows) && !std::memcmp(one.lo, bounds.lo, sizeof one.lo) && !std::memcmp(one.hi, bounds.hi, sizeof one.hi), "the one-sun bounds equal the per-cascade bounds of equal suns"); }
        const CascadeObject* objects[4] = {&plane, &box, script.high_box > 0. || point ? &high : nullptr, point ? &third : nullptr};
        const unsigned object_count = point ? 4 : script.high_box > 0. ? 3 : 2;
        unsigned masks[4]{}, per_cascade[cascade_count]{}, issues = 0;
        for (unsigned o = 0; o < object_count; ++o) {
            const int mask = r::shadow_cascade_bounds_mask(s.camera, clip, bounds, objects[o]->lo, objects[o]->hi);
            require(mask >= 0, "the bounds mask is known");
            masks[o] = unsigned(mask);
            for (unsigned c = 0; c < cascade_count; ++c) if (masks[o] & (1u << c)) { ++per_cascade[c]; ++issues; }
        }
        require(masks[0] == 7u, "the plane meets every cascade");
        int legacy_high = -2;
        if (script.high_box > 0.) {
            // The sun-column occluder is a cascade-0 caster: inside the asymmetric
            // range at 2,000 units, and beyond it at 16,000 because the box test's
            // light side is open (the replay pancakes it). The single map's box
            // test admits it by the same rule.
            require((masks[2] & 1u) != 0, "the occluder towards the light is a cascade-0 caster");
            require((script.high_box > double(set.cascades[0].depth_toward_light)) == (script.name == 'f'), "only case f lies beyond the light-side range");
            r::ShadowReplayCascade single{}; r::ShadowReplayBasis single_basis{}; float single_rows[12];
            require(r::shadow_replay_basis(s.camera, sun, single, single_basis) && r::shadow_replay_view_rows(s.camera, single_basis, single, single_rows), "the single-map box builds");
            legacy_high = r::shadow_replay_bounds_verdict(s.camera, clip, single_rows, high.lo, high.hi);
            require(legacy_high == 1, "the single-map box test admits the sun-column occluder (open light side)");
            // An object wholly behind every cascade (away from the light) is still refused.
            const float below_lo[3] = {-20.f, -40000.f, -20.f}, below_hi[3] = {20.f, -39000.f, 20.f};
            require(r::shadow_cascade_bounds_mask(s.camera, clip, bounds, below_lo, below_hi) == 0, "an object beyond every cascade's far side is no caster");
        }
        // The transaction: the far cascade yields to the budget on odd frames.
        r::ShadowReplayBasis bases[cascade_count]{};
        bool replays[cascade_count]{};
        r::ShadowReplayDraw draws[4]{};
        r::ShadowReplayIssue issue_store[12]{};
        r::ShadowReplayMapList lists[cascade_count]{};
        unsigned list_count = 0, used = 0;
        for (unsigned o = 0; o < object_count; ++o) {
            auto& d = draws[o];
            d.vertex_buffer = objects[o]->buffer.p; d.declaration = declaration.p; d.stride = 12; d.topology = D3DPT_TRIANGLELIST; d.primitives = objects[o]->triangles; d.cull_mode = D3DCULL_NONE;
        }
        for (unsigned c = 0; c < cascade_count; ++c) {
            require(r::shadow_replay_basis(s.camera, suns + c * 4, set.cascades[c], bases[c]), "the cascade basis builds");
            replays[c] = per_cascade[c] != 0 && r::shadow_cascade_replays(c, cascade_count, issues, script.budget, frame);
            if (!replays[c]) continue;
            lists[list_count].map = c; lists[list_count].issues = issue_store + used;
            for (unsigned o = 0; o < object_count; ++o) {
                if (!(masks[o] & (1u << c))) continue;
                double base[3][4];
                require(r::shadow_cascade_draw_rows(s.camera, clip, bases[c], base), "the draw's sun rows build"); // bases[c]: equal axes on a one-sun frame
                auto& issue = issue_store[used++];
                issue.draw = std::uint16_t(o);
                require(r::shadow_cascade_light_rows(base, bases[c], set.cascades[c], issue.rows), "the cascade light rows build");
                // The shared-product rows equal the single-map helper's within float rounding.
                float reference[16];
                require(r::shadow_replay_light_rows(s.camera, clip, bases[c], set.cascades[c], reference), "the reference light rows build");
                for (unsigned k = 0; k < 12; ++k) require(std::fabs(reference[k] - issue.rows[k]) <= 1e-5f * std::max(1.f, std::fabs(reference[k])), "cascade light rows equal shadow_replay_light_rows");
                require(reference[12] == 0.f && reference[13] == 0.f && reference[14] == 0.f && reference[15] == 1.f, "the light projection's w row is (0, 0, 0, 1)");
                ++lists[list_count].count;
            }
            ++list_count;
        }
        const bool far_skipped = per_cascade[cascade_count - 1] != 0 && !replays[cascade_count - 1];
        require(far_skipped == ((script.name == 'd' || script.name == 'e') && (frame & 1u)), "the far cascade is skipped exactly on the budgeted odd frames");
        // RT2 from the camera rays (the apply script's law).
        const float m20 = 2.f * script.jx / float(sun_apply_w), m21 = -2.f * script.jy / float(sun_apply_h);
        unsigned receivers = 0, sentinels = 0, share_free = 0;
        for (unsigned j = 0; j < sun_apply_h; ++j) for (unsigned i = 0; i < sun_apply_w; ++i) {
            const double ndc_x = (i + .5) / sun_apply_w * 2. - 1., ndc_y = 1. - (j + .5) / sun_apply_h * 2.;
            const Vec3 dv{(ndc_x - m20) / s.camera.m00, (ndc_y - m21) / s.camera.m11, 1.};
            const Vec3 dir = s.right * dv.x + s.up * dv.y + s.forward * dv.z;
            const double t = cascade_hit(s.position, dir, boxes, box_count);
            float* px = &s.rt2_data[(std::size_t(j) * sun_apply_w + i) * 2];
            if (t <= 0. || t >= far_z) { px[0] = -1.f; px[1] = 0.f; ++sentinels; continue; }
            const Vec3 hit = s.position + dir * t;
            px[0] = float(double(s.m22) + double(s.m32) / t);
            px[1] = (i % 9 == 4) ? 0.f : hit.y > 1e-6 * scale ? .85f : float(.2 + .75 * ((i * 3 + j * 5) % 17) / 16.);
            if (px[1] <= 0.f) ++share_free; else ++receivers;
        }
        sun_apply_upload(f, s.rt2_sys.p, s.rt2.p, s.rt2_data, sun_apply_w, sun_apply_h, 2);
        api(f.d->BeginScene(), "BeginScene");
        sun_apply_fill(f, s);
        s.before = sun_apply_read(f, s);
        // Hostile caller state both transactions must restore around themselves.
        api(f.d->SetRenderTarget(0, f.back.p), "SetRenderTarget back"); api(f.d->SetDepthStencilSurface(f.depth.p), "SetDepthStencilSurface");
        api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "hostile blend"); api(f.d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA), "hostile srcblend");
        api(f.d->SetRenderState(D3DRS_ZENABLE, TRUE), "hostile z"); api(f.d->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW), "hostile cull");
        api(f.d->SetTexture(0, f.textures[0].p), "hostile texture 0"); api(f.d->SetTexture(3, f.textures[0].p), "hostile texture 3");
        const RECT scissor{3, 5, 40, 41}; api(f.d->SetScissorRect(&scissor), "hostile scissor");
        const D3DVIEWPORT9 viewport{2, 3, Fixture::W - 8, Fixture::H - 9, .1f, .9f}; api(f.d->SetViewport(&viewport), "hostile viewport");
        api(f.d->SetPixelShader(f.flat.p), "hostile ps");
        const Snapshot before_state = f.snapshot();
        // Refusals touch nothing and void nothing they do not rewrite.
        r::ShadowReplayResult replayed{};
        { r::ShadowReplayMapList twice[2] = {lists[0], lists[0]};
          require(replay.execute_cascades(draws, object_count, twice, 2, true, false, &replayed) == E_INVALIDARG && replayed.failed == r::ShadowReplayStage::Validate, "a map listed twice is refused");
          r::ShadowReplayMapList beyond = lists[0]; beyond.map = cascade_count;
          require(replay.execute_cascades(draws, object_count, &beyond, 1, true, false, &replayed) == E_INVALIDARG, "a map beyond the attached count is refused");
          require(replay.execute_cascades(draws, object_count, lists, list_count, true, true, &replayed) == E_INVALIDARG, "a recording caller is refused");
          f.compare(before_state, f.snapshot(), "cascade_replay_refused"); }
        const r::ShadowReplayRetained* far_before = replay.retained(cascade_count - 1);
        const std::uint64_t far_frame_before = far_before ? far_before->frame : ~0ull;
        api(s.event->Issue(D3DISSUE_END), "Issue"); f.wait(s.event.p);
        LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
        const HRESULT replay_result = replay.execute_cascades(draws, object_count, lists, list_count, true, false, &replayed);
        api(s.event->Issue(D3DISSUE_END), "Issue"); f.wait(s.event.p);
        QueryPerformanceCounter(&end);
        const double replay_us = 1e6 * double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart);
        require(replay_result == S_OK && replayed.drawn == used, "the cascade transaction replayed every issue");
        f.compare(before_state, f.snapshot(), "cascade_replay");
        for (unsigned c = 0; c < cascade_count; ++c) {
            require(replayed.drawn_map[c] == (replays[c] ? per_cascade[c] : 0u), "per-cascade draws");
            if (replays[c]) { require(replay.retained(c) == nullptr, "a rewritten map retains nothing until the owner retains again"); replay.retain(c, bases[c], frame, replayed.drawn_map[c]); map_frames[c] = frame; }
        }
        const r::ShadowReplayRetained* far_kept = replay.retained(cascade_count - 1);
        if (far_skipped && !script.reset_before) require(far_kept && far_kept->frame == far_frame_before && far_frame_before + 1 == frame, "the skipped far cascade keeps the previous frame's basis");
        if (script.reset_before) require(far_kept == nullptr, "after the Reset the skipped far cascade is absent");
        // The apply: retained basis x current camera; the far map may be one frame old.
        r::SunShadowCascadeFrame in{};
        in.depth_share = s.rt2.p; in.target = s.color_surface.p; in.width = sun_apply_w; in.height = sun_apply_h;
        in.m00 = s.camera.m00; in.m11 = s.camera.m11; in.m20 = m20; in.m21 = m21; in.m22 = s.m22; in.m32 = s.m32;
        in.jitter_index = script.jitter_index; in.exponent = script.exponent; in.planar_step = .05f; in.count = cascade_count;
        for (unsigned c = 0; c < cascade_count; ++c) {
            const auto* kept = replay.retained(c);
            const bool valid = kept && (kept->frame == frame || (c + 1 == cascade_count && kept->frame + 1 == frame));
            require(r::shadow_replay_view_rows(s.camera, valid ? kept->basis : bases[c], set.cascades[c], in.cascades[c].rows), "the view -> sun rows build");
            in.cascades[c].valid = valid; in.cascades[c].map = valid ? replay.map_texture(c) : nullptr;
            in.cascades[c].bias_constant = biases[c].constant; in.cascades[c].bias_max = biases[c].max;
        }
        r::SunShadowApplyResult skipped{};
        { auto absent = in; for (auto& c : absent.cascades) { c.valid = false; c.map = nullptr; }
          require(s.pass.execute_cascades(absent, &skipped) == S_FALSE && skipped.skipped && !std::strcmp(skipped.skipped_reason, "absent"), "no valid cascade skips the quad"); }
        { auto wrong = in; wrong.cascades[0].map = s.rt2.p; wrong.cascades[0].valid = true; require(s.pass.execute_cascades(wrong, &skipped) == S_FALSE && !std::strcmp(skipped.skipped_reason, "format"), "a G32R32F cascade map skips the quad"); }
        { auto wrong = in; wrong.count = 5; require(s.pass.execute_cascades(wrong, &skipped) == S_FALSE && !std::strcmp(skipped.skipped_reason, "input"), "five cascades skip the quad"); }
        { auto wrong = in; wrong.caller_stateblock_recording = true; require(s.pass.execute_cascades(wrong, &skipped) == S_FALSE && skipped.skipped, "a recording caller skips the quad"); }
        { auto wrong = in; wrong.m22 = .5f; require(s.pass.execute_cascades(wrong, &skipped) == S_FALSE && !std::strcmp(skipped.skipped_reason, "params"), "an invalid projection skips the quad"); }
        require(sun_apply_read(f, s) == s.before, "the skipped executions leave the target byte-identical");
        f.compare(before_state, f.snapshot(), "cascade_apply_skipped");
        const bool caller_scene_open = (frame % 2) == 0;
        in.caller_scene_open = caller_scene_open;
        if (!caller_scene_open) api(f.d->EndScene(), "EndScene before the quad");
        api(s.event->Issue(D3DISSUE_END), "Issue"); f.wait(s.event.p);
        QueryPerformanceCounter(&begin);
        r::SunShadowApplyResult result{};
        const HRESULT hr = s.pass.execute_cascades(in, &result);
        api(s.event->Issue(D3DISSUE_END), "Issue"); f.wait(s.event.p);
        QueryPerformanceCounter(&end);
        const double us = 1e6 * double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart);
        unsigned valid_count = 0; for (unsigned c = 0; c < cascade_count; ++c) valid_count += in.cascades[c].valid;
        std::printf("SUNAPPLY_TIME frame=%u us=%.1f result=%08lx applied=%u stage=%u bound=%u caller_scene_open=%u replay_us=%.1f issues=%u\n", frame, us, hr, result.applied, unsigned(result.failed),
                    result.cascades_bound, caller_scene_open, replay_us, used);
        require(hr == S_OK && result.applied && !result.skipped && result.cascades_bound == valid_count, "the cascade quad applied");
        f.compare(before_state, f.snapshot(), "cascade_apply");
        if (caller_scene_open) api(f.d->EndScene(), "EndScene");
        else { api(f.d->BeginScene(), "BeginScene after the quad"); api(f.d->EndScene(), "EndScene after the quad"); }
        s.after = sun_apply_read(f, s);
        // After the Reset the only cascade holding this scale's shadow is absent: nothing may darken.
        if (script.reset_before) require(s.after == s.before, "an absent far cascade leaves its pixels lit (the target byte-identical)");
        else require(s.after != s.before, "the cascades shadow the target");
        unsigned untouched_diff = 0;
        for (std::size_t p = 0; p < std::size_t(sun_apply_w) * sun_apply_h; ++p)
            if (s.rt2_data[p * 2 + 1] <= 0.f && std::memcmp(&s.before[p * 8], &s.after[p * 8], 8) != 0) ++untouched_diff;
        require(untouched_diff == 0, "sentinel and share-free pixels are byte-identical");
        if (frame == 8) { reset_reference_before = s.before; reset_reference_after = s.after; }
        if (frame == 10) require(s.before == reset_reference_before && s.after == reset_reference_after, "the replay after the Reset frames equals the frame before the Reset byte for byte");
        // The record: shared inputs, the scene, per cascade what the twin needs and the counters.
        std::printf("SUNAPPLY_CASCADES frame=%u case=%c width=%u height=%u cascades=%u scale=%.9g elevation=%g jitter_index=%u exponent=%.9g planar_step=%.9g budget=%u issues=%u far_replayed=%u far_frame=%lld "
                    "m00=%.9g m11=%.9g m20=%.9g m21=%.9g m22=%.9g m32=%.9g camera=%.9g,%.9g,%.9g cam_right=%.9g,%.9g,%.9g cam_up=%.9g,%.9g,%.9g cam_forward=%.9g,%.9g,%.9g sun=%.9g,%.9g,%.9g "
                    "right=%.9g,%.9g,%.9g up=%.9g,%.9g,%.9g receivers=%u share_free=%u sentinels=%u masks=%u,%u,%u legacy_high=%d boxes=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
                    frame, script.name, sun_apply_w, sun_apply_h, cascade_count, scale, double(script.elevation_deg), script.jitter_index, double(script.exponent), .05, script.budget, issues,
                    unsigned(replays[cascade_count - 1]), far_kept || replays[cascade_count - 1] ? static_cast<long long>(map_frames[cascade_count - 1]) : -1ll,
                    double(s.camera.m00), double(s.camera.m11), double(m20), double(m21), double(s.m22), double(s.m32),
                    s.position.x, s.position.y, s.position.z, s.right.x, s.right.y, s.right.z, s.up.x, s.up.y, s.up.z, s.forward.x, s.forward.y, s.forward.z, double(sun[0]), double(sun[1]), double(sun[2]),
                    double(bases[0].right[0]), double(bases[0].right[1]), double(bases[0].right[2]), double(bases[0].up[0]), double(bases[0].up[1]), double(bases[0].up[2]),
                    receivers, share_free, sentinels, masks[0], masks[1], masks[2], legacy_high,
                    boxes[0].lo[0], boxes[0].lo[1], boxes[0].lo[2], boxes[0].hi[0], boxes[0].hi[1], boxes[0].hi[2]);
        for (unsigned b = 1; b < box_count; ++b) std::printf(";%.9g,%.9g,%.9g,%.9g,%.9g,%.9g", boxes[b].lo[0], boxes[b].lo[1], boxes[b].lo[2], boxes[b].hi[0], boxes[b].hi[1], boxes[b].hi[2]);
        if (point) {
            const Vec3 ground{s.position.x, 0., s.position.z}, ahead = normalize(Vec3{s.forward.x, 0., s.forward.z});
            std::printf(" light=%.12g,%.12g,%.12g light_distance=%.9g point_agreement_deg=%.6f point_rederived=%u mask3=%u suns=%.9g,%.9g,%.9g;%.9g,%.9g,%.9g;%.9g,%.9g,%.9g landings=", light.x, light.y, light.z,
                        cascade_point_distance, point_agreement, point_rederived, masks[3], suns[0], suns[1], suns[2], suns[4], suns[5], suns[6], suns[8], suns[9], suns[10]);
            for (unsigned b = 0; b < 3; ++b) { const Vec3 g = ground + ahead * cascade_point_track[b]; std::printf("%s%.9g,%.9g,%.9g,%.9g", b ? ";" : "", g.x, g.z, cascade_point_lift[b], cascade_point_half[b]); }
        }
        for (unsigned c = 0; c < cascade_count; ++c) {
            const auto& k = in.cascades[c];
            std::printf(" c%u=%u draws%u=%u valid%u=%u map_frame%u=%lld extent%u=%.9g bias%u=%.9g bias_max%u=%.9g texel_world%u=%.9g rows%u=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
                        c, per_cascade[c], c, replayed.drawn_map[c], c, unsigned(k.valid), c, k.valid ? static_cast<long long>(map_frames[c]) : -1ll, c, double(set.cascades[c].half_extent),
                        c, double(k.bias_constant), c, double(k.bias_max), c, double(biases[c].texel_world), c,
                        double(k.rows[0]), double(k.rows[1]), double(k.rows[2]), double(k.rows[3]), double(k.rows[4]), double(k.rows[5]), double(k.rows[6]), double(k.rows[7]),
                        double(k.rows[8]), double(k.rows[9]), double(k.rows[10]), double(k.rows[11]));
        }
        std::printf("\n");
        sun_apply_write("before.rgba16f", frame, s.before.data(), s.before.size());
        sun_apply_write("after.rgba16f", frame, s.after.data(), s.after.size());
        sun_apply_write("rt2.g32r32f", frame, s.rt2_data.data(), s.rt2_data.size() * 4);
        for (unsigned c = 0; c < cascade_count; ++c) {
            if (!replays[c]) continue; // a retained map's file is the one of the frame it was replayed on
            const std::vector<float> map = cascade_read_map(f, replay.map_surface(c), map_copy.p, cascade_map);
            // The pancaked occluder of case f is the only thing at the near plane (depth exactly 0).
            if (c == 0) { unsigned flattened = 0; for (float v : map) flattened += v == 0.f; require(script.name == 'f' ? flattened >= 50 : flattened == 0, "only the pancaked occluder lies on the near plane"); }
            char name[32]; std::snprintf(name, sizeof name, "map%u.r32f", c);
            sun_apply_write(name, frame, map.data(), map.size() * 4);
        }
        api(f.d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++f.frame; ++f.frames_since_reset;
    }
    // The bounds pass per draw: the single-map verdict against the cascade mask
    // (one corner transform either way, then 6 compares per cascade).
    {
        r::ShadowCascadeBounds bounds{}; r::ShadowReplayCascade single{}; r::ShadowReplayBasis basis{}; float rows[12];
        const float sun[4] = {.30151134f, .90453403f, -.30151134f, 0.f};
        float clip[16] = {s.camera.m00, 0, 0, 0, 0, s.camera.m11, 0, 0, 0, 0, 1, 0, 0, 0, 1, 5};
        r::ShadowCascadeSet four{};
        require(r::shadow_cascade_set(r::shadow_cascade_extent_defaults, 4, nullptr, nullptr, 640, four) && r::shadow_cascade_bounds(s.camera, sun, four, bounds) &&
                r::shadow_replay_basis(s.camera, sun, single, basis) && r::shadow_replay_view_rows(s.camera, basis, single, rows), "the bench inputs build");
        constexpr unsigned rounds = 2000000;
        const float lo[3] = {-3, -2, -1}, hi[3] = {4, 5, 6};
        // The same four cascades under four different suns (per-cascade rows: the corners transformed once per cascade).
        r::ShadowCascadeBounds split{};
        float split_suns[16]; for (unsigned c = 0; c < 4; ++c) { std::memcpy(split_suns + c * 4, sun, sizeof sun); split_suns[c * 4] += .001f * float(c); }
        require(r::shadow_cascade_bounds_suns(s.camera, split_suns, four, split) && !split.shared && bounds.shared, "the per-cascade bench bounds build");
        long long verdicts = 0, masks = 0, split_masks = 0;
        LARGE_INTEGER t0, t1, t2, t3;
        QueryPerformanceCounter(&t0);
        for (unsigned i = 0; i < rounds; ++i) { clip[3] = float(i & 1023u); verdicts += r::shadow_replay_bounds_verdict(s.camera, clip, rows, lo, hi); }
        QueryPerformanceCounter(&t1);
        for (unsigned i = 0; i < rounds; ++i) { clip[3] = float(i & 1023u); masks += r::shadow_cascade_bounds_mask(s.camera, clip, bounds, lo, hi); }
        QueryPerformanceCounter(&t2);
        for (unsigned i = 0; i < rounds; ++i) { clip[3] = float(i & 1023u); split_masks += r::shadow_cascade_bounds_mask(s.camera, clip, split, lo, hi); }
        QueryPerformanceCounter(&t3);
        std::printf("SUNAPPLY_BOUNDS_BENCH rounds=%u verdict_ns=%.1f mask_ns=%.1f cascades=4 verdicts=%lld masks=%lld split_mask_ns=%.1f split_masks=%lld\n", rounds,
                    1e9 * double(t1.QuadPart - t0.QuadPart) / double(frequency.QuadPart) / rounds, 1e9 * double(t2.QuadPart - t1.QuadPart) / double(frequency.QuadPart) / rounds, verdicts, masks,
                    1e9 * double(t3.QuadPart - t2.QuadPart) / double(frequency.QuadPart) / rounds, split_masks);
    }
    sun_apply_release_targets(s); map_copy.reset();
    replay.detach(); s.pass.detach();
    require(s.pass.references() == 0 && replay.references() == 0, "detach releases everything");
}
} // namespace
