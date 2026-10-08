// Engine light on the hull (docs/architecture/engine-light.md; gap 8 of
// docs/architecture/engine-exhaust-gap-analysis.md): the point-light term the
// original-shading fill block adds in linear light. Included by
// linear_material.cpp inside its anonymous namespace, after the token helpers
// and before original_fill_transform; pure bytecode emission, no D3D.
//
// Inputs every reviewed hull, palette, XT and glass pixel program carries
// (verification/results/engine-light/eye_normal_registers.py over the 168
// reviewed pairs: v2 = TEXCOORD1, v3 = TEXCOORD2 in all 104 such programs):
//   v2.xyz  the eye vector cam - P in world space (unnormalised on the hull
//           families and XT DEFAULT, normalised before interpolation on the
//           palette families and XT BUMPMAP: only its direction is used),
//   v3.xyz  the geometric world normal (g_mWorldIT, unnormalised),
//   vD.y    the current clip w of the motion transformer's depth interpolator
//           (MotionOutputProfile::pixel_depth_input_register; w = view z,
//           camera_reprojection.h: P[11] = 1).
// The pixel's position relative to the camera is D = e w / (e . F), e =
// nrm(v2), F the camera's forward axis: exact on both eye forms (an
// interpolated unit vector only bends the direction by its triangle's angular
// size squared), and independent of the partial-precision v2 magnitude.
// Per-draw constants (the route uploads them on every draw that binds an
// engine-light variant; no original program reads c50 or above):
//   c200 = (L - cam, R^2)      the light relative to the camera, world axes (plate 0's)
//   c201 = (colour, 1 / R^2)   linear colour of the light
//   c202 = (F, tier)           the camera's forward axis, world; .w the plate tier
//   c55-c197                   the plates and their lights' colours (below)
// Shader-local c199 = (cap, -2^-20, 2^-40, 0), c52-c54 the tier thresholds.
// E = colour x saturate(N . l) x saturate(1 - d^2 / R^2)^2 into r14.xyz, r15
// scratch; the fill block then adds min(E, saturate(cap - decode(sum))) to
// the decoded lobe sum before the encode, so a lit plate never exceeds the
// cap where the native lighting did not (E 1: the plume's own radiance class).
//
// Nozzle plates (docs/architecture/engine-light.md "Nozzle plates"): in a twin
// that also carries the hull light-map gain g, the block leaves the plate
// weight w in r15.w, the maximum over the ship's plates (up to 72 main
// nozzles, each at its nozzle's light point P_i with its value_eff v_i) of
// 1 within nozzle_plate_full x v_i, 0 from nozzle_plate_reach x v_i, linear
// in d^2 between; the gain site then scales the light-map term by
// g - (g - 1) w instead of g (texture value alone on the nozzle plates, the
// full gain on windows and markings elsewhere). Uploaded per draw beside the
// light: slot i's plate register c197 - 2i = ((P_i - cam) / v_i, 1 / v_i)
// (an unused slot (2, 0, 0, 0): (d / v)^2 = 4, weight 0), and c202.w = the
// ship's plate tier k for the uniform branches: slots 0 .. runs[k] - 1 run,
// runs = 1, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72 (engine_light::core::
// plate_runs; a pad slot is a copy of slot 0). The block forms the pixel's
// camera-relative position D = e t once, then per plate
// u_i = (D / v_i - (P_i - cam) / v_i)^2 (MAD, DP3) and m = min(u_i, m)
// (input first: a NaN u_i keeps m; m starts at c198.w = 4); after the light
// w = saturate(A m + B), A = -1 / (r1^2 - r0^2), B = r1^2 / (r1^2 - r0^2)
// (w is decreasing in (d / v)^2, so the min of the distances is the max of
// the weights), then max(w, 0) (input first). Shader-local c198 = (A, B, 0, 4).
//
// A light per plate (user decision 2026-10-08; engine-light.md "A light per
// plate"): each plate i carries its nozzle's light, plate 0's in c200-c201;
// a further plate's light is its plate point with R_i = 3 v_i, so slot i
// carries only its colour beside its plate register: c198 - 2i = (colour_i,
// 1 / R_i^2) (engine-light.md "Plate cap": two registers per plate hold 72
// slots in c55-c197). The pixel takes the light of the plate with the least
// u_i = (d_i / v_i)^2 (ties: the earlier, brighter plate), the plate nearest
// in units of its value_eff; that is the plate whose falloff
// saturate(1 - d_i^2 / R_i^2)^2 is the largest, and beyond every plate's reach
// the light is 0 whichever is taken. The selection copies two registers into
// r12 and r13 (the fill block's scratch, dead before it): r12 / r13 start as
// c200 / c201 (plate 0's light), and inside the first branch r12 becomes slot
// 0's plate register; per further slot run by the tier branches: cond = u_i -
// s (r14.y), CMP r12 from the slot's plate register and r13 from its colour
// register (cond >= 0 keeps the earlier; a NaN cond takes the slot, whose E
// then is NaN and cleared by the final max), s = min(u_i, s) in r14.w (s =
// u_0 from slot 0). Before the first branch closes, the selected plate becomes
// a light again: r12.xyz = r12.xyz / r12.w (L - cam = ((P - cam) / v) v),
// r12.w = 1 / r13.w (R^2). The light law then reads r12 / r13 instead of
// c200 / c201: one falloff evaluation per pixel. A one-plate ship (tier 0)
// runs no branch, so its light reads c200 / c201's values exactly as before.
// The non-plate twins (no light-map gain) carry the same selection; they keep
// e and t in r15 and form D = e t per slot (one MUL more each). A twin whose
// gain site cannot carry the plate weight (the block not before the light-map
// fetch at depth 0) keeps the single light of plate 0 (no branches between
// the fetch and the final; not seen in the corpus).
//
// Tier branches: eleven nested uniform if_ne on c202.w, level j (1-based)
// compares the tier with j - 1 (inside level j the tier is at least j - 1):
// level 1 opens before slot 1 (slot 0 without plates), level j + 1 at slot
// runs[j]. Thresholds 0 (c198.z with plates, c199.w without), 1 (c199.x, the
// cap 1) and 2 .. 10 in the shader-local c52-c54 (engine_light_tier_
// definition_words). The XT originals' form: if_ne on two temporary lanes,
// the tier and the threshold moved into r14.x / r14.y first (r14.xyz is the
// per-slot scratch, free between slots; r14.w the running minimum s). Nesting
// depth 11 (ps_3_0 allows 24).
constexpr unsigned engine_light_constant = 200, engine_light_definition = 199, engine_light_term = 14,
                   engine_light_scratch = 15, engine_light_eye = 2, engine_light_normal = 3,
                   engine_light_plate_constant = 198, engine_light_plate_top = 197, engine_light_plate_slots = 72,
                   engine_light_plate_tiers = 12, engine_light_selected = 12;
