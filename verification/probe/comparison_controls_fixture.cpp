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

    // Emitter keys (F5 additive, F6 engine gain, F4 effect gain): the same
    // chord, edge and focus rules, independent of each other and of F9/F10.
    x3m::ComparisonControls emitters;
    x3m::ComparisonKeys e{};e.foreground=true;e.control=e.shift=true;emitters.sample(e);
    e.screen_additive=true;{const auto a=emitters.sample(e);CHECK(a.screen_additive&&!a.engine_gain&&!a.effect_gain&&!a.bloom);}
    for(unsigned i=0;i<1000;++i){const auto a=emitters.sample(e);CHECK(!a.screen_additive);} // held is not a press
    e.engine_gain=e.effect_gain=true;{const auto a=emitters.sample(e);CHECK(!a.screen_additive&&a.engine_gain&&a.effect_gain);}
    e.shift=false;e.screen_additive=false;emitters.sample(e);e.shift=true;e.screen_additive=true;
    CHECK(!emitters.sample(e).screen_additive); // the chord must be armed in the previous sample
    CHECK(!emitters.sample(e).screen_additive); // and the key is now held
    e.screen_additive=false;emitters.sample(e);e.screen_additive=true;CHECK(emitters.sample(e).screen_additive);
    e.foreground=false;CHECK(!emitters.sample(e).screen_additive);
    e.foreground=true;CHECK(!emitters.sample(e).screen_additive); // held through alt-tab
    e.screen_additive=false;emitters.sample(e);e.screen_additive=true;CHECK(emitters.sample(e).screen_additive);
    emitters.reset_focus();{const auto a=emitters.sample(e);CHECK(!a.screen_additive&&!a.engine_gain&&!a.effect_gain);}

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
