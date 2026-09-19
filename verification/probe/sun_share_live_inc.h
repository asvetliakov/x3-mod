// Included in the actual D3D Fixture. Uses MotionOutput's existing native seam,
// authored original material pair and scene selector; no mocked lane producer.
void run_sun_lane(const char* bootstrap_vertex) {
    require(seam&&enabled&&taa&&hdr&&hdr_agx&&reference_ready&&emission_status&&emission_readback,
            "sun live needs the HDR/TAA native seam and reference");
    char mode[24]{};GetEnvironmentVariableA("X3M_FIXTURE_SUN_LIVE_CASE",mode,sizeof mode);
    const bool late=!std::strcmp(mode,"late_shader")||!std::strcmp(mode,"bind");
    // original_lane: the lane on original shading (X3M_LINEAR_MATERIALS=0,
    // legacy-sun-application.md section 4.1/4.2): the reviewed original pair
    // binds its own share variant (positive share, receivers counted) and on
    // frame 2 the cutout pair in its exact cutout state is admitted through
    // the tested-opaque arm (the exact arm is never configured without linear
    // materials) with its own share. shadow_apply (section 3.4): the depth
    // replay and the apply quad on (rotating camera seam, ownership bookends,
    // linear materials off, exponent 1); the sun is the fixture's default
    // LightDir_Dir0 (+Z world, the receiver's normal, so the light travels
    // toward the camera) and a caster plane at view depth 16 behind the
    // receiver at 12 is nearer to the light, so its footprint (4/3 of the
    // receiver's) shadows every receiver pixel (f = 0: C (1 - s)); frame 0
    // READONLY-locks the geometry after its draw (lease refused: map
    // unavailable, quad skipped), frame 2 has the untracked writer (lane
    // unavailable, quad skipped), a Reset precedes frame 4.
    // original_share_refused: the share producer refused for the reviewed pair
    // (X3M_FIXTURE_SUN_LANE_FAULT=original_share) under --original-fill 0.05:
    // the draw keeps its fill variant (the fill is never dropped), writes no
    // share, and the frame's lane is failed (available=0) with the draw counted.
    // original_lane_lightmap: original_lane under X3M_HULL_LIGHTMAP_GAIN=4
    // (hull-self-illumination.md 5): the reviewed pair binds the gained share
    // variant on every lane frame; the F4 action (hull_toggle, between frames
    // like the sampler) switches it off before frame 3 and on before frame 5.
    const bool lightmap=!std::strcmp(mode,"original_lane_lightmap");
    const bool original_lane=!std::strcmp(mode,"original_lane")||lightmap,shadow_apply=!std::strcmp(mode,"shadow_apply"),share_refused=!std::strcmp(mode,"original_share_refused");
    if(lightmap)require(hull_toggle!=nullptr,"hull toggle export");
    const bool untracked=!std::strcmp(mode,"untracked")||shadow_apply;
    const bool refused=!std::strcmp(mode,"caps")||!std::strcmp(mode,"cutout_drop")||!std::strcmp(mode,"alpha_mask");
    const bool allocation=!std::strcmp(mode,"allocation");
    const bool fallback=refused||allocation;
    const bool missing=!std::strcmp(mode,"composition_missing"),failed_m=!std::strcmp(mode,"composition_failed");
    // xt_state: the receiver draws in the XT class-C hull/station material
    // state of run 26 (alpha test on, ALPHAREF 1, GREATEREQUAL, RT0 mask 7;
    // z, z write on; blend, sRGB off) and must be tracked on every frame.
    // effects: a blended particle-like draw with z write off follows the
    // receiver on frame 2 and must not veto availability.
    // xt_state_lane_off: the same XT-state draws with X3M_SUN_SHADOW_LANE=0
    // must route exactly as before the tested-opaque arm existed: gate-4
    // refusal, nothing routed (the arm is lane-only). cutout_pair: a cutout
    // pair by identity (53a0a641107ed76c/63f96eba9eea7880) drawn with mask 7
    // and alpha test off, a state only the tested-opaque arm would admit, must
    // stay refused (state) with the lane on and veto as an untracked writer.
    // cutout_pair_bias: run 28 session B (run81): the same cutout pair drawn
    // in its exact cutout state (alpha test on, ALPHAREF 1, GREATEREQUAL,
    // mask 7, z write on) under a nonzero configured mip bias
    // (X3M_TAA_MIP_BIAS=-0.5), which leaves the exact cutout arm unconfigured
    // for the frame; the lane must track it through the tested-opaque arm
    // (routed, no gate-4 refusal, frame available) instead of vetoing.
    const bool lane_off=!std::strcmp(mode,"xt_state_lane_off"),cutout_pair=!std::strcmp(mode,"cutout_pair"),cutout_bias=!std::strcmp(mode,"cutout_pair_bias");
    const bool xt_state=!std::strcmp(mode,"xt_state")||lane_off,effects=!std::strcmp(mode,"effects");
    if(cutout_bias)require(mip_bias!=0,"cutout_pair_bias needs the configured nonzero mip bias");
    // unregistered (run 174, Argon Prime: the SM2 adeffects pair): an authored
    // vs_2_0/ps_2_0 pair outside the profile registry draws on frame 2 after
    // the receiver as an actual depth writer in a hostile state (alpha test on,
    // RT0 mask 7): the small triangle B nearer than the receiver (wins), then
    // the full-screen triangle behind it (loses everywhere), then an authored
    // vs_3_0/ps_3_0 pair, equally unknown, on B moved to the upper right (the
    // stamp follows the shader-model family of any future program). The route cannot
    // host it; the lane stamps share -1 over exactly the pixels it won and the
    // frame stays available. unregistered_fault: the stamp refused before its
    // first write (X3M_FIXTURE_SUN_LANE_FAULT=stamp): the draws veto as before.
    // unregistered_mid: the fault after the fourth issued state write (stamp_mid): everything restored, the draws veto.
    const bool stamp_mid=!std::strcmp(mode,"unregistered_mid");
    const bool stamp_fault=!std::strcmp(mode,"unregistered_fault")||stamp_mid,unregistered=!std::strcmp(mode,"unregistered")||stamp_fault;
    Com<IDirect3DVertexShader9> sm2_vs,sm3_vs;Com<IDirect3DPixelShader9> sm2_ps,sm3_ps;
    if(unregistered){
        // vs_2_0: def c200,0,0,0,1; dcl_position v0; r0.xyz=v0; r0.w=c200.w; oPos=rows(c24..c27)*r0
        const DWORD v[]={0xfffe0200u,0x05000051u,0xa00f00c8u,0,0,0,0x3f800000u,0x0200001fu,0x80000000u,0x900f0000u,
            0x02000001u,0x80070000u,0x90e40000u,0x02000001u,0x80080000u,0xa0ff00c8u,
            0x03000009u,0xc0010000u,0x80e40000u,0xa0e40018u,0x03000009u,0xc0020000u,0x80e40000u,0xa0e40019u,
            0x03000009u,0xc0040000u,0x80e40000u,0xa0e4001au,0x03000009u,0xc0080000u,0x80e40000u,0xa0e4001bu,0x0000ffffu};
        // ps_2_0: def c0,3,3,3,1; mov oC0,c0
        const DWORD p[]={0xffff0200u,0x05000051u,0xa00f0000u,0x40400000u,0x40400000u,0x40400000u,0x3f800000u,0x02000001u,0x800f0800u,0xa0e40000u,0x0000ffffu};
        api(d->CreateVertexShader(v,&sm2_vs.p),"authored unregistered vs_2_0");api(d->CreatePixelShader(p,&sm2_ps.p),"authored unregistered ps_2_0");
        // The same programs as vs_3_0 (dcl_position o0) and ps_3_0 (color 5).
        const DWORD v3[]={0xfffe0300u,0x05000051u,0xa00f00c8u,0,0,0,0x3f800000u,0x0200001fu,0x80000000u,0x900f0000u,0x0200001fu,0x80000000u,0xe00f0000u,
            0x02000001u,0x80070000u,0x90e40000u,0x02000001u,0x80080000u,0xa0ff00c8u,
            0x03000009u,0xe0010000u,0x80e40000u,0xa0e40018u,0x03000009u,0xe0020000u,0x80e40000u,0xa0e40019u,
            0x03000009u,0xe0040000u,0x80e40000u,0xa0e4001au,0x03000009u,0xe0080000u,0x80e40000u,0xa0e4001bu,0x0000ffffu};
        const DWORD p3[]={0xffff0300u,0x05000051u,0xa00f0000u,0x40a00000u,0x40a00000u,0x40a00000u,0x3f800000u,0x02000001u,0x800f0800u,0xa0e40000u,0x0000ffffu};
        api(d->CreateVertexShader(v3,&sm3_vs.p),"authored unregistered vs_3_0");api(d->CreatePixelShader(p3,&sm3_ps.p),"authored unregistered ps_3_0");
    }
    const std::string bootstrap_path(bootstrap_vertex);const auto bootstrap_slash=bootstrap_path.find_last_of("/\\");
    const auto folder=bootstrap_slash==std::string::npos?std::string{}:bootstrap_path.substr(0,bootstrap_slash+1);
    Com<IDirect3DPixelShader9> cutout_ps;
    if(cutout_pair||cutout_bias||original_lane){
        const auto p=load((folder+"ps_63f96eba9eea7880.bin").c_str());
        require(fnv(p.data(),p.size()*4)==0x63f96eba9eea7880ull,"cutout pair original PS");
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&cutout_ps.p),"cutout pair PS");
    }
    Com<IDirect3DVertexShader9> emitter_vs;Com<IDirect3DPixelShader9> emitter_ps;
    Com<IDirect3DVertexDeclaration9> emitter_decl;Com<IDirect3DVertexBuffer9> emitter_vb;Com<IDirect3DIndexBuffer9> emitter_ib;
    Com<IDirect3DTexture9> emitter_texture;
    if(suncomposition){
        require(emission_fault&&emissions_enabled,"sun composition uses enabled real emission pass");
        const auto v=load((folder+"vs_089091aab2d5eb13.bin").c_str()),p=load((folder+"ps_8559522220507d5e.bin").c_str());
        require(fnv(v.data(),v.size()*4)==0x089091aab2d5eb13ull&&fnv(p.data(),p.size()*4)==0x8559522220507d5eull,"sun original unfaded additive pair");
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&emitter_vs.p),"sun emitter VS");
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&emitter_ps.p),"sun emitter PS");
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        api(d->CreateVertexDeclaration(elements,&emitter_decl.p),"sun emitter declaration");
        const float l=-1-1.f/W,r=1-1.f/W,t=1+1.f/H,bottom=-1+1.f/H;
        const float quad[]={l,t,.1f,0,0,r,t,.1f,1,0,l,bottom,.1f,0,1,r,bottom,.1f,1,1};
        api(d->CreateVertexBuffer(sizeof quad,0,0,D3DPOOL_MANAGED,&emitter_vb.p,nullptr),"sun emitter geometry");
        void* data=nullptr;api(emitter_vb->Lock(0,0,&data,0),"sun emitter lock");std::memcpy(data,quad,sizeof quad);api(emitter_vb->Unlock(),"sun emitter unlock");
        const unsigned short indices[]={0,1,2,3};
        api(d->CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&emitter_ib.p,nullptr),"sun emitter indices");
        api(emitter_ib->Lock(0,0,&data,0),"sun emitter index lock");std::memcpy(data,indices,sizeof indices);api(emitter_ib->Unlock(),"sun emitter index unlock");
        api(d->CreateTexture(1,1,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&emitter_texture.p,nullptr),"sun emitter texture");
        D3DLOCKED_RECT locked{};api(emitter_texture->LockRect(0,&locked,nullptr,0),"sun emitter texture lock");
        const float texel[]={.5f,.25f,.125f,.125f};for(unsigned c=0;c<4;++c)static_cast<unsigned short*>(locked.pBits)[c]=float_to_half(texel[c]);api(emitter_texture->UnlockRect(0),"sun emitter texture unlock");
    }
    // A previous motion key authorizes exactly one lookup per frame. Use a
    // second stable object scope for the late-fault/interleaved submission and
    // warm BOTH keys every frame; duplicate A would legitimately force a cut.
    if(late||suncomposition||shadow_apply){b.vb=a.vb;b.covers=covers_a;}
    if(shadow_apply)require(camera,"shadow_apply needs the rotating camera seam (X3M_FIXTURE_CAMERA=rotate)");
    // shadow_apply geometry: perspective rows placing the authored triangle at
    // view depth Z with device depth dz exactly (clip = (x Z, y Z, dz Z, Z)), so
    // the apply's AO-law linearization of RT2 and the replay's clip.w agree.
    auto draw_at=[&](Object& o,float Z,float dz,bool matched){
        scope(&o);
        api(d->SetStreamSource(0,o.vb,0,24),"apply stream");
        api(d->SetVertexShader(vs.p),"apply VS");api(d->SetPixelShader(ps.p),"apply PS");
        float m[16]{};m[0]=Z;m[5]=Z;m[11]=dz*Z;m[15]=Z;
        api(d->SetVertexShaderConstantF(24,m,4),"apply rows");
        const Snapshot before=snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1),"apply DrawPrimitive");++draw_index;
        compare(before,snapshot(),"apply draw");
        const bool jittered=enabled&&jitter&&!scene_rejected;
        records.push_back({&o,0,0,0,true,matched,o.rt,o.rp,o.rzo,false,jittered,true});
        std::printf("EXPECT frame=%llu index=%u object=%s routed=1 matched=%u jittered=%u\n",frame,draw_index,o.name,unsigned(matched),unsigned(jittered));
        o.recorded=true;o.rt=0;o.rp=0;o.rzo=0;
    };
    unsigned positive_frames=0,zero_frames=0,history_frames=0;
    bool previous_mask_valid=true;
    for(unsigned step=0;step<6;++step){
        // FrameClear is the real M initialization operation at the scene latch.
        if(missing&&step==2)emission_fault(d.p,9,1);
        if(lightmap&&(step==3||step==5)){
            const int state=hull_toggle(d.p,1); // Ctrl+Shift+F4: the light-map gain
            std::printf("SUN_LIGHTMAP_TOGGLE frame=%llu state=%d\n",frame,state);
            require(state==(step==3?0:1),"the shared F4 flag: off before frame 3, on before frame 5");
        }
        frame_begin();linear_material_inputs();write_reserved();
        // D0 points toward the authored +Z normal. D1/points/emission are zero;
        // a unit lightmap keeps L positive on the exact-zero D0 control frame.
        const float sun[4]={.5f,.5f,.5f,0},zero[4]{};
        api(d->SetPixelShaderConstantF(5,step==1?zero:sun,1),"sun live D0 radiance");
        // Reset cannot recreate a deliberately absent cached shader. Bind-only
        // failure has a complete cache and can requalify; creation failure stays
        // fail-closed until that cache is replaced or the device is destroyed.
        const bool missing_cache=!std::strcmp(mode,"late_shader")&&step>=4;
        const bool expected_lane=!fallback&&!(late&&step==3)&&!missing_cache&&!lane_off;
        // RT2 lanes: R32F (1) off the lane, A32B32G32R32F (4) on it (docs/architecture/shadow-receiver-depth.md; .b = .a = clip w).
        const unsigned lane_stride=expected_lane?4u:1u;
        if(!lane_off)require(emission_status(d.p,90)==unsigned(!refused&&!missing_cache),"sun exact capability qualification");
        require(emission_status(d.p,91)==unsigned(expected_lane),"sun format selected only at frame latch");
        const unsigned routed_before=emission_status(d.p,89),gate4_before=emission_status(d.p,99);
        if(xt_state){
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE),"xt alpha test on");api(d->SetRenderState(D3DRS_ALPHAREF,1),"xt alpha reference 1");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL),"xt alpha GREATEREQUAL");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"xt RT0 mask 7");
        }
        // cutout_pair_bias, frame 2: a full mip chain on an otherwise unused
        // stage makes the route's bias observable (the material textures are
        // single-level): the routed receiver leaves -0.5 on stage 6 and the
        // admitted cutout pair must draw with the native value back.
        DWORD stage6_filter=0;
        if(cutout_bias&&step==2){
            api(d->GetSamplerState(6,D3DSAMP_MIPFILTER,&stage6_filter),"stage 6 native mip filter");
            api(d->SetTexture(6,ramp.p),"ramp on stage 6");api(d->SetSamplerState(6,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR),"stage 6 mip linear");
        }
        if(shadow_apply){
            draw_at(b,16.f,.7f,frames_since_reset!=0); // the caster, behind the receiver and nearer to the light
            draw_at(a,12.f,.5f,frames_since_reset!=0); // the receiver (LESSEQUAL: it wins the depth test)
            if(step==0){
                // The lease proof (shadow-replay-gates.md): a READONLY Lock after
                // the draw refuses the frame's replay; no map, the quad skips.
                void* data=nullptr;api(a.vb->Lock(0,0,&data,D3DLOCK_READONLY),"READONLY Lock of the geometry after its draw");api(a.vb->Unlock(),"Unlock of the geometry");
            }
        } else draw(a,0,0,0,true,!lane_off,frames_since_reset!=0&&!lane_off,Alter::None,!(cutout_bias&&step==2));
        if(cutout_bias&&step==2)require(sampler_bias(6)==float_bits(mip_bias),"routed receiver applied the route's bias to the mip-chain stage");
        if(xt_state){
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"xt alpha test off");api(d->SetRenderState(D3DRS_ALPHAREF,0),"xt alpha reference default");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_ALWAYS),"xt alpha func default");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"xt RT0 mask restore");
            const unsigned routed=emission_status(d.p,89)-routed_before,gate4=emission_status(d.p,99)-gate4_before;
            require(routed==unsigned(!lane_off)&&gate4==unsigned(lane_off),"XT-state receiver routes with the lane on and is refused at gate 4 with it off");
            std::printf("SUN_XT_STATE frame=%llu test=1 ref=1 func=%u mask=7 lane=%u routed=%u gate4=%u\n",frame,unsigned(D3DCMP_GREATEREQUAL),unsigned(!lane_off),routed,gate4);
        }
        if((late||suncomposition)&&!(late&&step==2)&&!(suncomposition&&step==3))
            draw(b,0,0,0,true,true,frames_since_reset!=0);
        auto lane_read=[&](){
            std::vector<float> data(std::size_t(W)*H*lane_stride);unsigned w=0,h=0;
            api(emission_readback(d.p,2,data.data(),unsigned(data.size()),&w,&h),"sun actual RT2 readback");
            require(w==W&&h==H,"sun RT2 readback dimensions");return data;
        };
        auto lane=lane_read();
        unsigned drawn=0,positive=0,zeros=0;
        // Lane off: the receiver was refused, so RT2 holds no receiver depth.
        for(unsigned y=2;y+2<H&&!lane_off;++y)for(unsigned x=2;x+2<W;++x){
            const unsigned pixel=y*W+x;const float depth_value=lane[pixel*lane_stride];
            // Interior coverage is fixed by the authored full-screen triangle,
            // not inferred from clear values or the share being tested.
            require_quiet(depth_value==.5f,"sun receiver interior actually wrote exact depth");++drawn;
            if(expected_lane){const float share=lane[pixel*lane_stride+1];
                if(!share_refused)require_quiet(std::isfinite(share)&&share>=0&&share<=1,"sun receiver interior valid share");
                if(lane_stride==4){const float w_lane=lane[pixel*4+2];require_quiet(std::isfinite(w_lane)&&w_lane>0.f&&lane[pixel*4+3]==w_lane,"sun receiver interior clip w lane positive and paired with .a");}
                positive+=share>0;zeros+=share==0;
            }
        }
        require(lane_off||drawn==(W-4)*(H-4),"sun receiver coverage positive control");
        if(expected_lane){auto at=[&](unsigned x,unsigned y){return double(lane[(y*W+x)*lane_stride+1]);};
            std::printf("SUN_SHARE_STATS frame=%llu drawn=%u positive=%u zero=%u center=%.6g left=%.6g right=%.6g top=%.6g bottom=%.6g corner=%.6g\n",frame,drawn,positive,zeros,at(W/2,H/2),at(3,H/2),at(W-4,H/2),at(W/2,3),at(W/2,H-4),at(3,3));}
        if(expected_lane&&!share_refused){require(step==1?zeros==drawn:positive==drawn,"drawn positive and zero-sun controls");positive_frames+=positive>0;zero_frames+=zeros>0;}
        if(share_refused){
            require(emission_status(d.p,73)==0&&emission_status(d.p,74)>=1,"the share producer refused the reviewed pair, no variant created");
            require(emission_status(d.p,76)==1,"the refused pair's routed depth writer counted once this frame");
            require(emission_status(d.p,77)==1,"the refused pair kept its original-fill variant");
            std::printf("SUN_SHARE_REFUSED frame=%llu refused_programs=%u refused_draws=%u fill_draws=%u failed=%u\n",frame,emission_status(d.p,74),emission_status(d.p,76),emission_status(d.p,77),emission_status(d.p,95));
        }
        Com<IDirect3DPixelShader9> late_ps;
        if(late&&step==2){
            api(SetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",mode)?S_OK:E_FAIL,"arm late lane fault");
            if(!std::strcmp(mode,"late_shader")){
                api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(ps_words.data()),&late_ps.p),"late original PS create survives lane failure");
                std::swap(ps.p,late_ps.p);
            }
            draw(b,0,0,0,true,true,true);
            if(late_ps.p){std::swap(ps.p,late_ps.p);api(d->SetPixelShader(ps.p),"retire late original PS binding");late_ps.reset();}
            api(SetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",nullptr)?S_OK:E_FAIL,"disarm late fault");
            require(emission_status(d.p,95)==1&&emission_status(d.p,91)==1,"late failure vetoes without midframe format replacement");
            lane=lane_read();
        }
        if(untracked&&step==2){
            // An unreviewed PS drawn with z write ON at the receiver's depth
            // (LESSEQUAL): an actual depth writer the lane did not track.
            draw(a,0,0,0,false,false,false,Alter::Hdr2);
            unsigned w=0,h=0;const auto rgb=hdr_image(&w,&h);
            require(rgb[(std::size_t(H/2)*W+W/2)*4]==2,"untracked writer really replaced owning scene color");
            require(lane_read()==lane,"untracked depth write retains stale positive share bytes");
        }
        if(unregistered&&step==2){
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE),"unregistered alpha test on");api(d->SetRenderState(D3DRS_ALPHAREF,1),"unregistered alpha reference 1");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL),"unregistered alpha GREATEREQUAL");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"unregistered RT0 mask 7");
            if(stamp_fault)api(SetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",stamp_mid?"stamp_mid":"stamp")?S_OK:E_FAIL,"arm stamp fault");
            draw(b,0,0,-.25f,false,false,false,Alter::None,true,24,sm2_vs.p,sm2_ps.p); // depth .25: wins over the receiver
            draw(a,0,0,.25f,false,false,false,Alter::None,true,24,sm2_vs.p,sm2_ps.p);  // depth .75: loses everywhere
            draw(b,1.f,0,-.25f,false,false,false,Alter::None,true,24,sm3_vs.p,sm3_ps.p); // SM3, B moved right by 1 NDC: wins
            if(stamp_fault)api(SetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",nullptr)?S_OK:E_FAIL,"disarm stamp fault");
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"unregistered alpha test off");api(d->SetRenderState(D3DRS_ALPHAREF,0),"unregistered alpha reference default");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_ALWAYS),"unregistered alpha func default");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"unregistered RT0 mask restore");
            api(d->SetVertexShader(vs.p),"retire unregistered VS binding");api(d->SetPixelShader(ps.p),"retire unregistered PS binding");
            unsigned w=0,h=0;const auto rgb=hdr_image(&w,&h);const auto after=lane_read();
            require(rgb[(std::size_t(9)*W+9)*4]==3,"unregistered writer really replaced owning scene color inside B");
            require(rgb[(std::size_t(9)*W+41)*4]==5,"unregistered SM3 writer really replaced owning scene color inside moved B");
            require(rgb[(std::size_t(H/2)*W+W/2)*4]<3,"the losing full-screen draw changed no color");
            unsigned stamped_pixels=0,sign_pixels=0;
            for(unsigned pixel=0;pixel<W*H;++pixel){
                const bool sign=rgb[std::size_t(pixel)*4]==3||rgb[std::size_t(pixel)*4]==5;sign_pixels+=sign;
                const float* was=&lane[std::size_t(pixel)*lane_stride];const float* now=&after[std::size_t(pixel)*lane_stride];
                if(sign&&!stamp_fault){
                    // The pixels the unroutable draw won, and only those: share -1, every other lane byte kept.
                    require_quiet(now[1]==-1.f&&now[0]==was[0]&&now[2]==was[2]&&now[3]==was[3],"stamp wrote share -1 alone on a pixel the unroutable draw won");++stamped_pixels;
                } else require_quiet(!std::memcmp(was,now,sizeof(float)*lane_stride),"pixels the unroutable draw lost (or a refused stamp) keep their lane bytes");
            }
            require(sign_pixels>300&&stamped_pixels==(stamp_fault?0u:sign_pixels),"stamp coverage equals the unroutable draw's visible coverage");
            std::printf("SUN_UNREGISTERED frame=%llu sign_pixels=%u stamped_pixels=%u fault=%u mid=%u\n",frame,sign_pixels,stamped_pixels,unsigned(stamp_fault),unsigned(stamp_mid));
        }
        if(cutout_pair&&step==2){
            // Mask 7 with alpha test off on a cutout pair: neither the opaque
            // arm (mask 15) nor the exact cutout arm (alpha test on) admits it,
            // and the tested-opaque arm must not either.
            const unsigned refusals_before=emission_status(d.p,99);
            std::swap(ps.p,cutout_ps.p);
            api(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"cutout pair RT0 mask 7");
            draw(a,0,0,0,false,false,false);
            api(d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"cutout pair RT0 mask restore");
            std::swap(ps.p,cutout_ps.p);api(d->SetPixelShader(ps.p),"retire cutout pair binding");
            require(emission_status(d.p,99)==refusals_before+1,"cutout pair never takes the tested-opaque arm");
            require(lane_read()==lane,"refused cutout pair leaves receiver depth/share bytes");
            std::printf("SUN_CUTOUT_PAIR frame=%llu ps=63f96eba9eea7880 test=0 mask=7 gate4=1\n",frame);
        }
        if(cutout_bias&&step==2){
            // The exact cutout state under a nonzero mip bias: the exact arm is
            // unconfigured for the frame, so the tested-opaque arm must track
            // the pair (routed, mode 0: scope unverified) and nothing vetoes.
            const unsigned routed_before_c=emission_status(d.p,89),gate4_before_c=emission_status(d.p,99);
            std::swap(ps.p,cutout_ps.p);
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE),"cutout bias alpha test on");api(d->SetRenderState(D3DRS_ALPHAREF,1),"cutout bias alpha reference 1");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL),"cutout bias alpha GREATEREQUAL");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"cutout bias RT0 mask 7");
            draw(a,0,0,0,false,true,false,Alter::None,false);
            const DWORD bias_after=sampler_bias(6);
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"cutout bias alpha test off");api(d->SetRenderState(D3DRS_ALPHAREF,0),"cutout bias alpha reference default");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_ALWAYS),"cutout bias alpha func default");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"cutout bias RT0 mask restore");
            std::swap(ps.p,cutout_ps.p);api(d->SetPixelShader(ps.p),"retire cutout bias binding");
            const unsigned routed_c=emission_status(d.p,89)-routed_before_c,gate4_c=emission_status(d.p,99)-gate4_before_c;
            api(d->SetTexture(6,nullptr),"ramp off stage 6");api(d->SetSamplerState(6,D3DSAMP_MIPFILTER,stage6_filter),"stage 6 native mip filter restored");
            std::printf("SUN_CUTOUT_BIAS frame=%llu ps=63f96eba9eea7880 test=1 ref=1 mask=7 bias=%g routed=%u gate4=%u untracked=%u stage_bias=%08lx\n",frame,double(mip_bias),routed_c,gate4_c,emission_status(d.p,94),static_cast<unsigned long>(bias_after));
            require(routed_c==1&&gate4_c==0,"cutout pair under a nonzero mip bias takes the tested-opaque arm");
            require(bias_after==0,"admitted cutout pair drew with the native LOD bias on the mip-chain stage");
            require(emission_status(d.p,94)==0,"tracked cutout pair is not an untracked writer");
            // Admission telemetry (linear_material_frame cutout_opaque_*): the
            // admitted pair is one tested-opaque-arm draw whose lane share was
            // written (the lane variant with the share extraction was bound).
            require(emission_status(d.p,37)==1&&emission_status(d.p,39)==1&&emission_status(d.p,38)==0,
                    "admitted cutout pair counted as a tested-opaque-arm routed draw with the lane share written");
            // The same pair refused on the same frame: z write off is the first
            // check of the gate's chain, and a ZERO/ONE blend leaves the colour
            // of the destination alone, so only the refusal bucket moves.
            std::swap(ps.p,cutout_ps.p);
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE),"cutout refusal alpha test on");api(d->SetRenderState(D3DRS_ALPHAREF,1),"cutout refusal alpha reference 1");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL),"cutout refusal alpha GREATEREQUAL");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"cutout refusal RT0 mask 7");
            api(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"cutout refusal depth write off");
            api(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"cutout refusal blend on");
            api(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ZERO),"cutout refusal source zero");api(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE),"cutout refusal destination one");
            draw(a,0,0,0,false,false,false);
            api(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"cutout refusal blend off");
            api(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE),"cutout refusal source restore");api(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO),"cutout refusal destination restore");
            api(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE),"cutout refusal depth write restore");
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"cutout refusal alpha test off");api(d->SetRenderState(D3DRS_ALPHAREF,0),"cutout refusal alpha reference default");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_ALWAYS),"cutout refusal alpha func default");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"cutout refusal RT0 mask restore");
            std::swap(ps.p,cutout_ps.p);api(d->SetPixelShader(ps.p),"retire cutout refusal binding");
            std::printf("SUN_CUTOUT_OPAQUE frame=%llu routed=%u lane=%u refused=%u no_zwrite=%u state=%u untracked=%u\n",
                frame,emission_status(d.p,37),emission_status(d.p,39),emission_status(d.p,38),emission_status(d.p,405),emission_status(d.p,407),emission_status(d.p,94));
            require(emission_status(d.p,38)==1&&emission_status(d.p,405)==1&&emission_status(d.p,37)==1,
                    "refused cutout pair counted once with its no_zwrite reason");
            require(emission_status(d.p,94)==0,"a colour-only refusal never vetoes the lane");
            lane=lane_read();
            for(unsigned y=2;y+2<H;++y)for(unsigned x=2;x+2<W;++x)require_quiet(lane[(y*W+x)*lane_stride]==.5f,"tracked cutout pair rewrote the interior depth exactly");
        }
        if(original_lane&&step==2){
            // The cutout pair in its exact cutout state on original shading:
            // no exact arm exists without linear materials, so the tested-opaque
            // arm admits it (routed, no gate-4 refusal, its own share written).
            require(emission_status(d.p,73)>=2&&emission_status(d.p,74)==0,"original share variants created for both registered originals, none refused");
            const unsigned routed_before_o=emission_status(d.p,89),gate4_before_o=emission_status(d.p,99);
            std::swap(ps.p,cutout_ps.p);
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE),"original cutout alpha test on");api(d->SetRenderState(D3DRS_ALPHAREF,1),"original cutout alpha reference 1");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL),"original cutout alpha GREATEREQUAL");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"original cutout RT0 mask 7");
            draw(a,0,0,0,false,true,false);
            api(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"original cutout alpha test off");api(d->SetRenderState(D3DRS_ALPHAREF,0),"original cutout alpha reference default");
            api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_ALWAYS),"original cutout alpha func default");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"original cutout RT0 mask restore");
            std::swap(ps.p,cutout_ps.p);api(d->SetPixelShader(ps.p),"retire original cutout binding");
            const unsigned routed_o=emission_status(d.p,89)-routed_before_o,gate4_o=emission_status(d.p,99)-gate4_before_o;
            std::printf("SUN_ORIGINAL_CUTOUT frame=%llu ps=63f96eba9eea7880 test=1 ref=1 mask=7 routed=%u gate4=%u opaque_routed=%u opaque_lane=%u opaque_refused=%u untracked=%u variants=%u refused=%u\n",
                frame,routed_o,gate4_o,emission_status(d.p,37),emission_status(d.p,39),emission_status(d.p,38),emission_status(d.p,94),emission_status(d.p,73),emission_status(d.p,74));
            require(routed_o==1&&gate4_o==0,"cutout pair admitted through the tested-opaque arm on original shading");
            require(emission_status(d.p,37)==1&&emission_status(d.p,39)==1&&emission_status(d.p,38)==0,"admitted cutout pair counted with its original share written");
            require(emission_status(d.p,94)==0,"tracked cutout pair is not an untracked writer");
            lane=lane_read();
            for(unsigned y=2;y+2<H;++y)for(unsigned x=2;x+2<W;++x){const unsigned pixel=y*W+x;require_quiet(lane[pixel*lane_stride]==.5f,"admitted cutout pair rewrote the interior depth exactly");
                const float share=lane[pixel*lane_stride+1];require_quiet(std::isfinite(share)&&share>=0&&share<=1,"cutout pair original share valid");}
        }
        if(effects&&step==2){
            // Additive blend, z test on, z write off: color-only over the
            // receiver, so the tracked depth is intact and nothing vetoes.
            api(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"effects depth write off");
            draw(a,0,0,0,false,false,false,Alter::Hdr8Additive);
            api(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE),"restore effects depth state");
            unsigned w=0,h=0;const auto rgb=hdr_image(&w,&h);
            require(rgb[(std::size_t(H/2)*W+W/2)*4]>=8,"effects draw really added to owning scene color");
            require(lane_read()==lane,"non-depth effects draw leaves receiver depth/share bytes");
            std::printf("SUN_EFFECTS frame=%llu depth_write=0 blend=1\n",frame);
        }
        const bool bad_mask=(missing||failed_m)&&step==2;
        unsigned mask_pixels=0,eligible_pixels=0,composition_draws=0;
        bool mask_valid=true;
        std::vector<float> mask(std::size_t(W)*H*4,0);
        if(suncomposition){
            const auto original_lane=lane;
            auto scene_color=[&](){unsigned width=0,height=0;auto pixels=hdr_image(&width,&height);require(width==W&&height==H,"sun composition owning color dimensions");return pixels;};
            const auto material_color=scene_color();
            std::vector<unsigned char> expected_mask(std::size_t(W)*H,0);
            auto emit=[&](bool right,unsigned fault){
                scope(nullptr);scene_states();
                api(d->SetVertexShader(emitter_vs.p),"sun composition original VS");api(d->SetPixelShader(emitter_ps.p),"sun composition original PS");
                api(d->SetVertexDeclaration(emitter_decl.p),"sun composition layout");api(d->SetStreamSource(0,emitter_vb.p,0,20),"sun composition stream");
                api(d->SetIndices(emitter_ib.p),"sun composition indexed geometry");
                api(d->SetVertexShaderConstantF(0,identity,4),"sun composition WVP");
                const float uv[]={1,0,0,0,0,1,0,0};api(d->SetVertexShaderConstantF(4,uv,2),"sun composition UV");
                api(d->SetPixelShaderConstantF(0,identity,3),"sun composition affine identity");
                for(unsigned sampler=0;sampler<4;++sampler)api(d->SetTexture(sampler,sampler?nullptr:emitter_texture.p),"sun composition texture binding");
                api(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE),"sun composition data sample");
                api(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"sun composition depth read only");
                api(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"sun composition blend");
                api(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE),"sun composition source one");api(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE),"sun composition destination one");api(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"sun composition add");
                const RECT rect{LONG(right?W/2:W/8),LONG(H/4),LONG(right?7*W/8:W/2),LONG(3*H/4)};
                api(d->SetScissorRect(&rect),"sun composition rectangle");api(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"sun composition scissor");
                const auto before_color=scene_color();const auto state=snapshot();
                const auto prior_linear=emission_status(d.p,5),prior_exchange=emission_status(d.p,10);
                if(fault)emission_fault(d.p,fault,1);
                api(d->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP,0,0,4,0,2),"sun real composition source");++draw_index;++composition_draws;
                compare(state,snapshot(),"sun composition source");
                const auto after_color=scene_color();const auto center=(std::size_t(H/2)*W+(right?5*W/8:W/4))*4;
                require(after_color[center]>before_color[center],"sun composition really changed owning color");
                std::printf("SUN_COMPOSE_SOURCE frame=%llu indexed=1 linear=%u exchanged=%u incomplete=%u stopped=%u\n",frame,emission_status(d.p,5),emission_status(d.p,10),emission_status(d.p,7),emission_status(d.p,17));
                if(!bad_mask)require(emission_status(d.p,5)==prior_linear+1&&emission_status(d.p,10)==prior_exchange+1,"sun composition completed real exchange publication");
                for(LONG y=rect.top;y<rect.bottom;++y)for(LONG x=rect.left;x<rect.right;++x)expected_mask[std::size_t(y)*W+x]=1;
                require(lane_read()==original_lane,"composition leaves earlier receiver depth/share bytes");
            };
            if(step==1)emit(false,0);
            if(step==2){if(!missing)emit(false,0);emit(true,failed_m?7u:0u);}
            if(step==3){
                emit(false,0);
                scene_states();material_state();linear_material_inputs();api(d->SetPixelShaderConstantF(5,sun,1),"interleaved D0");
                api(d->SetVertexDeclaration(declaration.p),"interleaved material layout");draw(b,0,0,0,true,true,true);
                const auto interleaved=scene_color();const auto center=(std::size_t(H/2)*W+W/4)*4;
                require(!std::memcmp(&interleaved[center],&material_color[center],3*sizeof(float)),"opaque draw interleaves between actual owning exchanges");
                emit(true,0);
            }
            if(step==5)emit(true,0);
            mask_valid=emission_status(d.p,1)!=0;
            require(mask_valid==!bad_mask,"actual M clear/finish failure invalidates coverage");
            unsigned mw=0,mh=0;api(emission_readback(d.p,3,mask.data(),unsigned(mask.size()),&mw,&mh),"sun actual composition M readback");
            require(mw==W&&mh==H,"sun M same target dimensions");
            for(unsigned pixel=0;pixel<W*H;++pixel){
                if(mask_valid){for(unsigned c=0;c<3;++c)require_quiet(mask[4*pixel+c]==expected_mask[pixel],"same-frame persistent M union and clear");mask_pixels+=expected_mask[pixel];}
                const unsigned x=pixel%W,y=pixel/W;
                if(mask_valid&&x>=2&&x+2<W&&y>=2&&y+2<H&&mask[4*pixel]==0)++eligible_pixels;
            }
            if(mask_valid){require(mask_pixels==(step==0||step==4?0u:step==1||step==5?768u:1536u),"actual excluded union pixel count");reference.upload_reactive(mask);}
            else require(emission_status(d.p,17)==1,"failed coverage stops actual composition frame");
            // Restore ordinary caller input layout before the next frame/Reset.
            scene_states();material_state();api(d->SetVertexDeclaration(declaration.p),"sun ordinary layout after composition");
            std::printf("SUN_M frame=%llu draws=%u valid=%u required=%u excluded=%u eligible=%u linear=%u exchanged=%u incomplete=%u stopped=%u interleaved=%u\n",frame,composition_draws,unsigned(mask_valid),emission_status(d.p,96),mask_pixels,eligible_pixels,emission_status(d.p,5),emission_status(d.p,10),emission_status(d.p,7),emission_status(d.p,17),unsigned(step==3));
        }
        // Resolve the real captured input through the existing independent
        // R32F Reference; the owner runner compares every FP16 output byte.
        std::vector<float> motion(std::size_t(W)*H*4),depth_values(std::size_t(W)*H);unsigned w=0,h=0;
        api(readback(d.p,motion.data(),unsigned(motion.size()),&w,&h),"sun live motion input");
        for(std::size_t p=0;p<depth_values.size();++p)depth_values[p]=lane[p*lane_stride];
        reference.upload(std::vector<DWORD>(depth_values.size()),motion,depth_values);
        const float k=hdr_reference_input();decide();
        // shadow_apply: the CPU reference of the shadowed scene. The caster at
        // view depth 16 is nearer to the +Z light than the receiver at 12 and
        // its footprint covers the whole receiver (4/3 of it in NDC, the sun
        // within 7 degrees of the view axis), so every receiver pixel with a
        // share is shadowed by all nine taps: C (1 - s) (f = 0, exponent 1).
        // The mask file names, per pixel, 0 exact / 1 within one FP16 code
        // (the GPU's pow/multiply rounding and the history of an earlier
        // shadowed frame) / 2 excluded (unused here), for the runner.
        const bool apply_frame=shadow_apply&&step!=0&&step!=2;
        unsigned apply_inner=0,apply_band=0,apply_changed=0;
        if(shadow_apply){
            unsigned w=0,h=0;auto image=hdr_image(&w,&h);require(w==W&&h==H,"apply colour dimensions");
            std::vector<unsigned char> apply_mask(std::size_t(W)*H,step>=3?1:0);
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                const std::size_t pixel=std::size_t(y)*W+x;
                const float share=expected_lane?lane[pixel*lane_stride+1]:0.f,depth_value=lane[pixel*lane_stride];
                if(!(apply_frame&&depth_value>=0&&share>0))continue;
                ++apply_inner;
                const float factor=1.f-std::min(share,1.f);
                bool changed=false;
                for(unsigned c=0;c<3;++c){const float shaded=image[pixel*4+c]*factor;changed|=float_to_half(shaded)!=float_to_half(image[pixel*4+c]);image[pixel*4+c]=shaded;}
                apply_changed+=changed;
            }
            reference.upload16(image);
            char mask_name[64];std::snprintf(mask_name,sizeof mask_name,"apply_mask_%llu.u8",frame);
            FILE* mask_file=std::fopen(mask_name,"wb");require(mask_file!=nullptr,"apply mask file");
            const auto mask_written=std::fwrite(apply_mask.data(),1,apply_mask.size(),mask_file);std::fclose(mask_file);require(mask_written==apply_mask.size(),"apply mask complete");
            if(step>=3)require(apply_inner>=400&&apply_changed>=400,"the shadowed footprint really darkens the reference by at least one FP16 code");
            if(step==1)require(apply_changed==0,"the zero-sun frame's shares leave the reference unchanged");
        }
        api(d->SetDepthStencilSurface(nullptr),"sun scene-end detach depth");
        const auto before=snapshot();
        api(d->StretchRect(back.p,nullptr,bloom_surface.p,nullptr,D3DTEXF_NONE),"sun scene-end publication and TAA");
        compare(before,snapshot(),"sun boundary");
        if(lightmap){
            // The gained share variant bound on every lane frame with the flag on
            // (frame 2 adds the cutout pair), none on the toggled-off frames 3
            // and 4 (the Reset between them keeps the flag).
            const unsigned gained_draws=emission_status(d.p,78),gained_variants=emission_status(d.p,79);
            std::printf("SUN_LIGHTMAP frame=%llu step=%u gained_draws=%u gained_variants=%u share_variants=%u\n",frame,step,gained_draws,gained_variants,emission_status(d.p,73));
            require(gained_variants>=2&&gained_variants==emission_status(d.p,73),"a gained share variant beside every share variant");
            require(gained_draws==((step==3||step==4)?0u:step==2?2u:1u),"the gained share variant bound exactly on the flag-on lane draws");
        }
        const bool available=expected_lane&&!(step==2&&(late||untracked||cutout_pair||stamp_fault))&&!bad_mask&&!share_refused;
        require(emission_status(d.p,92)==unsigned(available),"sun publication follows actual writers and faults");
        require(emission_status(d.p,97)==1,"sun unavailable frame still resolves TAA");
        if(shadow_apply){
            const unsigned applied=emission_status(d.p,70),attempted=emission_status(d.p,71),replayed=emission_status(d.p,72);
            std::printf("SUN_APPLY frame=%llu step=%u applied=%u attempted=%u replayed=%u expect_applied=%u inner=%u band=%u changed=%u\n",frame,step,applied,attempted,replayed,unsigned(apply_frame),apply_inner,apply_band,apply_changed);
            require(attempted==1,"the apply gate ran at this scene end");
            require((replayed>0)==(step!=0),"the depth replay produced this frame's map on every frame with a sun");
            require(applied==unsigned(apply_frame),"the apply quad drew exactly on the frames with the lane available, the map and the owner");
        }
        std::vector<DWORD> image;std::vector<unsigned char> expected;
        const auto policy=suncomposition?(mask_valid?x3m::renderer::ReactivePolicy::SupplementalMaskWithDepthSentinel:x3m::renderer::ReactivePolicy::Unavailable):x3m::renderer::ReactivePolicy::DerivedFromDepthSentinel;
        const auto output=reference.run(jx,jy,pjx,pjy,expected_cut(),decision.matrix,decision.policy==2,true,k,image,expected,policy);
        std::printf("SUN_HISTORY frame=%llu expected=%u reference=%u actual=%u fixture_cut=%u\n",frame,unsigned(frames_since_reset!=0&&mask_valid&&previous_mask_valid),unsigned(output.used_history),emission_status(d.p,98),unsigned(expected_cut()));
        require(output.used_history==(frames_since_reset!=0&&mask_valid&&previous_mask_valid)&&emission_status(d.p,98)==unsigned(output.used_history),"sun fallback preserves TAA history");
        history_frames+=output.used_history;previous_mask_valid=mask_valid;
        char filename[64];std::snprintf(filename,sizeof filename,"reference_taa_%llu.rgba16f",frame);
        FILE* file=std::fopen(filename,"wb");require(file!=nullptr,"sun reference file");
        const auto written=std::fwrite(expected.data(),1,expected.size(),file);std::fclose(file);require(written==expected.size(),"sun reference complete");
        std::printf("SUN_LIVE frame=%llu step=%u lane=%u available=%u drawn=%u positive=%u zero=%u fault=%u history=%u\n",frame,step,expected_lane,available,drawn,positive,zeros,unsigned(late&&step==2),unsigned(output.used_history));
        api(d->EndScene(),"sun live EndScene");
        api(d->SetDepthStencilSurface(depth.p),"sun live depth rebind");
        api(d->Present(nullptr,nullptr,nullptr,nullptr),"sun live Present");
        ++frame;++frames_since_reset;camera_history=camera_current;
        if(step==3)reset();
    }
    if(suncomposition)api(d->SetIndices(nullptr),"release sun emitter index binding");
    require(history_frames==((missing||failed_m)?2u:4u),"sun histories follow only actual coverage failure and Reset");
    std::printf("SUN_LIVE_PASS frames=6 positive_frames=%u zero_frames=%u histories=%u resets=1\n",positive_frames,zero_frames,history_frames);
}