// The tiers' runs: EngineLightAbi::plate_runs, equal to engine_light::core::plate_runs (the route's static_assert).
constexpr const unsigned (&engine_light_plate_runs)[engine_light_plate_tiers] = EngineLightAbi::plate_runs;
static_assert(EngineLightAbi::plate_count == engine_light_plate_slots &&
                  EngineLightAbi::tier_count == engine_light_plate_tiers &&
                  EngineLightAbi::plate_constant == engine_light_plate_top &&
                  EngineLightAbi::tier_constant == 52 && EngineLightAbi::light_constant == 55,
              "the ABI's layout is the block's");
constexpr unsigned engine_light_select_first = engine_light_plate_top + 2 - 2 * engine_light_plate_slots,
                   engine_light_tier_levels = engine_light_plate_tiers - 1,
                   engine_light_tier_registers = (engine_light_tier_levels - 2 + 3) / 4,
                   engine_light_tier_first = engine_light_select_first - engine_light_tier_registers;
constexpr unsigned engine_light_plate_register(unsigned i) noexcept {
    return engine_light_plate_top - 2 * i;
}
constexpr unsigned engine_light_colour_register(unsigned i) noexcept { // i >= 1
    return engine_light_plate_top + 1 - 2 * i;
}
static_assert(engine_light_plate_top + 1 == engine_light_plate_constant,
              "the plate registers end directly below c198");
