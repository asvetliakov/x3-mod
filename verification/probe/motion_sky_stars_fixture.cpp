// Route side of the sky stars (src/proxy/motion_output_sky_stars_inc.h, compiled unchanged with the real
// motion_output.h): the match, the mask, the batch, the refusals and the quarantine, with a scripted SetRenderState, a
// scripted pass (the real renderer::SkyStarsPass is checked natively by sky_stars_fixture.cpp) and the call order of
// before_draw/after_draw replayed by draw() below (test_motion_sky_stars.py checks that order in motion_output.cpp).
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "../../src/renderer/exposure.h"
#include "../../src/temporal/agx.h"
#include "../../src/temporal/sharpen.h"
#define private public
#include "../../src/proxy/motion_output.h"
#undef private

using namespace x3m;
namespace x3m::renderer {
class TemporalPass {};
}
namespace x3m::telemetry {
enum class Metric { HdrMeter, HdrWriteback, HdrWritebackDraw, HdrWritebackStretch, HdrBind };
}
namespace {
unsigned checks = 0, failures = 0, scenarios = 0;
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", name);
    }
}
struct World {
    std::vector<std::string> calls, rows;
    bool attach_ok = true;
    HRESULT draw_hr = S_OK, restore_hr = S_OK, mask_hr = S_OK;
    DWORD colour_mask = 7;
    unsigned attaches = 0;
    float twinkle[4] = {-1.f, -1.f, -1.f, -1.f}; // c251 of the last batch
    sky_stars::core::Look look{};                // the look of the last prepare
}* w = nullptr;
HRESULT set_rs(IDirect3DDevice9*, D3DRENDERSTATETYPE state, DWORD value) {
    w->calls.push_back("rs" + std::to_string(unsigned(state)) + "=" + std::to_string(value));
    return unsigned(state) == 168 && value == 0 ? w->mask_hr : S_OK;
}
HRESULT get_mode(IDirect3DDevice9*, UINT, D3DDISPLAYMODE* mode) {
    mode->Format = D3DFMT_A8R8G8B8;
    return S_OK;
}
} // namespace

namespace x3m {
namespace {
enum : unsigned { GetDisplayMode = 8, SetRenderState = 57 };
using SetRenderStateFn = decltype(&set_rs);
using GetDisplayModeFn = decltype(&get_mode);
template <class F> void call_preserved(F&& f) { f(); }
void sky_stars_texture_desc(IDirect3DBaseTexture9*, unsigned* a, unsigned* b, unsigned* c, unsigned* d) noexcept {
    *a = *b = *c = *d = 0;
}
} // namespace
void log(const char* format, ...) {
    const char* space = std::strchr(format, ' ');
    w->rows.push_back(space ? std::string(format, space) : std::string(format));
}
std::uint64_t resource_id(IDirect3DBaseTexture9*) { return 1; }
MotionOutput::MotionOutput() noexcept = default;
MotionOutput::~MotionOutput() = default;
HRESULT MotionOutput::render_state(D3DRENDERSTATETYPE state, DWORD* value) noexcept {
    *value = unsigned(state) == 168 ? w->colour_mask : 0;
    return S_OK;
}
void MotionOutput::invalidate_render_states() noexcept { w->calls.push_back("invalidate_states"); }
void MotionOutput::invalidate_taa(TaaInvalidateSite) noexcept { w->calls.push_back("invalidate_taa"); }
std::uint64_t MotionOutput::engine_qpc_frequency() noexcept { return 1; }
template <typename Fn> void MotionOutput::taa_call(Fn&& fn, const char*) noexcept { fn(); }
renderer::HdrPass::~HdrPass() = default;
renderer::LinearEmissionPass::~LinearEmissionPass() = default;
renderer::ShadowReplayPass::~ShadowReplayPass() = default;
renderer::SunShadowApplyPass::~SunShadowApplyPass() = default;
renderer::FogPass::~FogPass() = default;
renderer::EnginePlumesPass::~EnginePlumesPass() = default;
renderer::OcclusionCullPass::~OcclusionCullPass() = default;
renderer::EngineRibbonsPass::~EngineRibbonsPass() = default;
renderer::EngineShimmerPass::~EngineShimmerPass() = default;
renderer::OwnSunPass::~OwnSunPass() = default;
// The scripted pass: attach as World says, one "batch" call per draw with World's draw and restore results.
renderer::SkyStarsPass::~SkyStarsPass() {}
HRESULT renderer::SkyStarsPass::attach(IDirect3DDevice9* d, void* const*, const D3DCAPS9&, D3DFORMAT) noexcept {
    ++w->attaches;
    device_ = d;
    caps_.enabled = w->attach_ok;
    caps_.reason = w->attach_ok ? "ok" : "fixture";
    return w->attach_ok ? S_OK : D3DERR_NOTAVAILABLE;
}
void renderer::SkyStarsPass::detach() noexcept { caps_ = {}; }
unsigned renderer::SkyStarsPass::references() const noexcept { return 0; }
unsigned renderer::SkyStarsPass::prepare(std::uint32_t seed, const sky_stars::core::Look& look, bool* rebuilt) noexcept {
    if (rebuilt) *rebuilt = seed != built_seed_;
    built_seed_ = seed;
    w->look = look;
    built_stars_ = sky_stars::core::star_count(sky_stars::core::seed_k(seed), look.density);
    return built_stars_;
}
HRESULT renderer::SkyStarsPass::draw(const float*, const float* twinkle, SkyStarsReport* report) noexcept {
    w->calls.push_back("batch");
    std::memcpy(w->twinkle, twinkle, sizeof w->twinkle);
    report->operation = w->draw_hr;
    report->drew = SUCCEEDED(w->draw_hr);
    report->restore = w->restore_hr;
    return w->draw_hr;
}
#include "../../src/proxy/motion_output_sky_stars_inc.h"
} // namespace x3m

