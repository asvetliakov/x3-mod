// Thin-region cases of temporal_pass_fixture.cpp (lattice mode); docs/architecture/taa-lattice-crawl.md section 13.
// Included after temporal_far_inc.h: uses LineConfig / line_model (the 2-D oracle with the thin-region gate), FarRun,
// MaskCreationFault, EdgeScene, Snapshot, Fault.
//
// Scene "arm": horizontal shards 0.8 px tall, pitch 2.37 px, value 1 at depth 0.99, x in [2, 11), over the depth
// sentinel (value 0.25, motion alpha -1, policy 2 with the identity camera): under the 8-phase jitter their coverage
// toggles against the sentinel, a fringe wider than the 3x3 clip box, so the clip snaps the history to the current
// phase. A plain 8x8 square (depth 0.98) at x in [21, 29) is the ordinary silhouette: no 7-tap line through any of its
// pixels changes depth class twice, the grown region ends at x = 19, and it must stay bit-identical.
//
// Scene "pan" (section 32.1; thinPanX != 0): the same shards spanning the whole width (rows are uniform along x, so a fixed
// pixel sees the same content every frame and the ripple metric needs no tracking) while the camera translates by thinPanX
// px/frame along x (clip_to_previous; the sentinel background follows it under policy 2) and the shards, routed, move with
// it: screen speed thinPanX, camera-relative speed 0. Content enters from the left border, so a pixel at x holds at most
// x / thinPanX frames of history; the metrics use x in [18, 28). Optionally a bright independent mover (value 4, depth 0.5,
// 6 px square in rows [8, 14)) crosses the field at thinPatchV px/frame from frame thinPatchFrom, and a 6x6 patch of value 4
// is written into the pass's history after frame thinInjectFrame (the stale-history witness of section 15 / 32).
//
// Scene "forward" (section 32.3; thinForward): the camera flies FORWARD by forwardDz view units per frame with no rotation, so
// clip_to_previous (camera_far_plane_reprojection) is the identity and the sentinel background stands still, while static
// geometry shows parallax in proportion to 1 / view z. The 32 px window is an off-axis crop of that flight (projection m20 =
// -100: the radial expansion field is uniform across it to 1 %, along +x), so full-width shard rows at TWO depths - rows
// above y = 16 at 0.99 (view z 600), below at 0.995 (view z 1199) - move at two uniform speeds (0.6 and 0.30 px/frame, both
// past HI), routed, and a fixed pixel still sees the same content every frame. c8 comes from camera_depth_parallax() on the
// two CameraStates (thinForwardParallax = false withholds it: the rotation-only path of section 32.1). thinForwardMover: a
// routed 2x2 object at (6..7, 9..10), depth 0.99, claiming 2 px/frame more than the static geometry at its depth.
bool thinForward=false,thinForwardParallax=true,thinForwardMover=false;
constexpr float forwardDepth2=.995f,forwardM22=1.000003f,forwardM32=-6.0000184f,forwardM20=-100;constexpr double forwardSpeed=.6;
x3m::renderer::CameraState forward_camera(double z){x3m::renderer::CameraState c;c.valid=true;c.m00=c.m11=1;c.m20=forwardM20;c.m22=forwardM22;c.m32=forwardM32;c.r[0]=c.r[4]=c.r[8]=1;c.t[0]=1234567;c.t[1]=-654321;c.t[2]=float(z);return c;}
double forward_view_z(float depth){return double(forwardM32)/(double(depth)-double(forwardM22));}
// Camera advance per frame that gives the 0.99 rows forwardSpeed px/frame at the window centre: v = -m20 (S / 2) e / (1 + e), e = dz / z.
double forward_dz(){const double q=forwardSpeed/(-double(forwardM20)*EdgeScene::S*.5);return forward_view_z(lineDepth)*q/(1-q);}
// Analytic previous position (double, full unprojection and reprojection) of pixel centre (x, y) at `depth`: displacement in px, +x right / +y down.
void forward_oracle(double x,double y,float depth,double& dx,double& dy){constexpr double S=EdgeScene::S;const double nx=2*(x+.5)/S-1,ny=1-2*(y+.5)/S,z=forward_view_z(depth),zp=z+forward_dz();
    const double vx=(nx-double(forwardM20))*z,vy=ny*z,px_=vx/zp+double(forwardM20),py_=vy/zp;dx=(px_-nx)*S*.5;dy=-(py_-ny)*S*.5;}
double forward_velocity(float depth){double dx,dy;forward_oracle(EdgeScene::S*.5-.5,EdgeScene::S*.5-.5,depth,dx,dy);return -dx;} // content speed along +x
//
// Scene "flight" (section 32.4; thinFlight): the general form of "forward" for the mask-only cases. The camera yaws by
// flight.yaw rad/frame about world Y while advancing (dz) and sliding (dx) in world units per frame; projection m00 = m11 = 1,
// m20 = flight.m20. The two row bands carry flight.rowDepth and the routed velocities flight.rowV (the analytic camera path at
// the window centre unless a case claims otherwise). The TRUE depth law of the scene is forwardM22 / forwardM32; the
// CameraStates handed to the production helpers carry flight.latchM22 / latchM32 (a wrong latch = another view's near plane).
// flight.lane: the pass receives an A32B32G32R32F current depth (r = g = device depth, b = a = view z from the true law, -1
// on the sentinel), as the four-channel sun-shadow lane supplies it; flight.laneHole: columns x < laneHole carry b = -1 on
// valid depth as well (a MIXED frame: the mask must keep those pixels on the far plane, never on the c8 law). flight.withhold: neither c8 nor c9 (rotation-only path).
// flight.glass (section 32.5): a routed quad over the whole shard field drawn UNDER the rows that writes motion alpha 1 and the
// velocity of static geometry at the near band's distance but the depth SENTINEL (blended glass: no depth write), so every cell
// between the rows is a routed sentinel pixel whose speed against the far plane is the whole parallax (forwardSpeed > HI). It must
// cast no vote. flight.mover: a routed VALID-depth 2x2 object at (6..7, 9..10) moving 2 px/frame against the static geometry.
// flight_mask() is the CPU oracle of the published gates (b camera, a screen) from the last frame's depth and motion targets:
// full double unprojection / reprojection with the exact inverse rotation, the shader's max(w, 1e-6) clamp, 8-bit openness,
// 17x17 minimum, 11x11 FRAGMENTED maximum, clamped addressing.
struct Flight{double yaw=0,dz=0,dx=0;float m20=0;double pos[3]={1234,-654,5000};float latchM22=forwardM22,latchM32=forwardM32;bool lane=false,withhold=false,glass=false,mover=false;int laneHole=0;float rowDepth[2]={lineDepth,forwardDepth2};double rowV[2][2]={{0,0},{0,0}};};
bool thinFlight=false;Flight flight;
struct FlightLane{Com<IDirect3DTexture9> texture;Com<IDirect3DSurface9> surface;Com<IDirect3DPixelShader9> convert;};FlightLane* flightLane=nullptr;
x3m::renderer::CameraState flight_camera(double n){x3m::renderer::CameraState c;c.valid=true;c.m00=c.m11=1;c.m20=flight.m20;c.m22=flight.latchM22;c.m32=flight.latchM32;const double a=flight.yaw*n,co=std::cos(a),si=std::sin(a);
    const double R[9]={co,0,si,0,1,0,-si,0,co},p[3]={flight.pos[0]+flight.dx*n,flight.pos[1],flight.pos[2]+flight.dz*n};for(unsigned i=0;i<9;++i)c.r[i]=float(R[i]);
    for(unsigned j=0;j<3;++j){double t=0;for(unsigned i=0;i<3;++i)t-=p[i]*R[i*3+j];c.t[j]=float(t);}return c;}
// Previous clip (X, Y, W) over the current view z of NDC (nx, ny): at view z > 0, or at infinity (z <= 0: the sentinel).
void flight_previous(const x3m::renderer::CameraState& now,const x3m::renderer::CameraState& before,double nx,double ny,double z,double out[3]){
    const double m[9]={now.r[0],now.r[1],now.r[2],now.r[3],now.r[4],now.r[5],now.r[6],now.r[7],now.r[8]},det=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
    const double inv[9]={(m[4]*m[8]-m[5]*m[7])/det,(m[2]*m[7]-m[1]*m[8])/det,(m[1]*m[5]-m[2]*m[4])/det,(m[5]*m[6]-m[3]*m[8])/det,(m[0]*m[8]-m[2]*m[6])/det,(m[2]*m[3]-m[0]*m[5])/det,(m[3]*m[7]-m[4]*m[6])/det,(m[1]*m[6]-m[0]*m[7])/det,(m[0]*m[4]-m[1]*m[3])/det};
    const bool finite=z>0;const double scale=finite?z:1,dir[3]={(nx-double(now.m20))/double(now.m00)*scale,(ny-double(now.m21))/double(now.m11)*scale,scale};double world[3],P[3];
    for(unsigned k=0;k<3;++k){world[k]=0;for(unsigned i=0;i<3;++i)world[k]+=(dir[i]-(finite?double(now.t[i]):0.))*inv[i*3+k];}
    for(unsigned j=0;j<3;++j){P[j]=finite?double(before.t[j]):0.;for(unsigned k=0;k<3;++k)P[j]+=world[k]*double(before.r[k*3+j]);}
    out[0]=(P[0]*double(before.m00)+P[2]*double(before.m20))/scale;out[1]=(P[1]*double(before.m11)+P[2]*double(before.m21))/scale;out[2]=P[2]/scale;}
// Content velocity (px/frame, +x right / +y down) of static geometry at `depth` at the window centre, frames 30 -> 31.
void flight_velocity(float depth,double v[2]){constexpr double S=EdgeScene::S;double c[3];flight_previous(flight_camera(31),flight_camera(30),0,0,forward_view_z(depth),c);v[0]=-(c[0]/c[2])*S*.5;v[1]=(c[1]/c[2])*S*.5;}
// camera / screen: the screen-gate chain's composition (11x11 grow, 17x17 minimum); ownCamera / ownScreen: the per-pixel gates the
// camera mask's tests draw writes (a, r), which the camera gate (A') reads. residual: the largest |routed - camera path| over
// routed valid-depth pixels, px.
struct FlightMask{std::vector<float> camera,screen,ownCamera,ownScreen;double residual=0;};
FlightMask flight_mask(const std::vector<float>& depth,const std::vector<float>& motion,unsigned n){constexpr int S=int(EdgeScene::S);const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
    const auto now=flight_camera(n),before=flight_camera(double(n)-1);std::vector<float> cam(S*S),scr(S*S);FlightMask out;out.camera.resize(S*S);out.screen.resize(S*S);
    auto open=[&](double speed){const double o=1-(speed-double(farLo))/(double(farHi)-double(farLo));return o==o?quantise8(o):0.f;};
    for(int y=0;y<S;++y)for(int x=0;x<S;++x){const float d=px(depth,UINT(x),UINT(y));const bool valid=d>=0&&d<=1,path=valid||d<=-.5f,routed=px(motion,UINT(x),UINT(y),3)==1;const double u=(x+.5)/S,v=(y+.5)/S;double cu=u,cv=v;
        if(path){double c[3];flight_previous(now,before,2*(x-jx)/S-1,1-2*(y-jy)/S,valid&&!(flight.lane&&x<flight.laneHole)?forward_view_z(d):0,c);const double w=std::max(c[2],1e-6);cu=c[0]/w*.5+.5+(.5+jx)/S;cv=-c[1]/w*.5+.5+(.5+jy)/S;}
        const double pu=routed?double(px(motion,UINT(x),UINT(y),0))+jx/S:cu,pv=routed?double(px(motion,UINT(x),UINT(y),1))+jy/S:cv,relative=std::hypot((pu-cu)*S,(pv-cv)*S),screenSpeed=std::hypot((pu-u)*S,(pv-v)*S);
        // Section 32.5, intent: a routed pixel on the sentinel casts NO VOTE (1) when its correspondence is finite and reads CLOSED (0) when it is not.
        const float vote=!routed?1.f:valid?open(relative):std::isfinite(screenSpeed)?1.f:0.f;
        scr[y*S+x]=open(screenSpeed);cam[y*S+x]=path?std::max(scr[y*S+x],vote):scr[y*S+x];if(routed&&valid&&!flight.mover&&!(flight.lane&&x<flight.laneHole))out.residual=std::max(out.residual,relative);}
    out.ownCamera=cam;out.ownScreen=scr;
    for(int y=0;y<S;++y)for(int x=0;x<S;++x){bool any=false;float c=1,q=1;for(int dy=-8;dy<=8;++dy)for(int dx=-8;dx<=8;++dx){const int tx=std::min(std::max(x+dx,0),S-1),ty=std::min(std::max(y+dy,0),S-1);
            any=any||(std::abs(dx)<=5&&std::abs(dy)<=5&&fragmented(depth,tx,ty));c=std::min(c,cam[ty*S+tx]);q=std::min(q,scr[ty*S+tx]);}
        out.camera[y*S+x]=any?quantise8(c):0;out.screen[y*S+x]=any?quantise8(q):0;}
    return out;}
