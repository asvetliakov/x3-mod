// Included in the actual D3D Fixture. Hull emissive widening live proof
// (--hull-emissive-widening K[,B], X3M_HULL_EMISSIVE_WIDENING=K[,B];
// docs/architecture/hull-emissive-widening.md sections 3 and 8.5). Target
// 256 x 256, the fixture camera.
//
// Script "strips" (default): a square quad (NDC centred, UV 0..1) drawn with
// the standard DEFAULT hull pair vs_494fe349b8bc12ec / ps_7c83ed50c9894e44
// under X3M_HULL_LIGHTMAP_GAIN=4, the light map a 64-texel A8R8G8B8 texture
// with a full box-filtered mip chain (a DXT1 copy for two of them): a 1-texel
// horizontal strip at v = 0.5 plus a 1-texel vertical strip at u = 0.5
// ("strip1"), 4-texel strips ("strip4") or one 32-texel centred panel
// ("panel"; also drawn rotated 45 degrees on screen, "panel_rot45", for the
// gate's diagonal corner). The block's per-pixel k is the light map's own texel footprint,
// rho = K . texels per pixel = K . 64 / quad px, so the quad size selects rho:
// 192 px (rho 1 for K = 3: k = 1, the un-widened image), 96 (rho 2), 64
// (rho 3 = K), 32 (rho 6, clamped to K) and 128 (rho 1.5). The quad drifts
// 1.125 px per frame over eight sub-pixel phases. Per frame: the light-map
// term of the quad (pixel minus the quad's lower quartile), the horizontal strip's
// per-column peak/energy/width (columns u in [0.1, 0.4], clear of the
// vertical strip), the vertical strip's per-row metrics (rows v in
// [0.1, 0.4]), the panel's interior mean, the panel's edge rim (the largest
// term within +-4 px of a panel edge away from the corners) and corner (the
// same at the four corners), the count of pixels above 1.005 x the interior
// (a boosted panel pixel: the gate's t > 0), the lit count, the uploaded c217.yz (last_pixel_abi[5], [6]) and
// the widened-draw count. Every (map, quad) is drawn at every phase with the
// widened variant and again with the widening suppressed (the gained texld
// variant: LIGHTMAP_WIDEN_BASE), so every comparison is same-phase and
// same-process. Then one F4-off frame (no gain, no widening), a Reset and a
// last frame, and the oblique case (perspective rows, p = 0.6) with LINEAR
// and ANISOTROPIC(16) filtering of the light-map stage at quad 192 and 64.
//
// Script "programs": one reviewed pair per family group the fixture can drive,
// each drawn at rho 1 with the gained variant (suppressed), at rho 1 with the
// widened block (k = 1, t = 0: the block at k = 1 against texld, FP16 codes compared)
// and at rho 3; the transformer's own widened bytes are also created through
// CreatePixelShader here and disassembled with the game's d3dx9_37.dll for the
// slot count. Then every ps_*.bin of the local corpus beside the pair is
// transformed and created: the 100 light-map programs must all be accepted.
struct WidenMetrics { double median=0, max=0, lit=0, h_peak=0, h_energy=0, h_width=0, h_row=0, v_peak=0, v_energy=0, v_width=0, v_col=0, panel=0, rim=0, corner=0, boosted=0; };
// Light-map term of a quad at pixels [x0, x0+size) x [y0, y0+size) (lane 0).
// rotated: the window is the inscribed square of a quad rotated 45 degrees on
// screen carrying the 24-texel panel; the panel interior is the centre
// +-size/6 (inside the rotated panel, whose inscribed half-width is 8.5 px),
// the corner the window's maximum (the rotated panel's corners lie on the
// axes at +-12 px), the rim unused.
static WidenMetrics widen_metrics(const std::vector<float>& image, unsigned width, int x0, int y0, int size, bool rotated=false) {
    WidenMetrics m{};
    const int inset=4, a0=x0+inset, a1=x0+size-inset, b0=y0+inset, b1=y0+size-inset;
    std::vector<float> values; values.reserve(std::size_t(size)*size);
    for(int y=b0;y<b1;++y)for(int x=a0;x<a1;++x)values.push_back(image[(std::size_t(y)*width+x)*4]);
    // The hull's own colour (the term's baseline) is the window's lower quartile: the two strips widened to
    // K px on the 32-px quad light more than half of the window, so the median would sit on a lit pixel.
    std::vector<float> sorted(values); std::nth_element(sorted.begin(),sorted.begin()+sorted.size()/4,sorted.end());
    m.median=sorted[sorted.size()/4];
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
    // Panel interior: the centre +-3/16 of the quad (the panel spans +-1/4);
    // the rim: the largest term within +-4 px of a panel edge away from the
    // corners (the 1-D edge of the design's 1.33 bound); the corner: the same
    // within +-4 px of a panel corner in both axes; boosted: the quad's pixels
    // above 1.005 x the interior mean.
    const int c=rotated?size/6:size*3/16, e=size/4; unsigned n=0;
    for(int y=y0+size/2-c;y<y0+size/2+c;++y)for(int x=x0+size/2-c;x<x0+size/2+c;++x){ m.panel+=term(x,y); ++n; }
    m.panel/=n;
    for(int y=b0;y<b1;++y)for(int x=a0;x<a1;++x){
        const int dx=std::abs(x-(x0+size/2)),dy=std::abs(y-(y0+size/2)),d=std::max(dx,dy);
        const bool near_x=dx>=e-4&&dx<=e+4,near_y=dy>=e-4&&dy<=e+4;
        const double t=term(x,y);
        if(rotated)m.corner=std::max(m.corner,t);
        else if(d>=e-4&&d<=e+4){ if(near_x&&near_y)m.corner=std::max(m.corner,t); else m.rim=std::max(m.rim,t); }
        if(m.panel>0&&t>1.005*m.panel)++m.boosted;
    }
    return m;
}
// A square A8R8G8B8 light map with a complete box-filtered mip chain: level 0
// is `authored` (luminance 0..1 per texel), every level the exact block mean
// rounded to 8 bits (alpha = the same value).
static std::vector<float> widen_level(const std::vector<float>& authored, unsigned size, unsigned level) {
    const unsigned n=std::max(1u,size>>level), block=size/n;
    std::vector<float> out(n*n);
    for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
        double sum=0;
        for(unsigned by=0;by<block;++by)for(unsigned bx=0;bx<block;++bx)sum+=authored[(y*block+by)*size+x*block+bx];
        out[y*n+x]=float(sum/(double(block)*block));
    }
    return out;
}
void widen_lightmap(Com<IDirect3DTexture9>& t, unsigned size, const std::vector<float>& authored, const char* what) {
    api(d->CreateTexture(size,size,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t.p,nullptr),what);
    const DWORD levels=t->GetLevelCount();
    require(levels>=2&&(size>>(levels-1))==1,"a complete mip chain");
    for(DWORD level=0;level<levels;++level){
        const unsigned n=size>>level; const auto values=widen_level(authored,size,level);
        D3DLOCKED_RECT locked{};api(t->LockRect(level,&locked,nullptr,0),what);
        for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
            const unsigned v=unsigned(std::lround(values[y*n+x]*255.));
            const DWORD texel=(v<<24)|(v<<16)|(v<<8)|v;
            std::memcpy(static_cast<char*>(locked.pBits)+y*locked.Pitch+x*4,&texel,4);
        }
        api(t->UnlockRect(level),what);
    }
}
// The same map as DXT1 (documented block layout: two RGB565 endpoints, 2-bit
// indices, four-colour mode). Grey levels: per 4 x 4 block the endpoints are
// the block's maximum and minimum, every texel the nearest of the four
// palette entries. Levels below 4 x 4 fill one block from the clamped texel.
bool widen_lightmap_dxt1(Com<IDirect3DTexture9>& t, unsigned size, const std::vector<float>& authored) {
    if(FAILED(d->CreateTexture(size,size,0,0,D3DFMT_DXT1,D3DPOOL_MANAGED,&t.p,nullptr))||!t.p)return false;
    const DWORD levels=t->GetLevelCount();
    auto grey565=[](float v){ const unsigned r=unsigned(std::lround(std::min(1.f,std::max(0.f,v))*31.f)),g=unsigned(std::lround(std::min(1.f,std::max(0.f,v))*63.f)); return (r<<11)|(g<<5)|r; };
    auto decoded=[](unsigned c){ return (double((c>>11)&31)/31.*2.+double((c>>5)&63)/63.)/3.; };
    for(DWORD level=0;level<levels;++level){
        const unsigned n=std::max(1u,size>>level); const auto values=widen_level(authored,size,level);
        const unsigned blocks=(n+3)/4;
        D3DLOCKED_RECT locked{};if(FAILED(t->LockRect(level,&locked,nullptr,0)))return false;
        for(unsigned by=0;by<blocks;++by)for(unsigned bx=0;bx<blocks;++bx){
            float lo=1.f,hi=0.f; float texel[16];
            for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i){
                const unsigned x=std::min(n-1,bx*4+i),y=std::min(n-1,by*4+j);
                texel[j*4+i]=values[y*n+x]; lo=std::min(lo,texel[j*4+i]); hi=std::max(hi,texel[j*4+i]);
            }
            unsigned c0=grey565(hi),c1=grey565(lo); if(c0<c1)std::swap(c0,c1);
            const double p0=decoded(c0),p1=decoded(c1),palette[4]={p0,p1,(2.*p0+p1)/3.,(p0+2.*p1)/3.};
            std::uint32_t indices=0;
            for(unsigned k=0;k<16;++k){
                unsigned best=0; double error=1e9;
                for(unsigned q=0;q<(c0==c1?1u:4u);++q){ const double e=std::fabs(palette[q]-texel[k]); if(e<error){error=e;best=q;} }
                indices|=best<<(2*k);
            }
            unsigned char* out=static_cast<unsigned char*>(locked.pBits)+by*locked.Pitch+bx*8;
            out[0]=c0&255;out[1]=c0>>8;out[2]=c1&255;out[3]=c1>>8;std::memcpy(out+4,&indices,4);
        }
        if(FAILED(t->UnlockRect(level)))return false;
    }
    return true;
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
bool widen_suppress=false; // scope(): MotionOutputFixtureConfig::suppress_lightmap_widen
void run_lightmap_widen(const char* bootstrap_vertex) {
    require(seam&&enabled&&hdr&&hdr_readback&&!taa&&camera,"hull emissive widening live needs the HDR seam, its readback, the camera and TAA off");
    require(hull_toggle!=nullptr&&emission_status!=nullptr&&last_pixel_abi!=nullptr,"hull toggle, status and ABI exports");
    require(W==256&&H==256,"the widening script runs at 256 x 256");
    char text[96]{};
    const double configured=GetEnvironmentVariableA("X3M_HULL_LIGHTMAP_GAIN",text,sizeof text)>0?std::atof(text):1.;
    require(configured==4.,"the live proof uses light-map gain 4");
    double K=1,B=1;bool widen=false;
    if(GetEnvironmentVariableA("X3M_HULL_EMISSIVE_WIDENING",text,sizeof text)>0){
        const int fields=std::sscanf(text,"%lf,%lf",&K,&B);
        require(fields>=1&&K>1,"widening setting");if(fields==1)B=K;widen=true;
    }
    char script_text[16]{};GetEnvironmentVariableA("X3M_FIXTURE_WIDEN_SCRIPT",script_text,sizeof script_text);
    const bool programs=!std::strcmp(script_text,"programs");
    // X3M_FIXTURE_WIDEN_ANISO=1: the light-map stage samples ANISOTROPIC 16 on every frame (the axis-separated
    // gate's coarse fetches double one axis; with an isotropic LINEAR minification the hardware LOD is the larger
    // axis's, so both coarse fetches collapse to the isotropic 2 k fetch; with anisotropic filtering the LOD is
    // the smaller axis's and the taps run along the doubled one).
    char aniso_text[4]{};const bool aniso_all=GetEnvironmentVariableA("X3M_FIXTURE_WIDEN_ANISO",aniso_text,sizeof aniso_text)==1&&aniso_text[0]=='1';
    std::printf("LIGHTMAP_WIDEN_FILTER aniso=%u\n",aniso_all?1u:0u);
    const std::string bootstrap_path(bootstrap_vertex);const auto slash=bootstrap_path.find_last_of("/\\");
    const auto folder=slash==std::string::npos?std::string{}:bootstrap_path.substr(0,slash+1);
    // Light maps (64 texels): 1-texel strips, 4-texel strips, one centred 32-texel panel.
    constexpr unsigned S=64;
    std::vector<float> strip1(S*S,0.f),strip4(S*S,0.f),panel(S*S,0.f),panel24(S*S,0.f);
    for(unsigned i=0;i<S;++i){strip1[(S/2)*S+i]=1.f;strip1[i*S+S/2]=1.f;}
    for(unsigned i=0;i<S;++i)for(unsigned j=S/2-2;j<S/2+2;++j){strip4[j*S+i]=1.f;strip4[i*S+j]=1.f;}
    for(unsigned y=S/4;y<S*3/4;++y)for(unsigned x=S/4;x<S*3/4;++x)panel[y*S+x]=1.f;
    for(unsigned y=20;y<44;++y)for(unsigned x=20;x<44;++x)panel24[y*S+x]=1.f; // the rotated row: 24 texels, so the 45-degree panel fills under half of the inscribed window
    Com<IDirect3DTexture9> strip1_map,strip4_map,panel_map,panel24_map,strip1_dxt1,panel_dxt1;
    widen_lightmap(strip1_map,S,strip1,"strip1 light map");widen_lightmap(strip4_map,S,strip4,"strip4 light map");widen_lightmap(panel_map,S,panel,"panel light map");widen_lightmap(panel24_map,S,panel24,"panel24 light map");
    const bool dxt1=widen_lightmap_dxt1(strip1_dxt1,S,strip1)&&widen_lightmap_dxt1(panel_dxt1,S,panel);
    std::printf("LIGHTMAP_WIDEN_DXT1 available=%u\n",dxt1?1u:0u);
    Com<IDirect3DTexture9> art,mask,normal;
    for(unsigned which=0;which<3;++which){
        auto& t=which==0?art:which==1?mask:normal;
        api(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t.p,nullptr),"widen art");
        D3DLOCKED_RECT locked{};api(t->LockRect(0,&locked,nullptr,0),"widen art lock");
        const DWORD value=which==0?0x40202020u:which==1?0x40000000u:0x80008000u;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)std::memcpy(static_cast<char*>(locked.pBits)+y*locked.Pitch+x*4,&value,4);
        api(t->UnlockRect(0),"widen art unlock");
    }
    // The quad: two triangles, NDC [-0.5, 0.5]^2 before the rows' scale, UV 0..1 (u right, v down on
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
    // One frame: the pair, light map, quad size in pixels, drift and sampler filter; returns the FP16 image.
    struct Frame { double scale_x, scale_y; unsigned widened, gained, filter_sets; std::uint64_t hash; std::vector<float> image; };
    auto draw_frame=[&](const WidenPair& pair,IDirect3DVertexShader9* pvs,IDirect3DPixelShader9* pps,IDirect3DTexture9* light,unsigned quad_px,float drift_px,float perspective,bool anisotropic,bool rotated=false){
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
        api(d->SetSamplerState(stage,D3DSAMP_MINFILTER,anisotropic||aniso_all?D3DTEXF_ANISOTROPIC:D3DTEXF_LINEAR),"light-map min filter");
        api(d->SetSamplerState(stage,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR),"light-map mag filter");
        api(d->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR),"light-map mip filter");
        // MAXANISOTROPY 16 as the game holds it on every stage (run231's sampler log): the route raises only MINFILTER.
        api(d->SetSamplerState(stage,D3DSAMP_MAXANISOTROPY,16u),"light-map anisotropy");
        api(d->SetSamplerState(stage,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"light-map u");api(d->SetSamplerState(stage,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"light-map v");
        api(d->SetVertexShader(pvs),"pair VS");api(d->SetPixelShader(pps),"pair PS");
        // diag(quad / 128) (the base quad is 128 px) plus the drift (NDC shift) and the oblique perspective (w_clip = 1 + p x).
        const float s=float(quad_px)/128.f;
        float m[16];std::memcpy(m,identity,sizeof m);m[0]=m[5]=s;m[10]=1.f;m[15]=1.f;
        if(rotated){ const float c45=0.70710678f*s; m[0]=c45;m[1]=-c45;m[4]=c45;m[5]=c45; } // 45 degrees on screen
        m[3]=2.f*drift_px/float(W);m[7]=-2.f*drift_px/float(H);m[12]=perspective;
        api(d->SetVertexShaderConstantF(24,m,4),"widen rows");
        const Snapshot before=snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,2),"widen quad draw");++draw_index;
        compare(before,snapshot(),"widen quad draw");
        Frame f{};
        f.gained=emission_status(d.p,78);f.widened=emission_status(d.p,80);f.filter_sets=emission_status(d.p,82);
        // The route raises a LINEAR light-map stage to ANISOTROPIC for the widened draw and restores it in undo.
        DWORD filter_after=~0ul;api(d->GetSamplerState(stage,D3DSAMP_MINFILTER,&filter_after),"light-map min filter after the draw");
        require(filter_after==(anisotropic||aniso_all?DWORD(D3DTEXF_ANISOTROPIC):DWORD(D3DTEXF_LINEAR)),"the light-map stage's MINFILTER is the fixture's own again after the draw");
        require(f.filter_sets==(f.widened&&!(anisotropic||aniso_all)?1u:0u),"MINFILTER raised exactly on a widened draw of a LINEAR stage");
        float abi[8]{};api(last_pixel_abi(d.p,abi,8),"last pixel ABI");f.scale_x=abi[5];f.scale_y=abi[6];
        rows(0,0,0);
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
    const double expected_scale=widen?double(float(S)*float(K))*double(float(S)*float(K)):0.;
    auto rho=[&](unsigned quad_px){ return widen?K*double(S)/double(quad_px):0.; };
    auto k_of=[&](unsigned quad_px){ return widen?std::min(K,std::max(1.,rho(quad_px))):0.; };
    auto load_pair=[&](const WidenPair& pair,Com<IDirect3DVertexShader9>& pvs,Com<IDirect3DPixelShader9>& pps){
        const auto v=load((folder+"vs_"+pair.vs+".bin").c_str()),p=load((folder+"ps_"+pair.ps+".bin").c_str());
        require(fnv(v.data(),v.size()*4)==std::strtoull(pair.vs,nullptr,16)&&fnv(p.data(),p.size()*4)==std::strtoull(pair.ps,nullptr,16),"original pair identity");
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&pvs.p),"pair VS");
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&pps.p),"pair PS");
        return p;
    };
    auto origin=[&](unsigned quad_px,float drift){ return int(W)/2-int(quad_px)/2+int(drift); };
    if(!programs){
        const WidenPair& pair=widen_pairs[0];
        Com<IDirect3DVertexShader9> pvs;Com<IDirect3DPixelShader9> pps;load_pair(pair,pvs,pps);
        struct Case { const char* map; IDirect3DTexture9* light; unsigned quad; const char* label; bool rotated; } cases[]={
            {"strip1",strip1_map.p,192,"rho1",false},{"strip1",strip1_map.p,96,"rho2",false},{"strip1",strip1_map.p,64,"rho3",false},{"strip1",strip1_map.p,32,"rho6",false},
            {"strip4",strip4_map.p,128,"rho1.5",false},{"strip4",strip4_map.p,64,"rho3",false},
            {"panel",panel_map.p,64,"rho3",false},{"panel",panel_map.p,192,"rho1",false},{"panel_rot45",panel24_map.p,64,"rho3",true},
            {"strip1_dxt1",strip1_dxt1.p,64,"rho3",false},{"panel_dxt1",panel_dxt1.p,64,"rho3",false}};
        bool on=true;unsigned step=0;
        for(const auto& c:cases){
            if(!c.light)continue; // DXT1 unavailable on this backend (reported above)
            for(unsigned variant=0;variant<2;++variant){
                widen_suppress=variant==1;
                for(unsigned phase=0;phase<8;++phase){
                    const float drift=1.125f*float(phase);
                    const Frame f=draw_frame(pair,pvs.p,pps.p,c.light,c.quad,drift,0.f,false,c.rotated);
                    // The rotated quad's inscribed axis-aligned square is quad / sqrt(2) wide: analyse 44 px of it.
                    const int window=c.rotated?int(c.quad)*11/16:int(c.quad);
                    const WidenMetrics m=widen_metrics(f.image,W,origin(unsigned(window),drift),origin(unsigned(window),drift),window,c.rotated);
                    std::printf("%s frame=%llu step=%u map=%s quad=%u rotated=%u label=%s rho=%.9g k=%.9g phase=%u on=%u abi_scale=%.9g,%.9g expected_scale=%.9g gained_draws=%u widened_draws=%u filter_sets=%u median=%.9g max=%.9g lit=%g h_peak=%.9g h_energy=%.9g h_width=%.3f h_row=%.2f v_peak=%.9g v_energy=%.9g v_width=%.3f v_col=%.2f panel=%.9g rim=%.9g corner=%.9g boosted=%g hdr_hash=%016llx\n",
                                widen_suppress?"LIGHTMAP_WIDEN_BASE":"LIGHTMAP_WIDEN",frame-1,step++,c.map,c.quad,c.rotated?1u:0u,c.label,rho(c.quad),k_of(c.quad),phase,unsigned(on),f.scale_x,f.scale_y,expected_scale,f.gained,f.widened,f.filter_sets,
                                m.median,m.max,m.lit,m.h_peak,m.h_energy,m.h_width,m.h_row,m.v_peak,m.v_energy,m.v_width,m.v_col,m.panel,m.rim,m.corner,m.boosted,static_cast<unsigned long long>(f.hash));
                    require(f.gained==1u,"the gained variant binds on every F4-on frame");
                    require(f.scale_x==expected_scale&&f.scale_y==expected_scale,"c217.yz carry (size . K)^2 of the bound light map (0 with the option off)");
                    require(f.widened==(widen&&!widen_suppress?1u:0u),"the widened variant binds on every gain draw unless suppressed");
                    require(m.max>0&&m.lit>0,"the light-map term is visible on the quad");
                }
            }
        }
        widen_suppress=false;
        // F4 off at rho 3: neither the gain nor the widening.
        {
            const int state=hull_toggle(d.p,1);on=false;require(state==0,"F4 off");
            const Frame f=draw_frame(pair,pvs.p,pps.p,strip1_map.p,64,0.f,0.f,false);
            const WidenMetrics m=widen_metrics(f.image,W,origin(64,0.f),origin(64,0.f),64);
            std::printf("LIGHTMAP_WIDEN_OFF frame=%llu gained_draws=%u widened_draws=%u abi_scale=%.9g h_peak=%.9g v_peak=%.9g hdr_hash=%016llx\n",frame-1,f.gained,f.widened,f.scale_x,m.h_peak,m.v_peak,static_cast<unsigned long long>(f.hash));
            require(f.gained==0u&&f.widened==0u,"F4 off drops the gain and the widening together");
            require(hull_toggle(d.p,1)==1,"F4 on again");on=true;
        }
        // Reset: the variants are re-created through the registration path; the frame binds the widened one again.
        {
            reset();
            const Frame f=draw_frame(pair,pvs.p,pps.p,strip1_map.p,64,0.f,0.f,false);
            const WidenMetrics m=widen_metrics(f.image,W,origin(64,0.f),origin(64,0.f),64);
            std::printf("LIGHTMAP_WIDEN_RESET frame=%llu gained_draws=%u widened_draws=%u abi_scale=%.9g h_peak=%.9g v_peak=%.9g hdr_hash=%016llx\n",frame-1,f.gained,f.widened,f.scale_x,m.h_peak,m.v_peak,static_cast<unsigned long long>(f.hash));
            require(f.gained==1u&&f.widened==(widen?1u:0u)&&f.scale_x==expected_scale,"after Reset the frame binds the widened variant again");
        }
        // Oblique: perspective rows (w_clip = 1 + 0.6 x), LINEAR against ANISOTROPIC 16 on the light-map stage,
        // at rho 1 (k = 1) and rho 3 (k = K). Whole-quad max/sum of the term and the image hash.
        for(unsigned quad_px:{192u,64u})for(unsigned aniso=0;aniso<2;++aniso){
            const Frame f=draw_frame(pair,pvs.p,pps.p,strip1_map.p,quad_px,0.f,.6f,aniso!=0);
            // The oblique quad: x' = x / (1 + 0.6 x) maps [-0.5, 0.5] to [-0.714, 0.385] of the unit quad; analyse the inscribed square [-0.35, 0.35].
            const int size=int(quad_px)*35/50,x0=int(W)/2-size/2,y0=int(H)/2-size/2;
            double peak=0,sum=0;std::vector<float> values;
            for(int y=y0;y<y0+size;++y)for(int x=x0;x<x0+size;++x)values.push_back(f.image[(std::size_t(y)*W+x)*4]);
            std::vector<float> sorted(values);std::nth_element(sorted.begin(),sorted.begin()+sorted.size()/2,sorted.end());
            const double median=sorted[sorted.size()/2];
            for(float v:values){const double t=double(v)-median;peak=std::max(peak,t);sum+=t;}
            std::printf("LIGHTMAP_WIDEN_ANISO frame=%llu quad=%u rho=%.9g filter=%s abi_scale=%.9g widened_draws=%u peak=%.9g sum=%.9g hdr_hash=%016llx\n",
                        frame-1,quad_px,rho(quad_px),aniso?"aniso16":"linear",f.scale_x,f.widened,peak,sum,static_cast<unsigned long long>(f.hash));
        }
        api(d->SetTexture(0,nullptr),"unbind art");api(d->SetTexture(1,nullptr),"unbind mask");api(d->SetTexture(2,nullptr),"unbind light map");
        return;
    }
    // Script "programs": every drivable family pair: rho 1 gained (suppressed), rho 1 widened (k = 1), rho 3 (k = K).
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
    const x3m::renderer::HullLightmapWiden parameters{float(K),float(B)};
    for(const auto& pair:widen_pairs){
        Com<IDirect3DVertexShader9> pvs;Com<IDirect3DPixelShader9> pps;const auto original=load_pair(pair,pvs,pps);
        // The transformer's own bytes (static gain 4, K and B composed): CreatePixelShader acceptance and the D3DX slot count.
        std::vector<std::uint32_t> gained,widened;bool fill_applied=false,gain_applied=false,widen_applied=false;
        const auto gr=x3m::renderer::linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),0.f,4.f,gained,true,fill_applied,gain_applied,false);
        const auto wr=x3m::renderer::linear_material_hull_lightmap_gain_pixel_variant(original.data(),original.size(),0.f,4.f,widened,true,fill_applied,gain_applied,false,&parameters,&widen_applied);
        require(gr==x3m::renderer::LinearMaterialResult::Applied&&wr==x3m::renderer::LinearMaterialResult::Applied&&gain_applied&&widen_applied,"the pair's programs take the gain and the widening");
        Com<IDirect3DPixelShader9> created_gained,created_widened;
        const HRESULT create_gained=d->CreatePixelShader(reinterpret_cast<const DWORD*>(gained.data()),&created_gained.p);
        const HRESULT create_widened=d->CreatePixelShader(reinterpret_cast<const DWORD*>(widened.data()),&created_widened.p);
        std::printf("LIGHTMAP_WIDEN_PROGRAM ps=%s family=%s words=%u gained_words=%u widened_words=%u create_gained=%08lx create_widened=%08lx gained_slots=%d widened_slots=%d\n",
                    pair.ps,pair.family,unsigned(original.size()),unsigned(gained.size()),unsigned(widened.size()),create_gained,create_widened,slots_of(gained),slots_of(widened));
        require(SUCCEEDED(create_widened)&&created_widened.p,"CreatePixelShader accepts the widened program");
        widen_suppress=true;const Frame near_gained=draw_frame(pair,pvs.p,pps.p,strip1_map.p,192,0.f,0.f,false);
        widen_suppress=false;const Frame near_widened=draw_frame(pair,pvs.p,pps.p,strip1_map.p,192,0.f,0.f,false);
        const Frame frame_far=draw_frame(pair,pvs.p,pps.p,strip1_map.p,64,0.f,0.f,false);
        unsigned max_codes=0,differing=0;
        for(std::size_t i=0;i<near_gained.image.size();++i){
            const unsigned codes=unsigned(std::abs(int(float_to_half(near_gained.image[i]))-int(float_to_half(near_widened.image[i]))));
            if(codes){++differing;max_codes=std::max(max_codes,codes);}
        }
        const WidenMetrics a=widen_metrics(near_gained.image,W,origin(192,0.f),origin(192,0.f),192),c=widen_metrics(frame_far.image,W,origin(64,0.f),origin(64,0.f),64);
        std::printf("LIGHTMAP_WIDEN_PAIR ps=%s family=%s near_widened=%u near_scale=%.9g far_widened=%u far_scale=%.9g filter_sets=%u,%u near_hash=%016llx widened_hash=%016llx far_hash=%016llx max_codes=%u differing=%u near_h_peak=%.9g far_h_peak=%.9g near_h_energy=%.9g far_h_energy=%.9g near_v_peak=%.9g far_v_peak=%.9g near_v_energy=%.9g far_v_energy=%.9g near_h_width=%.3f far_h_width=%.3f near_lit=%g far_lit=%g\n",
                    pair.ps,pair.family,near_widened.widened,near_widened.scale_x,frame_far.widened,frame_far.scale_x,near_widened.filter_sets,frame_far.filter_sets,
                    static_cast<unsigned long long>(near_gained.hash),static_cast<unsigned long long>(near_widened.hash),static_cast<unsigned long long>(frame_far.hash),max_codes,differing,
                    a.h_peak,c.h_peak,a.h_energy,c.h_energy,a.v_peak,c.v_peak,a.v_energy,c.v_energy,a.h_width,c.h_width,a.lit,c.lit);
        require(near_gained.widened==0u&&near_widened.widened==1u&&frame_far.widened==1u,"gained (suppressed), widened at k = 1, widened at k = K");
        require(near_widened.scale_x==expected_scale&&frame_far.scale_x==expected_scale,"the footprint lanes carry (size . K)^2");
        require(a.max>0&&c.max>0,"the light-map term is visible");
    }
    // Every ps_*.bin of the corpus folder: the transformer's verdict and CreatePixelShader on every widened program.
    {
        unsigned programs_seen=0,applied=0,created=0;int max_slots=0;
        WIN32_FIND_DATAA found{};const HANDLE find=FindFirstFileA((folder+"ps_*.bin").c_str(),&found);
        if(find!=INVALID_HANDLE_VALUE){
            do{
                ++programs_seen;
                const auto code=load((folder+found.cFileName).c_str());
                std::vector<std::uint32_t> out;bool fa=false,ga=false,wa=false;
                const auto r=x3m::renderer::linear_material_hull_lightmap_gain_pixel_variant(code.data(),code.size(),0.f,4.f,out,true,fa,ga,false,&parameters,&wa);
                if(r!=x3m::renderer::LinearMaterialResult::Applied||!wa)continue;
                ++applied;
                Com<IDirect3DPixelShader9> created_program;
                const HRESULT hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(out.data()),&created_program.p);
                if(SUCCEEDED(hr)&&created_program.p)++created;
                else std::printf("LIGHTMAP_WIDEN_REJECTED ps=%s create=%08lx\n",found.cFileName,hr);
                max_slots=std::max(max_slots,slots_of(out));
            }while(FindNextFileA(find,&found));
            FindClose(find);
        }
        std::printf("LIGHTMAP_WIDEN_ALL programs=%u applied=%u created=%u max_slots=%d\n",programs_seen,applied,created,max_slots);
        require(applied==created,"CreatePixelShader accepts every widened program of the corpus");
    }
    api(d->SetTexture(0,nullptr),"unbind art");api(d->SetTexture(1,nullptr),"unbind mask");api(d->SetTexture(2,nullptr),"unbind light map");api(d->SetTexture(3,nullptr),"unbind s3");api(d->SetTexture(4,nullptr),"unbind s4");
}
