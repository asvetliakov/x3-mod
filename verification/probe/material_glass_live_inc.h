// Included inside Fixture. Glass-specific live qualification reuses the existing
// native evaluate_draw seam, snapshots, image readbacks and motion CPU oracle.
// Original shader programs remain external inputs. This is verification only.
void run_glass_materials(const char* original_path) {
    require(seam && enabled && hdr && hdr_agx && hdr_readback && wrap_snapshot && emission_status && !taa,
            "glass live needs HDR/readback/WRAP seam and TAA off");
    char setting[32]{};
    const bool material=GetEnvironmentVariableA("X3M_LINEAR_MATERIALS",setting,sizeof setting)==1 && setting[0]=='1';
    const std::string supplied(original_path);
    const auto slash=supplied.find_last_of("/\\");
    require(slash!=std::string::npos,"glass program directory");
    const auto directory=supplied.substr(0,slash+1);
    const char* glass_vs[]={"c30104cb0efb6675","e2ad860d5fbb3e59","74fdc00d802b4027"};
    const char* glass_ps[]={"a66fb1981ba755b2","ebc9b2b3f1564e9a","f31c9e2701c8eee4","9d49f288800f898d"};
    const unsigned pair_v[]={0,0,1,1,2,2},pair_p[]={0,1,2,3,2,3};
    Com<IDirect3DVertexShader9> vertex[3];
    Com<IDirect3DPixelShader9> pixel[4];
    for(unsigned i=0;i<3;++i) {
        const auto code=load((directory+"vs_"+glass_vs[i]+".bin").c_str());
        require(fnv(code.data(),code.size()*4)==std::strtoull(glass_vs[i],nullptr,16),"glass VS identity");
        api(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&vertex[i].p),"create glass VS");
    }
    for(unsigned i=0;i<4;++i) {
        const auto code=load((directory+"ps_"+glass_ps[i]+".bin").c_str());
        require(fnv(code.data(),code.size()*4)==std::strtoull(glass_ps[i],nullptr,16),"glass PS identity");
        api(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&pixel[i].p),"create glass PS");
    }
    // Two index triples preserve the same geometry with distinct history keys.
    Com<IDirect3DIndexBuffer9> indices;
    api(d->CreateIndexBuffer(12,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr),"glass indices");
    void* target=nullptr;
    api(indices->Lock(0,0,&target,0),"glass index lock");
    const unsigned short triangle[]={0,1,2,0,1,2};
    std::memcpy(target,triangle,sizeof triangle);
    api(indices->Unlock(),"glass index unlock");
    Com<IDirect3DTexture9> maps[2];
    const DWORD texels[]={0x80ffffffu,0x4080e020u}; // diffuse alpha128; mask red128, unrelated G/B/A
    for(unsigned i=0;i<2;++i) {
        api(d->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&maps[i].p,nullptr),"glass texture");
        D3DLOCKED_RECT lock{};
        api(maps[i]->LockRect(0,&lock,nullptr,0),"glass texture lock");
        for(unsigned y=0;y<2;++y) for(unsigned x=0;x<2;++x)
            std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+4*x,&texels[i],4);
        api(maps[i]->UnlockRect(0),"glass texture unlock");
    }
    // Endpoint cube RGB makes the paired s2-sRGB refusal numerically inert.
    const DWORD faces[]={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffff00u,0xff00ffffu,0xffff00ffu};
    for(unsigned face=0;face<6;++face) {
        D3DLOCKED_RECT lock{};
        api(cube->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0),"glass cube lock");
        for(unsigned y=0;y<2;++y) for(unsigned x=0;x<2;++x)
            std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+4*x,&faces[face],4);
        api(cube->UnlockRect(D3DCUBEMAP_FACES(face),0),"glass cube unlock");
    }
    const auto wrap_state=[](unsigned i){return D3DRENDERSTATETYPE(i<8?D3DRS_WRAP0+i:D3DRS_WRAP8+i-8);};
    std::uint64_t sequence=0;
    Object object=a;object.name="GLASS";
    // 0..11: each pair unseen/matched. 12..21: sampler0/1/2, RGB mask and
    // blend refusal each followed by restored admission. 22: stateblock,
    // Reset; 23/24: unseen/matched after Reset. 25/26: perspective twins.
    for(unsigned plan=0;plan<27;++plan) {
        const bool transport=plan>=25,post_reset=plan==23;
        const unsigned pair=plan<12?plan/2:plan==26?4:0;
        const unsigned step=plan<12?plan%2:plan-12;
        const bool fixed=pair_v[pair]==2,gate=plan==18||plan==20;
        const bool sampler_refused=plan==12||plan==14||plan==16;
        const unsigned refusal_stage=sampler_refused?(plan-12)/2:2;
        const bool combined=material&&!gate&&!sampler_refused;
        const float perspective=transport?.125f:0.f;
        if((plan<12&&plan%2==0)||plan==12||transport) {
            object.scope.node_serial=9000+plan;
            object.scope.node=0xa00000+unsigned(object.scope.node_serial)*0x100;
            object.scope.mesh=0xb00000+unsigned(object.scope.node_serial)*0x100;
            object.recorded=false;
        }
        if(post_reset)object.recorded=false;
        const bool matched=object.recorded&&!gate;
        frame_begin();write_reserved();
        float vc[48][4]{};
        const unsigned matrix=fixed?0:24,world=fixed?7:28,normal=fixed?10:31,camera=fixed?13:34;
        for(unsigned i=0;i<4;++i)vc[matrix+i][i]=1;
        vc[matrix+3][0]=perspective;
        for(unsigned base:{world,normal,camera})for(unsigned i=0;i<3;++i)vc[base+i][i]=1;
        vc[camera][3]=-1;vc[camera+1][3]=1;vc[camera+2][3]=1.5f;
        vc[fixed?16:37][0]=1;vc[fixed?17:38][1]=1;
        vc[fixed?18:39][0]=.625f;
        const unsigned emissive=fixed?19:40;
        vc[emissive][0]=.125f;vc[emissive][1]=.25f;vc[emissive][2]=.0625f;
        vc[fixed?20:41][0]=1;
        const unsigned point=fixed?4:0;
        vc[point][2]=4;
        for(unsigned i=0;i<3;++i){vc[point+1][i]=1;vc[point+2][i]=16;}
        api(d->SetVertexShaderConstantF(0,vc[0],48),"glass native VS constants");
        const int lights[4]={1,0,1,0};api(d->SetVertexShaderConstantI(0,lights,1),"glass point count");
        const BOOL fog=FALSE;api(d->SetVertexShaderConstantB(0,&fog,1),"glass no fog");
        const float pc[4][4]={{0,0,1,0},{1,0,0,0},{0,0,-1,0},{0,1,0,0}};
        api(d->SetPixelShaderConstantF(0,pc[0],4),"glass directional constants");
        for(unsigned i=0;i<7;++i) {
            IDirect3DBaseTexture9* texture=i<2?static_cast<IDirect3DBaseTexture9*>(maps[i].p):i==2?cube.p:nullptr;
            api(d->SetTexture(i,texture),"glass sampler roles");
            for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(d->SetSamplerState(i,filter,D3DTEXF_POINT),"glass point sampling");
            api(d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"glass no mip");
            api(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"glass clamp u");
            api(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"glass clamp v");
            if(!post_reset)api(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE),"glass decode false");
        }
        if(post_reset)for(unsigned i=0;i<3;++i){DWORD value=1;api(d->GetSamplerState(i,D3DSAMP_SRGBTEXTURE,&value),"glass Reset sampler default");require(value==FALSE,"glass Reset clears sampler refusal");}
        if(sampler_refused)api(d->SetSamplerState(refusal_stage,D3DSAMP_SRGBTEXTURE,TRUE),"glass sampler refusal");
        DWORD caller[16]{};
        if(!post_reset) {
            caller[0]=13;caller[1]=11;caller[2]=6;caller[4]=15;caller[5]=7;
            for(unsigned i=0;i<16;++i)api(d->SetRenderState(wrap_state(i),caller[i]),"glass caller WRAP");
        } else for(unsigned i=0;i<16;++i){DWORD value=1;api(d->GetRenderState(wrap_state(i),&value),"glass Reset WRAP default");require(value==0,"glass Reset clears WRAP shadow");}
        if(plan==18)api(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"glass native RGB-only gate");
        // ONE/ZERO isolates ALPHABLENDENABLE admission from blend arithmetic.
        api(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE),"glass blend source");
        api(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO),"glass blend destination");
        api(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"glass blend operation");
        if(plan==20)api(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"glass actual blend gate");
        if(plan==22) {
            api(d->BeginStateBlock(),"glass begin recorded");
            api(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE),"glass record decode");
            Com<IDirect3DStateBlock9> recorded;
            api(d->EndStateBlock(&recorded.p),"glass end recorded");
            DWORD value=1;api(d->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&value),"glass recorded unapplied");
            require(value==FALSE,"glass recorded sampler has no immediate effect");
            Com<IDirect3DStateBlock9> all;api(d->CreateStateBlock(D3DSBT_ALL,&all.p),"glass capture stateblock");
            api(d->SetSamplerState(2,D3DSAMP_SRGBTEXTURE,TRUE),"glass transient decode");
            api(d->SetRenderState(D3DRS_WRAP4,0),"glass transient WRAP");
            api(all->Apply(),"glass Apply restore");
        }
        float vr[8][4],pr[11][4];
        for(unsigned i=0;i<32;++i)vr[i/4][i%4]=100.f+i;
        for(unsigned i=0;i<44;++i)pr[i/4][i%4]=200.f+i;
        api(d->SetVertexShaderConstantF(244,vr[0],8),"glass poison VS reserved");
        api(d->SetPixelShaderConstantF(210,pr[0],11),"glass poison PS reserved");write_reserved();
        api(d->SetIndices(indices.p),"glass bind indices");
        api(d->SetVertexDeclaration(declaration.p),"glass bind declaration");
        scope(&object);
        api(d->SetStreamSource(0,object.vb,0,24),"glass bind stream");
        api(d->SetVertexShader(vertex[pair_v[pair]].p),"glass original VS bind");
        api(d->SetPixelShader(pixel[pair_p[pair]].p),"glass original PS bind");
        const Snapshot before=snapshot();
        float before_vs[48][4],before_ps[11][4];
        api(d->GetVertexShaderConstantF(0,before_vs[0],48),"glass native constants snapshot");
        api(d->GetPixelShaderConstantF(210,before_ps[0],11),"glass reserved snapshot");
        std::array<std::uint64_t,3> transport_baseline{};
        std::vector<float> transport_rgb;
        for(unsigned draw_number=0;draw_number<2;++draw_number) {
            if(transport)api(d->SetSamplerState(2,D3DSAMP_SRGBTEXTURE,draw_number==0),"glass paired ordinary then linear admission");
            api(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,3*draw_number,1),"glass original source draw");
            ++draw_index;
            records.push_back({&object,0,perspective,0,!gate,matched,object.rt,object.rp,object.rzo,false,jitter,!gate});
            x3m::MotionOutputFixtureWrapSnapshot observed{};
            api(wrap_snapshot(d.p,&observed),"glass native WRAP snapshot");++sequence;
            DWORD expected[16];std::memcpy(expected,caller,sizeof expected);
            if(!gate){expected[4]=0;if(materialwrap_depth)expected[5]=0;}
            std::printf("GLASS_WRAP plan=%u frame=%llu draw=%u sequence=%llu valid=%u result=%08lx values=",plan,frame,draw_number,static_cast<unsigned long long>(observed.sequence),observed.valid,observed.result);
            for(unsigned i=0;i<16;++i)std::printf("%s%lu",i?",":"",static_cast<unsigned long>(observed.values[i]));
            std::puts("");
            require(observed.valid&&observed.result==S_OK&&observed.sequence==sequence&&!std::memcmp(expected,observed.values,sizeof expected),"glass native-once and WRAP transport");
            require(emission_status(d.p,12)==draw_number+1,"glass original source submitted once");
            if(transport) {
                unsigned w=0,h=0;const auto image=hdr_image(&w,&h);
                require(w==W&&h==H,"glass paired image dimensions");
                std::vector<float> alpha_image(std::size_t(W)*H),motion_image(std::size_t(W)*H*4),depth_image(std::size_t(W)*H);
                for(std::size_t i=0;i<alpha_image.size();++i)alpha_image[i]=image[4*i+3];
                for(float value:image)require_quiet(std::isfinite(value),"glass paired finite image");
                api(readback(d.p,motion_image.data(),unsigned(motion_image.size()),&w,&h),"glass paired motion readback");
                if(materialwrap_depth)api(readback_depth(d.p,depth_image.data(),unsigned(depth_image.size()),&w,&h),"glass paired depth readback");
                const std::array<std::uint64_t,3> hashes={fnv(alpha_image.data(),alpha_image.size()*4),fnv(motion_image.data(),motion_image.size()*4),materialwrap_depth?fnv(depth_image.data(),depth_image.size()*4):0};
                unsigned pixels=0;double max_error=0,low=1e30,high=-1e30;
                if(!draw_number){transport_baseline=hashes;transport_rgb=image;}
                else {
                    require(hashes==transport_baseline,"glass perspective ordinary/linear alpha and temporal twins");
                    for(std::size_t i=0;i<alpha_image.size();++i) {
                        if(std::fabs(transport_rgb[4*i+3]-.625*128./255.)>.001)continue;
                        ++pixels;
                        for(unsigned lane=0;lane<3;++lane) {
                            const double ordinary=transport_rgb[4*i+lane];
                            require_quiet(ordinary>=0,"glass positive working RGB");
                            low=std::min(low,ordinary);high=std::max(high,ordinary);
                            const double wanted=material?std::pow(ordinary,1./2.2):ordinary;
                            max_error=std::max(max_error,std::fabs(image[4*i+lane]-wanted)/(.006*std::fabs(wanted)+.00002));
                        }
                    }
                    require(pixels>W*H/4&&high-low>.001&&max_error<=1,"glass calibrated colored radiance relation");
                }
                std::printf("GLASS_TRANSPORT plan=%u frame=%llu draw=%u combined=%u alpha=%016llx motion=%016llx depth=%016llx perspective=0.125 pixels=%u max_error=%.9g rgb_range=%.9g\n",plan,frame,draw_number,material&&draw_number,static_cast<unsigned long long>(hashes[0]),static_cast<unsigned long long>(hashes[1]),static_cast<unsigned long long>(hashes[2]),pixels,max_error,draw_number?high-low:0);
                for(unsigned y:{H/4,H/2,3*H/4})for(unsigned x:{W/4,W/2,3*W/4}) {
                    const auto* p=&image[(std::size_t(y)*W+x)*4];
                    std::printf("GLASS_SAMPLE plan=%u frame=%llu draw=%u x=%u y=%u rgba=%.9g,%.9g,%.9g,%.9g\n",plan,frame,draw_number,x,y,p[0],p[1],p[2],p[3]);
                }
            }
        }
        compare(before,snapshot(),"glass state after consecutive source draws");
        float after_vs[48][4],after_vr[8][4],after_ps[11][4];
        api(d->GetVertexShaderConstantF(0,after_vs[0],48),"glass native constants readback");
        api(d->GetVertexShaderConstantF(244,after_vr[0],8),"glass VS reserved readback");
        api(d->GetPixelShaderConstantF(210,after_ps[0],11),"glass PS reserved readback");
        require(!std::memcmp(before_vs,after_vs,sizeof before_vs)&&!std::memcmp(vr,after_vr,sizeof vr)&&!std::memcmp(before_ps,after_ps,sizeof before_ps),"glass native and reserved constants restored");
        object.recorded=!gate;object.rt=object.rzo=0;object.rp=perspective;
        unsigned width=0,height=0;const auto image=hdr_image(&width,&height);
        require(width==W&&height==H,"glass FP16 dimensions");
        const float* center=&image[(std::size_t(H/2)*W+W/2)*4];
        require(std::fabs(center[3]-(plan==18?1.:.625*128./255.))<.001,"glass native target alpha");
        for(float value:image)require_quiet(std::isfinite(value),"glass finite full FP16 image");
        std::vector<float> alpha_pixels(std::size_t(W)*H);
        for(std::size_t i=0;i<alpha_pixels.size();++i)alpha_pixels[i]=image[i*4+3];
        std::printf("GLASS_LIVE plan=%u frame=%llu pair=%u step=%u vs=%s ps=%s combined=%u refusal=%u matched=%u rgba=%.9g,%.9g,%.9g,%.9g alpha_hash=%016llx image_hash=%016llx\n",plan,frame,pair,step,glass_vs[pair_v[pair]],glass_ps[pair_p[pair]],combined,gate?5u:sampler_refused?4u:0u,matched,double(center[0]),double(center[1]),double(center[2]),double(center[3]),static_cast<unsigned long long>(fnv(alpha_pixels.data(),alpha_pixels.size()*4)),static_cast<unsigned long long>(fnv(image.data(),image.size()*4)));
        frame_end();
        if(plan==22){reset();object.recorded=false;}
    }
}
