// Sun-shadow apply quad script ("sunapply" mode; docs/architecture/
// legacy-sun-application.md, section 3.3). The production SunShadowApplyPass
// is linked into the fixture and driven directly (no proxy wiring): a
// synthetic G32R32F RT2 (device depth of a box on a plane under the jittered
// projection, a share pattern with share-free columns and sentinel sky) and a
// synthetic R32F map (the same scene ray-cast along the sun through the
// cascade shadow_replay_basis builds for the fixture camera) at three sun
// elevations, applied to a 128x128 FP16 target filled with 64 colour tiles.
// Frame 0 maps everything far (the target must stay byte-identical), frame 1
// maps everything at depth 0 (f = 0 everywhere inside the cascade), frames
// 2-4 are the general case (frame 4 with the converted-material exponent),
// a Reset precedes frame 5 which repeats frame 4 and must be byte-identical
// to it. Every frame's inputs and readbacks are written beside the
// executable for the runner's CPU projection of the same map; the fixture
// itself proves the skip paths (missing map, RT2/target format, size and
// device mismatches, recording caller, pending Reset), the state restoration
// around hostile caller state, the pass-owned scene on odd frames, the
// ShadowReplayPass accessors the wiring consumes, and the timing.
// Wide configuration (X3M_FIXTURE_SUNAPPLY_WIDE=1 with X3M_SHADOW_REPLAY_EXTENT,
// X3M_SHADOW_REPLAY_DEPTH_HALF and X3M_SHADOW_REPLAY_SIZE, the production variables): the scene is scaled
// by extent / 5 (box, camera, near and far plane), so the geometry keeps its
// proportions while the map resolves it at the requested world texel, and the
// bias comes from the production world-unit conversion (sun_shadow_apply_bias
// with X3M_SUN_SHADOW_BIAS_UNITS, default) instead of the literal fixture
// constants .003 / .01 of the default run, whose output stays byte-identical.
// Headers: sun_shadow_apply_pass.h and shadow_replay_projection.h, included by the fixture at file scope.
namespace {
constexpr unsigned sun_apply_w = 128, sun_apply_h = 128, sun_apply_frames = 6, sun_apply_reset_before = 5;
unsigned sun_apply_map = 256;  // map side this run (default 256; the wide run's X3M_SHADOW_REPLAY_SIZE)
double sun_apply_scale = 1.;   // scene scale this run (1; extent / 5 in the wide run)
constexpr float sun_apply_box_min[3] = {-1.f, 0.f, -1.f}, sun_apply_box_max[3] = {1.f, 2.f, 1.f}; // the unit scene
double sun_apply_box_lo[3] = {-1., 0., -1.}, sun_apply_box_hi[3] = {1., 2., 1.};                 // scaled
constexpr float sun_apply_camera[3] = {2.5f, 3.5f, -7.f}, sun_apply_look[3] = {0.f, .8f, 0.f};
constexpr float sun_apply_azimuth_deg = 340.f;
struct SunApplyScript { float elevation_deg; unsigned map_mode; unsigned jitter_index; float jx, jy; float exponent; };
constexpr SunApplyScript sun_apply_script[sun_apply_frames] = {
    {50.f, 0, 0, 0.f, 0.f, 1.f},              // map far: identity
    {50.f, 1, 1, .25f, -.125f, 1.f},          // map at depth 0: f = 0 inside the cascade
    {30.f, 2, 2, -.375f, .25f, 1.f},
    {50.f, 2, 3, .125f, .375f, 1.f},
    {70.f, 2, 5, -.25f, -.375f, 1.f / 2.2f},
    {70.f, 2, 5, -.25f, -.375f, 1.f / 2.2f},  // after the Reset: byte-identical to frame 4
};
struct Vec3 { double x, y, z; };
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 a) { const double n = std::sqrt(dot(a, a)); return a * (1. / n); }
// Nearest positive hit of the plane y = 0 and the box along o + t dir, or a negative value.
double sun_apply_hit(Vec3 o, Vec3 dir) {
    double best = -1.;
    if (dir.y < 0. && o.y > 0.) best = -o.y / dir.y;
    double enter = -1e30, leave = 1e30;
    const double* lo = sun_apply_box_lo; const double* hi = sun_apply_box_hi;
    const double oo[3] = {o.x, o.y, o.z}, dd[3] = {dir.x, dir.y, dir.z};
    for (unsigned k = 0; k < 3; ++k) {
        if (std::fabs(dd[k]) < 1e-12) { if (oo[k] < lo[k] || oo[k] > hi[k]) return best; continue; }
        double t0 = (lo[k] - oo[k]) / dd[k], t1 = (hi[k] - oo[k]) / dd[k];
        if (t0 > t1) std::swap(t0, t1);
        enter = std::max(enter, t0); leave = std::min(leave, t1);
    }
    if (enter <= leave && leave > 0.) { const double t = enter > 0. ? enter : leave; if (best < 0. || t < best) best = t; }
    return best;
}
struct SunApplyState {
    x3m::renderer::SunShadowApplyPass pass;
    Com<IDirect3DTexture9> color, rt2, map, rt2_sys, map_sys;
    Com<IDirect3DSurface9> color_surface, readback;
    Com<IDirect3DQuery9> event;
    std::vector<float> rt2_data, map_data;           // this frame's CPU inputs
    std::vector<unsigned char> before, after, previous_after;
    x3m::renderer::CameraState camera{};
    Vec3 right{}, up{}, forward{}, position{};
    float m22 = 0, m32 = 0;
};
void sun_apply_create_targets(Fixture& f, SunApplyState& s) {
    api(f.d->CreateTexture(sun_apply_w, sun_apply_h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &s.color.p, nullptr), "CreateTexture FP16 target");
    api(s.color->GetSurfaceLevel(0, &s.color_surface.p), "FP16 target level");
    api(f.d->CreateTexture(sun_apply_w, sun_apply_h, 1, 0, D3DFMT_G32R32F, D3DPOOL_DEFAULT, &s.rt2.p, nullptr), "CreateTexture RT2");
    api(f.d->CreateTexture(sun_apply_map, sun_apply_map, 1, 0, D3DFMT_R32F, D3DPOOL_DEFAULT, &s.map.p, nullptr), "CreateTexture map");
    api(f.d->CreateTexture(sun_apply_w, sun_apply_h, 1, 0, D3DFMT_G32R32F, D3DPOOL_SYSTEMMEM, &s.rt2_sys.p, nullptr), "CreateTexture RT2 sysmem");
    api(f.d->CreateTexture(sun_apply_map, sun_apply_map, 1, 0, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &s.map_sys.p, nullptr), "CreateTexture map sysmem");
    api(f.d->CreateOffscreenPlainSurface(sun_apply_w, sun_apply_h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &s.readback.p, nullptr), "CreateOffscreenPlainSurface FP16");
    api(f.d->CreateQuery(D3DQUERYTYPE_EVENT, &s.event.p), "CreateQuery EVENT");
}
void sun_apply_release_targets(SunApplyState& s) {
    s.event.reset(); s.readback.reset(); s.map_sys.reset(); s.rt2_sys.reset(); s.map.reset(); s.rt2.reset(); s.color_surface.reset(); s.color.reset();
}
void sun_apply_upload(Fixture& f, IDirect3DTexture9* sys, IDirect3DTexture9* target, const std::vector<float>& data, unsigned width, unsigned height, unsigned floats) {
    D3DLOCKED_RECT lock{}; api(sys->LockRect(0, &lock, nullptr, 0), "LockRect sysmem");
    for (unsigned y = 0; y < height; ++y) std::memcpy(static_cast<char*>(lock.pBits) + y * lock.Pitch, &data[std::size_t(y) * width * floats], std::size_t(width) * floats * 4);
    api(sys->UnlockRect(0), "UnlockRect sysmem");
    api(f.d->UpdateTexture(sys, target), "UpdateTexture");
}
std::vector<unsigned char> sun_apply_read(Fixture& f, SunApplyState& s) {
    api(f.d->GetRenderTargetData(s.color_surface.p, s.readback.p), "GetRenderTargetData FP16");
    D3DLOCKED_RECT lock{}; api(s.readback->LockRect(&lock, nullptr, D3DLOCK_READONLY), "LockRect FP16");
    std::vector<unsigned char> image(std::size_t(sun_apply_w) * sun_apply_h * 8);
    for (unsigned y = 0; y < sun_apply_h; ++y) std::memcpy(&image[std::size_t(y) * sun_apply_w * 8], static_cast<const unsigned char*>(lock.pBits) + y * lock.Pitch, sun_apply_w * 8);
    s.readback->UnlockRect();
    return image;
}
void sun_apply_write(const char* name, unsigned long long frame, const void* data, std::size_t bytes) {
    char path[64]; std::snprintf(path, sizeof path, "sunapply_%llu_%s", frame, name);
    FILE* file = std::fopen(path, "wb"); require(file != nullptr, "sunapply file written");
    std::fwrite(data, 1, bytes, file); std::fclose(file);
}
// 64 colour tiles through Clear rects (the FP16 values the target actually
// stores are read back as the reference C, so no conversion law is assumed).
void sun_apply_fill(Fixture& f, SunApplyState& s) {
    api(f.d->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface null");
    api(f.d->SetRenderTarget(0, s.color_surface.p), "SetRenderTarget FP16");
    for (unsigned k = 0; k < 64; ++k) {
        const D3DRECT rect{LONG((k % 8) * (sun_apply_w / 8)), LONG((k / 8) * (sun_apply_h / 8)), LONG((k % 8 + 1) * (sun_apply_w / 8)), LONG((k / 8 + 1) * (sun_apply_h / 8))};
        const D3DCOLOR color = D3DCOLOR_ARGB(255, (k * 37 + 11) % 256, (k * 91 + 5) % 256, (k * 53 + 23) % 256);
        api(f.d->Clear(1, &rect, D3DCLEAR_TARGET, color, 1.f, 0), "Clear tile");
    }
    api(f.d->SetRenderTarget(0, f.back.p), "SetRenderTarget back");
}
void run_sun_apply_integration(Fixture& f) {
    SunApplyState s;
    // The configuration: default (5 / 8 / 256, literal bias) or wide (the
    // production variables, the scene scaled by extent / 5, the bias resolved).
    x3m::renderer::ShadowReplayCascade cascade{}; cascade.half_extent = 5.f; cascade.depth_half_range = 8.f; cascade.size = 256;
    bool wide = false; double bias_units = x3m::renderer::sun_shadow_bias_units_default, clamp_texels = x3m::renderer::sun_shadow_bias_clamp_texels_default;
    {
        char text[32]{};
        const bool asked = GetEnvironmentVariableA("X3M_FIXTURE_SUNAPPLY_WIDE", text, sizeof text) == 1 && text[0] == '1';
        if (asked) {
            require(GetEnvironmentVariableA("X3M_SHADOW_REPLAY_EXTENT", text, sizeof text) > 0, "the wide run names X3M_SHADOW_REPLAY_EXTENT");
            const float extent = std::strtof(text, nullptr);
            require(extent >= x3m::renderer::shadow_replay_extent_min && extent <= x3m::renderer::shadow_replay_extent_max, "X3M_SHADOW_REPLAY_EXTENT within the production range");
            wide = true; cascade.half_extent = extent; cascade.depth_half_range = x3m::renderer::shadow_replay_depth_half_default; cascade.size = x3m::renderer::shadow_replay_size_default;
            if (GetEnvironmentVariableA("X3M_SHADOW_REPLAY_DEPTH_HALF", text, sizeof text) > 0) cascade.depth_half_range = std::strtof(text, nullptr);
            if (GetEnvironmentVariableA("X3M_SHADOW_REPLAY_SIZE", text, sizeof text) > 0) cascade.size = unsigned(std::atoi(text));
            if (GetEnvironmentVariableA("X3M_SUN_SHADOW_BIAS_UNITS", text, sizeof text) > 0) bias_units = std::strtod(text, nullptr);
            if (GetEnvironmentVariableA("X3M_SUN_SHADOW_BIAS_CLAMP_TEXELS", text, sizeof text) > 0) clamp_texels = std::strtod(text, nullptr);
            require(cascade.depth_half_range >= x3m::renderer::shadow_replay_depth_half_min && cascade.depth_half_range <= x3m::renderer::shadow_replay_depth_half_max
                    && cascade.size >= x3m::renderer::shadow_replay_size_min && cascade.size <= x3m::renderer::shadow_replay_size_max, "wide cascade within the production ranges");
        }
    }
    sun_apply_map = cascade.size; sun_apply_scale = wide ? double(cascade.half_extent) / 5. : 1.;
    for (unsigned k = 0; k < 3; ++k) { sun_apply_box_lo[k] = sun_apply_box_min[k] * sun_apply_scale; sun_apply_box_hi[k] = sun_apply_box_max[k] * sun_apply_scale; }
    float bias_constant = .003f, bias_max = .01f, texel_world = 0.f;
    if (wide) {
        x3m::renderer::SunShadowBias bias{};
        require(x3m::renderer::sun_shadow_apply_bias(bias_units, clamp_texels, cascade.half_extent, cascade.depth_half_range, cascade.size, bias), "the world-unit bias resolves for the wide cascade");
        bias_constant = bias.constant; bias_max = bias.max; texel_world = bias.texel_world;
    }
    std::printf("SUNAPPLY_CONFIG wide=%u extent=%.9g depth_half=%.9g map_size=%u scale=%.9g bias_units=%.9g clamp_texels=%.9g texel_world=%.9g bias_constant=%.9g bias_max=%.9g\n",
                unsigned(wide), double(cascade.half_extent), double(cascade.depth_half_range), sun_apply_map, sun_apply_scale, bias_units, clamp_texels, double(texel_world), double(bias_constant), double(bias_max));
    D3DCAPS9 caps{}; api(f.d->GetDeviceCaps(&caps), "GetDeviceCaps");
    const HRESULT attached = s.pass.attach(f.d.p, nullptr, caps, D3DFMT_X8R8G8B8, D3DFMT_A16B16G16R16F);
    std::printf("SUNAPPLY_DEVICE attached=%u result=%08lx reason=%s slots=%u references=%u\n", SUCCEEDED(attached), attached, s.pass.caps().reason, s.pass.caps().program_slots, s.pass.references());
    require(SUCCEEDED(attached) && s.pass.caps().enabled, "the apply pass attaches on this device");
    // Camera: position, look-at, D3D left-handed view axes (columns of R).
    const Vec3 look = Vec3{sun_apply_look[0], sun_apply_look[1], sun_apply_look[2]} * sun_apply_scale;
    s.position = Vec3{sun_apply_camera[0], sun_apply_camera[1], sun_apply_camera[2]} * sun_apply_scale;
    s.forward = normalize(look - s.position);
    s.right = normalize(cross(Vec3{0, 1, 0}, s.forward)); s.up = cross(s.forward, s.right);
    const double fov_half = 25. * 3.14159265358979323846 / 180., near_z = 1. * sun_apply_scale, far_z = 50. * sun_apply_scale;
    s.camera.valid = true; s.camera.m00 = s.camera.m11 = float(1. / std::tan(fov_half));
    s.m22 = float(far_z / (far_z - near_z)); s.m32 = float(-near_z * far_z / (far_z - near_z));
    const Vec3 axes[3] = {s.right, s.up, s.forward};
    // r[i*3+j] = world component i of view axis j; t = -R^T position.
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j) {
        const Vec3 a = axes[j]; s.camera.r[i * 3 + j] = float(i == 0 ? a.x : i == 1 ? a.y : a.z);
    }
    for (unsigned j = 0; j < 3; ++j) s.camera.t[j] = float(-dot(axes[j], s.position));
    cascade.forward_offset = float(std::sqrt(dot(look - s.position, look - s.position)));
    sun_apply_create_targets(f, s);
    // What the wiring consumes from ShadowReplayPass: the rows round-trip
    // detached and attached, before_reset invalidates them and drops the map,
    // prepare() publishes an R32F map texture of the attached size.
    {
        x3m::renderer::ShadowReplayPass replay;
        const float rows[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        require(replay.view_rows() == nullptr && replay.map_texture() == nullptr, "a detached replay pass publishes no rows and no map");
        replay.set_view_rows(rows);
        require(replay.view_rows() != nullptr && std::memcmp(replay.view_rows(), rows, sizeof rows) == 0, "set_view_rows/view_rows round-trip");
        replay.before_reset();
        require(replay.view_rows() == nullptr, "before_reset invalidates the rows (detached)");
        replay.set_view_rows(nullptr);
        require(replay.view_rows() == nullptr, "set_view_rows(null) invalidates the rows");
        api(replay.attach(f.d.p, nullptr, caps, D3DFMT_X8R8G8B8, sun_apply_map), "ShadowReplayPass attach");
        api(replay.prepare(), "ShadowReplayPass prepare");
        replay.set_view_rows(rows);
        D3DSURFACE_DESC desc{};
        require(replay.map_texture() != nullptr && SUCCEEDED(replay.map_texture()->GetLevelDesc(0, &desc)) && desc.Width == sun_apply_map && desc.Height == sun_apply_map &&
                desc.Format == replay.caps().map_format && std::memcmp(replay.view_rows(), rows, sizeof rows) == 0, "the prepared replay pass publishes its map texture and the rows");
        replay.before_reset();
        require(replay.view_rows() == nullptr && replay.map_texture() == nullptr && replay.reset_pending(), "before_reset drops the map and the rows");
        replay.after_reset(S_OK);
        require(!replay.reset_pending() && replay.map_texture() == nullptr, "after_reset leaves the map to prepare()");
        replay.detach();
        require(replay.references() == 0 && replay.view_rows() == nullptr, "detach releases the replay pass");
    }
    // A second device of the same window for the same_device refusal.
    Com<IDirect3DDevice9> other; Com<IDirect3DTexture9> other_rt2;
    {
        D3DPRESENT_PARAMETERS pp = f.pp;
        api(f.factory->CreateDevice(0, D3DDEVTYPE_HAL, f.window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &other.p), "CreateDevice other");
        api(other->CreateTexture(sun_apply_w, sun_apply_h, 1, 0, D3DFMT_G32R32F, D3DPOOL_DEFAULT, &other_rt2.p, nullptr), "CreateTexture RT2 other device");
    }
    s.rt2_data.resize(std::size_t(sun_apply_w) * sun_apply_h * 2); s.map_data.resize(std::size_t(sun_apply_map) * sun_apply_map);
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    for (unsigned frame = 0; frame < sun_apply_frames; ++frame) {
        const SunApplyScript& script = sun_apply_script[frame];
        if (frame == sun_apply_reset_before) {
            // Reset protocol: the block goes, execute is refused until after_reset, the targets are rebuilt by their owners.
            sun_apply_release_targets(s);
            s.pass.before_reset();
            require(s.pass.reset_pending() && s.pass.references() == 3, "before_reset releases the block and keeps the programs");
            x3m::renderer::SunShadowApplyResult refused{};
            x3m::renderer::SunShadowApplyFrame pending{};
            require(s.pass.execute(pending, &refused) == S_FALSE && refused.skipped && !std::strcmp(refused.skipped_reason, "reset_pending"), "execute is refused while the Reset is pending");
            f.reset();
            s.pass.after_reset(S_OK);
            require(!s.pass.reset_pending(), "after_reset clears the pending flag");
            sun_apply_create_targets(f, s);
        }
        // Sun direction (object -> light) and the cascade of this frame.
        const double az = sun_apply_azimuth_deg * 3.14159265358979323846 / 180., el = double(script.elevation_deg) * 3.14159265358979323846 / 180.;
        const float sun[4] = {float(std::sin(az) * std::cos(el)), float(std::sin(el)), float(std::cos(az) * std::cos(el)), 0.f};
        x3m::renderer::ShadowReplayBasis basis{};
        require(x3m::renderer::shadow_replay_basis(s.camera, sun, cascade, basis), "the cascade basis builds for the fixture camera");
        float rows[12]{};
        require(x3m::renderer::shadow_replay_view_rows(s.camera, basis, cascade, rows), "the view -> sun rows build");
        const Vec3 s_right{basis.right[0], basis.right[1], basis.right[2]}, s_up{basis.up[0], basis.up[1], basis.up[2]}, s_fwd{basis.forward[0], basis.forward[1], basis.forward[2]},
                   s_center{basis.center[0], basis.center[1], basis.center[2]};
        // RT2: the jittered projection's device depth of the nearest surface and the share pattern.
        const float m20 = 2.f * script.jx / float(sun_apply_w), m21 = -2.f * script.jy / float(sun_apply_h);
        unsigned receivers = 0, sentinels = 0, share_free = 0;
        for (unsigned j = 0; j < sun_apply_h; ++j) for (unsigned i = 0; i < sun_apply_w; ++i) {
            const double ndc_x = (i + .5) / sun_apply_w * 2. - 1., ndc_y = 1. - (j + .5) / sun_apply_h * 2.;
            const Vec3 dv{(ndc_x - m20) / s.camera.m00, (ndc_y - m21) / s.camera.m11, 1.};
            const Vec3 dir = s.right * dv.x + s.up * dv.y + s.forward * dv.z;
            const double t = sun_apply_hit(s.position, dir);
            float* px = &s.rt2_data[(std::size_t(j) * sun_apply_w + i) * 2];
            if (t <= 0.) { px[0] = -1.f; px[1] = 0.f; ++sentinels; continue; }
            const Vec3 hit = s.position + dir * t;
            const bool on_box = hit.y > 1e-6;
            px[0] = float(double(s.m22) + double(s.m32) / t);
            px[1] = (i % 9 == 4) ? 0.f : on_box ? .85f : float(.2 + .75 * ((i * 3 + j * 5) % 17) / 16.);
            if (px[1] <= 0.f) ++share_free; else ++receivers;
        }
        // The map: depth along the sun through every texel of the cascade (1 where nothing is hit).
        for (unsigned b = 0; b < sun_apply_map; ++b) for (unsigned a = 0; a < sun_apply_map; ++a) {
            float value = 1.f;
            if (script.map_mode == 1) value = 0.f;
            else if (script.map_mode == 2) {
                const double x = (a + .5) / sun_apply_map * 2. - 1., y = 1. - (b + .5) / sun_apply_map * 2.;
                const Vec3 q = s_center + s_right * (x * cascade.half_extent) + s_up * (y * cascade.half_extent) - s_fwd * cascade.depth_half_range;
                const double t = sun_apply_hit(q, s_fwd);
                if (t > 0.) value = float(std::min(1., t / (2. * cascade.depth_half_range)));
            }
            s.map_data[std::size_t(b) * sun_apply_map + a] = value;
        }
        sun_apply_upload(f, s.rt2_sys.p, s.rt2.p, s.rt2_data, sun_apply_w, sun_apply_h, 2);
        sun_apply_upload(f, s.map_sys.p, s.map.p, s.map_data, sun_apply_map, sun_apply_map, 1);
        api(f.d->BeginScene(), "BeginScene");
        sun_apply_fill(f, s);
        s.before = sun_apply_read(f, s);
        // Hostile caller state the quad must restore around itself.
        api(f.d->SetRenderTarget(0, f.back.p), "SetRenderTarget back"); api(f.d->SetDepthStencilSurface(f.depth.p), "SetDepthStencilSurface");
        api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE), "hostile blend"); api(f.d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA), "hostile srcblend");
        api(f.d->SetRenderState(D3DRS_ZENABLE, TRUE), "hostile z"); api(f.d->SetTexture(0, f.textures[0].p), "hostile texture 0"); api(f.d->SetTexture(1, s.map.p), "hostile texture 1");
        const RECT scissor{3, 5, 40, 41}; api(f.d->SetScissorRect(&scissor), "hostile scissor");
        const D3DVIEWPORT9 viewport{2, 3, Fixture::W - 8, Fixture::H - 9, .1f, .9f}; api(f.d->SetViewport(&viewport), "hostile viewport");
        api(f.d->SetPixelShader(f.flat.p), "hostile ps");
        const Snapshot before_state = f.snapshot();
        x3m::renderer::SunShadowApplyFrame in{};
        in.depth_share = s.rt2.p; in.map = s.map.p; in.target = s.color_surface.p; in.width = sun_apply_w; in.height = sun_apply_h;
        in.caller_scene_open = true;
        in.params.m00 = s.camera.m00; in.params.m11 = s.camera.m11; in.params.m20 = m20; in.params.m21 = m21; in.params.m22 = s.m22; in.params.m32 = s.m32;
        for (unsigned k = 0; k < 12; ++k) in.params.rows[k] = rows[k];
        in.params.jitter_index = script.jitter_index; in.params.exponent = script.exponent;
        in.params.bias_constant = bias_constant; in.params.bias_max = bias_max; in.params.planar_step = .05f;
        // Skip paths touch nothing: a missing map, a wrong RT2 format, a stateblock recording caller.
        x3m::renderer::SunShadowApplyResult skipped{};
        { auto missing = in; missing.map = nullptr; require(s.pass.execute(missing, &skipped) == S_FALSE && skipped.skipped && !std::strcmp(skipped.skipped_reason, "input"), "a missing map skips the quad"); }
        { auto wrong = in; wrong.depth_share = s.map.p; require(s.pass.execute(wrong, &skipped) == S_FALSE && skipped.skipped && !std::strcmp(skipped.skipped_reason, "format"), "an R32F RT2 skips the quad"); }
        { auto recording = in; recording.caller_stateblock_recording = true; require(s.pass.execute(recording, &skipped) == S_FALSE && skipped.skipped, "a recording caller skips the quad"); }
        { auto bad = in; bad.params.m22 = .5f; require(s.pass.execute(bad, &skipped) == S_FALSE && skipped.skipped && !std::strcmp(skipped.skipped_reason, "params"), "an invalid projection skips the quad"); }
        { auto wrong = in; wrong.target = f.back.p; require(s.pass.execute(wrong, &skipped) == S_FALSE && skipped.skipped && !std::strcmp(skipped.skipped_reason, "format"), "an A8R8G8B8 target skips the quad"); }
        { auto wrong = in; wrong.width = sun_apply_w / 2; require(s.pass.execute(wrong, &skipped) == S_FALSE && skipped.skipped && !std::strcmp(skipped.skipped_reason, "format"), "a width mismatch skips the quad"); }
        { auto wrong = in; wrong.height = sun_apply_h - 1; require(s.pass.execute(wrong, &skipped) == S_FALSE && skipped.skipped && !std::strcmp(skipped.skipped_reason, "format"), "a height mismatch skips the quad"); }
        { auto foreign = in; foreign.depth_share = other_rt2.p; require(s.pass.execute(foreign, &skipped) == S_FALSE && skipped.skipped && !std::strcmp(skipped.skipped_reason, "device"), "another device's RT2 skips the quad"); }
        require(sun_apply_read(f, s) == s.before, "the skipped executions leave the target byte-identical");
        f.compare(before_state, f.snapshot(), "sunapply_skipped");
        // Odd frames run the quad outside the caller's scene: the pass opens and closes its own.
        const bool caller_scene_open = (frame % 2) == 0;
        in.caller_scene_open = caller_scene_open;
        if (!caller_scene_open) api(f.d->EndScene(), "EndScene before the quad");
        // The quad, EVENT-synchronized and timed (CPU-inclusive, as the bench).
        api(s.event->Issue(D3DISSUE_END), "Issue"); f.wait(s.event.p);
        LARGE_INTEGER begin, end; QueryPerformanceCounter(&begin);
        x3m::renderer::SunShadowApplyResult result{};
        const HRESULT hr = s.pass.execute(in, &result);
        api(s.event->Issue(D3DISSUE_END), "Issue"); f.wait(s.event.p);
        QueryPerformanceCounter(&end);
        const double us = 1e6 * double(end.QuadPart - begin.QuadPart) / double(frequency.QuadPart);
        std::printf("SUNAPPLY_TIME frame=%llu us=%.1f result=%08lx applied=%u stage=%u map_size=%u caller_scene_open=%u\n", f.frame, us, hr, result.applied, unsigned(result.failed), result.map_size, caller_scene_open);
        require(hr == S_OK && result.applied && !result.skipped && result.map_size == sun_apply_map, "the quad applied");
        f.compare(before_state, f.snapshot(), "sunapply");
        if (caller_scene_open) api(f.d->EndScene(), "EndScene");
        else { api(f.d->BeginScene(), "BeginScene after the quad"); api(f.d->EndScene(), "EndScene after the quad"); } // a scene of the caller's own remains legal after the pass closed its scene
        s.after = sun_apply_read(f, s);
        if (script.map_mode == 0) require(s.after == s.before, "a far map leaves the target byte-identical");
        else require(s.after != s.before, "a shadowing map changes the target");
        if (frame == sun_apply_reset_before) require(s.after == s.previous_after, "the frame after the Reset equals the frame before it byte for byte");
        // Share-free and sentinel pixels are byte-identical in every frame.
        unsigned untouched_diff = 0;
        for (std::size_t p = 0; p < std::size_t(sun_apply_w) * sun_apply_h; ++p)
            if (s.rt2_data[p * 2 + 1] <= 0.f && std::memcmp(&s.before[p * 8], &s.after[p * 8], 8) != 0) ++untouched_diff;
        require(untouched_diff == 0, "sentinel and share-free pixels are byte-identical");
        std::printf("SUNAPPLY frame=%llu width=%u height=%u map_size=%u map_mode=%u elevation=%g jitter_index=%u exponent=%.9g bias_constant=%.9g bias_max=%.9g planar_step=%.9g "
                    "m00=%.9g m11=%.9g m20=%.9g m21=%.9g m22=%.9g m32=%.9g rows=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g "
                    "camera=%.9g,%.9g,%.9g cam_right=%.9g,%.9g,%.9g cam_up=%.9g,%.9g,%.9g cam_forward=%.9g,%.9g,%.9g sun=%.9g,%.9g,%.9g "
                    "right=%.9g,%.9g,%.9g up=%.9g,%.9g,%.9g forward=%.9g,%.9g,%.9g center=%.9g,%.9g,%.9g extent=%.9g depth_half=%.9g "
                    "box=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g receivers=%u share_free=%u sentinels=%u wide=%u scale=%.9g bias_units=%.9g texel_world=%.9g\n",
                    f.frame, sun_apply_w, sun_apply_h, sun_apply_map, script.map_mode, double(script.elevation_deg), script.jitter_index, double(script.exponent), double(bias_constant), double(bias_max), .05,
                    double(s.camera.m00), double(s.camera.m11), double(m20), double(m21), double(s.m22), double(s.m32),
                    double(rows[0]), double(rows[1]), double(rows[2]), double(rows[3]), double(rows[4]), double(rows[5]), double(rows[6]), double(rows[7]), double(rows[8]), double(rows[9]), double(rows[10]), double(rows[11]),
                    s.position.x, s.position.y, s.position.z, s.right.x, s.right.y, s.right.z, s.up.x, s.up.y, s.up.z, s.forward.x, s.forward.y, s.forward.z, double(sun[0]), double(sun[1]), double(sun[2]),
                    s_right.x, s_right.y, s_right.z, s_up.x, s_up.y, s_up.z, s_fwd.x, s_fwd.y, s_fwd.z, s_center.x, s_center.y, s_center.z, double(cascade.half_extent), double(cascade.depth_half_range),
                    sun_apply_box_lo[0], sun_apply_box_lo[1], sun_apply_box_lo[2], sun_apply_box_hi[0], sun_apply_box_hi[1], sun_apply_box_hi[2],
                    receivers, share_free, sentinels, unsigned(wide), sun_apply_scale, bias_units, double(texel_world));
        sun_apply_write("before.rgba16f", f.frame, s.before.data(), s.before.size());
        sun_apply_write("after.rgba16f", f.frame, s.after.data(), s.after.size());
        sun_apply_write("rt2.g32r32f", f.frame, s.rt2_data.data(), s.rt2_data.size() * 4);
        sun_apply_write("map.r32f", f.frame, s.map_data.data(), s.map_data.size() * 4);
        s.previous_after = s.after;
        api(f.d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++f.frame; ++f.frames_since_reset;
    }
    sun_apply_release_targets(s);
    other_rt2.reset(); other.reset();
    s.pass.detach();
    require(s.pass.references() == 0, "detach releases everything");
}
} // namespace
