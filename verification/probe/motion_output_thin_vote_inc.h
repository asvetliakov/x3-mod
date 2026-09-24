// Thin-vote script (X3M_TAA_THIN_VOTE; docs/architecture/taa-thin-geometry-alternatives.md section 3.2, "Implemented"),
// included in motion_output_fixture.cpp: the reviewed pair through the route with the ownership wrapper, TAA, the FP16
// scene and the sun-share lane (the four-channel RT2), 128 x 128. Every buffer is created as the game's clones are:
// MANAGED | WRITEONLY (option 0x660), which the readable-MANAGED creation policy (armed by the loader with the option
// on) creates without WRITEONLY. One vertex buffer and one INDEX16 index buffer hold two subsets drawn every frame:
//   P (panel): one 2.4 x 1.8 quad, object z 0.52, drawn first; its triangles are 46 px tall at the far scale;
//   S (struts): six 0.04375 x 1.4 quads, object z 0.5, in front of P; 1.4 px wide at the far scale, 5.6 px at the near one.
// The rows are an orthographic projection with clip w = 2 (so RT2 .a = w = 2 with the option off, and device depth =
// z / 2: S at 0.25, P at 0.26, too close for the tests draw's background margin, so the 7-tap line search does not flag
// S by itself: only the vote can). X3M_FIXTURE_THIN_SCALE (far = 1, the default; near = 4) scales x and y.
// Frame 0 queues the subsets (no histogram yet: c218.x = 1), its scene end reads them; from frame 1 the draws vote.
// X3M_FIXTURE_THIN_SCRIPT=hostile adds, each pair of struts on the panel at its own depth (far scale only):
//   U: its own VB/IB created while the policy is disarmed (x3m_thin_vote_fixture_readable_policy): WRITEONLY native
//      storage, refused without a Lock (not_readable), never votes;
//   D: a DEFAULT-pool WRITEONLY copy of U's vertices drawn with U's indices (not_managed), released before the Reset and
//      created again after it (a new allocation: not_managed again);
//   R: the panel drawn again with NumVertices past the end of the buffer (range);
//   T1 (from frame 1): its index buffer READONLY-locked by the application across frame 1's scene end (not_quiet, one
//      retry), read at frame 2's, voting at frame 3; rewritten in place 0.2 units wide at frame 4 (the write invalidates
//      its histogram: no vote from frame 4, read again at frame 5's scene end);
//   T2 (frame 3): released by the application right after its draw, while its read is queued (the queue's reference
//      keeps it alive to the scene end, which measures and releases it);
//   T3 (frame 4): released after its draw on a frame without a scene end; a Reset follows (the queue drops it unread);
//   V: a pair off screen (no pixels), rewritten in place before its draw at frames 1-4: each write drops the entry the
//      previous scene end measured (four reads, never a vote); the fourth write makes it volatile (the count survives
//      the Reset), so frame 5's scene end refuses it without a Lock (volatile_refused).
// Per frame the script prints the RT2 lanes of every subset's pixels (read back through the seam after the draws,
// classified by their device depth) and the last draw's twelve pixel-ABI floats; the runner reads the DLL's capture dumps
// (depth and taa_mask) for the tests target's b and compares the option-off twin's RT1 dumps.
constexpr unsigned thin_frames = 6;
struct ThinVertex { unsigned short data[12]; };
void thin_vertex(std::vector<ThinVertex>& out, float x, float y, float z) {
    ThinVertex v{};
    const unsigned short p[12] = {float_to_half(x), float_to_half(y), float_to_half(z), half(1), half(.5f), half(.5f), 0, half(1), 0, 0, half(1), half(1)};
    std::memcpy(v.data, p, sizeof p);
    out.push_back(v);
}
void thin_quad(std::vector<ThinVertex>& vertices, std::vector<unsigned short>& indices, float x, float y, float w, float l, float z) {
    const auto base = static_cast<unsigned short>(vertices.size());
    thin_vertex(vertices, x, y, z); thin_vertex(vertices, x + w, y, z); thin_vertex(vertices, x, y + l, z); thin_vertex(vertices, x + w, y + l, z);
    const unsigned short t[6] = {base, static_cast<unsigned short>(base + 1), static_cast<unsigned short>(base + 2),
                                 static_cast<unsigned short>(base + 2), static_cast<unsigned short>(base + 1), static_cast<unsigned short>(base + 3)};
    indices.insert(indices.end(), t, t + 6);
}
// One subset in its own (or a shared) buffer pair; the application owns the references until it releases them.
struct ThinSubset {
    const char* name = ""; float depth = 0; // device depth of its pixels (object z / 2)
    Com<IDirect3DVertexBuffer9> vb; Com<IDirect3DIndexBuffer9> ib;
    UINT vertices = 0, start = 0, primitives = 0;
    Object object{""};
};
void thin_buffers(Fixture& f, ThinSubset& s, const std::vector<ThinVertex>& vertices, const std::vector<unsigned short>& indices, D3DPOOL pool = D3DPOOL_MANAGED) {
    api(f.d->CreateVertexBuffer(UINT(vertices.size() * sizeof(ThinVertex)), D3DUSAGE_WRITEONLY, 0, pool, &s.vb.p, nullptr), "thin vertex buffer");
    void* data = nullptr; api(s.vb->Lock(0, 0, &data, 0), "thin vertex lock");
    std::memcpy(data, vertices.data(), vertices.size() * sizeof(ThinVertex)); api(s.vb->Unlock(), "thin vertex unlock");
    if (!indices.empty()) {
        api(f.d->CreateIndexBuffer(UINT(indices.size() * 2), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &s.ib.p, nullptr), "thin index buffer");
        api(s.ib->Lock(0, 0, &data, 0), "thin index lock");
        std::memcpy(data, indices.data(), indices.size() * 2); api(s.ib->Unlock(), "thin index unlock");
    }
    s.vertices = UINT(vertices.size());
}
void thin_pair(Fixture& f, ThinSubset& s, const char* name, float x0, float x1, float z, std::uint32_t id) {
    std::vector<ThinVertex> vertices; std::vector<unsigned short> indices;
    thin_quad(vertices, indices, x0, .2f, .04375f, 1.4f, z); thin_quad(vertices, indices, x1, .2f, .04375f, 1.4f, z);
    s.name = name; s.depth = z / 2; thin_buffers(f, s, vertices, indices); s.start = 0; s.primitives = 4;
    s.object.name = name; s.object.scope = {1, 3, 5, 20 + id, 21, 0x1400 + 0x100 * id, 0x2000, 0x3000, 0x4400 + 0x100 * id, 11 + id, 9, 0x15 + id, 0x2};
}
void run_thin_vote(Fixture& f) {
    require(f.seam && f.enabled && f.taa && f.hdr && f.emission_readback && f.last_pixel_abi, "thinvote runs on the seam DLL with the route, TAA, the FP16 scene and the readbacks");
    char setting[16]{};
    const bool vote = GetEnvironmentVariableA("X3M_TAA_THIN_VOTE", setting, sizeof setting) == 2 && !std::strcmp(setting, "on");
    const bool near_scale = GetEnvironmentVariableA("X3M_FIXTURE_THIN_SCALE", setting, sizeof setting) == 4 && !std::strcmp(setting, "near");
    const bool hostile = GetEnvironmentVariableA("X3M_FIXTURE_THIN_SCRIPT", setting, sizeof setting) == 7 && !std::strcmp(setting, "hostile");
    const float scale = near_scale ? 4.f : 1.f;
    const auto policy = symbol<HRESULT (*)(IDirect3DDevice9*, int)>(f.runtime, "x3m_thin_vote_fixture_readable_policy", false);
    require(!hostile || (vote && !near_scale && policy), "the hostile script runs with the vote, at the far scale, on a seam exporting the policy switch");
    // U first, while the policy is disarmed: WRITEONLY native storage.
    ThinSubset unarmed, pool_default, transient[3], rewritten;
    if (hostile) {
        api(policy(f.d.p, 0), "disarm the readable policy");
        thin_pair(f, unarmed, "U", -1.15f, -1.05f, .49f, 0);
        api(policy(f.d.p, 1), "arm the readable policy");
    }
    // P and S share one buffer pair (created armed when the option is on).
    ThinSubset main;
    {
        std::vector<ThinVertex> vertices; std::vector<unsigned short> indices;
        thin_quad(vertices, indices, -1.2f, 0.f, 2.4f, 1.8f, .52f);                                                      // P: indices 0..5
        for (unsigned i = 0; i < 6; ++i) thin_quad(vertices, indices, -.8f + .3f * float(i), .2f, .04375f, 1.4f, .5f); // S: 6..41
        thin_buffers(f, main, vertices, indices);
    }
    Object panel{"P"}, struts{"S"}, ranged{"R"};
    panel.scope = {1, 3, 5, 13, 21, 0x1200, 0x2000, 0x3000, 0x4200, 9, 9, 0x13, 0x2};
    struts.scope = {1, 3, 5, 14, 21, 0x1300, 0x2000, 0x3000, 0x4300, 10, 9, 0x14, 0x2};
    ranged.scope = {1, 3, 5, 15, 21, 0x1380, 0x2000, 0x3000, 0x4380, 10, 9, 0x14, 0x2};
    // D: U's vertices at another depth in the DEFAULT pool (recreated after the Reset), drawn with U's indices.
    const auto make_default = [&] {
        pool_default.vb.reset();
        std::vector<ThinVertex> vertices; std::vector<unsigned short> indices;
        thin_quad(vertices, indices, -.95f, .2f, .04375f, 1.4f, .48f); thin_quad(vertices, indices, -.88f, .2f, .04375f, 1.4f, .48f);
        thin_buffers(f, pool_default, vertices, {}, D3DPOOL_DEFAULT);
        pool_default.name = "D"; pool_default.depth = .24f; pool_default.start = 0; pool_default.primitives = 4;
        pool_default.object.name = "D"; pool_default.object.scope = {1, 3, 5, 19, 21, 0x1900, 0x2000, 0x3000, 0x4900, 16, 9, 0x19, 0x2};
    };
    if (hostile) { make_default(); thin_pair(f, rewritten, "V", 3.f, 3.1f, .44f, 8); }
    std::printf("THIN_MODE vote=%u scale=%g width=%u height=%u strut_px=%.4f panel_triangles=2 strut_triangles=12 script=%s\n", vote, double(scale),
                Fixture::W, Fixture::H, double(.04375f * scale * float(Fixture::W) / 4.f), hostile ? "hostile" : "plain");
    const auto draw = [&](Object& o, IDirect3DVertexBuffer9* vb, IDirect3DIndexBuffer9* ib, UINT vertices, UINT start, UINT primitives) {
        f.scope(&o);
        api(f.d->SetStreamSource(0, vb, 0, 24), "thin stream"); api(f.d->SetIndices(ib), "thin indices");
        api(f.d->SetVertexDeclaration(f.declaration.p), "thin declaration");
        api(f.d->SetVertexShader(f.vs.p), "thin VS"); api(f.d->SetPixelShader(f.ps.p), "thin PS");
        float m[16]; std::memcpy(m, identity, sizeof m); m[0] = scale; m[5] = scale; m[15] = 2.f; // clip w = 2
        api(f.d->SetVertexShaderConstantF(24, m, 4), "thin rows");
        api(f.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertices, start, primitives), "thin draw");
        ++f.draw_index;
    };
    const auto draw_subset = [&](ThinSubset& s) { draw(s.object, s.vb.p, s.ib.p ? s.ib.p : unarmed.ib.p, s.vertices, s.start, s.primitives); };
    struct Class { const char* name; float depth; };
    std::vector<Class> classes = {{"S", .25f}, {"P", .26f}};
    if (hostile) classes.insert(classes.end(), {{"U", .245f}, {"D", .24f}, {"T1", .235f}, {"T2", .23f}});
    std::vector<float> lanes(std::size_t(Fixture::W) * Fixture::H * 4);
    for (unsigned step = 0; step < thin_frames; ++step) {
        f.frame_begin();
        draw(panel, main.vb.p, main.ib.p, main.vertices, 0, 2);
        draw(struts, main.vb.p, main.ib.p, main.vertices, 6, 12);
        bool scene_end = true;
        IDirect3DIndexBuffer9* held = nullptr;
        if (hostile) {
            if (step >= 1 && step <= 4) {
                std::vector<ThinVertex> moved; std::vector<unsigned short> unused;
                thin_quad(moved, unused, 3.f + .01f * float(step), .2f, .04375f, 1.4f, .44f); thin_quad(moved, unused, 3.1f, .2f, .04375f, 1.4f, .44f);
                void* data = nullptr; api(rewritten.vb->Lock(0, 0, &data, 0), "V rewrite lock");
                std::memcpy(data, moved.data(), moved.size() * sizeof(ThinVertex)); api(rewritten.vb->Unlock(), "V rewrite unlock");
            }
            draw_subset(rewritten);
            draw_subset(unarmed);
            draw_subset(pool_default);
            draw(ranged, main.vb.p, main.ib.p, main.vertices + 64, 0, 2); // NumVertices past the buffer's end
            if (step == 1) thin_pair(f, transient[0], "T1", .8f, .9f, .47f, 5);
            if (step == 4) {
                // The application rewrites T1's vertices, 0.2 units wide (6.4 px, above the window), in place: the write
                // invalidation drops T1's histogram before its draw, which queues it again (read at frame 5's scene end).
                std::vector<ThinVertex> wide; std::vector<unsigned short> unused;
                thin_quad(wide, unused, .8f, .2f, .2f, 1.4f, .47f); thin_quad(wide, unused, .9f, .2f, .2f, 1.4f, .47f);
                void* data = nullptr; api(transient[0].vb->Lock(0, 0, &data, 0), "T1 rewrite lock");
                std::memcpy(data, wide.data(), wide.size() * sizeof(ThinVertex)); api(transient[0].vb->Unlock(), "T1 rewrite unlock");
                std::printf("THIN_REWRITTEN frame=%llu step=%u subset=T1 width=0.2\n", f.frame, step);
            }
            if (step >= 1) draw_subset(transient[0]);
            if (step == 1) {
                void* data = nullptr; // the application's READONLY Lock across the scene end: the read finds it pending
                api(transient[0].ib->Lock(0, 0, &data, D3DLOCK_READONLY), "T1 held READONLY lock");
                held = transient[0].ib.p;
            }
            if (step == 3 || step == 4) {
                auto& t = transient[step - 2];
                thin_pair(f, t, step == 3 ? "T2" : "T3", 1.f, 1.1f, .46f, step == 3 ? 6 : 7);
                draw_subset(t);
                t.vb.reset(); t.ib.reset(); // released by the application while its read is queued
                std::printf("THIN_RELEASED frame=%llu step=%u subset=%s\n", f.frame, step, step == 3 ? "T2" : "T3");
            }
            if (step == 4) scene_end = false; // T3's read stays queued past this frame's Present, then the Reset
        }
        // RT2 after the draws: .r device depth, .a the vote (on) or w = 2 (off).
        unsigned w = 0, h = 0;
        api(f.emission_readback(f.d.p, 2, lanes.data(), unsigned(lanes.size()), &w, &h), "thin RT2 readback");
        require(w == Fixture::W && h == Fixture::H, "thin RT2 is the four-channel lane");
        std::vector<unsigned> counts(classes.size() + 1);
        std::vector<float> lo(classes.size() + 1, 1e30f), hi(classes.size() + 1, -1e30f), blue(classes.size() + 1, 0.f);
        for (std::size_t p = 0; p < std::size_t(w) * h; ++p) {
            const float* t = &lanes[p * 4];
            std::size_t c = classes.size() + 1;
            if (t[0] < -.5f) c = classes.size();
            else for (std::size_t k = 0; k < classes.size(); ++k) if (t[0] > classes[k].depth - .0015f && t[0] < classes[k].depth + .0015f) c = k;
            if (c > classes.size()) continue;
            ++counts[c]; lo[c] = t[3] < lo[c] ? t[3] : lo[c]; hi[c] = t[3] > hi[c] ? t[3] : hi[c]; blue[c] = t[2];
        }
        for (std::size_t k = 0; k <= classes.size(); ++k)
            std::printf("THIN_RT2 frame=%llu step=%u subset=%s pixels=%u a_min=%.6g a_max=%.6g b=%.6g\n", f.frame, step, k < classes.size() ? classes[k].name : "fill",
                        counts[k], double(counts[k] ? lo[k] : 0.f), double(counts[k] ? hi[k] : 0.f), double(blue[k]));
        float abi[12]{}; api(f.last_pixel_abi(f.d.p, abi, 12), "thin pixel ABI");
        std::printf("THIN_ABI frame=%llu step=%u c216=%.9g,%.9g,%.9g,%.9g c217=%.9g,%.9g,%.9g,%.9g c218=%.9g,%.9g,%.9g,%.9g\n", f.frame, step,
                    double(abi[0]), double(abi[1]), double(abi[2]), double(abi[3]), double(abi[4]), double(abi[5]), double(abi[6]), double(abi[7]),
                    double(abi[8]), double(abi[9]), double(abi[10]), double(abi[11]));
        f.decide();
        api(f.d->SetDepthStencilSurface(nullptr), "thin scene-end detach depth");
        if (scene_end) api(f.d->StretchRect(f.back.p, nullptr, f.bloom_surface.p, nullptr, D3DTEXF_NONE), "thin scene end and TAA");
        api(f.d->EndScene(), "thin EndScene");
        api(f.d->SetDepthStencilSurface(f.depth.p), "thin depth rebind");
        api(f.d->Present(nullptr, nullptr, nullptr, nullptr), "thin Present");
        if (held) api(held->Unlock(), "T1 held lock released");
        ++f.frame; ++f.frames_since_reset; f.camera_history = f.camera_current;
        if (hostile && step == 4) {
            pool_default.vb.reset(); // DEFAULT pool: gone before the Reset, created again after it
            api(f.d->SetStreamSource(0, nullptr, 0, 0), "thin release stream before Reset"); api(f.d->SetIndices(nullptr), "thin release indices before Reset");
            f.reset();
            make_default();
        }
    }
    api(f.d->SetIndices(nullptr), "thin release index binding");
    api(f.d->SetStreamSource(0, nullptr, 0, 0), "thin release stream binding");
    std::puts("THIN PASS");
}
