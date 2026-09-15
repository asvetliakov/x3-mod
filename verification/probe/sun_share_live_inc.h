// Included in the actual D3D Fixture. Uses MotionOutput's existing native seam,
// authored original material pair and scene selector; no mocked lane producer.
void run_sun_lane(const char* bootstrap_vertex) {
    require(seam&&enabled&&taa&&hdr&&hdr_agx&&reference_ready&&emission_status&&emission_readback,
            "sun live needs the HDR/TAA native seam and reference");
    char mode[24]{};GetEnvironmentVariableA("X3M_FIXTURE_SUN_LIVE_CASE",mode,sizeof mode);
    const bool late=!std::strcmp(mode,"late_shader")||!std::strcmp(mode,"bind");
    const bool untracked=!std::strcmp(mode,"untracked");
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
    const std::string bootstrap_path(bootstrap_vertex);const auto bootstrap_slash=bootstrap_path.find_last_of("/\\");
    const auto folder=bootstrap_slash==std::string::npos?std::string{}:bootstrap_path.substr(0,bootstrap_slash+1);
    Com<IDirect3DPixelShader9> cutout_ps;
    if(cutout_pair||cutout_bias){
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
    if(late||suncomposition){b.vb=a.vb;b.covers=covers_a;}
    unsigned positive_frames=0,zero_frames=0,history_frames=0;
    bool previous_mask_valid=true;
    for(unsigned step=0;step<6;++step){
        // FrameClear is the real M initialization operation at the scene latch.
        if(missing&&step==2)emission_fault(d.p,9,1);
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
        draw(a,0,0,0,true,!lane_off,frames_since_reset!=0&&!lane_off,Alter::None,!(cutout_bias&&step==2));
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
            std::vector<float> data(std::size_t(W)*H*(expected_lane?2:1));unsigned w=0,h=0;
            api(emission_readback(d.p,2,data.data(),unsigned(data.size()),&w,&h),"sun actual RT2 readback");
            require(w==W&&h==H,"sun RT2 readback dimensions");return data;
        };
        auto lane=lane_read();
        unsigned drawn=0,positive=0,zeros=0;
        // Lane off: the receiver was refused, so RT2 holds no receiver depth.
        for(unsigned y=2;y+2<H&&!lane_off;++y)for(unsigned x=2;x+2<W;++x){
            const unsigned pixel=y*W+x;const float depth_value=lane[pixel*(expected_lane?2:1)];
            // Interior coverage is fixed by the authored full-screen triangle,
            // not inferred from clear values or the share being tested.
            require_quiet(depth_value==.5f,"sun receiver interior actually wrote exact depth");++drawn;
            if(expected_lane){const float share=lane[2*pixel+1];
                require_quiet(std::isfinite(share)&&share>=0&&share<=1,"sun receiver interior valid share");
                positive+=share>0;zeros+=share==0;
            }
        }
        require(lane_off||drawn==(W-4)*(H-4),"sun receiver coverage positive control");
        if(expected_lane){require(step==1?zeros==drawn:positive==drawn,"drawn positive and zero-sun controls");positive_frames+=positive>0;zero_frames+=zeros>0;}
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
            lane=lane_read();
            for(unsigned y=2;y+2<H;++y)for(unsigned x=2;x+2<W;++x)require_quiet(lane[(y*W+x)*2]==.5f,"tracked cutout pair rewrote the interior depth exactly");
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
        for(std::size_t p=0;p<depth_values.size();++p)depth_values[p]=lane[p*(expected_lane?2:1)];
        reference.upload(std::vector<DWORD>(depth_values.size()),motion,depth_values);
        const float k=hdr_reference_input();decide();
        api(d->SetDepthStencilSurface(nullptr),"sun scene-end detach depth");
        const auto before=snapshot();
        api(d->StretchRect(back.p,nullptr,bloom_surface.p,nullptr,D3DTEXF_NONE),"sun scene-end publication and TAA");
        compare(before,snapshot(),"sun boundary");
        const bool available=expected_lane&&!(step==2&&(late||untracked||cutout_pair))&&!bad_mask;
        require(emission_status(d.p,92)==unsigned(available),"sun publication follows actual writers and faults");
        require(emission_status(d.p,97)==1,"sun unavailable frame still resolves TAA");
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
