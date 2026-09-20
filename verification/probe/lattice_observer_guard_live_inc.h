// Included in motion_output_fixture.cpp after Fixture. Uses the actual proxy,
// reviewed routed shader pair, native DIP and production motion/depth readbacks.
void guard_cpu(LatticeGuardCpu& image){
    image.error=GetLastError();asm volatile("fnsave %0\n frstor %0\n stmxcsr %1":"=m"(image.x87),"=m"(image.mxcsr)::"memory");
}
void guard_seed(){const WORD cw=0x077f;const DWORD mx=0x3f80;
    asm volatile("fninit\n fld1\n fldpi\n fldcw %0\n ldmxcsr %1"::"m"(cw),"m"(mx):"memory");SetLastError(0x13572468);}
bool guard_same_cpu(const LatticeGuardCpu& a,const LatticeGuardCpu& b){return a.mxcsr==b.mxcsr&&a.error==b.error&&!std::memcmp(a.x87,b.x87,108);}
void guard_declaration(Fixture& f){
    if(f.declaration.p)return;
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_TEXCOORD,0},{0,16,D3DDECLTYPE_FLOAT16_4,0,D3DDECLUSAGE_NORMAL,0},D3DDECL_END()};
    api(f.d->CreateVertexDeclaration(elements,&f.declaration.p),"guard declaration");
}
void guard_word_diff(const char* channel,const std::vector<float>& baseline,const std::vector<float>& current){
    unsigned words=0,numerical=0,printed=0;
    for(unsigned i=0;i<baseline.size();++i){
        DWORD a=0,b=0;std::memcpy(&a,&baseline[i],4);std::memcpy(&b,&current[i],4);
        if(baseline[i]!=current[i])++numerical;
        if(a!=b){++words;if(printed++<8)std::printf("LATTICE_GUARD_DIFF channel=%s index=%u baseline=%08lx current=%08lx\n",channel,i,a,b);}
    }
    std::printf("LATTICE_GUARD_DIFF_TOTAL channel=%s words=%u numerical=%u total=%u\n",channel,words,numerical,unsigned(baseline.size()));
}
void run_lattice_observer_guard(Fixture& f){
    require(f.seam&&f.enabled&&!f.taa&&!f.jitter&&!f.hdr,"guard fixture mode prerequisites");
    const auto arm=symbol<LatticeGuardArm>(f.runtime,"x3m_lattice_observer_fixture_arm",true);
    const auto read=symbol<LatticeGuardRead>(f.runtime,"x3m_lattice_observer_fixture_read",true);
    const auto disarm=symbol<HRESULT(*)(IDirect3DDevice9*)>(f.runtime,"x3m_lattice_observer_fixture_disarm",true);
    Com<IDirect3DIndexBuffer9> indices;api(f.d->CreateIndexBuffer(6,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr),"guard indices");
    void* destination=nullptr;api(indices->Lock(0,0,&destination,0),"guard index lock");const WORD values[]={0,1,2};std::memcpy(destination,values,6);api(indices->Unlock(),"guard index unlock");
    auto prepare=[&]{guard_declaration(f);f.frame_begin();f.scope(&f.a);api(f.d->SetIndices(indices.p),"guard bind indices");
        api(f.d->SetVertexShader(f.vs.p),"guard VS");api(f.d->SetPixelShader(f.ps.p),"guard PS");f.rows(0,0,0);};
    auto finish=[&]{api(disarm(f.d.p),"guard disarm");api(f.d->EndScene(),"guard EndScene");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"guard Present");++f.frame;};
    auto draw=[&](unsigned mode,unsigned action,unsigned dropped){
        if(action==5)api(f.d->SetIndices(nullptr),"guard native-failure unbind indices");
        api(arm(f.d.p,mode,action),"guard arm");if(dropped)f.declaration.reset();
        guard_seed();LatticeGuardCpu incoming{},outgoing{};guard_cpu(incoming);
        const auto topology=D3DPT_TRIANGLELIST;
        const HRESULT hr=f.d->DrawIndexedPrimitive(topology,0,0,3,0,1);guard_cpu(outgoing);
        LatticeGuardResult result;api(read(f.d.p,&result),"guard result");
        require(hr==result.native_result&&result.native_calls==1,"guard unchanged native dispatch/result");
        const unsigned args[]={unsigned(topology),0,0,3,0,1};require(!std::memcmp(args,result.args,sizeof args),"guard unchanged DIP arguments");
        require(guard_same_cpu(incoming,result.incoming)&&guard_same_cpu(outgoing,result.outgoing),"guard native input/output CPU/LastError");
        require(result.native_query_depth==0,"guard scope ends before native DIP");
        require(result.sample_releases==0,"guard MRT sampling adds no Release callbacks");
        require(result.pin_acquires==(mode==2?1u:0u)&&result.pin_releases==result.pin_acquires,"guard balanced draw-lifetime pin");
        if(dropped)api(f.d->GetVertexDeclaration(&f.declaration.p),"guard reacquire declaration");
        return result;
    };
    for(unsigned cycle=0;cycle<2;++cycle){
        if(cycle){api(f.d->SetIndices(nullptr),"guard unbind before Reset");f.reset();}
        // Commit identical scope history before comparing complete auxiliary words.
        for(unsigned warm=0;warm<2;++warm){prepare();draw(0,0,0);finish();}
        std::vector<float> baseline_motion,baseline_depth,recovery_motion,recovery_depth;
        for(unsigned mode=0;mode<3;++mode)for(unsigned dropped=0;dropped<2;++dropped){
            // The prior arm's recovery draw duplicates its rigid history key.
            // Commit one unique predecessor before each measurement so all
            // arms compare valid previous rows, including observer-off controls.
            prepare();draw(0,0,0);finish();
            prepare();const auto result=draw(mode,0,dropped);
            const bool broken=mode==1&&dropped&&f.lazy;
            for(unsigned i=0;i<3;++i)require(result.samples[0].hr[i]==S_OK&&result.samples[0].pointer[i],"guard after-route MRTs bound");
            for(unsigned sample=1;sample<3;++sample)for(unsigned i=0;i<3;++i){
                if(broken&&i)require(result.samples[sample].hr[i]==D3DERR_NOTFOUND&&!result.samples[sample].pointer[i],"unguarded actual Release clears lazy MRT");
                else require(result.samples[sample].hr[i]==S_OK&&result.samples[sample].pointer[i]==result.samples[0].pointer[i],"guard native MRT parity");
            }
            require(!mode||!dropped||result.query_releases>0,"actual dropped declaration invokes device Release");
            require(mode!=2||result.query_restores==0,"guard observer callbacks skip restoration");
            if(mode==1&&dropped)require(result.query_restores>0,"unguarded callback uses actual restoration branch");
            std::vector<float> motion(Fixture::W*Fixture::H*4),depth(Fixture::W*Fixture::H);unsigned w=0,h=0;
            api(f.readback(f.d.p,motion.data(),unsigned(motion.size()),&w,&h),"guard actual motion readback");
            api(f.readback_depth(f.d.p,depth.data(),unsigned(depth.size()),&w,&h),"guard actual depth readback");
            const auto centre=(Fixture::H/2)*Fixture::W+Fixture::W/2;
            require(std::fabs(depth[centre]-(broken?-1.f:.5f))<1e-6f,"guard actual auxiliary depth write versus lost write");
            if(mode==0&&!dropped){baseline_motion=motion;baseline_depth=depth;}
            if(!broken){
                if(motion!=baseline_motion||depth!=baseline_depth){guard_word_diff("motion",baseline_motion,motion);guard_word_diff("depth",baseline_depth,depth);}
                require(motion==baseline_motion&&depth==baseline_depth,"guard complete auxiliary parity with observer off");
            }
            else require(depth!=baseline_depth,"unguarded backend callback produces auxiliary mismatch");
            // The next ordinary draw must recover, including after the broken witness.
            api(arm(f.d.p,0,0),"guard subsequent observer-off arm");
            api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1),"guard subsequent native draw");
            api(f.readback_depth(f.d.p,depth.data(),unsigned(depth.size()),&w,&h),"guard subsequent depth readback");
            api(f.readback(f.d.p,motion.data(),unsigned(motion.size()),&w,&h),"guard subsequent motion readback");
            if(mode==0&&!dropped){recovery_motion=motion;recovery_depth=depth;}
            require(depth==recovery_depth&&motion==recovery_motion&&depth==baseline_depth,
                    "guard subsequent ordinary draw matches both baseline auxiliary channels");
            std::printf("LATTICE_GUARD cycle=%u mode=%u dropped=%u lazy=%u query_releases=%u query_restores=%u sample_releases=%u native_calls=%u depth=%.9g broken=%u\n",cycle,mode,dropped,f.lazy,result.query_releases,result.query_restores,result.sample_releases,result.native_calls,double(broken?-1.f:.5f),broken);
            finish();
        }
        for(unsigned action:{1u,2u,4u,5u}){
            prepare();const auto result=draw(2,action,1);
            if(action<=2)require(result.nested_result==D3DERR_INVALIDCALL&&result.frame_before==result.frame_after&&result.reset_before==result.reset_after,"nested query Present/Reset refused before mutation");
            if(action==4)require(result.getter_failures==1&&SUCCEEDED(result.native_result),"failed getter keeps native dispatch and pin balanced");
            if(action==5){
                std::printf("LATTICE_GUARD_NATIVE_FAILURE result=%08lx indices_bound=0\n",result.native_result);
                require(FAILED(result.native_result),"actual backend rejects indexed draw without indices");
            }
            require(!result.query_restores,"nested cancellation preserves routed MRTs");
            std::printf("LATTICE_GUARD_NESTED cycle=%u action=%u result=%08lx frame=%llu reset=%llu\n",cycle,action,result.nested_result,result.frame_after,result.reset_after);finish();
        }
    }
    api(f.d->SetIndices(nullptr),"guard final indices unbind");
    std::puts("LATTICE_GUARD_MATRIX PASS cases=12 nested=4 getter_failures=2 native_failures=2 resets=1");
    indices.reset();
    // Separate devices test the last application Release from inside a query,
    // both without and with the renderer's owned references. No COM call uses
    // the dead device; the private readback export reads only fixture CPU storage.
    for(unsigned routed=0;routed<2;++routed){
        Fixture last;last.runtime=f.runtime;last.window=f.window;last.pp=f.pp;
        last.enabled=true;last.seam=true;last.lazy=f.lazy;last.configure=f.configure;
        last.vs_words=f.vs_words;last.ps_words=f.ps_words;last.vs_hash=f.vs_hash;last.ps_hash=f.ps_hash;last.flat_hash=f.flat_hash;
        api(f.factory->CreateDevice(0,D3DDEVTYPE_HAL,f.window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&last.pp,&last.d.p),"guard final-release device");
        Com<IDirect3DIndexBuffer9> last_indices;
        if(routed){
            last.create(false);last.frame_begin();last.scope(&last.a);
            api(last.d->CreateIndexBuffer(6,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&last_indices.p,nullptr),"guard final indices");
            void* pointer=nullptr;api(last_indices->Lock(0,0,&pointer,0),"guard final indices lock");std::memcpy(pointer,values,6);api(last_indices->Unlock(),"guard final indices unlock");
            api(last.d->SetIndices(last_indices.p),"guard final indices bind");api(last.d->SetVertexShader(last.vs.p),"guard final VS");api(last.d->SetPixelShader(last.ps.p),"guard final PS");last.rows(0,0,0);
            // Only binding/internal renderer references remain; every application
            // creation reference is dropped once, never dereferenced again.
            last.back.reset();last.depth.reset();last.bloom_surface.reset();last.bloom.reset();
            last.vs.reset();last.ps.reset();last.flat.reset();last.hdr2.reset();last.hdr8.reset();last.hdrmid.reset();last.hdrconst.reset();
            last.declaration.reset();last.vb_a.reset();last.vb_b.reset();last.cube.reset();last.ramp.reset();
            for(auto& texture:last.textures)texture.reset();
            last_indices.reset();
        }
        IDirect3DDevice9* const identity=last.d.p;
        api(arm(identity,2,3),"guard final-release arm");guard_seed();LatticeGuardCpu incoming{},outgoing{};guard_cpu(incoming);
        const HRESULT hr=last.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1);guard_cpu(outgoing);last.d.p=nullptr;
        LatticeGuardResult result;api(read(identity,&result),"guard retired CPU result");
        require(hr==result.native_result&&result.native_calls==1&&result.native_query_depth==0,"guard last-app-release still dispatches once");
        require(guard_same_cpu(incoming,result.incoming)&&guard_same_cpu(outgoing,result.outgoing),"guard final cleanup preserves native output CPU/LastError");
        require(result.pin_acquires==1&&result.pin_releases==1&&result.retired==1,"guard final pin retires device exactly once");
        require(!result.query_restores,"guard last-app-release never restores inside query");
        api(disarm(identity),"guard retired seam disarm");
        std::printf("LATTICE_GUARD_FINAL routed=%u retired=%u pins=%u result=%08lx\n",routed,result.retired,result.pin_releases,result.native_result);
    }
}