constexpr unsigned thinFrames=128,thinAnalysed=32;
double thinDrift=0;unsigned thinMoveFrom=~0u;
double thinPanX=0,thinPatchV=0;unsigned thinPatchFrom=~0u,thinInjectFrame=~0u;constexpr float patchDepth=.5f,patchValue=4;
// thinBadTap: one routed pixel of value 65504 (above the resolve's finite limit 65000) at (23, 12) of the pan scene, inside the
// injected patch. thinBadMotion != 0: a routed 2x2 object on the static arm with that x velocity (1e30: the speed overflows to a non-finite value
// inside the mask program; NaN: a NaN correspondence). thinK: luminance k.
bool thinBadTap=false;double thinBadMotion=0;float thinK=0;
// A' rows (temporal_region_hold_inc.h): thinPanStop, the frame from which the pan scene's camera and shards stand still;
// armPanX, the arm scene (shards and square) carried by a camera pan of that many px/frame (the box-open fraction row).
unsigned thinPanStop=~0u;double armPanX=0;
double pan_x_at(unsigned n){return n>=thinPanStop?0.:thinPanX;}
// Section 32.5: thinPatchGlass / thinBadGlass give the pan scene's mover / the bad-motion object the depth SENTINEL (routed blended
// glass: motion alpha 1, no depth write), drawn over the shards.
bool thinPatchGlass=false,thinBadGlass=false;
// Scene "sentinel" (docs/architecture/temporal-integration.md "Distant unrouted stations under a pan"; thinSentinel): no depth
// at all under the facets - full-width sub-pixel rows (0.8 px, pitch 2.37, value 0.75: the run214 station detail maximum)
// drawn COLOUR-ONLY over the sentinel, i.e. blended without depth and unrouted (motion alpha -1), jittered like geometry; the
// camera pans by thinPanX along x on the far-plane path and the rows, uniform along x, follow it by construction.
// sentinelFacets = false leaves the flat sentinel. sentinelProps (static camera): a plain 8x8 geometry square at (21..28,
// 4..11) and an 8x8 routed GLASS quad (depth sentinel, motion alpha 1) at (21..28, 20..27). sentinelMover != 0: a routed 2x2
// object at (6..7, 13..14) standing still on screen while claiming that x velocity, i.e. moving against the camera path.
// sentinelBarFrom: a colour-only bar of value 4, 8 px wide, rows [4, 28), crossing at 6 px/frame from that frame (a laser).
// thinCutFrame: camera_cut on that frame. thinFailRows: the row-target creation of the separable box fails on frame 1.
// sentinelBadBlock: a colour-only 3x3 block of 65504 (non-finite for the resolve) at (14..16, 14..16) and a static colour-only
// bar of value 4 at x in [18, 20): the block's centre has no finite tap in its inner 3x3 and a finite emitter within 3 px.
bool sentinelBadBlock=false;
// Scene "emissive" (docs/architecture/thin-glow-lines.md 8.3 R3; thinEmissive): a ROUTED dark hull (value 0.16 = the run231
// hull luma, depth 0.99) filling rows [0, 20) over the depth sentinel, with a 1.4 px bright strip (value 3 = the run231 strip
// peak) at x in [6.1, 7.5) on it: under the 8-phase jitter column 6 is bright on every phase and column 7 on 3 of the 8, the
// period-2 toggle of a strip about one pixel wide. Beside it a 16x16 UNIFORMLY LIT panel of the same value 3 at x in [14, 30),
// y in [2, 18) - its interior is no local peak - and, on the sentinel below the hull, an UNROUTED colour-only bright patch
// (value 3, motion alpha -1) at x in [12, 20), y in [26, 32). No 7-tap line anywhere in the window changes depth class twice,
// so FRAGMENTED is 0 on every pixel and the mask's b carries the emissive vote alone; with E = 0 the region is empty and every
// target is the plain resolve's bit for bit.
// thinEmissiveBad: the non-finite variant of the same hull. A CONTROL pixel of value 3 at (4, 10) (which must vote), a 65504 pixel
// (above the resolve's finite limit 65000) at (30, 10) on the bare hull, and a 15x15 uniformly lit panel of value 3 at x in [9, 24),
// y in [3, 18) with a NaN pixel at its centre (16, 10). Only the panel's boundary ring is a local peak (its 3x3 reaches the hull),
// so after the 11x11 grow the panel's own core is exactly the NaN's 3x3, columns 15-17 x rows 9-11, which no vote can reach from
// outside. That is what makes the row discriminating: the NaN's eight neighbours have luma 3 > E and a finite 3x3 minimum of 3, so
// they must NOT vote - they would if a non-finite tap counted as a low minimum (max(NaN, 0) folded to 0) - and the 65504 pixel,
// 7 px clear of the panel ring, would vote for itself without the limit.
bool thinEmissive=false,thinEmissiveBad=false;constexpr float emissiveHull=.16f,emissiveValue=3;
bool thinSentinel=false,sentinelFacets=true,sentinelProps=false,thinFailRows=false;double sentinelMover=0;unsigned sentinelBarFrom=~0u,thinCutFrame=~0u;
// Fade owner (X3M_FADE_RT2_OWNER; docs/architecture/fade-rt2-ownership.md section 4), sentinel scene: sentinelOwnerFrom != ~0u adds a far
// routed 8x8 square (value 0.6, depth ownerDepth, x in [6, 14), y in [12, 20)) drawn as the route's masked fade-band row (noDepth: RT1
// alpha 1, RT2 keeps the sentinel) before that frame and as an RT2 owner (its depth written) from it: one sentinel -> valid class change.
unsigned sentinelOwnerFrom=~0u;constexpr float ownerDepth=.999f;
constexpr float sentinelFacetValue=.75f,sentinelBarValue=4;constexpr double sentinelBarV=6;
constexpr int injectRect[4]={20,9,26,15};
std::vector<EdgeObject> thin_objects(unsigned n){std::vector<EdgeObject> o;constexpr double S=EdgeScene::S;
    if(thinEmissive){o.push_back({0,0,S,20,emissiveHull,lineDepth,0,0});
        if(thinEmissiveBad){o.push_back({4,10,5,11,emissiveValue,lineDepth,0,0});o.push_back({30,10,31,11,65504.f,lineDepth,0,0});
            o.push_back({9,3,24,18,emissiveValue,lineDepth,0,0});o.push_back({16,10,17,11,NAN,lineDepth,0,0});return o;}
        o.push_back({6.1,0,7.5,20,emissiveValue,lineDepth,0,0});
        o.push_back({14,2,30,18,emissiveValue,lineDepth,0,0});o.push_back({12,26,20,32,emissiveValue,-1.f,0,0,false,0,true});return o;}
    if(thinSentinel){const double offset=!cameraPanVertical?0:cameraPanAlternates?(n%2?cameraPanSpeed:0):cameraPanY*n; // the facets follow the vertical camera pan
        if(sentinelFacets)for(double top=std::fmod(2.31+offset,2.37)-2.37;top<S;top+=2.37)o.push_back({0,top,S,top+.8,sentinelFacetValue,-1.f,0,0,false,0,true});
        if(sentinelBadBlock){o.push_back({14,14,17,17,65504.f,-1.f,0,0,false,0,true});o.push_back({18,10,20,22,sentinelBarValue,-1.f,0,0,false,0,true});}
        if(sentinelProps){o.push_back({21,4,29,12,1,squareDepth,0,0});o.push_back({21,20,29,28,.6f,-1.f,0,0});}
        if(sentinelOwnerFrom!=~0u){EdgeObject owner{6,12,14,20,.6f,ownerDepth,0,0};owner.noDepth=n<sentinelOwnerFrom;o.push_back(owner);}
        if(sentinelMover!=0)o.push_back({6,13,8,15,1,lineDepth,sentinelMover,0});
        if(sentinelBarFrom!=~0u&&n>=sentinelBarFrom){const double l=-8+sentinelBarV*(n-sentinelBarFrom);if(l<S&&l+8>0)o.push_back({l,4,l+8,28,sentinelBarValue,-1.f,0,0,false,0,true});}
        return o;}
    if(thinFlight){if(flight.glass)o.push_back({0,2,S,30,.25f,-1.f,flight.rowV[0][0],flight.rowV[0][1]});
        for(double top=2.31;top<30;top+=2.37)if(top>=2&&!(top<16&&top+.8>15.5)){const bool lower=top>=16;o.push_back({0,top,S,top+.8,1,flight.rowDepth[lower],flight.rowV[lower][0],flight.rowV[lower][1]});}
        if(flight.mover)o.push_back({6,9,8,11,1,flight.rowDepth[0],flight.rowV[0][0]+2,flight.rowV[0][1]});
        return o;}
    if(thinForward){const double v[2]={forward_velocity(lineDepth),forward_velocity(forwardDepth2)};
        for(double top=2.31;top<30;top+=2.37)if(top>=2&&!(top<16&&top+.8>15.5)){const bool lower=top>=16;o.push_back({0,top,S,top+.8,1,lower?forwardDepth2:lineDepth,v[lower],0});}
        if(thinForwardMover)o.push_back({6,9,8,11,1,lineDepth,v[0]+2,0});
        return o;}
    if(thinPanX!=0){for(double top=2.31;top<30;top+=2.37)if(top>=2)o.push_back({0,top,S,top+.8,1,lineDepth,pan_x_at(n),0});
        if(thinBadTap)o.push_back({23,12,24,13,65504.f,lineDepth,thinPanX,0});
        if(thinPatchFrom!=~0u&&n>=thinPatchFrom){const double l=-8+thinPatchV*(n-thinPatchFrom);if(l<S&&l+6>0)o.push_back({l,8,l+6,14,patchValue,thinPatchGlass?-1.f:patchDepth,thinPatchV,0});}
        return o;}
    const double moved=n>thinMoveFrom?thinDrift*(n-thinMoveFrom):thinMoveFrom==~0u?thinDrift*n:0,phase=std::fmod(2.31+moved,2.37);
    const double shift=armPanX*n;
    for(double top=phase;top<30;top+=2.37)if(top>=2)o.push_back({2+shift,top,11+shift,top+.8,1,lineDepth,armPanX,n>thinMoveFrom||thinMoveFrom==~0u?thinDrift:0});
    o.push_back({21+shift,12,29+shift,20,1,squareDepth,armPanX,0});if(thinBadMotion!=0)o.push_back({6,13,8,15,1,thinBadGlass?-1.f:lineDepth,thinBadMotion,0});return o;}
double thin_velocity(double nearest){return nearest==double(lineDepth)?thinDrift:nearest==1.?cameraPanY:0;} // nearest 1: the sentinel, on the camera path
double thin_velocity_x(double nearest){return nearest==double(patchDepth)?thinPatchV:cameraPanX;}
// Fails the creation of A16B16G16R16F render-target textures (the box targets of the camera gate) while alive.
struct BoxCreationFault {
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
    static inline Create original=nullptr;static inline unsigned refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){
        if(format==D3DFMT_A16B16G16R16F&&(usage&D3DUSAGE_RENDERTARGET)){++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}return original(d,w,h,levels,usage,format,pool,out,shared);}
    explicit BoxCreationFault(IDirect3DDevice9* d):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[23],sizeof original);auto fn=&hook;std::memcpy(&table[23],&fn,sizeof fn);refused=0;*reinterpret_cast<void***>(d)=table;}
    ~BoxCreationFault(){*reinterpret_cast<void***>(device)=previous;}
};
// The camera mask's tests draw (line_mask_ps.hlsl c7.z = 0, the only camera-gate mask draw since the dilated chain went) on the
// last frame of a run, from the scene hooks: r = screen openness, a = camera openness (max of it and the camera-relative openness
// on routed valid depth; 1 elsewhere on the camera path: no vote), b = the flag / class code. Scenes on the far-plane camera path
// only (the pan, arm and sentinel scenes), not the forward / flight cameras.
void hold_tests_cpu(const FarRun& run,UINT x,UINT y,float out[3]){
    const float d=px(run.depth.back(),x,y),alpha=px(run.motion.back(),x,y,3);const bool valid=d>=0&&d<=1,sentinel=d<=-.5f,routed=alpha==1.f;
    const double vx=valid?line_velocity_x(d):cameraPanX,vy=valid?line_velocity(d):cameraPanY;
    auto open=[](double speed){return quantise8(1-(speed-double(farLo))/(double(farHi)-double(farLo)));};
    const float screen=open(std::hypot(vx,vy));
    out[0]=screen;out[1]=valid&&routed?std::max(screen,open(std::hypot(vx-cameraPanX,vy-cameraPanY))):(valid||sentinel)?1.f:screen;
    out[2]=quantise8((fragmented(run.depth.back(),int(x),int(y))?254./255:0)+(sentinel&&alpha==-1.f?1./255:0));}
double hold_tests_error(const FarRun& run){constexpr UINT S=EdgeScene::S;double e=0;for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){float t[3];hold_tests_cpu(run,x,y,t);
    e=std::max({e,std::fabs(double(px(run.mask.back(),x,y,0))-t[0]),std::fabs(double(px(run.mask.back(),x,y,3))-t[1]),std::fabs(double(px(run.mask.back(),x,y,2))-t[2])});}return e;}
// A pixel of the last frame that is routed geometry (motion alpha 1, valid depth): where the camera-relative vote is cast.
bool routed_geometry(const FarRun& run,UINT x,UINT y){const float d=px(run.depth.back(),x,y);return d>=0&&d<=1&&px(run.motion.back(),x,y,3)==1.f;}
// Refuses A16B16G16R16F render-target textures after the first `allowed` while alive: after a Reset the run creates its two
// FP16 colour histories first, then the box pair, so allowed = 2 refuses the box pair alone.
struct LateBoxFault {
    using Create=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*);
    static inline Create original=nullptr;static inline unsigned allowed=0,refused=0;
    void** previous;void* table[119];IDirect3DDevice9* device;
    static HRESULT WINAPI hook(IDirect3DDevice9* d,UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared){
        if(format==D3DFMT_A16B16G16R16F&&(usage&D3DUSAGE_RENDERTARGET)){if(allowed)--allowed;else{++refused;if(out)*out=nullptr;return D3DERR_OUTOFVIDEOMEMORY;}}
        return original(d,w,h,levels,usage,format,pool,out,shared);}
    LateBoxFault(IDirect3DDevice9* d,unsigned allow):previous(*reinterpret_cast<void***>(d)),device(d){std::copy(previous,previous+119,table);std::memcpy(&original,&table[23],sizeof original);auto fn=&hook;std::memcpy(&table[23],&fn,sizeof fn);allowed=allow;refused=0;*reinterpret_cast<void***>(d)=table;}
    ~LateBoxFault(){*reinterpret_cast<void***>(device)=previous;}
};

