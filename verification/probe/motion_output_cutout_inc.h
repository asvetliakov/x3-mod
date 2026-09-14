// Pure per-pixel witness reducer, extracted by the focused host corruption test.
// Inputs are actual live/native readbacks; coverage is the native draw's result.
unsigned cutout_pixel_errors(const float* color,const float* before_color,
        const float* motion,const float* before_motion,float depth,float before_depth,
        bool has_depth,bool pass,bool routed,bool matched,bool preserve_alpha,
        double u,double v,unsigned width,unsigned height) {
    unsigned errors=0;
    for(unsigned c=0;c<4;++c)if(!std::isfinite(color[c]))errors|=1;
    if(preserve_alpha&&color[3]!=before_color[3])errors|=2;
    if(!pass){
        if(std::memcmp(color,before_color,16))errors|=4;
        if(std::memcmp(motion,before_motion,16))errors|=8;
        if(has_depth&&depth!=before_depth)errors|=16;
    } else if(routed){
        if(has_depth&&!(std::fabs(depth-.3f)<4e-6))errors|=32;
        if(matched){
            if(!(std::fabs(motion[0]-u)*width<.01&&std::fabs(motion[1]-v)*height<.01&&std::fabs(motion[2]-.3f)<4e-6&&motion[3]==1))errors|=64;
        } else if(!(motion[0]==0&&motion[1]==0&&motion[2]==0&&motion[3]==-1))errors|=128;
    } else if(std::memcmp(motion,before_motion,16))errors|=256;
    return errors;
}

