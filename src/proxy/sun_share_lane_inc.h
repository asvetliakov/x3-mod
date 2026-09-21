// Included inside namespace x3m by motion_output.cpp. Documented D3D9 only.
// Independent enhancement qualification; the ordinary R32F tests stay intact.
void MotionOutput::qualify_sun_lane() noexcept {
    sun_lane_qualified_=false;sun_lane_depth_count_=0;
    if(!sun_lane_requested_)return;
    const char* reason="prerequisite"; char detail[192]="";
    IDirect3D9* factory=nullptr; IDirect3DSurface9* attachment=nullptr;
    D3DDEVICE_CREATION_PARAMETERS creation{}; D3DDISPLAYMODE display{}; D3DSURFACE_DESC depth{};
    D3DFORMAT candidates[]={D3DFMT_D16,D3DFMT_D24X8,D3DFMT_D24S8};unsigned count=3;
    HRESULT hr=D3DERR_NOTAVAILABLE;
    constexpr DWORD required=D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS|D3DPMISCCAPS_INDEPENDENTWRITEMASKS|D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING;
    do {
        if(!enabled_||!depth_enabled_||!taa_enabled_||!hdr_enabled_)break; // no linear-material prerequisite: original shading has its own share producer (legacy-sun-application.md 4.1)
        reason="caps";
        if(caps_.NumSimultaneousRTs<3||(caps_.PrimitiveMiscCaps&required)!=required||!(caps_.AlphaCmpCaps&D3DPCMPCAPS_GREATEREQUAL))break;
        reason="metadata";
        if(FAILED(hr=native<GetDirect3DFn>(GetDirect3D)(device_,&factory))||!factory||
           FAILED(hr=native<GetCreationFn>(GetCreationParameters)(device_,&creation))||
           FAILED(hr=native<GetDisplayModeFn>(GetDisplayMode)(device_,0,&display)))break;
        hr=native<GetDepthFn>(GetDepthStencilSurface)(device_,&attachment);
        if(hr==D3DERR_NOTFOUND&&!attachment)hr=S_OK;
        else if(SUCCEEDED(hr)&&attachment){hr=attachment->GetDesc(&depth);candidates[0]=depth.Format;count=1;}
        else {if(SUCCEEDED(hr))hr=E_FAIL;break;}
        if(FAILED(hr))break;
        reason="formats";
        // The lane's MRT triple: RT0 A16B16G16R16F, RT1 and RT2 A32B32G32R32F (the RT2 of
        // shadow-receiver-depth.md; the former G32R32F RT2 is gone): render target with
        // post-pixel-shader blending, sampled, and matched against the depth-stencil below.
        for(const auto format:{D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F}){
            hr=factory->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,display.Format,
                D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_TEXTURE,format);
            if(FAILED(hr))break;
            hr=factory->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,display.Format,0,D3DRTYPE_TEXTURE,format);
            if(FAILED(hr))break;
        }
        if(FAILED(hr))break;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        {char fault[16]{};GetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",fault,sizeof fault);
         if(!std::strcmp(fault,"caps")){reason="fixture_caps";hr=D3DERR_NOTAVAILABLE;break;}}
