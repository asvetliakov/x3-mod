src=open('engine_plumes_fixture.cpp').read()
probe = r'''
static float cfrac(float x) { return x - std::floor(x); }
static float cvnoise(float px, float py, float pz) {
    volatile float fx = cfrac(px), fy = cfrac(py), fz = cfrac(pz);
    float ix = px - fx, iy = py - fy, iz = pz - fz;
    float bx = ix * 0.3183099f + 0.1f, by = iy * 0.3183099f + 0.2f, bz = iz * 0.3183099f + 0.3f;
    float ax = cfrac(bx), ay = cfrac(by), az = cfrac(bz);
    float Bx = cfrac(bx + 0.3183099f), By = cfrac(by + 0.3183099f), Bz = cfrac(bz + 0.3183099f);
    float sx = fx * fx * (3.f - 2.f * fx), sy = fy * fy * (3.f - 2.f * fy), sz = fz * fz * (3.f - 2.f * fz);
    float X[4] = {ax, Bx, ax, Bx}, Y[4] = {ay, ay, By, By}, h[4];
    for (int k = 0; k < 4; ++k) {
        float xy = X[k] * Y[k] * 83521.f, sxy = X[k] + Y[k];
        float h0 = cfrac(xy * az * (sxy + az)), h1 = cfrac(xy * Bz * (sxy + Bz));
        h[k] = h0 + (h1 - h0) * sz;
    }
    float hx0 = h[0] + (h[1] - h[0]) * sx, hx1 = h[2] + (h[3] - h[2]) * sx;
    return hx0 + (hx1 - hx0) * sy;
}
static float cfbm(float x, float y, float z) {
    return .5f * cvnoise(x, y, z) + .25f * cvnoise(x * 2.03f + 1.7f, y * 2.03f + 1.7f, z * 2.03f + 1.7f) +
           .125f * cvnoise(x * 4.1f + 3.1f, y * 4.1f + 3.1f, z * 4.1f + 3.1f);
}
void noise_probe(IDirect3DDevice9* d, Targets& t, Quad& quad) {
    const char* ps = "float4 c:register(c0);"
"float vnoise(float3 p){float3 f=frac(p);const float3 i=p-f;const float3 base=i*0.3183099+float3(0.1,0.2,0.3);const float3 a=frac(base),b=frac(base+0.3183099);f=f*f*(3.0-2.0*f);const float4 X=float4(a.x,b.x,a.x,b.x);const float4 Y=float4(a.y,a.y,b.y,b.y);const float4 xy=X*Y*83521.0,sxy=X+Y;const float4 h0=frac(xy*a.z*(sxy+a.z));const float4 h1=frac(xy*b.z*(sxy+b.z));const float4 h=lerp(h0,h1,f.z);const float2 hx=lerp(h.xz,h.yw,f.x);return lerp(hx.x,hx.y,f.y);}"
"float fbm(float3 p){return 0.5*vnoise(p)+0.25*vnoise(p*2.03+1.7)+0.125*vnoise(p*4.1+3.1);}"
"float4 main(float2 v:VPOS):COLOR0{float qx=(v.x+0.5)/60.0;float qy=(v.y+0.5-8.0)/60.0;float3 p=float3(qx-c.x,qy*4.5,c.y);return float4(fbm(p*2.2+float3(5.0,2.0,1.0))-0.4375,0,0,0);}";
    std::vector<DWORD> code; compile(ps, "ps_3_0", code);
    Com<IDirect3DPixelShader9> p; check("probe ps", d->CreatePixelShader(code.data(), &p.p));
    Com<IDirect3DSurface9> sys; check("probe sys", d->CreateOffscreenPlainSurface(t.w, t.h, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr));
    ep::Look k = ep::default_look; float rate = 0.f; ep::flow_rate(k, &rate);
    const double flow1 = flow_at(10.f, &k), flow2 = flow1 + double(rate) * .2;
    const unsigned seeds[3] = {0u, 128u, 255u};
    const float values[4] = {100.f, 600.f, 1500.f, 10000.f};
    for (unsigned sj = 0; sj < 4; ++sj) {
      float seedv;
      if (sj < 3) seedv = float(seeds[sj]) / 255.f * 1861.5f + 10.f * .7f; else seedv = -1.f;
      for (unsigned vk = 0; vk < 4; ++vk) for (int which = 0; which < 2; ++which) {
        float factor = 0.f; ep::flow_factor(k, values[vk], &factor);
        float ph = 0.f; ep::nozzle_phase(which ? flow2 : flow1, factor, &ph);
        float sd = sj < 3 ? seedv : 0.f;
        const float c0[4] = {ph, sd, 0, 0};
        check("probe begin", d->BeginScene());
        check("probe rt", d->SetRenderTarget(0, t.lane1_s.p));
        check("blend off", d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
        check("probe c", d->SetPixelShaderConstantF(0, c0, 1));
        quad.draw(d, t.w, t.h, p.p);
        check("probe end", d->EndScene());
        check("probe rb", d->GetRenderTargetData(t.lane1_s.p, sys.p));
        D3DLOCKED_RECT r{}; check("probe lock", sys->LockRect(&r, nullptr, D3DLOCK_READONLY));
        double maxd = 0; unsigned big = 0, n = 0; double sum2 = 0;
        for (int y = 0; y < 17; ++y) for (int x = 0; x < 480; ++x) {
            const float g = reinterpret_cast<const float*>(static_cast<const unsigned char*>(r.pBits) + y * r.Pitch)[x];
            const float qx = (float(x) + .5f) / 60.f, qy = (float(y) + .5f - 8.f) / 60.f;
            const float c = cfbm((qx - ph) * 2.2f + 5.f, qy * 4.5f * 2.2f + 2.f, sd * 2.2f + 1.f) - .4375f;
            const double df = std::fabs(double(g) - double(c));
            maxd = std::max(maxd, df); if (df > 1e-3) ++big; ++n; sum2 += df * df;
            if (y == 8 && (x % 4 == 0)) std::printf("PROBEROW seed=%.4f V=%.0f which=%d x=%d gpu=%.7g cpu=%.7g\n", double(sd), double(values[vk]), which, x, double(g), double(c));
        }
        sys->UnlockRect();
        std::printf("PROBE w=%u seed=%.4f V=%.0f which=%d phase=%.9g max_abs=%.6g rms=%.6g over_1e-3=%u of %u\n", t.w, double(sd), double(values[vk]), which, double(ph), maxd, std::sqrt(sum2 / n), big, n);
      }
    }
}
'''
anchor = "void flow_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass) {"
src = src.replace(anchor, probe + anchor)
old = '            if (wanted("flow")) flow_case(d, *t, *scene, pass);'
assert old in src
open('engine_plumes_fixture.cpp','w').write(src)
