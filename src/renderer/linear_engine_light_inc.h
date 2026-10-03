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
//   c200 = (L - cam, R^2)      the light relative to the camera, world axes
//   c201 = (colour, 1 / R^2)   linear colour of the light
//   c202 = (F, 0)              the camera's forward axis, world
// Shader-local c199 = (cap, -2^-20, 2^-40, 0).
// E = colour x saturate(N . l) x saturate(1 - d^2 / R^2)^2 into r14.xyz, r15
// scratch; the fill block then adds min(E, saturate(cap - decode(sum))) to
// the decoded lobe sum before the encode, so a lit plate never exceeds the
// cap where the native lighting did not (E 1: the plume's own radiance class).
constexpr unsigned engine_light_constant = 200, engine_light_definition = 199, engine_light_term = 14,
                   engine_light_scratch = 15, engine_light_eye = 2, engine_light_normal = 3;
constexpr float engine_light_cap = 1.0f, engine_light_facing_floor = -0x1p-20f, engine_light_distance_floor = 0x1p-40f;
constexpr unsigned engine_light_eye_texcoord = 1, engine_light_normal_texcoord = 2;
void engine_light_definition_words(Words& out) {
    emit(out, def,
         {dst(constant, engine_light_definition, xyzw), bits(engine_light_cap), bits(engine_light_facing_floor),
          bits(engine_light_distance_floor), bits(0.0f)});
}
// 18 instructions, 22 weighted slots (two NRM x3).
void engine_light_block(Words& out, unsigned depth_input) {
    const unsigned e = engine_light_term, s = engine_light_scratch, a = engine_light_constant,
                   b = engine_light_constant + 1, f = engine_light_constant + 2, k = engine_light_definition;
    constexpr unsigned negate = 1;
    emit(out, 36, {dst(temp, s), src(input, engine_light_eye)});                 // e = nrm(v2)
    emit(out, 8, {dst(temp, s, 8), src(temp, s), src(constant, f)});             // e . F
    emit(out, min_op, {dst(temp, s, 8), lane(temp, s, 3), lane(constant, k, 1)}); // <= -2^-20 (input first)
    emit(out, 6, {dst(temp, s, 8), lane(temp, s, 3)});                           // 1 / (e . F)
    emit(out, mul, {dst(temp, s, 8), lane(temp, s, 3), lane(input, depth_input, 1)}); // t = w / (e . F)
    emit(out, mad, {dst(temp, e), src(temp, s), src(temp, s, 3 * 0x55, negate), src(constant, a)}); // L - cam - e t
    emit(out, 8, {dst(temp, e, 8), src(temp, e), src(temp, e)});                 // d^2
    emit(out, max_op, {dst(temp, e, 8), lane(temp, e, 3), lane(constant, k, 2)}); // >= 2^-40 (input first)
    emit(out, 7, {dst(temp, s, 8), lane(temp, e, 3)});                           // 1 / d
    emit(out, mul, {dst(temp, e), src(temp, e), lane(temp, s, 3)});              // l
    emit(out, 36, {dst(temp, s), src(input, engine_light_normal)});              // N = nrm(v3)
    emit(out, 8, {dst(temp, s, 1) | sat, src(temp, s), src(temp, e)});           // saturate(N . l)
    emit(out, add, {dst(temp, e, 8), src(temp, e, 3 * 0x55, negate), lane(constant, a, 3)}); // R^2 - d^2
    emit(out, mul, {dst(temp, e, 8) | sat, lane(temp, e, 3), lane(constant, b, 3)}); // q = saturate(1 - d^2 / R^2)
    emit(out, mul, {dst(temp, e, 8), lane(temp, e, 3), lane(temp, e, 3)});       // q^2
    emit(out, mul, {dst(temp, e, 8), lane(temp, e, 3), lane(temp, s, 0)});       // q^2 (N . l)
    emit(out, mul, {dst(temp, e), lane(temp, e, 3), src(constant, b)});          // E = colour q^2 (N . l)
    emit(out, max_op, {dst(temp, e), src(temp, e), lane(constant, k, 3)});       // NaN / negative -> 0 (input first)
}
// Inside the fill block, after the decoded sum (and the fill MAD) sit in r12:
// r13 = min(E, saturate(cap - r12)), r12 += r13. Three instructions, 3 slots.
void engine_light_add(Words& out) {
    emit(out, add, {dst(temp, 13) | sat, src(temp, 12, identity, 1), lane(constant, engine_light_definition, 0)});
    emit(out, min_op, {dst(temp, 13), src(temp, engine_light_term), src(temp, 13)});
    emit(out, add, {dst(temp, 12), src(temp, 12), src(temp, 13)});
}
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
            if (t == temp && (r == engine_light_term || r == engine_light_scratch)) return false;
            if (t == constant && r >= engine_light_definition && r <= engine_light_constant + 2) return false;
        }
    }
    return true;
}
// The emitted block references only its own temporaries, v2/v3/vD and its
// constants; the fill block with the light reads r14 besides its own set.
bool engine_light_range_free(const Word* code, std::size_t begin, std::size_t end, unsigned depth_input) noexcept {
    for (std::size_t at = begin; at < end;) {
        const Word token = code[at];
        const unsigned op = token & 0xffff, n = length(token);
        if (at + n >= end || op == def || op == dcl || op == 0xfffe) return false;
        for (unsigned q = 1; q <= n; ++q) {
            const auto t = kind(code[at + q]), r = index(code[at + q]);
            if (t == temp && r != engine_light_term && r != engine_light_scratch) return false;
            if (t == constant && (r < engine_light_definition || r > engine_light_constant + 2)) return false;
            if (t == input && r != engine_light_eye && r != engine_light_normal && r != depth_input) return false;
            if (t != temp && t != constant && t != input) return false;
        }
        at += n + 1;
    }
    return true;
}
