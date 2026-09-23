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
 // Hooked and unhooked admission agree on every mismatched required value
 // (ZENABLE 1 is the material's own value, admitted: its refused value is USEW, 2).
 for(bool hooked:{false,true})for(unsigned state=0;state<12;++state){
   MotionOutput m;m.state_hooks_=hooked;m.warm();const long flip=state==0?2:1;
   if(state<8){m.device_storage.states[state]^=flip;for(unsigned i=0;i<32;++i)if(shadow_states[i]==state)m.shadow_.states[i]^=DWORD(flip);}
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
 { // run273 A: a Ready sample of another sector (its id, both known) with the same placement key is a transit (no gap
   // needed): one invalidation, the stale camera dropped; the same object again is nothing; another sector with another
   // key only drops the camera. A heap-token change of the same sector (same or unread id) keeps field and camera (Run75).
   MotionOutput m;m.warm();m.fog_density_requested_=true;m.fog_density_config_.enabled=true;++m.frame_;m.sample("bluewell",0x1000);
   m.fog_density_key_=fog_sector_placement(m.fog_sector_).key;
   auto ready=[&](unsigned sector,std::uint32_t id,const char* family="bluewell"){m.fog_density_camera_valid_=true;++m.frame_;
     sector_background::Sample s;s.status=sector_background::Status::Ready;s.row_valid=s.name_valid=true;s.dust=8;s.sector=sector;s.sector_id=id;
     std::strcpy(s.family,family);m.volumetric_fog_sector_sample(m.frame_,s);};
   const unsigned before=m.fog_storage.density_calls;
   ready(0x1000,0);const bool same=m.fog_storage.density_calls==before&&m.fog_density_camera_valid_;
   ready(0x2000,0);const bool token=m.fog_storage.density_calls==before&&m.fog_density_camera_valid_;          // another object, id unread: a reallocation
   ready(0x2000,77);ready(0x2000,77);const bool id_same=m.fog_storage.density_calls==before&&m.fog_density_camera_valid_;
   ready(0x5000,77);const bool realloc=m.fog_storage.density_calls==before&&m.fog_density_camera_valid_;        // new token, same id: field kept
   ready(0x5000,78);const bool id_change=m.fog_storage.density_calls==before+1&&!m.fog_density_camera_valid_;  // the freed address reused, another id
   ready(0x119301a8,2317);const unsigned source=m.fog_storage.density_calls;                                // run273 frame 19555: token and id, index 2 both
   ready(0x6cb11818,3221);const bool run273=m.fog_storage.density_calls==source+1&&!m.fog_density_camera_valid_;
   ready(0x3000,79,"foggreenoutlands");const bool rekey=m.fog_storage.density_calls==source+1&&!m.fog_density_camera_valid_; // the latch re-keys
   m.fog_density_config_.enabled=false;m.fog_density_key_=0;ready(0x4000,80);const bool first=m.fog_storage.density_calls==source+1&&!m.fog_density_camera_valid_;
   assert(same&&token&&id_same&&realloc&&id_change&&run273&&rekey&&first);
   std::printf("run273_transit same=%u token=%u id_same=%u realloc=%u id_change=%u run273=%u rekey=%u first=%u PASS\n",unsigned(same),unsigned(token),unsigned(id_same),
               unsigned(realloc),unsigned(id_change),unsigned(run273),unsigned(rekey),unsigned(first));
 }
 { // run273 B: a confirmed prefill adopts its key (the latch logs no sector_key epoch) and drops the stale camera; a
   // discarded one drops it too and invalidates once.
   MotionOutput m;m.warm();m.fog_density_requested_=true;m.fog_density_config_.enabled=true;++m.frame_;m.sample();
   const std::uint64_t key=fog_sector_placement(m.fog_sector_).key;
   auto transit=[&](std::uint64_t prefill_key){
     m.fog_prefill_={true,0x1000,7,m.fog_sector_.index,m.fog_sector_.profile,m.fog_sector_.recipe,prefill_key,mock_qpc};
     m.fog_density_camera_valid_=true;m.fog_density_key_=0;
     m.frame_+=5;mock_qpc+=10;sector_background::Sample s;s.status=sector_background::Status::NoCockpit;
     m.volumetric_fog_sector_sample(m.frame_,s);++m.frame_;m.sample();
   };
   const unsigned before=m.fog_storage.density_calls;
   transit(key);const bool adopted=m.fog_density_key_==key&&!m.fog_density_camera_valid_&&m.fog_storage.density_calls==before;
   transit(key+1);const bool discarded=m.fog_density_key_==0&&!m.fog_density_camera_valid_&&m.fog_storage.density_calls==before+1;
   assert(adopted&&discarded);
   std::printf("run273_prefill_adopt adopted=%u discarded=%u PASS\n",unsigned(adopted),unsigned(discarded));
 }
 { // run273 C: the frame's first refusal is named (gate:<gate>, or ready:<component>), kept for the frame, reset at begin.
   auto refusal=[](const MotionOutput& m){return std::string(m.fog_card_refusal_?(m.fog_card_refusal_ready_?"ready:":""):"")+(m.fog_card_refusal_?m.fog_card_refusal_:"none");};
   unsigned named=0;
   auto expect=[&](MotionOutput& m,const char* want){const bool ok=refusal(m)==want;if(!ok)std::printf("refusal got=%s want=%s\n",refusal(m).c_str(),want);assert(ok);named+=std::strcmp(want,"none")!=0;};
   { MotionOutput m;m.warm();m.draw();expect(m,"none");assert(m.fog_cards_.suppressed==1); }
   { MotionOutput m;m.warm();m.active_queries_=1;m.draw();expect(m,"gate:queries");
     m.active_queries_=0;m.fog_cards_.refused=false;m.hdr_state_=HdrState::Off;m.draw();expect(m,"gate:queries"); // the first refusal stays
     m.next();expect(m,"none"); }
   { MotionOutput m;m.warm();m.hdr_state_=HdrState::Off;m.draw();expect(m,"gate:owner"); }
   { MotionOutput m;m.warm();m.counters_.filled=false;m.draw();expect(m,"gate:linear_depth"); }
   { MotionOutput m;m.warm();m.bound=false;m.draw();expect(m,"gate:scene"); }
   { MotionOutput m;m.warm();m.fog_frame_=m.frame_;m.draw();expect(m,"gate:pass_done"); }
   { MotionOutput m;m.warm();m.device_storage.frequency=2;m.draw();expect(m,"gate:frequency"); }
   { MotionOutput m;m.warm();m.device_storage.states[4]=0;m.draw();expect(m,"gate:states"); }
   { MotionOutput m;m.warm();m.prerequisites_ready=false;m.draw();expect(m,"ready:prerequisite");assert(!m.fog_card_ready_); }
   { MotionOutput m;m.warm();m.fog_storage.ready=false;m.draw();expect(m,"ready:resources"); }
   { MotionOutput m;m.warm();m.parameters_ready=false;m.draw();expect(m,"ready:parameters"); }
   { // The docked-at-load shape: cards armed by the cold step, density prepared and far-ready, the frame's origin not drawable.
     MotionOutput m;m.warm();m.fog_density_requested_=true;m.fog_density_prepared_=true;m.fog_storage.density.ready_far=1.f;m.next();
     m.fog_storage.drawable=false;m.draw();expect(m,"ready:density_drawable");assert(!m.fog_card_ready_&&m.fog_cards_.refused);
     m.fog_storage.drawable=true;m.next();m.draw();expect(m,"none");assert(m.fog_cards_.suppressed==1);m.complete(true,"ok");
     m.fog_density_prepared_=false;m.next();m.draw();expect(m,"none");assert(m.fog_cards_.warmup); // unprepared: warm-up, no refusal
   }
   { // The cards line: a refusal change is a change of the report (60-frame spacing), the printed line records it.
     MotionOutput m;m.warm();m.draw();m.run_volumetric_fog();const auto logs=m.fog_card_logs_;
     auto advance=[&](unsigned frames){for(unsigned i=0;i<frames;++i){m.next();m.draw();m.run_volumetric_fog();}}; // consecutive masked frames keep the cards armed
     advance(60);const bool quiet=m.fog_card_logs_==logs;
     m.next();m.active_queries_=1;m.draw();m.run_volumetric_fog();
     const bool refused_line=m.fog_card_logs_==logs+1&&m.fog_card_last_refusal_&&!std::strcmp(m.fog_card_last_refusal_,"gate:queries");
     m.active_queries_=0;for(unsigned i=0;i<60;++i){m.next();m.active_queries_=1;m.draw();m.run_volumetric_fog();} // the same refusal: no further line
     const bool held=m.fog_card_logs_==logs+1;
     m.active_queries_=0;m.next();m.hdr_state_=HdrState::Off;m.draw();m.run_volumetric_fog();
     const bool changed_line=m.fog_card_logs_==logs+2&&!std::strcmp(m.fog_card_last_refusal_,"gate:owner");
     for(unsigned i=0;i<30;++i){m.next();m.draw();m.run_volumetric_fog();} // still owner, within 60 frames: no line
     const bool spaced=m.fog_card_logs_==logs+2&&m.frame_<600;
     if(!(quiet&&held))std::printf("cards_line quiet=%u held=%u\n",unsigned(quiet),unsigned(held));
     assert(quiet&&held);
     assert(refused_line&&changed_line&&spaced);
   }
   std::printf("run273_card_refusal named=%u PASS\n",named);
 }
 { // run278 C: the docked-at-load card carries the material's own z/cull (ZENABLE 1, CULLMODE CW) instead of the dust
   // pass's override; admitted, hooked and unhooked, with the same native calls as the in-flight card. Every other
   // value stays refused, and the refused vector's row is spaced 300 frames apart.
   unsigned admitted=0,refused=0;
   auto set=[](MotionOutput& m,unsigned state,long value){m.device_storage.states[state]=value;for(unsigned i=0;i<32;++i)if(shadow_states[i]==state)m.shadow_.states[i]=DWORD(value);};
   for(bool hooked:{false,true})for(unsigned variant=0;variant<3;++variant){
     MotionOutput m;m.state_hooks_=hooked;m.warm();
     if(variant!=1)set(m,D3DRS_ZENABLE,1);if(variant!=2)set(m,D3DRS_CULLMODE,2); // (1,CW), (0,CW), (1,NONE)
     m.draw();assert(!m.fog_cards_.refused&&m.fog_cards_.suppressed==1&&m.device_storage.calls==2&&m.device_storage.gets==(hooked?0:12));
     assert(m.fog_card_states_log_frame_==0&&!m.fog_card_refusal_);++admitted;
   }
   for(bool hooked:{false,true})for(unsigned wrong=0;wrong<6;++wrong){
     MotionOutput m;m.state_hooks_=hooked;m.warm();set(m,D3DRS_ZENABLE,1);set(m,D3DRS_CULLMODE,2);
     switch(wrong){case 0:set(m,D3DRS_ZENABLE,2);break;case 1:set(m,D3DRS_CULLMODE,3);break;case 2:set(m,D3DRS_CULLMODE,0);break;
      case 3:set(m,D3DRS_ZWRITEENABLE,1);break;case 4:set(m,D3DRS_STENCILENABLE,1);break;case 5:set(m,D3DRS_ALPHATESTENABLE,1);break;}
     m.draw();assert(m.fog_cards_.refused&&m.device_storage.calls==0&&!std::strcmp(m.fog_card_refusal_,"gate:states"));++refused;
   }
   MotionOutput m;m.warm();set(m,D3DRS_ZWRITEENABLE,1);
   m.draw();assert(m.fog_card_states_log_frame_==m.frame_+300); // the first refusal prints
   const auto next=m.fog_card_states_log_frame_;
   for(unsigned i=0;i<299;++i){m.next();m.draw();assert(m.fog_card_states_log_frame_==next);} // within the spacing: no row
   m.next();m.draw();assert(m.fog_card_states_log_frame_==next+300&&m.frame_==next);
   m.fog_cards_.refused=false;m.draw();assert(m.fog_card_states_log_frame_==next+300); // a second refused card of the frame: no row
   std::printf("run278_docked_states admitted=%u refused=%u spaced=1 PASS\n",admitted,refused);
 }
 std::puts("actual MotionOutput card methods PASS");
}
