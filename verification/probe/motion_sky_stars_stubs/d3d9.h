#pragma once
// Host-only seam for the sky-star route fixture (verification/probe/motion_sky_stars_fixture.cpp): the handoff fixture's
// COM seam plus the documented D3D9 values and types the sky-star fragment names. No Windows ABI, no rendering.
#include "../motion_hdr_scene_stubs/d3d9.h"
constexpr D3DRENDERSTATETYPE D3DRS_ZWRITEENABLE = D3DRENDERSTATETYPE(14), D3DRS_ALPHATESTENABLE = D3DRENDERSTATETYPE(15),
                             D3DRS_DESTBLEND = D3DRENDERSTATETYPE(20), D3DRS_CULLMODE = D3DRENDERSTATETYPE(22),
                             D3DRS_ALPHABLENDENABLE = D3DRENDERSTATETYPE(27),
                             D3DRS_COLORWRITEENABLE = D3DRENDERSTATETYPE(168), D3DRS_BLENDOP = D3DRENDERSTATETYPE(171),
                             D3DRS_SEPARATEALPHABLENDENABLE = D3DRENDERSTATETYPE(206),
                             D3DRS_SRCBLENDALPHA = D3DRENDERSTATETYPE(207), D3DRS_DESTBLENDALPHA = D3DRENDERSTATETYPE(208);
struct D3DDISPLAYMODE {
    UINT Width, Height, RefreshRate;
    D3DFORMAT Format;
};
union LARGE_INTEGER {
    long long QuadPart;
};
inline int QueryPerformanceCounter(LARGE_INTEGER* p) {
    static long long ticks = 0;
    p->QuadPart = ++ticks;
    return 1;
}