// failBoxes: the first camera-gate run (frame 0; the two FP16 colour histories are allowed, the box pair refused) meets a
// box-target creation failure; the pass turns the thin region off for the session (no fallback program set).
FarRun thin_sequence(EdgeScene& s,const DWORD* resolver,const LineConfig& c,unsigned frames,bool failMasks=false,bool failBoxes=false){
    TemporalPass pass;check("thin initialize",thinFlight&&flight.lane?pass.initialize(s.d,nullptr,resolver,nullptr,nullptr,reinterpret_cast<const DWORD*>(x3m::renderer::hdr_writeback_program())):pass.initialize(s.d,nullptr,resolver));const bool on=c.thinW>0||c.farW>0;constexpr UINT S=EdgeScene::S;
    if(on){check("thin configure",pass.configure_far());if(c.sentS>0){require(!pass.sentinel_available(),"separable box programs are not created by configure_far");check("thin configure sentinel",pass.configure_sentinel());}require(pass.far_available(),"thin-region program created on this device");if(c.camera)require(pass.camera_gate_available(),"camera-gate programs created on this device");
        if(c.camera&&c.sentS>0)require(pass.sentinel_available(),"separable box twins created on this device");}
    const FlickerConfig f{c.name,0,0,.1f,.5f,false,.9f};FarRun run;bool sequence=true;
    for(unsigned n=0;n<frames;++n){const unsigned index=n%latticePhases+1;const double jx=halton(index,2)-.5,jy=halton(index,3)-.5;
        s.render(thin_objects(n),sentinelBackground,jx,jy);run.current.push_back(s.read(s.color.p));run.depth.push_back(s.read(s.depth32.p));if(thinSentinel||thinEmissive||c.camera)run.motion.push_back(s.read(s.motion.p));
        auto in=flicker_inputs(s,f,jx,jy,true);in.sentinel_strength=thinFailRows&&n==0?0.f:c.sentS;in.sentinel_emitter=c.sentE;in.camera_cut=n==thinCutFrame;in.thin_region_weight=c.thinW;in.thin_region_relax=c.relax;in.far_weight=c.farW;in.far_d0=farD0;in.far_inv=farInv;in.far_speed_lo=farLo;in.far_speed_hi=farHi;
        in.luminance_k=thinK;in.thin_region_emissive=c.emisE;in.thin_region_camera_gate=c.camera;{const double vx=cameraPanAlternates&&!cameraPanVertical?camera_pan_at(n):thinPanX!=0?pan_x_at(n):armPanX,vy=!cameraPanVertical?0:cameraPanAlternates?camera_pan_at(n):cameraPanY;
            if(vx!=0){in.clip_to_previous[3]=float(-2*vx/S);}
            if(vy!=0){in.clip_to_previous[7]=float(2*vy/S);}} // content moved down by vy px: previous clip y = y + 2 vy / S // previous clip x = x - 2 pan / S: content moved right by pan px
        if(thinForward){const auto now=forward_camera(-5000-forward_dz()*n),before=forward_camera(-5000-forward_dz()*(double(n)-1)); // view z of a world point falls by dz per frame: t_z = -camera z
            require(x3m::renderer::camera_far_plane_reprojection(now,before,in.clip_to_previous),"forward flight: far-plane matrix");
            if(thinForwardParallax)require(x3m::renderer::camera_depth_parallax(now,before,in.camera_depth_parallax),"forward flight: depth parallax term");}
        if(thinFlight){const auto now=flight_camera(n),before=flight_camera(double(n)-1);require(x3m::renderer::camera_far_plane_reprojection(now,before,in.clip_to_previous),"flight: far-plane matrix");
            if(!flight.withhold){require(x3m::renderer::camera_depth_parallax(now,before,in.camera_depth_parallax),"flight: depth parallax term");require(x3m::renderer::camera_lane_parallax(now,before,in.camera_lane_parallax),"flight: lane parallax term");}
            if(flight.lane){require(flightLane!=nullptr,"flight: lane target");s.target(flightLane->surface.p);check("flight lane Begin",s.d->BeginScene());check("flight lane PS",s.d->SetPixelShader(flightLane->convert.p));s.constant(forwardM22,forwardM32,float(flight.laneHole)/S,0);
                check("flight lane source",s.d->SetTexture(0,s.depth32.p));check("flight lane min",s.d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));check("flight lane mag",s.d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
                s.quad(0,0,S,S,0,0);check("flight lane End",s.d->EndScene());check("flight lane unbind",s.d->SetTexture(0,nullptr));s.target(s.colorSurface.p);in.current_depth=flightLane->texture.p;}}
        Output out;check("thin Begin resolve",s.d->BeginScene());
        if(failMasks&&n==0){MaskCreationFault fault(s.d);check(c.name,pass.run(in,&out));}
        else if(thinFailRows&&n==1){BoxCreationFault fault(s.d);check(c.name,pass.run(in,&out));require(BoxCreationFault::refused>0&&pass.sentinel_failed()&&!pass.camera_gate_failed(),"row-target creation fault reached; the sentinel stabiliser is off for the session, the camera gate carries on");}
        else if(failBoxes&&n==0){LateBoxFault fault(s.d,2);check(c.name,pass.run(in,&out));require(LateBoxFault::refused>0&&pass.camera_gate_failed(),"box-target creation fault reached; thin region off");}
        else check(c.name,pass.run(in,&out));
        check("thin End resolve",s.d->EndScene());
        sequence=sequence&&out.color&&pass.diagnostics().history_valid&&out.used_history==(n>0&&n!=thinCutFrame);
        // The camera gate (A'): the published mask is the tests target, recorded every frame for the oracle (line_model); the hold ran unless the box targets failed.
        if(c.camera)sequence=sequence&&pass.diagnostics().region_hold==!pass.camera_gate_failed();
        run.output.push_back(s.read(out.color));if(out.age)run.age.push_back(s.read(out.age));if(out.stabiliser_mask&&(c.camera||n+1==frames))run.mask.push_back(s.read(out.stabiliser_mask));
        if(n==thinInjectFrame){ // stale history: the bright patch written into the history the next frame reads (the oracle injects the same values)
            Com<IDirect3DSurface9> level;check("thin inject level",out.color->GetSurfaceLevel(0,&level.p));s.target(level.p);check("thin inject Begin",s.d->BeginScene());check("thin inject flat",s.d->SetPixelShader(s.flat.p));
            s.constant(patchValue,patchValue,patchValue,1);s.quad(injectRect[0],injectRect[1],injectRect[2],injectRect[3],0,0);check("thin inject End",s.d->EndScene());s.target(s.colorSurface.p);}}
    run.masksFailed=pass.line_masks_failed();require(sequence,"thin-region history follows the sequence");return run;}
// Temporal ripple of the shard region / peak-to-peak over the last jitter cycles, columns [x0, x1).
void thin_ripple(const FarRun& r,double& rms,double& p2p,UINT x0=4,UINT x1=9){double sum=0;unsigned count=0;p2p=0;const unsigned N=unsigned(r.output.size());
    for(UINT y=5;y<27;++y)for(UINT x=x0;x<x1;++x){double lo=1e9,hi=-1e9,mean=0;for(unsigned n=N-thinAnalysed;n<N;++n){const double v=px(r.output[n],x,y);lo=std::min(lo,v);hi=std::max(hi,v);mean+=v/thinAnalysed;}
        for(unsigned n=N-thinAnalysed;n<N;++n){const double e=px(r.output[n],x,y)-mean;sum+=e*e;++count;}p2p=std::max(p2p,hi-lo);}
    rms=std::sqrt(sum/count);}