#endif
        reason="sentinel";
        if(!sun_sentinel_ps_){
            DWORD code[std::size(sentinel_mrt_program)];std::copy(std::begin(sentinel_mrt_program),std::end(sentinel_mrt_program),code);
            // oC1 = c0.wxww -> (-1,0,-1,-1): .r sentinel, .g zero share, and on the
            // A32B32G32R32F RT2 (shadow-receiver-depth.md) the .b/.a sentinel too.
            code[std::size(code)-2]=0xa0f30000u;
            if(FAILED(hr=native<CreatePsFn>(CreatePixelShader)(device_,code,&sun_sentinel_ps_))||!sun_sentinel_ps_)break;
        }
        reason="shader_cache";
        for(const auto& item:pixel_){const auto& e=item.second;
            if(e.row&&renderer::material_motion_pixel_writes_depth(*e.row,depth_enabled_)&&
               ((e.variant&&!e.sun_motion_variant)||(e.material_variant&&!e.sun_material_variant)||(e.xt_default_ordinary_variant&&!e.sun_xt_variant)))sun_lane_failed_=true;
        }
        if(sun_lane_failed_)break;
        reason="self_test";
        for(unsigned i=0;i<count&&!motion_state_lost_;++i){
            const auto candidate=candidates[i];
            hr=factory->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,display.Format,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,candidate);
            for(const auto format:{D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F}){
                if(FAILED(hr))break;
                hr=factory->CheckDepthStencilMatch(creation.AdapterOrdinal,creation.DeviceType,display.Format,format,candidate);
            }
            bool tested=false;
            if(SUCCEEDED(hr)){
                const bool was_busy=taa_busy_;taa_busy_=true;
                tested=sun_lane_self_test(candidate,detail,sizeof detail);taa_busy_=was_busy;
            }
            if(tested)sun_lane_depth_formats_[sun_lane_depth_count_++]=candidate;
            log("sun_shadow_lane_depth device=%llu format=%u qualified=%u query=%08lx detail=%s",id_,unsigned(candidate),tested,hr,detail[0]?detail:"-");
        }
        sun_lane_qualified_=sun_lane_depth_count_&&!motion_state_lost_;
        if(sun_lane_qualified_){reason="ok";hr=S_OK;}
    }while(false);
    release(attachment);release(factory);
    log("sun_shadow_lane_device device=%llu requested=1 qualified=%u reason=%s result=%08lx depth_formats=%u shadows=0",
        id_,sun_lane_qualified_,reason,hr,sun_lane_depth_count_);
}

