// Live packed screen emission (docs/architecture/screen-emission-region.md,
// step C): the game's row-19 bullet pair vs_5e484a06672e28fb/ps_ec1f5c4a2f4e1445
// drawn non-indexed from a DISCARD-locked dynamic buffer filled like the
// bullet writer (FLOAT3 world position, FLOAT2 uv, D3DCOLOR; stride 24;
// whole-buffer lock; StartVertex 0), in the native screen state
// (ADD, ONE/INVSRCCOLOR, mask 15, Z-write off, alpha test on), mixed with
// one Asteroid fade source (pair 0) and the additive emission source of the
// fade fixture. The fixture owns original inputs, state/pixel witnesses and
// scheduling; Python holds the packed-law oracle. The bullet VS transforms
// world positions through c0-3 (g_mViewProjection): the rows are the
// identity, so positions are clip coordinates and the quad footprint is
// exact in pixels.
void run_screen_emission_integration(Fixture& f,const char* original_path) {
    require(f.seam&&f.enabled&&f.emission_status&&f.emission_fault&&f.emission_readback,"screen emission live seam");
    const bool qualified=f.taa&&f.hdr&&f.hdr_agx;
    const auto sibling=[&](const char* stage,const char* hash) {
        const std::string supplied(original_path);const auto slash=supplied.find_last_of("/\\");
        require(slash!=std::string::npos,"screen emission original directory");
        return supplied.substr(0,slash+1)+stage+"_"+hash+".bin";
    };
    const auto original_vs=[&](const char* hash,Com<IDirect3DVertexShader9>& out) {
        const auto code=load(sibling("vs",hash).c_str());
        require(fnv(code.data(),code.size()*4)==std::strtoull(hash,nullptr,16),"screen original VS identity");
        api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&out.p),"screen original VS");
    };
    const auto original_ps=[&](const char* hash,Com<IDirect3DPixelShader9>& out) {
        const auto code=load(sibling("ps",hash).c_str());
        require(fnv(code.data(),code.size()*4)==std::strtoull(hash,nullptr,16),"screen original PS identity");
        api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&out.p),"screen original PS");
    };
    Com<IDirect3DVertexShader9> bullet_vs,fade_vs,emission_vs;Com<IDirect3DPixelShader9> bullet_ps,fade_ps,emission_ps;
    original_vs("5e484a06672e28fb",bullet_vs);original_ps("ec1f5c4a2f4e1445",bullet_ps);
    if(!f.screenemission_bench) {
        original_vs("b0602757fce6e870",fade_vs);original_ps("517540ae6d5e5410",fade_ps);
        original_vs("d5e1c75351ed3f04",emission_vs);original_ps("8360f422de08b5bd",emission_ps);
    }
    // The bullet writer's layout and buffer (0x00608d58; 147456 bytes, whole
    // DISCARD lock, count*24 bytes valid, StartVertex 0).
    constexpr unsigned stride=24,max_vertices=6144,buffer_bytes=max_vertices*stride;
    const D3DVERTEXELEMENT9 bullet_elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,20,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    Com<IDirect3DVertexDeclaration9> bullet_declaration;Com<IDirect3DVertexBuffer9> bullets;
    api(f.d->CreateVertexDeclaration(bullet_elements,&bullet_declaration.p),"bullet declaration");
    const auto create_bullets=[&](){api(f.d->CreateVertexBuffer(buffer_bytes,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&bullets.p,nullptr),"bullet buffer");};
    create_bullets();
    // Quad footprint in clip space (rows = identity): functional script
    // x in [0,.5], y in [-.25,.25] -> pixels [W/2,3W/4) x [3H/8,5H/8);
    // bench: a small centred quad (+-.05: 96x54 px at 1080p, 64x38 at 1280x768).
    // X3M_SCREEN_EMISSION_ADDITIVE=G (the DLL's own switch; the packed option
    // off): the additive script, frames "" / ks / s / p. k is the bullet pair
    // drawn opaque (blend off: a different state, refused) with a near-black
    // texel, so the bolt of frame 1 lands on a dark quad; the bolt of frame 2
    // lands on the white scene (bright, > .8); the PROJECTED kind refuses to
    // native. The raw before/after images of every additive bolt are written
    // for the runner's G q + D law.
    char additive_setting[32]{};
    const float additive_gain=GetEnvironmentVariableA("X3M_SCREEN_EMISSION_ADDITIVE",additive_setting,sizeof additive_setting)>0?float(std::atof(additive_setting)):0.f;
    const bool additive=additive_gain>=1.f&&!f.screen_enabled; // the DLL's domain (1..8)
    const float qx0=f.screenemission_bench?-.05f:0.f,qx1=f.screenemission_bench?.05f:.5f,qy0=f.screenemission_bench?-.05f:-.25f,qy1=f.screenemission_bench?.05f:.25f,qz=.1f;
    const RECT quad_rect{LONG(std::lround((qx0+1)*f.W/2.)),LONG(std::lround((1-qy1)*f.H/2.)),LONG(std::lround((qx1+1)*f.W/2.)),LONG(std::lround((1-qy0)*f.H/2.))};
    // Step E overlap chain (kind c): `chain_quads` copies of the functional
    // quad in ONE non-indexed DIP, each shifted `chain_step_px` pixels along
    // x from `chain_origin` (inside the fade scissor, whose Asteroid draw in
    // the same frame leaves a non-white A under the chain: the screen blend
    // onto white is white) and textured with a soft sprite (uv corners 0..1
    // over the quad), so a pixel of the chain accumulates up to eight dim
    // fragments exactly as a run-17 bolt does; the raw before/after images
    // of the chain draw are written for the runner's native-twin comparison.
    constexpr unsigned chain_quads=8,chain_step_px=2,sprite_size=16;
    const float chain_step=chain_step_px*2.f/float(f.W),chain_origin=-.75f;
    const RECT chain_rect{LONG(std::lround((chain_origin+1)*f.W/2.)),quad_rect.top,LONG(std::lround((chain_origin+1)*f.W/2.))+(quad_rect.right-quad_rect.left)+LONG(chain_step_px*(chain_quads-1)),quad_rect.bottom};
    // Near-plane kinds (screen-emission-region.md, step B): rows x' = x,
    // y' = y, z' = .1 (z - 1), w = z put the D3D near plane at w = 1; the
    // vertices below are clip-space (x, y, w) triples. n: one triangle with a
    // vertex behind the camera (second triangle degenerate), visible as the
    // trapezoid NDC (0,.25),(.5,.25),(.75,.375),(0,.375); x: the quad with its
    // near edge exactly on the near plane (footprint = the functional quad);
    // h: the quad entirely behind (nothing rasterised, refused BehindNear);
    // b: a beam from the near plane to w = 1000 (footprint = the quad).
    const auto near_kind=[](char kind){return kind=='n'||kind=='x'||kind=='h'||kind=='b';};
    const float near_rows[16]={1,0,0,0, 0,1,0,0, 0,0,.1f,-.1f, 0,0,1,0};
    const float near_vertices[4][6][3]={
        {{0,0,-1},{0,.75f,3},{1.5f,.75f,3},{0,.75f,3},{0,.75f,3},{0,.75f,3}},                       // n
        {{0,-.25f,1},{.5f,-.25f,1},{0,.75f,3},{.5f,-.25f,1},{1.5f,.75f,3},{0,.75f,3}},               // x
        {{0,-.25f,-1},{.5f,-.25f,-1},{0,.75f,-3},{.5f,-.25f,-1},{1.5f,.75f,-3},{0,.75f,-3}},         // h
        {{0,-.25f,1},{.5f,-.25f,1},{0,250,1000},{.5f,-.25f,1},{500,250,1000},{0,250,1000}}};         // b
    const auto near_index=[](char kind){return kind=='n'?0u:kind=='x'?1u:kind=='h'?2u:3u;};
    // Pixel footprint the rasteriser can touch, per kind (64x64 functional
    // script): the trapezoid's bounding box for n, empty for h, the quad else.
    const auto kind_rect=[&](char kind) {
        if(kind=='n')return RECT{LONG(f.W/2),LONG(std::lround(.625*f.H/2.)),LONG(std::lround(1.75*f.W/2.)),LONG(std::lround(.75*f.H/2.))};
        if(kind=='h')return RECT{0,0,0,0};
        if(kind=='c')return chain_rect;
        return quad_rect;
    };
    // Whether the source of `kind` rasterises pixel (x, y): the n trapezoid
    // under the D3D9 convention (pixel centres at integer coordinates, the
    // top-left fill rule: top and left edges inclusive, bottom and right
    // exclusive, as the functional quad's edges already rely on), the
    // rectangle else.
    const auto covered_source=[&](char kind,const RECT& rect,unsigned x,unsigned y) {
        if(kind!='n')return LONG(x)>=rect.left&&LONG(x)<rect.right&&LONG(y)>=rect.top&&LONG(y)<rect.bottom;
        const double u=x/(f.W/2.)-1,v=1-y/(f.H/2.);
        return v>.25&&v<=.375&&u>=0&&u<.5+2*(v-.25);
    };
    // The writer: whole-buffer DISCARD lock, the stale tail first, then
    // `quads` copies of the quad (two triangles each) from vertex 0; a
    // near-plane kind writes its six vertices once.
    const auto write_bullets=[&](unsigned quads,char kind='s') {
        void* data=nullptr;api(bullets->Lock(0,buffer_bytes,&data,D3DLOCK_DISCARD),"bullet discard lock");
        auto* words=static_cast<float*>(data);for(unsigned n=0;n<buffer_bytes/4;++n)words[n]=0.f;
        const float corners[6][2]={{qx0,qy0},{qx1,qy0},{qx0,qy1},{qx1,qy0},{qx1,qy1},{qx0,qy1}};
        if(kind=='c')quads=chain_quads;
        for(unsigned q=0;q<quads;++q)for(unsigned k=0;k<6;++k) {
            auto* v=static_cast<unsigned char*>(data)+(q*6+k)*stride;
            const float shift=kind=='c'?chain_origin-qx0+chain_step*float(q):0.f;
            const float position[3]={corners[k][0]+shift,corners[k][1],qz};
            const float uv[2]={kind=='c'?(corners[k][0]==qx1?1.f:0.f):.5f,kind=='c'?(corners[k][1]==qy0?1.f:0.f):.5f};const DWORD colour=0xffffffffu; // h = COLOR0.w = 1
            if(near_kind(kind))std::memcpy(v,near_vertices[near_index(kind)][k],12);else std::memcpy(v,position,12);
            std::memcpy(v+12,uv,8);std::memcpy(v+20,&colour,4);
        }
        api(bullets->Unlock(),"bullet discard unlock");
    };
    // Textures: the bullet texel (q = T * h, native alpha a = T.a), the
    // Asteroid pair-0 texels and the emission texel of the fade fixture.
    const float bullet_texel[4]={.5f,.25f,.125f,.5f},dark_texel[4]={.05f,.05f,.05f,.5f}; // dark: the opaque k draw of the additive script
    const float texels[][4]={{.5f,.25f,.75f,.5f},{.25f,.375f,.75f,.625f},{.25f,.875f,.125f,.75f},{.125f,.25f,.0625f,.25f},{.5f,.25f,.75f,0},{.5f,.25f,.125f,.125f}};
    Com<IDirect3DTexture9> bullet_texture,dark_texture,textures[6];
    const auto texel=[&](const float* value,Com<IDirect3DTexture9>& out) {
        api(f.d->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&out.p,nullptr),"screen source texture");
        D3DLOCKED_RECT lock{};api(out->LockRect(0,&lock,nullptr,0),"screen texture lock");std::memcpy(lock.pBits,value,16);api(out->UnlockRect(0),"screen texture unlock");
    };
    texel(bullet_texel,bullet_texture);texel(dark_texel,dark_texture);for(unsigned i=0;i<6;++i)texel(texels[i],textures[i]);
    // The soft sprite of the chain: tint (.55, 1, .45) times a Gaussian of
    // sigma 3 px about the sprite centre, opaque alpha (the native alpha test
    // passes everywhere, so the chain footprint is the whole union).
    Com<IDirect3DTexture9> chain_texture;
    {
        api(f.d->CreateTexture(sprite_size,sprite_size,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&chain_texture.p,nullptr),"chain sprite texture");
        D3DLOCKED_RECT lock{};api(chain_texture->LockRect(0,&lock,nullptr,0),"chain sprite lock");
        const float tint[3]={.55f,1.f,.45f},sigma=3.f,centre=(sprite_size-1)/2.f;
        for(unsigned y=0;y<sprite_size;++y)for(unsigned x=0;x<sprite_size;++x) {
            const float dx=float(x)-centre,dy=float(y)-centre,g=std::exp(-(dx*dx+dy*dy)/(2*sigma*sigma));
            float* t=reinterpret_cast<float*>(static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch)+4*x;
            for(unsigned c=0;c<3;++c)t[c]=tint[c]*g;
            t[3]=1.f;
        }
        api(chain_texture->UnlockRect(0),"chain sprite unlock");
    }
    // The fade/emission quad and indices of the fade fixture.
    struct SourceVertex {float p[3],uv[2],n[3],b[3],t[3];};
    const float l=-1-1.f/f.W,r=1-1.f/f.W,t=1+1.f/f.H,b=-1+1.f/f.H;
    const SourceVertex quad[]={{{l,t,.1f},{0,0},{0,0,1},{0,1,0},{1,0,0}},{{r,t,.1f},{1,0},{0,0,1},{0,1,0},{1,0,0}},{{l,b,.1f},{0,1},{0,0,1},{0,1,0},{1,0,0}},{{r,b,.1f},{1,1},{0,0,1},{0,1,0},{1,0,0}}};
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,20,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},{0,44,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},D3DDECL_END()};
    Com<IDirect3DVertexDeclaration9> declaration;Com<IDirect3DVertexBuffer9> vertices;Com<IDirect3DIndexBuffer9> indices;
    api(f.d->CreateVertexDeclaration(elements,&declaration.p),"screen fade declaration");
    api(f.d->CreateVertexBuffer(sizeof quad,0,0,D3DPOOL_MANAGED,&vertices.p,nullptr),"screen fade vertices");
    void* data=nullptr;api(vertices->Lock(0,0,&data,0),"screen fade vertex lock");std::memcpy(data,quad,sizeof quad);api(vertices->Unlock(),"screen fade vertex unlock");
    const unsigned short triangles[]={0,1,2,2,1,3};
    api(f.d->CreateIndexBuffer(sizeof triangles,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr),"screen fade indices");
    api(indices->Lock(0,0,&data,0),"screen fade index lock");std::memcpy(data,triangles,sizeof triangles);api(indices->Unlock(),"screen fade index unlock");
    const auto raw=[&](unsigned target,std::vector<float>& image) {
        unsigned w=0,h=0;image.assign(std::size_t(f.W)*f.H*(target==2?1:4),0);
        const HRESULT hr=f.emission_readback(f.d.p,target,image.data(),unsigned(image.size()),&w,&h);
        require(hr==D3DERR_NOTFOUND||SUCCEEDED(hr),"screen raw supplemental readback");
        if(SUCCEEDED(hr))require(w==f.W&&h==f.H,"screen raw dimensions");
        return hr;
    };
    const auto scene=[&]() {
        Com<IDirect3DSurface9> logical;api(f.d->GetRenderTarget(0,&logical.p),"screen application RT view");
        if(!f.hdr){const auto image=f.color_image();std::vector<float> out(image.size()*4);for(std::size_t i=0;i<image.size();++i)for(unsigned c=0;c<4;++c)out[4*i+c]=float((image[i]>>(c==0?16:c==1?8:c==2?0:24))&255)/255.f;return out;}
        unsigned w=0,h=0;auto image=f.hdr_image(&w,&h);require(w==f.W&&h==f.H,"screen HDR image dimensions");return image;
    };
    const auto hash=[](const std::vector<float>& image){return fnv(image.data(),image.size()*sizeof(float));};
    // M's red lane alone: the live coverage; alpha is the packed bracket's
    // per-bracket scratch (seeded by the plane init even when the source
    // bind then fails; screen-emission-region.md, step A deviations).
    const auto hash_red=[](const std::vector<float>& image){std::vector<float> red(image.size()/4);for(std::size_t i=0;i<red.size();++i)red[i]=image[4*i];return fnv(red.data(),red.size()*sizeof(float));};
    const auto write_raw=[&](const char* kind,const std::vector<float>& image) {
        char name[96];std::snprintf(name,sizeof name,"screen_emission_%s_%llu.rgba32f",kind,f.frame);
        FILE* file=std::fopen(name,"wb");require(file!=nullptr,"screen raw output");require(std::fwrite(image.data(),sizeof(float),image.size(),file)==image.size(),"screen raw bytes");std::fclose(file);
    };
    const auto common_sampler=[&](unsigned stage) {
        for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(f.d->SetSamplerState(stage,filter,D3DTEXF_POINT),"screen point sampling");
        api(f.d->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"screen no mip");api(f.d->SetSamplerState(stage,D3DSAMP_SRGBTEXTURE,FALSE),"screen numeric sampler");
    };
    // The bullet draw state: the captured screen state (effects ledger row
    // 19, 19 of 54 draws alpha-tested), Z test on, no scissor.
    // `kind`: s the admitted state; p PROJECTED on stage 0; g sRGB on
    // sampler 0; d DITHERENABLE on. p/g refuse as readiness, d as a
    // different (pair) state; all three draw natively.
    const auto bind_bullets=[&](char kind) {
        const bool projected=kind=='p';
        f.scope(nullptr);f.scene_states();
        api(f.d->SetVertexDeclaration(bullet_declaration.p),"bullet declaration bind");api(f.d->SetStreamSource(0,bullets.p,0,stride),"bullet stream");
        api(f.d->SetStreamSourceFreq(0,1),"bullet frequency");api(f.d->SetIndices(nullptr),"bullet no indices");
        api(f.d->SetVertexShader(bullet_vs.p),"bullet VS bind");api(f.d->SetPixelShader(bullet_ps.p),"bullet PS bind");
        const float rows[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};api(f.d->SetVertexShaderConstantF(0,near_kind(kind)?near_rows:rows,4),"bullet g_mViewProjection");
        for(unsigned stage=0;stage<7;++stage){api(f.d->SetTexture(stage,stage?nullptr:static_cast<IDirect3DBaseTexture9*>(kind=='c'?chain_texture.p:bullet_texture.p)),"bullet diffuse");common_sampler(stage);}
        api(f.d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,kind=='g'?TRUE:FALSE),"bullet sampler 0 srgb");
        api(f.d->SetRenderState(D3DRS_DITHERENABLE,kind=='d'?TRUE:FALSE),"bullet dither");
        api(f.d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,projected?D3DTTFF_PROJECTED|D3DTTFF_COUNT3:D3DTTFF_DISABLE),"bullet stage 0 transform");
        api(f.d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"bullet no depth write");api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"bullet blending");
        api(f.d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE),"bullet ONE");api(f.d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCCOLOR),"bullet INVSRCCOLOR");api(f.d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"bullet ADD");
        api(f.d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"bullet mask 15");api(f.d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE),"bullet alpha test");
        api(f.d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL),"bullet alpha func");api(f.d->SetRenderState(D3DRS_ALPHAREF,1),"bullet alpha ref");
        api(f.d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE),"bullet no scissor");
        if(kind=='k'){api(f.d->SetTexture(0,dark_texture.p),"dark bullet texel");api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"dark bullet opaque");}
        return kind_rect(kind);
    };
    // The fade fixture's Asteroid pair 0 (run_linear_distance_fade case 0 inputs)
    // and its additive emission source, both under the quarter-viewport scissor.
    const auto bind_source=[&](bool emission) {
        f.scope(nullptr);f.scene_states();
        api(f.d->SetVertexDeclaration(declaration.p),"screen fade declaration bind");api(f.d->SetStreamSource(0,vertices.p,0,sizeof(SourceVertex)),"screen fade stream");
        api(f.d->SetStreamSourceFreq(0,1),"screen fade frequency");api(f.d->SetIndices(indices.p),"screen fade indices");
        api(f.d->SetVertexShader(emission?emission_vs.p:fade_vs.p),"screen fade VS bind");api(f.d->SetPixelShader(emission?emission_ps.p:fade_ps.p),"screen fade PS bind");
        float vc[48][4]{},pc[12][4]{};
        if(emission){for(unsigned i=0;i<4;++i)vc[i][i]=1;vc[10][0]=vc[11][1]=1;vc[12][0]=.5f;pc[0][0]=pc[1][1]=pc[2][2]=1;}
        else {
            const unsigned matrix=24,normal=31,camera=34,uv=37,alpha=39,emissive=40,point=0;
            for(unsigned i=0;i<4;++i)vc[matrix+i][i]=1;
            for(unsigned i=0;i<3;++i)vc[normal+i][i]=1;
            vc[camera+2][3]=4;vc[uv][2]=.0625f;vc[uv+1][2]=.1875f;
            vc[alpha][0]=.625f;vc[emissive][0]=.25f;vc[emissive][1]=.125f;vc[emissive][2]=.0625f;
            vc[41][0]=.75f;vc[41][1]=.125f;
            vc[point][2]=2;vc[point+1][0]=.5f;vc[point+1][1]=.25f;vc[point+1][2]=.125f;vc[point+2][0]=2;vc[point+2][1]=.25f;vc[point+2][2]=.125f;
            pc[0][2]=1;pc[1][0]=.375f;pc[1][1]=.25f;pc[1][2]=.5f;pc[2][2]=-1;pc[3][0]=.125f;pc[3][1]=.5f;pc[3][2]=.25f;pc[4][0]=.5f;pc[5][0]=1;
        }
        api(f.d->SetVertexShaderConstantF(0,vc[0],48),"screen fade VS inputs");api(f.d->SetPixelShaderConstantF(0,pc[0],12),"screen fade PS inputs");
        const int count[4]={1,0,1,0};const BOOL fog=!emission;api(f.d->SetVertexShaderConstantI(0,count,1),"screen fade point count");api(f.d->SetVertexShaderConstantB(0,&fog,1),"screen fade fog");
        for(unsigned stage=0;stage<7;++stage) {
            IDirect3DBaseTexture9* texture=nullptr;
            if(emission){if(stage==0)texture=textures[5].p;}
            else if(stage==0)texture=textures[0].p;else if(stage==1)texture=textures[2].p;else if(stage==2)texture=textures[3].p;
            api(f.d->SetTexture(stage,texture),"screen fade sampler role");common_sampler(stage);
        }
        api(f.d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE),"screen fade stage 0 transform");
        api(f.d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"screen fade no depth write");api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"screen fade blending");
        api(f.d->SetRenderState(D3DRS_SRCBLEND,emission?D3DBLEND_ONE:D3DBLEND_SRCALPHA),"screen fade blend source");api(f.d->SetRenderState(D3DRS_DESTBLEND,emission?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA),"screen fade blend destination");
        api(f.d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"screen fade additive operation");api(f.d->SetRenderState(D3DRS_COLORWRITEENABLE,emission?15:7),"screen fade mask");
        const RECT rect{LONG(f.W/8),LONG(f.H/4),LONG(5*f.W/8),LONG(3*f.H/4)};
        api(f.d->SetScissorRect(&rect),"screen fade scissor");api(f.d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"screen fade scissor enable");
        return rect;
    };
    // X3M_FIXTURE_SCREEN_RECT (seam only): the injected bound rectangle of the
    // straddling case; the bracket composes only inside it.
    char rect_setting[64]{};long il=0,it=0,ir=0,ib=0;
    const bool injected=GetEnvironmentVariableA("X3M_FIXTURE_SCREEN_RECT",rect_setting,sizeof rect_setting)>0&&std::sscanf(rect_setting,"%ld,%ld,%ld,%ld",&il,&it,&ir,&ib)==4;
    const RECT injected_rect{il,it,ir,ib};
    // X3M_FIXTURE_SCREEN_CAPS_FAULT=1 (seam only): the caps-refusal case; no
    // pass fault is queued for a screen draw that never reaches the pass.
    char caps_setting[8]{};
    const bool caps_fault=GetEnvironmentVariableA("X3M_FIXTURE_SCREEN_CAPS_FAULT",caps_setting,sizeof caps_setting)==1&&caps_setting[0]=='1';
    Com<IDirect3DQuery9> completion;LARGE_INTEGER frequency{};
    if(f.screenemission_bench){require(qualified,"screen benchmark complete activation");api(f.d->CreateQuery(D3DQUERYTYPE_EVENT,&completion.p),"screen benchmark EVENT");require(QueryPerformanceFrequency(&frequency),"screen benchmark QPC");}
    const auto fence=[&](){api(completion->Issue(D3DISSUE_END),"screen timed EVENT issue");f.wait(completion.p);};
    // Bolt footprint shape refusals (mode boltshape; docs/architecture/
    // bolt-footprint.md, "Shape refusal telemetry"): X3M_FIXTURE_BOLT_SHAPE=
    // prims draws, per frame 1-300, one bolt batch above the scan bound
    // (1100 quads = 2200 primitives on odd frames, 1025 quads = 2050 on even
    // frames) from the fixture's own DISCARD-locked buffer of 1024 x 72 x 24
    // bytes (sized after the part buffer Run 78 A inferred; not measured); =decl draws one quad whose declaration puts
    // POSITION FLOAT3 at offset 12 (uv at 0, colour at 8; stride still 24).
    // Both then draw one ordinary bolt from the writer's 147456-byte buffer,
    // which must pass the shape clause. Every bullet draw is admitted by the
    // additive route (G = X3M_SCREEN_EMISSION_ADDITIVE); 301 Presents close
    // one 300-frame bolt_footprint window. The runner reads the session log.
    char bolt_shape_setting[8]{};
    const DWORD bolt_shape_length=GetEnvironmentVariableA("X3M_FIXTURE_BOLT_SHAPE",bolt_shape_setting,sizeof bolt_shape_setting);
    if(f.boltshape) {
        const bool oversize=bolt_shape_length==5&&!std::strcmp(bolt_shape_setting,"prims"),misdeclared=bolt_shape_length==4&&!std::strcmp(bolt_shape_setting,"decl");
        require(oversize||misdeclared,"X3M_FIXTURE_BOLT_SHAPE is prims or decl");
        require(qualified&&additive,"bolt shape script needs the qualified additive configuration");
        constexpr unsigned part_bytes=1024u*72u*stride,big_quads=1100,small_quads=1025; // 6600 and 6150 vertices: both above max_vertices
        static_assert(small_quads*6>max_vertices&&big_quads*6*stride<=part_bytes,"both batches exceed the scan bound and fit the part buffer");
        const D3DVERTEXELEMENT9 shifted_elements[]={{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,8,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        Com<IDirect3DVertexDeclaration9> shifted_declaration;Com<IDirect3DVertexBuffer9> odd;
        api(f.d->CreateVertexDeclaration(shifted_elements,&shifted_declaration.p),"bolt shape shifted declaration");
        const unsigned odd_bytes=oversize?part_bytes:buffer_bytes;
        api(f.d->CreateVertexBuffer(odd_bytes,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&odd.p,nullptr),"bolt shape buffer");
        const float corners[6][2]={{qx0,qy0},{qx1,qy0},{qx0,qy1},{qx1,qy0},{qx1,qy1},{qx0,qy1}};
        const auto write_odd=[&](unsigned quads) {
            void* data=nullptr;api(odd->Lock(0,0,&data,D3DLOCK_DISCARD),"bolt shape discard lock");
            for(unsigned q=0;q<quads;++q)for(unsigned k=0;k<6;++k) {
                auto* v=static_cast<unsigned char*>(data)+(q*6+k)*stride;
                const float position[3]={corners[k][0],corners[k][1],qz},uv[2]={.5f,.5f};const DWORD colour=0xffffffffu;
                if(misdeclared){std::memcpy(v,uv,8);std::memcpy(v+8,&colour,4);std::memcpy(v+12,position,12);}
                else {std::memcpy(v,position,12);std::memcpy(v+12,uv,8);std::memcpy(v+20,&colour,4);}
            }
            api(odd->Unlock(),"bolt shape discard unlock");
        };
        constexpr unsigned bolt_frames=301;
        unsigned odd_admitted=0,plain_admitted=0,max_primitives=0,bolt_submissions=0;
        for(unsigned plan=0;plan<bolt_frames;++plan) {
            f.frame_begin();f.linear_material_inputs();f.write_reserved();
            f.draw(f.a,.03125f*float(plan%3),0,0,true,true,f.a.recorded,Alter::None,false);
            const unsigned required=f.emission_status(f.d.p,16);
            f.emissions_enabled=required!=0;
            if(plan) {
                const unsigned quads=misdeclared?1u:plan%2?big_quads:small_quads;
                for(unsigned draw=0;draw<2;++draw) {
                    const bool shaped=draw==0;
                    if(shaped)write_odd(quads);else write_bullets(1);
                    bind_bullets('s');
                    if(shaped){api(f.d->SetStreamSource(0,odd.p,0,stride),"bolt shape stream");if(misdeclared)api(f.d->SetVertexDeclaration(shifted_declaration.p),"bolt shape shifted declaration bind");}
                    const auto state=f.snapshot();
                    const unsigned admitted_before=f.emission_status(f.d.p,60),native_before=f.emission_status(f.d.p,49);
                    const UINT primitives=shaped?2*quads:2;
                    const HRESULT hr=f.d->DrawPrimitive(D3DPT_TRIANGLELIST,0,primitives);++bolt_submissions;++f.draw_index;
                    require(SUCCEEDED(hr),"bolt shape draw HRESULT");
                    f.compare(state,f.snapshot(),"bolt shape restoration");
                    require(f.emission_status(f.d.p,49)-native_before==1,"bolt shape actual native source once");
                    require(f.emission_status(f.d.p,60)-admitted_before==1,"bolt shape additive admission");
                    if(shaped){++odd_admitted;if(primitives>max_primitives)max_primitives=primitives;}else ++plain_admitted;
                    if(plan==1)std::printf("BOLT_SHAPE_DRAW frame=%llu shaped=%u primitives=%u vertices=%u buffer_bytes=%u position_offset=%u\n",f.frame,unsigned(shaped),unsigned(primitives),3u*unsigned(primitives),shaped?odd_bytes:buffer_bytes,shaped&&misdeclared?12u:0u);
                }
            }
            // The TAA reference reads the emission-mode reference image and mask (frame_end), as the screen plan publishes them.
            f.emission_reference_color=scene();
            raw(3,f.emission_reference_mask);
            f.emission_mask_valid=required&&f.emission_status(f.d.p,1);
            require_quiet(f.emission_reference_color.size()==std::size_t(f.W)*f.H*4,"bolt shape reference image published");
            f.frame_end();
        }
        api(f.d->SetIndices(nullptr),"bolt shape final index release");api(f.d->SetStreamSource(0,nullptr,0,0),"bolt shape final stream release");
        std::printf("BOLT_SHAPE script=%s frames=%u shaped_draws=%u plain_draws=%u max_primitives=%u submissions=%u\n",bolt_shape_setting,bolt_frames,odd_admitted,plain_admitted,max_primitives,bolt_submissions);
        return;
    }
    require(!bolt_shape_length,"X3M_FIXTURE_BOLT_SHAPE only with mode boltshape");
    // Effects stage, phase 1 (mode effectsstage; docs/architecture/effects-modernisation-opus.md "Implementation
    // (phase 1)", docs/verification/effects-stage.md, review B2): the production recogniser and stage end to end.
    // Frames 1-13 each draw one ordinary bolt from the writer's buffer under the qualified additive configuration
    // with X3M_EFFECTS_STAGE=1: the draw must be admitted by the additive route AND forwarded natively once (phase 1
    // suppresses nothing), the seam's effects_stage_frame row of that Present (X3M_TELEMETRY=1) must show it
    // recorded and drawn by the stage inside the resolve's bracket (bolts=1, result 0). Frame 6 queues the HDR
    // Resolve fault (HdrFault::Resolve = 14: MotionOutput::resolve fails without running the pass), so the stage
    // cannot run at that scene end: the row shows ran=0 with the record kept and the bolt still drawn natively
    // (the B1 path: nothing vanishes). After frame 9 the device is Reset (the pass's buffers go with the targets;
    // the next frame re-attaches: a second effects_stage_device row) and frames 10-13 draw again. The runner reads
    // the session log (validate_effects_stage).
    if(f.effectsstage) {
        require(qualified&&additive,"effects stage script needs the qualified additive configuration");
        constexpr unsigned stage_frames=14,fault_plan=6,reset_after=9;
        unsigned bolt_submissions=0,native_draws=0,admitted_draws=0;
        for(unsigned plan=0;plan<stage_frames;++plan) {
            if(plan==reset_after+1){bullets.reset();f.reset();create_bullets();}
            f.frame_begin();f.linear_material_inputs();f.write_reserved();
            f.draw(f.a,.03125f*float(plan%3),0,0,true,true,f.a.recorded,Alter::None,false);
            const unsigned required=f.emission_status(f.d.p,16);
            f.emissions_enabled=required!=0;
            if(plan) {
                write_bullets(4);bind_bullets('s'); // four copies of the quad: 24 vertices, the smallest stock bullet body (bolt_footprint::min_period), one instance
                const auto state=f.snapshot();
                const unsigned admitted_before=f.emission_status(f.d.p,60),native_before=f.emission_status(f.d.p,49);
                const HRESULT hr=f.d->DrawPrimitive(D3DPT_TRIANGLELIST,0,8);++bolt_submissions;++f.draw_index;
                require(SUCCEEDED(hr),"effects stage bolt draw HRESULT");
                f.compare(state,f.snapshot(),"effects stage bolt restoration");
                const unsigned native=f.emission_status(f.d.p,49)-native_before,admitted=f.emission_status(f.d.p,60)-admitted_before;
                require(native==1,"effects stage bolt draw forwarded natively once (phase 1 suppresses nothing)");
                require(admitted==1,"effects stage bolt draw admitted by the additive route");
                native_draws+=native;admitted_draws+=admitted;
                if(plan==fault_plan){require(f.hdr_fault!=nullptr,"effects stage resolve fault seam");f.hdr_fault(f.d.p,14,1);}
            }
            f.emission_reference_color=scene();
            raw(3,f.emission_reference_mask);
            f.emission_mask_valid=required&&f.emission_status(f.d.p,1);
            f.frame_end();
        }
        api(f.d->SetIndices(nullptr),"effects stage final index release");api(f.d->SetStreamSource(0,nullptr,0,0),"effects stage final stream release");
        std::printf("EFFECTS_STAGE frames=%u submissions=%u native_draws=%u admitted=%u fault_plan=%u reset_after=%u\n",stage_frames,bolt_submissions,native_draws,admitted_draws,fault_plan,reset_after);
        return;
    }
    // Plan: kind s = screen, f = fade, e = emission; overlap doubles the quad in
    // one DIP; faults are pass faults (5 SourceBind -> refusal 5 native; 6
    // Composite -> Incomplete, A|R recovered). Reset after frame 10 (the
    // buffer is recreated: frame 11 is the first draw again).
    struct Plan {const char* kinds;unsigned overlap;unsigned fault;};
    // Frames 14-16: the readiness refusals (PROJECTED, sRGB sampler) and the
    // dither state, each bound and native. Frames 17-20: the near-plane
    // kinds (n straddling, x exact, h behind, b beam). Frame 21: the fade
    // source, then the step E overlap chain (c) over its darkened rectangle.
    const Plan plans[]={{"",1,0},{"s",1,0},{"s",1,0},{"es",1,0},{"sf",1,0},{"fs",1,0},{"esf",1,0},{"s",2,0},{"s",1,5},{"s",1,6},{"s",1,0},{"s",1,0},{"s",1,0},{"se",1,0},{"p",1,0},{"g",1,0},{"d",1,0},{"n",1,0},{"x",1,0},{"h",1,0},{"b",1,0},{"fc",1,0}};
    const Plan control[]={{"",1,0},{"s",1,0},{"s",1,0}};
    const Plan additive_plans[]={{"",1,0},{"ks",1,0},{"s",1,0},{"p",1,0}};
    const unsigned frames=f.screenemission_bench?18:additive?unsigned(sizeof additive_plans/sizeof additive_plans[0]):qualified?unsigned(sizeof plans/sizeof plans[0]):unsigned(sizeof control/sizeof control[0]);
    unsigned submissions=0;
    const auto covered_by=[](const RECT& rect,unsigned x,unsigned y){return LONG(x)>=rect.left&&LONG(x)<rect.right&&LONG(y)>=rect.top&&LONG(y)<rect.bottom;};
    for(unsigned plan=0;plan<frames;++plan) {
        f.frame_begin();f.linear_material_inputs();f.write_reserved();
        const float ordinary_t=.03125f*float(plan%3);
        f.draw(f.a,ordinary_t,0,0,true,true,f.a.recorded,Alter::None,false);
        const unsigned required=f.emission_status(f.d.p,16);
        f.emissions_enabled=required!=0||f.screen_enabled;
        if(f.screenemission_bench) {
            const unsigned count=plan<6?1:plan<12?4:16,sample=plan%6;
            write_bullets(1);bind_bullets('s');
            const unsigned pixels_before=f.emission_status(f.d.p,46),admitted_before=f.emission_status(f.d.p,41),unbounded_before=f.emission_status(f.d.p,44);
            fence();LARGE_INTEGER begin,middle,end;QueryPerformanceCounter(&begin);
            for(unsigned i=0;i<count;++i){api(f.d->DrawPrimitive(D3DPT_TRIANGLELIST,0,2),"timed bullet source");++submissions;++f.draw_index;}
            fence();QueryPerformanceCounter(&middle);
            api(f.d->SetDepthStencilSurface(nullptr),"screen timed terminal depth");api(f.d->StretchRect(f.back.p,nullptr,f.bloom_surface.p,nullptr,D3DTEXF_NONE),"screen timed TAA AgX publication");api(f.d->EndScene(),"screen timed EndScene");fence();QueryPerformanceCounter(&end);
            const unsigned admitted=f.emission_status(f.d.p,41)-admitted_before,unbounded=f.emission_status(f.d.p,44)-unbounded_before,pixels=f.emission_status(f.d.p,46)-pixels_before;
            if(sample>=2)std::printf("SCREEN_TIMING width=%u height=%u count=%u sample=%u screen=%u admitted=%u unbounded=%u region_pixels=%u quad=%ld,%ld,%ld,%ld source_ms=%.9f terminal_ms=%.9f total_ms=%.9f\n",f.W,f.H,count,sample-2,f.screen_enabled,admitted,unbounded,pixels,quad_rect.left,quad_rect.top,quad_rect.right,quad_rect.bottom,1000.*double(middle.QuadPart-begin.QuadPart)/frequency.QuadPart,1000.*double(end.QuadPart-middle.QuadPart)/frequency.QuadPart,1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart);
            api(f.d->SetDepthStencilSurface(f.depth.p),"screen benchmark depth restore");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"screen benchmark Present");++f.frame;++f.frames_since_reset;continue;
        }
        const Plan& p=additive?additive_plans[plan]:qualified?plans[plan]:control[plan];
        std::vector<float> expected_mask(std::size_t(f.W)*f.H*4,0);
        auto before=scene();
        for(unsigned source=0;p.kinds[source];++source) {
            const char kind=p.kinds[source];
            const bool screen=kind=='s'||kind=='p'||kind=='g'||kind=='d'||kind=='c'||kind=='k'||near_kind(kind),emission=kind=='e';
            const unsigned overlap=kind=='c'?chain_quads:screen?p.overlap:1;
            unsigned fault=screen?p.fault:0;
            if(screen)write_bullets(overlap,kind);
            const RECT rect=screen?bind_bullets(kind):bind_source(emission);
            {Com<IDirect3DSurface9> logical;api(f.d->GetRenderTarget(0,&logical.p),"screen source snapshot application view");}
            const auto state=f.snapshot();
            float native_vs[48][4]{};api(f.d->GetVertexShaderConstantF(0,native_vs[0],48),"screen native VS constant snapshot");
            constexpr D3DRENDERSTATETYPE blend_states[]={D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3};
            DWORD blend_before[11]{};for(unsigned i=0;i<11;++i)api(f.d->GetRenderState(blend_states[i],&blend_before[i]),"screen caller blend snapshot");
            std::vector<float> motion_before,depth_before,mask_before;
            raw(1,motion_before);raw(2,depth_before);const HRESULT mask_hr_before=raw(3,mask_before);
            constexpr unsigned status_keys=63; // 0-53 composition and fade route, 60-62 the additive option
            unsigned prior[status_keys];for(unsigned k=0;k<status_keys;++k)prior[k]=f.emission_status(f.d.p,k);
            if(!(screen?f.screen_enabled&&!caps_fault:emission?required&1u:required&2u))fault=0;
            if(fault)f.emission_fault(f.d.p,fault,1);
            const HRESULT hr=screen?f.d->DrawPrimitive(D3DPT_TRIANGLELIST,0,2*overlap):f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2);
            if(kind=='c')write_raw("chain_before",before);
            ++submissions;++f.draw_index;
            require(SUCCEEDED(hr),"screen original source HRESULT");
            f.compare(state,f.snapshot(),"screen source restoration");
            float restored_vs[48][4]{};api(f.d->GetVertexShaderConstantF(0,restored_vs[0],48),"screen native VS constant readback");require(!std::memcmp(native_vs,restored_vs,sizeof native_vs),"screen native VS constants restored");
            for(unsigned i=0;i<11;++i){DWORD value=0;api(f.d->GetRenderState(blend_states[i],&value),"screen caller blend readback");require(value==blend_before[i],"screen exact caller blend/mask restoration");}
            unsigned delta[status_keys];for(unsigned k=0;k<status_keys;++k)delta[k]=f.emission_status(f.d.p,k)-prior[k];
            require((screen?delta[49]:delta[12])==1,"screen actual native source once");
            if(additive){
                require(delta[60]==(kind=='s'?1u:0u)&&delta[61]==(kind=='p'||kind=='k'?1u:0u)&&delta[62]==0,"additive admission: the screen state admits, PROJECTED and the opaque state refuse, nothing fails");
                require(delta[41]==0&&delta[40]==0&&delta[4]==0,"additive never enters the packed or composition route");
            } else require(delta[60]==0&&delta[61]==0&&delta[62]==0,"the additive option is off");
            auto after=scene();
            std::vector<float> motion_after,depth_after,mask_after;
            raw(1,motion_after);raw(2,depth_after);const HRESULT mask_hr_after=raw(3,mask_after);
            require(motion_before==motion_after&&depth_before==depth_after,"screen source preserves ordinary RT1 RT2 exactly");
            const bool packed=screen&&delta[41]==1,recovered=packed&&delta[43]==1;
            const RECT composed=packed&&injected?injected_rect:rect;
            if(additive&&screen){
                char name[32];std::snprintf(name,sizeof name,"additive_before_%u",source);write_raw(name,before);
                std::snprintf(name,sizeof name,"additive_after_%u",source);write_raw(name,after);
            }
            if(kind=='c') {
                write_raw("chain_after",after);
                std::printf("SCREEN_CHAIN frame=%llu source=%u quads=%u step_px=%u sprite=%u sigma=3 rect=%ld,%ld,%ld,%ld packed=%u composed=%ld,%ld,%ld,%ld\n",f.frame,source,chain_quads,chain_step_px,sprite_size,rect.left,rect.top,rect.right,rect.bottom,unsigned(packed),composed.left,composed.top,composed.right,composed.bottom);
            }
            for(unsigned y=0;y<f.H;++y)for(unsigned x=0;x<f.W;++x) {
                const unsigned i=(y*f.W+x)*4;const bool covered=covered_source(kind,rect,x,y)&&(!packed||covered_by(composed,x,y));
                for(unsigned k=0;k<4;++k)require_quiet(std::isfinite(after[i+k]),"screen finite actual scene");
                if(!covered||recovered)require_quiet(!std::memcmp(&before[i],&after[i],16),"screen excluded (or recovered) raw A exact");
                else if(!screen&&!emission)require_quiet(after[i+3]==before[i+3],"screen fade native destination alpha exact");
                else if(emission&&f.hdr)require_quiet(after[i+3]==before[i+3]+.125f,"screen mixed emission alpha source once");
                if(SUCCEEDED(mask_hr_before)&&SUCCEEDED(mask_hr_after)&&mask_before[i]>0)require_quiet(mask_after[i]>0,"screen earlier shared mask footprint retained");
                if(covered_source(kind,rect,x,y)&&delta[4]==1)for(unsigned k=0;k<3;++k)expected_mask[i+k]=1; // the source writes M unscissored: the whole quad
            }
            // Numerical witnesses: inside the quad and every scissor, and inside
            // the quad only (outside the fade/emission scissor and the injected
            // half rectangle). Python holds the packed / native / fade laws.
            const unsigned sample_x[2]={f.W/2+4,f.W/2+12};
            for(unsigned sample=0;sample<2;++sample) {
                const unsigned x=sample_x[sample],y=f.H/2,i=(y*f.W+x)*4;
                std::printf("SCREEN_SAMPLE frame=%llu source=%u kind=%c overlap=%u x=%u y=%u covered=%u bracket=%u packed=%u q=%.9g,%.9g,%.9g a=%.9g before=%.17g,%.17g,%.17g,%.17g after=%.17g,%.17g,%.17g,%.17g\n",f.frame,source,kind,overlap,x,y,unsigned(covered_source(kind,rect,x,y)),unsigned(packed),unsigned(packed&&covered_by(composed,x,y)),
                            double(bullet_texel[0]),double(bullet_texel[1]),double(bullet_texel[2]),double(bullet_texel[3]),before[i],before[i+1],before[i+2],before[i+3],after[i],after[i+1],after[i+2],after[i+3]);
            }
            std::printf("SCREEN_SOURCE frame=%llu source=%u kind=%c overlap=%u fault=%u hr=%08lx original_calls=%u prepared=%u linear=%u native=%u incomplete=%u refused=%u packed_eligible=%u packed_admitted=%u packed_linear=%u packed_incomplete=%u packed_unbounded=%u packed_caps=%u packed_region_pixels=%u prefix_bound=%u prefix_refused=%u rect=%ld,%ld,%ld,%ld mask_before=%u mask_after=%u hash_mask_before=%016llx hash_mask_after=%016llx hash_red_before=%016llx hash_red_after=%016llx additive_admitted=%u additive_refused=%u additive_failures=%u\n",
                        f.frame,source,kind,overlap,fault,hr,screen?delta[49]:delta[12],delta[4],delta[5],delta[6],delta[7],delta[8],delta[40],delta[41],delta[42],delta[43],delta[44],delta[45],delta[46],delta[47],delta[48],rect.left,rect.top,rect.right,rect.bottom,prior[1],f.emission_status(f.d.p,1),static_cast<unsigned long long>(hash(mask_before)),static_cast<unsigned long long>(hash(mask_after)),static_cast<unsigned long long>(hash_red(mask_before)),static_cast<unsigned long long>(hash_red(mask_after)),delta[60],delta[61],delta[62]);
            before=std::move(after);
        }
        f.emission_reference_color=scene();
        raw(3,f.emission_reference_mask);
        f.emission_mask_valid=(required||f.screen_enabled)&&f.emission_status(f.d.p,1);
        if(f.emission_mask_valid)for(std::size_t i=0;i<expected_mask.size();i+=4)require_quiet((f.emission_reference_mask[i]>0)==(expected_mask[i]>0),"screen canonical combined producer union");
        if(qualified){write_raw("color",f.emission_reference_color);write_raw("mask",f.emission_reference_mask);}
        std::vector<float> alpha(std::size_t(f.W)*f.H),motion,depth;
        for(std::size_t i=0;i<alpha.size();++i)alpha[i]=f.emission_reference_color[4*i+3];
        raw(1,motion);raw(2,depth);
        std::printf("SCREEN_LIVE frame=%llu screen=%u fade=%u emission=%u draws=%u",f.frame,f.screen_enabled,f.distancefade_enabled,f.distancefade_emissions_enabled,unsigned(std::strlen(p.kinds)));
        for(unsigned i=0;i<63;++i)std::printf(" s%u=%u",i,f.emission_status(f.d.p,i));
        std::printf(" hash_alpha=%016llx hash_motion=%016llx hash_depth=%016llx hash_mask=%016llx\n",static_cast<unsigned long long>(hash(alpha)),static_cast<unsigned long long>(hash(motion)),static_cast<unsigned long long>(hash(depth)),static_cast<unsigned long long>(hash(f.emission_reference_mask)));
        if(!qualified) {
            require(f.emission_status(f.d.p,41)==0,"screen missing prerequisite never admits");
            api(f.d->EndScene(),"screen admission-control EndScene");f.verify_motion();api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"screen admission-control Present");++f.frame;++f.frames_since_reset;
        } else f.frame_end();
        if(qualified&&plan==10) {
            // Reset: the pool targets go, the DEFAULT-pool bullet buffer is
            // recreated (a fresh buffer: its first draw is refused again).
            const unsigned refs_before=f.emission_status(f.d.p,20),allocations_before=f.emission_status(f.d.p,21);
            bullets.reset();
            f.reset();create_bullets();
            const unsigned refs_after=f.emission_status(f.d.p,20);
            std::printf("SCREEN_RESET frame=%u refs_before=%u refs_after=%u allocations=%u quarantine=%u state_lost=%u\n",plan,refs_before,refs_after,allocations_before,f.emission_status(f.d.p,3),f.emission_status(f.d.p,2));
            require(refs_after<refs_before&&refs_before-refs_after==allocations_before,"screen Reset releases every pool target and retains the programs");
            require(f.emission_status(f.d.p,3)==0&&f.emission_status(f.d.p,2)==0,"screen Reset clears state loss without quarantine");
        }
    }
    api(f.d->SetIndices(nullptr),"screen final index release");api(f.d->SetStreamSource(0,nullptr,0,0),"screen final stream release");
    std::printf("SCREEN_CHECKS frames=%u submissions=%u qualified=%u benchmark=%u quad=%ld,%ld,%ld,%ld injected=%u additive=%g\n",frames,submissions,qualified,f.screenemission_bench,quad_rect.left,quad_rect.top,quad_rect.right,quad_rect.bottom,unsigned(injected),double(additive?additive_gain:0.f));
}
