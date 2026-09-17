// Included in the actual D3D Fixture. Hull-emitter gain live proof (emitter
// plan phase 3): the original standard_lighting pair
// vs_494fe349b8bc12ec / ps_7c83ed50c9894e44 (one of the twelve covered
// programs) drawn on object B under the production DLL's HDR scene with the
// emitter art in the diffuse slot and the lightmap slot black, exactly as the
// ONE/ONE emitter materials bind their slots. Three frames over an opaque
// black underlay on B: opaque (blend off: refused, the base), ADD ONE/ONE
// (admitted: G x base within one FP16 code, alpha native) and screen
// ONE/INVSRCCOLOR (refused: base), then the same ONE/ONE draw after the
// Ctrl+Shift+F4 action switched the population off (base) and on again
// (G x base). The hull draw runs under object B's scope, so a capture frame
// names B in its hull_emission_draw line. Frame 0's blend-off draw is the one
// the motion route takes: the hull gain counts it opaque (never routed), and
// the runner compares its colour hash with the gain-1 run (the routed pair's
// output is the option-off output). With X3M_HULL_EMISSION_GAIN unset (1) the
// same script must leave every frame at the base (no variant, no admission,
// the toggle a refused no-op).
void run_hull_emission(const char* bootstrap_vertex) {
    require(seam&&enabled&&hdr&&hdr_readback&&!taa,"hull emission live needs the HDR seam, its readback and TAA off");
    char gain_text[32]{};
    const float gain=GetEnvironmentVariableA("X3M_HULL_EMISSION_GAIN",gain_text,sizeof gain_text)>0?std::strtof(gain_text,nullptr):1.f;
    require(gain==1.f||gain==2.f||gain==4.f,"the live proof uses gain 1 (off) or a power of two (exact FP16 scaling)");
    const std::string bootstrap_path(bootstrap_vertex);const auto slash=bootstrap_path.find_last_of("/\\");
    const auto folder=slash==std::string::npos?std::string{}:bootstrap_path.substr(0,slash+1);
    const auto v=load((folder+"vs_494fe349b8bc12ec.bin").c_str()),p=load((folder+"ps_7c83ed50c9894e44.bin").c_str());
    require(fnv(v.data(),v.size()*4)==0x494fe349b8bc12ecull&&fnv(p.data(),p.size()*4)==0x7c83ed50c9894e44ull,"hull emitter original pair");
    Com<IDirect3DVertexShader9> hull_vs;Com<IDirect3DPixelShader9> hull_ps;
    api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&hull_vs.p),"hull emitter VS");
    api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&hull_ps.p),"hull emitter PS");
    // Diffuse art (exact FP16 values), black specular mask, black lightmap.
    Com<IDirect3DTexture9> art,black;
    const float art_texel[]={.5f,.25f,.75f,.75f},black_texel[]={0,0,0,.25f};
    for(unsigned which=0;which<2;++which){
        auto& t=which?black:art;
        api(d->CreateTexture(1,1,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&t.p,nullptr),"hull emitter texture");
        D3DLOCKED_RECT locked{};api(t->LockRect(0,&locked,nullptr,0),"hull emitter texture lock");
        for(unsigned c=0;c<4;++c)static_cast<unsigned short*>(locked.pBits)[c]=float_to_half((which?black_texel:art_texel)[c]);
        api(t->UnlockRect(0),"hull emitter texture unlock");
    }
    const char* kinds[]={"opaque","additive","screen","additive_off","additive_on"};
    require(hull_toggle!=nullptr,"hull toggle export");
    std::vector<float> base;unsigned base_pixels=0;bool toggled_on=true;
    for(unsigned step=0;step<5;++step){
        if(step>=3){
            // The F4 action without the key, between frames like the sampler.
            const int state=hull_toggle(d.p);toggled_on=gain!=1.f?!toggled_on:toggled_on;
            std::printf("HULL_EMISSION_TOGGLE frame=%llu state=%d\n",frame,state);
            require(state==(gain==1.f?-1:toggled_on?1:0),"toggle state: off, then on; refused without the option");
        }
        const bool additive=step==1||step>=3;
        frame_begin();
        // Opaque black underlay on B (the constant program, c0 = 0), so the
        // blended draws add to zero and the FP16 sums are exact.
        scope(nullptr);
        const float zero[4]{};
        api(d->SetPixelShaderConstantF(0,zero,1),"underlay c0");
        api(d->SetStreamSource(0,b.vb,0,24),"underlay stream");
        api(d->SetVertexShader(vs.p),"underlay VS");api(d->SetPixelShader(hdrconst.p),"underlay PS");
        rows(0,0,0);
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1),"underlay draw");++draw_index;
        std::printf("EXPECT frame=%llu index=%u object=B routed=0 matched=0 jittered=0\n",frame,draw_index);
        // The hull pair: material_state's lights and constants, the
        // application-class coefficients (c8..c11: specular, power,
        // reflection, diffuse) and the emitter slots.
        scope(&b);
        material_state();
        const float coefficients[4][4]={{0,0,0,0},{8,0,0,0},{0,0,0,0},{1,0,0,0}};
        api(d->SetPixelShaderConstantF(8,coefficients[0],4),"hull coefficients");
        api(d->SetTexture(0,art.p),"diffuse art");api(d->SetTexture(1,black.p),"black mask");api(d->SetTexture(2,black.p),"black lightmap");
        api(d->SetVertexShader(hull_vs.p),"hull VS");api(d->SetPixelShader(hull_ps.p),"hull PS");
        if(step){
            api(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"blend on");api(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE),"src one");
            api(d->SetRenderState(D3DRS_DESTBLEND,additive?D3DBLEND_ONE:D3DBLEND_INVSRCCOLOR),"dest");api(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"blend add");
        }
        const Snapshot before=snapshot();
        api(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,1),"hull emitter draw");++draw_index;
        compare(before,snapshot(),"hull emitter draw");
        std::printf("EXPECT frame=%llu index=%u object=B routed=0 matched=0 jittered=0\n",frame,draw_index);
        if(step){api(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"blend off");api(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO),"dest restore");}
        api(d->EndScene(),"EndScene");
        const auto image=color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n",frame,static_cast<unsigned long long>(color_hash(image)));
        write_presented(image);
        unsigned w=0,h=0;const auto fp16=hdr_image(&w,&h);
        require(w==W&&h==H,"the FP16 target matches the main dimensions");
        // Inside B (edge band excluded): base on frame 0, the laws after.
        const float factor=additive&&toggled_on?gain:1.f;
        unsigned pixels=0,mismatches=0,alpha_mismatches=0,positive=0,max_codes=0,samples=0;
        if(step==0)base.assign(fp16.begin(),fp16.end());
        for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){
            double bx,by,bw;object_point(x,y,0,0,bx,by,bw);
            if(edge_distance(b,bx,by)<1e-2||!b.covers(bx,by))continue;
            const float* value=&fp16[(std::size_t(y)*W+x)*4];const float* reference=&base[(std::size_t(y)*W+x)*4];
            ++pixels;bool ok=true;
            for(unsigned k=0;k<3;++k){
                positive+=value[k]>0;
                const int actual=float_to_half(value[k]),expected=float_to_half(factor*reference[k]);
                const unsigned codes=unsigned(std::abs(actual-expected));max_codes=std::max(max_codes,codes);
                ok=ok&&codes<=1;
            }
            const bool alpha_ok=float_to_half(value[3])==float_to_half(reference[3]);
            alpha_mismatches+=!alpha_ok;
            if((!ok||!alpha_ok)&&++mismatches<=8)
                std::printf("HULL_EMISSION_DIFF frame=%llu x=%u y=%u actual=%.6g,%.6g,%.6g,%.6g base=%.6g,%.6g,%.6g,%.6g factor=%g\n",frame,x,y,value[0],value[1],value[2],value[3],reference[0],reference[1],reference[2],reference[3],double(factor));
            else if(samples<3&&(x+y)%7==0){++samples;std::printf("HULL_EMISSION_SAMPLE frame=%llu kind=%s x=%u y=%u actual=%.6g,%.6g,%.6g,%.6g base=%.6g,%.6g,%.6g,%.6g\n",frame,kinds[step],x,y,value[0],value[1],value[2],value[3],reference[0],reference[1],reference[2],reference[3]);}
        }
        if(step==0)base_pixels=pixels;
        std::printf("HULL_EMISSION frame=%llu kind=%s gain=%g factor=%g pixels=%u positive=%u max_codes=%u mismatches=%u alpha_mismatches=%u\n",
                    frame,kinds[step],double(gain),double(factor),pixels,positive,max_codes,mismatches,alpha_mismatches);
        require(pixels==base_pixels&&pixels>=100,"B covers the same pixel set on every frame");
        require(positive==3*pixels,"the emitter draw writes a positive colour on every lane of B");
        require(!mismatches&&!alpha_mismatches,factor!=1.f?"ONE/ONE draw: gain x base within one FP16 code, alpha native":"refused or toggled-off draw: base within one FP16 code, alpha native");
        api(d->Present(nullptr,nullptr,nullptr,nullptr),"Present");
        ++frame;++frames_since_reset;
    }
    api(d->SetTexture(0,nullptr),"unbind art");api(d->SetTexture(1,nullptr),"unbind mask");api(d->SetTexture(2,nullptr),"unbind lightmap");
}
