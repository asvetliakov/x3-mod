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
 for(unsigned failure=0;failure<8;++failure) {
   MotionOutput m;m.warm();m.draw();renderer::FogResult out{true,S_OK};
   const char* skip=nullptr;HRESULT result=S_OK;
   switch(failure){case 0:skip="late_query";break;case 1:out.applied=false;break;case 2:result=S_FALSE;break;
     case 3:result=-99;break;case 4:out.restore=-99;break;case 5:out.restore=S_FALSE;break;case 6:out.caller_state_restored=false;break;case 7:out.route_poisoned=true;break;}
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
 for(unsigned gate=0;gate<28;++gate) {
   MotionOutput m;m.warm();MotionDrawCall c;
   switch(gate) {
    case 0:m.active_queries_=1;break;case 1:m.shadow_.recording=true;break;
    case 2:m.bound=false;break;case 3:m.device_storage.states[4]=-1;break;case 4:m.device_storage.blends[1]=-1;break;
    case 5:m.device_storage.failed_state=0;break;case 6:m.device_storage.frequency=2;break;
    case 7:m.shadow_.declaration=0;break;case 8:c.user_memory=true;break;case 9:c.indexed=false;break;
    case 10:m.main_msaa_=true;break;case 11:m.taa_enabled_=false;break;case 12:m.counters_.taa.attempted=true;break;
    case 13:m.jitter_active_=false;break;case 14:m.counters_.filled=false;break;case 15:m.depth_surface_=nullptr;break;
    case 16:m.fog_storage.ready=false;break;case 17:m.prerequisites_ready=false;break;case 18:m.parameters_ready=false;break;
    case 19:m.hdr_state_=HdrState::Off;break;case 20:m.composition_busy_=true;break;
    case 21:m.fog_frame_=m.frame_;break;case 22:m.device_storage.fail_frequency=true;break;case 23:m.shadow_.fog_card_pair=false;break;case 24:m.scene_open_=false;break;case 25:m.depth_format_=0;break;case 26:m.sun_lane_failed_=true;break;case 27:m.sun_frame_.failed=true;break;
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
 // Current native state is sampled independently on every eligible card.
 for(bool hooked:{false,true}) {
   MotionOutput m;m.state_hooks_=hooked;m.warm();m.draw();
   assert(m.device_storage.freq_gets==1&&m.device_storage.gets==(hooked?0:12)&&m.device_storage.calls==2);
 }
 // Hooked and unhooked admission agree on every mismatched required value.
 for(bool hooked:{false,true})for(unsigned state=0;state<12;++state){
   MotionOutput m;m.state_hooks_=hooked;m.warm();
   if(state<8){m.device_storage.states[state]^=1;for(unsigned i=0;i<32;++i)if(shadow_states[i]==state)m.shadow_.states[i]^=1;}
   else {m.device_storage.blends[state-8]^=1;m.shadow_.composition_blend[state-8]^=1;}
   m.draw();assert(m.fog_cards_.refused&&m.device_storage.calls==0&&m.device_storage.gets==(hooked?0:12));
 }
 for(int state=0;state<12;++state){
   MotionOutput m;m.warm();m.device_storage.failed_state=state;m.draw();
   assert(m.device_storage.calls==0&&m.fog_cards_.refused&&m.device_storage.gets==12);
 }
 for(unsigned gate=0;gate<9;++gate){
   MotionOutput m;m.warm();MotionDrawCall c;
   switch(gate){case 0:m.shadow_.fog_card_pair=false;break;case 1:m.shadow_.declaration=0;break;
    case 2:c.topology=3;break;case 3:c.primitives=1;break;case 4:c.vertex_count=3;break;
    case 5:m.active_queries_=1;break;case 6:m.shadow_.recording=true;break;
    case 7:m.volumetric_fog_toggle();m.next();break;case 8:m.fog_cards_.warmup=true;break;}
   m.draw(0,c);assert(m.device_storage.gets==0&&m.device_storage.freq_gets==0&&m.device_storage.calls==0);
 }
 { MotionOutput m;m.warm();m.draw();assert(m.device_storage.gets==12&&m.device_storage.calls==2);
   m.device_storage.states[4]=0;m.draw();assert(m.device_storage.gets==24&&m.device_storage.calls==2&&m.fog_cards_.refused);
   // The next candidate samples changed state after a stateblock-equivalent native update.
   m.device_storage.states[4]=7;m.fog_cards_.refused=false;m.draw();assert(m.device_storage.gets==36&&m.device_storage.calls==4);
 }
 { MotionOutput m;m.warm();m.draw();m.device_storage.frequency=0x40000002u;m.draw();
   assert(m.device_storage.freq_gets==2&&m.device_storage.calls==2&&m.device_storage.gets==12);
   m.device_storage.frequency=1;m.fog_cards_.refused=false;m.draw();assert(m.device_storage.freq_gets==3&&m.device_storage.calls==4);
 }
 for(unsigned cards:{6u,8u}){MotionOutput m;m.warm();for(unsigned i=0;i<cards;++i)m.draw();
   assert(m.device_storage.gets==12*cards&&m.device_storage.freq_gets==cards&&m.device_storage.calls==2*cards);
   std::printf("card_native_calls cards=%u rs_get=%u freq_get=%u mask_set=%u total=%u\n",cards,m.device_storage.gets,m.device_storage.freq_gets,m.device_storage.calls,m.device_storage.gets+m.device_storage.freq_gets+m.device_storage.calls);
 }
 // Exact production reconciliation: untouched ordinary refusal is safe;
 // missing scene knowledge, failed reopen and restore poison both modes.
 for(bool replace:{false,true})for(unsigned failure=0;failure<6;++failure){
   MotionOutput m;m.fog_cards_replace_=replace;renderer::FogFrame in;renderer::FogResult out;
   switch(failure){case 1:out.scene_known=false;break;case 2:out.scene_open=false;break;
     case 3:out.restore=-99;break;case 4:out.caller_state_restored=false;break;case 5:out.route_poisoned=true;break;}
   m.reconcile_volumetric_fog(in,out,-99);assert(m.motion_state_lost_==(failure!=0));
   assert(m.scene_open_==(failure!=2));
 }
 // A same-family new sector must warm up again. Stale samples and a second
 // same-frame sample cannot replace the first BeginScene authority.
 { MotionOutput m;m.warm();++m.frame_;m.volumetric_fog_begin_frame();m.draw();assert(!m.fog_cards_.suppressed);
   m.sample("bluewell",0x2000);assert(m.fog_cards_.warmup&&!m.fog_cards_.armed);m.draw();assert(!m.fog_cards_.suppressed);
   m.sample("foggreenoutlands");assert(m.fog_sector_.profile==1);m.complete(true,"ok");
   ++m.frame_;m.volumetric_fog_begin_frame();m.sample("foggreenoutlands");assert(m.fog_cards_.warmup&&m.fog_sector_.profile==2);
 }
 { MotionOutput m;m.warm();++m.frame_;m.volumetric_fog_begin_frame();m.sample("unsupported");
   m.draw();assert(!m.fog_cards_.active&&!m.fog_cards_.suppressed&&!m.fog_cards_.medium_allowed());
 }
 { MotionOutput m;m.warm();m.fog_sector_.field_generation=0;m.draw();assert(m.fog_cards_.refused&&!m.fog_cards_.suppressed); }
 { MotionOutput m;m.warm();const auto invalidations=m.invalidations;m.volumetric_fog_step();m.volumetric_fog_step();
   assert(m.invalidations==invalidations+1); }
 // Every mapped family requires its own authority warmup before suppression.
 for(const auto& family:renderer::fog_field::family_profiles) {
   MotionOutput m;m.warm();++m.frame_;m.volumetric_fog_begin_frame();m.sample(family.family,0x4000);m.prepare_volumetric_fog_targets(1280,768);
   assert(m.fog_sector_.profile==unsigned(family.profile)&&m.fog_cards_.warmup&&!m.fog_cards_.armed);
   m.draw();assert(!m.fog_cards_.suppressed);m.complete(true,"ok");
   ++m.frame_;m.volumetric_fog_begin_frame();m.sample(family.family,0x4000);m.draw();
   assert(m.fog_cards_.suppressed==1&&m.fog_cards_.armed);m.complete(true,"ok");
 }

 // Run53B-shaped timeline through the actual scene-end method. Device/pass
 // dependencies are host doubles; this witnesses routing, not rendered pixels.
 { MotionOutput m;m.frame_=31480;m.sample("foggreenoutlands",0x66328b20);
   m.prepare_volumetric_fog_targets(64,48);m.volumetric_fog_begin_frame();m.run_volumetric_fog();
   assert(m.fog_cards_.armed);
   unsigned warmups=0,suppressed=0,applied=0,cuts=0;
   for(unsigned frame=31481;frame<=31512;++frame){
     m.frame_=frame;m.volumetric_fog_begin_frame();m.sample("foggreenoutlands",0x66328b20);
     m.cut_finished_=true;m.counters_.cut=frame>=31495&&frame<=31505;
     cuts+=m.counters_.cut;warmups+=m.fog_cards_.warmup;
     for(unsigned card=0;card<6;++card)assert(m.draw()==S_OK);
     suppressed+=m.fog_cards_.suppressed;
     const auto before=m.fog_storage.executes;m.run_volumetric_fog();
     applied+=m.fog_storage.executes==before+1&&m.fog_cards_.finished&&!m.fog_cards_.fault;
     // Both scene-end callers can qualify; the second must remain a no-op.
     m.run_volumetric_fog();assert(m.fog_storage.executes==before+1);
     assert(m.device_storage.target.refs==1&&m.depth_storage.texture.refs==1&&m.device_storage.mask==7);
   }
   std::fprintf(stderr,"cut_sequence frames=32 cuts=%u warmup=%u suppressed=%u applied=%u\n",cuts,warmups,suppressed,applied);
   assert(cuts==11&&warmups==0&&suppressed==192&&applied==32&&m.fog_cards_.armed);
   std::puts("cut_sequence frames=32 cuts=11 warmup=0 suppressed=192 applied=32 PASS");
 }
 // Genuine authority, Reset and toggle changes still need one successful
 // stacked frame. Failures keep their native fallback / Reset-only fault.
 for(unsigned scenario=0;scenario<8;++scenario){
   MotionOutput m;m.warm();m.cut_finished_=true;m.counters_.cut=true;
   if(scenario<5){
     const char* family=scenario==1?"foggreenoutlands":"bluewell";
     const unsigned sector=scenario==0?0x2000:0x1000;
     if(scenario==2)++m.generation_;
     if(scenario==3)m.reset_fog_for_test();
     if(scenario==4){m.volumetric_fog_toggle();m.volumetric_fog_toggle();}
     ++m.frame_;m.volumetric_fog_begin_frame();m.sample(family,sector);m.prepare_volumetric_fog_targets(64,48);
     assert(m.fog_cards_.warmup&&!m.fog_cards_.armed);m.draw();assert(m.fog_cards_.suppressed==0);
     m.run_volumetric_fog();assert(m.fog_storage.executes==1&&m.fog_cards_.armed);
     ++m.frame_;m.volumetric_fog_begin_frame();m.sample(family,sector);m.draw();m.run_volumetric_fog();
     assert(!m.fog_cards_.warmup&&m.fog_cards_.suppressed==1&&m.fog_storage.executes==2&&!m.fog_cards_.fault);
   }else if(scenario==5){
     m.reset_fog_for_test();m.next();m.prepare_volumetric_fog_targets(64,48);
     m.fog_storage.execute_result=E_FAIL;m.draw();m.run_volumetric_fog();
     assert(!m.fog_cards_.armed&&!m.fog_cards_.fault&&!m.fog_cards_.suppressed);
     m.next();assert(m.fog_cards_.warmup);m.fog_storage.execute_result=S_OK;m.draw();m.run_volumetric_fog();
     assert(m.fog_cards_.armed&&!m.fog_cards_.suppressed);m.next();m.draw();m.run_volumetric_fog();assert(m.fog_cards_.suppressed==1);
   }else{
     m.draw();assert(m.fog_cards_.suppressed==1);
     if(scenario==6)m.fog_storage.execute_result=E_FAIL;
     else m.prerequisites_ready=false; // late loss of owner/camera/depth qualification
     m.run_volumetric_fog();assert(m.fog_cards_.fault&&!m.fog_cards_.armed);
     const auto executes=m.fog_storage.executes;
     m.next();m.draw();m.run_volumetric_fog();assert(!m.fog_cards_.suppressed&&m.fog_storage.executes==executes);
     m.reset_fog_for_test();m.prerequisites_ready=true;m.fog_storage.execute_result=S_OK;m.next();
     m.prepare_volumetric_fog_targets(64,48);assert(m.fog_cards_.warmup);
     m.draw();m.run_volumetric_fog();assert(!m.fog_cards_.fault&&m.fog_cards_.armed&&!m.fog_cards_.suppressed);
   }
 }
 std::puts("cut_recovery scenarios=8 PASS");
 { // Stored-density range off: a gap never reaches the density path. On: a frame gap without 500 ms of wall clock
   // (a stutter) keeps the cache; a long one is a load and invalidates exactly once.
   MotionOutput m;m.warm();m.frame_+=5;mock_qpc+=10;m.sample();assert(m.fog_storage.density_calls==0);
   m.fog_density_requested_=true;m.fog_density_config_.enabled=true;++m.frame_;m.sample();++m.frame_;m.sample();assert(m.fog_storage.density_calls==0);
   m.frame_+=2;m.sample();assert(m.fog_storage.density_calls==0);           // one missed sample, no time passed
   ++m.frame_;mock_qpc+=10;m.sample();assert(m.fog_storage.density_calls==0); // slow frame, no missed sample
   m.frame_+=5;mock_qpc+=1;m.sample();assert(m.fog_storage.density_calls==1);++m.frame_;m.sample();assert(m.fog_storage.density_calls==1);
 }
 { // Stored range with card replacement: cards are never masked while the far ramp is below 1; the medium stacks meanwhile.
   MotionOutput m;m.warm();m.fog_density_requested_=true;m.fog_density_prepared_=true;
   for(float ready:{0.f,.25f,.99f}){
     m.fog_storage.density.ready_far=ready;m.next();assert(m.fog_cards_.warmup&&!m.fog_cards_.may_replace());
     m.draw();assert(!m.fog_cards_.suppressed&&!m.fog_cards_.refused);m.run_volumetric_fog();assert(!m.fog_cards_.fault);
   }
   m.fog_storage.density.ready_far=1.f;m.next();assert(!m.fog_cards_.warmup);m.draw();assert(m.fog_cards_.suppressed==1);m.run_volumetric_fog();assert(!m.fog_cards_.fault);
   m.fog_storage.density.ready_far=.5f;m.next();assert(m.fog_cards_.warmup);m.draw();assert(!m.fog_cards_.suppressed);m.run_volumetric_fog();assert(!m.fog_cards_.fault);
 }
 { // R3: a pending prefill holds the transit's gap; the first Ready sample decides once; the record ends there.
   MotionOutput m;m.warm();m.fog_density_requested_=true;m.fog_density_config_.enabled=true;++m.frame_;m.sample();
   const std::uint64_t key=fog_sector_placement(m.fog_sector_).key;
   auto transit=[&](std::uint64_t prefill_key){
     m.fog_prefill_={true,0x1000,7,m.fog_sector_.index,m.fog_sector_.profile,m.fog_sector_.recipe,prefill_key,mock_qpc};
     m.frame_+=5;mock_qpc+=10;sector_background::Sample s;s.status=sector_background::Status::NoCockpit; // the stall, then no_cockpit
     m.volumetric_fog_sector_sample(m.frame_,s);
     const bool held=m.fog_prefill_.pending;++m.frame_;m.sample();return held&&!m.fog_prefill_.pending;
   };
   const unsigned before=m.fog_storage.density_calls;
   const bool confirmed=transit(key)&&m.fog_storage.density_calls==before;          // same key: the fill is kept, no invalidation
   const bool discarded=transit(key+1)&&m.fog_storage.density_calls==before+1;      // another key: one invalidation, not two
   m.frame_+=5;mock_qpc+=10;m.sample();const bool later=m.fog_storage.density_calls==before+2; // no record left: a later gap invalidates as before
   assert(confirmed&&discarded&&later&&m.fog_prefill_logs_==2);
   std::printf("prefill_gap confirmed=%u discarded=%u later_gap=%u PASS\n",unsigned(confirmed),unsigned(discarded),unsigned(later));
 }
 std::puts("actual MotionOutput card methods PASS");
}
