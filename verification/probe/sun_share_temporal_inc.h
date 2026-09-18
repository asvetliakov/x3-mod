// Focused mode of temporal_pass_fixture.cpp; production pass and copy program.
struct SunCopyGuard {
    using Fn=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE);
    static inline Fn original=nullptr;static inline unsigned cross_format=0;
    IDirect3DDevice9* device;void** previous;void* table[119];
    static HRESULT WINAPI copy(IDirect3DDevice9* d,IDirect3DSurface9* a,const RECT* ar,IDirect3DSurface9* b,const RECT* br,D3DTEXTUREFILTERTYPE filter){
        D3DSURFACE_DESC x{},y{};if(a&&b&&SUCCEEDED(a->GetDesc(&x))&&SUCCEEDED(b->GetDesc(&y))&&(x.Format==D3DFMT_G32R32F||x.Format==D3DFMT_A32B32G32R32F)&&y.Format==D3DFMT_R32F){++cross_format;return D3DERR_INVALIDCALL;}
        return original(d,a,ar,b,br,filter);
    }
    explicit SunCopyGuard(IDirect3DDevice9* d):device(d),previous(*reinterpret_cast<void***>(d)){
        std::copy(previous,previous+119,table);std::memcpy(&original,&table[34],sizeof original);
        auto fn=&copy;std::memcpy(&table[34],&fn,sizeof fn);*reinterpret_cast<void***>(device)=table;cross_format=0;
    }
    ~SunCopyGuard(){*reinterpret_cast<void***>(device)=previous;}
};
std::vector<unsigned char> sun_read(IDirect3DDevice9* d,IDirect3DTexture9* input){
    D3DSURFACE_DESC desc{};check("sun read desc",input->GetLevelDesc(0,&desc));const unsigned stride=desc.Format==D3DFMT_R32F?4:desc.Format==D3DFMT_G32R32F?8:16;
    Com<IDirect3DSurface9> source,copy;check("sun read level",input->GetSurfaceLevel(0,&source.p));
    check("sun read copy",d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy.p,nullptr));
    check("sun readback",d->GetRenderTargetData(source.p,copy.p));D3DLOCKED_RECT lock{};check("sun lock",copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<unsigned char> bytes(desc.Width*desc.Height*stride);
    for(UINT y=0;y<desc.Height;++y)std::memcpy(bytes.data()+y*desc.Width*stride,static_cast<char*>(lock.pBits)+y*lock.Pitch,desc.Width*stride);
    check("sun unlock",copy->UnlockRect());return bytes;
}
void sun_lane_cases(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS& pp,Compiler compiler,const DWORD* resolver){
    TemporalPass ordinary,lane;
    const auto* copy=reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_program());
    check("sun ordinary init",ordinary.initialize(d,nullptr,resolver,nullptr,nullptr,copy));
    check("sun lane init",lane.initialize(d,nullptr,resolver,nullptr,nullptr,copy));
    unsigned histories=0,failures=0,resizes=0;
    for(unsigned generation=0;generation<2;++generation){
        {
            Fixture f(d,compiler);
            // The enhanced RT2 in both lane formats: G32R32F and, under the receiver-depth
            // option (docs/architecture/shadow-receiver-depth.md), A32B32G32R32F with the
            // clip w in .b/.a; the R32F history is the .r lane of either, bit for bit. The
            // width alternates between consecutive iterations so both passes' histories
            // restart together (the lane pass runs two extra frames per iteration).
            for(const D3DFORMAT wide:{D3DFMT_G32R32F,D3DFMT_A32B32G32R32F})for(const UINT width:{W,W/2}){
                const unsigned lanes=wide==D3DFMT_G32R32F?2u:4u;
                Com<IDirect3DTexture9> color,depth,enhanced;Com<IDirect3DSurface9> depth_level,enhanced_level,depth_upload,enhanced_upload;
                check("sun color",d->CreateTexture(width,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&color.p,nullptr));
                check("sun ordinary depth",d->CreateTexture(width,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth.p,nullptr));
                check("sun enhanced depth",d->CreateTexture(width,H,1,D3DUSAGE_RENDERTARGET,wide,D3DPOOL_DEFAULT,&enhanced.p,nullptr));
                check("sun depth level",depth->GetSurfaceLevel(0,&depth_level.p));check("sun enhanced level",enhanced->GetSurfaceLevel(0,&enhanced_level.p));
                check("sun depth staging",d->CreateOffscreenPlainSurface(width,H,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&depth_upload.p,nullptr));
                check("sun enhanced staging",d->CreateOffscreenPlainSurface(width,H,wide,D3DPOOL_SYSTEMMEM,&enhanced_upload.p,nullptr));
                std::vector<float> expected(width*H);
                const float depths[]={-1,0,.1234567f,.625f,.99999994f,1};
                const float shares[]={-1,0,.375f,1,NAN,INFINITY};
                D3DLOCKED_RECT a{},b{},c{};check("sun depth upload lock",depth_upload->LockRect(&a,nullptr,0));check("sun enhanced upload lock",enhanced_upload->LockRect(&b,nullptr,0));check("sun color upload lock",color->LockRect(0,&c,nullptr,0));
                for(UINT y=0;y<H;++y)for(UINT x=0;x<width;++x){const float r=depths[(x+y)%6],g=shares[(2*x+y)%6];expected[y*width+x]=r;
                    std::memcpy(static_cast<char*>(a.pBits)+y*a.Pitch+x*4,&r,4);const float lane_texel[]={r,g,37000.f+float(x),-1.f};std::memcpy(static_cast<char*>(b.pBits)+y*b.Pitch+x*lanes*4,lane_texel,lanes*4);
                    const unsigned short rgba[]={toHalf((x&1)?.75f:.25f),toHalf(.5f),toHalf(.25f),toHalf(.75f)};std::memcpy(static_cast<char*>(c.pBits)+y*c.Pitch+x*8,rgba,8);
                }
                check("sun depth unlock",depth_upload->UnlockRect());check("sun enhanced unlock",enhanced_upload->UnlockRect());check("sun color unlock",color->UnlockRect(0));
                check("sun depth upload",d->UpdateSurface(depth_upload.p,nullptr,depth_level.p,nullptr));check("sun enhanced upload",d->UpdateSurface(enhanced_upload.p,nullptr,enhanced_level.p,nullptr));
                auto inputs=[&](bool enhanced_input){auto in=f.inputs();in.color=color.p;in.depth_snapshot=nullptr;in.current_depth=enhanced_input?enhanced.p:depth.p;
                    in.width=width;in.height=H;in.epoch=10+generation;in.reactive_policy=x3m::renderer::ReactivePolicy::DerivedFromDepthSentinel;return in;};
                auto run=[&](TemporalPass& pass,FrameInputs in,bool enhanced_input,unsigned fail_draw=0){
                    f.hostile();Snapshot before(d);check("sun begin",d->BeginScene());Output out{};HRESULT hr;
                    {SunCopyGuard guard(d);Fault fault(d,fail_draw);hr=pass.run(in,&out);
                     require(SunCopyGuard::cross_format==0,"no G32R32F/A32B32G32R32F to R32F StretchRect");
                     if(!fail_draw)require(Fault::draws==(enhanced_input?2u:1u),"enhanced depth adds exactly one draw");}
                    check("sun end",d->EndScene());before.equals(d,"sun copy restores hostile state");
                    if(fail_draw){require(FAILED(hr)&&!out.depth&&!out.color&&!pass.diagnostics().history_valid,"failed history copy refuses atomic publication");++failures;}
                    else check("sun run",hr);
                    return out;
                };
                for(unsigned frame=0;frame<2;++frame){auto normal=run(ordinary,inputs(false),false),improved=run(lane,inputs(true),true);
                    const auto depth_bytes=sun_read(d,improved.depth);D3DSURFACE_DESC desc{};check("sun history desc",improved.depth->GetLevelDesc(0,&desc));
                    require(desc.Format==D3DFMT_R32F&&depth_bytes.size()==expected.size()*4&&!std::memcmp(depth_bytes.data(),expected.data(),depth_bytes.size()),"R32F history equals every source .r bit");
                    require(depth_bytes==sun_read(d,normal.depth)&&sun_read(d,improved.color)==sun_read(d,normal.color),"ordinary and enhanced color/depth history exact twins");
                    require(improved.used_history==normal.used_history,"ordinary and enhanced history validity twins");++histories;
                }
                run(lane,inputs(true),true,1);run(lane,inputs(true),true);++resizes;
                {TemporalPass no_copy;check("sun missing copy init",no_copy.initialize(d,nullptr,resolver));Output out{};
                 f.hostile();Snapshot before(d);require(FAILED(no_copy.run(inputs(true),&out))&&!out.depth&&!out.color,"enhanced input requires copy program");before.equals(d,"missing copy leaves state untouched");}
            }
        }
        ordinary.before_reset();lane.before_reset();
        check("sun reset",d->Reset(&pp));ordinary.after_reset(S_OK);lane.after_reset(S_OK);
        require(!ordinary.diagnostics().history_valid&&!lane.diagnostics().history_valid,"Reset drops both histories");
    }
    std::printf("SUN_LANE_PASS histories=%u copy_failures=%u sizes=%u resets=2 native_runtime_claim=0\n",histories,failures,resizes);
}
