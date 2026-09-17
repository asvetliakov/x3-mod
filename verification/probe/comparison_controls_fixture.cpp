// Runs production input edges and HdrPass comparison handoff on the host.
#include <cstdio>
#include <cstring>
#include <memory>
#include "../../src/proxy/comparison_controls.h"
#define private public
#include "../../src/renderer/hdr_pass.h"
#undef private
using namespace x3m::renderer;
namespace x3m::renderer {
HdrPass::~HdrPass() {} // fake resources; no native device exists in this test
#include "comparison_exposure_under_test_inc.h"
}
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line=%u %s\n",__LINE__,#x);}}while(0)
int main(){
    x3m::ComparisonControls controls;
    x3m::ComparisonKeys keys{};
    keys.foreground=true; keys.control=keys.shift=true;
    CHECK(!controls.sample(keys).exposure);
    keys.exposure=true;CHECK(controls.sample(keys).exposure);
    for(unsigned i=0;i<10000;++i){const auto action=controls.sample(keys);CHECK(!action.exposure&&!action.bloom);}
    keys.control=false;CHECK(!controls.sample(keys).exposure);
    keys.control=true;CHECK(!controls.sample(keys).exposure); // modifier alone is not a key edge
    keys.foreground=false;CHECK(!controls.sample(keys).exposure);
    keys.foreground=true;CHECK(!controls.sample(keys).exposure); // held through alt-tab
    keys.exposure=false;CHECK(!controls.sample(keys).exposure);
    keys.exposure=true;CHECK(controls.sample(keys).exposure);
    keys.bloom=true;{const auto a=controls.sample(keys);CHECK(!a.exposure&&a.bloom);}
    keys.exposure=keys.bloom=false;controls.sample(keys);
    keys.exposure=keys.bloom=true;{const auto a=controls.sample(keys);CHECK(a.exposure&&a.bloom);}
    controls.reset_focus();{const auto a=controls.sample(keys);CHECK(!a.exposure&&!a.bloom);}
    // Startup held key is ignored until release, including F-key press before
    // the first observed foreground frame.
    x3m::ComparisonControls initial;CHECK(!initial.sample(keys).exposure);
    x3m::ComparisonControls chord;
    x3m::ComparisonKeys unarmed{};unarmed.foreground=true;chord.sample(unarmed);
    unarmed.control=unarmed.shift=unarmed.exposure=true;
    CHECK(!chord.sample(unarmed).exposure);CHECK(!chord.sample(unarmed).exposure);
    unarmed.exposure=false;chord.sample(unarmed);unarmed.exposure=true;CHECK(chord.sample(unarmed).exposure);

    // Emitter keys (F5 additive, F6 emission source gain): the same chord,
    // edge and focus rules, independent of each other and of F9/F10.
    x3m::ComparisonControls emitters;
    x3m::ComparisonKeys e{};e.foreground=true;e.control=e.shift=true;emitters.sample(e);
    e.screen_additive=true;{const auto a=emitters.sample(e);CHECK(a.screen_additive&&!a.source_gain&&!a.bloom);}
    for(unsigned i=0;i<1000;++i){const auto a=emitters.sample(e);CHECK(!a.screen_additive);} // held is not a press
    e.source_gain=true;{const auto a=emitters.sample(e);CHECK(!a.screen_additive&&a.source_gain);}
    e.shift=false;e.screen_additive=false;emitters.sample(e);e.shift=true;e.screen_additive=true;
    CHECK(!emitters.sample(e).screen_additive); // the chord must be armed in the previous sample
    CHECK(!emitters.sample(e).screen_additive); // and the key is now held
    e.screen_additive=false;emitters.sample(e);e.screen_additive=true;CHECK(emitters.sample(e).screen_additive);
    e.foreground=false;CHECK(!emitters.sample(e).screen_additive);
    e.foreground=true;CHECK(!emitters.sample(e).screen_additive); // held through alt-tab
    e.screen_additive=false;emitters.sample(e);e.screen_additive=true;CHECK(emitters.sample(e).screen_additive);
    emitters.reset_focus();{const auto a=emitters.sample(e);CHECK(!a.screen_additive&&!a.source_gain);}
    // F4 (hull emitters): its own edge, independent of F6; held is not a press; focus loss disarms it.
    x3m::ComparisonControls hull;
    x3m::ComparisonKeys k{};k.foreground=true;k.control=k.shift=true;hull.sample(k);
    k.hull_gain=true;{const auto a=hull.sample(k);CHECK(a.hull_gain&&!a.source_gain&&!a.screen_additive);}
    k.source_gain=true;{const auto a=hull.sample(k);CHECK(!a.hull_gain&&a.source_gain);}
    for(unsigned i=0;i<100;++i)CHECK(!hull.sample(k).hull_gain);
    k.foreground=false;hull.sample(k);k.foreground=true;CHECK(!hull.sample(k).hull_gain);
    k.hull_gain=false;hull.sample(k);k.hull_gain=true;CHECK(hull.sample(k).hull_gain);
    // F12 (the sun shadows at rest): the same chord, edge and focus rules,
    // independent of every other key. The caller polls it only with
    // --sun-shadow-apply, so a held key here is a genuine press.
    x3m::ComparisonControls shadow;
    x3m::ComparisonKeys s{};s.foreground=true;s.control=s.shift=true;shadow.sample(s);
    s.sun_shadow=true;{const auto a=shadow.sample(s);CHECK(a.sun_shadow&&!a.hull_gain&&!a.ambient_occlusion&&!a.exposure&&!a.bloom);}
    for(unsigned i=0;i<1000;++i)CHECK(!shadow.sample(s).sun_shadow); // held is not a second press
    s.sun_shadow=false;shadow.sample(s);s.sun_shadow=true;CHECK(shadow.sample(s).sun_shadow);
    s.foreground=false;CHECK(!shadow.sample(s).sun_shadow);
    s.foreground=true;CHECK(!shadow.sample(s).sun_shadow); // held through alt-tab
    s.sun_shadow=false;shadow.sample(s);s.sun_shadow=true;CHECK(shadow.sample(s).sun_shadow);
    s.shift=false;s.sun_shadow=false;shadow.sample(s);s.shift=true;s.sun_shadow=true;
    CHECK(!shadow.sample(s).sun_shadow); // the chord must be armed in the previous sample
    s.sun_shadow=false;shadow.sample(s);s.sun_shadow=true;CHECK(shadow.sample(s).sun_shadow);
    s.ambient_occlusion=true;{const auto a=shadow.sample(s);CHECK(a.ambient_occlusion&&!a.sun_shadow);} // its own edge, not the other key's
    shadow.reset_focus();{const auto a=shadow.sample(s);CHECK(!a.sun_shadow&&!a.ambient_occlusion);}
    x3m::ComparisonControls startup; // a key held before the first foreground sample is not a press
    x3m::ComparisonKeys held{};held.foreground=true;held.control=held.shift=held.sun_shadow=true;
    CHECK(!startup.sample(held).sun_shadow);CHECK(!startup.sample(held).sun_shadow);
    // F7 (the FPS overlay): Ctrl+Alt with Shift up, so the Ctrl+Shift+F7
    // telemetry marker chord never fires it; the same raw-key edge and focus
    // rules as the other keys, independent of the Ctrl+Shift arm.
    x3m::ComparisonControls overlay;
    x3m::ComparisonKeys o{};o.foreground=true;overlay.sample(o);
    o.control=o.alt=o.fps_overlay=true;{const auto a=overlay.sample(o);CHECK(a.fps_overlay&&!a.exposure&&!a.bloom&&!a.sun_shadow);}
    for(unsigned i=0;i<1000;++i)CHECK(!overlay.sample(o).fps_overlay); // held is not a second press
    o.fps_overlay=false;overlay.sample(o);o.fps_overlay=true;CHECK(overlay.sample(o).fps_overlay);
    o.fps_overlay=false;overlay.sample(o);o.shift=true;o.fps_overlay=true;CHECK(!overlay.sample(o).fps_overlay); // Ctrl+Shift+Alt+F7: Shift excludes it
    o.shift=false;CHECK(!overlay.sample(o).fps_overlay); // releasing Shift on a held F7 is not a press
    o.fps_overlay=false;overlay.sample(o);o.alt=false;o.fps_overlay=true;CHECK(!overlay.sample(o).fps_overlay); // Ctrl+F7 without Alt
    o.alt=true;CHECK(!overlay.sample(o).fps_overlay); // adding Alt to a held F7 is not a press
    o.fps_overlay=false;overlay.sample(o);o.control=false;o.fps_overlay=true;CHECK(!overlay.sample(o).fps_overlay); // Alt+F7 without Ctrl
    o.control=true;o.fps_overlay=false;overlay.sample(o);o.fps_overlay=true;CHECK(overlay.sample(o).fps_overlay);
    o.foreground=false;CHECK(!overlay.sample(o).fps_overlay);
    o.foreground=true;CHECK(!overlay.sample(o).fps_overlay); // held through alt-tab
    o.fps_overlay=false;overlay.sample(o);o.fps_overlay=true;CHECK(overlay.sample(o).fps_overlay);
    o.shift=true;o.exposure=true;o.fps_overlay=false;overlay.sample(o);o.exposure=false;overlay.sample(o);o.exposure=true;
    {const auto a=overlay.sample(o);CHECK(a.exposure&&!a.fps_overlay);} // the Ctrl+Shift arm is untouched by the Alt key
    overlay.reset_focus();o.shift=false;o.exposure=false;o.fps_overlay=true;CHECK(!overlay.sample(o).fps_overlay); // first sample after a focus reset only latches
    CHECK(!overlay.sample(o).fps_overlay); // and the key is now held
    o.fps_overlay=false;overlay.sample(o);o.fps_overlay=true;CHECK(overlay.sample(o).fps_overlay);
    // A launch with only --fps-overlay: the caller leaves every other key
    // false (their polls are gated on their own options), so the overlay chord
    // is the only action the sampler can ever produce, edge after edge.
    {x3m::ComparisonControls alone;x3m::ComparisonKeys only{};only.foreground=true;alone.sample(only);
     for(unsigned i=0;i<50;++i){
        only.control=only.alt=true;only.shift=(i%5==0);only.fps_overlay=true;const auto a=alone.sample(only);
        CHECK(a.fps_overlay==!only.shift&&!a.exposure&&!a.bloom&&!a.ambient_occlusion&&!a.screen_additive&&!a.source_gain&&!a.hull_gain&&!a.sun_shadow);
        only.fps_overlay=false;const auto b=alone.sample(only);
        CHECK(!b.fps_overlay&&!b.exposure&&!b.bloom&&!b.ambient_occlusion&&!b.screen_additive&&!b.source_gain&&!b.hull_gain&&!b.sun_shadow);
     }}
    x3m::ComparisonControls marker;x3m::ComparisonKeys m{};m.foreground=true;m.control=m.shift=m.fps_overlay=true;marker.sample(m);
    m.fps_overlay=false;marker.sample(m);m.fps_overlay=true;CHECK(!marker.sample(m).fps_overlay); // the marker chord alone, Alt up: never the overlay

    HdrPass hdr; IDirect3DPixelShader9 shader;
    hdr.config_.tonemap=HdrTonemap::Agx;hdr.config_.allow_auto_toggle=true;
    hdr.config_.exposure=ExposureMode::Manual;
    hdr.tonemap_shader_=&shader;hdr.caps_.tonemap=hdr.caps_.meter=true;
    const auto params=hdr.config_.params;
    for(unsigned i=0;i<8;++i){
        hdr.exposure_.ev_adapted_=1.75f;hdr.exposure_.steps_=12;
        hdr.chain_pending_[0]=hdr.chain_pending_[1]=true;hdr.chain_slot_=1;hdr.latch_ticks_=9999;
        const auto mode=i%2?ExposureMode::Auto:ExposureMode::Manual;
        CHECK(hdr.comparison_exposure(mode));CHECK(hdr.exposure_mode()==mode);
        CHECK(hdr.exposure().ev()==0.f);CHECK(hdr.exposure().exposure()==1.f);
        CHECK(hdr.exposure().steps()==0);CHECK(!hdr.chain_pending_[0]&&!hdr.chain_pending_[1]);
        CHECK(hdr.chain_slot_==0&&hdr.latch_ticks_==0);
        CHECK(std::memcmp(&params,&hdr.config_.params,sizeof params)==0);
        CHECK(hdr.meter_active()==(mode==ExposureMode::Auto));
        CHECK(hdr.tonemap_active());
    }
    // Existing custom bounds remain authoritative; do not mislabel clamped
    // fixed exposure as neutral multiplier one.
    hdr.config_.params.ev_min=.5f;hdr.config_.params.ev_max=1.f;
    CHECK(hdr.comparison_exposure(ExposureMode::Manual));CHECK(hdr.exposure().ev()==.5f);
    hdr.caps_.meter=false;hdr.chain_pending_[0]=true;hdr.latch_ticks_=42;
    CHECK(!hdr.comparison_exposure(ExposureMode::Auto));CHECK(hdr.exposure_mode()==ExposureMode::Manual);
    CHECK(hdr.chain_pending_[0]&&hdr.latch_ticks_==42);CHECK(hdr.exposure().ev()==.5f);
    hdr.caps_.meter=true;hdr.tonemap_failures_=HdrPass::tonemap_failure_limit;
    CHECK(!hdr.comparison_exposure(ExposureMode::Auto));CHECK(hdr.exposure_mode()==ExposureMode::Manual);
    // Component fixtures retain their historical default; production selects
    // fixed and preprovisions optional AUTO explicitly at capture initialization.
    HdrConfig config{};CHECK(config.exposure==ExposureMode::Auto&&!config.allow_auto_toggle);
    CHECK(config.meter_requested());config.exposure=ExposureMode::Manual;CHECK(!config.meter_requested());
    config.allow_auto_toggle=true;CHECK(config.meter_requested());
    std::printf("comparison_controls checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
