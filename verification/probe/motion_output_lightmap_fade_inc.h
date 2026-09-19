// Included in the actual D3D Fixture. Light-map far fade live proof
// (--light-map-far-fade, X3M_LIGHT_MAP_FAR_FADE=P0,P1[,G];
// docs/architecture/taa-distant-line-fade.md section 11): the original hull
// pair vs_494fe349b8bc12ec / ps_7c83ed50c9894e44 drawn opaque on object B under
// the seam DLL's HDR scene with X3M_HULL_LIGHTMAP_GAIN=4, an exact FP16 light
// map in slot 2 and the fixture camera (P[0] = 0.8). The rows are diag(w) with
// w a power of two, so the raster is the w = 1 one bit for bit while the
// origin's view depth, and with it the footprint 2 w / (0.8 * 64), changes:
//   step 0 near (w 1)   1 far (w 128)   2 mid (w 64)
//   step 3 near, F4 off (the base: gain 1 through the fill/motion variant)
//   step 4 far, F4 off (still the base)   5 mid, F4 on again
//   Reset, then 6 mid and 7 near.
// With the option on the uploaded c217.w is the law's gain (near: the
// configured gain exactly; far: G exactly; mid: the linear value) and the
// image is base + (gain - 1) x L, L = (near - base) / (configured - 1) per
// pixel. With the option unset every F4-on step is the near image bit for bit
// and c217.w stays 0. The runner compares the near hash across the two runs.
void run_lightmap_fade(const char* bootstrap_vertex) {
    require(seam&&enabled&&hdr&&hdr_readback&&!taa&&camera,"light-map far fade live needs the HDR seam, its readback, the camera and TAA off");
    require(hull_toggle!=nullptr&&emission_status!=nullptr,"hull toggle and status exports");
    char text[96]{};
    const double configured=GetEnvironmentVariableA("X3M_HULL_LIGHTMAP_GAIN",text,sizeof text)>0?std::atof(text):1.;
    require(configured==4.,"the live proof uses light-map gain 4");
    double p0=0,p1=0,floor_gain=1;bool fade=false;
    if(GetEnvironmentVariableA("X3M_LIGHT_MAP_FAR_FADE",text,sizeof text)>0){
        const int parsed=std::sscanf(text,"%lf,%lf,%lf",&p0,&p1,&floor_gain);
        require(parsed>=2&&p0>0&&p1>p0,"fade setting");fade=true;
    }
    const std::string bootstrap_path(bootstrap_vertex);const auto slash=bootstrap_path.find_last_of("/\\");
    const auto folder=slash==std::string::npos?std::string{}:bootstrap_path.substr(0,slash+1);
    const auto v=load((folder+"vs_494fe349b8bc12ec.bin").c_str()),p=load((folder+"ps_7c83ed50c9894e44.bin").c_str());
    require(fnv(v.data(),v.size()*4)==0x494fe349b8bc12ecull&&fnv(p.data(),p.size()*4)==0x7c83ed50c9894e44ull,"hull original pair");
    Com<IDirect3DVertexShader9> hull_vs;Com<IDirect3DPixelShader9> hull_ps;
    api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&hull_vs.p),"hull VS");
    api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&hull_ps.p),"hull PS");
    Com<IDirect3DTexture9> art,black,light;
    const float texels[3][4]={{.5f,.25f,.75f,.75f},{0,0,0,.25f},{.25f,.5f,.125f,.25f}};
    for(unsigned which=0;which<3;++which){
        auto& t=which==0?art:which==1?black:light;
        api(d->CreateTexture(1,1,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&t.p,nullptr),"light-map fade texture");
        D3DLOCKED_RECT locked{};api(t->LockRect(0,&locked,nullptr,0),"light-map fade texture lock");
        for(unsigned c=0;c<4;++c)static_cast<unsigned short*>(locked.pBits)[c]=float_to_half(texels[which][c]);
        api(t->UnlockRect(0),"light-map fade texture unlock");
    }
    struct Step{const char* kind;float w;bool toggle,reset;};
    const Step steps[]={{"near",1,false,false},{"far",128,false,false},{"mid",64,false,false},{"near_off",1,true,false},
                        {"far_off",128,false,false},{"mid_on",64,true,false},{"mid_reset",64,false,true},{"near_reset",1,false,false}};
    std::vector<float> images[8];bool on=true;
    for(unsigned step=0;step<8;++step){
        const Step& s=steps[step];
        if(s.reset)reset();
        if(s.toggle){
            const int state=hull_toggle(d.p,1);on=!on;
            std::printf("LIGHTMAP_FADE_TOGGLE frame=%llu state=%d\n",frame,state);
            require(state==(on?1:0),"F4 action: off, then on");
        }
        frame_begin();
        scope(&b);
        material_state();
        const float coefficients[4][4]={{0,0,0,0},{8,0,0,0},{0,0,0,0},{1,0,0,0}};
        api(d->SetPixelShaderConstantF(8,coefficients[0],4),"hull coefficients");
        api(d->SetStreamSource(0,b.vb,0,24),"hull stream");
        api(d->SetTexture(0,art.p),"diffuse art");api(d->SetTexture(1,black.p),"black mask");api(d->SetTexture(2,light.p),"light map");
        api(d->SetVertexShader(hull_vs.p),"hull VS");api(d->SetPixelShader(hull_ps.p),"hull PS");
        // diag(w): clip = w x (x, y, z, 1); a power of two keeps the divide exact.
        float m[16];std::memcpy(m,identity,sizeof m);m[0]=m[5]=m[10]=m[15]=s.w;
        api(d->SetVertexShaderConstantF(24,m,4),"scaled rows");
        const Snapshot before=snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1),"hull draw");++draw_index;
        compare(before,snapshot(),"hull draw");
        const unsigned gained_draws=emission_status(d.p,78);
        float abi[8]{};api(last_pixel_abi(d.p,abi,8),"last pixel ABI");
        rows(0,0,0);
        api(d->EndScene(),"EndScene");
        write_presented(color_image());
        unsigned w=0,h=0;images[step]=hdr_image(&w,&h);
        require(w==W&&h==H,"the FP16 target matches the main dimensions");
        std::vector<unsigned short> codes(images[step].size());
        for(std::size_t i=0;i<codes.size();++i)codes[i]=float_to_half(images[step][i]);
        // The law, evaluated independently in double.
        const double footprint=2.*double(s.w)/(double(.8f)*double(W));
        const double t=fade?std::min(1.,std::max(0.,(footprint-p0)/(p1-p0))):0.;
        const double expected=on?configured+(floor_gain-configured)*t:1.;
        const double uploaded=fade?configured+(floor_gain-configured)*t:0.;
        std::printf("LIGHTMAP_FADE frame=%llu step=%u kind=%s w=%g footprint=%.9g fade=%u on=%u gained_draws=%u abi_gain=%.9g expected_upload=%.9g expected_gain=%.9g hdr_hash=%016llx\n",
                    frame,step,s.kind,double(s.w),footprint,unsigned(fade),unsigned(on),gained_draws,double(abi[7]),uploaded,expected,
                    static_cast<unsigned long long>(fnv(codes.data(),codes.size()*sizeof codes[0])));
        require(gained_draws==(on?1u:0u),"the gained variant binds exactly while the F4 flag is on");
        require(std::fabs(double(abi[7])-uploaded)<=1e-5*std::max(1.,uploaded),"c217.w carries the law's gain (0 with the option off)");
        if(fade&&(t==0.||t==1.))require(double(abi[7])==(t==0.?configured:floor_gain),"both ends of the fade are exact");
        api(d->Present(nullptr,nullptr,nullptr,nullptr),"Present");
        ++frame;++frames_since_reset;
    }
    // Images: base = step 3; L = (near - base) / (configured - 1).
    const auto& near_image=images[0];const auto& base=images[3];
    for(unsigned step=0;step<8;++step){
        const Step& s=steps[step];
        const bool step_on=!(step==3||step==4);
        const double footprint=2.*double(s.w)/(double(.8f)*double(W));
        const double t=fade?std::min(1.,std::max(0.,(footprint-p0)/(p1-p0))):0.;
        const double gain=step_on?configured+(floor_gain-configured)*t:1.;
        unsigned pixels=0,mismatches=0,max_codes=0,lit=0;
        for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){
            double bx,by,bw;object_point(x,y,0,0,bx,by,bw);
            if(edge_distance(b,bx,by)<1e-2||!b.covers(bx,by))continue;
            ++pixels;
            const std::size_t at=(std::size_t(y)*W+x)*4;
            for(unsigned k=0;k<4;++k){
                const double term=k<3?(double(near_image[at+k])-double(base[at+k]))/(configured-1.):0.;
                if(k<3&&term>0)++lit;
                const double value=double(base[at+k])+(gain-1.)*term;
                const unsigned codes=unsigned(std::abs(int(float_to_half(images[step][at+k]))-int(float_to_half(float(value)))));
                max_codes=std::max(max_codes,codes);
                // Exact where the law is an end (gain 1 or the configured gain:
                // the operands are the reference images' own); two codes for the
                // interpolated gain (two FP16 roundings in the reference term).
                const unsigned allowed=gain==1.||gain==configured?0u:2u;
                if(codes>allowed&&++mismatches<=8)
                    std::printf("LIGHTMAP_FADE_DIFF step=%u x=%u y=%u lane=%u actual=%.9g expected=%.9g base=%.9g near=%.9g\n",step,x,y,k,
                                double(images[step][at+k]),value,double(base[at+k]),double(near_image[at+k]));
            }
        }
        std::printf("LIGHTMAP_FADE_IMAGE step=%u kind=%s gain=%.9g pixels=%u lit=%u max_codes=%u mismatches=%u\n",step,s.kind,gain,pixels,lit,max_codes,mismatches);
        require(pixels>=100&&lit==3*pixels,"B is covered and the light-map term is positive on every colour lane");
        require(!mismatches,"image = base + (gain - 1) x light-map term");
    }
    api(d->SetTexture(0,nullptr),"unbind art");api(d->SetTexture(1,nullptr),"unbind mask");api(d->SetTexture(2,nullptr),"unbind light map");
}
