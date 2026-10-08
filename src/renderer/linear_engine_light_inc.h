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
//   c176-c189                  plates 1-7's lights (below), c190-c197 the plates
// Shader-local c199 = (cap, -2^-20, 2^-40, 0).
// E = colour x saturate(N . l) x saturate(1 - d^2 / R^2)^2 into r14.xyz, r15
// scratch; the fill block then adds min(E, saturate(cap - decode(sum))) to
// the decoded lobe sum before the encode, so a lit plate never exceeds the
// cap where the native lighting did not (E 1: the plume's own radiance class).
//
// Nozzle plates (docs/architecture/engine-light.md "Nozzle plates"): in a twin
// that also carries the hull light-map gain g, the block leaves the plate
// weight w in r15.w, the maximum over the ship's plates (up to eight main
// nozzles, each at its nozzle's light point P_i with its value_eff v_i) of
// 1 within nozzle_plate_full x v_i, 0 from nozzle_plate_reach x v_i, linear
// in d^2 between; the gain site then scales the light-map term by
// g - (g - 1) w instead of g (texture value alone on the nozzle plates, the
// full gain on windows and markings elsewhere). Uploaded per draw beside the
// light: c190 + i = ((P_i - cam) / v_i, 1 / v_i), i < 8 (an unused slot
// (2, 0, 0, 0): (d / v)^2 = 4, weight 0), and c202.w = the ship's plate tier
// (0: one plate, 1: two to four, 2: five to eight) for the uniform branches. The block forms the pixel's
// camera-relative position D = e t once, then per plate
// u_i = (D / v_i - (P_i - cam) / v_i)^2 (MAD, DP3) and m = min(u_i, m)
// (input first: a NaN u_i keeps m; m starts at c198.w = 4); after the light
// w = saturate(A m + B), A = -1 / (r1^2 - r0^2), B = r1^2 / (r1^2 - r0^2)
// (w is decreasing in (d / v)^2, so the min of the distances is the max of
// the weights), then max(w, 0) (input first). Shader-local c198 = (A, B, 0, 4).
//
// A light per plate (user decision 2026-10-08; engine-light.md "A light per
// plate"): each plate i carries its nozzle's light, plate 0's in c200-c201,
// plates 1-7 in c176 + 2 (i - 1) = (L_i - cam, R_i^2) and c177 + 2 (i - 1) =
// (colour_i, 1 / R_i^2). The pixel takes the light of the plate with the
// least u_i = (d_i / v_i)^2 (ties: the earlier, brighter plate), the plate
// nearest in units of its value_eff; R_i = 3 v_i for every plate, so that is
// the plate whose falloff saturate(1 - d_i^2 / R_i^2)^2 is the largest, and
// beyond every plate's reach the light is 0 whichever is taken. The selection
// copies the two registers into r12 (position, R^2) and r13 (colour, 1 / R^2)
// (the fill block's scratch, dead before it), starting from c200 / c201, and
// per further slot run by the tier branches: cond = u_i - s (r14.y), CMP r12
// and r13 (cond >= 0 keeps the earlier; a NaN cond takes the slot, whose E
// then is NaN and cleared by the final max), s = min(u_i, s) in r14.w (s =
// u_0 from slot 0). The light law then reads r12 / r13 instead of c200 /
// c201: one falloff evaluation per pixel. A one-plate ship (tier 0) runs no
// slot beyond 0, so its light reads c200 / c201's values exactly as before.
// The non-plate twins (no light-map gain) carry the same selection; they keep
// e and t in r15 and form D = e t per slot (one MUL more each). A twin whose
// gain site cannot carry the plate weight (the block not before the light-map
// fetch at depth 0) keeps the single light of plate 0 (no branches between
// the fetch and the final; not seen in the corpus).
constexpr unsigned engine_light_constant = 200, engine_light_definition = 199, engine_light_term = 14,
                   engine_light_scratch = 15, engine_light_eye = 2, engine_light_normal = 3,
                   engine_light_plate_constant = 198, engine_light_plate_first = 190, engine_light_plate_slots = 8,
                   engine_light_select_first = 176, engine_light_selected = 12;
static_assert(engine_light_plate_first + engine_light_plate_slots == engine_light_plate_constant,
              "the plate registers sit directly below c198");
