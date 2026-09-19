int main() {
 using namespace x3m;
 { MotionOutput m;m.volumetric_fog_begin_frame();assert(m.draw()==0&&m.device_storage.calls==0);
   m.complete(true,"ok");m.next();const auto before=m.invalidations;
   assert(m.draw()==0&&m.device_storage.calls==2&&m.device_storage.draws==2&&m.device_storage.mask==7);
   assert(m.fog_cards_.suppressed==1&&m.fog_cards_.observed==1&&m.fog_latch_.cards_recent(m.frame_));
   assert(m.invalidations==before+1);m.draw();assert(m.invalidations==before+1);
   m.complete(true,"ok");m.next();m.draw();assert(m.invalidations==before+1);m.complete(true,"ok");
   m.volumetric_fog_toggle();m.next();m.draw();assert(m.device_storage.calls==6);
   m.volumetric_fog_toggle();m.next();m.draw();assert(m.device_storage.calls==6&&m.fog_cards_.warmup);
 }
 // Feed the actual scene-end success predicate distinct refusal, write and
 // restoration outcomes. Renderer stage injection belongs to FogPass's suite.
 for(unsigned failure=0;failure<6;++failure) {
   MotionOutput m;m.warm();m.draw();renderer::FogResult out{true,S_OK};
   const char* skip=nullptr;HRESULT result=S_OK;
   switch(failure){case 0:skip="late_query";break;case 1:out.applied=false;break;case 2:result=S_FALSE;break;
     case 3:result=-99;break;case 4:out.restore=-99;break;case 5:out.restore=S_FALSE;break;}
   m.complete_volumetric_fog(skip,result,out);
   assert(m.fog_cards_.fault&&m.fog_card_mode_==3);m.next();m.draw();assert(m.device_storage.calls==2);
   m.volumetric_fog_toggle();m.next();m.volumetric_fog_toggle();m.next();m.draw();
   assert(m.fog_cards_.fault&&!m.fog_cards_.medium_allowed()&&m.device_storage.calls==2);
 }
 for(bool mutate : {false,true}) {
   MotionOutput m;m.warm();m.device_storage.fail_at=1;m.device_storage.mutate=mutate;
   assert(m.draw()==0&&m.device_storage.mask==7&&m.device_storage.calls==2&&m.device_storage.draws==1);
   assert(m.fog_cards_.refused&&!m.fog_cards_.medium_allowed()&&!m.motion_state_lost_);
 }
 { MotionOutput m;m.warm();m.device_storage.fail_at=1;m.device_storage.fail_restore=true;m.device_storage.mutate=true;
   assert(m.draw()==-99&&m.device_storage.draws==0&&m.motion_state_lost_&&m.fog_cards_.fault); }
 { MotionOutput m;m.warm();m.device_storage.fail_at=2;
   assert(m.draw(-7)==-7&&m.device_storage.draws==1&&m.motion_state_lost_&&m.fog_cards_.fault&&m.state_invalidations==1); }
 { MotionOutput m;m.warm();assert(m.draw(-7)==-7&&m.device_storage.draws==1&&m.device_storage.mask==7&&m.fog_cards_.fault); }
 // All mutable guards: no Set*, native result/count intact, replacement
 // medium refused outside the explicit warm-up, source observation retained.
 for(unsigned gate=0;gate<24;++gate) {
   MotionOutput m;m.warm();MotionDrawCall c;
   switch(gate) {
    case 0:m.active_queries_=1;break;case 1:m.shadow_.recording=true;break;
    case 2:m.bound=false;break;case 3:m.states[4]=-1;break;case 4:m.blends[1]=-1;break;
    case 5:m.state_hooks_=false;break;case 6:m.shadow_.stream0_frequency=2;break;
    case 7:m.shadow_.declaration=0;break;case 8:c.user_memory=true;break;case 9:c.indexed=false;break;
    case 10:m.main_msaa_=true;break;case 11:m.taa_enabled_=false;break;case 12:m.counters_.taa.attempted=true;break;
    case 13:m.jitter_active_=false;break;case 14:m.counters_.filled=false;break;case 15:m.depth_surface_=nullptr;break;
    case 16:m.fog_storage.ready=false;break;case 17:m.prerequisites_ready=false;break;case 18:m.parameters_ready=false;break;
    case 19:m.hdr_state_=HdrState::Off;break;case 20:m.composition_busy_=true;break;
    case 21:m.fog_frame_=m.frame_;break;case 22:m.shadow_.stream0_frequency_known=false;break;case 23:m.shadow_.fog_card_pair=false;break;
   }
   assert(m.draw(-7,c)==-7&&m.device_storage.draws==1&&m.device_storage.calls==0);
   assert(m.fog_cards_.observed==1&&m.fog_cards_.refused&&!m.fog_cards_.medium_allowed());
 }
 { MotionOutput m;m.warm();m.fog_strength_=0;m.next();m.draw();assert(m.device_storage.calls==0&&!m.fog_cards_.medium_allowed()); }
 { MotionOutput m;m.warm();m.draw();m.active_queries_=1;m.draw();
   m.complete(false,"late_query");assert(m.fog_cards_.fault&&m.device_storage.calls==2); }
 for(int error : {D3DERR_DEVICELOST,D3DERR_DEVICENOTRESET,-99}) {
   MotionOutput m;m.fog_storage.ready=false;m.fog_storage.result=error;
   m.prepare_volumetric_fog_targets(64,48);
   assert(!m.fog_disabled_&&m.fog_cards_.fault==(error==-99)&&m.fog_storage.prepares==1);
   m.fog_cards_={};m.fog_storage.result=0;m.prepare_volumetric_fog_targets(64,48);
   assert(m.fog_storage.ready&&!m.fog_cards_.fault);m.prepare_volumetric_fog_targets(64,48);assert(m.fog_storage.prepares==2);
 }
 std::puts("actual MotionOutput card methods PASS");
}