bool MotionOutput::sun_lane_self_test(D3DFORMAT depth_format,char* reason,std::size_t reason_size) noexcept {
    using CreateDsFn=HRESULT(WINAPI*)(D,UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*);
    using CreateBlockFn=HRESULT(WINAPI*)(D,D3DSTATEBLOCKTYPE,IDirect3DStateBlock9**);
    using SetTextureFn=HRESULT(WINAPI*)(D,DWORD,IDirect3DBaseTexture9*);
    using ClearFn=HRESULT(WINAPI*)(D,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD);
    using SetFrequencyFn=HRESULT(WINAPI*)(D,UINT,UINT);
    IDirect3DTexture9* textures[5]{};IDirect3DSurface9* targets[5]{},*copies[5]{},*depth=nullptr;
    IDirect3DPixelShader9 *writer=nullptr,*alternate=nullptr,*passing=nullptr,*invalid=nullptr,*copy=nullptr;
    IDirect3DStateBlock9* block=nullptr;
    SavedState saved;bool captured=false,ok=false;HRESULT hr=S_OK,restore=S_OK;
    const char* stage="create";unsigned checks=0;
    // The production MRT triple (64 + 128 + 128 bits: RT0, RT1, the wide RT2), the wide
    // sampled-copy target and the R32F lane-off / history target. The self test's depth
    // comparisons read .rg (8 bytes) of each 16-byte RT2 texel: the writer's .b/.a are
    // its own c2.zw, not part of the lane contract.
    const D3DFORMAT formats[]={D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F,D3DFMT_A32B32G32R32F,D3DFMT_A32B32G32R32F,D3DFMT_R32F};
    auto step=[&](HRESULT value){if(SUCCEEDED(hr)&&FAILED(value))hr=value;return SUCCEEDED(hr);};
    auto state=[&](D3DRENDERSTATETYPE key,DWORD value){return step(native<SetRenderStateFn>(SetRenderState)(device_,key,value));};
    auto draw=[&](IDirect3DPixelShader9* shader,float z){
        if(FAILED(hr))return false;
        if(!step(native<SetPsFn>(SetPixelShader)(device_,shader))||!step(native<SceneFn>(BeginScene)(device_)))return false;
        renderer::QuadVertex quad[4];renderer::quad_vertices(4,4,quad);for(auto& v:quad)v.z=z;
        step(native<DrawUpFn>(DrawPrimitiveUP)(device_,D3DPT_TRIANGLESTRIP,2,quad,sizeof quad[0]));
        step(native<SceneFn>(EndScene)(device_));return SUCCEEDED(hr);
    };
    auto read=[&](unsigned index,const void* expected,unsigned bytes){
        if(!step(native<GetRtDataFn>(GetRenderTargetData)(device_,targets[index],copies[index])))return false;
        D3DLOCKED_RECT lock{};if(!step(copies[index]->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return false;
        const unsigned stride=formats[index]==D3DFMT_A32B32G32R32F?16u:formats[index]==D3DFMT_A16B16G16R16F?8u:4u; // texel stride; `bytes` is the compared prefix
        bool equal=true;for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
            equal=equal&&!std::memcmp(static_cast<const char*>(lock.pBits)+y*lock.Pitch+x*stride,expected,bytes);
        step(copies[index]->UnlockRect());++checks;if(!equal)hr=E_FAIL;return SUCCEEDED(hr);
    };
    const std::uint16_t initial_color[]={0x3400,0x3800,0x3a00,0x3c00},other_color[]={0x3a00,0x3400,0x3800,0};
    const float initial_motion[]={1,2,3,-1},other_motion[]={4,5,6,-1},initial_depth[]={.625f,.375f},invalid_depth[]={.25f,-1};
    const std::uint16_t passed_color[]={0x3800,0x3a00,0x3400,0x3c00};
    const float passed_motion[]={7,8,9,-1},passed_depth[]={.125f,.75f};
    auto passed=[&](){return read(0,passed_color,8)&&read(1,passed_motion,16)&&read(2,passed_depth,8);};
    auto initial=[&](){return read(0,initial_color,8)&&read(1,initial_motion,16)&&read(2,initial_depth,8);};
    do {
        for(unsigned i=0;i<5&&SUCCEEDED(hr);++i){
            step(native<CreateTextureFn>(CreateTexture)(device_,4,4,1,D3DUSAGE_RENDERTARGET,formats[i],D3DPOOL_DEFAULT,&textures[i],nullptr));
            if(SUCCEEDED(hr)&&textures[i])step(textures[i]->GetSurfaceLevel(0,&targets[i]));else hr=E_FAIL;
            if(SUCCEEDED(hr))step(native<CreateOffscreenFn>(CreateOffscreenPlainSurface)(device_,4,4,formats[i],D3DPOOL_SYSTEMMEM,&copies[i],nullptr));
        }
        if(FAILED(hr)||!step(native<CreateDsFn>(29)(device_,4,4,depth_format,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr)))break;
        if(!step(native<CreatePsFn>(CreatePixelShader)(device_,self_test_depth_program,&writer)))break;
        DWORD alternate_code[std::size(self_test_depth_program)];std::copy(std::begin(self_test_depth_program),std::end(self_test_depth_program),alternate_code);
        // Independent constants: color (.75,.25,.5,0), motion (4,5,6,-1), depth (.25,.875,...).
        alternate_code[3]=0x3f400000;alternate_code[4]=0x3e800000;alternate_code[5]=0x3f000000;alternate_code[6]=0;
        alternate_code[9]=0x40800000;alternate_code[10]=0x40a00000;alternate_code[11]=0x40c00000;
        alternate_code[15]=0x3e800000;alternate_code[16]=0x3f600000;
        if(!step(native<CreatePsFn>(CreatePixelShader)(device_,alternate_code,&alternate)))break;
        DWORD passing_code[std::size(alternate_code)];std::copy(std::begin(alternate_code),std::end(alternate_code),passing_code);
        // A different passing alpha (.5) MUST change all RGB/MRT values, while
        // RGB-only RT0 writes MUST preserve the prior destination alpha (1).
        passing_code[3]=0x3f000000;passing_code[4]=0x3f400000;passing_code[5]=0x3e800000;passing_code[6]=0x3f000000;
        passing_code[9]=0x40e00000;passing_code[10]=0x41000000;passing_code[11]=0x41100000;
        passing_code[15]=0x3e000000;passing_code[16]=0x3f400000;
        if(!step(native<CreatePsFn>(CreatePixelShader)(device_,passing_code,&passing)))break;
        std::vector<std::uint32_t> invalid_code;
        try{invalid_code.assign(std::begin(alternate_code),std::end(alternate_code));}catch(...){hr=E_OUTOFMEMORY;break;}
        if(!renderer::material_motion_invalid_sun_share(invalid_code)){hr=E_FAIL;break;}
        if(!step(native<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(invalid_code.data()),&invalid))||
           !step(native<CreatePsFn>(CreatePixelShader)(device_,reinterpret_cast<const DWORD*>(renderer::hdr_writeback_program()),&copy)))break;
        stage="capture";
        if(!step(save_state(saved))||!step(native<CreateBlockFn>(59)(device_,D3DSBT_ALL,&block))||!block||!step(block->Capture()))break;
        captured=true;stage="normalize";
        step(native<SetDepthFn>(SetDepthStencilSurface)(device_,nullptr));
        for(unsigned i=0;i<saved.target_count;++i)step(native<SetRenderTargetFn>(SetRenderTarget)(device_,i,i<3?targets[i]:nullptr));
        step(native<SetDepthFn>(SetDepthStencilSurface)(device_,depth));
        const D3DVIEWPORT9 viewport{0,0,4,4,0,1};step(native<SetViewportFn>(SetViewport)(device_,&viewport));
        step(native<SetDeclarationFn>(SetVertexDeclaration)(device_,quad_declaration_));step(native<SetVsFn>(SetVertexShader)(device_,quad_vs_));
        for(unsigned i=0;i<caps_.MaxStreams;++i)step(native<SetFrequencyFn>(102)(device_,i,1));
        for(unsigned i=0;i<touched_count;++i)state(touched_states[i],touched_values[i]);
        state(D3DRS_DITHERENABLE,FALSE);state(D3DRS_ZENABLE,TRUE);state(D3DRS_ZWRITEENABLE,TRUE);state(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        state(D3DRS_ALPHATESTENABLE,FALSE);state(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL);state(D3DRS_ALPHAREF,1);
        step(native<ClearFn>(43)(device_,0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
        stage="opaque";if(!draw(writer,.5f)||!initial())break;
        state(D3DRS_ALPHATESTENABLE,TRUE);state(D3DRS_COLORWRITEENABLE,7);
        stage="cutout_pass";
        bool drop_passing=false;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        {char fault[24]{};GetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",fault,sizeof fault);
         drop_passing=!std::strcmp(fault,"cutout_drop");
         if(!std::strcmp(fault,"alpha_mask"))state(D3DRS_COLORWRITEENABLE,15);}
#endif
        if((!drop_passing&&!draw(passing,.25f))||!passed())break;
        stage="cutout_reject";if(!draw(alternate,0)||!passed())break;
        state(D3DRS_ALPHATESTENABLE,FALSE);
        stage="depth_reject";if(!draw(alternate,.375f)||!passed())break;
        state(D3DRS_ZENABLE,FALSE);state(D3DRS_ZWRITEENABLE,FALSE);state(D3DRS_COLORWRITEENABLE,15);state(D3DRS_COLORWRITEENABLE2,0);
        stage="independent_mask";if(!draw(alternate,0)||!read(0,other_color,8)||!read(1,other_motion,16)||!read(2,passed_depth,8))break;
        state(D3DRS_COLORWRITEENABLE2,3);
        stage="invalid_share";if(!draw(invalid,0)||!read(2,invalid_depth,8))break;
        // Actual sampling of BOTH lanes and the exact .r -> ordinary R32F copy.
        step(native<SetDepthFn>(SetDepthStencilSurface)(device_,nullptr));
        for(unsigned i=1;i<saved.target_count;++i)step(native<SetRenderTargetFn>(SetRenderTarget)(device_,i,nullptr));
        for(const auto sampler:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})step(native<SetSamplerStateFn>(SetSamplerState)(device_,0,sampler,D3DTEXF_POINT));
        step(native<SetSamplerStateFn>(SetSamplerState)(device_,0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        step(native<SetSamplerStateFn>(SetSamplerState)(device_,0,D3DSAMP_SRGBTEXTURE,FALSE));
        for(const auto sampler:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})step(native<SetSamplerStateFn>(SetSamplerState)(device_,0,sampler,D3DTADDRESS_CLAMP));
        step(native<SetTextureFn>(SetTexture)(device_,0,textures[2]));
        stage="sample_rg";step(native<SetRenderTargetFn>(SetRenderTarget)(device_,0,targets[3]));
        if(!draw(copy,0)||!read(3,invalid_depth,8))break;
        stage="history_r";step(native<SetRenderTargetFn>(SetRenderTarget)(device_,0,targets[4]));
        if(!draw(copy,0)||!read(4,invalid_depth,4))break;
#ifdef X3M_MOTION_OUTPUT_FIXTURE
        {char fault[16]{};GetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",fault,sizeof fault);
         if(!std::strcmp(fault,"selftest")){hr=E_FAIL;break;}}
#endif
        ok=true;
    }while(false);
    if(captured){
        restore=block->Apply();const auto explicit_restore=restore_state(saved);
        if(SUCCEEDED(restore))restore=explicit_restore;
        if(FAILED(restore)){motion_state_lost_=true;motion_state_error_=restore;invalidate_taa(TaaInvalidateSite::RestoreFailed);ok=false;}
    }
    release(block);release(writer);release(alternate);release(passing);release(invalid);release(copy);release(depth);
    for(unsigned i=0;i<5;++i){release(copies[i]);release(targets[i]);release(textures[i]);}
    std::snprintf(reason,reason_size,"stage=%s result=%08lx restore=%08lx checks=%u",stage,hr,restore,checks);
    return ok;
}

// Invalid-share stamp candidate: a scene draw refused at gate 3 as
// `unregistered` (a program outside the profile registry, e.g. an SM2 program
// the SM3 rewriter cannot host) after the first receiver. after_draw stamps it
// only if it is an actual depth writer. A registered row without a reviewed
// pair (`pair`) and the xt repair refusal keep the frame veto.
void MotionOutput::arm_sun_stamp(const MotionDrawCall& call, MotionRoute& route) noexcept {
    if (route.scene && route.gate == MotionGate::Pair && route.unmatched == UnmatchedReason::Unregistered &&
        !call.user_memory && sun_frame_.receivers) {
        sun_stamp_call_ = call; route.sun_stamp = true;
    }
}
// Invalid-share stamp of one unroutable depth writer (called from after_draw,
// after the application's own draw succeeded, with the application's bindings on
// the device: an unrouted draw had its lazy RT1/RT2 flushed in before_draw).
// The draw is issued once more with the application's VS, streams, constants
// and every raster state, and only these changes: a constant PS of the
// original's shader-model family, RT2 bound with COLORWRITEENABLE2 = GREEN,
// RT0/RT1 masked off, no depth write, ZFUNC EQUAL, alpha test, blending, fog and
// sRGB write off. EQUAL against the depth the same program just wrote selects
// exactly the pixels the draw owns (alpha-test/texkill holes and occluded
// fragments hold another depth), so RT2.g becomes -1 there: the explicit
// invalid share of a plain depth writer (directional-shadows.md), and the
// apply pass excludes those pixels. RT2.r keeps the earlier depth, as it does
// for every unrouted draw with the lane off. Stencil-enabled draws are refused
// (a second issue would run the stencil operations twice). Documented D3D9
// only; the independent write masks are a lane prerequisite (qualify_sun_lane).
// Everything is read before the first write; every attempted write is put back
// in reverse order, and a failed restoration quarantines like undo().
bool MotionOutput::sun_stamp_draw(MotionRoute& route) noexcept {
    using DrawFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, UINT, UINT);
    using DrawIndexedFn = HRESULT(WINAPI*)(D, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
    // The stamp PS takes the shader-model family of the bound programs (D3D9
    // pairs SM3 with SM3 only). A null VS (fixed-function vertex processing) or
    // a null PS pairs with ps_2_0; a bound program whose version the registry
    // never saw, or a null stage beside an SM3 program, is refused.
    if ((shadow_.vs && !shadow_.vs_major) || (shadow_.ps && !shadow_.ps_major)) return false;
    const bool sm3 = shadow_.vs_major >= 3 || shadow_.ps_major >= 3;
    if (sm3 && (!shadow_.vs || !shadow_.ps)) return false;
    if (shadow_.ps && shadow_.ps_depth_out) return false; // the original replaces the rasterized depth: EQUAL would not select its pixels
    const unsigned slot = sm3 ? 1u : 0u;
    if (!depth_surface_ || motion_state_lost_ || sun_stamp_ps_failed_[slot]) return false;
    if (!sun_stamp_ps_[slot]) {
        // def c0, -1, -1, -1, -1 ; mov oC0, c0 ; mov oC1, c0 ; mov oC2, c0 (every
        // output up to the lane's index is written; RT0/RT1 are masked off).
        const DWORD program[] = {slot ? 0xffff0300u : 0xffff0200u,
            0x05000051u, 0xa00f0000u, 0xbf800000u, 0xbf800000u, 0xbf800000u, 0xbf800000u,
            0x02000001u, 0x800f0800u, 0xa0e40000u, 0x02000001u, 0x800f0801u, 0xa0e40000u,
            0x02000001u, 0x800f0802u, 0xa0e40000u, 0x0000ffffu};
        const HRESULT created = native<CreatePsFn>(CreatePixelShader)(device_, program, &sun_stamp_ps_[slot]);
        if (FAILED(created) || !sun_stamp_ps_[slot]) {
            release(sun_stamp_ps_[slot]); sun_stamp_ps_failed_[slot] = true;
            log("sun_shadow_lane_stamp_shader device=%llu frame=%llu model=%u create=%08lx", id_, frame_, slot ? 3u : 2u, created);
            return false;
        }
    }
    struct Item { D3DRENDERSTATETYPE key; DWORD value; };
    static constexpr Item items[] = {
        {D3DRS_ZWRITEENABLE, FALSE}, {D3DRS_ZFUNC, D3DCMP_EQUAL}, {D3DRS_ALPHATESTENABLE, FALSE},
        {D3DRS_ALPHABLENDENABLE, FALSE}, {D3DRS_SRGBWRITEENABLE, FALSE}, {D3DRS_FOGENABLE, FALSE},
        {D3DRS_COLORWRITEENABLE, 0}, {D3DRS_COLORWRITEENABLE1, 0}, {D3DRS_COLORWRITEENABLE2, D3DCOLORWRITEENABLE_GREEN},
        {D3DRS_COLORWRITEENABLE3, 0}}; // RT3 is never the route's; an application binding there stays untouched
    constexpr unsigned item_count = unsigned(sizeof items / sizeof items[0]);
    DWORD saved[item_count]{}, stencil = TRUE;
    if (FAILED(render_state(D3DRS_STENCILENABLE, &stencil)) || stencil) return false;
    for (unsigned i = 0; i < item_count; ++i) if (FAILED(render_state(items[i].key, &saved[i]))) return false;
    if (FAILED(acquire_restore(route))) return false; // the stamp binds a program: own the restoration bindings first, before any state write
    HRESULT hr = S_OK;
    unsigned written = 0; // bit i: items[i] was attempted (a failed setter may still have mutated)
    bool target = false, shader = false;
    bool mid_fault = false; // fixture only: the fourth attempted state write reports failure after it was issued
#ifdef X3M_MOTION_OUTPUT_FIXTURE
    {char fault[16]{};GetEnvironmentVariableA("X3M_FIXTURE_SUN_LANE_FAULT",fault,sizeof fault);
     if(!std::strcmp(fault,"stamp"))hr=E_FAIL;
     mid_fault=!std::strcmp(fault,"stamp_mid");}
#endif
    for (unsigned i = 0; i < item_count && SUCCEEDED(hr); ++i) {
        if (saved[i] == items[i].value) continue;
        written |= 1u << i;
        hr = direct_call<SetRenderStateFn>(SetRenderState, items[i].key, items[i].value);
        if (mid_fault && SUCCEEDED(hr) && i >= 3) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) { target = true; hr = bind_target(2, depth_surface_); }
    if (SUCCEEDED(hr)) { shader = true; hr = native<SetPsFn>(SetPixelShader)(device_, sun_stamp_ps_[slot]); }
    if (SUCCEEDED(hr)) {
        const auto& c = sun_stamp_call_;
        hr = c.indexed
            ? native<DrawIndexedFn>(82)(device_, c.topology, c.base_vertex, c.min_vertex, c.vertex_count, c.first, c.primitives)
            : native<DrawFn>(81)(device_, c.topology, c.first, c.primitives);
    }
    HRESULT restore = S_OK;
    auto step = [&](HRESULT value) { if (SUCCEEDED(restore) && FAILED(value)) restore = value; };
    if (shader) step(native<SetPsFn>(SetPixelShader)(device_, route.restore_ps));
    if (target) step(bind_target(2, nullptr));
    for (unsigned i = item_count; i-- > 0;)
        if (written & (1u << i)) step(direct_call<SetRenderStateFn>(SetRenderState, items[i].key, saved[i]));
    if (FAILED(restore)) {
        if (!motion_state_lost_) { motion_state_lost_ = true; motion_state_error_ = restore; }
        invalidate_taa(TaaInvalidateSite::RestoreFailed);
        ++counters_.restore_failures; invalidate_render_states();
        if (logged_failures_ < failure_log_limit) {
            ++logged_failures_;
            log("motion_output_restore_failed device=%llu frame=%llu index=%lu result=%08lx what=sun_stamp", id_, frame_, counters_.draws, restore);
        }
    }
    return SUCCEEDED(hr) && SUCCEEDED(restore);
}