static_assert(engine_light_plate_runs[engine_light_plate_tiers - 1] == engine_light_plate_slots &&
                  engine_light_plate_runs[1] > 1,
              "the last tier runs every slot; level 2 opens after the first branch's slot");
static_assert(engine_light_select_first == 55 && engine_light_tier_first == 52 && engine_light_tier_first >= 50,
              "one refusal range c52-c202, above every constant the originals read (c49 at most)");
// engine_light_reach_ratio must equal engine_light::core::reach (src/proxy/engine_light_core.h; pinned by
// verification/analysis/test_engine_light.py).
constexpr float engine_light_reach_ratio = 3.0f, nozzle_plate_full = 0.75f, nozzle_plate_reach = 1.0f;
constexpr float nozzle_plate_slope =
                    -1.0f / (nozzle_plate_reach * nozzle_plate_reach - nozzle_plate_full * nozzle_plate_full),
                nozzle_plate_offset = nozzle_plate_reach * nozzle_plate_reach /
                                      (nozzle_plate_reach * nozzle_plate_reach - nozzle_plate_full * nozzle_plate_full),
                nozzle_plate_none = 4.0f; // (d / v)^2 of an unused slot (engine_light::core::unused_plate) and the start
static_assert(nozzle_plate_full >= 0.0f && nozzle_plate_full < nozzle_plate_reach &&
                  nozzle_plate_reach < engine_light_reach_ratio &&
                  nozzle_plate_none >= nozzle_plate_reach * nozzle_plate_reach,
              "the plate weight reaches 0 inside the light's radius and at an unused slot");
constexpr float engine_light_cap = 1.0f, engine_light_facing_floor = -0x1p-20f, engine_light_distance_floor = 0x1p-40f;
// The second tier level compares the tier with c199.x, the cap: it must stay 1, and c198.z / c199.w the tier 0.
constexpr float engine_light_plate_tier_one = 1.0f;
static_assert(engine_light_cap == engine_light_plate_tier_one,
              "the second tier level reads c199.x as the tier 1: a cap other than 1 needs its own DEF");
