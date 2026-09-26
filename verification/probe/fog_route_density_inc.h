// Included by fog_route_bridge.cpp inside its anonymous namespace: the stored-density range
// (X3M_VOLUMETRIC_FOG_RANGE=stored) through the unchanged production fog fragment, the real
// FogPass, its worker thread and a native D3D9 device. The owner stays synthetic.
struct DensityRun {
    IDirect3D9* api;IDirect3DDevice9* d;const D3DCAPS9& caps;fog_spatial_state::Inputs& inputs;
    static ULONG device_refs(IDirect3DDevice9* d){d->AddRef();return d->Release();}
    static double now_ms(){LARGE_INTEGER t{},f{};QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return double(t.QuadPart)*1e3/double(f.QuadPart);}
    static void place(x3m::MotionOutput& m){
        // Pose A of the stored-density screen: fog within reach. Identity rotation, t = -camera.
        const float camera[3]{95576.f,97323.f,82698.f};for(unsigned i=0;i<3;++i)m.camera_scene_.t[i]=-camera[i];
    }
    static inline bool masked_below_ramp=false;
    struct Frame {std::vector<std::uint16_t> source,image;bool suppressed=false,applied=false;float ready_far=0,ready_fine=0;unsigned quads=0,copies=0;std::uint64_t nodes=0;};
    static Frame frame(Bridge& b,const char* family="bluewell",unsigned sector=0x1000,bool sample=true){
        auto& m=b.motion;Frame f;const auto applied=m.fog_applied_frames_;
        b.begin(family,sector,8,sample);check(b.draw(),"density card");f.source=b.source();f.suppressed=m.fog_cards_.suppressed!=0;
        b.end();f.applied=m.fog_applied_frames_==applied+1;f.quads=b.hooks.calls[83];f.copies=b.hooks.calls[34];f.image=readback_words(b.d,b.scene.s0.p);
        if(m.fog_){const auto& s=m.fog_->density_status();f.ready_far=s.ready_far;f.ready_fine=s.ready_fine;f.nodes=s.nodes_generated;}
        if(m.fog_density_requested_&&!m.fog_density_refused_&&f.suppressed&&f.ready_far<1)masked_below_ramp=true;
        return f;
    }
    // Frames until both levels are fully ramped and the replacement applies. Every frame before the
    // far level is drawable must be native-exact with no fog transaction; ramps rise by <= 1/90.
    struct Fill {Frame last;unsigned frames=0,filling=0;double far_ms=0,fine_ms=0;bool native_exact=true,no_transaction=true,monotone=true,bounded=true,no_fault=true,stacked=true;unsigned ramping=0;};
    static Fill fill(Bridge& b,const std::vector<std::uint16_t>& vanilla,const char* family="bluewell",unsigned sector=0x1000){
        Fill r;const double begin=now_ms();float far_level=0,fine_level=0;
        for(;;){
            Frame f=frame(b,family,sector);++r.frames;
            if(!(f.ready_far>0)){++r.filling;r.native_exact=r.native_exact&&f.source==vanilla&&!f.suppressed&&f.image==vanilla;r.no_transaction=r.no_transaction&&!f.applied&&f.quads==0&&f.copies==0;}
            if(f.ready_far>0&&f.ready_far<1){++r.ramping;r.stacked=r.stacked&&f.applied&&!f.suppressed&&f.source==vanilla&&f.quads==3;} // cards and the ramping medium together
            if(f.ready_far>0&&far_level>0){r.monotone=r.monotone&&f.ready_far>=far_level;r.bounded=r.bounded&&f.ready_far-far_level<=1.f/90.f+1e-6f;}
            if(f.ready_fine>0&&fine_level>0)r.bounded=r.bounded&&f.ready_fine-fine_level<=1.f/90.f+1e-6f;
            far_level=f.ready_far;fine_level=f.ready_fine;
            if(!r.far_ms&&f.ready_far>=1)r.far_ms=now_ms()-begin;if(!r.fine_ms&&f.ready_fine>=1)r.fine_ms=now_ms()-begin;
            r.no_fault=r.no_fault&&!b.motion.fog_cards_.fault&&!b.motion.motion_state_lost_;
            r.last=std::move(f);
            if(r.last.ready_far>=1&&r.last.ready_fine>=1&&r.last.suppressed&&r.last.applied)return r;
            if(now_ms()-begin>90e3)throw std::runtime_error("stored-density fill timeout");
            Sleep(1);
        }
    }
    // The frame row the fragment logged for the owner's current frame (empty when it logged none).
    static std::string row_of(const x3m::MotionOutput& m){
        char needle[64];std::snprintf(needle,sizeof needle," frame=%llu applied=",(unsigned long long)m.frame_);
        return std::strstr(x3m::last_frame_row,needle)?std::string(x3m::last_frame_row):std::string();
    }
    static bool has(const std::string& row,const char* field){return row.find(field)!=std::string::npos;}
    // The motes' on/off fixture seam (fog-dust-motes.md section 5.3; the Ctrl+Alt+F11 key went on 2026-09-26) through the
    // production fragment, called between frames, a launch with the motes on (N 8192 over the widest
    // window, R 5000, so the pose's fog is within reach). The drift clock is the real one, so on frames are not compared
    // byte for byte; the off frames are the launch-off stored frame, and the placement law is the pass fixture's.
    void motes_ab(const std::vector<std::uint16_t>& vanilla,const std::vector<std::uint16_t>& in_march){
        fog_spatial_state::Hooks hooks(d,api);fog_spatial_state::Scene scene(d,inputs,caps,std::vector<DWORD>(std::begin(card_program),std::end(card_program)));
        Bridge b(d,hooks,scene,caps);b.record_aux();auto& m=b.motion;place(m);m.fog_density_requested_=true;m.fog_timing_=false;
        x3m::renderer::FogMoteTuning motes{};motes.count=8192;motes.radius=5000.f;
        m.fog_dust_motes_launch_=true;m.fog_density_config_.motes=motes;m.fog_density_config_.dust_motes=true; // configure_volumetric_fog_dust_motes
        // A frame whose number is a multiple of 600 prints its row whatever changed. The jump there is a sample gap, which
        // disarms the card replacement for one warm-up frame (fog_sector_.frame + 1 < frame_): two frames re-arm it first.
        auto periodic=[&]{const std::uint64_t r=m.frame_%600;m.frame_+=r<=597?597-r:1197-r;frame(b);frame(b);return frame(b);};
        Fill on_fill=fill(b,vanilla);IDirect3DVertexBuffer9* const vb=m.fog_->fixture_mote_vertices();
        Frame on=periodic();const std::string on_row=row_of(m);
        std::printf("MOTES_AB on_frames=%u vb=%u visible=%u on_row=%s\n",on_fill.frames,unsigned(vb!=nullptr),unsigned(on.image!=in_march),on_row.empty()?"none":on_row.c_str());
        require(vb&&m.fog_->motes_variant()&&on.applied&&on.quads==3&&m.fog_motes_drawn_,"motes_ab_launch_on_creates_at_prepare_and_draws");
        require(has(on_row," motes=1 mote_count=8192 mote_calls=12 ")&&has(on_row," mote_shadow=none mote_refused=none"),"motes_ab_launch_on_row_fields");
        // Toggle off at the frame boundary: one row; the frame is the launch-off stored frame; the buffers stay.
        const unsigned allocations=m.fog_->allocations(),references=m.fog_->references(),rows_before=x3m::motes_rows;
        require(m.volumetric_fog_dust_motes_toggle()==0&&x3m::motes_rows==rows_before+1&&has(x3m::last_motes_row,"enabled=0 refused=none"),"motes_ab_toggle_off_logs_one_row");
        Frame off=periodic();const std::string off_row=row_of(m);
        std::printf("MOTES_AB off_row=%s\n",off_row.empty()?"none":off_row.c_str());
        require(off.applied&&off.image==in_march&&off.quads==3&&!m.fog_->motes_variant()&&!m.fog_motes_drawn_,"motes_ab_toggled_off_frame_is_the_launch_off_frame");
        require(has(off_row," motes=0 mote_count=0 mote_calls=0 "),"motes_ab_toggled_off_row_fields");
        require(m.fog_->fixture_mote_vertices()==vb&&m.fog_->allocations()==allocations&&m.fog_->references()==references,"motes_ab_toggled_off_keeps_the_buffers");
        // Twenty alternating frames: one toggle row per press, nothing created; every off frame is the launch-off frame.
        const unsigned toggles_before=x3m::motes_rows;bool alternating=true;
        for(unsigned i=0;i<20;++i){const int state=m.volumetric_fog_dust_motes_toggle();Frame f=frame(b);alternating=alternating&&f.applied&&f.quads==3&&m.fog_motes_drawn_==(state==1)&&(state==1||f.image==in_march);}
        std::printf("MOTES_AB alternating_frames=20 toggle_rows=%u allocations=%u,%u\n",x3m::motes_rows-toggles_before,allocations,m.fog_->allocations());
        require(alternating&&x3m::motes_rows-toggles_before==20u&&!m.fog_->motes_variant(),"motes_ab_alternating_frames_applied");
        require(m.fog_->fixture_mote_vertices()==vb&&m.fog_->allocations()==allocations&&m.fog_->references()==references,"motes_ab_alternating_creates_nothing");
        // Reset while on: the VB/IB go with the targets and come back once, at the first latch that prepares the pass.
        require(m.volumetric_fog_dust_motes_toggle()==1,"motes_ab_toggle_on_before_reset");
        const unsigned before_reset=m.fog_->allocations();
        m.fog_->before_reset();const bool released=!m.fog_->fixture_mote_vertices();
        m.fog_sector_={};m.fog_cards_={};m.fog_attach_failed_=false;m.fog_density_prepared_=false;++m.generation_;m.fog_->after_reset(S_OK);
        Fill reset_on=fill(b,vanilla);IDirect3DVertexBuffer9* const regrown=m.fog_->fixture_mote_vertices();const unsigned after_fill=m.fog_->allocations();
        Frame again=frame(b);Frame again2=frame(b);
        std::printf("MOTES_AB reset_allocations=%u,%u,%u released=%u regrown=%u march_scale=%u\n",before_reset,after_fill,m.fog_->allocations(),unsigned(released),unsigned(regrown!=nullptr),m.fog_->density_march_scale());
        require(released&&reset_on.native_exact&&reset_on.no_fault,"motes_ab_reset_releases_the_buffers");
        // The family atlas 1 (prepare_field) + targets 1 + the quarter march target at spacing 4 (1, none at 2) + DEFAULT density
        // atlases 2 + mote VB/IB 1, and nothing more after.
        const unsigned quarter=unsigned(m.fog_->density_march_scale()==x3m::renderer::fog_march_scale_quarter);
        require(regrown&&after_fill==before_reset+5+quarter&&m.fog_->allocations()==after_fill&&m.fog_->fixture_mote_vertices()==regrown,"motes_ab_reset_recreates_the_buffers_once");
        require(again.applied&&again2.applied&&m.fog_motes_drawn_,"motes_ab_after_reset_draws");
    }
    void run(){
        std::vector<std::uint16_t> legacy_image,legacy_warm,vanilla,stored_reference;
        {   // Legacy reference at the same camera: the option off must never reach the density path.
            fog_spatial_state::Hooks hooks(d,api);fog_spatial_state::Scene scene(d,inputs,caps,std::vector<DWORD>(std::begin(card_program),std::end(card_program)));
            Bridge b(d,hooks,scene,caps);b.record_aux();place(b.motion);
            Frame warm=frame(b);vanilla=warm.source;legacy_warm=warm.image;Frame replaced=frame(b);legacy_image=replaced.image;
            require(replaced.suppressed&&replaced.applied&&replaced.quads==2&&legacy_image!=vanilla,"density_reference_legacy_frame");
            const auto& s=b.motion.fog_->density_status();
            require(!b.motion.fog_->fixture_density_cache()&&!s.available&&!std::strcmp(s.reason,"off")&&s.nodes_generated==0&&!b.motion.fog_->density_ready(scene.input.w,scene.input.h),"legacy_mode_never_touches_density");
            std::printf("IMAGE legacy_pose_a %016llx\n",fnv(legacy_image));
        }
        {   // Capability refusal: one line, then exactly the legacy frames.
            D3DCAPS9 small=caps;small.MaxPixelShader30InstructionSlots=511;
            fog_spatial_state::Hooks hooks(d,api);fog_spatial_state::Scene scene(d,inputs,caps,std::vector<DWORD>(std::begin(card_program),std::end(card_program)));
            Bridge b(d,hooks,scene,small);b.record_aux();place(b.motion);b.motion.fog_density_requested_=true;
            Frame warm=frame(b),second=frame(b),replaced=frame(b); // the first frame has no posted camera yet
            require(b.motion.fog_density_refused_&&!b.motion.fog_->fixture_density_cache()&&!std::strcmp(b.motion.fog_->density_status().reason,"density_ps30_slots"),"stored_refusal_reason_and_no_worker");
            require(warm.source==vanilla&&warm.image==vanilla&&second.image==legacy_warm&&replaced.image==legacy_image&&replaced.quads==2,"stored_refused_is_legacy_bit_identical");
        }
        const ULONG refs0=device_refs(d);
        {
            fog_spatial_state::Hooks hooks(d,api);fog_spatial_state::Scene scene(d,inputs,caps,std::vector<DWORD>(std::begin(card_program),std::end(card_program)));
            Bridge b(d,hooks,scene,caps);b.record_aux();auto& m=b.motion;place(m);m.fog_density_requested_=true;m.fog_timing_=false;
            const ULONG refs_bridge=device_refs(d);
            Fill a=fill(b,vanilla);const auto stored_a=a.last.image;const auto key_a=m.fog_density_config_.sector_key;const double offset_a[3]{m.fog_density_config_.world_offset[0],m.fog_density_config_.world_offset[1],m.fog_density_config_.world_offset[2]};
            std::printf("DENSITY_FILL sector=a frames=%u filling_frames=%u far_ready_ms=%.1f fine_ready_ms=%.1f nodes=%llu\n",a.frames,a.filling,a.far_ms,a.fine_ms,(unsigned long long)a.last.nodes);
            require(a.filling>0&&a.native_exact&&a.no_transaction,"stored_filling_frames_native_exact_no_transaction");
            require(a.monotone&&a.bounded&&a.no_fault,"stored_ramps_monotone_bounded");
            require(a.ramping>=80&&a.stacked,"stored_ramp_stacks_medium_on_native_cards");
            require(a.last.quads==3&&a.last.copies==1&&a.last.source==inputs.scene,"stored_march_composite_repair_once");
            require(stored_a!=legacy_image&&stored_a!=inputs.scene&&stored_a!=vanilla,"stored_renders_through_route");
            require(m.fog_->fixture_density_cache()&&m.fog_->density_status().available&&m.fog_->density_ready(scene.input.w,scene.input.h),"stored_dynamic_cache_live");
            Frame steady=frame(b);require(steady.image==stored_a&&steady.suppressed&&steady.applied,"stored_steady_frame_bit_identical");
            stored_reference=stored_a; // the launch-off (in-march) frame at pose A, default spacing, for the motes A/B below
            {   // The dust motes absent (fog-dust-motes.md section 4): nothing created, no report, no row field, the toggle a no-op.
                const unsigned rows=x3m::motes_rows;
                require(!m.fog_->fixture_mote_vertices()&&m.fog_->mote_report().frame==~std::uint64_t(0)&&!m.fog_->motes_variant()&&!std::strstr(x3m::last_frame_row," motes=")&&
                        m.volumetric_fog_dust_motes_toggle()==-1&&x3m::motes_rows==rows,"motes_ab_option_absent_creates_nothing_rows_absent");
            }
            std::printf("IMAGE stored_sector_a %016llx\n",fnv(stored_a));
            // Sector change: re-key, zero readiness in the same frame, native cards while it refills.
            const auto nodes_a=steady.nodes;
            {   // Reallocation: new heap tokens, same record index and family. Cards re-warm; the cache neither re-keys nor refills.
                // (the worker is still growing its window, so node totals move; a refill is a new first fill)
                const auto fills=m.fog_->fixture_density_cache()->stats().first_fills;
                Fill moved_tokens=fill(b,vanilla,"bluewell",0x7000);
                require(m.fog_density_config_.sector_key==key_a&&moved_tokens.filling==0&&moved_tokens.ramping==0&&m.fog_->fixture_density_cache()->stats().first_fills==fills&&moved_tokens.last.image==stored_a,"heap_token_change_keeps_key_cache_and_image");
                fill(b,vanilla);
            }
            const auto invalidations_b=m.invalidations;b.index=2;Frame first_b=frame(b,"bluewell",0x3000);
            bool moved=false;for(unsigned i=0;i<3;++i)moved=moved||m.fog_density_config_.world_offset[i]!=offset_a[i];
            for(unsigned i=0;i<3;++i){const double n=m.fog_density_config_.world_offset[i]/4096.;moved=moved&&n==std::floor(n)&&n>=-2048&&n<2048;}
            require(m.fog_density_config_.sector_key!=key_a&&moved,"sector_change_rekeys_whole_far_node_offset");
            require(first_b.ready_far==0&&first_b.ready_fine==0&&!first_b.applied&&!first_b.suppressed&&first_b.source==vanilla&&first_b.quads==0&&m.invalidations==invalidations_b+1,"sector_change_drops_readiness_same_frame");
            Fill sector_b=fill(b,vanilla,"bluewell",0x3000); // b.index stays 2
            std::printf("DENSITY_FILL sector=b frames=%u filling_frames=%u far_ready_ms=%.1f fine_ready_ms=%.1f nodes=%llu\n",sector_b.frames,sector_b.filling,sector_b.far_ms,sector_b.fine_ms,(unsigned long long)sector_b.last.nodes);
            require(sector_b.filling>0&&sector_b.native_exact&&sector_b.no_transaction&&sector_b.monotone&&sector_b.bounded&&sector_b.no_fault&&sector_b.last.nodes>nodes_a,"sector_change_refills_and_ramps");
            require(sector_b.last.image!=stored_a&&sector_b.last.image!=vanilla,"sector_change_places_clouds_differently");
            std::printf("IMAGE stored_sector_b %016llx\n",fnv(sector_b.last.image));
            b.index=1;Fill again=fill(b,vanilla);require(again.last.image==stored_a,"sector_rekey_is_deterministic");
            // The fog seam off: nothing is prepared or drawn, the worker completes its window and parks.
            const unsigned references=m.fog_->references(),allocations=m.fog_->allocations();const ULONG refs_on=device_refs(d);
            m.volumetric_fog_toggle();
            const auto uploads_off=m.fog_->density_status().upload_bytes_total;bool off_native=true;
            std::uint64_t nodes=m.fog_->fixture_density_cache()->stats().nodes_generated;double stable_since=now_ms();const double off_begin=now_ms();
            while(now_ms()-stable_since<400.){
                Frame f=frame(b);off_native=off_native&&f.source==vanilla&&f.image==vanilla&&!f.applied&&f.quads==0;
                const auto n=m.fog_->fixture_density_cache()->stats().nodes_generated;if(n!=nodes){nodes=n;stable_since=now_ms();}
                if(now_ms()-off_begin>60e3)throw std::runtime_error("worker did not park");Sleep(5);
            }
            require(off_native&&m.fog_->density_status().upload_bytes_total==uploads_off,"toggle_off_native_and_no_density_work");
            std::printf("DENSITY_PARK ms=%.1f nodes=%llu\n",now_ms()-off_begin-400.,(unsigned long long)nodes);
            m.volumetric_fog_toggle();Fill resumed=fill(b,vanilla);
            require(resumed.last.image==stored_a&&m.fog_->references()==references&&m.fog_->allocations()==allocations&&device_refs(d)==refs_on,"toggle_on_resumes_without_leak");
            require(m.fog_->fixture_density_cache()->stats().nodes_generated==nodes,"toggle_on_regenerates_nothing");
            // Pass Reset edges with the synthetic owner's reset state (as the legacy witnesses above).
            const auto uploaded=m.fog_->density_status().upload_bytes_total;
            m.fog_->before_reset();m.fog_sector_={};m.fog_cards_={};m.fog_attach_failed_=false;m.fog_density_prepared_=false;++m.generation_;m.fog_->after_reset(S_OK);
            Fill reset=fill(b,vanilla);
            std::printf("DENSITY_RESET frames=%u reuploaded_bytes=%llu regenerated_nodes=%llu\n",reset.frames,(unsigned long long)(m.fog_->density_status().upload_bytes_total-uploaded),(unsigned long long)(m.fog_->fixture_density_cache()->stats().nodes_generated-nodes));
            require(reset.filling>0&&reset.native_exact&&reset.no_transaction&&reset.no_fault&&reset.last.image==stored_a,"reset_refills_gpu_and_reapplies_bit_identical");
            require(m.fog_->fixture_density_cache()->stats().nodes_generated==nodes&&m.fog_->density_status().upload_bytes_total-uploaded>=2*x3m::fog::kAtlasBytes,"reset_reuploads_without_regeneration");
            // A stutter: one frame without a sample, no wall clock to speak of. Nothing is invalidated.
            {const auto kept=m.fog_->fixture_density_cache()->stats().first_fills;frame(b,"bluewell",0x1000,false);Frame next=frame(b);
             require(next.ready_far==1&&next.ready_fine==1&&next.applied&&m.fog_->fixture_density_cache()->stats().first_fills==kept,"one_frame_gap_keeps_cache");
             Fill settled=fill(b,vanilla);require(settled.filling==0&&settled.ramping==0&&settled.last.image==stored_a&&m.fog_->fixture_density_cache()->stats().first_fills==kept,"one_frame_gap_resumes_bit_identical");}
            // A load: scene samples stop for longer than 500 ms. The cache is dropped and ramps again.
            for(unsigned i=0;i<3;++i)frame(b,"bluewell",0x1000,false);
            Sleep(600);
            Frame after_gap=frame(b);require(after_gap.ready_far==0&&!after_gap.applied&&!after_gap.suppressed&&after_gap.source==vanilla,"load_gap_invalidates_same_frame");
            Fill loaded=fill(b,vanilla);require(loaded.last.image==stored_a&&loaded.monotone&&loaded.bounded&&loaded.last.nodes>nodes,"load_gap_refills_and_ramps");
            // Camera cut inside the resident window: the world-anchored field needs no refill and no ramp.
            m.camera_scene_.t[0]+=300.f;Frame cut=frame(b),cut2=frame(b);
            require(cut.applied&&cut2.applied&&cut2.ready_far==1&&cut2.ready_fine==1&&cut2.image!=stored_a,"small_cut_stays_resident");
            // A jump: readiness drops for the frame's own camera before the cache has even heard of it.
            m.camera_scene_.t[0]-=2.6e6f;Frame jump=frame(b);
            require(!jump.applied&&!jump.suppressed&&jump.quads==0&&jump.source==vanilla&&!m.fog_cards_.fault,"camera_jump_is_native_same_frame");
            Fill far_away=fill(b,vanilla);require(far_away.no_fault&&far_away.native_exact&&far_away.no_transaction,"camera_jump_refills");
            // prepare cost on the render thread (diagnostic; not game FPS).
            std::sort(b.prepare_us.begin(),b.prepare_us.end());std::sort(b.prepare_upload_us.begin(),b.prepare_upload_us.end());
            auto pick=[](const std::vector<double>& v,double q){return v.empty()?0.:v[std::min(v.size()-1,size_t(q*double(v.size())))];};
            std::printf("DENSITY_COST steady_frames=%zu steady_us_median=%.2f steady_us_p99=%.2f upload_frames=%zu upload_us_median=%.1f upload_us_p99=%.1f upload_us_max=%.1f\n",b.prepare_us.size(),pick(b.prepare_us,.5),pick(b.prepare_us,.99),
                b.prepare_upload_us.size(),pick(b.prepare_upload_us,.5),pick(b.prepare_upload_us,.99),b.prepare_upload_us.empty()?0.:b.prepare_upload_us.back());
            std::printf("DENSITY_COST_DETAIL first_call_us=%.1f worst_upload_us=%.1f worst_upload_frame=%llu worst_upload_rects=%u upload_frames_over_1ms=%zu\n",b.first_prepare_us,b.worst_upload_us,(unsigned long long)b.worst_upload_frame,b.worst_upload_rects,
                size_t(b.prepare_upload_us.end()-std::upper_bound(b.prepare_upload_us.begin(),b.prepare_upload_us.end(),1000.)));
            // Device release with a live worker: re-key, let it generate, then the production release statements.
            frame(b,"bluewell",0x5000);frame(b,"bluewell",0x5000);Sleep(30);
            const auto generating=m.fog_->fixture_density_cache()->stats().nodes_generated;Sleep(30);
            const bool live=m.fog_->fixture_density_cache()->stats().nodes_generated>generating&&m.fog_->fixture_density_cache()->running();
            const double t0=now_ms();m.release_fog();const double joined=now_ms()-t0;
            std::printf("DENSITY_RELEASE live_worker=%u join_ms=%.2f device_refs=%lu expected=%lu\n",unsigned(live),joined,device_refs(d),refs_bridge);
            require(live&&!m.fog_&&joined<2000.&&device_refs(d)==refs_bridge,"device_release_with_live_worker_joins_and_balances");
        }
        require(device_refs(d)==refs0,"stored_route_leaves_device_refcount");
        require(!masked_below_ramp,"cards_never_masked_below_full_far_ramp");
        {   // Process-detach form: abandon, then ~FogPass. No join, no wait; D3D objects still released.
            fog_spatial_state::Hooks hooks(d,api);fog_spatial_state::Scene scene(d,inputs,caps,std::vector<DWORD>(std::begin(card_program),std::end(card_program)));
            Bridge b(d,hooks,scene,caps);b.record_aux();auto& m=b.motion;place(m);m.fog_density_requested_=true;
            const ULONG refs=device_refs(d);
            for(unsigned i=0;i<4;++i)frame(b);Sleep(20);
            require(m.fog_->fixture_density_cache()&&m.fog_->fixture_density_cache()->running(),"abandon_witness_has_live_worker");
            const double t0=now_ms();m.fog_->abandon_density_worker();const bool gone=!m.fog_->fixture_density_cache();m.fog_.reset();const double took=now_ms()-t0;
            std::printf("DENSITY_ABANDON ms=%.2f\n",took);
            require(gone&&took<200.&&device_refs(d)==refs,"abandon_then_pass_destructor_is_prompt_and_balanced");
        }
        motes_ab(vanilla,stored_reference);
    }
};