namespace {
constexpr std::uint32_t seed = 0xa7u | 1u << 8 | 65u << 16 | sky_stars::core::seed_valid;
IDirect3DDevice9 device;
IDirect3DSurface9 target;
void* table[120]{};
void setup(MotionOutput& m) {
    table[8] = reinterpret_cast<void*>(&get_mode);
    table[57] = reinterpret_cast<void*>(&set_rs);
    m.device_ = &device;
    m.native_ = table;
    m.enabled_ = true;
    m.configure_sky_stars(true, sky_stars::core::Look{}, .3f, true);
    m.hdr_ = std::make_unique<renderer::HdrPass>();
    m.hdr_->target_ = &target;
    m.hdr_state_ = MotionOutput::HdrState::Active;
    m.camera_background_.valid = true;
    m.camera_background_.r[0] = m.camera_background_.r[4] = m.camera_background_.r[8] = 1.f;
    m.camera_background_.m00 = m.camera_background_.m11 = 1.f;
    m.pending_.only_rt0 = true;
    m.pending_.rt.width = m.pending_.viewport.width = 64;
    m.pending_.rt.height = m.pending_.viewport.height = 32;
    m.pending_.viewport.known = true;
}
// A frame's latching Clear, then the phase the draws run in.
void frame(MotionOutput& m, renderer::BoundaryState state) {
    m.sky_stars_latch();
    m.selector_.state_ = state;
}
// before_draw's sky-star steps in production order (sky_stars_draw_begin first, the classification after
// evaluate_draw, the mask in the submit branch after the fog card), the native draw, then after_draw's finish (before
// the card's finish). Returns the calls this draw made.
std::vector<std::string> draw(MotionOutput& m, bool bind, std::uint64_t vs = 1, std::uint64_t ps = 2) {
    w->calls.clear();
    if (bind) m.sky_stars_bind(seed);
    m.shadow_.vs_hash = vs;
    m.shadow_.ps_hash = ps;
    m.sky_stars_draw_begin();
    MotionRoute route{};
    MotionDrawCall call{};
    call.primitives = 2;
    m.sky_stars_classify(call, route);
    if (route.submit && route.sky_stars && !route.fog_card_mask.masked) m.prepare_sky_stars(route);
    w->calls.push_back("native");
    if (route.sky_stars) m.finish_sky_stars(route, S_OK);
    check(!route.fog_card_mask.masked && !route.sky_stars_masked, "mask never left set after a draw");
    return w->calls;
}
using Calls = std::vector<std::string>;
const Calls masked_batch{"rs168=0", "native", "rs168=7", "batch"}, masked_only{"rs168=0", "native", "rs168=7"},
    native_only{"native"};
} // namespace

