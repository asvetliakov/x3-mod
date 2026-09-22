// Far-cascade caster pool control script ("shadowpool" mode;
// docs/architecture/shadow-cascade-extents.md, "Caster pool control"). The
// seam DLL with two cascades narrowed to the unit-size geometry (8 and 40
// units), the retention script's world-placed camera and synthetic nodes
// (RetentionScript: serial, handle, class bits born in the seam's observer;
// the anchor node of class 0x20 first on every frame). X3M_FIXTURE_SHADOW_POOL:
//   static      X3M_SHADOW_CASCADE_STATIC_FROM=1: a static node S, a node M
//               moving 1/16 of a row unit per frame (0.078 units > eps 0.05)
//               and W, a 200-unit sliver below them moving the same way. All
//               shadow cascade 0 every frame; cascade 1 takes the anchor and S
//               once they are classified static (from frame 1 by the ring:
//               their rows equal the previous sighting's; under a retention
//               store S waits for its eight verified sightings, frame 9), never
//               M, and W from frame 1 exactly when X3M_SHADOW_CASCADE_LARGE_MIN
//               is set below 200 (large_admitted1). Run with the store off,
//               census and live, and once strict (no large_min).
//   importance  X3M_SHADOW_CASCADE_DROP_ORDER=importance, cascade 1 capped at
//               4: seven nodes of one shape with scales 1.5 .. 0.13 (ids 3 and
//               4 share 0.44) plus the anchor, submitted in an order rotated
//               every frame. Frame 0 (no extents: every size 0) keeps the four
//               lowest serials; every later frame keeps 5, 6, 7 and, at the
//               boundary, 3: equal to 4 on odd frames (the serial tie-break)
//               and a few percent smaller on even frames (t + 0.02), where the
//               hysteresis keeps it. dropped_min_size1 is 4's size, identical
//               on every frame (4 never moves).
//   records     X3M_SHADOW_CASCADE_RECORDS=1024,4096 with cascade 1 capped at
//               4,095 under the importance order: 4,095 nodes on one mesh
//               inside cascade 1 only (view x 20..36 units) plus the anchor:
//               4,096 records every frame (the list is full, nothing
//               overflows), the smallest (the farthest node) dropped, 4,096
//               issues over the budget 640 so the far cascade replays on even
//               frames only. Counters only (no map twin at this count).
// The run 40 A (run116) cases (docs/verification/directional-shadows.md, "Run 40 A (run116)
// diagnosis"), all with X3M_SHADOW_CASCADE_STATIC_FROM=1 but `hull`:
//   cycle       a static node S (both cascades) and S2 meeting cascade 1 alone
//               (view x 20: inside 40, outside 8), under a census or live store.
//               Cause 1: before the fix S2 alternated between admitted (the
//               ring: static) and refused (the store's fresh node: moving),
//               unseen and dropped (new_nodes / moving_dropped 1 on alternate
//               frames, period 2). Now every frame from 1 admits the anchor, S
//               and S2 to cascade 1 (the store defers to the ring until its
//               eight verified sightings, the refused frame 0 is a sighting
//               too), the store never drops a node, both are promoted by frame 9.
//               M2 (cascade 1 alone, moving) is refused every frame and seen
//               every frame through the refused-draw path (gate_sightings=),
//               whose cost the case records (X3M_SHADOW_RETENTION_TIMING=1).
//   jitter      X3M_FIXTURE_SHADOW_CASCADES=8,200: cascade 1's texel is 400 / 256
//               = 1.5625 units, its eps texel / 8 = 0.195; a node J (B at eight
//               times its size, so the map twin covers texels) at both
//               cascades jitters 0.125 row units (0.156 world units) on alternate frames (beyond the
//               base eps 0.05: moving at cascade 0's law, which admits movers;
//               within cascade 1's). Cause 3: J is refused from cascade 1 by the
//               base eps before the fix, admitted from frame 1 after it (the
//               ring, then the store's tier-1 promotion on frame 9 under a live store).
//   hull        no static rule: the 200-unit sliver W whose object origin lies
//               2 units BEHIND the camera plane (rows w0 = -2; its vertices
//               straddle the plane). Cause 4: the origin distance is unknown,
//               so before the fix the near gate refused W from every cascade
//               although its AABB meets both boxes; now its extent admits it
//               to both from frame 1 (frame 0 has no extent yet: POOL_EXPECT
//               carries leased=).
// Per frame: SHADOW_CAMERA / SHADOW_SUN / SHADOW_DRAW (with scale= and w0=) for the
// runner's CPU twin of every compared map, POOL_KEPT per cascade (the casters
// the map must show) and POOL_EXPECT (the counter line's values).
namespace {
constexpr unsigned pool_static_frames = 14, pool_importance_frames = 8, pool_records_frames = 6, pool_cycle_frames = 12, pool_jitter_frames = 12, pool_hull_frames = 6;
constexpr unsigned pool_footprint_frames = 6, pool_footprint_nodes = 4;
// Under a live store the script runs longer and stops drawing one sub-threshold caster once it is
// promoted (shadow_retention::static_sightings = 8 verified sightings: static from frame 9), so the
// store retains it and re-issues it: the gate refuses it from cascade 1 on every retained frame
// (footprint_aged1) while cascade 0 keeps it. Those frames compare no map (a retained record is not
// in the frame's draw list; the retention script owns that twin).
constexpr unsigned pool_footprint_retained_frames = 14, pool_footprint_retain_from = 10, pool_footprint_retained_id = 2;
constexpr float pool_footprint_scales[pool_footprint_nodes] = {1.f, 3.f, 10.f, 30.f};
// The largest LATERAL side of one caster's sun-space box, per unit of scale: the scaled B
// triangle's object AABB (x over 0.6 x scale, y over 0.6 x scale, z constant) through the
// script's rows (t, p = 0.125) and the scripted camera (yaw 0, m00 0.8, m11 4/3, the run111
// eye) into the fixture sun's basis gives sides 0.5834 and 0.6236 per unit of scale, so the
// measure is 0.6236 x scale units; every caster sits at t = its scale, which keeps its box
// inside both cascade boxes (x within +-19, y within +-14 of the snapped centres).
constexpr float pool_footprint_lateral_per_scale = .6236f;
constexpr float pool_jitter_step = .125f;  // dyadic row units, 0.156 world units (/ m00 0.8): beyond eps 0.05, within cascade 1's texel / 8 at extent 200 (0.195)
constexpr float pool_hull_w0 = -2.f;       // the sliver's origin 2 units behind the camera plane (fade_route::origin_distance fails: d = -1)
constexpr unsigned pool_store_static_frame = 9; // shadow_retention::static_sightings = 8 verified sightings: promoted at the scene end of frame 8
constexpr float pool_moving_step = .0625f;      // dyadic: exactly representable rows
constexpr unsigned pool_importance_nodes = 7, pool_records_nodes = 4095;
constexpr float pool_scales[pool_importance_nodes] = {.13f, .2f, .44f, .44f, .67f, 1.f, 1.5f}; // id i has scale pool_scales[i - 1]: the lowest serials are the smallest; 3 and 4 tie
constexpr float pool_boundary_step = .02f; // id 3's row offset on even frames: a few percent farther, inside the hysteresis band
constexpr float shadow_tri_w[3][2] = {{-100, -5}, {100, -5}, {0, -4.5f}}; // W: 200 units across, below every unit caster (shadow_replay_depth.py shape W)
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
    // `leased`: the counter line's leased= when it differs from the draws (a draw refused from every cascade; -1: every draw).
    void frame(const std::vector<RetentionNode*>& listed, const std::vector<std::vector<unsigned>>& kept, unsigned c0, unsigned c1, unsigned capped1, unsigned refused1, bool compare, unsigned large1 = 0, int leased = -1,
               unsigned footprint0 = 0, unsigned footprint1 = 0, unsigned aged1 = 0) {
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
            if (compare) std::printf("SHADOW_DRAW frame=%llu caster=%u shape=%c t=%.9g p=%.9g zo=%.9g scale=%.9g w0=%.9g\n", now, n->id, n->shape, n->t, n->p, n->zo, scale == scale_of.end() ? 1. : double(scale->second), double(n->w0));
            f.rows_w = n->w0;
            f.draw(n->object, n->t, n->p, n->zo, known, known, matched, Alter::None, false);
            f.rows_w = 1.f;
            n->last_drawn = static_cast<long long>(now);
        }
        std::printf("POOL_FRAME frame=%llu case=%s drawn=%u compare=%u\n", now, name, unsigned(drawn.size()), unsigned(compare));
        std::printf("POOL_EXPECT frame=%llu c0=%u c1=%u capped1=%u static_only_refused1=%u large_admitted1=%u leased=%d footprint_refused0=%u footprint_refused1=%u footprint_aged1=%u\n",
                    now, c0, c1, capped1, refused1, large1, leased, footprint0, footprint1, aged1);
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
    const bool is_cycle = !std::strcmp(script, "cycle"), is_jitter = !std::strcmp(script, "jitter"), is_hull = !std::strcmp(script, "hull");
    const bool is_footprint = !std::strcmp(script, "footprint");
    require(is_static || is_importance || is_records || is_cycle || is_jitter || is_hull || is_footprint, "X3M_FIXTURE_SHADOW_POOL is static, importance, records, cycle, jitter, hull or footprint");
    std::printf("POOL_MODE script=%s store=%u\n", script, s.mode);
    s.lifetime(7, 0, 0); s.lifetime(3, f.b.scope.load_epoch, f.b.scope.registry_epoch);
    std::memcpy(f.camera_position, retention_eye, sizeof retention_eye); f.camera_yaw = 0;
    shadow_ensure_bloom(f);
    s.read(); require(s.s[RsEnabled] == (s.on() ? 1u : 0u) && s.s[RsMode] == s.mode, "the store exists exactly while the option is on");
    s.anchor = &s.make(0, 'B', 1.5f, .3f, nullptr, 0, 0x20); // never retained (class 0x20): the ring classifies it under every store setting
    PoolScript p{s, f, {}, script};
    if (is_static) {
        auto& fixed = s.make(1, 'B', -.05f, .05f); auto& mover = s.make(2, 'B', .375f, .10f);
        Com<IDirect3DVertexBuffer9> wide_mesh;
        api(f.d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &wide_mesh.p, nullptr), "CreateVertexBuffer wide node");
        pool_fill_scaled(wide_mesh.p, shadow_tri_w, 1.f);
        auto& wide = s.make(3, 'W', .2f, .15f, wide_mesh.p); // a 200-unit moving sliver: the capital-hull stand-in
        float large_min = 0.f;
        if (GetEnvironmentVariableA("X3M_SHADOW_CASCADE_LARGE_MIN", setting, sizeof setting) > 0) large_min = std::strtof(setting, nullptr);
        const bool wide_admitted = large_min > 0.f && large_min <= 200.f; // W's extent is 200 units (its box test knows it from frame 1)
        std::printf("POOL_LARGE_MIN units=%.9g wide_admitted=%u\n", double(large_min), unsigned(wide_admitted));
        // S enters cascade 1 on frame 1 under every store setting: the store defers to the ring until its eight
        // verified sightings promote S (frame 9; the ring's anchor from frame 0 already answers static).
        const unsigned fixed_from = 1u;
        for (unsigned frame = 0; frame < pool_static_frames; ++frame) {
            mover.t = .375f + pool_moving_step * float(frame);
            wide.t = .2f + pool_moving_step * float(frame);
            std::vector<unsigned> far_ids; // (`far` is a Win16 macro)
            if (frame >= 1) far_ids.push_back(0);
            if (frame >= fixed_from) far_ids.push_back(1);
            const unsigned large = wide_admitted && frame >= 1 ? 1u : 0u;
            if (large) far_ids.push_back(3);
            p.frame({&fixed, &mover, &wide}, {{0, 1, 2, 3}, far_ids}, 4, unsigned(far_ids.size()), 0, 4 - unsigned(far_ids.size()), true, large);
        }
        s.expect(RsNodes, 3, "S, M and W are the store's nodes"); s.expect(RsStatics, 1, "only S is static in the store");
        s.expect(RsPromoted, 1, "S was promoted once (frame 8's scene end)");
        wide_mesh.reset();
    } else if (is_cycle) {
        require(s.on(), "the cycle script runs under a census or live store (the cycle is the store's)");
        auto& fixed = s.make(1, 'B', -.05f, .05f);   // S: both cascades
        auto& alone = s.make(2, 'B', 16.f, .05f);    // S2: view x 20, cascade 1 alone (as the records script's nodes)
        auto& mover = s.make(3, 'B', 18.f, .05f);    // M2: cascade 1 alone, moving 0.078 units per frame: refused every frame, a sighting every frame (the refused-draw path's steady state)
        for (unsigned frame = 0; frame < pool_cycle_frames; ++frame) {
            mover.t = 18.f + pool_moving_step * float(frame);
            std::vector<unsigned> far_ids;
            if (frame >= 1) far_ids = {0, 1, 2};
            p.frame({&fixed, &alone, &mover}, {{0, 1}, far_ids}, 2, unsigned(far_ids.size()), 0, 4 - unsigned(far_ids.size()), true, 0, frame ? 3 : 2); // frame 0: S2 and M2 are refused from their only cascade (sightings, not candidates)
            s.read();
            s.expect(RsMovingDropped, 0, "no node is dropped as moving: the refused frames and every admitted frame are sightings");
            s.expect(RsNodes, 3, "S, S2 and M2 are the store's nodes on every frame (the anchor's class is excluded)");
            s.expect(RsStatics, frame >= pool_store_static_frame - 1 ? 2 : 0, "S and S2 are promoted at frame 8's scene end; M2 never");
        }
        s.expect(RsPromoted, 2, "S and S2 were promoted once each");
    } else if (is_jitter) {
        Com<IDirect3DVertexBuffer9> big_mesh; // B at eight times its size: 4.8 units across, about three texels of cascade 1's 1.56-unit map
        api(f.d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &big_mesh.p, nullptr), "CreateVertexBuffer jitter node");
        pool_fill_scaled(big_mesh.p, shadow_tri_b, 8.f);
        auto& jitter = s.make(1, 'B', -.05f, .05f, big_mesh.p); // J: both cascades, 0.125 units to and fro
        p.scale_of[1] = 8.f;
        for (unsigned frame = 0; frame < pool_jitter_frames; ++frame) {
            jitter.t = -.05f + (frame % 2 ? pool_jitter_step : 0.f);
            std::vector<unsigned> far_ids;
            if (frame >= 1) far_ids = {0, 1};
            p.frame({&jitter}, {{0, 1}, far_ids}, 2, unsigned(far_ids.size()), 0, 2 - unsigned(far_ids.size()), true);
            s.read();
            s.expect(RsMovingDropped, 0, "J is seen every frame (cascade 0 admits it): never dropped");
            s.expect(RsNodes, 1, "J is the store's node"); s.expect(RsStatics, 0, "J moves beyond the base eps: not static at cascade 0's law");
        }
        s.expect(RsPromoted, 0, "J is never promoted at the base tier (its tier-1 promotion is the static_mask, not is_static)");
        big_mesh.reset();
    } else if (is_hull) {
        Com<IDirect3DVertexBuffer9> wide_mesh;
        api(f.d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &wide_mesh.p, nullptr), "CreateVertexBuffer hull node");
        pool_fill_scaled(wide_mesh.p, shadow_tri_w, 1.f);
        auto& wide = s.make(3, 'W', .2f, .15f, wide_mesh.p); // W: 200 units across (both boxes), its origin behind the camera plane
        wide.w0 = pool_hull_w0;
        for (unsigned frame = 0; frame < pool_hull_frames; ++frame) {
            std::vector<unsigned> ids{0};
            if (frame >= 1) ids.push_back(3); // frame 0: no extent yet and no origin rule (the origin is behind the camera)
            p.frame({&wide}, {ids, ids}, unsigned(ids.size()), unsigned(ids.size()), 0, 0, true, 0, int(ids.size()));
        }
        wide_mesh.reset();
    } else if (is_importance) {
        std::vector<RetentionNode*> nodes;
        std::vector<Com<IDirect3DVertexBuffer9>> meshes(pool_importance_nodes);
        for (unsigned i = 0; i < pool_importance_nodes; ++i) {
            api(f.d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &meshes[i].p, nullptr), "CreateVertexBuffer scaled node");
            pool_fill_scaled(meshes[i].p, shadow_tri_a, pool_scales[i]);
            nodes.push_back(&s.make(1 + i, 'A', .3f, i == 3 ? .5f : .05f + .05f * float(i), meshes[i].p)); // id 4 well behind id 3: the twin tells them apart by depth
            p.scale_of[1 + i] = pool_scales[i];
        }
        for (unsigned frame = 0; frame < pool_importance_frames; ++frame) {
            std::vector<RetentionNode*> order;
            for (unsigned k = 0; k < pool_importance_nodes; ++k) order.push_back(nodes[(k + frame) % pool_importance_nodes]);
            nodes[2]->t = .3f + (frame % 2 == 0 ? pool_boundary_step : 0.f); // id 3: farther on even frames
            std::vector<unsigned> all{0}, far_ids;
            for (unsigned i = 1; i <= pool_importance_nodes; ++i) all.push_back(i);
            if (frame == 0) far_ids = {0, 1, 2, 3}; else far_ids = {3, 5, 6, 7}; // sizes unknown: the lowest serials; known: the largest scales, 3 held at the boundary
            p.frame(order, {all, far_ids}, 1 + pool_importance_nodes, 4, 1 + pool_importance_nodes - 4, 0, true);
        }
        for (auto& m : meshes) m.reset();
    } else if (is_footprint) {
        // Minimum light-space footprint (docs/architecture/shadow-cascades.md, "Minimum caster
        // footprint"): four casters of the B shape at scales 1, 3, 10 and 30 plus the anchor
        // (scale 1), measured 0.62 / 1.87 / 6.24 / 18.71 units across in light space
        // (pool_footprint_lateral_per_scale). The thresholds of the two cascades at extents
        // 8 / 40, 256-texel maps, m00 0.8 and the fixture's 64-pixel back buffer (Fixture::W)
        // are 3 texels = 0.1875 u on cascade 0 (no cascade below it, so the texel floor alone)
        // and max(P x 0.95 x 8 x 2 / (0.8 x 64), 0.9375) u on cascade 1: 2.375 u at P = 8 (the
        // anchor and the two smallest casters, 0.62 and 1.87 u, leave cascade 1) and 7.125 u at
        // P = 24 (the 6.24-u one as well). Nothing ever leaves cascade 0, and frame 0 has no
        // extent read yet, so it has no measure at all: every draw keeps the cascades the origin
        // rule gave it.
        char cascades[32]{};
        require(GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_CASCADES", cascades, sizeof cascades) > 0 && !std::strcmp(cascades, "8,40"),
                "the footprint script runs on the 8 / 40 cascade pair");
        float px = 0.f;
        if (GetEnvironmentVariableA("X3M_SHADOW_CASCADE_MIN_FOOTPRINT", setting, sizeof setting) > 0) px = std::strtof(setting, nullptr);
        const float min0 = 3.f * 2.f * 8.f / 256.f;
        const float screen = px * .95f * 8.f * 2.f / (.8f * float(Fixture::W)), texel1 = 3.f * 2.f * 40.f / 256.f;
        const float min1 = px > 0.f ? (screen > texel1 ? screen : texel1) : 0.f;
        std::printf("POOL_FOOTPRINT px=%.9g width=%u min0=%.9g min1=%.9g\n", double(px), unsigned(Fixture::W), double(min0), double(min1));
        std::vector<RetentionNode*> listed;
        std::vector<Com<IDirect3DVertexBuffer9>> meshes(pool_footprint_nodes);
        for (unsigned i = 0; i < pool_footprint_nodes; ++i) {
            const float scale = pool_footprint_scales[i];
            api(f.d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &meshes[i].p, nullptr), "CreateVertexBuffer footprint node");
            pool_fill_scaled(meshes[i].p, shadow_tri_b, scale);
            listed.push_back(&s.make(1 + i, 'B', scale, .05f, meshes[i].p));
            p.scale_of[1 + i] = scale;
        }
        const auto lateral = [](float scale) { return pool_footprint_lateral_per_scale * scale; };
        const bool retains = s.on() && s.mode == 2; // a live store retains the withdrawn caster
        const unsigned frames = retains ? pool_footprint_retained_frames : pool_footprint_frames;
        std::printf("POOL_FOOTPRINT_RETAIN retains=%u from=%u id=%u\n", unsigned(retains), pool_footprint_retain_from, pool_footprint_retained_id);
        // Frame 0 has no extent read yet, so the origin rule alone decides: a draw whose object
        // origin (view x = t / m00) is within a cascade's half-extent meets it. The anchor (t 1.5)
        // and the casters at t 1 and 3 are within 8; every one of them is within 40. From frame 1
        // the box test admits all five to both cascades (each box straddles both) and the gate
        // decides cascade 1 alone.
        for (unsigned frame = 0; frame < frames; ++frame) {
            const bool measured = frame >= 1;
            const bool withdrawn = retains && frame >= pool_footprint_retain_from; // the retained caster is not submitted
            const auto origin = [](float t) { return t / .8f; };
            std::vector<unsigned> near_ids, far_ids;
            const float ts[1 + pool_footprint_nodes] = {1.5f, pool_footprint_scales[0], pool_footprint_scales[1], pool_footprint_scales[2], pool_footprint_scales[3]};
            const float scales[1 + pool_footprint_nodes] = {1.f, pool_footprint_scales[0], pool_footprint_scales[1], pool_footprint_scales[2], pool_footprint_scales[3]};
            std::vector<RetentionNode*> drawn;
            for (unsigned i = 0; i < pool_footprint_nodes; ++i) if (!withdrawn || 1 + i != pool_footprint_retained_id) drawn.push_back(listed[i]);
            unsigned live = 0, refused1 = 0;
            for (unsigned i = 0; i < 1 + pool_footprint_nodes; ++i) {
                if (withdrawn && i == pool_footprint_retained_id) continue; // not submitted: no live record
                ++live;
                if (measured || origin(ts[i]) <= 8.f) near_ids.push_back(i);
                const bool met = measured || origin(ts[i]) <= 40.f;
                if (met && (!measured || lateral(scales[i]) >= min1)) far_ids.push_back(i);
            }
            if (measured) refused1 = live - unsigned(far_ids.size());
            // The retained record takes the same gate on re-issue: refused from cascade 1 exactly
            // while the option is on (its measure is 1.87 u, below every threshold this case uses).
            const unsigned aged1 = withdrawn && min1 > 0.f && lateral(pool_footprint_scales[pool_footprint_retained_id - 1]) < min1 ? 1u : 0u;
            p.frame(drawn, {near_ids, far_ids}, unsigned(near_ids.size()), unsigned(far_ids.size()), 0, 0, !withdrawn, 0, -1, 0, refused1, aged1);
        }
        if (retains) {
            s.read();
            s.expect(RsNodes, pool_footprint_nodes, "the four casters are the store's nodes (the anchor's class is excluded)");
            s.expect(RsMovingDropped, 0, "no node is dropped as moving: nothing moves in this script");
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
