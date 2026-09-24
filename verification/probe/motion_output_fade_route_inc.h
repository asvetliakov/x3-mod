// Fade-band motion arm live witness (docs/architecture/linear-distance-fade-region.md,
// "Fade-band route"; docs/reverse-engineering/asteroid-fog-temporal.md, run 49).
// Every frame draws the routed full-screen object A, then two quads of the exact
// fade pair b0602757fce6e870/517540ae6d5e5410 in the engine's fade-band state
// (Z test on, Z-write off, SRCALPHA/INVSRCALPHA ADD, RGB mask 7): P (top left,
// constant texels, the live fade fixture's pair-0 inputs, for the composite
// oracle at two sample pixels) and Q (top right, a diffuse ramp in u and v, for
// the runner's per-frame Lucas-Kanade shift of the raw and the resolved image).
// X3M_FIXTURE_FADE_SCRIPT=routed: g_AlphaValue 1, g_FogClip (1, 0), diffuse
// alpha 1 -> fraction 1000 permille -> the arm routes both quads (own RT1 rows,
// RT2 masked, no bracket, M clear). masked: the live fixture's .625 / (.75, .125)
// with fog on -> 390 permille on the CPU (below the default 500) -> the fade
// bracket composes them as before and M covers them. sentinel: the routed
// inputs with A scissored to the bottom half (rows 32..63: the scene still
// has its depth writer, which the selector's Scene phase requires), so both
// quads sit over the route's sentinel fill (no routed opaque draw beneath)
// under X3M_TAA_SENTINEL=2 (the run-49 configuration: far-plane reprojection
// of the fill plus the quads' own RT1 rows at alpha 1). hover: the
// diffuse-alpha-.5 inputs with P's g_AlphaValue per frame (permille 507,
// 449, 449, 390, 449, 449, 507, 449, 390, 507, 449, 449): the arm's
// hysteresis keeps a node admitted at >= 500 down to 400 (held), a frame
// below 400 disarms it, and 449 stays refused until 507 arms it again. Q
// follows the same estimate through g_FogClip = (permille/1000 - 1, -1) at
// g_AlphaValue 1 and camera distance 4, where the shader's factor saturates
// at 1: its raster keeps a constant alpha (the shift witness needs a constant
// amplitude) while the CPU estimate at the origin distance 1 hovers.
// original: the hover schedule over the sentinel fill (A scissored) under
// original shading (X3M_LINEAR_MATERIALS=0, X3M_LINEAR_DISTANCE_FADE=0: no fade
// bracket, no composition, no M; a refused frame is the plain native draw),
// the run-125 production configuration in which the station panel pair was
// refused at gate 4 for the never-probed cutout verdict alone
// (asteroid-fog-temporal.md, "Run 125"): the probe's verdict must be Ready
// (status 30) and the arm routes both quads over the sentinel fill.
// behind: the original script with the quads' clip rows placing the object
// origin behind the camera plane (rows diag(2, 2, 2) with row 3 = (0, 0, 10,
// -1): every vertex at z .3 keeps w 2, so the raster, depth .3 and the jitter
// shift are the identity rows' bit for bit, while the origin's clip is (0, 0,
// 0, -1), w < 0 at distance 1). Run 130's leg-2 station module: the arm
// refused it at the origin-distance step every frame (gate 4, `unmatched=
// fade_origin`) although the trace had resolved its node; the distance is
// defined for w <= 0 and the hover schedule must route, hold and refuse
// exactly as `original` does, the refused frames attributed `fade_threshold`.
// overlay: the run-130 distant-object class, the hull pair
// 53a0a641107ed76c/63f96eba9eea7880 (the cutout pair) drawn as the same
// node's source-over sub-mesh (blend on, SRCALPHA/INVSRCALPHA, Z-write off,
// alpha test off, mask 7: the ship's glass/window layer, own vertex buffer
// and textures, the node's rows) right after the node's routed opaque draw:
// P and Q carry A's scope (node 0x1000) with their own vertex buffers, drawn
// over A under original shading (no bracket, no M). The overlay arm routes
// both quads every frame at permille 1000 (own RT1 rows, RT2 masked so the
// depth target is unchanged, counted as overlay_routed), so the resolved
// shift of Q stays a fraction of the raw one.
// foreign: the overlay script with P on A's node address under another
// lifetime serial (scope-gate refusal) and Q on its own node (gate 4): the arm's fail-safe refuses both
// every frame (gate 4, `unmatched=overlay_node`,
// overlay_refused 2) and they stay plain native draws.
// X3M_FADE_RT2_OWNER=on (docs/architecture/fade-rt2-ownership.md): every quad
// the fade arm routes owns RT2: its interior carries the quads' z/w (.3, FP32
// raster tolerance), and with the four-channel lane (X3M_SUN_SHADOW_LANE=1)
// .g = -1 (the invalid-share twin), .b = the clip w (1, or 2 for behind) and
// .a = 1; a refused quad leaves RT2 unchanged, no draw changes RT2 outside its
// own raster, and over the sentinel fill the rest of the upper half keeps -1.
// overlay becomes a fade-arm case (the hull pair's VS has a registers row:
// fade_routed 2, overlay_routed 0) and foreign runs the pair at g_AlphaValue
// .390625 (390 permille, refused at the threshold).
// hull (owner cases; without the owner the run214 refusal): the station
// family 494fe349b8bc12ec/fffdabd910793aba over the sentinel fill on its own
// nodes (no routed draw of the same node before it), fog on at g_FogClip
// (1, 0), fraction 1000: routed, owner.
// cutout (docs/architecture/fade-alpha-cutout-ownership.md section 2.4; original
// shading, four-channel lane, over the sentinel fill): P is a panel drawn with
// a 16x16 texture whose alpha is 0 in the centred 8x8 texels (the hole) and 1
// elsewhere, in the fade-band state plus the alpha test (GREATEREQUAL, ref 1),
// after its textured z_only prepass (803ebfd17f79e413, null PS, the same alpha
// test on the fixed-function stage-0 texture alpha); Q is a blended hull
// quad without the alpha test at z .5 behind the left part of the panel, after
// its own z_only prepass (c78b4c68a87fce74), drawn last (X3M_FIXTURE_FADE_CUTOUT
// selects the order / mip variants, see the cutout loop). Owner on: the panel
// is routed through the fade arm with the alpha test on (fade_tested) and owns
// RT2 exactly on the texels the engine's test passes; the holes keep the fill,
// or the hull's depth where the hull passes the prepass. Owner off: the panel
// is refused at gate 4 (no_zwrite, the pre-change behaviour) and leaves RT1
// and RT2 untouched; the hull is routed with RT2 masked.
// Twelve frames across the eight jitter phases with the rotating camera (cut
// at frame 7); the raw FP16 scene after the quads, RT1, M (not under
// original) and the presented frame are dumped per frame.
void run_fade_route_integration(Fixture& f,const char* original_path) {
    require(f.seam&&f.enabled&&f.taa&&f.hdr&&f.hdr_agx&&f.camera&&f.emission_readback&&f.emission_status,"fade route HDR TAA seam with camera");
    require(f.reference_ready,"fade route reference resolve device");
    const std::string supplied(original_path);const auto slash=supplied.find_last_of("/\\");
    require(slash!=std::string::npos,"fade route original directory");
    char script_setting[16]{};GetEnvironmentVariableA("X3M_FIXTURE_FADE_SCRIPT",script_setting,sizeof script_setting);
    const bool routed_script=std::strcmp(script_setting,"routed")==0,masked_script=std::strcmp(script_setting,"masked")==0,
               sentinel_script=std::strcmp(script_setting,"sentinel")==0,hover_script=std::strcmp(script_setting,"hover")==0,
               behind_script=std::strcmp(script_setting,"behind")==0,foreign_script=std::strcmp(script_setting,"foreign")==0,overlay_script=std::strcmp(script_setting,"overlay")==0||foreign_script,
               hull_script=std::strcmp(script_setting,"hull")==0;
    // zonly / zonly-unjit (owner only): the fog-band depth prepass before the routed quads (see the zonly loop below).
    const bool zonly_script=std::strcmp(script_setting,"zonly")==0,zonly_unjit=std::strcmp(script_setting,"zonly-unjit")==0,zonly_any=zonly_script||zonly_unjit;
    // cutout (owner on or off): the alpha-tested panel over its textured prepass and a hull behind it (see the cutout loop below).
    const bool cutout_script=std::strcmp(script_setting,"cutout")==0;
    // X3M_FADE_RT2_OWNER (fade-rt2-ownership.md) and the four-channel lane RT2 (X3M_SUN_SHADOW_LANE=1).
    char owner_setting[8]{};GetEnvironmentVariableA("X3M_FADE_RT2_OWNER",owner_setting,sizeof owner_setting);const bool owner=std::strcmp(owner_setting,"on")==0;
    char lane_setting[4]{};GetEnvironmentVariableA("X3M_SUN_SHADOW_LANE",lane_setting,sizeof lane_setting);const bool lane=std::strcmp(lane_setting,"1")==0;
    // X3M_FIXTURE_FADE_SCISSOR=1: the option-off age twins scissor A for real as the owner cases do (the pinned scripts do not).
    char scissor_setting[4]{};GetEnvironmentVariableA("X3M_FIXTURE_FADE_SCISSOR",scissor_setting,sizeof scissor_setting);const bool scissor=std::strcmp(scissor_setting,"1")==0;
    if(owner)require(f.emission_status(f.d.p,83)==1u,"fade route: the DLL resolved the owner option on"); // off cases keep their check count
    // original (and behind, the origin-behind-the-camera rows): the hover schedule over the sentinel fill under original shading
    // (X3M_LINEAR_MATERIALS=0, no fade bracket, no composition, no M): the arm alone decides. overlay: the hull pair's
    // same-node source-over sub-mesh over A under original shading (the overlay arm alone decides).
    const bool original_script=std::strcmp(script_setting,"original")==0||behind_script||overlay_script||hull_script;
    require(routed_script||masked_script||sentinel_script||hover_script||original_script||zonly_any||cutout_script,"X3M_FIXTURE_FADE_SCRIPT=routed|masked|sentinel|hover|original|behind|overlay|foreign|hull|zonly|zonly-unjit|cutout");
    const bool over_sentinel=sentinel_script||zonly_any||(original_script&&!overlay_script);
    if(original_script)require(f.emission_status(f.d.p,30)==1u,"fade route original shading: the cutout probe's verdict is Ready without linear materials");
    constexpr unsigned frames=12;
    // hover: g_AlphaValue.x per frame (binary fractions: the fraction .625 * alpha is exact in float) and the arm's decision.
    constexpr float hover_alpha[frames]={.8125f,.71875f,.71875f,.625f,.71875f,.71875f,.8125f,.71875f,.625f,.8125f,.71875f,.71875f};
    constexpr float hover_fog_x[frames]={-.4921875f,-.55078125f,-.55078125f,-.609375f,-.55078125f,-.55078125f,-.4921875f,-.55078125f,-.609375f,-.4921875f,-.55078125f,-.55078125f}; // Q: fraction 1 * (fog_x + 1)
    constexpr bool hover_routed[frames]={1,1,1,0,0,0,1,1,0,1,1,1},hover_held[frames]={0,1,1,0,0,0,0,1,0,0,1,1};
    const bool full_alpha=routed_script||sentinel_script||overlay_script||hull_script||zonly_any||cutout_script; // g_AlphaValue 1, g_FogClip (1, 0), diffuse alpha 1: fraction 1000 (overlay: routed by the same-node rule)
    // original and behind follow the hover schedule (routed, held and below-threshold frames) over the sentinel fill.
    const bool hover_schedule=hover_script||(original_script&&!overlay_script&&!hull_script);
    // hull without the owner: its pair is no fade pair and no same-node overlay, so the arm refuses it (the run214 class).
    const auto routed_frame=[&](unsigned plan){return hover_schedule?hover_routed[plan]:full_alpha&&!foreign_script&&(owner||!hull_script);};
    const auto held_frame=[&](unsigned plan){return hover_schedule&&hover_held[plan];};
    // foreign with the owner: the same pair is a fade pair then, so it keeps its refusal below the threshold (.390625: 390 permille).
    const auto alpha_value=[&](unsigned plan){return hover_schedule?hover_alpha[plan]:foreign_script&&owner?.390625f:full_alpha?1.f:.625f;};
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> ps;
    {
        // overlay: the hull (cutout) pair; hull: the run214 station family; every other script the fade pair.
        const char* vs_id=hull_script?"494fe349b8bc12ec":overlay_script?"53a0a641107ed76c":"b0602757fce6e870";const char* ps_id=hull_script?"fffdabd910793aba":overlay_script?"63f96eba9eea7880":"517540ae6d5e5410";
        auto v=load((supplied.substr(0,slash+1)+"vs_"+vs_id+".bin").c_str());auto p=load((supplied.substr(0,slash+1)+"ps_"+ps_id+".bin").c_str());
        require(fnv(v.data(),v.size()*4)==std::strtoull(vs_id,nullptr,16)&&fnv(p.data(),p.size()*4)==std::strtoull(ps_id,nullptr,16),"fade route exact originals");
        api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(v.data()),&vs.p),"fade route original VS");
        api(f.d->CreatePixelShader(reinterpret_cast<const DWORD*>(p.data()),&ps.p),"fade route original PS");
    }
    // Two quads, clip z .3 (in front of A), the live fade fixture's vertex
    // layout. P: x -.8..-.2, y .2...8 (pixels 6.4..25.6); Q: x .2...8, same rows.
    struct SourceVertex {float p[3],uv[2],n[3],b[3],t[3];};
    const auto quad=[](float l,float r,float t,float b,SourceVertex out[4]) {
        const SourceVertex v[4]={{{l,t,.3f},{0,0},{0,0,1},{0,1,0},{1,0,0}},{{r,t,.3f},{1,0},{0,0,1},{0,1,0},{1,0,0}},{{l,b,.3f},{0,1},{0,0,1},{0,1,0},{1,0,0}},{{r,b,.3f},{1,1},{0,0,1},{0,1,0},{1,0,0}}};
        std::memcpy(out,v,sizeof v);
    };
    SourceVertex quads[2][4];quad(-.8f,-.2f,.8f,.2f,quads[0]);quad(.2f,.8f,.8f,.2f,quads[1]);
    // cutout: Q is the hull behind the left part of the panel (x -.8..-.5: pixels 6.4..16, z .5).
    if(cutout_script){quad(-.8f,-.5f,.8f,.2f,quads[1]);for(auto& v:quads[1])v.p[2]=.5f;}
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,20,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},{0,44,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},D3DDECL_END()};
    Com<IDirect3DVertexDeclaration9> declaration;Com<IDirect3DVertexBuffer9> vertices[2];Com<IDirect3DIndexBuffer9> indices;
    api(f.d->CreateVertexDeclaration(elements,&declaration.p),"fade route declaration");
    void* data=nullptr;
    // hull: the station family's VS reads TEXCOORD0 as four lanes (the light map's UV in .zw, which its pixel program's
    // output follows), so its quads carry (u, v, u, v) there; every other script keeps the two-lane layout above.
    struct HullVertex {float p[3],uv[4],n[3],b[3],t[3];};
    HullVertex hull_quads[2][4];
    for(unsigned i=0;i<2;++i)for(unsigned k=0;k<4;++k){const auto& v=quads[i][k];hull_quads[i][k]={{v.p[0],v.p[1],v.p[2]},{v.uv[0],v.uv[1],v.uv[0],v.uv[1]},{v.n[0],v.n[1],v.n[2]},{v.b[0],v.b[1],v.b[2]},{v.t[0],v.t[1],v.t[2]}};}
    const D3DVERTEXELEMENT9 hull_elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,28,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,40,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_BINORMAL,0},{0,52,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TANGENT,0},D3DDECL_END()};
    if(hull_script){declaration.reset();api(f.d->CreateVertexDeclaration(hull_elements,&declaration.p),"fade route hull declaration");}
    const UINT vertex_stride=hull_script?UINT(sizeof(HullVertex)):UINT(sizeof(SourceVertex));
    for(unsigned i=0;i<2;++i){const void* source=hull_script?static_cast<const void*>(hull_quads[i]):static_cast<const void*>(quads[i]);const UINT bytes=hull_script?UINT(sizeof hull_quads[i]):UINT(sizeof quads[i]);
        api(f.d->CreateVertexBuffer(bytes,0,0,D3DPOOL_MANAGED,&vertices[i].p,nullptr),"fade route vertices");api(vertices[i]->Lock(0,0,&data,0),"fade route vertex lock");std::memcpy(data,source,bytes);api(vertices[i]->Unlock(),"fade route vertex unlock");}
    const unsigned short triangles[]={0,1,2,2,1,3};
    api(f.d->CreateIndexBuffer(sizeof triangles,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr),"fade route indices");
    api(indices->Lock(0,0,&data,0),"fade route index lock");std::memcpy(data,triangles,sizeof triangles);api(indices->Unlock(),"fade route index unlock");
    // Textures: P's constant texels are run_linear_distance_fade.cases()[0]
    // (diffuse alpha 1 in the routed script: the routed composite is the
    // source itself); Q's diffuse is a 16x16 ramp (r = u, g = v, b .5, alpha
    // 1: Q is the shift witness, not the oracle, and needs contrast over A).
    const float diffuse_alpha=full_alpha?1.f:.5f;
    const float texels[][4]={{.5f,.25f,.75f,diffuse_alpha},{.25f,.875f,.125f,.75f},{.125f,.25f,.0625f,.25f}};
    Com<IDirect3DTexture9> textures[3],ramp;
    for(unsigned i=0;i<3;++i) {
        api(f.d->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&textures[i].p,nullptr),"fade route texture");
        D3DLOCKED_RECT lock{};api(textures[i]->LockRect(0,&lock,nullptr,0),"fade route texture lock");std::memcpy(lock.pBits,texels[i],16);api(textures[i]->UnlockRect(0),"fade route texture unlock");
    }
    // overlay: the hull pair samples the node's cube map at stage 3 (one constant texel per face).
    Com<IDirect3DCubeTexture9> cube;
    if(overlay_script||hull_script){
        api(f.d->CreateCubeTexture(1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&cube.p,nullptr),"fade route overlay cube");
        for(unsigned face=0;face<6;++face){D3DLOCKED_RECT lock{};api(cube->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0),"fade route overlay cube lock");const float value[]={.25f,.5f,.125f,1};std::memcpy(lock.pBits,value,16);api(cube->UnlockRect(D3DCUBEMAP_FACES(face),0),"fade route overlay cube unlock");}
    }
    {
        api(f.d->CreateTexture(16,16,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&ramp.p,nullptr),"fade route ramp");
        D3DLOCKED_RECT lock{};api(ramp->LockRect(0,&lock,nullptr,0),"fade route ramp lock");
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const float value[4]={float(x)/15.f,float(y)/15.f,.5f,1.f};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,value,16);}
        api(ramp->UnlockRect(0),"fade route ramp unlock");
    }
    // The fade pair's native inputs (motion_output_distance_fade_inc.h pair 0):
    // clip rows c24-27 identity, normal rows c31-33, camera c36.w = 4, uv
    // rows c37/38 (oT0 = ((u, v, 1) . c37.xyz, (u, v, 1) . c38.xyz): P keeps
    // the live fixture's constant offset, Q the identity rows so the ramp
    // spans the quad), g_AlphaValue c39, emissive c40, g_FogClip c41, point
    // light c0-2, b0 fog on, i0 one point light. Q's camera sits at the
    // origin (c36.w 0) so its shader alpha in the masked script is ~.4 rather
    // than P's .078 (distance 4): the CPU estimate uses the clip rows, not
    // the camera constant, and stays 390 permille for both quads.
    const auto bind=[&](unsigned which,unsigned plan) {
        f.scene_states();
        for(auto s:{D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_DITHERENABLE,D3DRS_STENCILENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_ALPHATESTENABLE})api(f.d->SetRenderState(s,FALSE),"fade route off state");
        api(f.d->SetRenderState(D3DRS_ZENABLE,TRUE),"fade route depth test");api(f.d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"fade route no depth write");api(f.d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL),"fade route LE depth");
        api(f.d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"fade route both faces");api(f.d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID),"fade route solid");
        api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"fade route blend");api(f.d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA),"fade route source alpha");api(f.d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA),"fade route inverse source alpha");api(f.d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD),"fade route ADD");
        api(f.d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"fade route RGB mask");
        const RECT whole{0,0,LONG(f.W),LONG(f.H)};api(f.d->SetScissorRect(&whole),"fade route scissor");api(f.d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"fade route scissor enabled");
        for(unsigned i=0;i<16;++i)api(f.d->SetRenderState(D3DRENDERSTATETYPE((i<8?D3DRS_WRAP0:D3DRS_WRAP8)+i%8),0),"fade route WRAP inputs");
        api(f.d->SetVertexDeclaration(declaration.p),"fade route declaration bind");api(f.d->SetStreamSource(0,vertices[which].p,0,vertex_stride),"fade route stream");api(f.d->SetStreamSourceFreq(0,1),"fade route frequency");api(f.d->SetIndices(indices.p),"fade route index binding");
        api(f.d->SetVertexShader(vs.p),"fade route VS bind");api(f.d->SetPixelShader(ps.p),"fade route PS bind");
        float vc[48][4]{},pc[24][4]{};
        // behind: the same raster as the identity rows (w 2 at every vertex, z .3) with the origin's w = -1 (see the header comment).
        if(behind_script){vc[24][0]=2;vc[25][1]=2;vc[26][2]=2;vc[27][2]=10;vc[27][3]=-1;}else for(unsigned i=0;i<4;++i)vc[24+i][i]=1;
        if(zonly_any)vc[26][0]=-.125f; // zonly: z = .3 - x/8 (see the zonly loop)
        for(unsigned i=0;i<3;++i)vc[31+i][i]=1;
        const bool hover_q=hover_schedule&&which==1;
        if(which){vc[37][0]=1;vc[38][1]=1;if(hover_q)vc[36][3]=4;}else{vc[36][3]=4;vc[37][2]=.0625f;vc[38][2]=.1875f;}
        vc[39][0]=hover_q?1.f:alpha_value(plan);vc[40][0]=.25f;vc[40][1]=.125f;vc[40][2]=.0625f;
        vc[41][0]=hover_q?hover_fog_x[plan]:full_alpha?1.f:.75f;vc[41][1]=hover_q?-1.f:full_alpha?0.f:.125f;
        vc[0][2]=2;vc[1][0]=.5f;vc[1][1]=.25f;vc[1][2]=.125f;vc[2][0]=2;vc[2][1]=.25f;vc[2][2]=.125f;
        // The hull pair's pixel inputs and sampler roles are the cutout fixture's (motion_output_cutout_inc.h pair 0).
        if(overlay_script){pc[0][0]=pc[1][1]=pc[2][2]=1;pc[4][2]=1;pc[5][0]=.375f;pc[5][1]=.25f;pc[5][2]=.5f;pc[6][2]=-1;pc[7][0]=.125f;pc[7][1]=.5f;pc[7][2]=.25f;}
        // hull (fffdabd910793aba's CTAB): the same two directional lights one register up (c5..c8), diffuse strength c12 = 1,
        // specular power c10 = 8, every other material term 0; b0 (decal) and b1 (colour mixing) off.
        else if(hull_script){pc[5][2]=1;pc[6][0]=.375f;pc[6][1]=.25f;pc[6][2]=.5f;pc[7][2]=-1;pc[8][0]=.125f;pc[8][1]=.5f;pc[8][2]=.25f;pc[10][0]=8;pc[12][0]=1;}
        else{pc[0][2]=1;pc[1][0]=.375f;pc[1][1]=.25f;pc[1][2]=.5f;pc[2][2]=-1;pc[3][0]=.125f;pc[3][1]=.5f;pc[3][2]=.25f;pc[4][0]=.5f;pc[5][0]=1;}
        api(f.d->SetVertexShaderConstantF(0,vc[0],48),"fade route VS inputs");api(f.d->SetPixelShaderConstantF(0,pc[0],hull_script?24:12),"fade route PS inputs");
        if(hull_script){const BOOL off[2]={FALSE,FALSE};api(f.d->SetPixelShaderConstantB(0,off,2),"fade route hull PS switches");}
        const int count[4]={1,0,1,0};const BOOL fog=!overlay_script;api(f.d->SetVertexShaderConstantI(0,count,1),"fade route point count");api(f.d->SetVertexShaderConstantB(0,&fog,1),"fade route fog");
        for(unsigned stage=0;stage<7;++stage) {
            const bool hull_roles=overlay_script||hull_script; // the hull pairs' sampler roles (stage 4: fffdabd9's occlusion map)
            const bool hull_ramp=hull_script&&which&&stage==2; // hull Q: the ramp on the light-map stage its output follows
            IDirect3DBaseTexture9* texture=stage==0?(which?ramp.p:textures[0].p):stage==1?(hull_roles?textures[2].p:textures[1].p):stage==2?(hull_ramp?ramp.p:hull_roles?textures[1].p:textures[2].p):(stage==3&&hull_roles)?static_cast<IDirect3DBaseTexture9*>(cube.p):(stage==4&&hull_script)?textures[1].p:nullptr;
            api(f.d->SetTexture(stage,texture),"fade route sampler role");
            for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})api(f.d->SetSamplerState(stage,filter,(which&&stage==0)||hull_ramp?D3DTEXF_LINEAR:D3DTEXF_POINT),"fade route sampling");
            api(f.d->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"fade route no mip");api(f.d->SetSamplerState(stage,D3DSAMP_SRGBTEXTURE,FALSE),"fade route numeric sampler");
            api(f.d->SetSamplerState(stage,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"fade route address u");api(f.d->SetSamplerState(stage,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"fade route address v");
            const float bias=0;DWORD bits;std::memcpy(&bits,&bias,4);api(f.d->SetSamplerState(stage,D3DSAMP_MIPMAPLODBIAS,bits),"fade route zero bias");
        }
    };
    const auto read=[&](unsigned which){std::vector<float> out(std::size_t(f.W)*f.H*(which==2?(lane?4:1):4));unsigned w=0,h=0;api(f.emission_readback(f.d.p,which,out.data(),unsigned(out.size()),&w,&h),"fade route raw target");require(w==f.W&&h==f.H,"fade route target dimensions");return out;};
    const auto scene=[&](){Com<IDirect3DSurface9> logical;api(f.d->GetRenderTarget(0,&logical.p),"fade route logical view restores lazy MRTs");unsigned w=0,h=0;auto result=f.hdr_image(&w,&h);require(w==f.W&&h==f.H,"fade route HDR dimensions");return result;};
    const auto write=[&](const char* kind,const std::vector<float>& values){char path[96];std::snprintf(path,sizeof path,"fade_route_%s_%llu.f32",kind,f.frame);FILE* file=std::fopen(path,"wb");require(file!=nullptr,"fade route evidence file");require(std::fwrite(values.data(),4,values.size(),file)==values.size(),"fade route evidence bytes");std::fclose(file);};
    Object objects[2]={f.b,f.b};
    for(unsigned i=0;i<2;++i){objects[i].scope.node_serial=9100+i;objects[i].scope.node=0x910000+i*0x100;objects[i].scope.mesh=0x920000+i*0x100;objects[i].vb=vertices[i].p;objects[i].recorded=false;}
    // overlay: both quads are A's node (its scope; their own vertex buffers key the rows), drawn right after A.
    if(overlay_script&&!foreign_script)for(unsigned i=0;i<2;++i)objects[i].scope=f.a.scope;
    // foreign: P, the draw right after A, is A's node address with another lifetime serial (a node freed and
    // reallocated within the frame: passes the gate-4 identity, refused at the scope gate); Q keeps its own
    // node (refused at gate 4 by identity and adjacency).
    if(foreign_script){objects[0].scope=f.a.scope;objects[0].scope.node_serial=9100;}
    // Interior pixels of each quad (edges excluded): P columns 9..22, Q 41..54, rows 9..22.
    const auto interior=[&](unsigned which,unsigned x,unsigned y){const unsigned l=which?41:9,r=which?55:23;return x>=l&&x<r&&y>=9&&y<23;};
    // The pixels a quad's raster can reach under the jitter (P 6.4..25.6, Q 38.4..57.6, rows 6.4..25.6, +-0.5 px).
    const auto near_quad=[&](unsigned which,unsigned x,unsigned y){const unsigned l=which?37:5,r=which?59:27;return x>=l&&x<r&&y>=5&&y<27;};
    const unsigned rt2_lanes=lane?4u:1u;const float quad_w=behind_script?2.f:1.f;
    // The lane RT2 under linear materials: a material fade row has no invalid-share twin, so the owner is withdrawn and
    // RT2 stays masked (fade_owner_masked counts the routed quads).
    const bool owner_masked=owner&&lane&&!original_script;
    // ---- zonly / zonly-unjit (fade-rt2-ownership.md section 7: the prepass parity oracle over RT2) ----
    //
    // The engine's fog-band sequence (distance-fade.md section 5, asteroid-fog-temporal.md run 47): a depth-only
    // prepass of every fog-band node (z_only vs_1_1, null PS, ZWRITEENABLE on, COLORWRITEENABLE 0, LESSEQUAL), then
    // the blended draws. Here both quads get the prepass first, then the routed fade-band draws of the exact fade
    // pair at fraction 1000 with the owner on (R32F RT2, linear materials, A scissored to the lower half so the quads
    // sit over the sentinel fill). The clip rows put a depth slope along x (row 2 = (-1/8, 0, 1, 0): z = .3 - x/8,
    // w 1; x/8 is exact, so both programs compute the same depths), ~3.9e-3 per pixel: an x offset between the
    // prepass and the quad of a fraction of a pixel decides LESSEQUAL. zonly: the prepass is the reviewed z_only
    // alias, which the route jitters with the scene: every interior pixel of both quads must be covered in colour and
    // own RT2 (the quad's z/w). zonly-unjit (the witness): the prepass is an unreviewed vs_1_1 with the same four
    // clip rows (dp4 of the position against c0-c3), which the route does not jitter and counts as an unjittered
    // depth writer: on frames with jx > 0 the jittered quad fragment carries the content of p - jx (farther on this
    // slope) and fails LESSEQUAL, so the interior drops in colour and RT2 keeps the fill; jx < 0 passes. Both
    // scripts: per interior pixel the colour is covered exactly where RT2 was written (parity), RT2 elsewhere
    // unchanged.
    if(zonly_any){
        require(owner&&!lane&&!original_script&&f.emission_status(f.d.p,83)==1u,"fade zonly: the owner on the R32F RT2 under linear materials");
        Com<IDirect3DVertexShader9> prepass_vs;
        if(zonly_script){
            const auto code=load((supplied.substr(0,slash+1)+"vs_c78b4c68a87fce74.bin").c_str());
            require(code.size()==89&&fnv(code.data(),code.size()*4)==0xc78b4c68a87fce74ull,"fade zonly: local z_only program is the reviewed alias");
            api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&prepass_vs.p),"fade zonly z_only VS");
        } else {
            // vs_1_1; dcl_position v0; dp4 oPos.x/y/z/w, v0, c0/c1/c2/c3 (v0.w 1 from the FLOAT3 element).
            static constexpr DWORD unreviewed[]={0xfffe0101u,0x0000001fu,0x80000000u,0x900f0000u,
                0x00000009u,0xc0010000u,0x90e40000u,0xa0e40000u,0x00000009u,0xc0020000u,0x90e40000u,0xa0e40001u,
                0x00000009u,0xc0040000u,0x90e40000u,0xa0e40002u,0x00000009u,0xc0080000u,0x90e40000u,0xa0e40003u,0x0000ffffu};
            api(f.d->CreateVertexShader(unreviewed,&prepass_vs.p),"fade zonly unreviewed depth VS");
        }
        const auto prepass=[&](unsigned which,unsigned plan){
            f.scope(nullptr);bind(which,plan);
            api(f.d->SetVertexShader(prepass_vs.p),"fade zonly prepass VS");api(f.d->SetPixelShader(nullptr),"fade zonly null PS");
            api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"fade zonly prepass blend off");api(f.d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE),"fade zonly prepass depth write");
            api(f.d->SetRenderState(D3DRS_COLORWRITEENABLE,0),"fade zonly prepass colour mask off");
            float rows[16]{};rows[0]=rows[5]=rows[10]=rows[15]=1;rows[8]=-.125f; // the quads' clip rows (c24-27 of the fade pair)
            api(f.d->SetVertexShaderConstantF(0,rows,4),"fade zonly prepass clip rows");
            const auto state=f.snapshot();
            api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"fade zonly prepass DIP");++f.draw_index;
            f.compare(state,f.snapshot(),"fade zonly prepass restoration");
            float after[16];api(f.d->GetVertexShaderConstantF(0,after,4),"fade zonly prepass rows after");
            require(std::memcmp(rows,after,sizeof rows)==0,"fade zonly: c0-3 restored bit-exactly after the prepass");
        };
        for(unsigned plan=0;plan<frames;++plan){
            f.frame_begin();f.linear_material_inputs();f.write_reserved();
            const RECT lower{0,LONG(f.H/2),LONG(f.W),LONG(f.H)};api(f.d->SetScissorRect(&lower),"fade zonly A below the quads");
            api(f.d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"fade zonly A scissored");
            f.draw(f.a,0,0,0,true,true,f.a.recorded,Alter::None,false);
            require(f.emission_status(f.d.p,16)==2u,"fade zonly: the fade producer is the required one");
            prepass(0,plan);prepass(1,plan);
            // z/w tolerance: the flat quads' FP32 raster bound (4e-6) plus 1/256 px of sub-pixel position quantisation on the slope.
            constexpr double depth_tolerance=4e-6+.125*2./64./256.;
            unsigned pixels=0,fill_before=0,color_holes=0,rt2_holes=0,mismatch=0,outside_changed=0,routed=0;double max_depth_error=0;
            for(unsigned which=0;which<2;++which){
                f.scope(&objects[which]);bind(which,plan);
                const auto before=scene(),before_motion=read(1),before_depth=read(2);
                const auto state=f.snapshot();
                const unsigned routed_before=f.emission_status(f.d.p,50),refused_before=f.emission_status(f.d.p,51);
                api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"fade zonly actual original DIP");++f.draw_index;
                f.compare(state,f.snapshot(),"fade zonly complete draw restoration");
                const auto after=scene(),motion=read(1),depth=read(2);
                require(f.emission_status(f.d.p,50)-routed_before==1u&&f.emission_status(f.d.p,51)==refused_before,"fade zonly: the arm routes the quad (fraction 1000)");++routed;
                const bool matched=objects[which].recorded;
                for(unsigned y=0;y<f.H;++y)for(unsigned x=0;x<f.W;++x){
                    const std::size_t n=std::size_t(y)*f.W+x,i=4*n;
                    const bool written=std::memcmp(&depth[n],&before_depth[n],4)!=0;
                    if(!interior(which,x,y)){if(!near_quad(which,x,y))outside_changed+=written;continue;}
                    ++pixels;fill_before+=before_depth[n]==-1.f;
                    const bool covered=std::memcmp(&before[i],&after[i],12)!=0;
                    color_holes+=!covered;rt2_holes+=!written&&depth[n]==-1.f;mismatch+=covered!=written;
                    if(!written)continue;
                    const double z=.3-.125*(2.*(x-f.jx)/f.W-1.); // the quad's z/w at the jittered sample (D3D9: pixel centres at integer positions)
                    max_depth_error=std::max(max_depth_error,std::fabs(double(depth[n])-z));
                    const double u=(x-f.jx)/f.W+.5/f.W,v=(y-f.jy)/f.H+.5/f.H;
                    const bool own=matched?(std::fabs(motion[i]-u)*f.W<.01&&std::fabs(motion[i+1]-v)*f.H<.01&&std::fabs(motion[i+2]-z)<depth_tolerance&&motion[i+3]==1)
                                   :before_motion[i+3]==-1&&motion[i]==0&&motion[i+1]==0&&motion[i+2]==0&&motion[i+3]==-1;
                    if(!own)std::printf("FADE_ZONLY_PIXEL_DIFF frame=%llu quad=%u x=%u y=%u matched=%u motion=%.9g,%.9g,%.9g,%.9g depth=%.9g\n",f.frame,which,x,y,matched,motion[i],motion[i+1],motion[i+2],motion[i+3],depth[n]);
                    require_quiet(own,"fade zonly: a covered pixel carries the quad's own RT1 rows (the sentinel over the fill when unmatched)");
                }
                objects[which].recorded=true;
            }
            std::printf("FADE_ZONLY frame=%llu script=%s jx=%.6f jy=%.6f pixels=%u fill_before=%u color_holes=%u rt2_holes=%u mismatch=%u outside_changed=%u max_depth_error=%.9g routed=%u fade_routed=%u\n",
                        f.frame,script_setting,f.jx,f.jy,pixels,fill_before,color_holes,rt2_holes,mismatch,outside_changed,max_depth_error,routed,f.emission_status(f.d.p,50));
            require(pixels==2u*14u*14u&&fill_before==pixels,"fade zonly: RT2 holds the fill under both quads after the prepass");
            require(mismatch==0&&outside_changed==0&&max_depth_error<depth_tolerance,"fade zonly: RT2 written exactly where the colour is covered, with the quad's z/w; nowhere else");
            if(zonly_script)require(color_holes==0&&rt2_holes==0,"fade zonly: the jittered prepass drops no interior pixel in colour or RT2");
            else if(f.jx>0)require(rt2_holes>pixels/2,"fade zonly-unjit: the unjittered prepass drops the interior of the jittered quads (RT2 keeps the fill)");
            else if(f.jx<0)require(rt2_holes==0,"fade zonly-unjit: a negative x jitter passes LESSEQUAL on this slope");
            const auto color=scene(),mask=read(3);
            f.emission_reference_color=color;f.emission_reference_mask=mask;f.emissions_enabled=true;f.emission_mask_valid=f.emission_status(f.d.p,1)!=0;
            f.boundary();
            api(f.d->EndScene(),"fade zonly EndScene");f.write_presented(f.color_image());api(f.d->SetDepthStencilSurface(f.depth.p),"fade zonly depth restore");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"fade zonly Present");++f.frame;++f.frames_since_reset;
        }
        api(f.d->SetIndices(nullptr),"fade zonly final indices release");api(f.d->SetStreamSource(0,nullptr,0,0),"fade zonly final stream release");
        std::printf("FADE_ZONLY_CHECKS frames=%u script=%s quads=2\n",frames,script_setting);
        return;
    }
    // ---- cutout (fade-alpha-cutout-ownership.md section 2.4: the alpha-tested fade-band panel as an RT2 owner) ----
    //
    // Per frame, over the sentinel fill (A scissored to the lower half), original shading, the four-channel lane RT2:
    // the panel's textured z_only prepass (803ebfd17f79e413: oPos from c0-c3, oT0 = TEXCOORD0; null PS, the
    // fixed-function stage 0 selects the texture's alpha, alpha test GREATEREQUAL, Z-write on, colour mask 0), the
    // hull's prepass (c78b4c68a87fce74, no alpha test) at z .5, then the fade-band draws of the fade pair at fraction
    // 1000: the panel with the alpha test on (GREATEREQUAL 1, the prepass's texture) and the hull without it.
    // X3M_FIXTURE_FADE_CUTOUT selects the variant: unset, panel then hull (a wrong Z test would let the hull overwrite
    // the panel's RT2); `order`, hull first, the prepass at ALPHAREF 128 and the texture's bottom four texel rows at
    // alpha .25 (the colour draw passes them, the prepass does not: the hull behind passes there first and the panel,
    // nearer, overwrites it); `mip`, a 32x32 texture whose level 0 is opaque everywhere and level 1 carries the hole,
    // sampled with MIPFILTER POINT at LOD 0.74 (level 1): under X3M_TAA_MIP_BIAS=-0.5 a biased colour draw would read
    // level 0 (LOD 0.24) and cover the hole. The panel's 16-texel pattern (level 1 under `mip`) has 1.2 px texels: the
    // hole is texels 4..11 on both axes (positions 11.2..20.8), `order`'s band texel rows 12..15. A pixel is
    // classified when every sample position its jitter can reach (+-0.5 px) sees one panel class (opaque, band, hole or
    // outside the quad) and one hull state; for it the fixture evaluates the engine's sequence on the CPU (the Z buffer
    // after both prepasses, each draw's alpha and Z test in draw order) and requires each draw's colour coverage, RT2
    // write, RT2 value and RT1 rows to match it, and the frame's final RT2 to hold the last passing draw's depth or the
    // fill. Every other pixel of the region is held to the per-draw parity (RT2 written exactly where the colour is
    // covered, never with the owner off) and every frame-final RT2 change to a pixel some draw covered in colour.
    if(cutout_script){
        require(lane&&f.emission_status(f.d.p,30)==1u,"fade cutout: the four-channel lane under original shading, the cutout probe's verdict Ready");
        require(f.emission_status(f.d.p,83)==unsigned(owner),"fade cutout: the DLL resolved the owner option as requested");
        char variant_setting[8]{};GetEnvironmentVariableA("X3M_FIXTURE_FADE_CUTOUT",variant_setting,sizeof variant_setting);
        const bool order_variant=std::strcmp(variant_setting,"order")==0,mip_variant=std::strcmp(variant_setting,"mip")==0;
        require(!variant_setting[0]||order_variant||mip_variant,"X3M_FIXTURE_FADE_CUTOUT=|order|mip");
        const DWORD prepass_ref=order_variant?128:1,colour_ref=1;
        Com<IDirect3DVertexShader9> panel_prepass,hull_prepass;
        {
            const char* ids[2]={"803ebfd17f79e413","c78b4c68a87fce74"};const std::size_t words[2]={95,89};Com<IDirect3DVertexShader9>* targets[2]={&panel_prepass,&hull_prepass};
            for(unsigned k=0;k<2;++k){
                const auto code=load((supplied.substr(0,slash+1)+"vs_"+ids[k]+".bin").c_str());
                require(code.size()==words[k]&&fnv(code.data(),code.size()*4)==std::strtoull(ids[k],nullptr,16),"fade cutout: local z_only programs are the reviewed aliases");
                api(f.d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&targets[k]->p),"fade cutout z_only VS");
            }
        }
        // The 16-texel pattern's alpha: 0 in the hole, .25 in order's band, 1 elsewhere.
        const auto pattern_alpha=[&](unsigned tx,unsigned ty){return tx>=4&&tx<12&&ty>=4&&ty<12?0.f:order_variant&&ty>=12?.25f:1.f;};
        Com<IDirect3DTexture9> cutout;
        {
            const UINT size=mip_variant?32:16,levels=mip_variant?2:1;
            api(f.d->CreateTexture(size,size,levels,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&cutout.p,nullptr),"fade cutout texture");
            for(UINT level=0;level<levels;++level){
                const UINT n=size>>level;const bool pattern=n==16; // mip: level 0 (32) opaque, level 1 (16) the pattern
                D3DLOCKED_RECT lock{};api(cutout->LockRect(level,&lock,nullptr,0),"fade cutout texture lock");
                for(UINT y=0;y<n;++y)for(UINT x=0;x<n;++x){const float value[4]={.5f,.25f,.75f,pattern?pattern_alpha(x,y):1.f};std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,value,16);}
                api(cutout->UnlockRect(level),"fade cutout texture unlock");
            }
        }
        const auto alpha_test=[&](DWORD ref){
            api(f.d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE),"fade cutout alpha test");api(f.d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATEREQUAL),"fade cutout GREATEREQUAL");
            api(f.d->SetRenderState(D3DRS_ALPHAREF,ref),"fade cutout alpha ref");
        };
        // The panel: P's rows and pixel inputs with identity UV rows (c37/c38) so the texture spans the quad.
        const auto bind_panel=[&](unsigned plan,DWORD ref){
            bind(0,plan);
            const float uv[2][4]={{1,0,0,0},{0,1,0,0}};api(f.d->SetVertexShaderConstantF(37,uv[0],2),"fade cutout identity UV rows");
            api(f.d->SetTexture(0,cutout.p),"fade cutout texture bind");alpha_test(ref);
            if(mip_variant)api(f.d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_POINT),"fade cutout nearest mip");
        };
        const auto prepass=[&](unsigned which,unsigned plan){
            f.scope(nullptr);if(which)bind(1,plan);else bind_panel(plan,prepass_ref);
            api(f.d->SetVertexShader(which?hull_prepass.p:panel_prepass.p),"fade cutout prepass VS");api(f.d->SetPixelShader(nullptr),"fade cutout null PS");
            api(f.d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"fade cutout prepass blend off");api(f.d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE),"fade cutout prepass depth write");
            api(f.d->SetRenderState(D3DRS_COLORWRITEENABLE,0),"fade cutout prepass colour mask off");
            // Fixed-function stage 0: colour and alpha straight from the texture (the prepass alpha test reads its alpha).
            const D3DTEXTURESTAGESTATETYPE stage_states[6]={D3DTSS_COLOROP,D3DTSS_COLORARG1,D3DTSS_ALPHAOP,D3DTSS_ALPHAARG1,D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS};
            const DWORD stage_values[6]={D3DTOP_SELECTARG1,D3DTA_TEXTURE,D3DTOP_SELECTARG1,D3DTA_TEXTURE,0,D3DTTFF_DISABLE};
            for(unsigned k=0;k<6;++k)api(f.d->SetTextureStageState(0,stage_states[k],stage_values[k]),"fade cutout stage 0");
            api(f.d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE),"fade cutout stage 1 colour off");api(f.d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE),"fade cutout stage 1 alpha off");
            float rows[16]{};rows[0]=rows[5]=rows[10]=rows[15]=1; // the quads' clip rows (c24-27 of the fade pair)
            api(f.d->SetVertexShaderConstantF(0,rows,4),"fade cutout prepass clip rows");
            const auto state=f.snapshot();
            api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"fade cutout prepass DIP");++f.draw_index;
            f.compare(state,f.snapshot(),"fade cutout prepass restoration");
            float after[16];api(f.d->GetVertexShaderConstantF(0,after,4),"fade cutout prepass rows after");
            require(std::memcmp(rows,after,sizeof rows)==0,"fade cutout: c0-3 restored bit-exactly after the prepass");
        };
        // Classification (screen positions; the panel spans 6.4..25.6 on both axes, the hull x 6.4..16, y 6.4..25.6).
        // panel_class: -1 ambiguous, 0 no panel fragment (hole or outside the quad), 1 opaque, 2 band.
        constexpr unsigned region=32; // x, y 0..31: both quads; nothing outside it may change
        const auto class_at=[&](double sx,double sy)->int{
            if(sx<6.4||sx>=25.6||sy<6.4||sy>=25.6)return 0;
            const float a=pattern_alpha(unsigned((sx-6.4)/1.2),unsigned((sy-6.4)/1.2));return a==0.f?0:a==1.f?1:2;
        };
        const auto panel_class=[&](unsigned x,unsigned y)->int{
            const int c=class_at(x-.5,y-.5);
            return class_at(x+.5,y-.5)==c&&class_at(x-.5,y+.5)==c&&class_at(x+.5,y+.5)==c?c:-1;
        };
        const auto hull_state=[&](unsigned x,unsigned y)->int{ // 1 inside, 0 outside, -1 ambiguous
            const bool in=x-.5>=6.4&&x+.5<16.&&y-.5>=6.4&&y+.5<25.6,out=x+.5<6.4||x-.5>=16.||y+.5<6.4||y-.5>=25.6;
            return in?1:out?0:-1;
        };
        struct Model{bool classified=false,pass[2]{};double final_z=-1;};
        std::vector<Model> model(region*region);
        const double quad_z[2]={.3,.5};const unsigned draw_order[2]={order_variant?1u:0u,order_variant?0u:1u};
        for(unsigned y=0;y<region;++y)for(unsigned x=0;x<region;++x){
            auto& m=model[y*region+x];const int c=panel_class(x,y),h=hull_state(x,y);
            if(c<0||h<0)continue;
            m.classified=true;
            const double a=c==1?1.:c==2?.25:0.;
            double zbuf=1.;
            if(c&&a*255.>=double(prepass_ref))zbuf=std::min(zbuf,.3); // the panel prepass (alpha tested), then the hull's
            if(h)zbuf=std::min(zbuf,.5);
            for(unsigned which:draw_order){
                const bool pass=which?h==1&&.5<=zbuf:c!=0&&a*255.>=double(colour_ref)&&.3<=zbuf;
                m.pass[which]=pass;if(pass)m.final_z=quad_z[which];
            }
        }
        constexpr double depth_tolerance=4e-6;
        for(unsigned plan=0;plan<frames;++plan){
            f.frame_begin();f.linear_material_inputs();f.write_reserved();
            const RECT lower{0,LONG(f.H/2),LONG(f.W),LONG(f.H)};api(f.d->SetScissorRect(&lower),"fade cutout A below the quads");
            api(f.d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"fade cutout A scissored");
            f.draw(f.a,0,0,0,true,true,f.a.recorded,Alter::None,false);
            require(f.emission_status(f.d.p,16)==0u,"fade cutout: no fade producer under original shading");
            prepass(0,plan);prepass(1,plan);
            const auto frame_rt2_before=read(2);
            std::vector<unsigned char> covered_any(std::size_t(f.W)*f.H);
            unsigned classified=0,passes[2]{},owned[2]{},model_mismatch=0,parity_mismatch=0,outside_changed=0,motion_mismatch=0;
            double max_depth_error=0,max_w_error=0;
            unsigned panel_routed=0,panel_tested=0,panel_refused=0,panel_gate4=0,hull_routed=0,hull_tested=0;
            for(unsigned which:draw_order){
                f.scope(&objects[which]);if(which)bind(1,plan);else bind_panel(plan,colour_ref);
                const auto before=scene(),before_motion=read(1),before_depth=read(2);
                const auto state=f.snapshot();
                const unsigned routed_before=f.emission_status(f.d.p,50),tested_before=f.emission_status(f.d.p,86),refused_before=f.emission_status(f.d.p,51),gate4_before=f.emission_status(f.d.p,99);
                api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"fade cutout actual original DIP");++f.draw_index;
                f.compare(state,f.snapshot(),"fade cutout complete draw restoration");
                const auto after=scene(),motion=read(1),depth=read(2);
                const unsigned routed=f.emission_status(f.d.p,50)-routed_before,tested=f.emission_status(f.d.p,86)-tested_before;
                if(which){hull_routed=routed;hull_tested=tested;}
                else{panel_routed=routed;panel_tested=tested;panel_refused=f.emission_status(f.d.p,51)-refused_before;panel_gate4=f.emission_status(f.d.p,99)-gate4_before;}
                // Owner: both draws own RT2; off: the panel is refused (native) and the hull routed with RT2 masked.
                const bool writes_rt2=owner,routes=which||owner,matched=routes&&objects[which].recorded;
                const double z=quad_z[which];
                for(unsigned y=0;y<f.H;++y)for(unsigned x=0;x<f.W;++x){
                    const std::size_t n=std::size_t(y)*f.W+x,i=4*n;
                    const float* now=&depth[n*4];const float* was=&before_depth[n*4];
                    const bool written=std::memcmp(now,was,16)!=0,covered=std::memcmp(&before[i],&after[i],12)!=0;
                    const bool motion_changed=std::memcmp(&motion[i],&before_motion[i],16)!=0;
                    if(x>=region||y>=region){outside_changed+=written||covered||motion_changed;continue;}
                    covered_any[n]|=covered;
                    parity_mismatch+=writes_rt2?covered!=written:written; // off: RT2 is never written
                    if(!routes&&motion_changed)++motion_mismatch;
                    const auto& m=model[y*region+x];
                    if(!m.classified)continue;
                    const bool pass=m.pass[which];
                    const bool owned_value=written&&std::fabs(double(now[0])-z)<depth_tolerance&&now[1]==-1.f&&std::fabs(double(now[2])-1.)<=1e-6&&now[3]==1.f;
                    if(written){max_depth_error=std::max(max_depth_error,std::fabs(double(now[0])-z));max_w_error=std::max(max_w_error,std::fabs(double(now[2])-1.));}
                    passes[which]+=covered;owned[which]+=owned_value;
                    const bool ok=covered==pass&&(pass&&writes_rt2?owned_value:!written)&&(pass&&routes?true:!motion_changed);
                    if(!ok){if(model_mismatch<8)std::printf("FADE_CUTOUT_MODEL_DIFF frame=%llu quad=%u x=%u y=%u pass=%u covered=%u written=%u rt2=%.9g,%.9g,%.9g,%.9g\n",f.frame,which,x,y,unsigned(pass),unsigned(covered),unsigned(written),now[0],now[1],now[2],now[3]);++model_mismatch;}
                    if(pass&&routes){
                        const double u=(x-f.jx)/f.W+.5/f.W,v=(y-f.jy)/f.H+.5/f.H;
                        const bool own=matched?(std::fabs(motion[i]-u)*f.W<.01&&std::fabs(motion[i+1]-v)*f.H<.01&&std::fabs(motion[i+2]-z)<depth_tolerance&&motion[i+3]==1)
                                              :before_motion[i+3]==-1&&motion[i]==0&&motion[i+1]==0&&motion[i+2]==0&&motion[i+3]==-1;
                        if(!own){if(motion_mismatch<8)std::printf("FADE_CUTOUT_PIXEL_DIFF frame=%llu quad=%u x=%u y=%u matched=%u motion=%.9g,%.9g,%.9g,%.9g\n",f.frame,which,x,y,unsigned(matched),motion[i],motion[i+1],motion[i+2],motion[i+3]);++motion_mismatch;}
                    }
                }
                objects[which].recorded=routes;
            }
            const auto color=scene(),motion=read(1),rt2=read(2);
            // Frame end: a classified pixel holds the last passing draw's depth (owner) or its pre-draw value (the fill,
            // .r -1); any RT2 change of the frame lies on a pixel some draw covered in colour (owned RT2 texels are a
            // subset of the colour coverage).
            unsigned final_panel=0,final_hull=0,final_fill=0,final_mismatch=0,subset_violations=0;
            for(unsigned y=0;y<region;++y)for(unsigned x=0;x<region;++x){
                const std::size_t n=std::size_t(y)*f.W+x;
                if(std::memcmp(&rt2[n*4],&frame_rt2_before[n*4],16)!=0&&!covered_any[n])++subset_violations;
                const auto& m=model[y*region+x];
                if(!m.classified)continue;
                const double want=owner&&m.final_z>0?m.final_z:-1.;const double got=rt2[n*4];
                const bool ok=want<0?got==-1.:std::fabs(got-want)<depth_tolerance;final_mismatch+=!ok;
                final_panel+=ok&&want==.3;final_hull+=ok&&want==.5;final_fill+=ok&&want<0;
            }
            for(const auto& m:model)classified+=m.classified;
            write("color",color);write("motion",motion);write("rt2",rt2);
            std::printf("FADE_CUTOUT frame=%llu script=%s variant=%s owner=%u jx=%.6f jy=%.6f classified=%u panel_pass=%u hull_pass=%u panel_owned=%u hull_owned=%u final_panel=%u final_hull=%u final_fill=%u"
                        " model_mismatch=%u final_mismatch=%u parity_mismatch=%u subset_violations=%u outside_changed=%u motion_mismatch=%u max_depth_error=%.9g max_w_error=%.9g"
                        " panel_routed=%u panel_tested=%u panel_refused=%u panel_gate4=%u hull_routed=%u hull_tested=%u fade_routed=%u fade_tested=%u fade_refused=%u fade_owner_masked=%u\n",
                        f.frame,script_setting,variant_setting[0]?variant_setting:"base",unsigned(owner),f.jx,f.jy,classified,passes[0],passes[1],owned[0],owned[1],final_panel,final_hull,final_fill,
                        model_mismatch,final_mismatch,parity_mismatch,subset_violations,outside_changed,motion_mismatch,max_depth_error,max_w_error,
                        panel_routed,panel_tested,panel_refused,panel_gate4,hull_routed,hull_tested,f.emission_status(f.d.p,50),f.emission_status(f.d.p,86),f.emission_status(f.d.p,51),f.emission_status(f.d.p,84));
            require(model_mismatch==0u&&final_mismatch==0u,"fade cutout: every classified pixel matches the engine model (alpha and Z tests in draw order), per draw and at frame end");
            require(parity_mismatch==0u&&subset_violations==0u&&outside_changed==0u&&motion_mismatch==0u,"fade cutout: RT2 written exactly where the colour is covered (owner) or never (off); own RT1 rows; nothing outside the quads");
            require(!owner||(max_depth_error<depth_tolerance&&max_w_error<=1e-6),"fade cutout: owner pixels hold z/w, -1, w, 1");
            require(panel_routed==unsigned(owner)&&panel_tested==unsigned(owner)&&panel_refused==0u&&panel_gate4==unsigned(!owner)&&hull_routed==1u&&hull_tested==0u,"fade cutout: the arm admits the alpha-tested panel with the owner only (off: gate 4, uncounted); the hull routes");
            require(f.emission_status(f.d.p,50)==1u+unsigned(owner)&&f.emission_status(f.d.p,86)==unsigned(owner)&&f.emission_status(f.d.p,51)==0u&&f.emission_status(f.d.p,84)==0u,"fade cutout: frame counters fade_routed / fade_tested / fade_refused / fade_owner_masked");
            f.emission_reference_color=color;
            f.boundary();
            api(f.d->EndScene(),"fade cutout EndScene");f.write_presented(f.color_image());api(f.d->SetDepthStencilSurface(f.depth.p),"fade cutout depth restore");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"fade cutout Present");++f.frame;++f.frames_since_reset;
        }
        api(f.d->SetIndices(nullptr),"fade cutout final indices release");api(f.d->SetStreamSource(0,nullptr,0,0),"fade cutout final stream release");
        std::printf("FADE_CUTOUT_CHECKS frames=%u script=%s owner=%u quads=2\n",frames,script_setting,unsigned(owner));
        return;
    }
    for(unsigned plan=0;plan<frames;++plan) {
        const bool routed_plan=routed_frame(plan);
        f.frame_begin();f.linear_material_inputs();f.write_reserved();
        if(over_sentinel){const RECT lower{0,LONG(f.H/2),LONG(f.W),LONG(f.H)};api(f.d->SetScissorRect(&lower),"fade route A below the quads");}
        // frame_begin's scene states leave SCISSORTESTENABLE off, so the rectangle alone does not keep A out of the upper half
        // (the option-off scripts keep that, their pinned behaviour); the owner cases and hull enable it: their quads really
        // sit over the sentinel fill.
        if(over_sentinel&&(owner||hull_script||scissor))api(f.d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE),"fade route A scissored");
        f.draw(f.a,0,0,0,true,true,f.a.recorded,Alter::None,false);
        const unsigned required=f.emission_status(f.d.p,16);
        require(required==(original_script?0u:2u),"fade route: the fade producer is the required one (none under original shading)");
        unsigned covered=0,own_motion=0,mask_set=0,preserved=1,alpha_kept=1;
        unsigned rt2_own=0,rt2_kept=0,rt2_outside_changed=0;double rt2_max_depth_error=0,rt2_max_w_error=0;
        for(unsigned which=0;which<2;++which) {
            f.scope(&objects[which]);bind(which,plan);
            const auto before=scene(),before_motion=read(1),before_depth=overlay_script||hull_script||owner?read(2):std::vector<float>();
            const auto state=f.snapshot();
            // overlay: the arm's decision is counted as overlay_routed/overlay_refused (54/55), never as a fade draw
            // (with the owner the hull pairs are fade pairs: 50/51; hull without it is refused by the overlay arm: 55).
            const bool overlay_arm=(overlay_script||hull_script)&&!owner;
            const unsigned routed_status=overlay_arm?54u:50u,refused_status=overlay_arm?55u:51u;
            const unsigned prepared_before=f.emission_status(f.d.p,4),routed_before=f.emission_status(f.d.p,routed_status),refused_before=f.emission_status(f.d.p,refused_status);
            api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2),"fade route actual original DIP");++f.draw_index;
            f.compare(state,f.snapshot(),"fade route complete draw restoration");
            const auto after=scene(),motion=read(1),mask=original_script?std::vector<float>():read(3); // no composition, no M under original shading
            const unsigned prepared=f.emission_status(f.d.p,4)-prepared_before,routed_delta=f.emission_status(f.d.p,routed_status)-routed_before,refused_delta=f.emission_status(f.d.p,refused_status)-refused_before;
            require(routed_delta==unsigned(routed_plan)&&refused_delta==unsigned(!routed_plan)&&prepared==(original_script?0u:unsigned(!routed_plan)),"fade route arm decision matches the script (no bracket preparation under original shading)");
            if(overlay_arm)require(read(2)==before_depth,"fade route overlay draw masks RT2: the depth target is unchanged");
            if(owner){
                // Owner: the routed quad's interior holds its own depth (z/w .3; lane: .g -1, .b = w, .a = 1); a refused quad
                // leaves RT2 as it was; nothing outside the quad's own raster changes.
                const auto depth=read(2);
                for(unsigned y=0;y<f.H;++y)for(unsigned x=0;x<f.W;++x){
                    const std::size_t n=std::size_t(y)*f.W+x;const float* now=&depth[n*rt2_lanes];const float* was=&before_depth[n*rt2_lanes];
                    const bool changed=std::memcmp(now,was,rt2_lanes*4)!=0;
                    if(interior(which,x,y)){
                        if(routed_plan&&!owner_masked){
                            const double e=std::fabs(double(now[0])-.3),we=lane?std::fabs(double(now[2])-quad_w):0.;
                            rt2_max_depth_error=std::max(rt2_max_depth_error,e);rt2_max_w_error=std::max(rt2_max_w_error,we);
                            const bool ok=e<4e-6&&(!lane||(now[1]==-1.f&&we<=1e-6*quad_w&&now[3]==1.f));rt2_own+=ok;
                            if(!ok)std::printf("FADE_ROUTE_RT2_DIFF frame=%llu quad=%u x=%u y=%u rt2=%.9g,%.9g,%.9g,%.9g before=%.9g\n",f.frame,which,x,y,now[0],lane?now[1]:0.f,lane?now[2]:0.f,lane?now[3]:0.f,was[0]);
                        } else rt2_kept+=!changed;
                    } else if(!near_quad(which,x,y))rt2_outside_changed+=changed;
                }
            }
            const bool matched=routed_plan&&objects[which].recorded;
            for(unsigned y=0;y<f.H;++y)for(unsigned x=0;x<f.W;++x) {
                const unsigned n=y*f.W+x,i=4*n;
                const bool changed=std::memcmp(&before[i],&after[i],12)!=0;covered+=changed;
                if(after[i+3]!=before[i+3])alpha_kept=0;
                if(!routed_plan&&std::memcmp(&motion[i],&before_motion[i],16))preserved=0;
                if(!interior(which,x,y))continue;
                if(!original_script&&(mask[i]>0||mask[i+1]>0||mask[i+2]>0))++mask_set;
                if(routed_plan) {
                    const double u=(x-f.jx)/f.W+.5/f.W,v=(y-f.jy)/f.H+.5/f.H;
                    // Unmatched (mode 0, alpha -1 under SRCALPHA/INVSRCALPHA: -src + 2 dst): over the fill sentinel it stays the
                    // sentinel; over A's matched rows (alpha 1) it leaves the rejected alpha 3, which the resolve treats as current-only.
                    const bool own=matched?(std::fabs(motion[i]-u)*f.W<.01&&std::fabs(motion[i+1]-v)*f.H<.01&&std::fabs(motion[i+2]-.3f)<4e-6&&motion[i+3]==1)
                                   :before_motion[i+3]==-1?(motion[i]==0&&motion[i+1]==0&&motion[i+2]==0&&motion[i+3]==-1):motion[i+3]==3;
                    own_motion+=own;
                    if(!own)std::printf("FADE_ROUTE_PIXEL_DIFF frame=%llu quad=%u x=%u y=%u matched=%u before_alpha=%.9g motion=%.9g,%.9g,%.9g,%.9g\n",f.frame,which,x,y,matched,before_motion[i+3],motion[i],motion[i+1],motion[i+2],motion[i+3]);
                    require_quiet(own,"fade route routed quad writes its own RT1 rows (alpha 1; unmatched: the sentinel over the fill, alpha 3 over A's rows)");
                }
            }
            if(which==0)for(unsigned s=0;s<2;++s){const unsigned x=s?20:12,y=x,i=(y*f.W+x)*4;std::printf("FADE_ROUTE_SAMPLE frame=%llu x=%u y=%u before=%.17g,%.17g,%.17g,%.17g after=%.17g,%.17g,%.17g,%.17g\n",f.frame,x,y,before[i],before[i+1],before[i+2],before[i+3],after[i],after[i+1],after[i+2],after[i+3]);}
            objects[which].recorded=routed_plan; // the row history keeps one frame: a refused frame loses the match
        }
        const auto color=scene(),motion=read(1),mask=original_script?std::vector<float>():read(3);
        write("color",color);write("motion",motion);if(!original_script)write("mask",mask);
        if(owner){
            // Over the sentinel fill the upper half outside the quads keeps the depth sentinel (.r -1; the lane fill's other
            // lanes are not all -1, .g is 0, and outside_changed already covers every lane).
            unsigned sentinel_other=0,sentinel_checked=0;float sentinel_sample=-1.f;
            if(over_sentinel){const auto depth=read(2);sentinel_sample=depth[(2*std::size_t(f.W)+2)*rt2_lanes];for(unsigned y=0;y<30;++y)for(unsigned x=0;x<f.W;++x){if(near_quad(0,x,y)||near_quad(1,x,y))continue;++sentinel_checked;
                const float r=depth[(std::size_t(y)*f.W+x)*rt2_lanes];
                if(r!=-1.f){if(!sentinel_other)std::printf("FADE_ROUTE_RT2_SENTINEL_DIFF frame=%llu x=%u y=%u value=%.9g\n",f.frame,x,y,r);++sentinel_other;}}}
            std::printf("FADE_ROUTE_RT2 frame=%llu script=%s owner=1 lane=%u routed=%u own=%u kept=%u outside_changed=%u sentinel_checked=%u sentinel_other=%u max_depth_error=%.9g max_w_error=%.9g masked=%u sentinel_sample=%.9g\n",
                        f.frame,script_setting,unsigned(lane),unsigned(routed_plan),rt2_own,rt2_kept,rt2_outside_changed,sentinel_checked,sentinel_other,rt2_max_depth_error,rt2_max_w_error,f.emission_status(f.d.p,84),sentinel_sample);
            const bool writes=routed_plan&&!owner_masked;
            require(rt2_own==(writes?2u*14u*14u:0u)&&rt2_kept==(writes?0u:2u*14u*14u),"fade route owner: a routed quad writes its exact depth into RT2, a refused (or lane-masked) quad leaves it");
            require(f.emission_status(f.d.p,84)==(owner_masked&&routed_plan?2u:0u),"fade route owner: fade_owner_masked counts exactly the lane-masked routed quads");
            require(rt2_outside_changed==0&&sentinel_other==0,"fade route owner: RT2 changes only under the routed quads; the sentinel elsewhere");
        }
        require(alpha_kept,"fade route RGB-masked draws keep the destination alpha");
        if(!routed_plan&&!original_script)require(preserved,"fade route bracketed draws preserve RT1");
        require(mask_set==((routed_plan||original_script)?0u:2u*14u*14u),"fade route M covers exactly the bracketed quads (no M under original shading)");
        require(f.emission_status(f.d.p,53)==(held_frame(plan)?2u:0u),"fade route hysteresis holds exactly the script's held frames");
        // The raw scene after the quads is the reference resolve's current image
        // in every script. original: no composition, so the reference keeps the
        // plain depth-sentinel reactive policy (emissions stay disabled, no M,
        // mask_valid 0).
        f.emission_reference_color=color;
        if(!original_script){f.emission_reference_mask=mask;f.emissions_enabled=true;f.emission_mask_valid=f.emission_status(f.d.p,1)!=0;}
        std::printf("FADE_ROUTE frame=%llu script=%s routed=%u matched=%u fade_routed=%u fade_refused=%u fade_held=%u prepared=%u covered=%u own_motion=%u mask_set=%u mask_valid=%u threshold=%u draws=2 overlay_routed=%u overlay_refused=%u\n",
                    f.frame,script_setting,routed_plan,routed_plan&&plan>0&&routed_frame(plan-1),f.emission_status(f.d.p,50),f.emission_status(f.d.p,51),f.emission_status(f.d.p,53),f.emission_status(f.d.p,4),covered,own_motion,mask_set,f.emission_mask_valid,f.emission_status(f.d.p,52),f.emission_status(f.d.p,54),f.emission_status(f.d.p,55));
        f.boundary();
        api(f.d->EndScene(),"fade route EndScene");f.write_presented(f.color_image());api(f.d->SetDepthStencilSurface(f.depth.p),"fade route depth restore");api(f.d->Present(nullptr,nullptr,nullptr,nullptr),"fade route Present");++f.frame;++f.frames_since_reset;
    }
    api(f.d->SetIndices(nullptr),"fade route final indices release");api(f.d->SetStreamSource(0,nullptr,0,0),"fade route final stream release");
    std::printf("FADE_ROUTE_CHECKS frames=%u script=%s quads=2\n",frames,script_setting);
}