static_assert(engine_light_select_first + 2 * (engine_light_plate_slots - 1) == engine_light_plate_first,
              "the plate lights sit directly below the plates: one refusal range c176-c202");
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
// The plate block's second branch compares the tier with c199.x, the cap: it must stay 1 (the tier between two to four
// plates and five to eight), and c198.z the tier 0.
constexpr float engine_light_plate_tier_one = 1.0f;
static_assert(engine_light_cap == engine_light_plate_tier_one,
              "the plate block's slots 5-8 branch reads c199.x as the tier 1: a cap other than 1 needs its own DEF");
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
// if_ne with the comparison NE (5) in the token's bits 16-23; endif.
constexpr unsigned engine_light_if_ne = 41u | (5u << 16), engine_light_endif = 43u;
// Without `select` (a gained twin whose gain site cannot take the plate
// weight): 18 instructions, 22 weighted slots (two NRM x3), the single light
// c200-c201. With `select` and without `plate` (the twins without light-map
// gain): 80 / 88 (MOV r12 / r13 from c200 / c201, two uniform if_ne / endif
// pairs around slots 0-7 and 4-7 with two MOVs each for their temporary
// operands (a ship runs 1, 4 or 8 slots), slot 0 three (D = e t, MAD, DP3
// into r14.w), slots 1-7 seven each (D, MAD, DP3, the condition, two CMP, the
// running minimum)); the light then reads r12 / r13. With `plate` (implies the
// selection) 83 / 91: the pixel position D = e t once into r15.xyz, slot 0
// (MAD, DP3, MIN into r15.w from c198.w) ahead of the branches, slots 1-7 six
// each (MAD, DP3 into r14.x, the weight's MIN, the condition, two CMP, the
// running minimum: seven), the weight into r15.w after the light, read by the
// gain site (lightmap_gain_instruction): the light then reads D (ADD instead
// of the MAD) and keeps 1 / d in r15.x, so r15.w carries the weight's running
// minimum through the light.
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
        // Uniform branches on the ship's plate tier (c202.w: 0 one plate, 1 two to four, 2 five to eight; the operands
        // come from constants, so no divergence): slot 0 always (inside the first branch without plates: only the
        // selection needs it), slots 1-3 under if_ne tier, 0 (c198.z with plates, else c199.w), slots 4-7 nested under
        // if_ne tier, 1 (c199.x, the cap 1: engine_light_plate_tier_one). The form the corpus's XT originals use, if_ne
        // on two temporary lanes: the tier and the threshold moved into r14.x / r14.y first (r14.xyz is the per-slot
        // scratch, free between slots; r14.w the selection's running minimum s).
        for (unsigned i = 0; i < engine_light_plate_slots; ++i) {
            if (i == (plate ? 1u : 0u) || i == 4) {
                emit(out, mov, {dst(temp, e, 1), lane(constant, f, 3)}); // r14.x = tier
                emit(out, mov, {dst(temp, e, 2), i == 4 ? lane(constant, k, 0)
                                                 : plate ? lane(constant, c, 2) : lane(constant, k, 3)}); // 0 or 1
                emit(out, engine_light_if_ne, {lane(temp, e, 0), lane(temp, e, 1)});
            }
            const unsigned p = engine_light_plate_first + i;
            if (!plate) emit(out, mul, {dst(temp, e), src(temp, s), lane(temp, s, 3)}); // D = e t
            emit(out, mad, {dst(temp, e), src(temp, plate ? s : e), lane(constant, p, 3), src(constant, p, identity, negate)});
            const unsigned u = i ? 0u : 3u; // (d / v_i)^2: r14.w for slot 0 (s = u_0), else r14.x
            emit(out, 8, {dst(temp, e, 1u << u), src(temp, e), src(temp, e)});
            if (plate)
                emit(out, min_op, {dst(temp, s, 8), lane(temp, e, u), i ? lane(temp, s, 3) : lane(constant, c, 3)});
            if (!i) continue;
            const unsigned q = engine_light_select_first + 2 * (i - 1);
            emit(out, add, {dst(temp, e, 2), lane(temp, e, 0), src(temp, e, 3 * 0x55, negate)}); // u_i - s
            emit(out, cmp, {dst(temp, la, xyzw), lane(temp, e, 1), src(temp, la), src(constant, q)});
            emit(out, cmp, {dst(temp, lb, xyzw), lane(temp, e, 1), src(temp, lb), src(constant, q + 1)});
            emit(out, min_op, {dst(temp, e, 8), lane(temp, e, 0), lane(temp, e, 3)}); // s = min(u_i, s) (input first)
        }
        emit(out, engine_light_endif, {});
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
            if (t == constant && r >= engine_light_select_first && r <= engine_light_constant + 2) return false;
        }
    }
    return true;
}
// The emitted block references only its own temporaries (r14/r15, and the
// selected light r12/r13, the fill block's scratch: dead before it), v2/v3/vD
// and its constants (c176-c202); the fill block with the light reads r14
// besides its own set.
bool engine_light_range_free(const Word* code, std::size_t begin, std::size_t end, unsigned depth_input) noexcept {
    for (std::size_t at = begin; at < end;) {
        const Word token = code[at];
        const unsigned op = token & 0xffff, n = length(token);
        if (at + n >= end || op == def || op == dcl || op == 0xfffe) return false;
        for (unsigned q = 1; q <= n; ++q) {
            const auto t = kind(code[at + q]), r = index(code[at + q]);
            if (t == temp && (r < engine_light_selected || r > engine_light_scratch)) return false;
            if (t == constant && (r < engine_light_select_first || r > engine_light_constant + 2)) return false;
            if (t == input && r != engine_light_eye && r != engine_light_normal && r != depth_input) return false;
            if (t != temp && t != constant && t != input) return false;
        }
        at += n + 1;
    }
    return true;
}
