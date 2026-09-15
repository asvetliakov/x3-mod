// Detached generated-material producer; no live receiver/state/coverage claim.
void sun_share_material_fixture(IDirect3DDevice9* d,Shaders& shaders,const std::vector<Case>& cases){
    Gpu gpu(d,shaders,16);
    gpu.current.p->Release();gpu.current.p=nullptr;
    api(d->CreateRenderTarget(16,16,D3DFMT_G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&gpu.current.p,nullptr));
    // A no-draw pixel must retain the runtime's (-1,0) depth/share sentinel.
    // Clear(D3DCLEAR_TARGET,0) cannot express negative floating-point depth.
    // Use the production quad ABI and a constant-output ps_3_0, then rebind
    // the fixture's material state before submitting the actual geometry.
    constexpr DWORD clear_words[]={0xffff0300u,0x05000051u,0xa00f0000u,0,0,0,0xbf800000u,
        0x02000001u,0x800f0800u,0xa0000000u,
        0x02000001u,0x800f0801u,0xa0000000u,
        0x02000001u,0x800f0802u,0xa0030000u,0x0000ffffu};
    Com<IDirect3DVertexShader9> clear_vs;Com<IDirect3DPixelShader9> clear_ps;
    Com<IDirect3DVertexDeclaration9> clear_decl;
    api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(quad_vertex_program()),&clear_vs.p));
    api(d->CreatePixelShader(clear_words,&clear_ps.p));
    api(d->CreateVertexDeclaration(quad_declaration,&clear_decl.p));
    auto clear=[&](const Case& input,unsigned mode){
        gpu.state(input,mode);
        api(d->SetVertexShader(clear_vs.p));api(d->SetPixelShader(clear_ps.p));api(d->SetVertexDeclaration(clear_decl.p));
        QuadVertex vertices[4];quad_vertices(gpu.width,gpu.width,vertices);
        api(d->BeginScene());api(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof vertices[0]));api(d->EndScene());
    };
    require(!cases.empty(),"sun cases present");
    clear(cases.front(),4);
    const auto empty=gpu.read(gpu.current.p,D3DFMT_G32R32F);
    unsigned clear_drawn=0;
    for(const auto& pixel:empty){
        require(pixel.f[0]==-1&&pixel.f[1]==0,"undrawn pixel retains (-1,0), not false zero sun");
        clear_drawn+=std::isfinite(pixel.f[0])&&pixel.f[0]>=0&&pixel.f[0]<=1;
    }
    require(clear_drawn==0,"no-draw negative control has no eligible pixels");
    std::printf("SUN_MATERIAL_CLEAR pixels=%zu drawn=%u valid=0 positive=0 zero=0\n",empty.size(),clear_drawn);
    unsigned total_drawn=0,total_valid=0,total_positive=0,total_zero=0,total_invalid=0,run=0;double max_error=0;
    for(const auto& c:cases){
        require(c.depth==1&&c.fp16==1,"sun cases require depth and FP16 owner");
        auto draw=[&](const Case& input,unsigned mode){clear(input,mode);gpu.state(input,mode);gpu.draw(input);};
        draw(c,2);const auto before=gpu.read(gpu.color[1].p,D3DFMT_A16B16G16R16F),motion=gpu.read(gpu.motion.p,D3DFMT_A32B32G32R32F),depth=gpu.read(gpu.current.p,D3DFMT_G32R32F);
        draw(c,4);const auto after=gpu.read(gpu.color[1].p,D3DFMT_A16B16G16R16F),motion_after=gpu.read(gpu.motion.p,D3DFMT_A32B32G32R32F),lane=gpu.read(gpu.current.p,D3DFMT_G32R32F);
        require(before.size()==after.size()&&!std::memcmp(before.data(),after.data(),before.size()*sizeof(Pixel)),"sun lane preserves color and alpha exactly");
        require(!std::memcmp(motion.data(),motion_after.data(),motion.size()*sizeof(Pixel)),"sun lane preserves motion exactly");
        Case no_sun=c;std::fill(no_sun.f+22,no_sun.f+25,0.f);draw(no_sun,2);
        const auto dark=gpu.read(gpu.color[1].p,D3DFMT_A16B16G16R16F);
        unsigned drawn=0,valid=0,positive=0,zero=0,invalid=0;double error=0;
        for(unsigned i=0;i<lane.size();++i){
            require(!std::memcmp(&depth[i].f[0],&lane[i].f[0],4),"sun lane preserves depth bits");
            const float f=lane[i].f[1],z=lane[i].f[0];
            if(z==-1)continue;
            require(std::isfinite(z)&&z>=0&&z<=1,"drawn depth is valid or untouched sentinel");
            ++drawn;
            if(f==-1){++invalid;continue;}
            require(std::isfinite(f)&&f>=0&&f<=1,"sun share domain or explicit invalid sentinel");
            ++valid;positive+=f>0;zero+=f==0;
            // Independent GPU color executions, with only authored D0 RGB zeroed.
            // Fill is zero in this focused mode. FP16 encoding/decoding rounds.
            double l=0,s=0;const double weights[]={.2126,.7152,.0722};
            for(unsigned k=0;k<3;++k){const double full=std::pow(double(before[i].f[k]),2.2),without=std::pow(double(dark[i].f[k]),2.2);l+=weights[k]*full;s+=weights[k]*(full-without);}
            if(l>=std::ldexp(1.,-20))error=std::max(error,std::abs(double(f)-s/l));
        }
        require(error<=.01,"sun share agrees with independent GPU D0 subtraction within FP16 tolerance");
        require(drawn>0&&valid>0,"case has valid drawn material samples");
        if(c.f[22]==0&&c.f[23]==0&&c.f[24]==0)
            require(positive==0&&zero==valid,"zero-sun case proves zero only on drawn pixels");
        total_drawn+=drawn;total_valid+=valid;total_positive+=positive;total_zero+=zero;total_invalid+=invalid;max_error=std::max(max_error,error);++run;
        std::printf("SUN_MATERIAL id=%u pair=%u drawn=%u valid=%u positive=%u zero=%u invalid=%u max_error=%.9g\n",c.id,c.pair,drawn,valid,positive,zero,invalid,error);
    }
    require(total_valid>0&&total_positive>0,"sun producer writes positive valid pixels");
    std::printf("SUN_MATERIAL_PASS cases=%u drawn=%u valid=%u positive=%u zero=%u invalid=%u max_error=%.9g\n",run,total_drawn,total_valid,total_positive,total_zero,total_invalid,max_error);
}
