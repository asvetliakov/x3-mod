// Weapon effects, shield hit (docs/architecture/weapon-effects.md 5.1.1): the vertex program of the screen-space hex
// patch. A patch draw is one face of an owner as one box (renderer/weapon_effects_pass.cpp CellVertex: eight corners
// (u, v, a) in the face's frame, view units): the bounds of its cells in the face's lattice plane and of their
// anchors' depths -+ the extent along its axis, so the box holds every surface point the face's cells may light; the
// view-space point is centre + U u + V v + A a (the face's u, v and axis taken through the owner's rows to view space
// on the CPU in double, the owner's centre too). Clip-space output (w = view z, z = view z - near z): the rasterizer
// clips at the near plane. The pixel program finds the surface inside the box from the lane. Compiled with
// tools/shaders/generate_weapon_effects_programs.py (vs_3_0).
float4 projection : register(c0);     // m00, m11, m20, m21 of the jittered projection routed draws use
float4 viewport : register(c1);       // unused by the patches (the decals' window law)
float4 centre : register(c2);         // the owner's view-space centre, near view z
float4 frame[3] : register(c3);       // U, V, A (view space, xyz)
float4 main(float4 corner : POSITION) : POSITION {
    float3 v = centre.xyz + frame[0].xyz * corner.x + frame[1].xyz * corner.y + frame[2].xyz * corner.z;
    return float4(projection.x * v.x + projection.z * v.z, projection.y * v.y + projection.w * v.z, v.z - centre.w, v.z);
}
