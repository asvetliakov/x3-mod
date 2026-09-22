// Included in the actual D3D Fixture. Hull emissive widening live proof
// (--hull-emissive-widening, X3M_HULL_EMISSIVE_WIDENING=K,Q0,Q1;
// docs/architecture/hull-emissive-widening.md section 3). Target 256 x 256,
// the fixture camera (P[0] = 0.8): footprint = 2 w / (0.8 x 256) = w / 102.4.
//
// Script "strips" (default): a 128-px quad (NDC [-0.5, 0.5]^2, UV 0..1) drawn
// with the standard DEFAULT hull pair vs_494fe349b8bc12ec / ps_7c83ed50c9894e44
// under X3M_HULL_LIGHTMAP_GAIN=4, the light map a 256-texel A8R8G8B8 texture
// with a full box-filtered mip chain (2 texels/px: a 1-texel strip is 0.5 px
// wide): a horizontal strip at v = 0.5, a vertical strip at u = 0.5 (texture
// "strips"), or one 64-texel panel at the centre ("panel"). The rows are
// diag(w) plus a drift of 6.125 px per frame in x and y (eight sub-pixel
// phases); w = 4 (below Q0), 128 (inside: t = 0.5 for Q = 0.5, 2) and 512
// (above Q1) selects the per-draw k while the raster stays the same size.
// Per frame: the light-map term of the quad (pixel minus the quad median),
// the horizontal strip's per-column peak/energy/width (columns u in
// [0.1, 0.4], clear of the vertical strip), the vertical strip's per-row
// metrics (rows v in [0.1, 0.4]), the panel's interior mean, the lit count,
// the uploaded c217.z (last_pixel_abi[6]) and the widened-draw count. Then
// one F4-off frame (no gain, no widening), a Reset and a last "above" frame,
// and the oblique case (perspective rows, p = 0.6) with LINEAR and
// ANISOTROPIC(16) filtering of the light-map stage at w = 4 and w = 512.
//
// Script "programs": one reviewed pair per family group the fixture can drive,
// each drawn near with the gained variant, near with the widened variant
// forced (k = 1 uploaded: texldd(k=1) against texld, FP16 codes compared),
// then far (k = K); the transformer's own widened bytes are also created
// through CreatePixelShader here and disassembled with the game's
// d3dx9_37.dll for the slot count.
struct WidenMetrics { double median=0, max=0, lit=0, h_peak=0, h_energy=0, h_width=0, h_row=0, v_peak=0, v_energy=0, v_width=0, v_col=0, panel=0; };
// Light-map term of a quad at pixels [x0, x0+size) x [y0, y0+size) (lane 0).
static WidenMetrics widen_metrics(const std::vector<float>& image, unsigned width, int x0, int y0, int size) {
    WidenMetrics m{};
    const int inset=4, a0=x0+inset, a1=x0+size-inset, b0=y0+inset, b1=y0+size-inset;
    std::vector<float> values; values.reserve(std::size_t(size)*size);
    for(int y=b0;y<b1;++y)for(int x=a0;x<a1;++x)values.push_back(image[(std::size_t(y)*width+x)*4]);
    std::vector<float> sorted(values); std::nth_element(sorted.begin(),sorted.begin()+sorted.size()/2,sorted.end());
    m.median=sorted[sorted.size()/2];
    auto term=[&](int x,int y){ return double(image[(std::size_t(y)*width+x)*4])-m.median; };
    for(int y=b0;y<b1;++y)for(int x=a0;x<a1;++x)m.max=std::max(m.max,term(x,y));
    for(int y=b0;y<b1;++y)for(int x=a0;x<a1;++x)if(term(x,y)>.02*m.max)++m.lit;
    // Horizontal strip: columns u in [0.1, 0.4] of the quad, every row.
    unsigned columns=0;
    for(int x=x0+size/10;x<x0+size*2/5;++x){
        double peak=0,energy=0; int row=0;
        for(int y=b0;y<b1;++y){ const double t=term(x,y); if(t>peak){peak=t;row=y;} energy+=t; }
        double wide=0; for(int y=b0;y<b1;++y)if(term(x,y)>.5*peak)++wide;
        m.h_peak+=peak; m.h_energy+=energy; m.h_width+=wide; m.h_row+=row-y0; ++columns;
    }
    m.h_peak/=columns; m.h_energy/=columns; m.h_width/=columns; m.h_row/=columns;
    // Vertical strip: rows v in [0.1, 0.4], every column.
    unsigned rows_counted=0;
    for(int y=y0+size/10;y<y0+size*2/5;++y){
        double peak=0,energy=0; int col=0;
        for(int x=a0;x<a1;++x){ const double t=term(x,y); if(t>peak){peak=t;col=x;} energy+=t; }
        double wide=0; for(int x=a0;x<a1;++x)if(term(x,y)>.5*peak)++wide;
        m.v_peak+=peak; m.v_energy+=energy; m.v_width+=wide; m.v_col+=col-x0; ++rows_counted;
    }
    m.v_peak/=rows_counted; m.v_energy/=rows_counted; m.v_width/=rows_counted; m.v_col/=rows_counted;
    // Panel interior: the centre +-3/16 of the quad (the panel spans +-1/4).
    const int c=size*3/16; unsigned n=0;
    for(int y=y0+size/2-c;y<y0+size/2+c;++y)for(int x=x0+size/2-c;x<x0+size/2+c;++x){ m.panel+=term(x,y); ++n; }
    m.panel/=n;
    return m;
}
// A square A8R8G8B8 light map with a complete box-filtered mip chain: level 0
// is `authored` (luminance 0..1 per texel), every level the exact block mean
// rounded to 8 bits (alpha = the same value).
void widen_lightmap(Com<IDirect3DTexture9>& t, unsigned size, const std::vector<float>& authored, const char* what) {
    api(d->CreateTexture(size,size,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t.p,nullptr),what);
    const DWORD levels=t->GetLevelCount();
    require(levels>=2&&(size>>(levels-1))==1,"a complete mip chain");
    for(DWORD level=0;level<levels;++level){
        const unsigned n=size>>level, block=size/n;
        D3DLOCKED_RECT locked{};api(t->LockRect(level,&locked,nullptr,0),what);
        for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
            double sum=0;
            for(unsigned by=0;by<block;++by)for(unsigned bx=0;bx<block;++bx)sum+=authored[(y*block+by)*size+x*block+bx];
            const unsigned v=unsigned(std::lround(sum/(double(block)*block)*255.));
            const DWORD texel=(v<<24)|(v<<16)|(v<<8)|v;
            std::memcpy(static_cast<char*>(locked.pBits)+y*locked.Pitch+x*4,&texel,4);
        }
        api(t->UnlockRect(level),what);
    }
}
struct WidenPair { const char* vs; const char* ps; const char* family; bool bump, fixed, standard; };
static constexpr WidenPair widen_pairs[]={
    {"494fe349b8bc12ec","7c83ed50c9894e44","standard_default",false,false,true},
    {"53a0a641107ed76c","8759c7838bbc86c2","argon_default",false,false,false},
    {"53a0a641107ed76c","3b94320087e81945","shared_default",false,false,false},
    {"53a0a641107ed76c","462342e3e5781384","split_default",false,false,false},
    {"53a0a641107ed76c","ef2bf556f207b8bd","terran_default",false,false,false},
    {"4944d81dfe531b37","ca6bfa4a6cca7e2a","argon_bump",true,false,false},
    {"4944d81dfe531b37","0c1f3f0f440e4a0c","standard_bump",true,false,true},
    {"29d7c575396ed280","39eb3c2258a516e1","boron_default",false,true,false},
    {"37e6956afd8b8d76","9d27e7ba242f3831","paranid_default",false,true,false},
    {"494fe349b8bc12ec","fffdabd910793aba","xt_default",false,false,false},
    {"494fe349b8bc12ec","fd58e6b7e8cf969c","xt_terra",false,false,false},
};
bool widen_force=false; // scope(): MotionOutputFixtureConfig::force_lightmap_widen
void run_lightmap_widen(const char* bootstrap_vertex) {
    require(seam&&enabled&&hdr&&hdr_readback&&!taa&&camera,"hull emissive widening live needs the HDR seam, its readback, the camera and TAA off");
    require(hull_toggle!=nullptr&&emission_status!=nullptr&&last_pixel_abi!=nullptr,"hull toggle, status and ABI exports");
    require(W==256&&H==256,"the widening script runs at 256 x 256");
    char text[96]{};
    const double configured=GetEnvironmentVariableA("X3M_HULL_LIGHTMAP_GAIN",text,sizeof text)>0?std::atof(text):1.;
    require(configured==4.,"the live proof uses light-map gain 4");
    double K=1,q0=0,q1=0;bool widen=false;
    if(GetEnvironmentVariableA("X3M_HULL_EMISSIVE_WIDENING",text,sizeof text)>0){
        require(std::sscanf(text,"%lf,%lf,%lf",&K,&q0,&q1)==3&&K>1&&q1>q0,"widening setting");widen=true;
    }
    char script_text[16]{};GetEnvironmentVariableA("X3M_FIXTURE_WIDEN_SCRIPT",script_text,sizeof script_text);
    const bool programs=!std::strcmp(script_text,"programs");
    const std::string bootstrap_path(bootstrap_vertex);const auto slash=bootstrap_path.find_last_of("/\\");
    const auto folder=slash==std::string::npos?std::string{}:bootstrap_path.substr(0,slash+1);
    // Light maps: strips (h at v = 0.5, v at u = 0.5), one centred panel, black.
    constexpr unsigned S=256;
    std::vector<float> strips(S*S,0.f),panel(S*S,0.f),dark(S*S,0.f);
    for(unsigned i=0;i<S;++i){strips[(S/2)*S+i]=1.f;strips[i*S+S/2]=1.f;}
    for(unsigned y=S*3/8;y<S*5/8;++y)for(unsigned x=S*3/8;x<S*5/8;++x)panel[y*S+x]=1.f;
    Com<IDirect3DTexture9> strips_map,panel_map;
    widen_lightmap(strips_map,S,strips,"strips light map");widen_lightmap(panel_map,S,panel,"panel light map");
    Com<IDirect3DTexture9> art,mask,normal;
    for(unsigned which=0;which<3;++which){
        auto& t=which==0?art:which==1?mask:normal;
        api(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t.p,nullptr),"widen art");
        D3DLOCKED_RECT locked{};api(t->LockRect(0,&locked,nullptr,0),"widen art lock");
        const DWORD value=which==0?0x40202020u:which==1?0x40000000u:0x80008000u;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(locked.pBits)+y*locked.Pitch+x*4,&value,4);
        api(t->UnlockRect(0),"widen art unlock");
    }
    // The quad: two triangles, NDC [-0.5, 0.5]^2, UV 0..1 (u right, v down on
    // screen with y up in NDC), DEFAULT (24 B) and BUMP (40 B) layouts.
    Com<IDirect3DVertexBuffer9> quad,quad_bump;Com<IDirect3DVertexDeclaration9> bump_declaration;
    {
        const float corners[6][2]={{-.5f,.5f},{.5f,.5f},{-.5f,-.5f},{.5f,.5f},{.5f,-.5f},{-.5f,-.5f}};
        api(d->CreateVertexBuffer(24*6,0,0,D3DPOOL_MANAGED,&quad.p,nullptr),"widen quad");
        api(d->CreateVertexBuffer(40*6,0,0,D3DPOOL_MANAGED,&quad_bump.p,nullptr),"widen BUMP quad");
        void *plain=nullptr,*bumped=nullptr;api(quad->Lock(0,0,&plain,0),"widen quad lock");api(quad_bump->Lock(0,0,&bumped,0),"widen BUMP quad lock");
        const unsigned short basis[]={0,half(1),0,0,half(1),0,0,0};
        for(unsigned i=0;i<6;++i){
            const float u=corners[i][0]+.5f,v=.5f-corners[i][1];
            unsigned short data[12]={half(corners[i][0]),half(corners[i][1]),half(.5f),half(7),half(u),half(v),0,half(7),0,0,half(1),half(7)};
            std::memcpy(static_cast<char*>(plain)+i*24,data,24);
            std::memcpy(static_cast<char*>(bumped)+i*40,data,24);std::memcpy(static_cast<char*>(bumped)+i*40+24,basis,sizeof basis);
        }
        api(quad->Unlock(),"widen quad unlock");api(quad_bump->Unlock(),"widen BUMP quad unlock");
        const D3DVERTEXELEMENT9 elements[]={
            {0,0,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,8,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
            {0,16,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},
            {0,32,D3DDECLTYPE_FLOAT16_4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},D3DDECL_END()};
        api(d->CreateVertexDeclaration(elements,&bump_declaration.p),"widen BUMP declaration");
    }
    // One frame: the pair, light map, rows and sampler filter; returns the FP16 image.
    struct Frame { double k_uploaded; unsigned widened, gained; std::uint64_t hash; std::vector<float> image; };
    auto draw_frame=[&](const WidenPair& pair,IDirect3DVertexShader9* pvs,IDirect3DPixelShader9* pps,IDirect3DTexture9* light,float w,float drift_px,float perspective,bool anisotropic){
        frame_begin();
        scope(&b);
        material_state();
        const unsigned stage=pair.bump?3u:2u;
        if(pair.fixed){
            // Fixed-point originals: c0..3 position, c7..20 world/normal/view/UV/alpha (the linear-materials corpus values).
            float values[21][4]{};std::memcpy(values,identity,sizeof identity);
            for(unsigned base:{7u,10u,13u})for(unsigned lane=0;lane<3;++lane)values[base+lane][lane]=1;
            values[4][2]=4;values[6][0]=1;values[15][3]=4;values[16][0]=values[17][1]=1;values[18][0]=.625f;values[20][0]=1;
            api(d->SetVertexShaderConstantF(0,values[0],21),"fixed VS constants");
        }
        if(pair.standard){
            const float coefficients[4][4]={{0,0,0,0},{8,0,0,0},{0,0,0,0},{1,0,0,0}};
            api(d->SetPixelShaderConstantF(8,coefficients[0],4),"standard coefficients");
        }
        if(pair.bump){
            api(d->SetTexture(0,art.p),"art");api(d->SetTexture(1,normal.p),"normal");api(d->SetTexture(2,mask.p),"mask");api(d->SetTexture(3,light),"light map");
            api(d->SetTexture(4,cube.p),"reflection");api(d->SetSamplerState(4,D3DSAMP_SRGBTEXTURE,FALSE),"s4 state");
            api(d->SetVertexDeclaration(bump_declaration.p),"BUMP declaration");api(d->SetStreamSource(0,quad_bump.p,0,40),"BUMP quad stream");
        } else {
            api(d->SetTexture(0,art.p),"art");api(d->SetTexture(1,mask.p),"mask");api(d->SetTexture(2,light),"light map");
            api(d->SetStreamSource(0,quad.p,0,24),"quad stream");
        }
        // The light-map stage samples with a mip chain (trilinear), anisotropic for the oblique case.
        api(d->SetSamplerState(stage,D3DSAMP_MINFILTER,anisotropic?D3DTEXF_ANISOTROPIC:D3DTEXF_LINEAR),"light-map min filter");
        api(d->SetSamplerState(stage,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR),"light-map mag filter");
        api(d->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR),"light-map mip filter");
        api(d->SetSamplerState(stage,D3DSAMP_MAXANISOTROPY,anisotropic?16u:1u),"light-map anisotropy");
        api(d->SetSamplerState(stage,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"light-map u");api(d->SetSamplerState(stage,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"light-map v");
        api(d->SetVertexShader(pvs),"pair VS");api(d->SetPixelShader(pps),"pair PS");
        // diag(w) plus the drift (NDC shift x drift, clip = w x NDC) and the oblique perspective (w_clip = w (1 + p x)).
        float m[16];std::memcpy(m,identity,sizeof m);m[0]=m[5]=m[10]=m[15]=w;
        m[3]=w*2.f*drift_px/float(W);m[7]=-w*2.f*drift_px/float(H);m[12]=w*perspective;
        api(d->SetVertexShaderConstantF(24,m,4),"widen rows");
        const Snapshot before=snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,2),"widen quad draw");++draw_index;
        compare(before,snapshot(),"widen quad draw");
        Frame f{};
        f.gained=emission_status(d.p,78);f.widened=emission_status(d.p,80);
        float abi[8]{};api(last_pixel_abi(d.p,abi,8),"last pixel ABI");f.k_uploaded=abi[6];
        rows(0,0,0);
        api(d->SetSamplerState(stage,D3DSAMP_MAXANISOTROPY,1u),"light-map anisotropy restore");
        api(d->EndScene(),"EndScene");
        write_presented(color_image());
        unsigned iw=0,ih=0;f.image=hdr_image(&iw,&ih);
        require(iw==W&&ih==H,"the FP16 target matches the main dimensions");
        std::vector<unsigned short> codes(f.image.size());
        for(std::size_t i=0;i<codes.size();++i)codes[i]=float_to_half(f.image[i]);
        f.hash=fnv(codes.data(),codes.size()*sizeof codes[0]);
        api(d->Present(nullptr,nullptr,nullptr,nullptr),"Present");
        ++frame;++frames_since_reset;
        return f;
    };
    auto law=[&](float w){ const double f=2.*double(w)/(double(.8f)*double(W)); const double t=widen?std::min(1.,std::max(0.,(f-q0)/(q1-q0))):0.; return widen?1.+(K-1.)*t:0.; };
    auto load_pair=[&](const WidenPair& pair,Com<IDirect3DVertexShader9>& pvs,Com<IDirect3DPixelShader9>& pps){
        const auto v=load((folder+"vs_"+pair.vs+".bin").c_str()),p=load((folder+"ps_"+pair.ps+".bin").c_str());
        require(fnv(v.data(),v.size()*4)==std::strtoull(pair.vs,nullptr,16)&&fnv(p.data(),p.size()*4)==std::strtoull(pair.ps,nullptr,16),"original pair identity");
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&pvs.p),"pair VS");
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&pps.p),"pair PS");
        return p;
    };
    const int quad_size=int(W)/2;
    if(!programs){
        const WidenPair& pair=widen_pairs[0];
        Com<IDirect3DVertexShader9> pvs;Com<IDirect3DPixelShader9> pps;load_pair(pair,pvs,pps);
        struct Distance { const char* name; float w; } distances[]={{"below",4},{"inside",128},{"above",512}};
        bool on=true;unsigned step=0;
        for(unsigned which=0;which<2;++which){
            const char* map_name=which?"panel":"strips";IDirect3DTexture9* light=which?panel_map.p:strips_map.p;
            for(const auto& dist:distances){
                if(which==1&&!std::strcmp(dist.name,"inside"))continue; // the panel: the ends of the ramp suffice
                for(unsigned phase=0;phase<8;++phase){
                    const float drift=6.125f*float(phase);
                    const Frame f=draw_frame(pair,pvs.p,pps.p,light,dist.w,drift,0.f,false);
                    const int x0=int(W)/4+int(drift),y0=int(H)/4+int(drift);
                    const WidenMetrics m=widen_metrics(f.image,W,x0,y0,quad_size);
                    const double expected=law(dist.w);
                    std::printf("LIGHTMAP_WIDEN frame=%llu step=%u map=%s dist=%s w=%g phase=%u footprint=%.9g on=%u abi_k=%.9g expected_k=%.9g gained_draws=%u widened_draws=%u median=%.9g max=%.9g lit=%g h_peak=%.9g h_energy=%.9g h_width=%.3f h_row=%.2f v_peak=%.9g v_energy=%.9g v_width=%.3f v_col=%.2f panel=%.9g hdr_hash=%016llx\n",
                                frame-1,step++,map_name,dist.name,double(dist.w),phase,2.*double(dist.w)/(double(.8f)*double(W)),unsigned(on),f.k_uploaded,expected,f.gained,f.widened,
                                m.median,m.max,m.lit,m.h_peak,m.h_energy,m.h_width,m.h_row,m.v_peak,m.v_energy,m.v_width,m.v_col,m.panel,static_cast<unsigned long long>(f.hash));
                    require(f.gained==1u,"the gained variant binds on every F4-on frame");
                    require(std::fabs(f.k_uploaded-expected)<=1e-5*std::max(1.,expected),"c217.z carries the ramp's k (0 with the option off)");
                    require(f.widened==(expected>1.?1u:0u),"the widened variant binds exactly when k exceeds 1");
                    require(m.max>0&&m.lit>0,"the light-map term is visible on the quad");
                }
            }
        }
        // F4 off at the far distance: neither the gain nor the widening.
        {
            const int state=hull_toggle(d.p,1);on=false;require(state==0,"F4 off");
            const Frame f=draw_frame(pair,pvs.p,pps.p,strips_map.p,512.f,0.f,0.f,false);
            const WidenMetrics m=widen_metrics(f.image,W,int(W)/4,int(H)/4,quad_size);
            std::printf("LIGHTMAP_WIDEN_OFF frame=%llu gained_draws=%u widened_draws=%u abi_k=%.9g h_peak=%.9g v_peak=%.9g hdr_hash=%016llx\n",frame-1,f.gained,f.widened,f.k_uploaded,m.h_peak,m.v_peak,static_cast<unsigned long long>(f.hash));
            require(f.gained==0u&&f.widened==0u,"F4 off drops the gain and the widening together");
            require(hull_toggle(d.p,1)==1,"F4 on again");on=true;
        }
        // Reset: the variants are re-created through the registration path; the far frame binds the widened one again.
        {
            reset();
            const Frame f=draw_frame(pair,pvs.p,pps.p,strips_map.p,512.f,0.f,0.f,false);
            const WidenMetrics m=widen_metrics(f.image,W,int(W)/4,int(H)/4,quad_size);
            std::printf("LIGHTMAP_WIDEN_RESET frame=%llu gained_draws=%u widened_draws=%u abi_k=%.9g h_peak=%.9g v_peak=%.9g hdr_hash=%016llx\n",frame-1,f.gained,f.widened,f.k_uploaded,m.h_peak,m.v_peak,static_cast<unsigned long long>(f.hash));
            require(f.gained==1u&&f.widened==(widen?1u:0u)&&std::fabs(f.k_uploaded-law(512.f))<=1e-5*std::max(1.,law(512.f)),"after Reset the far frame binds the widened variant again");
        }
        // Oblique: perspective rows (w_clip = w (1 + 0.6 x)), LINEAR against ANISOTROPIC 16 on the light-map stage,
        // near (k = 1: texld) and far (k = K: texldd). Whole-quad max/sum of the term and the image hash.
        for(float w:{4.f,512.f})for(unsigned aniso=0;aniso<2;++aniso){
            const Frame f=draw_frame(pair,pvs.p,pps.p,strips_map.p,w,0.f,.6f,aniso!=0);
            // The oblique quad: x' = x / (1 + 0.6 x) maps [-0.5, 0.5] to [-0.714, 0.385]; analyse the inscribed square [-0.35, 0.35].
            const int x0=int(W)/2-int(W)*35/200,y0=int(H)/2-int(H)*35/200,size=int(W)*35/100;
            double peak=0,sum=0;std::vector<float> values;
            for(int y=y0;y<y0+size;++y)for(int x=x0;x<x0+size;++x)values.push_back(f.image[(std::size_t(y)*W+x)*4]);
            std::vector<float> sorted(values);std::nth_element(sorted.begin(),sorted.begin()+sorted.size()/2,sorted.end());
            const double median=sorted[sorted.size()/2];
            for(float v:values){const double t=double(v)-median;peak=std::max(peak,t);sum+=t;}
            std::printf("LIGHTMAP_WIDEN_ANISO frame=%llu w=%g filter=%s abi_k=%.9g widened_draws=%u peak=%.9g sum=%.9g hdr_hash=%016llx\n",
                        frame-1,double(w),aniso?"aniso16":"linear",f.k_uploaded,f.widened,peak,sum,static_cast<unsigned long long>(f.hash));
        }
        api(d->SetTexture(0,nullptr),"unbind art");api(d->SetTexture(1,nullptr),"unbind mask");api(d->SetTexture(2,nullptr),"unbind light map");
        return;
    }
    // Script "programs": every drivable family pair: near gained, near widened (forced, k = 1), far (k = K).
    require(widen,"the programs script runs with the option on");
    using Disassemble=HRESULT(WINAPI*)(const DWORD*,BOOL,const char*,ID3DXBuffer**);
    Disassemble disassemble=nullptr;
    if(HMODULE d3dx=LoadLibraryA("C:\\X3\\d3dx9_37.dll")){const FARPROC symbol=GetProcAddress(d3dx,"D3DXDisassembleShader");std::memcpy(&disassemble,&symbol,sizeof disassemble);}
    std::printf("LIGHTMAP_WIDEN_DISASSEMBLER available=%u\n",disassemble?1u:0u);
    auto slots_of=[&](const std::vector<std::uint32_t>& code){
        if(!disassemble)return -1;
        ID3DXBuffer* buffer=nullptr;
        if(FAILED(disassemble(reinterpret_cast<const DWORD*>(code.data()),FALSE,nullptr,&buffer))||!buffer)return -2;
        const std::string listing(static_cast<const char*>(buffer->GetBufferPointer()),buffer->GetBufferSize());buffer->Release();
        const auto at=listing.rfind("approximately "); // the last one: the originals embed their own compile comment
        return at==std::string::npos?-3:std::atoi(listing.c_str()+at+14);
    };
    for(const auto& pair:widen_pairs){
        Com<IDirect3DVertexShader9> pvs;Com<IDirect3DPixelShader9> pps;const auto original=load_pair(pair,pvs,pps);
        // The transformer's own bytes (static gain 4, K composed): CreatePixelShader acceptance and the D3DX slot count.
        std::vector<std::uint32_t> gained,widened;bool fill_applied=false,gain_applied=false,widen_applied=false;
        const auto gr=x3m::renderer::linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),0.f,4.f,gained,true,fill_applied,gain_applied,false);
        const auto wr=x3m::renderer::linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),0.f,4.f,widened,true,fill_applied,gain_applied,false,true,&widen_applied);
        require(gr==x3m::renderer::LinearMaterialResult::Applied&&wr==x3m::renderer::LinearMaterialResult::Applied&&gain_applied&&widen_applied,"the pair's programs take the gain and the widening");
        Com<IDirect3DPixelShader9> created_gained,created_widened;
        const HRESULT create_gained=d->CreatePixelShader(reinterpret_cast<const DWORD*>(gained.data()),&created_gained.p);
        const HRESULT create_widened=d->CreatePixelShader(reinterpret_cast<const DWORD*>(widened.data()),&created_widened.p);
        std::printf("LIGHTMAP_WIDEN_PROGRAM ps=%s family=%s words=%u gained_words=%u widened_words=%u create_gained=%08lx create_widened=%08lx gained_slots=%d widened_slots=%d\n",
                    pair.ps,pair.family,unsigned(original.size()),unsigned(gained.size()),unsigned(widened.size()),create_gained,create_widened,slots_of(gained),slots_of(widened));
        require(SUCCEEDED(create_widened)&&created_widened.p,"CreatePixelShader accepts the widened program");
        widen_force=false;const Frame near_gained=draw_frame(pair,pvs.p,pps.p,strips_map.p,4.f,0.f,0.f,false);
        widen_force=true;const Frame near_widened=draw_frame(pair,pvs.p,pps.p,strips_map.p,4.f,0.f,0.f,false);
        widen_force=false;const Frame frame_far=draw_frame(pair,pvs.p,pps.p,strips_map.p,512.f,0.f,0.f,false);
        unsigned max_codes=0,differing=0;
        for(std::size_t i=0;i<near_gained.image.size();++i){
            const unsigned codes=unsigned(std::abs(int(float_to_half(near_gained.image[i]))-int(float_to_half(near_widened.image[i]))));
            if(codes){++differing;max_codes=std::max(max_codes,codes);}
        }
        const WidenMetrics a=widen_metrics(near_gained.image,W,int(W)/4,int(H)/4,quad_size),c=widen_metrics(frame_far.image,W,int(W)/4,int(H)/4,quad_size);
        std::printf("LIGHTMAP_WIDEN_PAIR ps=%s family=%s near_widened=%u near_k=%.9g far_widened=%u far_k=%.9g near_hash=%016llx forced_hash=%016llx far_hash=%016llx max_codes=%u differing=%u near_h_peak=%.9g far_h_peak=%.9g near_h_energy=%.9g far_h_energy=%.9g near_v_peak=%.9g far_v_peak=%.9g near_v_energy=%.9g far_v_energy=%.9g near_lit=%g far_lit=%g\n",
                    pair.ps,pair.family,near_widened.widened,near_widened.k_uploaded,frame_far.widened,frame_far.k_uploaded,
                    static_cast<unsigned long long>(near_gained.hash),static_cast<unsigned long long>(near_widened.hash),static_cast<unsigned long long>(frame_far.hash),max_codes,differing,
                    a.h_peak,c.h_peak,a.h_energy,c.h_energy,a.v_peak,c.v_peak,a.v_energy,c.v_energy,a.lit,c.lit);
        require(near_gained.widened==0u&&near_widened.widened==1u&&frame_far.widened==1u,"gained, forced widened, widened");
        require(near_gained.k_uploaded==1.&&near_widened.k_uploaded==1.&&std::fabs(frame_far.k_uploaded-K)<=1e-6,"k uploads: 1 near, K far");
        require(a.max>0&&c.max>0,"the light-map term is visible");
    }
    api(d->SetTexture(0,nullptr),"unbind art");api(d->SetTexture(1,nullptr),"unbind mask");api(d->SetTexture(2,nullptr),"unbind light map");api(d->SetTexture(3,nullptr),"unbind s3");api(d->SetTexture(4,nullptr),"unbind s4");
}