constexpr unsigned engine_light_eye_texcoord = 1, engine_light_normal_texcoord = 2;
void engine_light_definition_words(Words& out) {
    emit(out, def,
         {dst(constant, engine_light_definition, xyzw), bits(engine_light_cap), bits(engine_light_facing_floor),
          bits(engine_light_distance_floor), bits(0.0f)});
}
void engine_light_plate_definition_words(Words& out) {
    emit(out, def,
         {dst(constant, engine_light_plate_constant, xyzw), bits(nozzle_plate_slope), bits(nozzle_plate_offset),
          bits(0.0f), bits(nozzle_plate_none)});
}
// The tier thresholds 2 .. levels - 1 of the selecting twins: c52 = (2, 3, 4, 5), c53 = (6, 7, 8, 9), c54 = (10, 0,
// 0, 0) (a lane past the last threshold 0, never read).
void engine_light_tier_definition_words(Words& out) {
    for (unsigned r = 0; r < engine_light_tier_registers; ++r) {
        float v[4];
        for (unsigned l = 0; l < 4; ++l) {
            const unsigned k = 2 + 4 * r + l;
            v[l] = k < engine_light_tier_levels ? float(int(k)) : 0.0f;
        }
        emit(out, def,
             {dst(constant, engine_light_tier_first + r, xyzw), bits(v[0]), bits(v[1]), bits(v[2]), bits(v[3])});
    }
}
// The threshold of tier level `level` (1-based): tier != level - 1.
Word engine_light_tier_threshold(unsigned level, bool plate) noexcept {
    const unsigned k = level - 1;
    if (k == 0) return plate ? lane(constant, engine_light_plate_constant, 2) : lane(constant, engine_light_definition, 3);
    if (k == 1) return lane(constant, engine_light_definition, 0);
    return lane(constant, engine_light_tier_first + (k - 2) / 4, (k - 2) % 4);
}
// if_ne with the comparison NE (5) in the token's bits 16-23; endif.
constexpr unsigned engine_light_if_ne = 41u | (5u << 16), engine_light_endif = 43u;
// Without `select` (a gained twin whose gain site cannot take the plate
// weight): 18 instructions, 22 weighted slots (two NRM x3), the single light
// c200-c201. With `select` and without `plate` (the twins without light-map
// gain): the MOVs of r12 / r13 from c200 / c201, eleven uniform if_ne / endif
// levels with two MOVs each for their temporary operands, MOV r12 from slot
// 0's plate register in the first, slot 0 three (D = e t, MAD, DP3 into
// r14.w), slots 1-71 seven each (D, MAD, DP3, the condition, two CMP, the
// running minimum), and before the first level closes the light from the
// selected plate (RCP, MUL, RCP); the light then reads r12 / r13. With
// `plate` (implies the selection): the pixel position D = e t once into
// r15.xyz, slot 0 (MAD, DP3, MIN into r15.w from c198.w) ahead of the
// branches, slots 1-71 seven each (MAD, DP3 into r14.x, the weight's MIN, the
// condition, two CMP, the running minimum), the weight into r15.w after the
// light, read by the gain site (lightmap_gain_instruction): the light then
// reads D (ADD instead of the MAD) and keeps 1 / d in r15.x, so r15.w carries
// the weight's running minimum through the light. The slot counts are in
// EngineLightAbi (linear_material.h) and the structure tool's rows.
void engine_light_block(Words& out, unsigned depth_input, bool plate = false, bool select = false) {
    select = select || plate;
    const unsigned e = engine_light_term, s = engine_light_scratch, a = engine_light_constant,
                   b = engine_light_constant + 1, f = engine_light_constant + 2, k = engine_light_definition,
                   c = engine_light_plate_constant, la = engine_light_selected, lb = engine_light_selected + 1;
    constexpr unsigned negate = 1;
    // The light's two registers: the selected copies r12 / r13, or c200 / c201 directly.
    const auto position = [&](unsigned sw) { return select ? src(temp, la, sw) : src(constant, a, sw); };
    const auto colour = [&](unsigned sw) { return select ? src(temp, lb, sw) : src(constant, b, sw); };
    emit(out, 36, {dst(temp, s), src(input, engine_light_eye)});                 // e = nrm(v2)
    emit(out, 8, {dst(temp, s, 8), src(temp, s), src(constant, f)});             // e . F
    emit(out, min_op, {dst(temp, s, 8), lane(temp, s, 3), lane(constant, k, 1)}); // <= -2^-20 (input first)
    emit(out, 6, {dst(temp, s, 8), lane(temp, s, 3)});                           // 1 / (e . F)
    emit(out, mul, {dst(temp, s, 8), lane(temp, s, 3), lane(input, depth_input, 1)}); // t = w / (e . F)
    const unsigned inverse_lane = plate ? 0u : 3u; // 1 / d: r15.x with plates (r15.w holds the minimum)
    if (plate) emit(out, mul, {dst(temp, s), src(temp, s), lane(temp, s, 3)}); // D = e t
    if (select) {
        emit(out, mov, {dst(temp, la, xyzw), src(constant, a)}); // plate 0's light: c200 / c201
        emit(out, mov, {dst(temp, lb, xyzw), src(constant, b)});
        // Uniform branches on the ship's plate tier (c202.w; the operands come from constants, so no divergence): slot
        // 0 always (inside the first level without plates: only the selection needs it), level 1 from slot 1 (0), level
        // j + 1 from slot runs[j].
        unsigned level = 0;
        for (unsigned i = 0; i < engine_light_plate_slots; ++i) {
            if (level < engine_light_tier_levels &&
                i == (level ? engine_light_plate_runs[level] : plate ? 1u : 0u)) {
                ++level;
                emit(out, mov, {dst(temp, e, 1), lane(constant, f, 3)});                       // r14.x = tier
                emit(out, mov, {dst(temp, e, 2), engine_light_tier_threshold(level, plate)}); // r14.y = level - 1
                emit(out, engine_light_if_ne, {lane(temp, e, 0), lane(temp, e, 1)});
                if (level == 1) // the selection from here on: slot 0's plate register
                    emit(out, mov, {dst(temp, la, xyzw), src(constant, engine_light_plate_register(0))});
            }
            const unsigned p = engine_light_plate_register(i);
            if (!plate) emit(out, mul, {dst(temp, e), src(temp, s), lane(temp, s, 3)}); // D = e t
            emit(out, mad, {dst(temp, e), src(temp, plate ? s : e), lane(constant, p, 3), src(constant, p, identity, negate)});
            const unsigned u = i ? 0u : 3u; // (d / v_i)^2: r14.w for slot 0 (s = u_0), else r14.x
            emit(out, 8, {dst(temp, e, 1u << u), src(temp, e), src(temp, e)});
            if (plate)
                emit(out, min_op, {dst(temp, s, 8), lane(temp, e, u), i ? lane(temp, s, 3) : lane(constant, c, 3)});
            if (!i) continue;
            emit(out, add, {dst(temp, e, 2), lane(temp, e, 0), src(temp, e, 3 * 0x55, negate)}); // u_i - s
            emit(out, cmp, {dst(temp, la, xyzw), lane(temp, e, 1), src(temp, la), src(constant, p)});
            emit(out, cmp, {dst(temp, lb, xyzw), lane(temp, e, 1), src(temp, lb),
                            src(constant, engine_light_colour_register(i))});
            emit(out, min_op, {dst(temp, e, 8), lane(temp, e, 0), lane(temp, e, 3)}); // s = min(u_i, s) (input first)
        }
        for (; level > 1; --level) emit(out, engine_light_endif, {});
        // The selected plate as a light: L - cam = ((P - cam) / v) v, R^2 = 1 / (1 / R^2).
        emit(out, 6, {dst(temp, e, 1), lane(temp, la, 3)});                    // v
        emit(out, mul, {dst(temp, la, 7), src(temp, la), lane(temp, e, 0)});   // L - cam
        emit(out, 6, {dst(temp, la, 8), lane(temp, lb, 3)});                   // R^2
        emit(out, engine_light_endif, {});
    }
    if (plate)
        emit(out, add, {dst(temp, e), position(identity), src(temp, s, identity, negate)}); // L - cam - D
    else
        emit(out, mad, {dst(temp, e), src(temp, s), src(temp, s, 3 * 0x55, negate), position(identity)}); // L - cam - e t
    emit(out, 8, {dst(temp, e, 8), src(temp, e), src(temp, e)});                 // d^2
    emit(out, max_op, {dst(temp, e, 8), lane(temp, e, 3), lane(constant, k, 2)}); // >= 2^-40 (input first)
    emit(out, 7, {dst(temp, s, 1u << inverse_lane), lane(temp, e, 3)});         // 1 / d
    emit(out, mul, {dst(temp, e), src(temp, e), lane(temp, s, inverse_lane)});   // l
    emit(out, 36, {dst(temp, s), src(input, engine_light_normal)});              // N = nrm(v3)
    emit(out, 8, {dst(temp, s, 1) | sat, src(temp, s), src(temp, e)});           // saturate(N . l)
    emit(out, add, {dst(temp, e, 8), src(temp, e, 3 * 0x55, negate), position(3 * 0x55)}); // R^2 - d^2
    emit(out, mul, {dst(temp, e, 8) | sat, lane(temp, e, 3), colour(3 * 0x55)}); // q = saturate(1 - d^2 / R^2)
    if (plate) {
        emit(out, mad, {dst(temp, s, 8) | sat, lane(temp, s, 3), lane(constant, c, 0), lane(constant, c, 1)}); // w
        emit(out, max_op, {dst(temp, s, 8), lane(temp, s, 3), lane(constant, c, 2)}); // NaN -> 0 (input first)
    }
    emit(out, mul, {dst(temp, e, 8), lane(temp, e, 3), lane(temp, e, 3)});       // q^2
    emit(out, mul, {dst(temp, e, 8), lane(temp, e, 3), lane(temp, s, 0)});       // q^2 (N . l)
    emit(out, mul, {dst(temp, e), lane(temp, e, 3), colour(identity)});          // E = colour q^2 (N . l)
    emit(out, max_op, {dst(temp, e), src(temp, e), lane(constant, k, 3)});       // NaN / negative -> 0 (input first)
}
// Inside the fill block, after the decoded sum (and the fill MAD) sit in r12:
// r13 = min(E, saturate(cap - r12)), r12 += r13. Three instructions, 3 slots.
void engine_light_add(Words& out) {
    emit(out, add, {dst(temp, 13) | sat, src(temp, 12, identity, 1), lane(constant, engine_light_definition, 0)});
    emit(out, min_op, {dst(temp, 13), src(temp, engine_light_term), src(temp, 13)});
    emit(out, add, {dst(temp, 12), src(temp, 12), src(temp, 13)});
}
// The gain site of a plate twin reads the weight from r15.w
// (lightmap_plate_gain_instructions, beside the gain MUL above).
static_assert(engine_light_scratch == lightmap_plate_weight, "the plate weight lives in the block's scratch");
// The program declares the two inputs with the expected semantics (TEXCOORD1
// -> v2, TEXCOORD2 -> v3, xyz lanes present) and the motion depth input; the
// block's temporaries and constants are free of the original. Pure scan.
bool engine_light_inputs(const Word* code, const Structure& s, unsigned depth_input) noexcept {
    bool eye = false, normal = false;
    for (const auto& i : s.instructions) {
        if (i.opcode != dcl || i.count != 2 || kind(code[i.at + 2]) != input) continue;
        const Word usage = code[i.at + 1], reg_token = code[i.at + 2];
        const unsigned number = index(reg_token), lanes = mask(reg_token);
        if ((usage & 31) != 5 || (lanes & xyz) != xyz) continue;
        const unsigned semantic = (usage >> 16) & 15;
        if (number == engine_light_eye && semantic == engine_light_eye_texcoord) eye = true;
        if (number == engine_light_normal && semantic == engine_light_normal_texcoord) normal = true;
        if (number == depth_input) return false; // the original must not declare the motion depth input
    }
    if (!eye || !normal) return false;
    for (const auto& i : s.instructions) {
        if (i.opcode == dcl) continue;
        const unsigned last = i.opcode == def ? 1 : i.count;
        for (unsigned q = 1; q <= last; ++q) {
            const auto t = kind(code[i.at + q]), r = index(code[i.at + q]);
            if (t == temp && r >= engine_light_selected && r <= engine_light_scratch) return false;
            if (t == constant && r >= engine_light_tier_first && r <= engine_light_constant + 2) return false;
        }
    }
    return true;
}
// The emitted block references only its own temporaries (r14/r15, and the
// selected light r12/r13, the fill block's scratch: dead before it), v2/v3/vD
// and its constants (c52-c202); the fill block with the light reads r14
// besides its own set.
bool engine_light_range_free(const Word* code, std::size_t begin, std::size_t end, unsigned depth_input) noexcept {
    for (std::size_t at = begin; at < end;) {
        const Word token = code[at];
        const unsigned op = token & 0xffff, n = length(token);
        if (at + n >= end || op == def || op == dcl || op == 0xfffe) return false;
        for (unsigned q = 1; q <= n; ++q) {
            const auto t = kind(code[at + q]), r = index(code[at + q]);
            if (t == temp && (r < engine_light_selected || r > engine_light_scratch)) return false;
            if (t == constant && (r < engine_light_tier_first || r > engine_light_constant + 2)) return false;
            if (t == input && r != engine_light_eye && r != engine_light_normal && r != depth_input) return false;
            if (t != temp && t != constant && t != input) return false;
        }
        at += n + 1;
    }
    return true;
}