// Selected live cutouts. Original programs remain local inputs. The unhooked
// device supplies actual D3D alpha/depth coverage, never a CPU alpha threshold.
void run_cutout_integration(Fixture& f,const char* original_path) {
    require(f.seam&&f.enabled&&f.hdr&&f.hdr_agx&&f.emission_readback,"cutout HDR motion seam");
    const std::string supplied(original_path);const auto slash=supplied.find_last_of("/\\");
    require(slash!=std::string::npos,"cutout original directory");
    const char* vs_ids[]={"53a0a641107ed76c","4944d81dfe531b37"};
    const char* ps_ids[]={"63f96eba9eea7880","5e0a10fe752b6140"};
    struct Vertex {float p[3],uv[2],n[3],b[3],t[3];};
    const Vertex quad[]={{{-1,1,.3f},{0,0},{0,0,1},{0,1,0},{1,0,0}},{{1,1,.3f},{1,0},{0,0,1},{0,1,0},{1,0,0}},{{-1,-1,.3f},{0,1},{0,0,1},{0,1,0},{1,0,0}},{{1,-1,.3f},{1,1},{0,0,1},{0,1,0},{1,0,0}}};
    const unsigned short triangles[]={0,1,2,2,1,3,0,2,1,2,3,1};
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,20,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},{0,44,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},D3DDECL_END()};
    struct Inputs {
        Com<IDirect3DVertexShader9> vs[2],fade_vs;Com<IDirect3DPixelShader9> ps[2],fade_ps;
        Com<IDirect3DVertexDeclaration9> decl;Com<IDirect3DVertexBuffer9> vb;Com<IDirect3DIndexBuffer9> ib;
        Com<IDirect3DTexture9> tex[4];Com<IDirect3DCubeTexture9> cube;
    } live,native;
    const auto create=[&](IDirect3DDevice9* d,Inputs& in) {
        for(unsigned i=0;i<2;++i) {
            auto v=load((supplied.substr(0,slash+1)+"vs_"+vs_ids[i]+".bin").c_str());
            auto p=load((supplied.substr(0,slash+1)+"ps_"+ps_ids[i]+".bin").c_str());
            require(fnv(v.data(),v.size()*4)==std::strtoull(vs_ids[i],nullptr,16)&&fnv(p.data(),p.size()*4)==std::strtoull(ps_ids[i],nullptr,16),"cutout exact originals");
            api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&in.vs[i].p),"cutout original VS");
            api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&in.ps[i].p),"cutout original PS");
        }
        api(d->CreateVertexDeclaration(elements,&in.decl.p),"cutout declaration");
        api(d->CreateVertexBuffer(sizeof quad,0,0,D3DPOOL_MANAGED,&in.vb.p,nullptr),"cutout managed VB");
        void* memory=nullptr;api(in.vb->Lock(0,0,&memory,0),"cutout VB lock");std::memcpy(memory,quad,sizeof quad);api(in.vb->Unlock(),"cutout VB unlock");
        api(d->CreateIndexBuffer(sizeof triangles,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&in.ib.p,nullptr),"cutout managed IB");
        api(in.ib->Lock(0,0,&memory,0),"cutout IB lock");std::memcpy(memory,triangles,sizeof triangles);api(in.ib->Unlock(),"cutout IB unlock");
        const float rgb[][4]={{.5f,.25f,.75f,1},{.125f,.25f,.0625f,1},{.25f,0,0,1},{.25f,.5f,.75f,.5f}};
        const float threshold=1.f/255.f,pattern[]={0,std::nextafter(threshold,0.f),threshold,std::nextafter(threshold,1.f),0,1.f/128,.5f,1};
        for(unsigned t=0;t<4;++t) {
            api(d->CreateTexture(16,16,2,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&in.tex[t].p,nullptr),"cutout texture");
            for(unsigned level=0;level<2;++level) {
                D3DLOCKED_RECT lock{};api(in.tex[t]->LockRect(level,&lock,nullptr,0),"cutout texture lock");
                for(unsigned y=0;y<(16u>>level);++y)for(unsigned x=0;x<(16u>>level);++x) {
                    float value[4];std::memcpy(value,rgb[t],16);if(t<2)value[3]=pattern[(x+(t?3:0)+level)%8];
                    std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,value,16);
                }
                api(in.tex[t]->UnlockRect(level),"cutout texture unlock");
            }
        }
        api(d->CreateCubeTexture(1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&in.cube.p,nullptr),"cutout cube");
        for(unsigned face=0;face<6;++face){D3DLOCKED_RECT lock{};api(in.cube->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0),"cutout cube lock");const float value[]={.25f,.5f,.125f,1};std::memcpy(lock.pBits,value,16);api(in.cube->UnlockRect(D3DCUBEMAP_FACES(face),0),"cutout cube unlock");}
    };
    char mode_setting[8]{};const bool mixed=GetEnvironmentVariableA("X3M_FIXTURE_CUTOUT_MIXED",mode_setting,sizeof mode_setting)==1&&mode_setting[0]=='1';
    create(f.d.p,live);
    if(mixed){auto v=load((supplied.substr(0,slash+1)+"vs_b0602757fce6e870.bin").c_str());auto p=load((supplied.substr(0,slash+1)+"ps_517540ae6d5e5410.bin").c_str());require(fnv(v.data(),v.size()*4)==0xb0602757fce6e870ull&&fnv(p.data(),p.size()*4)==0x517540ae6d5e5410ull,"cutout interleave exact fade originals");api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&live.fade_vs.p),"cutout interleave fade VS");api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&live.fade_ps.p),"cutout interleave fade PS");}
    // The reference is created by the existing fixture even without TAA for
    // motion-only runs, keeping native coverage independent of the live hooks.
    require(f.reference_ready,"cutout unhooked native device");create(f.reference.d.p,native);
    Com<IDirect3DSurface9> target,staging,native_depth;
    api(f.reference.d->CreateRenderTarget(f.W,f.H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target.p,nullptr),"cutout native FP16");
    api(f.reference.d->CreateOffscreenPlainSurface(f.W,f.H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging.p,nullptr),"cutout native staging");
    api(f.reference.d->CreateDepthStencilSurface(f.W,f.H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&native_depth.p,nullptr),"cutout native depth");
    Object objects[2]={f.a,f.b};for(unsigned i=0;i<2;++i){objects[i].scope.node_serial=9000+i;objects[i].scope.node=0x900000+i*0x100;objects[i].scope.mesh=0x910000+i*0x100;objects[i].vb=live.vb.p;objects[i].recorded=false;}
    char setting[16]{};const bool material=GetEnvironmentVariableA("X3M_LINEAR_MATERIALS",setting,sizeof setting)==1&&setting[0]=='1';
    const bool ordinary=GetEnvironmentVariableA("X3M_FIXTURE_CUTOUT_ORDINARY",setting,sizeof setting)==1&&setting[0]=='1';
    // Each refusal changes precisely one admitted state. Invalid API enums are
    // not used: these are real accepted application state setters.
    const D3DRENDERSTATETYPE wrong_state[]={D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_COLORWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_DITHERENABLE,D3DRS_CULLMODE,D3DRS_FILLMODE,D3DRS_STENCILENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ZENABLE,D3DRS_SRGBWRITEENABLE};
    const DWORD wrong_value[]={D3DCMP_GREATER,2,15,TRUE,TRUE,TRUE,D3DCULL_CW,D3DFILL_WIREFRAME,TRUE,FALSE,D3DCMP_LESS,FALSE,TRUE};
    const auto bind=[&](IDirect3DDevice9* d,Inputs& in,unsigned pair,unsigned step,float shift,bool control,int wrong) {
        for(auto s:{D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_DITHERENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE})api(d->SetRenderState(s,FALSE),"cutout off state");
        api(d->SetRenderState(D3DRS_ZENABLE,TRUE),"cutout depth test");api(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE),"cutout depth write");api(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL),"cutout LE depth");
        api(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"cutout both faces");api(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID),"cutout solid");
        api(d->SetRenderState(D3DRS_ALPHATESTENABLE,step!=10),"cutout native test");api(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL),"cutout native compare");api(d->SetRenderState(D3DRS_ALPHAREF,1),"cutout native reference");api(d->SetRenderState(D3DRS_COLORWRITEENABLE,step==10?15:7),"cutout caller write mask");
        api(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE),"cutout blend source");api(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO),"cutout blend destination");
        RECT rect{LONG(f.W/8),LONG(f.H/8),LONG(7*f.W/8),LONG(7*f.H/8)};api(d->SetScissorRect(&rect),"cutout scissor");api(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"cutout scissor enabled");
        for(unsigned i=0;i<16;++i)api(d->SetRenderState(D3DRENDERSTATETYPE((i<8?D3DRS_WRAP0:D3DRS_WRAP8)+i%8),0),"cutout WRAP inputs");
        if(wrong>=0)api(d->SetRenderState(wrong_state[wrong],wrong_value[wrong]),"cutout single refusal state");
        api(d->SetVertexDeclaration(in.decl.p),"cutout declaration bind");api(d->SetStreamSource(0,in.vb.p,0,sizeof(Vertex)),"cutout stream bind");api(d->SetStreamSourceFreq(0,1),"cutout stream frequency");api(d->SetIndices(in.ib.p),"cutout indices bind");api(d->SetVertexShader(in.vs[pair].p),"cutout VS bind");api(d->SetPixelShader(in.ps[pair].p),"cutout PS bind");
        float vc[48][4]{},pc[8][4]{};for(unsigned i=0;i<4;++i)vc[24+i][i]=1;vc[24][3]=shift;for(unsigned i=0;i<3;++i)vc[31+i][i]=1;vc[36][3]=4;
        if(control){vc[24][3]+=float(2*f.jx/f.W);vc[25][3]-=float(2*f.jy/f.H);}
        const float scale=step==4?4.f:1.f;vc[37][0]=scale;vc[38][1]=scale;vc[37][2]=.5f/16;vc[38][2]=.5f/16;
        vc[39][0]=step==5?0:step==9?.625f:1;vc[40][0]=.25f;vc[40][1]=.125f;vc[40][2]=.0625f;vc[41][0]=.75f;vc[41][1]=.125f;
        vc[0][2]=2;vc[1][0]=.5f;vc[1][1]=.25f;vc[1][2]=.125f;vc[2][0]=2;vc[2][1]=.25f;vc[2][2]=.125f;
        pc[0][0]=pc[1][1]=pc[2][2]=1;pc[3][0]=step==7?1:step==8?.375f:0;pc[4][2]=1;pc[5][0]=.375f;pc[5][1]=.25f;pc[5][2]=.5f;pc[6][2]=-1;pc[7][0]=.125f;pc[7][1]=.5f;pc[7][2]=.25f;
        api(d->SetVertexShaderConstantF(0,vc[0],48),"cutout original vertex inputs");api(d->SetPixelShaderConstantF(0,pc[0],8),"cutout original pixel inputs");const int count[]={1,0,1,0};const BOOL fog=step==9;api(d->SetVertexShaderConstantI(0,count,1),"cutout point count");api(d->SetVertexShaderConstantB(0,&fog,1),"cutout shader fog");
        for(unsigned i=0;i<7;++i){IDirect3DBaseTexture9* tex=nullptr;if(i==0)tex=in.tex[0].p;else if(i==1)tex=in.tex[pair?3:2].p;else if(i==2)tex=in.tex[pair?2:1].p;else if(pair&&i==3)tex=in.tex[1].p;else if(i==(pair?4u:3u))tex=in.cube.p;api(d->SetTexture(i,tex),"cutout sampler role");for(auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(d->SetSamplerState(i,s,step==3?D3DTEXF_LINEAR:D3DTEXF_POINT),"cutout filter");api(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_POINT),"cutout mip filter");api(d->SetSamplerState(i,D3DSAMP_MAXMIPLEVEL,step==4?1:0),"cutout mip selection");api(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP),"cutout address u");api(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP),"cutout address v");api(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE),"cutout numeric sampler");const float bias=control&&step==10?f.mip_bias:0;DWORD bits;std::memcpy(&bits,&bias,4);api(d->SetSamplerState(i,D3DSAMP_MIPMAPLODBIAS,bits),"cutout matched effective LOD bias");}
    };
    const auto read=[&](unsigned which){std::vector<float> out(std::size_t(f.W)*f.H*(which==2?1:4));unsigned w=0,h=0;const HRESULT hr=f.emission_readback(f.d.p,which,out.data(),unsigned(out.size()),&w,&h);if(which==2&&!f.materialwrap_depth){require(hr==D3DERR_NOTFOUND,"cutout no current-depth target");return std::vector<float>{};}api(hr,"cutout raw target");require(w==f.W&&h==f.H,"cutout target dimensions");return out;};
    const auto scene=[&](){Com<IDirect3DSurface9> logical;api(f.d->GetRenderTarget(0,&logical.p),"cutout logical view restores lazy MRTs");unsigned w=0,h=0;auto result=f.hdr_image(&w,&h);require(w==f.W&&h==f.H,"cutout HDR dimensions");return result;};
    const auto write=[&](const char* kind,const std::vector<float>& values){char path[96];std::snprintf(path,sizeof path,"cutout_%s_%llu.f32",kind,f.frame);FILE* file=std::fopen(path,"wb");require(file!=nullptr,"cutout evidence file");require(std::fwrite(values.data(),4,values.size(),file)==values.size(),"cutout evidence bytes");std::fclose(file);};
    Com<IDirect3DQuery9> event;LARGE_INTEGER frequency{};api(f.d->CreateQuery(D3DQUERYTYPE_EVENT,&event.p),"cutout completion EVENT");require(QueryPerformanceFrequency(&frequency),"cutout QPC");
    const auto fence=[&](){api(event->Issue(D3DISSUE_END),"cutout completion issue");f.wait(event.p);};
    // One actual source-over source from the existing six-pair fade route.
    // Reuse our managed geometry/textures and native inputs; only the two
    // original programs and their native sampler/constant roles differ.
    const auto fade=[&](unsigned source){
        f.scope(nullptr);bind(f.d.p,live,0,1,0,false,-1);
        api(f.d->SetVertexShader(live.fade_vs.p),"cutout interleave fade vertex");api(f.d->SetPixelShader(live.fade_ps.p),"cutout interleave fade pixel");
        const float alpha[]={.625f,0,0,0};api(f.d->SetVertexShaderConstantF(39,alpha,1),"cutout interleave fade alpha");
        const float pc[][4]={{0,0,1,0},{.375f,.25f,.5f,0},{0,0,-1,0},{.125f,.5f,.25f,0},{.5f,0,0,0},{1,0,0,0}};api(f.d->SetPixelShaderConstantF(0,pc[0],6),"cutout interleave fade pixel inputs");
        api(f.d->SetTexture(3,nullptr),"cutout interleave fade no cube");api(f.d->SetRenderState(D3DRS_ZENABLE,TRUE),"cutout interleave fade depth test (the fade admission requires it)");api(f.d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"cutout interleave fade no depth write");api(f.d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"cutout interleave fade no alpha test");api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"cutout interleave fade blending");api(f.d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA),"cutout interleave source alpha");api(f.d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA),"cutout interleave destination alpha");api(f.d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"cutout interleave ADD");
        RECT rect{LONG((source?3:1)*f.W/8),LONG(f.H/4),LONG((source?7:5)*f.W/8),LONG(3*f.H/4)};api(f.d->SetScissorRect(&rect),"cutout interleave fade rectangle");
        const auto before=scene(),motion=read(1),depth=read(2);const auto state=f.snapshot();const unsigned prepared=f.emission_status(f.d.p,4),calls=f.emission_status(f.d.p,12);
        api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"cutout actual interleaved fade DIP");f.compare(state,f.snapshot(),"cutout interleaved fade restoration");
        const auto after=scene();require(read(1)==motion&&read(2)==depth,"cutout interleaved fade preserves current motion/depth");for(std::size_t i=3;i<after.size();i+=4)require_quiet(after[i]==before[i],"cutout interleaved fade retains native destination alpha");
        require(f.emission_status(f.d.p,4)==prepared+1&&f.emission_status(f.d.p,12)==calls+1,"cutout interleaved fade prepared once and source called once");
        std::printf("CUTOUT_FADE frame=%llu source=%u prepared=1 original_calls=1\n",f.frame,source);
    };
    unsigned last_seen[2]={~0u,~0u},last_first[2]={0,0};
    constexpr unsigned VSFailure=58,PSFailure=59,RSFailure=60,SamplerFailure=61,ResetFailure=62;
    const unsigned frames=f.cutout_bench?18:mixed?3:70;
    for(unsigned iteration=0;iteration<frames;++iteration) {
        const unsigned plan=mixed?70+iteration:iteration;
        const unsigned near_steps[]={1,5,1,7,8,1};
        const unsigned pair=f.cutout_bench||plan>=64?0:plan<32?plan/16:plan%2,step=f.cutout_bench?1:plan<32?plan%16:plan>=64&&plan<70?near_steps[plan-64]:1;const int wrong=plan>=32&&plan<45&&!f.cutout_bench?int(plan-32):plan==RSFailure||plan==71?1:-1;
        // 1 px origin steps (0,1,2,1 px at 64 px): a sawtooth wrap would jump 3 px and trip the route's 2.4 px median cut rule.
        const float shift=.03125f*float(2-std::abs(int(plan%4)-2)),prior=objects[pair%2].rt;
        Object& object=objects[pair%2];const bool known=step!=13;
        const unsigned cap=plan>=45&&plan<=54?plan-45:plan==56?10:0;
        const bool routed=(material&&f.mip_bias==0&&wrong<0&&!(cap>=1&&cap<=10))||step==10;
        const bool arm_active=cap==0&&f.mip_bias==0; // inactive arm: ordinary native draw, no missed coverage, history kept
        if(!f.cutout_bench&&((plan>=45&&plan<=54)||plan==56))f.emission_fault(f.d.p,200,cap);
        f.frame_begin();f.linear_material_inputs();f.write_reserved();
        const float background_z=plan>=64&&plan<70?.300001f-.5f:step==12?-.3f:0;
        f.draw(f.a,-.015625f*float(plan%3),0,background_z,true,true,f.a.recorded,Alter::None,false);
        if(mixed)fade(0);
        f.scope(known?&object:nullptr);bind(f.d.p,live,pair%2,step,shift,false,plan==RSFailure?-1:wrong);
        if(f.cutout_bench){
            if(ordinary)api(f.d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE),"cutout timed ordinary motion fallback");
            const unsigned count=plan<6?1:plan<12?4:16;fence();LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);for(unsigned i=0;i<count;++i)api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"cutout measured DIP");fence();QueryPerformanceCounter(&end);
            if(plan%6>=2)std::printf("CUTOUT_TIMING width=%u height=%u count=%u sample=%u material=%u ordinary=%u ms=%.9f\n",f.W,f.H,count,plan%6-2,material,ordinary,1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart);
            api(f.d->EndScene(),"cutout benchmark EndScene");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"cutout benchmark Present");++f.frame;++f.frames_since_reset;continue;
        }
        if(step==14){Com<IDirect3DStateBlock9> saved;api(f.d->CreateStateBlock(D3DSBT_ALL,&saved.p),"cutout stateblock capture");api(f.d->SetRenderState(D3DRS_ALPHAREF,27),"cutout transient reference");api(f.d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE),"cutout transient sampler");api(saved->Apply(),"cutout stateblock restores unknown shadow");}
        const auto before_color=scene(),before_motion=read(1),before_depth=read(2);auto state=f.snapshot();
        DWORD before_extra[13]{};for(unsigned i=0;i<13;++i)api(f.d->GetRenderState(wrong_state[i],&before_extra[i]),"cutout admission snapshot");
        if(material&&(plan==VSFailure||plan==PSFailure))f.emission_fault(f.d.p,plan==VSFailure?210:211,1);
        if(!material&&plan==RSFailure){api(f.d->SetRenderState(D3DRS_ALPHAREF,2),"cutout off reference control");before_extra[1]=2;}
        if(material&&plan==RSFailure){f.emission_fault(f.d.p,220,D3DRS_ALPHAREF);require(f.d->SetRenderState(D3DRS_ALPHAREF,2)==E_FAIL,"cutout RS failure after mutation");before_extra[1]=2;require(!(f.emission_status(f.d.p,36)&(1u<<25)),"cutout failed reference setter invalidates shadow");}
        if(material&&plan==SamplerFailure){f.emission_fault(f.d.p,221,1);require(f.d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE)==E_FAIL,"cutout sampler failure after mutation");}
        if(material&&plan==SamplerFailure)state=f.snapshot();
        const unsigned queries_before=f.emission_status(f.d.p,31);
        const unsigned routed_before=f.emission_status(f.d.p,33),missed_before=f.emission_status(f.d.p,34);
        api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,step==2?6:0,2),"cutout actual original DIP");++f.draw_index;
        // A routed ordinary draw under a live nonzero bias holds the route's bias on the
        // mip-chain stages (the 1-level cube is ineligible) until the next restore point,
        // exactly as the mip-bias script models; everything else is restored immediately.
        const unsigned biased=routed&&f.bias_live()?(pair?0x0fu:0x07u):0u;
        if(biased){f.expect_bias("cutout routed ordinary draw holds the bias",biased);for(unsigned s=0;s<8;++s)if(biased>>s&1)state.samplers[s][sampler_count-1]=f.float_bits(f.mip_bias);}
        f.compare(state,f.snapshot(),"cutout complete draw restoration");require(f.emission_status(f.d.p,31)==queries_before,"cutout draw does not query capabilities");for(unsigned i=0;i<13;++i){DWORD value=0;api(f.d->GetRenderState(wrong_state[i],&value),"cutout admission readback");require(value==before_extra[i],"cutout exact admission-state restoration");}
        const auto color=scene(),motion=read(1),depth=read(2);
        // Save reference device state because its production TemporalPass shares
        // this unhooked device. All native resources survive only this fixture.
        IDirect3DDevice9* nd=f.reference.d.p;Com<IDirect3DStateBlock9> saved;api(nd->CreateStateBlock(D3DSBT_ALL,&saved.p),"cutout native state save");Com<IDirect3DSurface9> saved_rt,saved_ds;api(nd->GetRenderTarget(0,&saved_rt.p),"cutout native RT save");const HRESULT ds_hr=nd->GetDepthStencilSurface(&saved_ds.p);require(SUCCEEDED(ds_hr)||ds_hr==D3DERR_NOTFOUND,"cutout native DS save");
        api(nd->SetDepthStencilSurface(nullptr),"cutout native DS unbind");api(nd->SetRenderTarget(0,target.p),"cutout native RT");api(nd->SetDepthStencilSurface(native_depth.p),"cutout native DS");const D3DVIEWPORT9 viewport{0,0,f.W,f.H,0,1};api(nd->SetViewport(&viewport),"cutout native viewport");
        bind(nd,native,pair,step,shift,true,wrong);if(material&&plan==SamplerFailure)api(nd->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE),"cutout native mutated sampler");api(nd->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE),"cutout native full clear");api(nd->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xffff00ff,plan>=64&&plan<70?.300001f:step==12?.2f:.5f,0),"cutout native poison and occluder depth");api(nd->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"cutout native scissor restore");api(nd->BeginScene(),"cutout native BeginScene");api(nd->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,step==2?6:0,2),"cutout native threshold oracle");api(nd->EndScene(),"cutout native EndScene");api(nd->GetRenderTargetData(target.p,staging.p),"cutout native readback");
        // Magenta poison stays distinct even if a refused fixed-fog state makes
        // native RGB black. Coverage still comes only from the actual original draw.
        D3DLOCKED_RECT lock{};api(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY),"cutout native pixels lock");std::vector<float> coverage(std::size_t(f.W)*f.H);unsigned accepted=0,holes=0,owned=0;int rgb_sample=-1;
        const bool matched=routed&&known&&object.recorded&&last_seen[pair]+1==plan&&last_first[pair]==(step==2?6u:0u);
        for(unsigned y=0;y<f.H;++y)for(unsigned x=0;x<f.W;++x){const unsigned n=y*f.W+x,i=4*n;const auto* p=reinterpret_cast<const unsigned short*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)+4*x;const bool pass=p[0]!=0x3c00||p[1]!=0||p[2]!=0x3c00;coverage[n]=pass;accepted+=pass;holes+=!pass;if(pass&&rgb_sample<0)rgb_sample=int(n);
            if(pass&&(!material||plan==VSFailure||plan==PSFailure||plan==SamplerFailure)&&wrong<0){for(unsigned c=0;c<3;++c){const unsigned h=p[c],exponent=(h>>10)&31,mantissa=h&1023;const float native_rgb=std::ldexp(float(exponent?mantissa+1024:mantissa),exponent?int(exponent)-25:-24)*(h&0x8000?-1:1);require_quiet(color[i+c]==native_rgb,"cutout original fallback RGB exact native twin");}}
            const double u=(x-f.jx)/f.W+.5*(prior-shift)+.5/f.W,v=(y-f.jy)/f.H+.5/f.H;
            const unsigned errors=cutout_pixel_errors(&color[i],&before_color[i],&motion[i],&before_motion[i],
                depth.empty()?0:depth[n],before_depth.empty()?0:before_depth[n],!depth.empty(),pass,routed,matched,step!=10&&wrong!=2,u,v,f.W,f.H);
            if(errors)std::printf("CUTOUT_PIXEL_DIFF frame=%llu x=%u y=%u errors=%u pass=%u routed=%u matched=%u motion=%.9g,%.9g,%.9g,%.9g\n",f.frame,x,y,errors,pass,routed,matched,motion[i],motion[i+1],motion[i+2],motion[i+3]);
            require_quiet(errors==0,"cutout native coverage, alpha and motion/depth ownership");
            owned+=pass&&routed;
        }
        api(staging->UnlockRect(),"cutout native pixels unlock");api(nd->SetDepthStencilSurface(nullptr),"cutout native DS release");api(nd->SetRenderTarget(0,saved_rt.p),"cutout native RT restore");api(nd->SetDepthStencilSurface(saved_ds.p),"cutout native DS restore");api(saved->Apply(),"cutout native state restore");
        require(holes>0,"cutout nonvacuous background");if(wrong<0&&step!=5&&step!=12)require(accepted>0,"cutout nonvacuous accepted samples");if(step==5||step==12)require(accepted==0,"cutout alpha-zero and front depth reject all");
        f.records.push_back({&object,shift,0,-.2f,routed,matched,prior,0,-.2f,false,f.jitter,routed&&known});if(routed&&known){object.recorded=true;object.rt=shift;last_seen[pair]=plan;last_first[pair]=step==2?6:0;}
        if(material&&rgb_sample>=0&&routed&&wrong<0&&plan!=VSFailure&&plan!=PSFailure&&plan!=SamplerFailure){const unsigned i=4*unsigned(rgb_sample);std::printf("CUTOUT_RGB frame=%llu pair=%u step=%u reverse=%u rgb=%.9g,%.9g,%.9g\n",f.frame,pair,step,step==2,color[i],color[i+1],color[i+2]);}
        const unsigned routed_delta=f.emission_status(f.d.p,33)-routed_before,missed_delta=f.emission_status(f.d.p,34)-missed_before;
        if(step!=10)require(routed_delta==unsigned(routed)&&missed_delta==unsigned(material&&!routed&&arm_active),"cutout actual routing counters");
        write("color",color);write("motion",motion);write("depth",depth);write("coverage",coverage);
        std::printf("CUTOUT_LIVE frame=%llu pair=%u step=%u wrong=%d material=%u depth=%u routed=%u matched=%u accepted=%u holes=%u owned=%u cap=%u cap_status=%u cap_queries=%u routed_delta=%u missed_delta=%u unavailable=%u\n",f.frame,pair,step,wrong,material,f.materialwrap_depth,routed,matched,accepted,holes,owned,cap,f.emission_status(f.d.p,30),f.emission_status(f.d.p,31),routed_delta,missed_delta,f.emission_status(f.d.p,32));
        if(mixed)fade(1);
        f.emission_reference_color=mixed?scene():color;f.emissions_enabled=mixed||(material&&!routed&&arm_active); // only an active arm's refusal drops historyf.emission_mask_valid=mixed&&routed;
        if(mixed){f.emission_reference_mask=read(3);unsigned covered=0;for(unsigned y=0;y<f.H;++y)for(unsigned x=0;x<f.W;++x){const bool wanted=x>=f.W/8&&x<7*f.W/8&&y>=f.H/4&&y<3*f.H/4;const unsigned i=4*(y*f.W+x);for(unsigned c=0;c<3;++c)require_quiet((f.emission_reference_mask[i+c]>0)==wanted,"cutout interleaved native fade-mask union");covered+=wanted;}write("composed",f.emission_reference_color);write("mask",f.emission_reference_mask);std::printf("CUTOUT_UNION frame=%llu covered=%u unavailable=%u\n",f.frame,covered,!routed);}
        if(f.taa)f.boundary();
        else {api(f.d->SetDepthStencilSurface(nullptr),"cutout non-TAA boundary depth");api(f.d->StretchRect(f.back.p,nullptr,f.bloom_surface.p,nullptr,D3DTEXF_NONE),"cutout non-TAA publication");}
        api(f.d->EndScene(),"cutout EndScene");f.write_presented(f.color_image());api(f.d->SetDepthStencilSurface(f.depth.p),"cutout depth restore");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"cutout Present");++f.frame;++f.frames_since_reset;
        if(plan==ResetFailure){require(FAILED(f.d->Reset(&f.pp)),"cutout held surfaces force failed Reset");require(f.emission_status(f.d.p,30)!=1,"cutout failed Reset clears positive capabilities");f.reset();for(auto& o:objects)o.recorded=false;}
        if(step==15){f.reset();for(auto& o:objects)o.recorded=false;}
    }
    api(f.d->SetIndices(nullptr),"cutout final indices release");api(f.d->SetStreamSource(0,nullptr,0,0),"cutout final stream release");
    std::printf("CUTOUT_CHECKS frames=%u benchmark=%u pairs=2\n",frames,f.cutout_bench);
}
