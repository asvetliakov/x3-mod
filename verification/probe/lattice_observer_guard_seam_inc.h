// Actual capture/MotionOutput integration. Only fixture selection/checkpoints are
// substituted; the queried Capture::effective and native resource Release are real.
void fixture_guard_sample(Device& ctx,IDirect3DDevice9* d,unsigned index){
    auto& f=fixture_observer;if(!fixture_observer_active(d))return;
    f.sampling=true;
    for(unsigned i=0;i<3;++i){
        IDirect3DSurface9* surface=nullptr;
        auto& sample=f.result.samples[index];sample.hr[i]=ctx.get<lattice_state::GetTarget>(38)(d,i,&surface);
        sample.pointer[i]=std::uint32_t(reinterpret_cast<std::uintptr_t>(surface));
        // Keep each first getter's owning reference through native submission.
        // Later MRT samples release only aliases while this external ref exists.
        if(index==0)f.held[i]=surface;else if(surface)surface->Release();
    }
    f.sampling=false;
}
void fixture_guard_query_action(Device& ctx,IDirect3DDevice9* d){
    auto& r=fixture_observer.result;r.frame_before=ctx.frame;r.reset_before=ctx.reset_generation;
    if(r.action==1)r.nested_result=present(d,nullptr,nullptr,nullptr,nullptr);
    else if(r.action==2){D3DPRESENT_PARAMETERS p{};r.nested_result=reset_common(d,&p,nullptr,false);}
    else if(r.action==3)r.nested_result=HRESULT(release_device(d));
    r.frame_after=ctx.frame;r.reset_after=ctx.reset_generation;
}
HRESULT WINAPI fixture_guard_native(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT n,UINT s,UINT c){
    auto& f=fixture_observer;
    fixture_guard_cpu(f.result.incoming);
    CpuCallBoundary cpu;
    ++f.result.native_calls;
    f.result.args[0]=t;f.result.args[1]=unsigned(b);f.result.args[2]=m;f.result.args[3]=n;f.result.args[4]=s;f.result.args[5]=c;
    const auto owner=devices.at(d);f.result.native_query_depth=owner->lattice_query_depth;
    fixture_guard_sample(*owner,d,2);
    cpu.before_original();const HRESULT result=f.native(d,t,b,m,n,s,c);
    fixture_guard_cpu(f.result.outgoing);cpu.after_original();
    f.result.native_result=result;return result;
}
void fixture_guard_finish_samples(){
    auto& f=fixture_observer;f.sampling=true;
    for(auto*& surface:f.held)if(surface){surface->Release();surface=nullptr;}
    f.sampling=false;
}

HRESULT WINAPI fixture_guard_declaration(IDirect3DDevice9* d,IDirect3DVertexDeclaration9** out){
    auto& f=fixture_observer;
    if(fixture_observer_active(d)&&f.queries&&f.result.action==4){
        if(out)*out=nullptr;
        ++f.result.getter_failures;return E_FAIL;
    }
    using Get=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DVertexDeclaration9**);
    return reinterpret_cast<Get>(f.declaration_entry)(d,out);
}
