// Far-cascade caster pool control script ("shadowpool" mode;
// docs/architecture/shadow-cascade-extents.md, "Caster pool control"). The
// seam DLL with two cascades narrowed to the unit-size geometry (8 and 40
// units), the retention script's world-placed camera and synthetic nodes
// (RetentionScript: serial, handle, class bits born in the seam's observer;
// the anchor node of class 0x20 first on every frame). X3M_FIXTURE_SHADOW_POOL:
//   static      X3M_SHADOW_CASCADE_STATIC_FROM=1: a static node S and a node M
//               moving 1/16 of a row unit per frame (0.078 units > eps 0.05).
//               Both shadow cascade 0 every frame; cascade 1 takes the anchor
//               and S once they are classified static (from frame 1 by the
//               ring: their rows equal the previous sighting's; under a
//               retention store S waits for its eight verified sightings,
//               frame 9) and never M. Run with the store off, census and live.
//   importance  X3M_SHADOW_CASCADE_DROP_ORDER=importance, cascade 1 capped at
//               4: seven nodes of one shape at one distance with distinct
//               scales (1.5 .. 0.13) plus the anchor, submitted in an order
//               rotated every frame. Frame 0 (no extents: every size 0) keeps
//               the four lowest serials; every later frame keeps the four
//               largest, whatever the order, and dropped_min_size1 is the
//               fifth's size, identical on every frame.
//   records     X3M_SHADOW_CASCADE_RECORDS=1024,4096 with cascade 1 capped at
//               4,095 under the importance order: 4,095 nodes on one mesh
//               inside cascade 1 only (view x 20..36 units) plus the anchor:
//               4,096 records every frame (the list is full, nothing
//               overflows), the smallest (the farthest node) dropped, 4,096
//               issues over the budget 640 so the far cascade replays on even
//               frames only. Counters only (no map twin at this count).
// Per frame: SHADOW_CAMERA / SHADOW_SUN / SHADOW_DRAW (with scale=) for the
// runner's CPU twin of every compared map, POOL_KEPT per cascade (the casters
// the map must show) and POOL_EXPECT (the counter line's values).
namespace {
constexpr unsigned pool_static_frames = 14, pool_importance_frames = 8, pool_records_frames = 6;
constexpr unsigned pool_store_static_frame = 9; // shadow_retention::static_sightings = 8 verified sightings: promoted at the scene end of frame 8
constexpr float pool_moving_step = .0625f;      // dyadic: exactly representable rows
constexpr unsigned pool_importance_nodes = 7, pool_records_nodes = 4095;
constexpr float pool_scales[pool_importance_nodes] = {.13f, .2f, .3f, .44f, .67f, 1.f, 1.5f}; // id i has scale pool_scales[i - 1]: the lowest serials are the smallest
void pool_fill_scaled(IDirect3DVertexBuffer9* buffer, const float tri[3][2], float scale) {
    void* dst = nullptr; api(buffer->Lock(0, 0, &dst, 0), "Lock pool node");
    for (UINT v = 0; v < 3; ++v) {
        unsigned short data[12] = {half(tri[v][0] * scale), half(tri[v][1] * scale), half(.5f), half(7), half(float(v & 1)), half(float(v >> 1)), 0, half(7), 0, 0, half(1), half(7)};
        std::memcpy(static_cast<char*>(dst) + v * 24, data, 24);
    }
    api(buffer->Unlock(), "Unlock pool node");
}
struct PoolScript {
    RetentionScript& s; Fixture& f;
    std::map<unsigned, float> scale_of; // id -> scale (1 unless set)
    const char* name = "";
    // One frame: the anchor, then `listed` in that order. `kept[c]`: the ids the map of cascade c must show.
    void frame(const std::vector<RetentionNode*>& listed, const std::vector<std::vector<unsigned>>& kept, unsigned c0, unsigned c1, unsigned capped1, unsigned refused1, bool compare) {
        std::vector<RetentionNode*> drawn{s.anchor}; drawn.insert(drawn.end(), listed.begin(), listed.end());
        f.camera_scripted = true;
        f.frame_begin();
        api(f.d->SetPixelShaderConstantF(4, s.sun, 1), "SetPixelShaderConstantF LightDir_Dir0");
        const auto& cam = f.camera_current;
        const unsigned long long now = f.frame;
        if (compare) {
            std::printf("SHADOW_CAMERA frame=%llu m00=%.9g m11=%.9g r=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g t=%.9g,%.9g,%.9g\n", now, cam.m00, cam.m11,
                        cam.r[0], cam.r[1], cam.r[2], cam.r[3], cam.r[4], cam.r[5], cam.r[6], cam.r[7], cam.r[8], cam.t[0], cam.t[1], cam.t[2]);
            std::printf("SHADOW_SUN frame=%llu direction=%.9g,%.9g,%.9g\n", now, double(s.sun[0]), double(s.sun[1]), double(s.sun[2]));
        }
        for (auto* n : drawn) {
            const bool known = n->object.scope.known != 0;
            const bool matched = known && n->last_drawn == static_cast<long long>(now) - 1 && f.frames_since_reset > 0;
            const auto scale = scale_of.find(n->id);
            if (compare) std::printf("SHADOW_DRAW frame=%llu caster=%u shape=%c t=%.9g p=%.9g zo=%.9g scale=%.9g\n", now, n->id, n->shape, n->t, n->p, n->zo, scale == scale_of.end() ? 1. : double(scale->second));
            f.draw(n->object, n->t, n->p, n->zo, known, known, matched, Alter::None, false);
            n->last_drawn = static_cast<long long>(now);
        }
        std::printf("POOL_FRAME frame=%llu case=%s drawn=%u compare=%u\n", now, name, unsigned(drawn.size()), unsigned(compare));
        std::printf("POOL_EXPECT frame=%llu c0=%u c1=%u capped1=%u static_only_refused1=%u\n", now, c0, c1, capped1, refused1);
        if (compare) for (unsigned c = 0; c < kept.size(); ++c) {
            std::string ids;
            for (unsigned id : kept[c]) { if (!ids.empty()) ids += ','; ids += std::to_string(id); }
            std::printf("POOL_KEPT frame=%llu cascade=%u count=%u casters=%s\n", now, c, unsigned(kept[c].size()), ids.empty() ? "-" : ids.c_str());
        }
        shadow_bloom_copy(f);
        api(f.d->EndScene(), "EndScene");
        const auto image = f.color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", now, static_cast<unsigned long long>(Fixture::color_hash(image)));
        f.previous_presented = image;
        api(f.d->SetDepthStencilSurface(f.depth.p), "SetDepthStencilSurface rebind");
        api(f.d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++f.frame; ++f.frames_since_reset;
        if (compare) shadow_cascade_readback(f, s.readback, 2, now);
        s.read();
        s.expect(RsPending, 0, "no Release is owed after a scene end");
    }
};
void run_shadow_pool_integration(Fixture& f) {
    require(f.seam && f.camera && f.enabled && !f.taa, "shadowpool runs on the seam DLL with the route and the scripted camera");
    char setting[16]{};
    const auto flag = [&](const char* variable) { return GetEnvironmentVariableA(variable, setting, sizeof setting) == 1 && setting[0] == '1'; };
    RetentionScript s(f, symbol<RetentionStatsFn>(f.runtime, "x3m_shadow_retention_fixture_stats", false), symbol<RetentionLifetimeFn>(f.runtime, "x3m_shadow_retention_fixture_lifetime", false),
                      symbol<ShadowCascadeReadbackFn>(f.runtime, "x3m_shadow_replay_fixture_cascade_readback", false));
    s.mode = flag("X3M_SHADOW_CASTER_RETENTION") ? 2 : flag("X3M_SHADOW_RETENTION_CENSUS") ? 1 : 0;
    require(s.stats_fn && s.lifetime && s.readback, "the seam DLL exports the retention and cascade seams");
    char script[16]{};
    require(GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_POOL", script, sizeof script) > 0, "X3M_FIXTURE_SHADOW_POOL names the script");
    const bool is_static = !std::strcmp(script, "static"), is_importance = !std::strcmp(script, "importance"), is_records = !std::strcmp(script, "records");
    require(is_static || is_importance || is_records, "X3M_FIXTURE_SHADOW_POOL is static, importance or records");
    std::printf("POOL_MODE script=%s store=%u\n", script, s.mode);
    s.lifetime(7, 0, 0); s.lifetime(3, f.b.scope.load_epoch, f.b.scope.registry_epoch);
    std::memcpy(f.camera_position, retention_eye, sizeof retention_eye); f.camera_yaw = 0;
    shadow_ensure_bloom(f);
    s.read(); require(s.s[RsEnabled] == (s.on() ? 1u : 0u) && s.s[RsMode] == s.mode, "the store exists exactly while the option is on");
    s.anchor = &s.make(0, 'B', 1.5f, .3f, nullptr, 0, 0x20); // never retained (class 0x20): the ring classifies it under every store setting
    PoolScript p{s, f, {}, script};
    if (is_static) {
        auto& fixed = s.make(1, 'B', -.05f, .05f); auto& mover = s.make(2, 'B', .375f, .10f);
        const unsigned fixed_from = s.on() ? pool_store_static_frame : 1u; // the store knows S from its first sighting and needs eight verified ones; the ring one
        for (unsigned frame = 0; frame < pool_static_frames; ++frame) {
            mover.t = .375f + pool_moving_step * float(frame);
            std::vector<unsigned> far_ids; // (`far` is a Win16 macro)
            if (frame >= 1) far_ids.push_back(0);
            if (frame >= fixed_from) far_ids.push_back(1);
            p.frame({&fixed, &mover}, {{0, 1, 2}, far_ids}, 3, unsigned(far_ids.size()), 0, 3 - unsigned(far_ids.size()), true);
        }
        s.expect(RsNodes, 2, "S and M are the store's nodes"); s.expect(RsStatics, 1, "only S is static in the store");
    } else if (is_importance) {
        std::vector<RetentionNode*> nodes;
        std::vector<Com<IDirect3DVertexBuffer9>> meshes(pool_importance_nodes);
        for (unsigned i = 0; i < pool_importance_nodes; ++i) {
            api(f.d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &meshes[i].p, nullptr), "CreateVertexBuffer scaled node");
            pool_fill_scaled(meshes[i].p, shadow_tri_a, pool_scales[i]);
            nodes.push_back(&s.make(1 + i, 'A', .3f, .05f + .05f * float(i), meshes[i].p));
            p.scale_of[1 + i] = pool_scales[i];
        }
        for (unsigned frame = 0; frame < pool_importance_frames; ++frame) {
            std::vector<RetentionNode*> order;
            for (unsigned k = 0; k < pool_importance_nodes; ++k) order.push_back(nodes[(k + frame) % pool_importance_nodes]);
            std::vector<unsigned> all{0}, far_ids;
            for (unsigned i = 1; i <= pool_importance_nodes; ++i) all.push_back(i);
            if (frame == 0) far_ids = {0, 1, 2, 3}; else far_ids = {4, 5, 6, 7}; // sizes unknown: the lowest serials; known: the largest scales
            p.frame(order, {all, far_ids}, 1 + pool_importance_nodes, 4, 1 + pool_importance_nodes - 4, 0, true);
        }
        for (auto& m : meshes) m.reset();
    } else {
        Com<IDirect3DVertexBuffer9> mesh; s.make_buffer('B', mesh);
        std::vector<RetentionNode*> nodes;
        for (unsigned i = 0; i < pool_records_nodes; ++i) nodes.push_back(&s.make(1 + i, 'B', 16.f + 12.8f * float(i) / float(pool_records_nodes - 1), .05f, mesh.p)); // view x 20..36: inside cascade 1 only
        for (unsigned frame = 0; frame < pool_records_frames; ++frame) p.frame(nodes, {}, 1, pool_records_nodes, 1, 0, false);
        mesh.reset();
    }
    std::printf("POOL_END script=%s frames=%llu\n", script, f.frame);
    f.camera_scripted = false;
}
} // namespace
