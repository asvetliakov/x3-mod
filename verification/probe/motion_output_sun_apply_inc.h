// Sun-shadow apply quad helpers (docs/architecture/legacy-sun-application.md,
// section 3.3; shadow-cascades.md, section 4): the targets, the synthetic
// A32B32G32R32F RT2 of the lane, the raster latch and the colour tiles that
// the cascade scripts (motion_output_sun_apply_cascades_inc.h, "sunapply" mode
// with X3M_FIXTURE_SUNAPPLY_CASCADES) drive the production SunShadowApplyPass
// with. The single-map script (run_sun_apply_integration, the default and
// wide "sunapply" runs) was removed with the single shadow map on 2026-09-25
// (docs/architecture/directional-shadows.md, "Single map removed"); its skip,
// restoration, Reset and twin checks run on the cascade program there.
// Headers: sun_shadow_apply_pass.h and shadow_replay_projection.h, included by the fixture at file scope.
namespace {
constexpr unsigned sun_apply_w = 128, sun_apply_h = 128;
unsigned sun_apply_map = 256;  // map side this run (the cascade script sets it)
// The RT2 of the lane (docs/architecture/shadow-receiver-depth.md, the only encoding
// since 2026-09-18): A32B32G32R32F, .r = z/w (the -1 sentinel), .g = share, .b = .a = the
// view depth itself, which the quads read as the receiver depth. The records before the
// flip (G32R32F, depth_encoding=device) are gone; the twin keeps loading them by key.
constexpr unsigned sun_apply_lanes = 4;
D3DFORMAT sun_apply_rt2_format() { return D3DFMT_A32B32G32R32F; }
const char* sun_apply_rt2_file() { return "rt2.rgba32f"; }
const char* sun_apply_encoding_name() { return "linear"; }
// One RT2 texel: .r = device depth by the AO law (or the -1 sentinel), .g = share,
// .b = .a = the view depth t (the sentinel's -1 there too).
void sun_apply_rt2_texel(float* px, double m22, double m32, double t, float share) {
    if (t <= 0.) { px[0] = -1.f; px[1] = 0.f; px[2] = px[3] = -1.f; return; }
    px[0] = float(m22 + m32 / t); px[1] = share; px[2] = px[3] = float(t);
}
constexpr float sun_apply_box_min[3] = {-1.f, 0.f, -1.f}, sun_apply_box_max[3] = {1.f, 2.f, 1.f}; // the unit scene
constexpr float sun_apply_camera[3] = {2.5f, 3.5f, -7.f}, sun_apply_look[3] = {0.f, .8f, 0.f};
constexpr float sun_apply_azimuth_deg = 340.f;
struct Vec3 { double x, y, z; };
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 normalize(Vec3 a) { const double n = std::sqrt(dot(a, a)); return a * (1. / n); }
struct SunApplyState {
    x3m::renderer::SunShadowApplyPass pass;
    Com<IDirect3DTexture9> color, rt2, map, rt2_sys, map_sys, rt2_narrow; // rt2_narrow: a G32R32F RT2 of the lane before the flip, which the quads must skip
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
    api(f.d->CreateTexture(sun_apply_w, sun_apply_h, 1, 0, sun_apply_rt2_format(), D3DPOOL_DEFAULT, &s.rt2.p, nullptr), "CreateTexture RT2");
    api(f.d->CreateTexture(sun_apply_map, sun_apply_map, 1, 0, D3DFMT_R32F, D3DPOOL_DEFAULT, &s.map.p, nullptr), "CreateTexture map");
    api(f.d->CreateTexture(sun_apply_w, sun_apply_h, 1, 0, sun_apply_rt2_format(), D3DPOOL_SYSTEMMEM, &s.rt2_sys.p, nullptr), "CreateTexture RT2 sysmem");
    api(f.d->CreateTexture(sun_apply_w, sun_apply_h, 1, 0, D3DFMT_G32R32F, D3DPOOL_DEFAULT, &s.rt2_narrow.p, nullptr), "CreateTexture G32R32F RT2");
    api(f.d->CreateTexture(sun_apply_map, sun_apply_map, 1, 0, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &s.map_sys.p, nullptr), "CreateTexture map sysmem");
    api(f.d->CreateOffscreenPlainSurface(sun_apply_w, sun_apply_h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &s.readback.p, nullptr), "CreateOffscreenPlainSurface FP16");
    api(f.d->CreateQuery(D3DQUERYTYPE_EVENT, &s.event.p), "CreateQuery EVENT");
}
void sun_apply_release_targets(SunApplyState& s) {
    s.event.reset(); s.readback.reset(); s.map_sys.reset(); s.rt2_sys.reset(); s.map.reset(); s.rt2.reset(); s.rt2_narrow.reset(); s.color_surface.reset(); s.color.reset();
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
// The raster and the latch of both scripts. RT2 is synthesised as the D3D9
// rasterizer fills it: texel (i, j) holds the scene at NDC (2 i / W - 1,
// 1 - 2 j / H) of the JITTERED projection (raster_m20/m21 = the jitter in NDC,
// as jitter_rows applies it), so the quad's latch is that jitter plus the
// pixel-centre term of the quad's uv, exactly as motion_output.cpp latches it
// (renderer::quad_pixel_centre_m20/m21). X3M_FIXTURE_SUNAPPLY_LEGACY_LATCH=1
// (fixture only) drops the term: the law of builds before 2026-09-18, the
// witness that the runner's analytic edge fit (evaluated at the D3D9 pixel
// centre, independent of the latch) catches the half-pixel receiver error.
bool sun_apply_legacy_latch() {
    char text[4]{};
    return GetEnvironmentVariableA("X3M_FIXTURE_SUNAPPLY_LEGACY_LATCH", text, sizeof text) == 1 && text[0] == '1';
}
struct SunApplyLatch { float raster_m20, raster_m21, m20, m21; bool legacy; };
SunApplyLatch sun_apply_latch(float jx, float jy) {
    SunApplyLatch l{};
    l.raster_m20 = 2.f * jx / float(sun_apply_w); l.raster_m21 = -2.f * jy / float(sun_apply_h);
    l.legacy = sun_apply_legacy_latch();
    l.m20 = l.raster_m20 + (l.legacy ? 0.f : x3m::renderer::quad_pixel_centre_m20(sun_apply_w));
    l.m21 = l.raster_m21 + (l.legacy ? 0.f : x3m::renderer::quad_pixel_centre_m21(sun_apply_h));
    return l;
}
// The view-space direction of RT2 texel (i, j) under the raster's law.
Vec3 sun_apply_texel_direction(unsigned i, unsigned j, const SunApplyLatch& l, float m00, float m11) {
    const double ndc_x = 2. * i / sun_apply_w - 1., ndc_y = 1. - 2. * j / sun_apply_h;
    return {(ndc_x - l.raster_m20) / m00, (ndc_y - l.raster_m21) / m11, 1.};
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
} // namespace