int main() {
    using renderer::BoundaryState;
    {   // A: a background draw with the star texture: mask, native, mask lifted, one batch; a second call of the same
        // pair is masked without a batch; an untextured draw keeps nothing; the frame row says ready.
        ++scenarios;
        World world;
        w = &world;
        MotionOutput m;
        setup(m);
        frame(m, BoundaryState::Background);
        check(draw(m, true) == masked_batch, "A first match: mask, native, unmask, batch");
        check(draw(m, true) == masked_only, "A same pair again: masked, no second batch");
        check(draw(m, false, 9, 9) == native_only, "A untextured draw after it: untouched");
        m.sky_stars_phase_end();
        check(!std::strcmp(m.sky_stars_logged_status_, "ready") && m.sky_stars_frame_matched_ == 2 &&
                  m.sky_stars_frame_masked_ == 2 && world.attaches == 1,
              "A row: ready, matched 2, masked 2, one attach");
        // c251 of the batch: the wall clock (the stub's QPC ticks at frequency 1) modulo 128 s and the amplitude.
        check(world.twinkle[0] >= 0.f && world.twinkle[0] < 128.f && world.twinkle[0] == float(int(world.twinkle[0])) &&
                  world.twinkle[1] == .3f && world.twinkle[2] == 0.f && world.twinkle[3] == 0.f,
              "A c251 = (wall seconds mod 128, amplitude, 0, 0)");
        check(sky_stars::core::same_look(world.look, sky_stars::core::Look{}), "A the configured look reaches prepare");
    }
    {   // J: refused settings fall back to the defaults (the pass still runs); accepted ones reach prepare and c251.
        ++scenarios;
        World world;
        w = &world;
        MotionOutput m;
        setup(m);
        m.configure_sky_stars(true, sky_stars::core::Look{9.f, .1f, -1.f, 4.f, 2.5f}, 1.5f, true);
        frame(m, BoundaryState::Background);
        check(draw(m, true) == masked_batch, "J refused settings: the pass still runs");
        check(sky_stars::core::same_look(world.look, sky_stars::core::Look{}) && world.twinkle[1] == .3f,
              "J refused settings: defaults");
        MotionOutput n;
        setup(n);
        n.configure_sky_stars(true, sky_stars::core::Look{1.f, 4.f, 0.f, 1.f, 0.f}, 0.f, true);
        frame(n, BoundaryState::Background);
        draw(n, true);
        check(sky_stars::core::same_look(world.look, sky_stars::core::Look{1.f, 4.f, 0.f, 1.f, 0.f}) &&
                  world.twinkle[1] == 0.f,
              "J accepted settings reach prepare and c251");
    }
    {   // B (F2): a bind bit taken by a draw that returns early (blocked, state lost, engine draw, UP) cannot make a
        // later background draw of another pair count as freshly bound.
        ++scenarios;
        World world;
        w = &world;
        MotionOutput m;
        setup(m);
        frame(m, BoundaryState::Background);
        m.sky_stars_bind(seed);
        m.sky_stars_draw_begin(); // an early-returning draw: the bit goes with it
        check(draw(m, false, 5, 6) == native_only, "B stale bind after an early return: not masked");
        check(m.sky_stars_frame_matched_ == 0 && !m.sky_stars_fresh_ && !m.sky_stars_draw_fresh_, "B no match, no bit");
        frame(m, BoundaryState::Scene);
        check(draw(m, true) == native_only && m.sky_stars_outside_ == 1, "B bind in the scene phase: outside");
        frame(m, BoundaryState::Background);
        check(draw(m, false, 5, 6) == native_only, "B the scene bind's bit does not reach the next background draw");
    }
    {   // C: outside the phase or the latch: counted, never masked.
        ++scenarios;
        World world;
        w = &world;
        MotionOutput m;
        setup(m);
        frame(m, BoundaryState::Scene);
        check(draw(m, true) == native_only, "C scene phase: not masked");
        frame(m, BoundaryState::Background);
        m.hdr_state_ = MotionOutput::HdrState::Off;
        check(draw(m, true) == native_only, "C no HDR latch: not masked");
        check(m.sky_stars_outside_ == 2 && world.attaches == 0, "C two outside, nothing attached");
    }
    {   // D: a second render target bound in the background phase: refused (targets), never masked.
        ++scenarios;
        World world;
        w = &world;
        MotionOutput m;
        setup(m);
        frame(m, BoundaryState::Background);
        m.pending_.only_rt0 = false;
        check(draw(m, true) == native_only, "D second RT: not masked");
        check(m.sky_stars_refused_ == 1 && !std::strcmp(m.sky_stars_frame_reason_, "targets"), "D refused: targets");
    }
    {   // E: a refused pass leaves the native draw unmasked and is not retried before Reset.
        ++scenarios;
        World world;
        world.attach_ok = false;
        w = &world;
        MotionOutput m;
        setup(m);
        frame(m, BoundaryState::Background);
        check(draw(m, true) == native_only, "E attach refused: not masked");
        frame(m, BoundaryState::Background);
        check(draw(m, true) == native_only && world.attaches == 1, "E no retry before Reset");
        bool device_row = false;
        for (const auto& r : world.rows) device_row |= r == "sky_stars_device";
        check(device_row && !std::strcmp(m.sky_stars_frame_reason_, "attach"), "E one device row, reason attach");
    }
    {   // F: a failed restore quarantines the route and stops the pass until Reset; after Reset it draws again.
        ++scenarios;
        World world;
        world.restore_hr = E_FAIL;
        w = &world;
        MotionOutput m;
        setup(m);
        frame(m, BoundaryState::Background);
        const Calls quarantined{"rs168=0", "native", "rs168=7", "batch", "invalidate_states", "invalidate_taa"};
        check(draw(m, true) == quarantined, "F batch, then quarantine");
        check(m.motion_state_lost_ && m.sky_stars_attach_failed_, "F state lost, pass off");
        m.motion_state_lost_ = false; // the route's own recovery (recover_motion_state) stands in here
        frame(m, BoundaryState::Background);
        check(draw(m, true) == native_only, "F off until Reset: not masked");
        world.restore_hr = S_OK;
        m.sky_stars_before_reset();
        frame(m, BoundaryState::Background);
        check(draw(m, true) == masked_batch, "F after Reset: masked and drawn again");
    }
    {   // G: the mask's SetRenderState fails: put back at once, no batch, the draw goes out unmasked.
        ++scenarios;
        World world;
        world.mask_hr = E_FAIL;
        w = &world;
        MotionOutput m;
        setup(m);
        frame(m, BoundaryState::Background);
        check(draw(m, true) == Calls({"rs168=0", "rs168=7", "native"}), "G mask failure: restored, unmasked, no batch");
    }
    {   // H: three failed batches in a row stop the matching (the painted stars come back).
        ++scenarios;
        World world;
        world.draw_hr = E_FAIL;
        w = &world;
        MotionOutput m;
        setup(m);
        for (unsigned i = 0; i < 3; ++i) {
            frame(m, BoundaryState::Background);
            check(draw(m, true) == masked_batch, "H failed batch frames stay masked");
        }
        frame(m, BoundaryState::Background);
        check(draw(m, true) == native_only && m.sky_stars_attach_failed_, "H fourth frame: not masked");
    }
    {   // I (N2): the table holds 512 marks (AP+Mayhem has 322 star textures); refusals reach the row.
        ++scenarios;
        World world;
        w = &world;
        MotionOutput m;
        setup(m);
        static IDirect3DBaseTexture9 textures[600];
        unsigned stored = 0;
        for (auto& t : textures) stored += m.sky_stars_mark_insert(&t, 1, seed);
        check(stored == 512 && m.sky_stars_marks_->refused_full == 88, "I 512 stored, 88 refused");
        check(m.sky_stars_lookup(&textures[3]) == seed, "I lookup returns the packed seed");
        frame(m, BoundaryState::Background);
        draw(m, true);
        m.sky_stars_phase_end();
        check(m.sky_stars_logged_full_ == 88, "I table_full=88 in the row");
    }
    std::printf("motion_sky_stars scenarios=%u checks=%u failures=%u\n", scenarios, checks, failures);
    return failures ? 1 : 0;
}
