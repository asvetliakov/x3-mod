// Sun-shadow caster retention script ("shadowretention" mode;
// docs/architecture/shadow-caster-retention.md, "Fixture and twin cases").
// The seam DLL with two cascades narrowed to the unit-size geometry, the
// script's own camera (a yaw and a world position 81,000 units from the origin)
// and synthetic nodes: every node is an Object with its own scope identity
// (serial, handle, model, lod, class bits) born in the seam's synthetic
// lifetime observer. "Culled" = the script omits the node's draws. One script
// runs under three settings of the DLL: retention live, census, and off (the
// control of every case); it asserts the store's levels, counters and the
// COM reference counts of its own buffers through the seam, and prints per
// frame the live draws (SHADOW_DRAW) and the draws a live store must replay
// (RETENTION_KEPT, with the frame whose camera placed them) for the runner's
// CPU twin of each cascade map. COLOR lines carry the presented frame's hash:
// the three settings must present byte-identical frames.
namespace {
using RetentionStatsFn = unsigned (*)(IDirect3DDevice9*, std::uint64_t*, unsigned);
using RetentionLifetimeFn = void (*)(unsigned, std::uint64_t, std::uint64_t);
enum RetentionStat : unsigned { RsEnabled, RsMode, RsNodes, RsRecords, RsRefs, RsStatics, RsUnseen, RsRetainedIssues, RsRetired, RsBoxExit, RsAge, RsEvicted, RsBufferChanged, RsBufferGone,
                                RsBufferOrphaned, RsReclassified, RsLodReplaced, RsModelReplaced, RsMovingDropped, RsRevalidated, RsJournalOverflow, RsRefused, RsPromoted,
                                RsFlushNone, RsFlushEpoch, RsFlushReset, RsFlushDevice, RsFlushTeardown, RsFlushSun, RsFlushObserver, RsOrphanProbe, RsPending,
                                RsContextLost, RsFarAlternate, RsFlushIdle, RsReclassifiedAfterUnseen, RsCount };
constexpr unsigned retention_node_reserve = 8; // shadow_retention::node_reserve: the scene end keeps this many node slots free
constexpr double retention_eye[3] = {55962., 20286., 55517.}; // the run111 world offset
constexpr unsigned retention_settle = 10;                      // sightings: the ninth agreeing one makes a node static
struct RetentionNode {
    Object object{""}; Com<IDirect3DVertexBuffer9> buffer; char shape = 'B'; unsigned id = 0;
    float t = 0, p = .125f, zo = 0, w0 = 1.f; long long last_drawn = -2, placed_frame = -1; float placed_t = 0; // w0: the rows' w constant (Fixture::rows_w)
};
ULONG retention_count(IUnknown* object) { object->AddRef(); return object->Release(); }
struct RetentionScript {
    Fixture& f; RetentionStatsFn stats_fn; RetentionLifetimeFn lifetime; ShadowCascadeReadbackFn readback;
    unsigned mode = 0; // 0 off, 1 census, 2 live
    std::vector<std::unique_ptr<RetentionNode>> nodes;
    const float* sun = shadow_sun;
    std::uint64_t s[RsCount]{};
    const char* name = "";
    bool check_issues = true; // off while a drop is pending or the kept list is not spelled out (bulk frames)
    bool adaptive = false;    // this frame's kept list holds only if the store still issued it (a drop may land on this very frame)
    bool live() const { return mode == 2; }
    bool on() const { return mode != 0; }
    RetentionScript(Fixture& fixture, RetentionStatsFn stats, RetentionLifetimeFn observer, ShadowCascadeReadbackFn maps) : f(fixture), stats_fn(stats), lifetime(observer), readback(maps) {}
    void make_buffer(char shape, Com<IDirect3DVertexBuffer9>& buffer) {
        api(f.d->CreateVertexBuffer(24 * 3, 0, 0, D3DPOOL_MANAGED, &buffer.p, nullptr), "CreateVertexBuffer node");
        fill(buffer.p, shape == 'A' ? shadow_tri_a : shadow_tri_b);
    }
    static void fill(IDirect3DVertexBuffer9* buffer, const float tri[3][2]) {
        void* dst = nullptr; api(buffer->Lock(0, 0, &dst, 0), "Lock node");
        for (UINT v = 0; v < 3; ++v) {
            unsigned short data[12] = {half(tri[v][0]), half(tri[v][1]), half(.5f), half(7), half(float(v & 1)), half(float(v >> 1)), 0, half(7), 0, 0, half(1), half(7)};
            std::memcpy(static_cast<char*>(dst) + v * 24, data, 24);
        }
        api(buffer->Unlock(), "Unlock node");
    }
    // A node: its own serial, handle and node pointer, born in the synthetic observer. `shared`: another node's buffer.
    RetentionNode& make(unsigned id, char shape, float t, float zo, IDirect3DVertexBuffer9* shared = nullptr, std::uint32_t lod = 0, std::uint32_t flags12c = 0, std::uint32_t flags130 = 0, unsigned identity = 0) {
        nodes.push_back(std::make_unique<RetentionNode>()); auto& n = *nodes.back();
        const unsigned who = identity ? identity : id;
        n.id = id; n.shape = shape; n.t = t; n.zo = zo;
        if (!shared) make_buffer(shape, n.buffer);
        n.object.name = shape == 'A' ? "A" : "B"; n.object.vb = shared ? shared : n.buffer.p; n.object.covers = shape == 'A' ? covers_a : covers_b;
        n.object.scope = f.b.scope;
        n.object.scope.node_serial = 1000 + who; n.object.scope.node = 0x100000 + 0x100 * who; n.object.scope.mesh = 0x800000 + 0x100 * id; n.object.scope.node_handle = 500 + who;
        n.object.scope.model = 0x40 + who; n.object.scope.lod = lod; n.object.scope.flags12c = flags12c; n.object.scope.flags130 = flags130;
        if (!identity && lifetime) lifetime(0, n.object.scope.node_handle, n.object.scope.node_serial);
        return n;
    }
    void read() { std::memset(s, 0, sizeof s); if (stats_fn) stats_fn(f.d.p, s, RsCount); }
    // Holds only under a store (census or live); `live_value` where the two differ.
    void expect(RetentionStat index, std::uint64_t value, const char* label) { if (on()) { if (s[index] != value) std::printf("RETENTION_WITNESS case=%s stat=%u value=%llu expected=%llu\n", name, unsigned(index), static_cast<unsigned long long>(s[index]), static_cast<unsigned long long>(value)); require(s[index] == value, label); } }
    void expect_live(RetentionStat index, std::uint64_t live_value, std::uint64_t census_value, const char* label) { expect(index, live() ? live_value : census_value, label); }
    // One frame: `drawn` submitted, `kept` what a live store replays although not submitted.
    // Every frame also submits the anchor first: a camera-facing-class node (flags12c & 0x20), routed and
    // replayed live like any draw but never retained, so the frame has a scene end (the route finds the bloom
    // copy only behind a scene draw) while the store's levels and references stay those of the case.
    void frame(const std::vector<RetentionNode*>& listed, const std::vector<RetentionNode*>& kept, bool compare, bool oracle = true) {
        std::vector<RetentionNode*> drawn{anchor}; drawn.insert(drawn.end(), listed.begin(), listed.end());
        f.camera_scripted = true;
        f.frame_begin();
        if (!poll) api(f.d->SetPixelShaderConstantF(4, sun, 1), "SetPixelShaderConstantF LightDir_Dir0");
        const auto& cam = f.camera_current;
        const unsigned long long now = f.frame;
        if (compare) {
            std::printf("SHADOW_CAMERA frame=%llu m00=%.9g m11=%.9g r=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g t=%.9g,%.9g,%.9g\n", now, cam.m00, cam.m11,
                        cam.r[0], cam.r[1], cam.r[2], cam.r[3], cam.r[4], cam.r[5], cam.r[6], cam.r[7], cam.r[8], cam.t[0], cam.t[1], cam.t[2]);
            // The effective sun: the script's constant, or under the point source the direction from the camera
            // position to the light (what the cascade centres and the nodes, a few units apart, see within 1e-4).
            double effective[3] = {sun[0], sun[1], sun[2]};
            if (poll) {
                double eye[3], length = 0;
                for (unsigned i = 0; i < 3; ++i) { eye[i] = 0; for (unsigned j = 0; j < 3; ++j) eye[i] -= double(cam.t[j]) * double(cam.r[i * 3 + j]); effective[i] = poll_light[i] - eye[i]; length += effective[i] * effective[i]; }
                for (double& v : effective) v /= std::sqrt(length);
            }
            std::printf("SHADOW_SUN frame=%llu direction=%.9g,%.9g,%.9g\n", now, effective[0], effective[1], effective[2]);
        }
        for (auto* n : drawn) {
            const bool known = n->object.scope.known != 0;
            const bool matched = known && n->last_drawn == static_cast<long long>(now) - 1 && f.frames_since_reset > 0;
            if (compare) std::printf("SHADOW_DRAW frame=%llu caster=%u shape=%c t=%.9g p=%.9g zo=%.9g\n", now, n->id, n->shape, n->t, n->p, n->zo);
            if (poll) upload_sun(*n);
            f.draw(n->object, n->t, n->p, n->zo, known, known, matched, Alter::None, oracle && drawn.size() <= 8);
            n->last_drawn = static_cast<long long>(now);
            if (known && (n->placed_frame < 0 || n->placed_t != n->t)) { n->placed_frame = static_cast<long long>(now); n->placed_t = n->t; camera_of[now] = cam; }
        }
        std::printf("RETENTION_FRAME frame=%llu case=%s mode=%u drawn=%u kept=%u compare=%u\n", now, name, mode, unsigned(drawn.size()), unsigned(kept.size()), unsigned(compare));
        const auto print_kept = [&] { if (compare) for (auto* n : kept) {
            const auto& c = camera_of.at(static_cast<unsigned long long>(n->placed_frame));
            std::printf("RETENTION_KEPT frame=%llu caster=%u shape=%c t=%.9g p=%.9g zo=%.9g placed_frame=%lld m00=%.9g m11=%.9g r=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g ct=%.9g,%.9g,%.9g\n", now, n->id, n->shape, n->t, n->p, n->zo, n->placed_frame,
                        c.m00, c.m11, c.r[0], c.r[1], c.r[2], c.r[3], c.r[4], c.r[5], c.r[6], c.r[7], c.r[8], c.t[0], c.t[1], c.t[2]);
        } };
        if (!adaptive) print_kept();
        shadow_bloom_copy(f);
        api(f.d->EndScene(), "EndScene");
        const auto image = f.color_image();
        std::printf("COLOR frame=%llu hash=%016llx\n", now, static_cast<unsigned long long>(Fixture::color_hash(image)));
        if (oracle) { f.verify_coverage(image); f.verify_motion(); }
        f.previous_presented = image;
        api(f.d->SetDepthStencilSurface(f.depth.p), "SetDepthStencilSurface rebind");
        api(f.d->Present(nullptr, nullptr, nullptr, nullptr), "Present");
        ++f.frame; ++f.frames_since_reset;
        if (compare) shadow_cascade_readback(f, readback, 2, now);
        read();
        if (adaptive) { // the drop may have landed on this frame: the kept list stands only if it was issued
            if (on()) require(s[RsRetainedIssues] == (live() ? 2 * kept.size() : 0) || s[RsRetainedIssues] == 0, "the kept records were issued whole or not at all");
            if (!live() || s[RsRetainedIssues]) print_kept();
        } else if (check_issues) expect_live(RsRetainedIssues, 2 * kept.size(), 0, "the replay issues every kept record into both cascades");
        expect(RsPending, 0, "no Release is owed after a scene end");
    }
    void settle(const std::vector<RetentionNode*>& drawn, const std::vector<RetentionNode*>& kept = {}, unsigned frames = retention_settle, bool oracle = true) { for (unsigned i = 0; i < frames; ++i) frame(drawn, kept, false, oracle); }
    void retire(RetentionNode& n) { if (lifetime) lifetime(1, n.object.scope.node_serial, 0); }
    void begin(const char* label) { name = label; std::printf("RETENTION_CASE name=%s state=begin frame=%llu\n", name, f.frame); }
    void end() { std::printf("RETENTION_CASE name=%s state=PASS frame=%llu\n", name, f.frame); }
    std::map<unsigned long long, x3m::renderer::CameraState> camera_of;
    RetentionNode* anchor = nullptr;
    // X3M_FIXTURE_SHADOW_POLL=agree: the sun is a point light 100,000 units along the script's sun from the
    // world origin; every draw uploads LightDir_Dir0 = normalize(light - its own origin) as the engine does.
    bool poll = false; double poll_light[3] = {0, 0, 0};
    void upload_sun(const RetentionNode& n) {
        if (!poll) { api(f.d->SetPixelShaderConstantF(4, sun, 1), "SetPixelShaderConstantF LightDir_Dir0"); return; }
        const auto& c = f.camera_current;
        const double view[3] = {double(n.t) / c.m00, 0., 1.}; // the object origin through the rows (t, 0, zo, 1)
        double world[3], direction[4] = {0, 0, 0, 0}, length = 0;
        for (unsigned i = 0; i < 3; ++i) { world[i] = 0; for (unsigned j = 0; j < 3; ++j) world[i] += (view[j] - double(c.t[j])) * double(c.r[i * 3 + j]); direction[i] = poll_light[i] - world[i]; length += direction[i] * direction[i]; }
        length = std::sqrt(length);
        float constant[4] = {float(direction[0] / length), float(direction[1] / length), float(direction[2] / length), 0.f};
        api(f.d->SetPixelShaderConstantF(4, constant, 1), "SetPixelShaderConstantF LightDir_Dir0 (point)");
    }
};
void run_shadow_retention_integration(Fixture& f) {
    require(f.seam && f.camera && f.enabled && !f.taa, "shadowretention runs on the seam DLL with the route and the scripted camera");
    char setting[16]{};
    const auto flag = [&](const char* variable) { return GetEnvironmentVariableA(variable, setting, sizeof setting) == 1 && setting[0] == '1'; };
    RetentionScript s(f, symbol<RetentionStatsFn>(f.runtime, "x3m_shadow_retention_fixture_stats", false), symbol<RetentionLifetimeFn>(f.runtime, "x3m_shadow_retention_fixture_lifetime", false),
                      symbol<ShadowCascadeReadbackFn>(f.runtime, "x3m_shadow_replay_fixture_cascade_readback", false));
    s.mode = flag("X3M_SHADOW_CASTER_RETENTION") ? 2 : flag("X3M_SHADOW_RETENTION_CENSUS") ? 1 : 0;
    require(s.stats_fn && s.lifetime && s.readback, "the seam DLL exports the retention seam");
    unsigned age_cap = 0; if (GetEnvironmentVariableA("X3M_SHADOW_CASTER_RETENTION_AGE", setting, sizeof setting) > 0) age_cap = unsigned(std::atoi(setting));
    require(age_cap >= 620 && age_cap <= 2000, "the script expects an age cap above case a's 600 frames");
    std::printf("RETENTION_MODE mode=%u age_cap=%u\n", s.mode, age_cap);
    s.lifetime(7, 0, 0); s.lifetime(3, f.b.scope.load_epoch, f.b.scope.registry_epoch);
    // The sun-position poll seam (the replay script's `agree` block): the DLL builds every cascade's basis from the polled light.
    static ShadowPollContext poll_context; // read by the DLL until the process ends
    char poll_mode[16]{};
    if (GetEnvironmentVariableA("X3M_FIXTURE_SHADOW_POLL", poll_mode, sizeof poll_mode) > 0) {
        require(!std::strcmp(poll_mode, "agree"), "the retention script runs the poll seam in its agree mode only");
        const auto install = symbol<void (*)(const std::uint32_t*)>(f.runtime, "x3m_sun_light_poll_fixture_install", false);
        require(install != nullptr, "the seam DLL exports the sun-position poll seam");
        for (unsigned k = 0; k < 3; ++k) s.poll_light[k] = shadow_poll_distance * double(shadow_sun[k]);
        poll_context.build(s.poll_light, false); install(&poll_context.slot); s.poll = true;
        std::printf("SHADOW_POLL mode=agree light=%.9g,%.9g,%.9g candidates=4 directional=3\n", s.poll_light[0], s.poll_light[1], s.poll_light[2]);
    }
    std::memcpy(f.camera_position, retention_eye, sizeof retention_eye); f.camera_yaw = 0;
    shadow_ensure_bloom(f);
    s.read(); require(s.s[RsEnabled] == (s.on() ? 1u : 0u) && s.s[RsMode] == s.mode, "the store exists exactly while the option is on");
    const std::uint64_t orphan_probe = s.s[RsOrphanProbe];
    std::printf("RETENTION_PROBE orphan_probe=%llu\n", static_cast<unsigned long long>(orphan_probe));
    s.anchor = &s.make(0, 'B', 1.5f, .3f, nullptr, 0, 0x20);
    const ULONG declaration_base = retention_count(f.declaration.p);
    const auto held = [&](ULONG base) { return base + (s.live() ? 1u : 0u); };

    { // a. the camera turns away from a static caster: it stays for 600 frames, also while inside the frustum and unsubmitted
        s.begin("a_turn_away");
        auto& n1 = s.make(1, 'B', -.05f, .05f); auto& n2 = s.make(2, 'B', .7f, .10f);
        const ULONG base1 = retention_count(n1.buffer.p), base2 = retention_count(n2.buffer.p);
        s.settle({&n1, &n2});
        s.expect(RsNodes, 2, "two nodes"); s.expect(RsStatics, 2, "both static after the settling sightings"); s.expect(RsRecords, 2, "one record each");
        s.expect_live(RsRefs, 3, 0, "one reference per distinct resource: two buffers and the declaration");
        require(retention_count(n1.buffer.p) == held(base1) && retention_count(n2.buffer.p) == held(base2) && retention_count(f.declaration.p) == held(declaration_base), "the store's own reference on each held resource, exactly one");
        for (unsigned i = 0; i < 3; ++i) s.frame({&n2}, {&n1}, true); // inside the frustum, unsubmitted (the size/distance-cull stand-in)
        s.expect(RsUnseen, 1, "one unseen node retained");
        for (unsigned i = 0; i < 600; ++i) { f.camera_yaw = 120. + .5 * i; s.frame({}, {&n1, &n2}, i < 3 || i % 100 == 0 || i == 599, i < 3 || i == 599); }
        s.expect(RsNodes, 2, "both retained through 600 unseen frames"); s.expect(RsUnseen, 2, "both unseen");
        require(retention_count(n1.buffer.p) == held(base1), "the reference is held, once, while retained");
        f.camera_yaw = 0;
        s.retire(n1); s.retire(n2); s.frame({}, {}, true);
        s.expect(RsNodes, 0, "retired nodes leave"); s.expect(RsRefs, 0, "no reference remains");
        require(retention_count(n1.buffer.p) == base1 && retention_count(n2.buffer.p) == base2 && retention_count(f.declaration.p) == declaration_base, "reference counts back to the baseline");
        s.end();
    }
    { // b. a moving node leaves on its first unseen frame; a static node moved while unseen shows once, at the new place
        s.begin("b_moving");
        auto& fixed = s.make(3, 'B', -.05f, .05f); auto& mover = s.make(4, 'B', .375f, .10f);
        const std::uint64_t dropped = s.s[RsMovingDropped], reclassified = s.s[RsReclassified];
        for (unsigned i = 0; i < retention_settle; ++i) { mover.t = .375f + .015625f * float(i); s.frame({&fixed, &mover}, {}, false); } // dyadic steps: the motion oracle's tolerance assumes exactly representable rows
        s.expect(RsStatics, 1, "the moving node never becomes static");
        s.frame({&fixed}, {}, true);
        s.expect(RsMovingDropped, dropped + 1, "the moving node is dropped on its first unseen frame"); s.expect(RsNodes, 1, "only the static node remains");
        for (unsigned i = 0; i < 3; ++i) s.frame({}, {&fixed}, i == 0);
        fixed.t = .55f; s.frame({&fixed}, {}, true); // moved while unseen: one blob, at the new place
        s.expect(RsReclassified, reclassified + 1, "rows beyond eps on resubmission reclassify the node");
        s.retire(fixed); s.retire(mover); s.frame({}, {}, false); s.expect(RsNodes, 0, "case b leaves nothing");
        s.end();
    }
    { // c. destruction through the journal, then through an overflow's full revalidation
        s.begin("c_retired");
        auto& n5 = s.make(5, 'B', -.05f, .05f); auto& n6 = s.make(6, 'B', .7f, .10f);
        const ULONG base5 = retention_count(n5.buffer.p), base6 = retention_count(n6.buffer.p);
        s.settle({&n5, &n6}); s.frame({}, {&n5, &n6}, true);
        const std::uint64_t retired = s.s[RsRetired], overflow = s.s[RsJournalOverflow], revalidated = s.s[RsRevalidated];
        s.retire(n5); s.frame({}, {&n6}, true);
        s.expect(RsRetired, retired + 1, "the retired serial is gone before the next replay"); s.expect(RsNodes, 1, "the other node stays");
        require(retention_count(n5.buffer.p) == base5 && retention_count(n6.buffer.p) == held(base6), "the retired node's references are released, the other's held");
        s.lifetime(2, n6.object.scope.node_serial, 0); s.lifetime(6, 2049, 0); // the node dies without an entry that survives: 2,049 entries between drains
        s.frame({}, {}, true);
        s.expect(RsJournalOverflow, overflow + 1, "2,049 entries between drains overflow the ring"); s.expect(RsNodes, 0, "the full revalidation flushes the dead node");
        if (s.on()) require(s.s[RsRevalidated] > revalidated, "every retained node was revalidated");
        require(retention_count(n6.buffer.p) == base6, "reference counts back to the baseline after the revalidation");
        s.end();
    }
    { // d. LOD swap on a stable serial: the new mesh only
        s.begin("d_lod_swap");
        auto& lod0 = s.make(7, 'A', .8f, 0.f); auto& partner = s.make(8, 'B', -.05f, .05f); // the base script's known-good rows (the depth oracle's tolerance)
        auto& lod1 = s.make(9, 'B', .8f, 0.f, nullptr, 1, 0, 0, 7); // node 7's identity with another buffer and lod 1
        lod1.object.scope.model = lod0.object.scope.model;
        const ULONG base0 = retention_count(lod0.buffer.p), base1 = retention_count(lod1.buffer.p);
        const std::uint64_t replaced = s.s[RsLodReplaced];
        s.settle({&lod0, &partner}); s.expect(RsRecords, 2, "one record per node");
        s.frame({&lod1, &partner}, {}, true);
        s.expect(RsLodReplaced, replaced + 1, "the resubmission with another lod replaces the draw set"); s.expect(RsRecords, 2, "replaced, not added"); s.expect(RsNodes, 2, "the same node");
        require(retention_count(lod0.buffer.p) == base0 && retention_count(lod1.buffer.p) == held(base1), "the old mesh's reference is released, the new one's held");
        auto& other_model = s.make(29, 'B', .8f, 0.f, nullptr, 1, 0, 0, 7); other_model.object.scope.model = lod0.object.scope.model + 0x100; // the same serial with another model
        const std::uint64_t models = s.s[RsModelReplaced]; const ULONG base_other = retention_count(other_model.buffer.p);
        s.frame({&other_model, &partner}, {}, true);
        s.expect(RsModelReplaced, models + 1, "the resubmission with another model replaces the draw set"); s.expect(RsRecords, 2, "replaced, not added");
        require(retention_count(lod1.buffer.p) == base1 && retention_count(other_model.buffer.p) == held(base_other), "the lod-1 mesh is released, the new model's mesh held");
        s.retire(lod0); s.retire(partner); s.frame({}, {}, false); s.expect(RsNodes, 0, "case d leaves nothing");
        s.end();
    }
    { // e. one mesh, two nodes: two blobs, one reference per resource
        s.begin("e_shared_mesh");
        auto& n10 = s.make(10, 'B', -.05f, .05f); auto& n11 = s.make(11, 'B', .7f, .10f, n10.buffer.p);
        const ULONG base = retention_count(n10.buffer.p);
        s.settle({&n10, &n11}); s.frame({}, {&n10, &n11}, true);
        s.expect(RsRecords, 2, "two records"); s.expect_live(RsRefs, 2, 0, "one buffer and the declaration"); require(retention_count(n10.buffer.p) == held(base), "one reference for two records");
        s.retire(n10); s.frame({}, {&n11}, true);
        s.expect(RsNodes, 1, "the other node keeps its record"); require(retention_count(n10.buffer.p) == held(base), "the shared buffer stays held");
        s.retire(n11); s.frame({}, {}, false); require(retention_count(n10.buffer.p) == base, "released with the last record");
        s.end();
    }
    { // f. a writable Lock of a held unseen buffer: dropped within 8 frames
        s.begin("f_buffer_lock");
        auto& n12 = s.make(12, 'B', -.05f, .05f); auto& n13 = s.make(13, 'B', .7f, .10f);
        const ULONG base = retention_count(n12.buffer.p); const std::uint64_t changed = s.s[RsBufferChanged];
        s.settle({&n12, &n13}); s.frame({&n13}, {&n12}, true);
        RetentionScript::fill(n12.buffer.p, shadow_tri_b);
        unsigned frames = 0; s.check_issues = false; // the twin is not asked while the drop is pending; every setting runs the same eight frames
        for (unsigned i = 1; i <= 8; ++i) { s.frame({&n13}, {}, false, false); if (!frames && s.s[RsNodes] != 2) frames = i; }
        s.check_issues = true;
        std::printf("RETENTION_LOCK_DROP frames=%u\n", frames);
        if (s.on()) require(frames >= 1 && frames <= 8 && s.s[RsBufferChanged] == changed + 1 && s.s[RsNodes] == 1, "the rewritten buffer's record is dropped within 8 frames");
        s.frame({&n13}, {}, true);
        require(retention_count(n12.buffer.p) == base, "its reference is released");
        s.retire(n12); s.retire(n13); s.frame({}, {}, false); s.expect(RsNodes, 0, "case f leaves nothing");
        s.end();
    }
    { // g. the engine releases the mesh before (or without) the retirement
        s.begin("g_release_first");
        auto& n14 = s.make(14, 'B', -.05f, .05f); auto& n15 = s.make(15, 'B', .7f, .10f);
        s.settle({&n14, &n15}); s.frame({&n15}, {&n14}, true);
        const std::uint64_t orphaned = s.s[RsBufferOrphaned], gone = s.s[RsBufferGone];
        const IDirect3DVertexBuffer9* old = n14.buffer.p;
        n14.buffer.reset(); n14.object.vb = nullptr; // the application's last reference
        Com<IDirect3DVertexBuffer9> successor; s.make_buffer('B', successor);
        if (s.live()) require(successor.p != old, "a held buffer's allocation cannot be re-issued");
        s.adaptive = true; s.frame({&n15}, {&n14}, true); s.adaptive = false; // the intervening replay uses the store's own reference
        unsigned frames = s.on() && s.s[RsNodes] != 2 ? 1 : 0; s.check_issues = false;
        for (unsigned i = 2; i <= 8; ++i) { s.frame({&n15}, {}, false, false); if (!frames && s.s[RsNodes] != 2) frames = i; }
        s.check_issues = true;
        std::printf("RETENTION_ORPHAN_DROP frames=%u orphan_probe=%llu\n", frames, static_cast<unsigned long long>(orphan_probe));
        if (s.live() && orphan_probe) require(frames >= 1 && frames <= 8 && s.s[RsBufferOrphaned] == orphaned + 1 && s.s[RsNodes] == 1, "the last reference being the store's drops the record within 8 frames");
        if (s.mode == 1) require(frames >= 1 && frames <= 8 && s.s[RsBufferGone] == gone + 1 && s.s[RsNodes] == 1, "the census counts the vanished buffer");
        s.retire(n14); s.retire(n15); s.frame({}, {}, false); s.expect(RsNodes, 0, "case g leaves nothing"); s.expect(RsRefs, 0, "no reference remains");
        s.end();
    }
    { // h. Reset and a failed Reset: every reference released before the native call, the store empty; retention resumes
        s.begin("h_reset");
        auto& n16 = s.make(16, 'B', -.05f, .05f); auto& n17 = s.make(17, 'B', .7f, .10f);
        const ULONG base16 = retention_count(n16.buffer.p);
        s.settle({&n16, &n17}); s.frame({}, {&n16, &n17}, true);
        std::uint64_t resets = s.s[RsFlushReset];
        f.reset(); shadow_ensure_bloom(f); s.read();
        s.expect(RsNodes, 0, "the store is empty after Reset"); s.expect(RsRefs, 0, "no reference survives Reset"); s.expect(RsFlushReset, resets + 1, "flush=reset");
        require(retention_count(n16.buffer.p) == base16 && retention_count(f.declaration.p) == declaration_base, "reference counts at the baseline after Reset");
        s.frame({}, {}, true); // nothing is retained on the first frame after Reset
        s.settle({&n16, &n17}); s.frame({}, {&n16, &n17}, true);
        require(retention_count(n16.buffer.p) == held(base16), "retention resumes on resubmission");
        // A Reset that fails: a default-pool buffer of the fixture's own is still alive.
        resets = s.s[RsFlushReset];
        Com<IDirect3DVertexBuffer9> control; api(f.d->CreateVertexBuffer(64, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &control.p, nullptr), "CreateVertexBuffer default-pool control");
        f.back.reset(); f.depth.reset(); f.bloom_surface.reset(); f.bloom.reset();
        for (UINT i = 0; i < 4; ++i) api(f.d->SetTexture(i, nullptr), "SetTexture null");
        api(f.d->SetStreamSource(0, nullptr, 0, 0), "SetStreamSource null");
        const HRESULT failed = f.d->Reset(&f.pp);
        std::printf("RETENTION_FAILED_RESET result=%08lx\n", failed);
        s.read();
        s.expect(RsFlushReset, resets + 1, "a Reset attempt flushes, successful or not"); s.expect(RsRefs, 0, "no reference survives a failed Reset");
        require(retention_count(n16.buffer.p) == base16, "reference counts at the baseline after the failed Reset");
        control.reset();
        if (FAILED(failed)) api(f.d->Reset(&f.pp), "Reset after the control buffer is released");
        std::puts("RESET PASS");
        f.acquire_swapchain_surfaces(); f.frames_since_reset = 0; f.camera_history = {}; shadow_ensure_bloom(f);
        s.frame({}, {}, true);
        s.settle({&n16, &n17}); s.frame({}, {&n16, &n17}, true);
        s.retire(n16); s.retire(n17); s.frame({}, {}, false); s.expect(RsNodes, 0, "case h leaves nothing");
        s.end();
    }
    { // m. the observer's other signals: unavailable, FlushAll, an epoch that moved in the drain, the observer epoch as a key, a lost camera, device loss
        s.begin("m_observer");
        auto& n30 = s.make(30, 'B', -.05f, .05f); auto& n31 = s.make(31, 'B', .7f, .10f);
        const ULONG base30 = retention_count(n30.buffer.p);
        auto retained_pair = [&] { s.settle({&n30, &n31}); s.frame({}, {&n30, &n31}, false); s.expect(RsNodes, 2, "both retained"); };
        // available=false: full revalidation, every node unconfirmed (a lost context, not a death), flush=observer
        retained_pair();
        std::uint64_t observer_flushes = s.s[RsFlushObserver], context_lost = s.s[RsContextLost], retired = s.s[RsRetired];
        s.lifetime(4, 0, 0); s.frame({}, {}, false); s.lifetime(4, 1, 0);
        s.expect(RsNodes, 0, "an unavailable observer empties the store"); s.expect(RsFlushObserver, observer_flushes + 1, "flush=observer"); s.expect(RsContextLost, context_lost + 2, "unconfirmed nodes leave under their own count");
        s.expect(RsRetired, retired, "not counted as retired"); require(retention_count(n30.buffer.p) == base30, "baseline");
        // a lost camera: the observer answers about the camera, not the node
        retained_pair(); context_lost = s.s[RsContextLost];
        s.lifetime(2, 0xFFFFFFFFull, 0); s.lifetime(6, 2049, 0); s.lifetime(9, 1, 0); s.frame({}, {}, false); s.lifetime(9, 0, 0); // an overflow forces the revalidation while the camera is unknown
        s.expect(RsNodes, 0, "unconfirmed nodes leave"); s.expect(RsContextLost, context_lost + 2, "revalidate_context_lost counts them"); require(retention_count(n30.buffer.p) == base30, "baseline");
        // FlushAll through the drain
        retained_pair(); observer_flushes = s.s[RsFlushObserver];
        s.lifetime(5, 0, 0); s.frame({}, {}, false);
        s.expect(RsNodes, 0, "FlushAll drops the store"); s.expect(RsFlushObserver, observer_flushes + 1, "FlushAll counts as an observer flush");
        // the drain's epochs moved
        retained_pair(); const std::uint64_t epoch_flushes = s.s[RsFlushEpoch];
        s.lifetime(3, f.b.scope.load_epoch + 1, f.b.scope.registry_epoch); s.frame({}, {}, false); s.lifetime(3, f.b.scope.load_epoch, f.b.scope.registry_epoch);
        s.expect(RsNodes, 0, "another epoch in the drain flushes the store"); s.expect(RsFlushEpoch, epoch_flushes + 1, "flush=epoch"); require(retention_count(n30.buffer.p) == base30, "baseline");
        // the observer epoch is a key component: a draw under another one defers a flush to the scene end
        retained_pair(); n31.object.scope.observer_epoch = 1; s.frame({&n31}, {}, false); n31.object.scope.observer_epoch = 0;
        s.expect(RsFlushEpoch, epoch_flushes + 2, "another observer epoch flushes the store at the scene end"); s.expect(RsNodes, 0, "nothing is retained across it");
        // device loss, as a failed Present reports it
        retained_pair(); const std::uint64_t device_flushes = s.s[RsFlushDevice];
        s.lifetime(8, 0, 0); s.read();
        s.expect(RsFlushDevice, device_flushes + 1, "flush=device"); s.expect(RsNodes, 0, "the store is empty"); s.expect(RsRefs, 0, "no reference survives"); require(retention_count(n30.buffer.p) == base30, "baseline after device loss");
        s.frame({}, {}, false);
        s.retire(n30); s.retire(n31); s.frame({}, {}, false); s.expect(RsNodes, 0, "case m leaves nothing");
        s.end();
    }
    { // j. excluded classes and an unknown lifetime: never retained
        s.begin("j_excluded");
        auto& partner = s.make(18, 'B', -.05f, .05f);
        const std::uint32_t bits[5][2] = {{0x20, 0}, {0x200, 0}, {0x4000, 0}, {0x10000000, 0}, {0, 0x200}};
        std::vector<RetentionNode*> drawn{&partner};
        const float depths[5] = {.1f, .15f, .2f, .25f, .35f}; // distinct parallel planes for the map twin; the presented-frame oracles are tuned to the base script's rows and stay off here
        for (unsigned i = 0; i < 5; ++i) drawn.push_back(&s.make(19 + i, 'B', .2f + .12f * float(i), depths[i], nullptr, 0, bits[i][0], bits[i][1]));
        auto& unknown = s.make(24, 'B', .9f, .45f); unknown.object.scope.known = 0;
        drawn.push_back(&unknown);
        s.settle(drawn, {}, retention_settle - 1, false); s.frame(drawn, {}, true, false); // live, and identical with the option off
        s.expect(RsNodes, 1, "only the ordinary node enters the store"); s.expect(RsRecords, 1, "one record");
        s.frame({}, {&partner}, true); s.expect(RsUnseen, 1, "only the ordinary node is retained");
        s.retire(partner); s.frame({}, {}, false); s.expect(RsNodes, 0, "case j leaves nothing");
        s.end();
    }
    { // i. capacity: the 1,025th node evicts an unseen one; 600 retirements in one frame; the ring's overflow
        s.begin("i_capacity");
        Com<IDirect3DVertexBuffer9> mesh; s.make_buffer('B', mesh);
        const ULONG base = retention_count(mesh.p);
        std::vector<RetentionNode*> first, second;
        for (unsigned i = 0; i < 724; ++i) first.push_back(&s.make(2000 + i, 'B', -3.2f + .006f * float(i), 0, mesh.p));
        for (unsigned i = 0; i < 300; ++i) second.push_back(&s.make(2800 + i, 'B', 1.3f + .006f * float(i), 0, mesh.p));
        s.check_issues = false; // bulk frames: the kept list is not spelled out
        s.settle(first, {}, retention_settle, false); s.expect(RsStatics, 724, "the first batch is static");
        s.settle(second, {}, retention_settle, false);
        // The frame that filled the table evicted the reserve's worth of the farthest unseen nodes at its scene end (never at a draw).
        s.expect(RsNodes, 1024 - retention_node_reserve, "the store keeps its reserve free"); s.expect(RsEvicted, retention_node_reserve, "the reserve was made by evicting unseen nodes, once");
        s.expect(RsUnseen, 724 - retention_node_reserve, "the first batch is retained unseen"); s.expect_live(RsRefs, 2, 0, "one mesh and the declaration for the records");
        if (s.live()) require(s.s[RsRetainedIssues] >= 690, "the retained records are issued"); // the far cascade alternates above the budget
        const std::uint64_t evicted = s.s[RsEvicted], refused = s.s[RsRefused], retired = s.s[RsRetired], overflow = s.s[RsJournalOverflow], far_alternate = s.s[RsFarAlternate];
        if (s.live()) require(far_alternate >= 1, "the far cascade alternated because of retained issues (live issues alone fit the budget)");
        second.push_back(&s.make(3100, 'B', -3.3f, 0, mesh.p));
        s.frame(second, {}, false, false);
        s.expect(RsEvicted, evicted + 1, "the new node takes a reserve slot and the scene end refills it by one eviction"); s.expect(RsNodes, 1024 - retention_node_reserve, "no overflow write"); s.expect(RsRefused, refused, "nothing is refused while the reserve holds");
        unsigned burst = 0;
        for (unsigned i = 0; i < 724 && burst < 600; ++i) { s.retire(*first[i]); ++burst; }
        s.frame(second, {}, false, false);
        if (s.on()) require(s.s[RsRetired] >= retired + 600 - retention_node_reserve - 1 && s.s[RsRetired] <= retired + 600, "600 retirements drain in one frame"); // up to nine of them were the evicted nodes
        s.expect(RsJournalOverflow, overflow, "600 entries do not overflow the ring");
        for (auto* n : second) s.lifetime(2, n->object.scope.node_serial, 0);
        for (unsigned i = 600; i < 724; ++i) s.lifetime(2, first[i]->object.scope.node_serial, 0);
        s.lifetime(6, 2049, 0);
        s.frame({}, {}, false, false);
        s.expect(RsJournalOverflow, overflow + 1, "2,049 entries overflow"); s.expect(RsNodes, 0, "the revalidation empties the store"); s.expect(RsRefs, 0, "no reference remains");
        require(retention_count(mesh.p) == base, "the shared mesh's count is back at the baseline");
        s.check_issues = true;
        s.end();
    }
    { // k. the age cap, then the sun re-latch
        s.begin("k_age_and_sun");
        auto& n25 = s.make(25, 'B', -.05f, .05f); auto& n26 = s.make(26, 'B', .7f, .10f);
        const ULONG base25 = retention_count(n25.buffer.p);
        s.settle({&n25, &n26});
        const std::uint64_t aged = s.s[RsAge];
        for (unsigned i = 0; i < age_cap; ++i) s.frame({&n26}, {&n25}, i == 0 || i + 1 == age_cap, false);
        s.expect(RsAge, aged, "retained up to the age cap"); s.expect(RsNodes, 2, "still retained");
        s.frame({&n26}, {}, true);
        s.expect(RsAge, aged + 1, "unseen past the age cap: gone"); s.expect(RsNodes, 1, "only the live node"); require(retention_count(n25.buffer.p) == base25, "its reference is released");
        s.settle({&n25, &n26}); s.frame({&n26}, {&n25}, true);
        if (s.poll) { // the sun source switches point -> latch (the context pointer goes null): flushed like a re-latch
            const std::uint64_t switches = s.s[RsFlushSun];
            poll_context.slot = 0; s.check_issues = false; s.frame({&n26}, {}, false, false); s.check_issues = true;
            std::printf("RETENTION_SOURCE_SWITCH frame=%llu\n", f.frame - 1);
            s.expect(RsFlushSun, switches + 1, "a sun source switch flushes the store"); s.expect(RsNodes, 0, "nothing retained across the switch");
            s.settle({&n25, &n26}); s.frame({&n26}, {&n25}, true); // under the latch now
        }
        const std::uint64_t sun_flushes = s.s[RsFlushSun];
        s.sun = shadow_sun_flip; s.poll = false; // another validated direction, persisting: re-latched after 8 frames (the poll case is on the latch by now)
        unsigned frames = 0; s.check_issues = false;
        for (unsigned i = 1; i <= 8; ++i) { s.frame({&n26}, {}, false, false); if (!frames && s.s[RsFlushSun] != sun_flushes) frames = i; }
        s.check_issues = true;
        std::printf("RETENTION_SUN_RELATCH frames=%u\n", frames);
        if (s.on()) { require(frames == 8 && s.s[RsFlushSun] == sun_flushes + 1, "the re-latched sun flushes the store the same frame"); require(s.s[RsNodes] == 0 && s.s[RsRefs] == 0, "nothing is retained under the old sun"); }
        require(retention_count(n25.buffer.p) == base25, "reference counts at the baseline after the sun flush");
        s.settle({&n25, &n26}); s.frame({&n26}, {&n25}, true); // under the new sun
        s.end();
    }
    // Teardown with references held, one of them on a buffer the application has released.
    s.begin("teardown");
    s.read(); s.expect(RsNodes, 2, "two nodes are retained at the end"); s.expect_live(RsRefs, 3, 0, "their references are held into the teardown");
    std::printf("RETENTION_TEARDOWN nodes=%llu refs=%llu\n", static_cast<unsigned long long>(s.s[RsNodes]), static_cast<unsigned long long>(s.s[RsRefs]));
    s.end();
    f.camera_scripted = false;
}
} // namespace
