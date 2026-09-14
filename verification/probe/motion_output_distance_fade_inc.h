// Actual live source-over fade -> shared composition pool -> HDR/TAA exercise.
// This fixture owns only original inputs, state/pixel witnesses and scheduling.
// The detached 71-case oracle remains the owner of the full material equations.
void run_distance_fade_integration(Fixture& f,const char* original_path) {
    require(f.seam&&f.enabled&&f.emission_status&&f.emission_fault&&f.emission_readback,"distance fade live seam");
    const bool qualified=f.taa&&f.hdr&&f.hdr_agx;
    const auto sibling=[&](const char* stage,const char* hash) {
        const std::string supplied(original_path);const auto slash=supplied.find_last_of("/\\");
        require(slash!=std::string::npos,"distance fade original directory");
        return supplied.substr(0,slash+1)+stage+"_"+hash+".bin";
    };
    const char* vertex_ids[]={"b0602757fce6e870","0c223ad11bce02d5","233d17d26ce0c1fc","167eb2d5629ab9d3","330ceb9dd874ede2","12b8a13f13fe8cfe","d5e1c75351ed3f04"};
    const char* pixel_ids[]={"517540ae6d5e5410","7a0c3388065bb08d","d44db87778a43b61","550c2a4d4d3ed70f","8360f422de08b5bd"};
    const unsigned pair_pixel[]={0,1,1,2,3,3};
    Com<IDirect3DVertexShader9> vertex[7];Com<IDirect3DPixelShader9> pixel[5];
    for(unsigned i=0;i<7;++i) {
        if(f.distancefade_bench&&i!=0)continue;
        const auto code=load(sibling("vs",vertex_ids[i]).c_str());
        require(fnv(code.data(),code.size()*4)==std::strtoull(vertex_ids[i],nullptr,16),"fade original VS identity");
        api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&vertex[i].p),"fade original VS");
    }
    for(unsigned i=0;i<5;++i) {
        if(f.distancefade_bench&&i!=0)continue;
        const auto code=load(sibling("ps",pixel_ids[i]).c_str());
        require(fnv(code.data(),code.size()*4)==std::strtoull(pixel_ids[i],nullptr,16),"fade original PS identity");
        api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&pixel[i].p),"fade original PS");
    }
    // One full-screen source quad; exact scissor rectangles bound the temporal
    // union independently of pixel shader color, including alpha-zero coverage.
    struct SourceVertex {float p[3],uv[2],n[3],b[3],t[3];};
    const float l=-1-1.f/f.W,r=1-1.f/f.W,t=1+1.f/f.H,b=-1+1.f/f.H;
    const SourceVertex quad[]={{{l,t,.1f},{0,0},{0,0,1},{0,1,0},{1,0,0}},{{r,t,.1f},{1,0},{0,0,1},{0,1,0},{1,0,0}},{{l,b,.1f},{0,1},{0,0,1},{0,1,0},{1,0,0}},{{r,b,.1f},{1,1},{0,0,1},{0,1,0},{1,0,0}}};
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,20,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},{0,44,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},D3DDECL_END()};
    Com<IDirect3DVertexDeclaration9> declaration;Com<IDirect3DVertexBuffer9> vertices;Com<IDirect3DIndexBuffer9> indices;
    api(f.d->CreateVertexDeclaration(elements,&declaration.p),"fade source declaration");
    api(f.d->CreateVertexBuffer(sizeof quad,0,0,D3DPOOL_MANAGED,&vertices.p,nullptr),"fade source vertices");
    void* data=nullptr;api(vertices->Lock(0,0,&data,0),"fade vertex lock");std::memcpy(data,quad,sizeof quad);api(vertices->Unlock(),"fade vertex unlock");
    const unsigned short triangles[]={0,1,2,2,1,3,0,1,2,2,1,3};
    api(f.d->CreateIndexBuffer(sizeof triangles,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr),"fade source indices");
    api(indices->Lock(0,0,&data,0),"fade index lock");std::memcpy(data,triangles,sizeof triangles);api(indices->Unlock(),"fade index unlock");
    const float texels[][4]={{.5f,.25f,.75f,.5f},{.25f,.375f,.75f,.625f},{.25f,.875f,.125f,.75f},{.125f,.25f,.0625f,.25f},{.5f,.25f,.75f,0},{.5f,.25f,.125f,.125f},{.5f,.25f,.125f,.25f}};
    Com<IDirect3DTexture9> textures[7];
    for(unsigned i=0;i<7;++i) {
        api(f.d->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&textures[i].p,nullptr),"fade source texture");
        D3DLOCKED_RECT lock{};api(textures[i]->LockRect(0,&lock,nullptr,0),"fade source texture lock");std::memcpy(lock.pBits,texels[i],16);api(textures[i]->UnlockRect(0),"fade source texture unlock");
    }
    // Native controls run on the existing unhooked system-D3D reference device.
    // They neither touch the live selector/counters nor seed temporal history.
    Com<IDirect3DVertexShader9> native_vertex[6];Com<IDirect3DPixelShader9> native_pixel[4];
    Com<IDirect3DVertexDeclaration9> native_declaration;Com<IDirect3DVertexBuffer9> native_vertices;
    Com<IDirect3DIndexBuffer9> native_indices;Com<IDirect3DTexture9> native_textures[7];
    const auto raw=[&](unsigned target,std::vector<float>& image) {
        unsigned w=0,h=0;image.assign(std::size_t(f.W)*f.H*(target==2?1:4),0);
        const HRESULT hr=f.emission_readback(f.d.p,target,image.data(),unsigned(image.size()),&w,&h);
        require(hr==D3DERR_NOTFOUND||SUCCEEDED(hr),"fade raw supplemental readback");
        if(SUCCEEDED(hr))require(w==f.W&&h==f.H,"fade raw dimensions");
        return hr;
    };
    const auto scene=[&]() {
        Com<IDirect3DSurface9> logical;api(f.d->GetRenderTarget(0,&logical.p),"fade application RT view");
        if(!f.hdr){const auto image=f.color_image();std::vector<float> out(image.size()*4);for(std::size_t i=0;i<image.size();++i)for(unsigned c=0;c<4;++c)out[4*i+c]=float((image[i]>>(c==0?16:c==1?8:c==2?0:24))&255)/255.f;return out;}
        unsigned w=0,h=0;auto image=f.hdr_image(&w,&h);require(w==f.W&&h==f.H,"fade HDR image dimensions");return image;
    };
    const auto hash=[](const std::vector<float>& image){return fnv(image.data(),image.size()*sizeof(float));};
    const auto write_raw=[&](const char* kind,const std::vector<float>& image) {
        char name[96];std::snprintf(name,sizeof name,"distance_fade_%s_%llu.rgba32f",kind,f.frame);
        FILE* file=std::fopen(name,"wb");require(file!=nullptr,"fade raw output");require(std::fwrite(image.data(),sizeof(float),image.size(),file)==image.size(),"fade raw bytes");std::fclose(file);
    };
    // Original asteroid inputs match run_linear_distance_fade.cases()[pair*10].
    // World position is constant zero, so lighting/fog are uniform despite the
    // full-screen clip geometry. The normal texture remains nontrivial AG data.
    const auto bind_source=[&](unsigned pair,bool emission,bool zero,unsigned source,bool failed,bool native_control=false) {
        IDirect3DDevice9* device=native_control?f.reference.d.p:f.d.p;
        if(!native_control){f.scope(nullptr);f.scene_states();}
        else {
            for(auto state:{D3DRS_ZENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_DITHERENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_LIGHTING})api(device->SetRenderState(state,FALSE),"fade native control state");
            api(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"fade native control cull");api(device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID),"fade native control fill");
        }
        api(device->SetVertexDeclaration(native_control?native_declaration.p:declaration.p),"fade source declaration bind");api(device->SetStreamSource(0,native_control?native_vertices.p:vertices.p,0,sizeof(SourceVertex)),"fade source stream");api(device->SetStreamSourceFreq(0,1),"fade source frequency");api(device->SetIndices(failed?nullptr:native_control?native_indices.p:indices.p),"fade actual index binding");
        api(device->SetVertexShader(native_control?native_vertex[pair].p:vertex[emission?6:pair].p),"fade source VS bind");api(device->SetPixelShader(native_control?native_pixel[pair_pixel[pair]].p:pixel[emission?4:pair_pixel[pair]].p),"fade source PS bind");
        float vc[48][4]{},pc[8][4]{};
        const bool fixed=pair==2||pair==5,bump=pair>=3;
        if(emission) {
            for(unsigned i=0;i<4;++i)vc[i][i]=1;
            vc[10][0]=vc[11][1]=1;vc[12][0]=.5f;
            pc[0][0]=pc[1][1]=pc[2][2]=1;
        } else {
            const unsigned matrix=fixed?0:24,normal=fixed?10:31,camera=fixed?13:34,uv=fixed?16:37,alpha=fixed?18:39,emissive=fixed?19:40,point=fixed?4:0;
            for(unsigned i=0;i<4;++i)vc[matrix+i][i]=1;
            for(unsigned i=0;i<3;++i)vc[normal+i][i]=1;
            vc[camera+2][3]=4;vc[uv][2]=.0625f;vc[uv+1][2]=.1875f;
            vc[alpha][0]=.625f;vc[emissive][0]=.25f;vc[emissive][1]=.125f;vc[emissive][2]=.0625f;
            vc[fixed?20:41][0]=.75f;vc[fixed?20:41][1]=.125f;
            vc[point][2]=2;vc[point+1][0]=.5f;vc[point+1][1]=.25f;vc[point+1][2]=.125f;vc[point+2][0]=2;vc[point+2][1]=.25f;vc[point+2][2]=.125f;
            pc[0][2]=1;pc[1][0]=.375f;pc[1][1]=.25f;pc[1][2]=.5f;
            const unsigned dirs=pair==0||pair==3?2:1;
            if(dirs==2){pc[2][2]=-1;pc[3][0]=.125f;pc[3][1]=.5f;pc[3][2]=.25f;}
            pc[2*dirs][0]=.5f;pc[2*dirs+1][0]=1;
        }
        api(device->SetVertexShaderConstantF(0,vc[0],48),"fade native VS inputs");api(device->SetPixelShaderConstantF(0,pc[0],8),"fade native PS inputs");
        const int count[4]={1,0,1,0};const BOOL fog=!emission;api(device->SetVertexShaderConstantI(0,count,1),"fade point count");api(device->SetVertexShaderConstantB(0,&fog,1),"fade original fog");
        auto* source_textures=native_control?native_textures:textures;
        for(unsigned stage=0;stage<7;++stage) {
            IDirect3DBaseTexture9* texture=nullptr;
            if(emission){if(stage==0)texture=source_textures[source>=2?6:5].p;}
            else if(stage==0)texture=source_textures[zero?4:0].p;
            else if(stage==1)texture=source_textures[bump?1:2].p;
            else if(stage==2)texture=source_textures[bump?2:3].p;
            else if(stage==3&&bump)texture=source_textures[3].p;
            api(device->SetTexture(stage,texture),"fade native sampler role");
            for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(device->SetSamplerState(stage,filter,D3DTEXF_POINT),"fade point sampling");
            api(device->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"fade no mip");api(device->SetSamplerState(stage,D3DSAMP_SRGBTEXTURE,FALSE),"fade numeric sampler state");
        }
        api(device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"fade no depth write");api(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"fade source blending");api(device->SetRenderState(D3DRS_SRCBLEND,emission?D3DBLEND_ONE:D3DBLEND_SRCALPHA),"fade blend source");api(device->SetRenderState(D3DRS_DESTBLEND,emission?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA),"fade blend destination");api(device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"fade additive operation");api(device->SetRenderState(D3DRS_COLORWRITEENABLE,emission?15:7),"fade native alpha mask");
        RECT rect{LONG((source?3:1)*f.W/8),LONG(f.H/4),LONG((source?7:5)*f.W/8),LONG(3*f.H/4)};
        if(native_control)rect={0,0,8,8};
        api(device->SetScissorRect(&rect),"fade source scissor");api(device->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"fade scissor enable");
        return rect;
    };
    if(qualified&&!f.distancefade_bench) {
        require(f.reference_ready,"fade native controls need the existing reference device");
        IDirect3DDevice9* device=f.reference.d.p;
        Com<IDirect3DStateBlock9> saved;api(device->CreateStateBlock(D3DSBT_ALL,&saved.p),"fade native control state save");
        Com<IDirect3DSurface9> saved_rt,saved_depth,target,staging;
        api(device->GetRenderTarget(0,&saved_rt.p),"fade native control original RT");
        const HRESULT depth_hr=device->GetDepthStencilSurface(&saved_depth.p);require(SUCCEEDED(depth_hr)||depth_hr==D3DERR_NOTFOUND,"fade native control original depth");
        for(unsigned i=0;i<6;++i){const auto code=load(sibling("vs",vertex_ids[i]).c_str());api(device->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&native_vertex[i].p),"fade native control VS");}
        for(unsigned i=0;i<4;++i){const auto code=load(sibling("ps",pixel_ids[i]).c_str());api(device->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&native_pixel[i].p),"fade native control PS");}
        api(device->CreateVertexDeclaration(elements,&native_declaration.p),"fade native control declaration");
        api(device->CreateVertexBuffer(sizeof quad,0,0,D3DPOOL_MANAGED,&native_vertices.p,nullptr),"fade native control VB");
        api(native_vertices->Lock(0,0,&data,0),"fade native control VB lock");std::memcpy(data,quad,sizeof quad);api(native_vertices->Unlock(),"fade native control VB unlock");
        api(device->CreateIndexBuffer(sizeof triangles,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&native_indices.p,nullptr),"fade native control IB");
        api(native_indices->Lock(0,0,&data,0),"fade native control IB lock");std::memcpy(data,triangles,sizeof triangles);api(native_indices->Unlock(),"fade native control IB unlock");
        for(unsigned i=0;i<7;++i){api(device->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&native_textures[i].p,nullptr),"fade native control texture");D3DLOCKED_RECT lock{};api(native_textures[i]->LockRect(0,&lock,nullptr,0),"fade native control texture lock");std::memcpy(lock.pBits,texels[i],16);api(native_textures[i]->UnlockRect(0),"fade native control texture unlock");}
        api(device->CreateRenderTarget(8,8,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target.p,nullptr),"fade native control FP16 RT");
        api(device->CreateOffscreenPlainSurface(8,8,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging.p,nullptr),"fade native control readback");
        api(device->SetDepthStencilSurface(nullptr),"fade native control no depth");api(device->SetRenderTarget(0,target.p),"fade native control target bind");
        const D3DVIEWPORT9 viewport{0,0,8,8,0,1};api(device->SetViewport(&viewport),"fade native control viewport");
        api(device->BeginScene(),"fade native control BeginScene");
        for(unsigned pair=0;pair<6;++pair) {
            bind_source(pair,false,false,0,false,true);
            api(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffffu,1,0),"fade native control unit background");
            api(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"fade native control original draw");
            api(device->GetRenderTargetData(target.p,staging.p),"fade native control readback");
            D3DLOCKED_RECT lock{};api(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY),"fade native control readback lock");
            const auto* encoded=reinterpret_cast<const unsigned short*>(static_cast<const char*>(lock.pBits)+4*lock.Pitch)+4*4;
            double rgba[4]{};for(unsigned k=0;k<4;++k){const unsigned h=encoded[k],exponent=(h>>10)&31,mantissa=h&1023;require(exponent<31,"fade native control finite");rgba[k]=std::ldexp(double(exponent?mantissa+1024:mantissa),exponent?int(exponent)-25:-24)*(h&0x8000?-1:1);}
            api(staging->UnlockRect(),"fade native control readback unlock");require(rgba[3]==1,"fade native control target alpha retained");
            std::printf("FADE_NATIVE pair=%u before=1,1,1,1 after=%.17g,%.17g,%.17g,%.17g\n",pair,rgba[0],rgba[1],rgba[2],rgba[3]);
        }
        api(device->EndScene(),"fade native control EndScene");
        api(device->SetRenderTarget(0,saved_rt.p),"fade native control RT restore");api(device->SetDepthStencilSurface(saved_depth.p),"fade native control depth restore");api(saved->Apply(),"fade native control state restore");
    }
    Com<IDirect3DQuery9> completion;LARGE_INTEGER frequency{};
    if(f.distancefade_bench){require(qualified,"fade benchmark complete activation");api(f.d->CreateQuery(D3DQUERYTYPE_EVENT,&completion.p),"fade benchmark EVENT");require(QueryPerformanceFrequency(&frequency),"fade benchmark QPC");}
    const auto fence=[&](){api(completion->Issue(D3DISSUE_END),"fade timed EVENT issue");f.wait(completion.p);};
    const unsigned frames=f.distancefade_bench?18:qualified?30:4;
    unsigned submissions=0;
    for(unsigned plan=0;plan<frames;++plan) {
        f.frame_begin();f.linear_material_inputs();f.write_reserved();
        if(qualified&&!f.distancefade_bench&&plan==13) {
            // Reuse the existing matched opaque object, avoiding a new-object
            // global cut that would hide previous-mask rejection on return.
            D3DLOCKED_RECT lock{};api(f.textures[2]->LockRect(0,&lock,nullptr,0),"fade opaque return map lock");
            const DWORD color=0x40804020u;
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+4*x,&color,4);
            api(f.textures[2]->UnlockRect(0),"fade opaque return map unlock");
            std::printf("FADE_OPAQUE_RETURN frame=%llu matched=%u\n",f.frame,f.a.recorded);
        }
        const float ordinary_t=.03125f*float(plan%3);
        f.draw(f.a,ordinary_t,0,0,true,true,f.a.recorded,Alter::None,false);
        if(!f.distancefade_bench)std::printf("FADE_GEOMETRY frame=%llu ordinary_t=%.9g\n",f.frame,double(ordinary_t));
        const unsigned required=f.emission_status(f.d.p,16);
        // Reuse only the existing supplemental reference fields. In this new
        // mode their enable means the effective producer set, not emission alone.
        f.emissions_enabled=required!=0;
        if(f.distancefade_bench) {
            const unsigned count=plan<6?1:plan<12?4:16,sample=plan%6;
            bind_source(0,false,false,0,false);
            fence();LARGE_INTEGER begin,middle,end;QueryPerformanceCounter(&begin);
            for(unsigned i=0;i<count;++i){api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"timed original fade source");++submissions;}
            fence();QueryPerformanceCounter(&middle);
            api(f.d->SetDepthStencilSurface(nullptr),"fade timed terminal depth");api(f.d->StretchRect(f.back.p,nullptr,f.bloom_surface.p,nullptr,D3DTEXF_NONE),"fade timed TAA AgX publication");api(f.d->EndScene(),"fade timed EndScene");fence();QueryPerformanceCounter(&end);
            if(sample>=2)std::printf("FADE_TIMING width=%u height=%u count=%u sample=%u fade=%u emission=%u source_ms=%.9f terminal_ms=%.9f total_ms=%.9f\n",f.W,f.H,count,sample-2,f.distancefade_enabled,f.distancefade_emissions_enabled,1000.*double(middle.QuadPart-begin.QuadPart)/frequency.QuadPart,1000.*double(end.QuadPart-middle.QuadPart)/frequency.QuadPart,1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart);
            api(f.d->SetDepthStencilSurface(f.depth.p),"fade benchmark depth restore");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"fade benchmark Present");++f.frame;++f.frames_since_reset;continue;
        }
        unsigned issued=0,pair=0;bool first_emission=false,second_emission=false;
        if(!qualified){issued=plan?1:0;first_emission=plan==2;}
        else if(plan==1)issued=1;
        else if(plan>=2&&plan<14){pair=(plan-2)/2;issued=plan%2==0;}
        else if(plan==15||plan==21||plan==26)issued=1;
        else if(plan==16||plan==17||plan==18||plan==19||plan==20||plan==22||plan==23||plan==24||plan==25||plan==27||plan==28||plan==29){issued=2;first_emission=plan==18||plan==19||plan==28;second_emission=plan==17||plan==20||plan==22||plan==23||plan==24||plan==25||plan==27||plan==29;}
        if(qualified&&(plan==19||plan==20||plan==24||plan==29))issued=3;
        // A first-source failure can recover at the next frame Clear. A failed
        // original after published enhancement must retain export quarantine.
        const bool source_failure=qualified&&(plan==22||plan==29);
        std::vector<float> expected_mask(std::size_t(f.W)*f.H*4,0);
        auto before=scene();
        for(unsigned source=0;source<issued;++source) {
            const bool emission=source==1?second_emission:first_emission;
            const bool failed=source_failure&&source==(plan==22?0u:2u),zero=plan==1;
            // As in the qualified emission fixture, the second additive source
            // uses .25 so the FP16 destination sum remains exactly representable.
            const float emission_alpha=source>=2?.25f:.125f;
            const unsigned overlap=qualified&&plan==15?2:1;
            unsigned fault=qualified&&((plan==19||plan==20)&&source==1)?3:qualified&&plan==21?6:qualified&&plan==24&&source==1?7:0;
            const RECT rect=bind_source(pair,emission,zero,source,failed);
            if(qualified&&plan==26) {
                api(f.d->BeginStateBlock(),"fade BeginStateBlock");api(f.d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE),"fade recorded sampler");Com<IDirect3DStateBlock9> recorded;api(f.d->EndStateBlock(&recorded.p),"fade EndStateBlock");DWORD value=1;api(f.d->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&value),"fade record no immediate effect");require(value==FALSE,"fade recorded sampler remains unapplied");
                Com<IDirect3DStateBlock9> all;api(f.d->CreateStateBlock(D3DSBT_ALL,&all.p),"fade capture stateblock");api(f.d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE),"fade transient sampler");api(all->Apply(),"fade stateblock Apply");
            }
            {Com<IDirect3DSurface9> logical;api(f.d->GetRenderTarget(0,&logical.p),"fade source snapshot application view");}
            const auto state=f.snapshot();
            float native_vs[48][4]{};api(f.d->GetVertexShaderConstantF(0,native_vs[0],48),"fade native VS constant snapshot");
            constexpr D3DRENDERSTATETYPE blend_states[]={D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA};
            DWORD blend_before[7]{};for(unsigned i=0;i<7;++i)api(f.d->GetRenderState(blend_states[i],&blend_before[i]),"fade caller blend snapshot");
            std::vector<float> motion_before,depth_before,mask_before;
            raw(1,motion_before);raw(2,depth_before);const HRESULT mask_hr_before=raw(3,mask_before);
            const unsigned prior_calls=f.emission_status(f.d.p,12),prior_linear=f.emission_status(f.d.p,5),prior_native=f.emission_status(f.d.p,6),prior_prepared=f.emission_status(f.d.p,4),prior_mask=f.emission_status(f.d.p,1);
            if(!(emission?f.distancefade_emissions_enabled:f.distancefade_enabled))fault=0;
            if(fault)f.emission_fault(f.d.p,fault,1);
            const HRESULT hr=f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2*overlap);
            ++submissions;++f.draw_index;
            require(failed?FAILED(hr):SUCCEEDED(hr),"fade original source HRESULT");
            f.compare(state,f.snapshot(),"distance fade source restoration");
            float restored_vs[48][4]{};api(f.d->GetVertexShaderConstantF(0,restored_vs[0],48),"fade native VS constant readback");require(!std::memcmp(native_vs,restored_vs,sizeof native_vs),"fade native VS constants restored");
            for(unsigned i=0;i<7;++i){DWORD value=0;api(f.d->GetRenderState(blend_states[i],&value),"fade caller blend readback");require(value==blend_before[i],"fade exact caller blend restoration");}
            const unsigned actual_calls=f.emission_status(f.d.p,12)-prior_calls,linear=f.emission_status(f.d.p,5)-prior_linear,native=f.emission_status(f.d.p,6)-prior_native,prepared=f.emission_status(f.d.p,4)-prior_prepared;
            require(actual_calls==1,"fade actual native source once");
            auto after=scene();
            std::vector<float> motion_after,depth_after,mask_after;
            raw(1,motion_after);raw(2,depth_after);const HRESULT mask_hr_after=raw(3,mask_after);
            require(motion_before==motion_after&&depth_before==depth_after,"fade source preserves ordinary RT1 RT2 exactly");
            for(unsigned y=0;y<f.H;++y)for(unsigned x=0;x<f.W;++x) {
                const unsigned i=(y*f.W+x)*4;const bool covered=LONG(x)>=rect.left&&LONG(x)<rect.right&&LONG(y)>=rect.top&&LONG(y)<rect.bottom;
                for(unsigned k=0;k<4;++k)require_quiet(std::isfinite(after[i+k]),"fade finite actual scene");
                if(!covered||failed||zero)require_quiet(!std::memcmp(&before[i],&after[i],16),"fade excluded and zero-alpha raw A exact");
                else if(!emission)require_quiet(after[i+3]==before[i+3],"fade native destination alpha exact");
                else if(f.hdr)require_quiet(after[i+3]==before[i+3]+emission_alpha,"fade mixed emission alpha source once");
                if(SUCCEEDED(mask_hr_before)&&SUCCEEDED(mask_hr_after)&&mask_before[i]>0)require_quiet(mask_after[i]>0,"fade earlier shared mask footprint retained");
                if(covered&&!failed&&(linear||native))for(unsigned k=0;k<3;++k)expected_mask[i+k]=1;
            }
            // Small independent numerical witnesses; Python obtains L from the
            // existing Asteroid oracle, never from the composed output.
            const bool corpus_sample=!emission&&(plan==1||(plan>=2&&plan<14&&plan%2==0));
            const bool mixed_sample=qualified&&plan>=15&&plan<=18;
            if(corpus_sample||mixed_sample) {
                const unsigned sample_x[]={f.W/4,f.W/2};
                for(unsigned sample=mixed_sample?1:0;sample<2;++sample) {
                    const unsigned x=sample_x[sample],y=f.H/2,i=(y*f.W+x)*4;
                    if(source==0)for(unsigned k=0;k<3;++k)require(before[i+k]==1.f,"fade independent witness known unit background");
                    std::printf("FADE_SAMPLE frame=%llu source=%u kind=%s overlap=%u pair=%u x=%u y=%u alpha=%.9g before=%.17g,%.17g,%.17g,%.17g after=%.17g,%.17g,%.17g,%.17g\n",f.frame,source,emission?"emission":"fade",overlap,pair,x,y,emission?double(emission_alpha):zero?0.:.078125,before[i],before[i+1],before[i+2],before[i+3],after[i],after[i+1],after[i+2],after[i+3]);
                }
            }
            std::printf("FADE_SOURCE frame=%llu source=%u kind=%s pair=%u alpha=%.9g overlap=%u fault=%u hr=%08lx original_calls=%u prepared=%u linear=%u native=%u mask_before=%u mask_after=%u hash_mask_before=%016llx hash_mask_after=%016llx\n",f.frame,source,emission?"emission":"fade",pair,emission?double(emission_alpha):zero?0.:.078125,overlap,fault,hr,actual_calls,prepared,linear,native,prior_mask,f.emission_status(f.d.p,1),static_cast<unsigned long long>(hash(mask_before)),static_cast<unsigned long long>(hash(mask_after)));
            before=std::move(after);
        }
        f.emission_reference_color=scene();
        raw(3,f.emission_reference_mask);
        f.emission_mask_valid=required&&f.emission_status(f.d.p,1);
        if(f.emission_mask_valid)for(std::size_t i=0;i<expected_mask.size();i+=4)require_quiet((f.emission_reference_mask[i]>0)==(expected_mask[i]>0),"fade canonical combined producer union");
        if(qualified){write_raw("color",f.emission_reference_color);write_raw("mask",f.emission_reference_mask);}
        std::vector<float> alpha(std::size_t(f.W)*f.H),motion,depth;
        for(std::size_t i=0;i<alpha.size();++i)alpha[i]=f.emission_reference_color[4*i+3];
        raw(1,motion);raw(2,depth);
        std::printf("FADE_LIVE frame=%llu fade=%u emission=%u draws=%u",f.frame,f.distancefade_enabled,f.distancefade_emissions_enabled,issued);
        for(unsigned i=0;i<22;++i)std::printf(" s%u=%u",i,f.emission_status(f.d.p,i));
        std::printf(" hash_alpha=%016llx hash_motion=%016llx hash_depth=%016llx hash_mask=%016llx\n",static_cast<unsigned long long>(hash(alpha)),static_cast<unsigned long long>(hash(motion)),static_cast<unsigned long long>(hash(depth)),static_cast<unsigned long long>(hash(f.emission_reference_mask)));
        if(!qualified) {
            require(f.emission_status(f.d.p,15)==0,"fade missing prerequisite never enhances");
            api(f.d->EndScene(),"fade admission-control EndScene");f.verify_motion();api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"fade admission-control Present");++f.frame;++f.frames_since_reset;
        } else if(source_failure) {
            f.scene_rejected=true;api(f.d->SetDepthStencilSurface(nullptr),"fade rejected depth unbind");
            const auto state=f.snapshot();api(f.d->StretchRect(f.back.p,nullptr,f.bloom_surface.p,nullptr,D3DTEXF_NONE),"fade rejected native copy");f.compare(state,f.snapshot(),"fade rejected copy state");
            const auto image=f.color_image();require(image==f.color_image(f.bloom_surface.p),"fade rejected native copy exact");
            f.reference.pass.invalidate();f.history_dropped=true;f.camera_history={};++taa_skipped_frames;
            api(f.d->EndScene(),"fade rejected EndScene");f.verify_motion();api(f.d->SetDepthStencilSurface(f.depth.p),"fade rejected depth restore");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"fade rejected Present");++f.frame;++f.frames_since_reset;
            std::printf("FADE_REJECTED frame=%u source_failed=1 taa=0 history_seeded=0 copy_exact=1\n",plan);
        } else f.frame_end();
        if(qualified&&plan==29) {
            const unsigned quarantine=f.emission_status(f.d.p,3),state_lost=f.emission_status(f.d.p,2);
            std::printf("FADE_EXPORT frame=%u quarantine=%u state_lost=%u\n",plan,quarantine,state_lost);
            require(quarantine==unsigned(required!=0)&&state_lost==0,"fade failed export quarantines earlier enhancement without state loss");
        }
        if(qualified&&(plan==26||plan==29)) {
            const unsigned refs_before=f.emission_status(f.d.p,20);
            f.reset();const unsigned refs_after=f.emission_status(f.d.p,20);
            std::printf("FADE_RESET frame=%u refs=%u allocations=%u quarantine=%u state_lost=%u\n",plan,refs_after,f.emission_status(f.d.p,21),f.emission_status(f.d.p,3),f.emission_status(f.d.p,2));
            require(refs_before>=4?refs_after==refs_before-4:refs_before==0&&refs_after==0,"fade Reset releases four targets and retains reusable programs");
            require(f.emission_status(f.d.p,3)==unsigned(plan==29&&required!=0)&&f.emission_status(f.d.p,2)==0,"fade Reset clears state loss and preserves export quarantine");
        }
    }
    api(f.d->SetIndices(nullptr),"fade final index release");api(f.d->SetStreamSource(0,nullptr,0,0),"fade final stream release");
    std::printf("FADE_CHECKS frames=%u submissions=%u qualified=%u benchmark=%u\n",frames,submissions,qualified,f.distancefade_bench);
}
