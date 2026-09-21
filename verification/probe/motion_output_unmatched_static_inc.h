// "unmatchedstatic" (seam, TAA, X3M_FIXTURE_CAMERA=rotate): the static-world
// previous rows of X3M_TAA_UNMATCHED_STATIC. Three world-placed triangles (the
// A geometry; true W.V.P rows, unlike the screen-space rows of the other
// scripts) under a camera that translates and yaws every frame:
//   S  static in the world; its key changes at frame 3 (scope.lod 0 -> 1: the
//      same node and lifetime serial, as a LOD or mesh swap) and stays changed;
//   M  moves in the world every frame and is always matched (the matched path);
//   N  a new node that first appears at frame 6.
// Option off: S at frame 3 and N at frame 6 carry the missing-history sentinel
// and resolve current-only. node: S at frame 3 carries the camera-path motion
// (its placement through the PREVIOUS view and projection), N stays sentinel.
// all: N at frame 6 as well. The expectations are forward compositions of the
// script's own placement and camera matrices in double precision; nothing here
// calls src/renderer/static_previous_rows.h.
struct UnmatchedStaticScript {
    Fixture& f;
    unsigned mode = 0; // 0 off, 1 node, 2 all (the DLL's parse)
    struct Body { Object object; double scale, place[3], drift; int first; float rows[16]{}, previous_rows[16]{}; bool drawn_previous = false; int expect = 0; double static_rows[16]{}; };
    Body bodies[3];
    double projection[16]{}, view[16]{}, previous_projection[16]{}, previous_view[16]{};
    bool have_previous_camera = false;
    explicit UnmatchedStaticScript(Fixture& fixture) : f(fixture) {
        char setting[8]{};
        if (GetEnvironmentVariableA("X3M_TAA_UNMATCHED_STATIC", setting, sizeof setting) > 0) mode = !std::strcmp(setting, "node") ? 1 : !std::strcmp(setting, "all") ? 2 : 0;
        const double scales[3] = {8, 5, 5}, places[3][3] = {{-16, 4, 40}, {14, -2, 40}, {17, 18, 40}}, drifts[3] = {0, .3, 0};
        const char* names[3] = {"S", "M", "N"};
        for (unsigned i = 0; i < 3; ++i) {
            Body& body = bodies[i];
            body.object.name = names[i]; body.object.vb = f.vb_a.p; body.object.covers = covers_a; body.object.scope = f.a.scope;
            body.object.scope.node_serial = 5000 + i; body.object.scope.node = 0x500000 + 0x100 * i; body.object.scope.mesh = 0x600000 + 0x100 * i;
            body.object.scope.node_handle = 700 + i; body.object.scope.model = 0x70 + i; body.object.scope.lod = 0;
            body.scale = scales[i]; std::memcpy(body.place, places[i], sizeof body.place); body.drift = drifts[i]; body.first = i == 2 ? 6 : 0;
        }
    }
    // Row-major column-vector object->clip rows of X = scale pos + place under a
    // row-vector view V and projection P (view = X V; clip = view P).
    static void compose(const double* P, const double* V, double scale, const double place[3], double out[16]) {
        double v[3][4];
        for (unsigned j = 0; j < 3; ++j) {
            for (unsigned i = 0; i < 3; ++i) v[j][i] = scale * V[i * 4 + j];
            v[j][3] = V[12 + j]; for (unsigned i = 0; i < 3; ++i) v[j][3] += place[i] * V[i * 4 + j];
        }
        for (unsigned k = 0; k < 4; ++k) {
            out[k] = P[0] * v[0][k] + P[8] * v[2][k]; out[4 + k] = P[5] * v[1][k] + P[9] * v[2][k];
            out[8 + k] = P[10] * v[2][k] + (k == 3 ? P[14] : 0.); out[12 + k] = P[11] * v[2][k];
        }
    }
    template <class T> static void clip(const T* rows, double ox, double oy, double c[4]) {
        for (unsigned i = 0; i < 4; ++i) c[i] = double(rows[i * 4]) * ox + double(rows[i * 4 + 1]) * oy + double(rows[i * 4 + 2]) * .5 + double(rows[i * 4 + 3]);
    }
    // The object point (z = .5, the buffer's) whose current clip position lands on the NDC sample.
    static bool object_point(const float* rows, double nx, double ny, double& ox, double& oy) {
        double a[2][3];
        for (unsigned r = 0; r < 2; ++r) { const double n = r ? ny : nx; for (unsigned k = 0; k < 3; ++k) {
            const unsigned column = k == 2 ? 3 : k; a[r][k] = double(rows[r * 4 + column]) - n * double(rows[12 + column]);
            if (k == 2) a[r][2] += .5 * (double(rows[r * 4 + 2]) - n * double(rows[14])); } }
        const double det = a[0][0] * a[1][1] - a[0][1] * a[1][0];
        if (std::fabs(det) < 1e-12) return false;
        ox = (-a[0][2] * a[1][1] + a[0][1] * a[1][2]) / det; oy = (-a[0][0] * a[1][2] + a[0][2] * a[1][0]) / det;
        return true;
    }
    void draw(Body& body) {
        f.scope(&body.object);
        api(f.d->SetStreamSource(0, body.object.vb, 0, 24), "SetStreamSource body");
        api(f.d->SetVertexShader(f.vs.p), "SetVertexShader body"); api(f.d->SetPixelShader(f.ps.p), "SetPixelShader body");
        api(f.d->SetVertexShaderConstantF(24, body.rows, 4), "SetVertexShaderConstantF body rows");
        const Snapshot before = f.snapshot();
        api(f.d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive body"); ++f.draw_index;
        f.compare(before, f.snapshot(), "draw");
    }
    void verify(const std::vector<Body*>& drawn) {
        std::vector<float> data(std::size_t(Fixture::W) * Fixture::H * 4), depth(std::size_t(Fixture::W) * Fixture::H); unsigned w = 0, h = 0;
        api(f.readback(f.d.p, data.data(), unsigned(data.size()), &w, &h), "unmatched-static motion readback");
        api(f.readback_depth(f.d.p, depth.data(), unsigned(depth.size()), &w, &h), "unmatched-static depth readback");
        std::printf("MOTION_HASH frame=%llu motion=%016llx depth=%016llx\n", f.frame, static_cast<unsigned long long>(fnv(data.data(), data.size() * 4)),
                    static_cast<unsigned long long>(fnv(depth.data(), depth.size() * 4)));
        struct Tally { unsigned pixels = 0, changed = 0; double uv = 0, z = 0; } tally[3];
        unsigned checked = 0, skipped = 0, mismatches = 0, background = 0;
        for (UINT y = 0; y < Fixture::H; ++y) for (UINT x = 0; x < Fixture::W; ++x) {
            Body* owner = nullptr; bool ambiguous = false; double ox = 0, oy = 0;
            for (Body* body : drawn) {
                bool covered = false, same = true;
                for (int k = 0; k < 5 && same; ++k) {
                    const double px = x - f.jx + (k == 1 ? 1.5 : k == 2 ? -1.5 : 0), py = y - f.jy + (k == 3 ? 1.5 : k == 4 ? -1.5 : 0);
                    double bx = 0, by = 0; if (!object_point(body->rows, 2 * px / Fixture::W - 1, 1 - 2 * py / Fixture::H, bx, by)) { same = false; break; }
                    const bool c = covers_a(bx, by);
                    if (k == 0) { covered = c; if (c) { ox = bx; oy = by; } } else same = c == covered;
                }
                if (!same) { ambiguous = true; break; }
                if (covered) { if (owner) ambiguous = true; owner = body; }
            }
            if (ambiguous) { ++skipped; continue; }
            ++checked;
            const std::size_t index = std::size_t(y) * Fixture::W + x;
            const float* actual = &data[index * 4];
            bool ok;
            if (!owner) { ++background; ok = actual[0] == 0 && actual[1] == 0 && actual[2] == 0 && actual[3] == -1 && depth[index] == -1; }
            else {
                const unsigned who = unsigned(owner - bodies);
                double now[4]; // (ox, oy): the owner's object point at the jittered sample, from the coverage loop
                clip(owner->rows, ox, oy, now);
                ok = std::fabs(depth[index] - now[2] / now[3]) <= 4e-6;
                ++tally[who].pixels;
                if (f.boundary_before.size() == data.size() / 4 && f.boundary_after[index] != f.boundary_before[index]) ++tally[who].changed;
                if (owner->expect == 0) ok = ok && actual[0] == 0 && actual[1] == 0 && actual[2] == 0 && actual[3] == -1;
                else {
                    double before[4];
                    if (owner->expect == 1) clip(owner->previous_rows, ox, oy, before); else clip(owner->static_rows, ox, oy, before);
                    const double eu = std::fabs(actual[0] - (.5 * before[0] / before[3] + .5 + .5 / Fixture::W)) * Fixture::W,
                                 ev = std::fabs(actual[1] - (-.5 * before[1] / before[3] + .5 + .5 / Fixture::H)) * Fixture::H, ez = std::fabs(actual[2] - before[2] / before[3]);
                    ok = ok && std::isfinite(actual[0]) && std::isfinite(actual[1]) && std::isfinite(actual[2]) && eu <= .01 && ev <= .01 && ez <= 4e-6 && actual[3] == 1;
                    if (ok) { tally[who].uv = std::max(tally[who].uv, std::max(eu, ev)); tally[who].z = std::max(tally[who].z, ez); }
                }
            }
            if (!ok && ++mismatches <= 8) std::printf("UNMATCHED_STATIC_DIFF frame=%llu x=%u y=%u owner=%s expect=%d actual=%.9g,%.9g,%.9g,%.9g depth=%.9g\n", f.frame, x, y,
                                                       owner ? owner->object.name : "-", owner ? owner->expect : -1, actual[0], actual[1], actual[2], actual[3], depth[index]);
        }
        for (Body* body : drawn) { const Tally& t = tally[body - bodies];
            std::printf("UNMATCHED_STATIC frame=%llu body=%s expect=%s lod=%u pixels=%u changed=%u max_uv_pixels=%.9g max_depth_error=%.9g\n", f.frame, body->object.name,
                        body->expect == 0 ? "sentinel" : body->expect == 1 ? "matched" : "static", body->object.scope.lod, t.pixels, t.changed, t.uv, t.z);
            require(t.pixels >= 20, "every body covers unambiguous pixels"); }
        std::printf("MOTION frame=%llu checked=%u skipped=%u background=%u mismatches=%u mode=%u\n", f.frame, checked, skipped, background, mismatches, mode);
        require(!mismatches && checked > 0, "motion and depth targets match the world-placement oracle");
        ++frames_verified;
    }
    void run() {
        require(f.enabled && f.seam && f.taa && f.camera, "unmatchedstatic needs the seam, the resolve and the fixture camera");
        f.unmatchedstatic = true; f.camera_scripted = true;
        for (unsigned frame = 0; frame < 9; ++frame) {
            f.camera_yaw = .5 * frame; f.camera_position[0] = .5 * frame; f.camera_position[1] = .25 * frame; f.camera_position[2] = 1. * frame;
            if (frame == 3) bodies[0].object.scope.lod = 1; // the key change: same node, same lifetime serial
            f.frame_begin();
            for (unsigned i = 0; i < 16; ++i) { projection[i] = fake_projection[i]; view[i] = fake_view[i]; }
            std::vector<Body*> drawn;
            for (Body& body : bodies) {
                if (int(frame) < body.first) continue;
                double place[3] = {body.place[0] + body.drift * frame, body.place[1], body.place[2]}, rows[16];
                compose(projection, view, body.scale, place, rows);
                for (unsigned i = 0; i < 16; ++i) body.rows[i] = float(rows[i]);
                const bool new_key = !body.drawn_previous || (&body == &bodies[0] && frame == 3);
                const bool object_known = body.drawn_previous;
                body.expect = !new_key ? 1 : (have_previous_camera && ((mode == 1 && object_known) || mode == 2)) ? 2 : 0;
                if (body.expect == 2) { double still[3] = {place[0], place[1], place[2]}; compose(previous_projection, previous_view, body.scale, still, body.static_rows); }
                // The cut rule counts a static-assumed draw as the miss it is (keyed, not matched).
                f.records.push_back({&body.object, 0, 0, 0, true, body.expect == 1, 0, 0, 0, false, true, true});
                std::printf("EXPECT frame=%llu index=%u object=%s routed=1 matched=%u jittered=1 static=%u\n", f.frame, f.draw_index + 1, body.object.name, body.expect == 1, body.expect == 2);
                draw(body);
                drawn.push_back(&body);
            }
            pending = &drawn;
            f.frame_end();
            pending = nullptr;
            for (Body& body : bodies) { body.drawn_previous = int(frame) >= body.first; std::memcpy(body.previous_rows, body.rows, sizeof body.rows); }
            std::memcpy(previous_projection, projection, sizeof projection); std::memcpy(previous_view, view, sizeof view); have_previous_camera = true;
        }
        require(frames_verified == 9, "every frame verified against the world-placement oracle");
        require(taa_frames == 9 && taa_history_frames == 8 && taa_reference_frames == 9, "every frame after the first resolves with history and equals the reference resolve");
    }
    const std::vector<Body*>* pending = nullptr;
    static void verify_hook(Fixture& fixture) { auto* script = static_cast<UnmatchedStaticScript*>(fixture.custom_verify_context); if (script && script->pending) script->verify(*script->pending); }
};
void run_unmatched_static(Fixture& f) {
    UnmatchedStaticScript script(f);
    f.custom_verify = &UnmatchedStaticScript::verify_hook; f.custom_verify_context = &script;
    script.run();
    f.custom_verify = nullptr; f.custom_verify_context = nullptr;
}