// ---- sentinel stabiliser (docs/architecture/temporal-integration.md "Distant unrouted stations under a pan"), the seven rows of its design ----
void sentinel_stabiliser_cases(EdgeScene& s,const DWORD* resolver,const LineConfig& camera97){
    constexpr UINT S=EdgeScene::S;IDirect3DDevice9* const d=s.d;
    LineConfig off=camera97,on=camera97,unbound=camera97;off.name="sentinel-0-emitter-0";off.sentS=0;off.sentE=0;on.name="sentinel-0.7";on.sentS=.7f;unbound.name="sentinel-0.7-emitter-0";unbound.sentS=.7f;unbound.sentE=0;
    auto same_mask=[&](const FarRun& a,const FarRun& b){return !a.mask.empty()&&a.mask==b.mask;};
    auto same_pixel=[&](const FarRun& a,const FarRun& b,unsigned n,UINT x,UINT y){return std::memcmp(&a.output[n][(y*S+x)*4],&b.output[n][(y*S+x)*4],4*sizeof(float))==0&&(a.age.empty()||a.age[n][(y*S+x)*4]==b.age[n][(y*S+x)*4]);};
    // refusals, hostile state, a failed columns draw, Reset
    {thinSentinel=true;s.render(thin_objects(0),sentinelBackground,0,0);Output out;const FlickerConfig none{"sentinel-validation",0,0,.1f,.5f,false,.9f};
        TemporalPass pass;check("sentinel initialize",pass.initialize(d,nullptr,resolver));check("sentinel configure",pass.configure_far());
        auto in=flicker_inputs(s,none,0,0,true);in.caller_scene_open=false;in.thin_region_weight=.97f;in.thin_region_camera_gate=true;
        for(float bad:{-.1f,1.5f,NAN}){in.sentinel_strength=bad;require(pass.run(in,&out)==E_INVALIDARG,"sentinel strength outside [0, 1] is refused");}in.sentinel_strength=.7f;
        require(pass.run(in,&out)==E_INVALIDARG,"sentinel strength without configure_sentinel is refused");check("sentinel configure sentinel",pass.configure_sentinel());check("sentinel configure is idempotent",pass.configure_sentinel());require(pass.sentinel_available(),"separable box programs created by configure_sentinel");
        for(float bad:{-1.f,NAN}){in.sentinel_emitter=bad;require(pass.run(in,&out)==E_INVALIDARG,"negative or non-finite emitter bound is refused while S > 0");in.sentinel_strength=0;require(SUCCEEDED(pass.run(in,&out)),"the emitter bound is not validated at S = 0");in.sentinel_strength=.7f;}in.sentinel_emitter=1;
        in.thin_region_camera_gate=false;in.sentinel_strength=NAN;require(SUCCEEDED(pass.run(in,&out)),"sentinel fields are ignored without the camera gate");in.thin_region_camera_gate=true;in.sentinel_strength=.7f;
        require(SUCCEEDED(pass.run(in,&out))&&out.stabiliser_mask,"sentinel run publishes the mask");
        const float junk[4]={9,8,7,6};for(UINT r:{0u,4u,6u,22u,23u,24u})check("sentinel hostile constant",d->SetPixelShaderConstantF(r,junk,1));
        for(UINT slot:{1u,2u,3u,6u,9u,10u}){check("sentinel hostile texture",d->SetTexture(slot,s.wave.p));}
        check("sentinel hostile CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,0));
        {Snapshot before(d);check("sentinel hostile run",pass.run(in,&out));before.equals(d,"sentinel run restores c0..c7, c22..c24, samplers 1..3, 6, 8..10, RT1 and COLORWRITEENABLE1");}
        {Output failed;{Fault fault(d,3);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!pass.diagnostics().history_valid,"failed columns draw of the separable box (the third draw: tests, rows, columns, resolve) publishes nothing");}
            check("sentinel recovery",pass.run(in,&out));require(out.color&&!out.used_history,"after a failed box draw the sentinel run restarts without history");}
        pass.before_reset();pass.after_reset(S_OK);check("sentinel after Reset",pass.run(in,&out));require(out.color&&!out.used_history&&out.stabiliser_mask&&pass.sentinel_available()&&!pass.sentinel_failed(),"Reset protocol keeps the separable box programs and recreates the row targets");
        for(UINT slot:{1u,2u,3u,4u,6u,8u,9u,10u}){check("sentinel unbind",d->SetTexture(slot,nullptr));}
        ++state_checks;s.target(s.colorSurface.p);thinSentinel=false;}
    // (1) S = 0 and E = 0 against the camera-gate run (S = 0, E at its default): the arm scene at rest and the pan scene
    for(double pan:{0.,.5}){thinPanX=cameraPanX=pan;const auto a=thin_sequence(s,resolver,camera97,48),b=thin_sequence(s,resolver,off,48);const bool same=same_rgb(a.output,b.output)&&same_rgb(a.age,b.age)&&same_mask(a,b);
        std::printf("SENTINEL_STABILISER row=off pan=%.2f colour_identical=%u age_identical=%u mask_identical=%u\n",pan,unsigned(same_rgb(a.output,b.output)),unsigned(same_rgb(a.age,b.age)),unsigned(same_mask(a,b)));
        ++numeric_checks;require(same,"sentinel stabiliser off (S = 0, E = 0): colour, age and mask bit-identical to the camera-gate run");}
    thinPanX=cameraPanX=0;thinSentinel=true;
    // (2) sentinel sub-pixel facets under a pan on the far-plane path: oracle, mask, ripple against S = 0
    // The facets vary along y, so only a VERTICAL pan moves content across them (run214's pan). Flicker = rms, codes, of the
    // output against the motion-compensated earlier output showing the same content at the same pixel phase: lag 1 at rest,
    // lag 10 / 3 px for the steady 0.3 px/frame pan, lag 2 for a pan that reverses every frame (same speed, the history stays
    // inside the 32 px frame), lag 1 / pan px for the steady integer pans. A steady pan of p px/frame leaves a pixel at row y
    // at most y / p frames of history (content enters at the top border): the steady 2 and 4 px/frame rows are measured on
    // rows [20, 28) (history 10..14 and 5..7 frames) and reported, not asserted; the reversing rows carry those speeds.
    struct PanCase{const char* mode;double pan;bool vertical,alternating;unsigned lag;int shift;UINT y0;bool asserted;};
    const PanCase pans[]={{"rest",0,false,false,1,0,8,true},{"steady",.3,true,false,10,3,12,true},{"reversing",.3,true,true,2,0,8,true},{"reversing",2,true,true,2,0,8,true},{"reversing",4,true,true,2,0,8,true},{"steady",2,true,false,1,2,20,false},{"steady",4,true,false,1,4,20,false}};
    for(const PanCase& pc:pans){cameraPanVertical=pc.vertical;cameraPanAlternates=pc.alternating;cameraPanSpeed=pc.alternating?pc.pan:0;cameraPanY=pc.alternating?0:pc.pan;oracleSkipCeiling=pc.pan<1?0:unsigned(std::ceil(pc.pan)+1)*(S-6)*thinFrames; // about pan + 1 border rows per frame
        const auto baseRun=thin_sequence(s,resolver,camera97,thinFrames),run=thin_sequence(s,resolver,on,thinFrames);const auto model=line_model(run,on,&run.mask);const unsigned skipped=oracleSkipped;
        auto flicker=[&](const FarRun& r){double sum=0;unsigned count=0;for(unsigned n=thinFrames-thinAnalysed;n<thinFrames;++n)for(UINT y=pc.y0;y<28;++y)for(UINT x=4;x<28;++x){const double e=double(px(r.output[n],x,y))-double(px(r.output[n-pc.lag],x,UINT(int(y)-pc.shift)));sum+=e*e;++count;}return std::sqrt(sum/count);};
        auto detail=[&](const FarRun& r){double sum=0;unsigned count=0;for(unsigned n=thinFrames-thinAnalysed;n<thinFrames;++n)for(UINT y=pc.y0;y<28;++y)for(UINT x=4;x<28;++x){const double e=double(px(r.output[n],x,y+1))-double(px(r.output[n],x,y));sum+=e*e;++count;}return std::sqrt(sum/count);};
        double oracle=0,ageOracle=0,maskError=0;const double baseRms=flicker(baseRun),rms=flicker(run);
        for(unsigned n=0;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){oracle=std::max(oracle,double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));ageOracle=std::max(ageOracle,double(std::fabs(px(run.age[n],x,y)-model.age[n][y*S+x])));}
        maskError=hold_tests_error(run);
        std::printf("SENTINEL_STABILISER row=facets pan_axis=%s pan=%.2f pan_mode=%s ratio_asserted=%u strength=%.2f oracle_error=%.6f age_oracle_error=%.6f mask_error=%.6f oracle_skipped_px=%u oracle_skip_ceiling=%u base_flicker_codes=%.3f flicker_codes=%.3f flicker_ratio=%.4f base_detail_codes=%.3f detail_codes=%.3f\n",pc.vertical?"y":"none",pc.pan,pc.mode,unsigned(pc.asserted),double(on.sentS),oracle,ageOracle,maskError,skipped,oracleSkipCeiling,255*baseRms,255*rms,rms/baseRms,255*detail(baseRun),255*detail(run));
        metric("sentinel stabiliser: shader matches the 2-D CPU oracle within the FP16 bound",oracle,0,.0006/(1-.97));metric("sentinel stabiliser: age target matches the CPU oracle",ageOracle,0,0);
        metric("sentinel stabiliser: the published tests target equals the CPU tests draw (the class code on the unrouted sky)",maskError,0,.5/255);
        if(pc.asserted){++numeric_checks;require(rms<.6*baseRms,"sentinel facets: flicker below 0.6 x the S = 0 run under this pan");}}
    cameraPanVertical=cameraPanAlternates=false;cameraPanSpeed=cameraPanY=0;oracleSkipCeiling=0;
    // (3) geometry silhouette and (6) routed sentinel pixels (glass), camera at rest: bit-identical to S = 0 on every frame
    {sentinelProps=true;const auto baseRun=thin_sequence(s,resolver,camera97,64),run=thin_sequence(s,resolver,on,64);unsigned squareDiffers=0,glassDiffers=0,skyDiffers=0,glassMaskDiffers=0;bool classes=true;
        for(unsigned n=0;n<64;++n)for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const bool differs=!same_pixel(run,baseRun,n,x,y),square=x>=21&&x<29&&y>=4&&y<12,glass=x>=21&&x<29&&y>=20&&y<28;
            classes=classes&&(!square||(px(run.depth[n],x,y)>=0&&px(run.depth[n],x,y)<=1))&&(!glass||(px(run.depth[n],x,y)<=-.5f&&px(run.motion[n],x,y,3)==1.f));
            if(square)squareDiffers+=differs;else if(glass)glassDiffers+=differs;else skyDiffers+=differs;}
        for(UINT y=20;y<28;++y)for(UINT x=21;x<29;++x)glassMaskDiffers+=px(run.mask.back(),x,y,2)!=px(baseRun.mask.back(),x,y,2)||px(run.mask.back(),x,y,3)!=px(baseRun.mask.back(),x,y,3);
        std::printf("SENTINEL_STABILISER row=silhouette frames=64 square_px=64 square_differs=%u unrouted_differs=%u\n",squareDiffers,skyDiffers);
        std::printf("SENTINEL_STABILISER row=routed_sentinel frames=64 glass_px=64 glass_differs=%u glass_mask_differs=%u\n",glassDiffers,glassMaskDiffers);
        require(classes,"sentinel props: the square holds geometry and the glass is routed on the depth sentinel on every frame");numeric_checks+=2;require(squareDiffers==0&&skyDiffers>0,"geometry silhouette against the sentinel: geometry pixels bit-identical to S = 0 while the unrouted sky changes");
        require(glassDiffers==0&&glassMaskDiffers==0,"routed sentinel pixels (motion alpha 1): colour, age and strength bit-identical to S = 0");sentinelProps=false;}
    // (5) (a routed object moving against the camera path closed the stabiliser within 8 px of itself: the dilated chain's 17x17
    // minimum, removed 2026-09-24; with A' the closure is the pixel's and its nearest-depth neighbour's, held one jitter cycle)
    // (4) an emitter crossing the sentinel at 6 px/frame, camera at rest, with and without the emitter bound. Measured against the
    // S = 0 run: the 3x3 clip of the installed resolve already keeps one bright pixel behind the bar and one dark pixel inside
    // its leading edge (the 3x3 there holds both colours), so the absolute trail (trail_px_*) is reported beside what the
    // stabiliser ADDS: columns behind the bar (added_trail) and columns INSIDE the bar that keep older, darker history
    // (added_inside: the box of a pixel within 3 px of either edge still holds the background, on the leading side this frame
    // and on the trailing side the frame after). added_reach is the largest distance of any changed pixel from the nearest
    // edge of the bar: the design's bound, 3 px unbound and 1 px with the emitter bound.
    {sentinelFacets=false;sentinelBarFrom=16;const unsigned frames=16+10;unsigned trail[2]{},addedTrail[2]{},addedLead[2]{},after[2]{};double brightest[2]{},added[2]{},reach[2]{};double basePeak=0;
        const auto baseRun=thin_sequence(s,resolver,camera97,frames);
        for(unsigned n=sentinelBarFrom;n<frames;++n)for(UINT y=8;y<24;++y)for(UINT x=0;x<S;++x)if(!(px(baseRun.current[n],x,y)>.3f))basePeak=std::max(basePeak,double(px(baseRun.output[n],x,y))-.25);
        for(unsigned which=0;which<2;++which){const auto run=thin_sequence(s,resolver,which?unbound:on,frames);
            for(unsigned n=sentinelBarFrom;n<frames;++n){const double l=-8+sentinelBarV*(n-sentinelBarFrom);for(UINT y=8;y<24;++y){unsigned columns=0,behind=0,lead=0;
                for(UINT x=0;x<S;++x){const double excess=double(px(run.output[n],x,y))-.25,delta=std::fabs(double(px(run.output[n],x,y))-double(px(baseRun.output[n],x,y)));const bool bar=px(run.current[n],x,y)>.3f;
                    if(l>=S){after[which]+=excess!=0;continue;}
                    if(!bar&&excess>.01){++columns;brightest[which]=std::max(brightest[which],excess);}
                    if(delta>.01){added[which]=std::max(added[which],delta);if(bar)++lead;else ++behind;reach[which]=std::max(reach[which],bar?std::min(double(x)-l+1,l+8-double(x)):double(x)<l?l-double(x):double(x)-(l+7));}}
                trail[which]=std::max(trail[which],columns);addedTrail[which]=std::max(addedTrail[which],behind);addedLead[which]=std::max(addedLead[which],lead);}}}
        std::printf("SENTINEL_STABILISER row=emitter bar_value=%.1f px_per_frame=%.0f trail_px_bound_1=%u trail_px_unbound=%u trail_excess_s0=%.4f trail_excess_bound_1=%.4f trail_excess_unbound=%.4f added_trail_px_bound_1=%u added_trail_px_unbound=%u added_inside_px_bound_1=%u added_inside_px_unbound=%u added_reach_px_bound_1=%.0f added_reach_px_unbound=%.0f added_max_bound_1=%.4f added_max_unbound=%.4f nonzero_px_after_exit_bound_1=%u nonzero_px_after_exit_unbound=%u\n",double(sentinelBarValue),sentinelBarV,trail[0],trail[1],basePeak,brightest[0],brightest[1],addedTrail[0],addedTrail[1],addedLead[0],addedLead[1],reach[0],reach[1],added[0],added[1],after[0],after[1]);
        numeric_checks+=2;// Peak: the installed resolve already leaves basePeak one pixel behind the bar (the 3x3 variance clip there reaches
        // m1 + 1.25 sigma = 3.71 of the bar's 4). The stabiliser may add at most S of the rest up to the bar's own value and
        // its weight gain: measured +0.33 with E = 1. Unbound is LOWER than the installed resolve, not better: that pixel was
        // inside the bar a frame earlier, within 3 px of its leading edge, where the unbound box had kept the old background.
        ++numeric_checks;require(brightest[0]<=basePeak+.4&&brightest[1]<=basePeak+.4&&brightest[0]<double(sentinelBarValue)-.25&&brightest[1]<double(sentinelBarValue)-.25,"emitter trail peak: at most 0.4 above the installed resolve's and below the emitter's own value");
        require(trail[0]<=1&&trail[1]<=3&&addedTrail[0]<=1&&addedTrail[1]<=3&&reach[0]<=1&&reach[1]<=3&&addedTrail[1]>=addedTrail[0],"emitter crossing the sentinel: trail <= 1 px with E = 1, <= 3 px with E = 0, and every pixel the stabiliser changes lies within that distance of an edge of the bar");require(after[0]==0&&after[1]==0,"emitter trail is zero once the bar has left");sentinelFacets=true;sentinelBarFrom=~0u;}
    // Non-finite block beside an emitter: the centre of the block has no finite tap in its inner 3x3 while the 7x7 holds a
    // finite emitter above E. Its own sample is non-finite, so the resolve is current-only (black) there and never reads the
    // box, and every finite pixel has itself in its inner 3x3: an empty box is unreachable, and thin_box_columns_ps.hlsl
    // writes (0, 0) for it anyway. Asserted at the output: the block equals the S = 0 run and nothing leaves [0, bar value].
    {sentinelFacets=false;sentinelBadBlock=true;const auto baseRun=thin_sequence(s,resolver,camera97,32),run=thin_sequence(s,resolver,on,32);unsigned blockDiffers=0;double lowest=1e9,highest=-1e9;bool finite=true;
        for(unsigned n=0;n<32;++n)for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){const double v=px(run.output[n],x,y);finite=finite&&std::isfinite(v);lowest=std::min(lowest,v);highest=std::max(highest,v);if(x>=14&&x<17&&y>=14&&y<17)blockDiffers+=!same_pixel(run,baseRun,n,x,y)||v!=0;}
        std::printf("SENTINEL_STABILISER row=nonfinite_block frames=32 block_px=9 block_differs_or_nonzero=%u output_min=%.6f output_max=%.6f all_finite=%u\n",blockDiffers,lowest,highest,unsigned(finite));
        ++numeric_checks;require(finite&&blockDiffers==0&&lowest>=0&&highest<=double(sentinelBarValue),"non-finite block beside an emitter: block current-only as at S = 0, every output finite and within [0, emitter]");sentinelFacets=true;sentinelBadBlock=false;}
    // (7) history-invalid frame (camera cut) under the stabiliser: current-only (Reset is covered by the validation block above)
    {thinCutFrame=40;const auto run=thin_sequence(s,resolver,on,48);double cut=0,before=0;for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x){cut=std::max(cut,double(std::fabs(px(run.output[40],x,y)-px(run.current[40],x,y))));before=std::max(before,double(std::fabs(px(run.output[39],x,y)-px(run.current[39],x,y))));}
        std::printf("SENTINEL_STABILISER row=cut frame=40 output_minus_current_max=%.6f previous_frame_output_minus_current_max=%.6f\n",cut,before);
        ++numeric_checks;require(cut==0&&before>.01,"history-invalid frame under the sentinel stabiliser: the output is the current frame");thinCutFrame=~0u;}
    // row-target creation failure: the stabiliser is off for the session, the camera-gate run bit for bit, history kept
    {const auto baseRun=thin_sequence(s,resolver,camera97,32);thinFailRows=true;const auto failed=thin_sequence(s,resolver,on,32);thinFailRows=false;
        ++numeric_checks;require(same_rgb(baseRun.output,failed.output)&&same_rgb(baseRun.age,failed.age),"row-target creation failure: the camera-gate run bit for bit, history kept");}
    thinSentinel=false;
}
// ---- emissive vote in the thin region (docs/architecture/thin-glow-lines.md 8.3 R3; taa-lattice-crawl.md section 32.6) ----
// The "emissive" scene at rest over 64 frames of the 8-phase jitter, on the screen gate (the vote is in the mask's own
// fragmentation channel, so the camera gate adds nothing at rest). Rows: E = 0 against the plain resolve, then E = 1 against the
// CPU oracle, the three classes (routed strip / lit panel interior / unrouted sentinel emitter) and the strip's rest leak.
void emissive_vote_cases(EdgeScene& s,const DWORD* resolver,const LineConfig& region){
    constexpr UINT S=EdgeScene::S;constexpr unsigned frames=64,analysed=32;
    const LineConfig plain{"emissive-plain",0,0};
    LineConfig off=region,vote=region;off.name="emissive-E-0";off.emisE=0;vote.name="emissive-E-1";vote.emisE=1;
    struct Scene{Scene(){thinEmissive=true;}~Scene(){thinEmissive=false;}} scene;
    const auto plainRun=thin_sequence(s,resolver,plain,frames),offRun=thin_sequence(s,resolver,off,frames),voteRun=thin_sequence(s,resolver,vote,frames);
    double offDiff=0,offMaskB=0;
    for(unsigned n=0;n<frames;++n)for(UINT i=0;i<S*S*4;++i)offDiff=std::max(offDiff,double(std::fabs(offRun.output[n][i]-plainRun.output[n][i])));
    for(UINT i=0;i<S*S;++i)offMaskB=std::max(offMaskB,double(offRun.mask[0][i*4+2]));
    double maskError=0;
    for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)maskError=std::max(maskError,double(std::fabs(px(voteRun.mask[0],x,y,2)-
        quantise8(thin_region_strength(voteRun.depth.back(),int(x),int(y),vote.camera,&voteRun.motion.back(),vote.sentS,&voteRun.current.back(),vote.emisE)))));
    double stripB=1,panelB=0,skyB=0;
    for(UINT y=2;y<18;++y)for(UINT x=6;x<8;++x)stripB=std::min(stripB,double(px(voteRun.mask[0],x,y,2)));
    for(UINT y=8;y<12;++y)for(UINT x=20;x<24;++x)panelB=std::max(panelB,double(px(voteRun.mask[0],x,y,2)));
    for(UINT y=26;y<32;++y)for(UINT x=12;x<20;++x)skyB=std::max(skyB,double(px(voteRun.mask[0],x,y,2)));
    // Rest leak: rms of the frame-to-frame step of the resolved image on the toggling strip column (7), last 32 frames.
    auto leak=[&](const FarRun& r){double sum=0;unsigned count=0;for(unsigned n=frames-analysed;n<frames;++n)for(UINT y=2;y<18;++y){
        const double e=double(px(r.output[n],7,y))-double(px(r.output[n-1],7,y));sum+=e*e;++count;}return std::sqrt(sum/count);};
    const double offLeak=leak(offRun),voteLeak=leak(voteRun);
    std::printf("THIN_REGION_EMISSIVE frames=%u hull=%.2f strip=%.1f weight=%.2f e0_vs_plain_max_diff=%.9f e0_mask_b_max=%.6f e1_mask_oracle_error=%.6f e1_strip_b_min=%.4f e1_panel_core_b_max=%.4f e1_unrouted_sentinel_b_max=%.4f e0_strip_delta_codes=%.4f e1_strip_delta_codes=%.4f delta_ratio=%.4f\n",
        frames,double(emissiveHull),double(emissiveValue),double(vote.thinW),offDiff,offMaskB,maskError,stripB,panelB,skyB,255*offLeak,255*voteLeak,offLeak/std::max(voteLeak,1e-12));
    ++numeric_checks;require(offDiff==0&&offMaskB==0,"emissive vote at E = 0: colour, alpha and mask bit-identical to the plain resolve (no pixel of this scene is fragmented)");
    metric("emissive vote at E = 1: published strength equals the CPU oracle's (vote, 11x11 grow, 17x17 speed gate)",maskError,0,.5/255);
    ++numeric_checks;require(stripB==1,"emissive vote: the routed strip carries the full thin-region strength on every row");
    ++numeric_checks;require(panelB==0&&skyB==0,"emissive vote: a uniformly lit panel's interior and an unrouted sentinel emitter of the same luma cast no vote");
    ++numeric_checks;require(voteLeak>0&&offLeak>=2.5*voteLeak,"emissive vote: the strip's frame-to-frame leak at rest falls at least 2.5 x (IIR prediction 3.4 x for 0.9 -> 0.97)");
    // Non-finite taps: the NaN inside the lit panel and the 65504 pixel on the hull must not vote, and neither may the NaN's eight
    // neighbours, whose own luma is above E and whose finite 3x3 minimum is their own. The control pixel proves the scene still
    // votes, and the CPU oracle applies the same finite rule, so any disagreement over which pixels the shader admitted shows up
    // as oracle error over the whole window.
    {thinEmissiveBad=true;const auto bad=thin_sequence(s,resolver,vote,16);double control=0,nanB=0,overflowB=0,badOracle=0;
        for(UINT y=0;y<S;++y)for(UINT x=0;x<S;++x)badOracle=std::max(badOracle,double(std::fabs(px(bad.mask[0],x,y,2)-
            quantise8(thin_region_strength(bad.depth.back(),int(x),int(y),vote.camera,&bad.motion.back(),vote.sentS,&bad.current.back(),vote.emisE)))));
        for(UINT y=9;y<12;++y)for(UINT x=15;x<18;++x)nanB=std::max(nanB,double(px(bad.mask[0],x,y,2)));
        for(UINT y=9;y<12;++y)for(UINT x=29;x<S;++x)overflowB=std::max(overflowB,double(px(bad.mask[0],x,y,2)));
        control=px(bad.mask[0],4,10,2);
        const float centre=px(bad.current.back(),16,10);const bool nanSeen=!(centre==centre),overflowSeen=px(bad.current.back(),30,10)>65000;
        std::printf("THIN_REGION_EMISSIVE_NONFINITE frames=16 nan_present=%u overflow_present=%u control_b=%.4f nan_and_panel_core_b_max=%.4f overflow_b_max_3x3=%.4f mask_oracle_error=%.6f\n",
            unsigned(nanSeen),unsigned(overflowSeen),control,nanB,overflowB,badOracle);
        metric("emissive vote, non-finite scene: published strength equals the CPU oracle's",badOracle,0,.5/255);
        ++numeric_checks;require(nanSeen&&overflowSeen&&control==1&&nanB==0&&overflowB==0,"emissive vote: a NaN inside a lit panel, its eight neighbours (the panel's ungrown core) and a pixel above the resolve's finite limit cast no vote, while a finite peak elsewhere does");
        thinEmissiveBad=false;}
}
void thin_region_cases(IDirect3DDevice9* d,Compiler compiler,const DWORD* resolver){
    std::puts("THIN_REGION_CASES");EdgeScene s(d,compiler);constexpr UINT S=EdgeScene::S;
    struct Defer{Defer(){deferMetrics=true;deferredFailures.clear();}~Defer(){deferMetrics=false;}} defer;
    struct Hooks{Hooks(){line_velocity=thin_velocity;line_velocity_x=thin_velocity_x;farD0=.98f;farInv=200;} // far gate for the combined config: farw 1 on the shards (0.99), 0 on the square (0.98)
        ~Hooks(){line_velocity=line_velocity_default;line_velocity_x=line_velocity_x_default;thinDrift=0;thinMoveFrom=~0u;thinBadTap=false;thinBadMotion=0;thinPatchGlass=thinBadGlass=false;thinK=0;oracleK=0;thinFlight=false;flight=Flight{};flightLane=nullptr;thinForward=thinForwardMover=false;thinForwardParallax=true;thinPanX=cameraPanX=thinPatchV=0;thinPatchFrom=thinInjectFrame=oracleInjectFrame=~0u;farD0=farInv=0;cameraPanAlternates=false;cameraPanSpeed=0;thinSentinel=sentinelProps=thinFailRows=sentinelBadBlock=thinEmissive=thinEmissiveBad=false;cameraPanVertical=false;cameraPanY=0;oracleSkipCeiling=0;sentinelFacets=true;sentinelMover=0;sentinelBarFrom=thinCutFrame=~0u;}} hooks;
    const LineConfig base{"thin-base",0,0},on97{"thin-region-0.97",0,0,0,0,.97f,1},on985{"thin-region-0.985",0,0,0,0,.985f,1},half{"thin-region-0.97-relax-0.5",0,0,0,0,.97f,.5f},weightOnly{"thin-region-0.97-relax-0",0,0,0,0,.97f,0},withFar{"thin-region-0.97+far-weight-0.985",0,0,.985f,0,.97f,1},
        camera97{"thin-region-0.97-camera-gate",0,0,0,0,.97f,1,true};
    // ---- refusals, hostile state, failed draw, Reset ----
    {s.render(thin_objects(0),sentinelBackground,0,0);Output out;const FlickerConfig none{"thin-validation",0,0,.1f,.5f,false,.9f};
        TemporalPass bare;check("thin bare initialize",bare.initialize(d,nullptr,resolver));auto in=flicker_inputs(s,none,0,0,true);in.caller_scene_open=false;in.thin_region_weight=.97f;
        require(bare.run(in,&out)==E_INVALIDARG,"thin region without configure_far is refused");
        TemporalPass pass;check("thin validation initialize",pass.initialize(d,nullptr,resolver));check("thin validation configure",pass.configure_far());check("thin validation flicker",pass.configure_flicker());
        for(float bad:{-.1f,.5f,.995f,NAN}){in.thin_region_weight=bad;require(pass.run(in,&out)==E_INVALIDARG,"thin-region weight outside {0} U [weight, 0.99] is refused");}in.thin_region_weight=.97f;
        for(float bad:{-.1f,1.5f,NAN}){in.thin_region_relax=bad;require(pass.run(in,&out)==E_INVALIDARG,"thin-region relax outside [0, 1] is refused");}in.thin_region_relax=1;
        for(float bad:{-.1f,NAN}){in.thin_region_emissive=bad;require(pass.run(in,&out)==E_INVALIDARG,"negative or non-finite emissive E is refused while the thin region is on");
            in.thin_region_weight=0;require(SUCCEEDED(pass.run(in,&out)),"the emissive vote is neither read nor validated with the thin region off");in.thin_region_weight=.97f;}in.thin_region_emissive=1;
        in.thin_clip=.75f;require(pass.run(in,&out)==E_INVALIDARG,"thin region beside the 3x3 thin clip is refused (the program has no sentinel soft clip)");in.thin_region_weight=0;in.far_weight=.985f;require(pass.run(in,&out)==E_INVALIDARG,"far stabiliser beside the 3x3 thin clip is refused");in.far_weight=0;in.thin_region_weight=.97f;in.thin_clip=0;
        in.thin_clip=.75f;in.adaptive_weight=.97f;require(pass.run(in,&out)==E_INVALIDARG,"thin region beside the adaptive weight is refused");in.thin_clip=0;in.adaptive_weight=0;
        in.motion_policy=MotionPolicy::KnownCameraOnly;in.motion=nullptr;require(pass.run(in,&out)==E_INVALIDARG,"thin region without per-pixel motion is refused");in.motion_policy=MotionPolicy::PerPixel;in.motion=s.motion.p;
        require(SUCCEEDED(pass.run(in,&out))&&out.age&&out.stabiliser_mask,"thin-region run publishes the age target and the mask");
        const float junk[4]={9,8,7,6};for(UINT r:{0u,3u,5u,6u,10u,22u,24u})check("thin hostile constant",d->SetPixelShaderConstantF(r,junk,1));
        check("thin hostile s0",d->SetTexture(0,s.wave.p));check("thin hostile s8",d->SetTexture(8,s.wave.p));check("thin hostile s4",d->SetTexture(4,s.wave.p));check("thin hostile s8 min",d->SetSamplerState(8,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check("thin hostile CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,0));
        {Snapshot before(d);check("thin hostile run",pass.run(in,&out));before.equals(d,"thin-region run restores c0..c7, c10, c22, c24, samplers 0, 4 and 8, RT1 and COLORWRITEENABLE1");}
        {Output failed;{Fault fault(d,2);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!pass.diagnostics().history_valid,"failed second mask draw publishes nothing");}
            check("thin recovery",pass.run(in,&out));require(out.color&&!out.used_history,"after a failed run the thin-region resolve restarts without history");}
        pass.before_reset();pass.after_reset(S_OK);check("thin after Reset",pass.run(in,&out));require(out.color&&!out.used_history&&out.stabiliser_mask,"Reset protocol recreates the masks and restarts the history");
        // ---- camera gate (section 32.1): refusals, hostile state, failed box draw, Reset ----
        require(pass.camera_gate_available(),"camera-gate programs created by configure_far");
        in.thin_region_camera_gate=true;
        require(bare.run(in,&out)==E_INVALIDARG,"camera gate without configure_far is refused");
        in.thin_region_weight=0;require(SUCCEEDED(pass.run(in,&out))&&!out.stabiliser_mask,"camera gate without the thin region is ignored (plain resolve)");in.thin_region_weight=.97f;
        require(SUCCEEDED(pass.run(in,&out))&&out.age&&out.stabiliser_mask,"camera-gate run publishes the age target and the mask");
        for(UINT r:{0u,4u,5u,6u,10u,22u,24u})check("thin camera hostile constant",d->SetPixelShaderConstantF(r,junk,1));
        check("thin camera hostile s9",d->SetTexture(9,s.wave.p));check("thin camera hostile s10",d->SetTexture(10,s.wave.p));check("thin camera hostile s9 min",d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));check("thin camera hostile s10 u",d->SetSamplerState(10,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));check("thin camera hostile CWE1",d->SetRenderState(D3DRS_COLORWRITEENABLE1,0));
        {Snapshot before(d);check("thin camera hostile run",pass.run(in,&out));before.equals(d,"camera-gate run restores c0..c7, c10, c22, c24, samplers 8..10, RT1 and COLORWRITEENABLE1");}
        {Output failed;{Fault fault(d,2);require(pass.run(in,&failed)==E_FAIL&&!failed.color&&!pass.diagnostics().history_valid,"failed box draw (the second draw: tests, box, resolve) publishes nothing");}
            check("thin camera recovery",pass.run(in,&out));require(out.color&&!out.used_history,"after a failed box draw the camera-gate resolve restarts without history");}
        pass.before_reset();pass.after_reset(S_OK);check("thin camera after Reset",pass.run(in,&out));require(out.color&&!out.used_history&&out.stabiliser_mask&&pass.camera_gate_available()&&!pass.camera_gate_failed(),"Reset protocol keeps the camera-gate programs and recreates the box targets");
        in.thin_region_camera_gate=false;in.thin_region_emissive=0;check("thin unbind s9",d->SetTexture(9,nullptr));check("thin unbind s10",d->SetTexture(10,nullptr));
        check("thin unbind s0",d->SetTexture(0,nullptr));check("thin unbind s8",d->SetTexture(8,nullptr));check("thin unbind s4",d->SetTexture(4,nullptr));state_checks+=2;s.target(s.colorSurface.p);}
    // ---- static shards, and drifting inside the gate (0.12 px/frame, t = 0.41) and past HI (0.30) ----
    double staticRms[2]{};
    // (The camera gate's rows are THIN_REGION_HOLD in temporal_region_hold_inc.h; its bit-identity to the screen gate with a static
    // camera held for the dilated chain only, removed 2026-09-24.)
    for(double drift:{0.,.12,.3}){thinDrift=drift;thinMoveFrom=~0u;const auto baseRun=thin_sequence(s,resolver,base,thinFrames);double baseRms=0,baseP2p=0;thin_ripple(baseRun,baseRms,baseP2p);
        for(const LineConfig* c:{&base,&on97,&on985,&half,&weightOnly,&withFar}){const auto run=c==&base?baseRun:thin_sequence(s,resolver,*c,thinFrames);const auto model=line_model(run,*c);double oracle=0,ageOracle=0,rms=0,p2p=0,maskError=0;unsigned squareDiffers=0,squarePixels=0;thin_ripple(run,rms,p2p);
            for(unsigned n=0;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){oracle=std::max(oracle,double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));
                if(!run.age.empty())ageOracle=std::max(ageOracle,double(std::fabs(px(run.age[n],x,y)-model.age[n][y*S+x])));
                if(x>=20){++squarePixels;squareDiffers+=std::memcmp(&run.output[n][(y*S+x)*4],&baseRun.output[n][(y*S+x)*4],4*sizeof(float))!=0;}}
            if(!run.mask.empty())for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){maskError=std::max(maskError,double(std::fabs(px(run.mask[0],x,y,2)-quantise8(thin_region_strength(run.depth.back(),int(x),int(y),c->camera)))));
                if(c->camera)maskError=std::max(maskError,double(std::fabs(px(run.mask[0],x,y,3)-quantise8(thin_region_strength(run.depth.back(),int(x),int(y),false)))));}
            std::printf("THIN_REGION drift=%.2f config=%s oracle_error=%.6f age_oracle_error=%.6f mask_error=%.6f shard_rms_codes=%.3f shard_p2p_codes=%.1f rms_ratio=%.4f square_px=%u square_differs=%u\n",drift,c->name,oracle,ageOracle,maskError,255*rms,255*p2p,rms/baseRms,squarePixels,squareDiffers);
            metric((std::string("thin region ")+c->name+": shader matches the 2-D CPU oracle within the FP16 bound").c_str(),oracle,0,.0006/(1-(c->thinW>0?c->thinW:.9)));
            if(c->thinW>0){metric((std::string("thin region ")+c->name+": age target matches the CPU oracle").c_str(),ageOracle,0,0);
                metric((std::string("thin region ")+c->name+": published gate equals the oracle's (fragmented 7x7, closed by the fastest pixel)").c_str(),maskError,0,.5/255);}
            ++numeric_checks;require(squarePixels>0&&squareDiffers==0,"plain silhouette (the square and everything right of x = 20) bit-identical to the plain resolve, all four channels");
            if(drift==0&&c==&on97){staticRms[0]=baseRms;staticRms[1]=rms;++numeric_checks;require(rms<=.35*baseRms&&p2p<=.5*baseP2p,"static shards: ripple <= 0.35 x and peak-to-peak <= 0.5 x the installed resolve");}
            if(drift==0&&c==&weightOnly){++numeric_checks;require(rms>staticRms[1],"the weight without the clip relaxation leaves more ripple (the clip is the cause)");}
            // (Measured 1.10 x the thin region alone: pixels whose depth toggles with the phase alternate between the two weight targets.)
            if(drift==0&&c==&withFar){++numeric_checks;require(rms<=.15*baseRms&&rms<=staticRms[1]*1.25,"far stabiliser + thin region (the expected default pair): shard ripple <= 0.15 x the installed resolve and within 1.25 x the thin region alone");}
            if(drift>=.25&&c->thinW>0){++numeric_checks;require(same_rgb(run.output,baseRun.output),"past the speed gate the thin-region run is the plain resolve bit for bit, every pixel");}
        }}
    // ---- motion starts after 64 static frames (0.4 px/frame): the gate closes at once; the stabilised history is released by the clip ----
    {thinDrift=.4;thinMoveFrom=64;const auto baseRun=thin_sequence(s,resolver,base,thinFrames),run=thin_sequence(s,resolver,on97,thinFrames);double first=0,late=0,trail=0,baseTrail=0;
        for(unsigned n=65;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x<20;++x){const double e=std::fabs(px(run.output[n],x,y)-px(baseRun.output[n],x,y));if(n==65)first=std::max(first,e);if(n>=65+24)late=std::max(late,e);
            if(px(run.depth[n],x,y)<=-.5f){trail=std::max(trail,std::fabs(double(px(run.output[n],x,y))-.25)*(n>=65+8));baseTrail=std::max(baseTrail,std::fabs(double(px(baseRun.output[n],x,y))-.25)*(n>=65+8));}}
        std::printf("THIN_REGION_MOTION_START first_frame_difference=%.6f after_24_frames=%.6f background_trail=%.6f base_background_trail=%.6f\n",first,late,trail,baseTrail);
        metric("thin region: 24 frames after motion starts the output is the installed resolve's",late,0,2./255);
        metric("thin region: from 8 frames after motion starts, uncovered background carries at most 0.04 more shard colour than under the installed resolve (ghost bound; 0 after 24 frames above)",std::max(trail-baseTrail,0.),0,.04);
        thinMoveFrom=~0u;thinDrift=0;}
    // ---- camera pan at 0.5 px/frame (section 32.1): the screen gate closes the whole field (0.5 > HI), the camera gate keeps it open ----
    // Runs: screen and camera gates, clean; then the same with the bright independent mover (frames 40..59) and the stale-history
    // patch injected after frame 100. Metrics on columns [18, 28) (>= 36 frames of history at 0.5 px/frame). The camera gate runs
    // A' (its oracle rows are THIN_REGION_HOLD_PAN); here its tests target and the stale bounds.
    {thinPanX=cameraPanX=.5;const auto baseRun=thin_sequence(s,resolver,base,thinFrames),screenRun=thin_sequence(s,resolver,on97,thinFrames),cameraRun=thin_sequence(s,resolver,camera97,thinFrames);
        double baseRms=0,baseP2p=0,screenRms=0,screenP2p=0,cameraRms=0,cameraP2p=0;thin_ripple(baseRun,baseRms,baseP2p,18,28);thin_ripple(screenRun,screenRms,screenP2p,18,28);thin_ripple(cameraRun,cameraRms,cameraP2p,18,28);
        double screenShare=0,cameraShare=0,cameraScreenChannel=0;unsigned window=0;
        for(UINT y=5;y<27;++y)for(UINT x=18;x<28;++x){++window;screenShare+=px(screenRun.mask[0],x,y,2)>0;cameraShare+=px(cameraRun.mask.back(),x,y,3)>0;cameraScreenChannel=std::max(cameraScreenChannel,double(px(cameraRun.mask.back(),x,y,0)));}
        screenShare/=window;cameraShare/=window;
        for(const auto* pair:{&screenRun}){const bool camera=false;const auto model=line_model(*pair,on97);double oracle=0,ageOracle=0,maskError=0;
            for(unsigned n=0;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){oracle=std::max(oracle,double(std::fabs(px(pair->output[n],x,y)-model.color[n][y*S+x])));ageOracle=std::max(ageOracle,double(std::fabs(px(pair->age[n],x,y)-model.age[n][y*S+x])));}
            for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){maskError=std::max(maskError,double(std::fabs(px(pair->mask[0],x,y,2)-quantise8(thin_region_strength(pair->depth.back(),int(x),int(y),camera)))));if(camera)maskError=std::max(maskError,double(std::fabs(px(pair->mask[0],x,y,3)-quantise8(thin_region_strength(pair->depth.back(),int(x),int(y),false)))));}
            std::printf("THIN_REGION_PAN pan=0.50 config=%s oracle_error=%.6f age_oracle_error=%.6f mask_error=%.6f\n",camera?camera97.name:on97.name,oracle,ageOracle,maskError);
            metric(camera?"pan, camera gate: shader matches the 2-D CPU oracle (camera-relative gate, 7x7 box) within the FP16 bound":"pan, screen gate: shader matches the 2-D CPU oracle within the FP16 bound",oracle,0,.0006/(1-.97));
            metric(camera?"pan, camera gate: age target matches the CPU oracle":"pan, screen gate: age target matches the CPU oracle",ageOracle,0,0);
            metric(camera?"pan, camera gate: published gates (b camera, a screen) equal the oracle's":"pan, screen gate: published gate equals the oracle's",maskError,0,.5/255);}
        std::printf("THIN_REGION_CAMERA pan=0.50 base_rms_codes=%.3f screen_rms_codes=%.3f camera_rms_codes=%.3f camera_over_screen=%.4f screen_p2p_codes=%.1f camera_p2p_codes=%.1f screen_gate_share=%.4f camera_gate_share=%.4f camera_mask_screen_channel_max=%.4f\n",255*baseRms,255*screenRms,255*cameraRms,cameraRms/screenRms,255*screenP2p,255*cameraP2p,screenShare,cameraShare,cameraScreenChannel);
        ++numeric_checks;require(same_rgb(screenRun.output,baseRun.output)&&screenShare==0,"pan past HI: the screen-gated thin region is the plain resolve bit for bit and its published gate is closed everywhere");
        ++numeric_checks;require(cameraShare>=.99&&cameraScreenChannel==0,"pan past HI: the camera gate's tests target reads the whole shard field open (camera openness, share >= 0.99) while its screen channel reads closed");
        ++numeric_checks;require(cameraRms<=.5*screenRms&&cameraP2p<=.6*screenP2p,"pan past HI: the camera gate halves the shard ripple (rms <= 0.5 x, peak-to-peak <= 0.6 x the screen gate)");
        // The bright mover crosses the field at 2 px/frame (camera-relative 1.5 px/frame: it closes the gate around itself in both
        // modes); after it has left (frame 60) nothing may exceed the scene's own maximum (1) by more than 2 codes in either mode.
        // The patch injected after frame 100 measures the stale-history bound at frame 101 against the clean runs.
        thinPatchV=2;thinPatchFrom=40;thinInjectFrame=oracleInjectFrame=100;std::copy(injectRect,injectRect+4,oracleInjectRect);oracleInjectValue=patchValue;
        const auto screenPatch=thin_sequence(s,resolver,on97,thinFrames),cameraPatch=thin_sequence(s,resolver,camera97,thinFrames);
        double excess[2]={0,0},added[2]={0,0},addedLate[2]={0,0},oracle[2]={0,0};
        for(unsigned which=0;which<2;++which){const auto& run=which?cameraPatch:screenPatch;const auto& clean=which?cameraRun:screenRun;const auto model=line_model(run,which?camera97:on97,&run.mask);
            for(unsigned n=61;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){if(n<100)excess[which]=std::max(excess[which],double(px(run.output[n],x,y))-1);
                if(n>100)oracle[which]=std::max(oracle[which],double(std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x])));}
            for(int y=injectRect[1]-1;y<=injectRect[3];++y)for(int x=injectRect[0]-1;x<=injectRect[2];++x){added[which]=std::max(added[which],double(px(run.output[101],UINT(x),UINT(y))-px(clean.output[101],UINT(x),UINT(y))));addedLate[which]=std::max(addedLate[which],double(px(run.output[105],UINT(x),UINT(y))-px(clean.output[105],UINT(x),UINT(y))));}}
        std::printf("THIN_REGION_STALE pan=0.50 mover_px_per_frame=2 injected_value=%.1f screen_excess_after_mover=%.6f camera_excess_after_mover=%.6f screen_added_frame101=%.6f camera_added_frame101=%.6f camera_over_screen=%.4f screen_added_frame105=%.6f camera_added_frame105=%.6f unbounded_estimate=%.4f screen_oracle_error=%.6f camera_oracle_error=%.6f\n",
            patchValue,excess[0],excess[1],added[0],added[1],added[1]/std::max(added[0],1e-9),addedLate[0],addedLate[1],.97*(patchValue-.25),oracle[0],oracle[1]);
        metric("pan + bright mover, screen gate: no ghost above the scene's maximum after the mover left",excess[0],0,2./255);
        metric("pan + bright mover, camera gate: no ghost above the scene's maximum after the mover left (the mover closes the gate around itself)",excess[1],0,2./255);
        metric("pan + stale patch, camera gate: shader matches the oracle's 7x7 box clip of the injected history (frames 101..127)",oracle[1],0,.0006/(1-.97));
        metric("pan + stale patch, screen gate: shader matches the oracle on the injected history (frames 101..127)",oracle[0],0,.0006/(1-.97));
        ++numeric_checks;require(added[0]>.1&&added[1]<=2*added[0],"stale patch (value 4) one frame after injection: the camera gate's 7x7 box bounds it to at most 2 x the screen gate's clipped value");
        ++numeric_checks;require(added[1]<.5*.97*(patchValue-.25),"stale patch: the box binds (well below the clip-off estimate 0.97 x (4 - 0.25))");
        thinPatchV=0;thinPatchFrom=~0u;
        // The box pass's own weighting and finite rule: k = 0.5 and one current pixel of 65504 (not finite for the resolve) inside
        // the injected patch. A box that kept the bad tap would not bind around it (maximum 65504); one that weighed differently
        // from the resolve would clip to the wrong bound. Both show as oracle error on frames 101..127.
        {thinBadTap=true;thinK=.5f;oracleK=.5;const auto run=thin_sequence(s,resolver,camera97,thinFrames);const auto model=line_model(run,camera97,&run.mask);double error=0,beside=0,bad=0;unsigned badPixels=0;
            for(unsigned n=101;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){const double e=std::fabs(px(run.output[n],x,y)-model.color[n][y*S+x]);error=std::max(error,e);
                if(px(run.current[n],x,y)>65000){++badPixels;bad=std::max(bad,double(std::fabs(px(run.output[n],x,y))));}
                if(n==101&&int(x)>=injectRect[0]&&int(x)<injectRect[2]&&int(y)>=injectRect[1]&&int(y)<injectRect[3]&&px(run.current[n],x,y)<=65000)beside=std::max(beside,double(px(run.output[n],x,y)));}
            std::printf("THIN_REGION_BOX_DOMAIN pan=0.50 k=0.5 bad_tap_value=65504 bad_pixel_frames=%u bad_pixel_output_max=%.6f oracle_error=%.6f patch_output_max_frame101=%.6f\n",badPixels,bad,error,beside);
            metric("pan + stale patch, k = 0.5 and a non-finite current tap: shader matches the oracle (box weighed as the resolve, bad tap ignored)",error,0,.0006/(1-.97));
            ++numeric_checks;require(badPixels>0&&bad==0&&beside<1.5,"non-finite tap: exercised, resolved black, and the stale patch beside it stays bounded by the finite 7x7 colours");
            thinBadTap=false;thinK=0;oracleK=0;}
        thinInjectFrame=oracleInjectFrame=~0u;
        // Section 32.5: the same bright mover as routed SENTINEL-depth glass (value 4, 2 px/frame over the shards). It casts no vote, so
        // the camera gate stays open around it (the valid-depth mover above closes it) and the 7x7 box is the only ghost bound. The
        // mover advances 2 px/frame against a box radius of 3: a pixel 5 px or more behind the trailing edge l (x <= l - 5) was last
        // covered 3 frames ago and its box [x - 3, x + 3] ends at l - 2, short of the mover even with the +-0.5 px jitter, so its
        // history is clipped to the scene's own maximum (1); only x in [l - 4, l - 2] may carry mover colour, bounded by the mover's value.
        {thinPatchV=2;thinPatchFrom=40;thinPatchGlass=true;const auto run=thin_sequence(s,resolver,camera97,thinFrames),present=thin_sequence(s,resolver,camera97,50);
            double after=0,behind=0,within=0,gateMin=1,screenMax=0,maskError=0;unsigned glassPixels=0;const auto motion=s.read(s.motion.p); // frame 49 of `present`: mover at x in [10, 16)
            for(unsigned n=61;n<thinFrames;++n)for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x)after=std::max(after,double(px(run.output[n],x,y))-1);
            for(unsigned n=46;n<=56;++n){const int l=-8+2*int(n-40);for(UINT y=9;y<13;++y)for(int x=3;x<=l-2;++x){const double v=double(px(run.output[n],UINT(x),y))-1;if(x<=l-5)behind=std::max(behind,v);else within=std::max(within,v);}}
            for(UINT y=3;y<23;++y)for(UINT x=3;x<25;++x){gateMin=std::min(gateMin,double(px(present.mask.back(),x,y,3)));screenMax=std::max(screenMax,double(px(present.mask.back(),x,y,0)));glassPixels+=px(motion,x,y,3)==1&&px(present.depth.back(),x,y)<=-.5f;}
            maskError=hold_tests_error(present);
            std::printf("THIN_REGION_GLASS_MOVER pan=0.50 mover_px_per_frame=2 mover_value=%.1f routed_sentinel_px=%u camera_gate_min_within_8px=%.4f screen_gate_max_within_8px=%.4f mask_error=%.6f excess_after_mover=%.6f excess_5px_behind=%.6f excess_within_box_reach=%.6f box_reach_bound=%.4f\n",
                patchValue,glassPixels,gateMin,screenMax,maskError,after,behind,within,double(patchValue)-1);
            ++numeric_checks;require(glassPixels>=24&&gateMin>=.99&&screenMax==0,"sentinel-depth fast mover: it casts no vote, the tests target's camera openness stays 1 around it while the screen channel reads closed");
            ++numeric_checks;require(maskError<=.5/255,"sentinel-depth fast mover: the published tests target equals the depth-only oracle (the mover counts as background)");
            ++numeric_checks;require(after<=2./255&&behind<=2./255,"sentinel-depth fast mover: no ghost above the scene's maximum 5 px or more behind the mover, nor after it left (7x7 box clip)");
            ++numeric_checks;require(within<=double(patchValue)-1+2./255,"sentinel-depth fast mover: within the box's reach of the trailing edge the ghost is bounded by the mover's own value");
            thinPatchV=0;thinPatchFrom=~0u;thinPatchGlass=false;}
        // Box-target creation failure on the first camera-gate frame: the thin region off for the rest of the session.
        {const auto fell=thin_sequence(s,resolver,camera97,32,false,true),plain32=thin_sequence(s,resolver,base,32);
            ++numeric_checks;require(fell.mask.empty()&&fell.age.empty()&&same_rgb(fell.output,plain32.output),"box-target creation failure: the thin region off for the session (no fallback program set), the plain resolve bit for bit, history kept");}
        thinPanX=cameraPanX=0;}
    // ---- forward flight (section 32.3): static routed shards at two depths, identity far-plane matrix, c8 from camera_depth_parallax ----
    {thinForward=true;const double v[2]={forward_velocity(lineDepth),forward_velocity(forwardDepth2)};
        // CPU residuals over the metric window, px: the routed (scene) correspondence against the analytic path, and the shader's
        // float32 form far_plane + c8.xyz * (d - c8.w) (identity far plane here) against the same analytic path.
        float c8[4];require(x3m::renderer::camera_depth_parallax(forward_camera(-5000-forward_dz()*100),forward_camera(-5000-forward_dz()*99),c8),"forward flight: c8");
        double sceneResidual[2]={0,0},formResidual[2]={0,0};
        for(unsigned band=0;band<2;++band){const float depth=band?forwardDepth2:lineDepth;for(UINT y=band?18:5;y<(band?27u:14u);++y)for(UINT x=18;x<28;++x){double dx,dy;forward_oracle(x,y,depth,dx,dy);
            sceneResidual[band]=std::max(sceneResidual[band],std::hypot(dx+v[band],dy));
            const float nx=2*(float(x)+.5f)/S-1,ny=1-2*(float(y)+.5f)/S,sd=depth-c8[3],X=nx+c8[0]*sd,Y=ny+c8[1]*sd,W=1+c8[2]*sd;
            formResidual[band]=std::max(formResidual[band],std::hypot((double(X/W)-nx)*S*.5-dx,-(double(Y/W)-ny)*S*.5-dy));}}
        const auto screenRun=thin_sequence(s,resolver,on97,thinFrames),cameraRun=thin_sequence(s,resolver,camera97,thinFrames);
        thinForwardParallax=false;const auto rotationRun=thin_sequence(s,resolver,camera97,thinFrames);thinForwardParallax=true;
        double screenRms=0,screenP2p=0,cameraRms=0,cameraP2p=0;thin_ripple(screenRun,screenRms,screenP2p,18,28);thin_ripple(cameraRun,cameraRms,cameraP2p,18,28);
        // Camera gate (A'): the tests target, per pixel. Its camera openness (a) on the window, the rotation-only gate's and the
        // screen channel (r) on the routed rows (the sentinel between them casts no vote and reads open in a; its own screen
        // speed is the far plane's, 0 here).
        double share[2]={0,0},minOpen[2]={1,1},screenShare=0,rotationShare=0,cameraScreenChannel=0;unsigned window[2]={0,0},routedPx=0;
        for(UINT y=5;y<27;++y)for(UINT x=18;x<28;++x){if(y>=14&&y<18)continue;const unsigned band=y>=18;++window[band];const double b=px(cameraRun.mask.back(),x,y,3);share[band]+=b>0;minOpen[band]=std::min(minOpen[band],b);
            screenShare+=px(screenRun.mask[0],x,y,2)>0;if(routed_geometry(rotationRun,x,y)){++routedPx;rotationShare=std::max(rotationShare,double(px(rotationRun.mask.back(),x,y,3)));}
            if(routed_geometry(cameraRun,x,y))cameraScreenChannel=std::max(cameraScreenChannel,double(px(cameraRun.mask.back(),x,y,0)));}
        share[0]/=window[0];share[1]/=window[1];screenShare/=window[0]+window[1];
        // The routed mover (2 px/frame against the static geometry at its depth) closes the gate on itself; the window 10 px away stays open.
        thinForwardMover=true;const auto moverRun=thin_sequence(s,resolver,camera97,32);thinForwardMover=false;double nearMover=0,farOpen=1;
        for(UINT y=9;y<11;++y)for(UINT x=6;x<8;++x)nearMover=std::max(nearMover,double(px(moverRun.mask.back(),x,y,3)));
        for(UINT y=5;y<27;++y)for(UINT x=18;x<28;++x)if(!(y>=14&&y<18))farOpen=std::min(farOpen,double(px(moverRun.mask.back(),x,y,3)));
        std::printf("THIN_REGION_CAMERA_FORWARD dz=%.6f speed_near=%.4f speed_far=%.4f scene_residual_near_px=%.6f scene_residual_far_px=%.6f form_residual_near_px=%.6f form_residual_far_px=%.6f screen_gate_share=%.4f rotation_only_routed_px=%u rotation_only_open_max=%.4f camera_share_near=%.4f camera_share_far=%.4f camera_min_open_near=%.4f camera_min_open_far=%.4f camera_tests_screen_channel_max=%.4f screen_rms_codes=%.3f camera_rms_codes=%.3f camera_over_screen=%.4f screen_p2p_codes=%.1f camera_p2p_codes=%.1f mover_gate_max=%.4f mover_window_min_open=%.4f rotation_identical_to_screen=%u\n",
            forward_dz(),v[0],v[1],sceneResidual[0],sceneResidual[1],formResidual[0],formResidual[1],screenShare,routedPx,rotationShare,share[0],share[1],minOpen[0],minOpen[1],cameraScreenChannel,255*screenRms,255*cameraRms,cameraRms/screenRms,255*screenP2p,255*cameraP2p,nearMover,farOpen,unsigned(same_rgb(rotationRun.output,screenRun.output)));
        ++numeric_checks;require(v[0]>farHi&&v[1]>farHi&&v[0]>1.9*v[1],"forward flight: both depths move past HI, the near rows twice as fast as the far rows");
        ++numeric_checks;require(std::max(formResidual[0],formResidual[1])<.005&&std::max(sceneResidual[0],sceneResidual[1])<.05,"forward flight: the float32 c8 form is the analytic path within 0.005 px; the static routed rows sit on it within 0.05 px");
        ++numeric_checks;require(screenShare==0&&routedPx>0&&rotationShare==0,"forward flight: the screen gate is closed everywhere and the rotation-only camera gate (no c8) on every routed row pixel");
        ++numeric_checks;require(share[0]>=.99&&share[1]>=.99&&minOpen[0]>=.9&&minOpen[1]>=.9&&cameraScreenChannel==0,"forward flight: the depth-aware camera gate is open on the static shards at both depths while the screen channel reads closed");
        ++numeric_checks;require(cameraRms<=.6*screenRms&&cameraP2p<=.7*screenP2p,"forward flight: the camera gate cuts the shard ripple (rms <= 0.6 x, peak-to-peak <= 0.7 x the screen gate)");
        ++numeric_checks;require(nearMover==0&&farOpen>=.9,"forward flight: a routed object moving against the static geometry closes the gate on itself, and the window stays open");
        thinForward=false;}
    // ---- general flight (section 32.4): rotation with translation, a wrong m22 / m32 latch, near geometry and the w clamp; R32F law and four-channel lane ----
    {thinFlight=true;FlightLane lane;flightLane=&lane;check("flight lane texture",d->CreateTexture(S,S,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&lane.texture.p,nullptr));check("flight lane surface",lane.texture->GetSurfaceLevel(0,&lane.surface.p));
        {Com<ID3DXBuffer> code;compile(compiler,"sampler2D source:register(s0);float4 law:register(c0);float4 main(float2 uv:TEXCOORD0):COLOR0{float d=tex2D(source,uv).r;float w=-1;if(d>=0&&d<=1&&uv.x>=law.z)w=law.y/(d-law.x);return float4(d,d,w,w);}","ps_3_0",&code.p);check("flight lane convert",d->CreatePixelShader(static_cast<DWORD*>(code->GetBufferPointer()),&lane.convert.p));}
        const float savedLo=farLo,savedHi=farHi;constexpr double wrongNear=106;const float wrongM22=float(2e6/(2e6-wrongNear)),wrongM32=float(-2e6/(2e6-wrongNear)*wrongNear); // another view's near plane: zn = 106 (the engine's FOV arm), zf = 2e6
        struct Case{const char* name;Flight f;float lo,hi;bool claim;double claimed[2];int expect;}; // expect: 1 open on both bands, 0 closed everywhere, -1 graded (near)
        std::vector<Case> cases;
        {Flight f;f.yaw=-2.5e-6;f.dz=forward_dz();f.m20=forwardM20;cases.push_back({"yaw+forward",f,savedLo,savedHi,false,{0,0},1});f.lane=true;cases.push_back({"yaw+forward-lane",f,savedLo,savedHi,false,{0,0},1});f.lane=false;f.withhold=true;cases.push_back({"yaw+forward-withheld",f,savedLo,savedHi,false,{0,0},0});}
        {Flight f;f.dz=forward_dz();f.m20=forwardM20;f.latchM22=wrongM22;f.latchM32=wrongM32;cases.push_back({"wrong-latch",f,savedLo,savedHi,false,{0,0},0});f.lane=true;cases.push_back({"wrong-latch-lane",f,savedLo,savedHi,false,{0,0},1});f.laneHole=6;cases.push_back({"wrong-latch-lane-mixed",f,savedLo,savedHi,false,{0,0},1});}
        // Section 32.5: routed sentinel-depth glass between the rows (expect 1: no vote, open on both bands); with a routed valid-depth
        // fast mover (expect 2: closed on the mover, open on the window). R32F law and lane. Camera gate (A'): the published
        // mask is the tests target, compared per pixel with the oracle's own gates (FlightMask::own*); the window statistics are
        // over routed row pixels (the vote), the sentinel between the rows reads open (no vote).
        {Flight f;f.dz=forward_dz();f.m20=forwardM20;f.glass=true;cases.push_back({"glass",f,savedLo,savedHi,false,{0,0},1});f.lane=true;cases.push_back({"glass-lane",f,savedLo,savedHi,false,{0,0},1});
            f.mover=true;cases.push_back({"glass-mover-lane",f,savedLo,savedHi,false,{0,0},2});f.lane=false;cases.push_back({"glass-mover",f,savedLo,savedHi,false,{0,0},2});}
        {Flight f;f.dz=12;f.dx=20;f.rowDepth[0]=f.rowDepth[1]=.5f;cases.push_back({"near",f,2,10,false,{0,0},-1});f.lane=true;cases.push_back({"near-lane",f,2,10,false,{0,0},-1});f.lane=false;f.withhold=true;cases.push_back({"near-withheld",f,2,10,false,{0,0},-1});}
        {Flight f;f.dz=-12.5;f.rowDepth[0]=f.rowDepth[1]=.5f;cases.push_back({"behind",f,2,10,true,{12,0},0});f.lane=true;cases.push_back({"behind-lane",f,2,10,true,{12,0},0});}
        double nearMean[3]={0,0,0};unsigned nearIndex=0;
        for(const auto& c:cases){flight=c.f;farLo=c.lo;farHi=c.hi;for(unsigned band=0;band<2;++band){if(c.claim){flight.rowV[band][0]=c.claimed[0];flight.rowV[band][1]=c.claimed[1];}else flight_velocity(flight.rowDepth[band],flight.rowV[band]);}
            const auto run=thin_sequence(s,resolver,camera97,32);const auto motion=s.read(s.motion.p);const auto model=flight_mask(run.depth.back(),motion,31);
            const bool modelled=!flight.withhold&&(flight.lane||flight.latchM32==forwardM32); // the oracle is the true camera path: not what a withheld term or a wrong latch on the R32F law computes
            double error=0,screenError=0,share[2]={0,0},lo=1,hi=0,mean=0,holeMax=0,moverMax=0,glassShare=0;unsigned window[2]={0,0},field=0,holePx=0,moverPx=0;const auto& tests=run.mask.back();
            if(flight.mover)for(UINT y=9;y<11;++y)for(UINT x=6;x<8;++x)if(routed_geometry(run,x,y)){++moverPx;moverMax=std::max(moverMax,double(px(tests,x,y,3)));} // the mover's (6..7, 9..10)
            for(UINT y=5;y<27;++y)for(UINT x=18;x<28;++x){++field;glassShare+=px(motion,x,y,3)==1&&px(run.depth.back(),x,y)<=-.5f;}
            glassShare/=field;
            for(UINT y=5;y<27;++y)for(UINT x=0;x<UINT(flight.laneHole);++x)if(routed_geometry(run,x,y)){++holePx;holeMax=std::max(holeMax,double(px(tests,x,y,3)));}
            for(UINT y=3;y+3<S;++y)for(UINT x=3;x+3<S;++x){const double b=px(tests,x,y,3),a=px(tests,x,y,0);if(modelled)error=std::max(error,std::fabs(b-double(model.ownCamera[y*S+x])));screenError=std::max(screenError,std::fabs(a-double(model.ownScreen[y*S+x])));
                if(x>=18&&x<28&&y>=5&&y<27&&!(y>=14&&y<18)&&routed_geometry(run,x,y)){const unsigned band=y>=18;++window[band];share[band]+=b>0;lo=std::min(lo,b);hi=std::max(hi,b);mean+=b;}}
            share[0]/=window[0];share[1]/=window[1];mean/=window[0]+window[1];
            char residualText[32];if(flight.mover)std::snprintf(residualText,sizeof residualText,"not_measured");else std::snprintf(residualText,sizeof residualText,"%.6f",model.residual); // the mover is routed valid depth off the camera path by construction
            std::printf("THIN_REGION_CAMERA_FLIGHT case=%s lane=%u withheld=%u lo=%.2f hi=%.2f speed_near=%.4f,%.4f speed_far=%.4f,%.4f routed_residual_px=%s camera_share_near=%.4f camera_share_far=%.4f window_min=%.4f window_max=%.4f window_mean=%.4f modelled=%u oracle_error=%.6f screen_oracle_error=%.6f lane_hole_columns=%d hole_reach_max=%.4f glass=%u mover=%u routed_sentinel_share=%.4f mover_px=%u mover_max=%.4f hole_px=%u\n",
                c.name,unsigned(flight.lane),unsigned(flight.withhold),double(c.lo),double(c.hi),flight.rowV[0][0],flight.rowV[0][1],flight.rowV[1][0],flight.rowV[1][1],residualText,share[0],share[1],lo,hi,mean,unsigned(modelled),error,screenError,flight.laneHole,holeMax,unsigned(flight.glass),unsigned(flight.mover),glassShare,moverPx,moverMax,holePx);
            if(flight.laneHole>0){++numeric_checks;require(holePx>0&&holeMax==0,"mixed frame: valid depth without a lane z stays on the far plane (closed on the hole's routed pixels), whatever the latch");}
            ++numeric_checks;require(error<=2./255&&screenError<=2./255,"flight: published gates equal the CPU oracle (double reprojection, w clamp) within 2 codes");
            if(c.expect==1){++numeric_checks;require(share[0]>=.99&&share[1]>=.99&&lo>=.9&&model.residual<.05,"flight: the camera gate is open on the static rows of both bands (residual < 0.05 px)");}
            if(c.expect==2){++numeric_checks;require(glassShare>=.3&&share[0]>=.99&&share[1]>=.99&&lo>=.9&&moverPx>0&&moverMax==0,"glass with a mover: routed sentinel cells leave the gate open on the shards while a routed valid-depth fast mover closes it on itself");}
            if(flight.glass&&c.expect==1){++numeric_checks;require(glassShare>=.3,"glass: routed sentinel-depth cells fill the space between the shard rows");}
            if(c.expect==0){++numeric_checks;require(window[0]+window[1]>0&&hi==0,"flight: the camera gate is closed on the window's routed rows");}
            if(c.expect==-1&&nearIndex<3)nearMean[nearIndex++]=mean;}
        ++numeric_checks;require(nearMean[0]>.05&&nearMean[0]<.95&&std::fabs(nearMean[0]-nearMean[1])<=2./255&&nearMean[2]<nearMean[0]-.05,"near geometry (c8 term O(1)): a graded gate, the same through the law and the lane, and lower without the term");
        farLo=savedLo;farHi=savedHi;flight=Flight{};flightLane=nullptr;thinFlight=false;}
    // ---- non-finite routed motion on the static arm (a routed 2x2 object at (6..7, 13..14)) ----
    // 1e30 px/frame: the speed overflows inside the mask program. The camera mask carries openness (saturate(1 - inf) = 0) and its
    // tests target must close both gates on every covered pixel (the resolve takes the smaller of a pixel's and its nearest-depth
    // neighbour's openness), as the plain mask closes its one within 8 px; every output stays finite. NaN: reported only. This backend compiles shaders with fast-math semantics, under which no in-shader expression is
    // reliable on a NaN (the plain mask's closure reads open as well); the openness form is closed under IEEE and D3D UNORM rules.
    // Section 32.5 (glass = 1): the same object on the depth SENTINEL. Its finite vote is "none", formed from the scaled screen speed,
    // so a non-finite correspondence must still close the camera gate.
    for(const bool glass:{false,true})for(double bad:{1e30,double(NAN)}){thinBadMotion=bad;thinBadGlass=glass;const bool asserted=bad==bad;const auto screenRun=thin_sequence(s,resolver,on97,32),cameraRun=thin_sequence(s,resolver,camera97,32);double open[2]={0,0},cameraScreenChannel=0;unsigned covered=0;
        const auto motion=s.read(s.motion.p); // the last frame's motion target: the covered pixels are those whose alpha is 1 and whose depth is the object's but lie off the shard rows' own motion
        for(UINT y=12;y<16;++y)for(UINT x=5;x<9;++x){const float u=px(motion,x,y,0);if(px(motion,x,y,3)==1&&!(std::fabs(u)<=2)){++covered;
            for(int dy=-8;dy<=8;++dy)for(int dx=-8;dx<=8;++dx){const UINT qx=UINT(std::min(std::max(int(x)+dx,0),int(S)-1)),qy=UINT(std::min(std::max(int(y)+dy,0),int(S)-1));
                open[0]=std::max(open[0],double(px(screenRun.mask[0],qx,qy,2)));}
            open[1]=std::max(open[1],double(px(cameraRun.mask.back(),x,y,3)));cameraScreenChannel=std::max(cameraScreenChannel,double(px(cameraRun.mask.back(),x,y,0)));}}
        bool finite=true;for(const auto& frame:cameraRun.output)for(float v:frame)finite=finite&&std::isfinite(v);
        const bool identical=same_rgb(cameraRun.output,screenRun.output)&&same_rgb(cameraRun.age,screenRun.age);
        std::printf("THIN_REGION_BAD_MOTION kind=%s glass=%u asserted=%u covered_px=%u screen_gate_max_within_8px=%.4f camera_gate_max_covered=%.4f camera_screen_channel_max_covered=%.4f camera_output_finite=%u identical_to_screen_gate=%u\n",asserted?"overflow_1e30":"nan",unsigned(glass),unsigned(asserted),covered,open[0],open[1],cameraScreenChannel,unsigned(finite),unsigned(identical));
        if(asserted){++numeric_checks;require(covered>0&&open[1]==0&&cameraScreenChannel==0&&open[0]==0,"non-finite speed (overflow): the camera mask's tests target closes both gates on every covered pixel, as the plain mask closes its one within 8 px");
            ++numeric_checks;require(finite,"non-finite speed (overflow): every camera-gate output finite");}}
    thinBadMotion=0;thinBadGlass=false;
    emissive_vote_cases(s,resolver,on97);
    sentinel_stabiliser_cases(s,resolver,camera97);
    // ---- mask-target creation failure: option off for the session, plain resolve bit for bit, history kept ----
    {const auto baseRun=thin_sequence(s,resolver,base,32),failed=thin_sequence(s,resolver,on97,32,true);
        ++numeric_checks;require(same_rgb(baseRun.output,failed.output)&&failed.masksFailed,"mask-target creation failure under the thin region: plain resolve, history kept");}
    if(!deferredFailures.empty())throw std::runtime_error(deferredFailures.front());
}
