// Fade-band motion arm live witness (docs/architecture/linear-distance-fade-region.md,
// "Fade-band route"; docs/reverse-engineering/asteroid-fog-temporal.md, run 49).
// Every frame draws the routed full-screen object A, then two quads of the exact
// fade pair b0602757fce6e870/517540ae6d5e5410 in the engine's fade-band state
// (Z test on, Z-write off, SRCALPHA/INVSRCALPHA ADD, RGB mask 7): P (top left,
// constant texels, the live fade fixture's pair-0 inputs, for the composite
// oracle at two sample pixels) and Q (top right, a diffuse ramp in u and v, for
// the runner's per-frame Lucas-Kanade shift of the raw and the resolved image).
// X3M_FIXTURE_FADE_SCRIPT=routed: g_AlphaValue 1, g_FogClip (1, 0), diffuse
// alpha 1 -> fraction 1000 permille -> the arm routes both quads (own RT1 rows,
// RT2 masked, no bracket, M clear). masked: the live fixture's .625 / (.75, .125)
// with fog on -> 390 permille on the CPU (below the default 500) -> the fade
// bracket composes them as before and M covers them. sentinel: the routed
// inputs with A scissored to the bottom half (rows 32..63: the scene still
// has its depth writer, which the selector's Scene phase requires), so both
// quads sit over the route's sentinel fill (no routed opaque draw beneath)
// under X3M_TAA_SENTINEL=2 (the run-49 configuration: far-plane reprojection
// of the fill plus the quads' own RT1 rows at alpha 1). hover: the
// diffuse-alpha-.5 inputs with P's g_AlphaValue per frame (permille 507,
// 449, 449, 390, 449, 449, 507, 449, 390, 507, 449, 449): the arm's
// hysteresis keeps a node admitted at >= 500 down to 400 (held), a frame
// below 400 disarms it, and 449 stays refused until 507 arms it again. Q
// follows the same estimate through g_FogClip = (permille/1000 - 1, -1) at
// g_AlphaValue 1 and camera distance 4, where the shader's factor saturates
// at 1: its raster keeps a constant alpha (the shift witness needs a constant
// amplitude) while the CPU estimate at the origin distance 1 hovers.
// Twelve frames across the eight jitter phases with the rotating camera (cut
// at frame 7); the raw FP16 scene after the quads, RT1, M and the presented
// frame are dumped per frame.
void run_fade_route_integration(Fixture& f,const char* original_path) {
    require(f.seam&&f.enabled&&f.taa&&f.hdr&&f.hdr_agx&&f.camera&&f.emission_readback&&f.emission_status,"fade route HDR TAA seam with camera");
    require(f.reference_ready,"fade route reference resolve device");
    const std::string supplied(original_path);const auto slash=supplied.find_last_of("/\\");
    require(slash!=std::string::npos,"fade route original directory");
    char script_setting[16]{};GetEnvironmentVariableA("X3M_FIXTURE_FADE_SCRIPT",script_setting,sizeof script_setting);
    const bool routed_script=std::strcmp(script_setting,"routed")==0,masked_script=std::strcmp(script_setting,"masked")==0,
               sentinel_script=std::strcmp(script_setting,"sentinel")==0,hover_script=std::strcmp(script_setting,"hover")==0;
    require(routed_script||masked_script||sentinel_script||hover_script,"X3M_FIXTURE_FADE_SCRIPT=routed|masked|sentinel|hover");
    constexpr unsigned frames=12;
    // hover: g_AlphaValue.x per frame (binary fractions: the fraction .625 * alpha is exact in float) and the arm's decision.
    constexpr float hover_alpha[frames]={.8125f,.71875f,.71875f,.625f,.71875f,.71875f,.8125f,.71875f,.625f,.8125f,.71875f,.71875f};
    constexpr float hover_fog_x[frames]={-.4921875f,-.55078125f,-.55078125f,-.609375f,-.55078125f,-.55078125f,-.4921875f,-.55078125f,-.609375f,-.4921875f,-.55078125f,-.55078125f}; // Q: fraction 1 * (fog_x + 1)
    constexpr bool hover_routed[frames]={1,1,1,0,0,0,1,1,0,1,1,1},hover_held[frames]={0,1,1,0,0,0,0,1,0,0,1,1};
    const bool full_alpha=routed_script||sentinel_script; // g_AlphaValue 1, g_FogClip (1, 0), diffuse alpha 1: fraction 1000
    const auto routed_frame=[&](unsigned plan){return hover_script?hover_routed[plan]:full_alpha;};
    const auto held_frame=[&](unsigned plan){return hover_script&&hover_held[plan];};
    const auto alpha_value=[&](unsigned plan){return hover_script?hover_alpha[plan]:full_alpha?1.f:.625f;};
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> ps;
    {
        auto v=load((supplied.substr(0,slash+1)+"vs_b0602757fce6e870.bin").c_str());auto p=load((supplied.substr(0,slash+1)+"ps_517540ae6d5e5410.bin").c_str());
        require(fnv(v.data(),v.size()*4)==0xb0602757fce6e870ull&&fnv(p.data(),p.size()*4)==0x517540ae6d5e5410ull,"fade route exact originals");
        api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&vs.p),"fade route original VS");
        api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&ps.p),"fade route original PS");
    }
    // Two quads, clip z .3 (in front of A), the live fade fixture's vertex
    // layout. P: x -.8..-.2, y .2...8 (pixels 6.4..25.6); Q: x .2...8, same rows.
    struct SourceVertex {float p[3],uv[2],n[3],b[3],t[3];};
    const auto quad=[](float l,float r,float t,float b,SourceVertex out[4]) {
        const SourceVertex v[4]={{{l,t,.3f},{0,0},{0,0,1},{0,1,0},{1,0,0}},{{r,t,.3f},{1,0},{0,0,1},{0,1,0},{1,0,0}},{{l,b,.3f},{0,1},{0,0,1},{0,1,0},{1,0,0}},{{r,b,.3f},{1,1},{0,0,1},{0,1,0},{1,0,0}}};
        std::memcpy(out,v,sizeof v);
    };
    SourceVertex quads[2][4];quad(-.8f,-.2f,.8f,.2f,quads[0]);quad(.2f,.8f,.8f,.2f,quads[1]);
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,20,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},{0,44,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},D3DDECL_END()};
    Com<IDirect3DVertexDeclaration9> declaration;Com<IDirect3DVertexBuffer9> vertices[2];Com<IDirect3DIndexBuffer9> indices;
    api(f.d->CreateVertexDeclaration(elements,&declaration.p),"fade route declaration");
    void* data=nullptr;
    for(unsigned i=0;i<2;++i){api(f.d->CreateVertexBuffer(sizeof quads[i],0,0,D3DPOOL_MANAGED,&vertices[i].p,nullptr),"fade route vertices");api(vertices[i]->Lock(0,0,&data,0),"fade route vertex lock");std::memcpy(data,quads[i],sizeof quads[i]);api(vertices[i]->Unlock(),"fade route vertex unlock");}
    const unsigned short triangles[]={0,1,2,2,1,3};
    api(f.d->CreateIndexBuffer(sizeof triangles,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr),"fade route indices");
    api(indices->Lock(0,0,&data,0),"fade route index lock");std::memcpy(data,triangles,sizeof triangles);api(indices->Unlock(),"fade route index unlock");
    // Textures: P's constant texels are run_linear_distance_fade.cases()[0]
    // (diffuse alpha 1 in the routed script: the routed composite is the
    // source itself); Q's diffuse is a 16x16 ramp (r = u, g = v, b .5, alpha
    // 1: Q is the shift witness, not the oracle, and needs contrast over A).
    const float diffuse_alpha=full_alpha?1.f:.5f;
    const float texels[][4]={{.5f,.25f,.75f,diffuse_alpha},{.25f,.875f,.125f,.75f},{.125f,.25f,.0625f,.25f}};
    Com<IDirect3DTexture9> textures[3],ramp;
    for(unsigned i=0;i<3;++i) {
        api(f.d->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&textures[i].p,nullptr),"fade route texture");
        D3DLOCKED_RECT lock{};api(textures[i]->LockRect(0,&lock,nullptr,0),"fade route texture lock");std::memcpy(lock.pBits,texels[i],16);api(textures[i]->UnlockRect(0),"fade route texture unlock");
    }
    {
        api(f.d->CreateTexture(16,16,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&ramp.p,nullptr),"fade route ramp");
        D3DLOCKED_RECT lock{};api(ramp->LockRect(0,&lock,nullptr,0),"fade route ramp lock");
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const float value[4]={float(x)/15.f,float(y)/15.f,.5f,1.f};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,value,16);}
        api(ramp->UnlockRect(0),"fade route ramp unlock");
    }
    // The fade pair's native inputs (motion_output_distance_fade_inc.h pair 0):
    // clip rows c24-27 identity, normal rows c31-33, camera c36.w = 4, uv
    // rows c37/38 (oT0 = ((u, v, 1) . c37.xyz, (u, v, 1) . c38.xyz): P keeps
    // the live fixture's constant offset, Q the identity rows so the ramp
    // spans the quad), g_AlphaValue c39, emissive c40, g_FogClip c41, point
    // light c0-2, b0 fog on, i0 one point light. Q's camera sits at the
    // origin (c36.w 0) so its shader alpha in the masked script is ~.4 rather
    // than P's .078 (distance 4): the CPU estimate uses the clip rows, not
    // the camera constant, and stays 390 permille for both quads.
    const auto bind=[&](unsigned which,unsigned plan) {
        f.scene_states();
        for(auto s:{D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_DITHERENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_ALPHATESTENABLE})api(f.d->SetRenderState(s,FALSE),"fade route off state");
        api(f.d->SetRenderState(D3DRS_ZENABLE,TRUE),"fade route depth test");api(f.d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"fade route no depth write");api(f.d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL),"fade route LE depth");
        api(f.d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"fade route both faces");api(f.d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID),"fade route solid");
        api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"fade route blend");api(f.d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA),"fade route source alpha");api(f.d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA),"fade route inverse source alpha");api(f.d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"fade route ADD");
        api(f.d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"fade route RGB mask");
        const RECT whole{0,0,LONG(f.W),LONG(f.H)};api(f.d->SetScissorRect(&whole),"fade route scissor");api(f.d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"fade route scissor enabled");
        for(unsigned i=0;i<16;++i)api(f.d->SetRenderState(D3DRENDERSTATETYPE((i<8?D3DRS_WRAP0:D3DRS_WRAP8)+i%8),0),"fade route WRAP inputs");
        api(f.d->SetVertexDeclaration(declaration.p),"fade route declaration bind");api(f.d->SetStreamSource(0,vertices[which].p,0,sizeof(SourceVertex)),"fade route stream");api(f.d->SetStreamSourceFreq(0,1),"fade route frequency");api(f.d->SetIndices(indices.p),"fade route index binding");
        api(f.d->SetVertexShader(vs.p),"fade route VS bind");api(f.d->SetPixelShader(ps.p),"fade route PS bind");
        float vc[48][4]{},pc[12][4]{};
        for(unsigned i=0;i<4;++i)vc[24+i][i]=1;
        for(unsigned i=0;i<3;++i)vc[31+i][i]=1;
        const bool hover_q=hover_script&&which==1;
        if(which){vc[37][0]=1;vc[38][1]=1;if(hover_q)vc[36][3]=4;}else{vc[36][3]=4;vc[37][2]=.0625f;vc[38][2]=.1875f;}
        vc[39][0]=hover_q?1.f:alpha_value(plan);vc[40][0]=.25f;vc[40][1]=.125f;vc[40][2]=.0625f;
        vc[41][0]=hover_q?hover_fog_x[plan]:full_alpha?1.f:.75f;vc[41][1]=hover_q?-1.f:full_alpha?0.f:.125f;
        vc[0][2]=2;vc[1][0]=.5f;vc[1][1]=.25f;vc[1][2]=.125f;vc[2][0]=2;vc[2][1]=.25f;vc[2][2]=.125f;
        pc[0][2]=1;pc[1][0]=.375f;pc[1][1]=.25f;pc[1][2]=.5f;pc[2][2]=-1;pc[3][0]=.125f;pc[3][1]=.5f;pc[3][2]=.25f;pc[4][0]=.5f;pc[5][0]=1;
        api(f.d->SetVertexShaderConstantF(0,vc[0],48),"fade route VS inputs");api(f.d->SetPixelShaderConstantF(0,pc[0],12),"fade route PS inputs");
        const int count[4]={1,0,1,0};const BOOL fog=TRUE;api(f.d->SetVertexShaderConstantI(0,count,1),"fade route point count");api(f.d->SetVertexShaderConstantB(0,&fog,1),"fade route fog");
        for(unsigned stage=0;stage<7;++stage) {
            IDirect3DBaseTexture9* texture=stage==0?(which?ramp.p:textures[0].p):stage==1?textures[1].p:stage==2?textures[2].p:nullptr;
            api(f.d->SetTexture(stage,texture),"fade route sampler role");
            for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(f.d->SetSamplerState(stage,filter,which&&stage==0?D3DTEXF_LINEAR:D3DTEXF_POINT),"fade route sampling");
            api(f.d->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"fade route no mip");api(f.d->SetSamplerState(stage,D3DSAMP_SRGBTEXTURE,FALSE),"fade route numeric sampler");
            api(f.d->SetSamplerState(stage,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"fade route address u");api(f.d->SetSamplerState(stage,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"fade route address v");
            const float bias=0;DWORD bits;std::memcpy(&bits,&bias,4);api(f.d->SetSamplerState(stage,D3DSAMP_MIPMAPLODBIAS,bits),"fade route zero bias");
        }
    };
    const auto read=[&](unsigned which){std::vector<float> out(std::size_t(f.W)*f.H*(which==2?1:4));unsigned w=0,h=0;api(f.emission_readback(f.d.p,which,out.data(),unsigned(out.size()),&w,&h),"fade route raw target");require(w==f.W&&h==f.H,"fade route target dimensions");return out;};
    const auto scene=[&](){Com<IDirect3DSurface9> logical;api(f.d->GetRenderTarget(0,&logical.p),"fade route logical view restores lazy MRTs");unsigned w=0,h=0;auto result=f.hdr_image(&w,&h);require(w==f.W&&h==f.H,"fade route HDR dimensions");return result;};
    const auto write=[&](const char* kind,const std::vector<float>& values){char path[96];std::snprintf(path,sizeof path,"fade_route_%s_%llu.f32",kind,f.frame);FILE* file=std::fopen(path,"wb");require(file!=nullptr,"fade route evidence file");require(std::fwrite(values.data(),4,values.size(),file)==values.size(),"fade route evidence bytes");std::fclose(file);};
    Object objects[2]={f.b,f.b};
    for(unsigned i=0;i<2;++i){objects[i].scope.node_serial=9100+i;objects[i].scope.node=0x910000+i*0x100;objects[i].scope.mesh=0x920000+i*0x100;objects[i].vb=vertices[i].p;objects[i].recorded=false;}
    // Interior pixels of each quad (edges excluded): P columns 9..22, Q 41..54, rows 9..22.
    const auto interior=[&](unsigned which,unsigned x,unsigned y){const unsigned l=which?41:9,r=which?55:23;return x>=l&&x<r&&y>=9&&y<23;};
    for(unsigned plan=0;plan<frames;++plan) {
        const bool routed_plan=routed_frame(plan);
        f.frame_begin();f.linear_material_inputs();f.write_reserved();
        if(sentinel_script){const RECT lower{0,LONG(f.H/2),LONG(f.W),LONG(f.H)};api(f.d->SetScissorRect(&lower),"fade route A below the quads");}
        f.draw(f.a,0,0,0,true,true,f.a.recorded,Alter::None,false);
        const unsigned required=f.emission_status(f.d.p,16);
        require(required==2,"fade route: the fade producer is the required one");
        unsigned covered=0,own_motion=0,mask_set=0,preserved=1,alpha_kept=1;
        for(unsigned which=0;which<2;++which) {
            f.scope(&objects[which]);bind(which,plan);
            const auto before=scene(),before_motion=read(1);
            const auto state=f.snapshot();
            const unsigned prepared_before=f.emission_status(f.d.p,4),routed_before=f.emission_status(f.d.p,50),refused_before=f.emission_status(f.d.p,51);
            api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"fade route actual original DIP");++f.draw_index;
            f.compare(state,f.snapshot(),"fade route complete draw restoration");
            const auto after=scene(),motion=read(1),mask=read(3);
            const unsigned prepared=f.emission_status(f.d.p,4)-prepared_before,routed_delta=f.emission_status(f.d.p,50)-routed_before,refused_delta=f.emission_status(f.d.p,51)-refused_before;
            require(routed_delta==unsigned(routed_plan)&&refused_delta==unsigned(!routed_plan)&&prepared==unsigned(!routed_plan),"fade route arm decision matches the script");
            const bool matched=routed_plan&&objects[which].recorded;
            for(unsigned y=0;y<f.H;++y)for(unsigned x=0;x<f.W;++x) {
                const unsigned n=y*f.W+x,i=4*n;
                const bool changed=std::memcmp(&before[i],&after[i],12)!=0;covered+=changed;
                if(after[i+3]!=before[i+3])alpha_kept=0;
                if(!routed_plan&&std::memcmp(&motion[i],&before_motion[i],16))preserved=0;
                if(!interior(which,x,y))continue;
                if(mask[i]>0||mask[i+1]>0||mask[i+2]>0)++mask_set;
                if(routed_plan) {
                    const double u=(x-f.jx)/f.W+.5/f.W,v=(y-f.jy)/f.H+.5/f.H;
                    // Unmatched (mode 0, alpha -1 under SRCALPHA/INVSRCALPHA: -src + 2 dst): over the fill sentinel it stays the
                    // sentinel; over A's matched rows (alpha 1) it leaves the rejected alpha 3, which the resolve treats as current-only.
                    const bool own=matched?(std::fabs(motion[i]-u)*f.W<.01&&std::fabs(motion[i+1]-v)*f.H<.01&&std::fabs(motion[i+2]-.3f)<4e-6&&motion[i+3]==1)
                                   :before_motion[i+3]==-1?(motion[i]==0&&motion[i+1]==0&&motion[i+2]==0&&motion[i+3]==-1):motion[i+3]==3;
                    own_motion+=own;
                    if(!own)std::printf("FADE_ROUTE_PIXEL_DIFF frame=%llu quad=%u x=%u y=%u matched=%u before_alpha=%.9g motion=%.9g,%.9g,%.9g,%.9g\n",f.frame,which,x,y,matched,before_motion[i+3],motion[i],motion[i+1],motion[i+2],motion[i+3]);
                    require_quiet(own,"fade route routed quad writes its own RT1 rows (alpha 1; unmatched: the sentinel over the fill, alpha 3 over A's rows)");
                }
            }
            if(which==0)for(unsigned s=0;s<2;++s){const unsigned x=s?20:12,y=x,i=(y*f.W+x)*4;std::printf("FADE_ROUTE_SAMPLE frame=%llu x=%u y=%u before=%.17g,%.17g,%.17g,%.17g after=%.17g,%.17g,%.17g,%.17g\n",f.frame,x,y,before[i],before[i+1],before[i+2],before[i+3],after[i],after[i+1],after[i+2],after[i+3]);}
            objects[which].recorded=routed_plan; // the row history keeps one frame: a refused frame loses the match
        }
        const auto color=scene(),motion=read(1),mask=read(3);
        write("color",color);write("motion",motion);write("mask",mask);
        require(alpha_kept,"fade route RGB-masked draws keep the destination alpha");
        if(!routed_plan)require(preserved,"fade route bracketed draws preserve RT1");
        require(mask_set==(routed_plan?0u:2u*14u*14u),"fade route M covers exactly the bracketed quads");
        require(f.emission_status(f.d.p,53)==(held_frame(plan)?2u:0u),"fade route hysteresis holds exactly the script's held frames");
        f.emission_reference_color=color;f.emission_reference_mask=mask;f.emissions_enabled=true;f.emission_mask_valid=f.emission_status(f.d.p,1)!=0;
        std::printf("FADE_ROUTE frame=%llu script=%s routed=%u matched=%u fade_routed=%u fade_refused=%u fade_held=%u prepared=%u covered=%u own_motion=%u mask_set=%u mask_valid=%u threshold=%u draws=2\n",
                    f.frame,script_setting,routed_plan,routed_plan&&plan>0&&routed_frame(plan-1),f.emission_status(f.d.p,50),f.emission_status(f.d.p,51),f.emission_status(f.d.p,53),f.emission_status(f.d.p,4),covered,own_motion,mask_set,f.emission_mask_valid,f.emission_status(f.d.p,52));
        f.boundary();
        api(f.d->EndScene(),"fade route EndScene");f.write_presented(f.color_image());api(f.d->SetDepthStencilSurface(f.depth.p),"fade route depth restore");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"fade route Present");++f.frame;++f.frames_since_reset;
    }
    api(f.d->SetIndices(nullptr),"fade route final indices release");api(f.d->SetStreamSource(0,nullptr,0,0),"fade route final stream release");
    std::printf("FADE_ROUTE_CHECKS frames=%u script=%s quads=2\n",frames,script_setting);
}
