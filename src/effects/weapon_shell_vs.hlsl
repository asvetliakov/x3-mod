// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1): the vertex program of the hex patch. A patch
// draw is its cells as flat hexagons (renderer/weapon_effects_pass.cpp CellVertex: six corners per cell, a fan of four
// triangles) in its region's lattice plane, each at its own depth along the region axis. A corner is an integer lattice
// point (x in half cells, y in sqrt 3 / 6 cells, so neighbouring cells at one depth share bit-identical corners: no
// crack, no pixel drawn twice), its plane point q = corner x (1/2, sqrt 3 / 6) x cell; the view-space point is
// centre + U q.x + V q.y + A depth (the region's u, v and axis taken through the owner's rows to view space on the CPU
// in double, the owner's centre too, so a far camera loses nothing). Clip-space output (w = view z, z = view z - near
// z): the rasterizer clips at the near plane, so an eye close to the hull (inside the owner's bounding sphere) still
// draws the cells in front of it. Compiled with tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0);     // m00, m11, m20, m21 of the jittered projection routed draws use
float4 viewport : register(c1);       // unused by the patches (the decals' window law)
float4 centre : register(c2);         // the owner's view-space centre, near view z
float4 frame[3] : register(c3);       // U, V, A (view space, xyz); the cell (view units) in frame[0].w
float4 anchor[3] : register(c6);      // the region's axis, u and v in the owner's frame (xyz)
struct Output {
    float4 position : POSITION;
    float4 cell : TEXCOORD0;     // the offset from the cell centre (cells), view z, the cell's clock
    float4 key : TEXCOORD1;      // the cell's integer key, unused
    float3 local : TEXCOORD2;    // the point in the owner's frame (view units): the seam score
};
Output main(float4 corner : POSITION, float4 key : TEXCOORD0) {
    Output o;
    float2 lattice = corner.xy * float2(0.5, 0.28867513);
    float2 q = lattice * frame[0].w;
    float3 v = centre.xyz + frame[0].xyz * q.x + frame[1].xyz * q.y + frame[2].xyz * corner.z;
    o.position = float4(projection.x * v.x + projection.z * v.z, projection.y * v.y + projection.w * v.z, v.z - centre.w, v.z);
    o.cell = float4(lattice - float2(0.5, 0.8660254) * key.xy, v.z, corner.w);
    o.key = key;
    o.local = anchor[1].xyz * q.x + anchor[2].xyz * q.y + anchor[0].xyz * corner.z;
    return o;
}
