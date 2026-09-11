// Host-only structural oracle for every row of the generated profile table.
// Raw game programs are read from a local directory at run time, never embedded.
// Usage: material_motion_structure <programs directory>
// Rows whose original programs are absent locally are skipped with a ROW line
// saying so; the runner decides whether a skip is acceptable.
#include "../../src/renderer/material_motion.h"
#include "../../src/renderer/rigid_motion_pixel_program.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Words = std::vector<std::uint32_t>;
using namespace x3m::renderer;
static unsigned checks = 0;
static std::size_t mutations_total = 0, aliases_total = 0;
static void require_at(int line, bool value, const char* what = "structural mismatch") {
    if (!value) throw std::runtime_error(std::string(what) + " (line " + std::to_string(line) + ")");
}
#define require(...) require_at(__LINE__, __VA_ARGS__)
static void passed(unsigned row, const char* name) {
    ++checks;
    std::cout << "CHECK row=" << row << ' ' << name << '\n';
}
static bool read(const std::string& path, Words& result) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), {});
    require(bytes.size() % 4 == 0, "local program size is not a DWORD multiple");
    result.clear();
    for (std::size_t i = 0; i < bytes.size(); i += 4)
        result.push_back(std::uint32_t(bytes[i]) | (std::uint32_t(bytes[i+1]) << 8) |
            (std::uint32_t(bytes[i+2]) << 16) | (std::uint32_t(bytes[i+3]) << 24));
    return true;
}
static std::string program_path(const std::string& directory, const char* stage, std::uint64_t fingerprint) {
    char name[64];
    std::snprintf(name, sizeof name, "/%s_%016llx.bin", stage, static_cast<unsigned long long>(fingerprint));
    return directory + name;
}
static Words slice(const Words& w, std::size_t begin, std::size_t end) {
    require(begin <= end && end <= w.size());
    return Words(w.begin() + begin, w.begin() + end);
}
static unsigned kind(std::uint32_t word) { return ((word & 0x70000000u) >> 28) | ((word & 0x1800u) >> 8); }
static unsigned opcode(std::uint32_t word) { return word & 0xffffu; }
static std::size_t length(std::uint32_t word) { return opcode(word) == 0xfffe ? ((word >> 16) & 0x7fffu) : ((word >> 24) & 15u); }
static bool executable(std::uint32_t word) { const auto op = opcode(word); return op != 0xfffe && op != 0x1f && op != 0x51 && op != 0x2f && op != 0x30; }
static bool direct(std::uint32_t word, unsigned type) { return (word & 0x80002000u) == 0x80000000u && kind(word) == type; }
// Instruction start offsets in order, comments included, END excluded.
static std::vector<std::size_t> boundaries(const Words& w) {
    std::vector<std::size_t> at;
    for (std::size_t i = 1; i < w.size();) {
        if (opcode(w[i]) == 0xffff) { require(i == w.size() - 1, "END is not the last word"); return at; }
        const auto n = length(w[i]);
        require(n <= w.size() - i - 1, "instruction overruns the program");
        at.push_back(i); i += n + 1;
    }
    require(false, "program has no END"); return at;
}
// One relocated operand of the authored fragment: only the register index may
// change, to the row's choice for that register class.
static void operand(const MotionOutputProfile& row, std::uint32_t original, std::uint32_t relocated) {
    require((original & 0x80002000u) == 0x80000000u);
    // No bit outside the register index may change: class, write mask, swizzle,
    // source modifier, destination modifier and shift are compared bitwise.
    require((original & ~0x7ffu) == (relocated & ~0x7ffu));
    unsigned expected = 0;
    switch (kind(original)) {
    case 0: require((original & 0x7ffu) <= 2); expected = (original & 0x7ffu) + row.pixel_temporary_base; break;
    case 1: require((original & 0x7ffu) == 0); expected = row.pixel_input_register; break;
    case 2: require((original & 0x7ffu) <= 4); expected = (original & 0x7ffu) + row.pixel_constant_base; break;
    case 8: require((original & 0x7ffu) == 0); expected = row.pixel_output_register; break;
    default: require(false);
    }
    require((relocated & 0x7ffu) == expected);
}
static void authored(const MotionOutputProfile& row, const Words& defs, const Words& declaration, const Words& body) {
    const auto& source = rigid_motion_pixel_program();
    const std::size_t extent = std::size(source);
    require(source[0] == 0xffff0300u && source[extent - 1] == 0xffffu);
    std::size_t d = 0, b = 0, declarations = 0, output_count = 0;
    for (std::size_t i = 1; i < extent - 1;) {
        const auto instruction = source[i], op = instruction & 0xffffu;
        if (op == 0xfffe) {
            const auto skip = 1 + ((instruction >> 16) & 0x7fffu);
            require(skip <= extent - i - 1); i += skip; continue;
        }
        if (op == 81) {
            require(instruction == 0x05000051u && i + 6 <= extent - 1 && d + 6 <= defs.size());
            require(defs[d] == instruction); operand(row, source[i+1], defs[d+1]);
            for (unsigned literal = 2; literal != 6; ++literal) require(source[i+literal] == defs[d+literal]);
            d += 6; i += 6; continue;
        }
        if (op == 31) {
            require(i + 3 <= extent - 1 && instruction == 0x0200001fu);
            require(source[i+1] == 0x80000005u && source[i+2] == 0x900f0000u);
            require(declaration == Words({instruction, 0x80000005u | (std::uint32_t(row.texcoord_index) << 16),
                                          0x900f0000u | row.pixel_input_register}));
            ++declarations; i += 3; continue;
        }
        // Decode known operation arity independently of the instruction length.
        const unsigned arity = op == 1 || op == 6 ? 2 :
            op == 2 || op == 5 || op == 8 || op == 9 || op == 11 ? 3 :
            op == 4 || op == 88 ? 4 : 0;
        require(arity != 0 && instruction == ((arity << 24) | op));
        require(i + arity + 1 <= extent - 1 && b + arity + 1 <= body.size());
        require(body[b] == instruction);
        for (unsigned k = 1; k <= arity; ++k) operand(row, source[i+k], body[b+k]);
        if (kind(source[i+1]) == 8) ++output_count;
        i += arity + 1; b += arity + 1;
    }
    require(d == 18 && d == defs.size() && b == 111 && b == body.size() && declarations == 1 && output_count == 1);
}
static bool same(const MaterialMotionVariant& a, const MaterialMotionVariant& b) {
    return a.vertex == b.vertex && a.pixel == b.pixel;
}
// Output hashes of the hand-written Argon transformer this table-driven one
// replaced (commit 1b72239 lineage); the refactor must reproduce them exactly.
constexpr std::uint64_t argon_vertex = 0x53a0a641107ed76cull, argon_pixel = 0x8759c7838bbc86c2ull;
constexpr std::uint64_t argon_vertex_variant_fnv = 0x07805a216f19b9b6ull;
constexpr std::uint64_t argon_pixel_variant_fnv = 0x92bd1a32ae7d2a6dull;
constexpr std::size_t vertex_added = 3 + 16, pixel_added = 18 + 3 + 111;

static void test_row(unsigned index, const MotionOutputProfile& row, const Words& vs, const Words& ps) {
    require(vs.size() == row.vertex_dword_count && ps.size() == row.pixel_dword_count, "local program length differs from the row");
    const std::size_t D = row.vertex_declaration_insert_dword, A = row.vertex_arithmetic_insert_dword;
    const std::size_t Pd = row.pixel_definition_insert_dword, Pc = row.pixel_declaration_insert_dword, Pa = row.pixel_append_dword;
    MaterialMotionVariant output;
    require(material_motion_variant(vs.data(), vs.size(), ps.data(), ps.size(), output) == MaterialMotionResult::Applied);
    require(output.vertex.size() == vs.size() + vertex_added && output.pixel.size() == ps.size() + pixel_added);
    // The per-stage lookups the live route uses must produce the same programs
    // as the pair form, and the row-explicit forms the same again.
    Words stage_vs, stage_ps, explicit_vs, explicit_ps;
    require(material_motion_vertex_variant(vs.data(), vs.size(), stage_vs) == MaterialMotionResult::Applied);
    require(material_motion_pixel_variant(ps.data(), ps.size(), stage_ps) == MaterialMotionResult::Applied);
    require(material_motion_vertex_variant_for(row, vs.data(), vs.size(), explicit_vs) == MaterialMotionResult::Applied);
    require(material_motion_pixel_variant_for(row, ps.data(), ps.size(), explicit_ps) == MaterialMotionResult::Applied);
    require(stage_vs == output.vertex && stage_ps == output.pixel && explicit_vs == output.vertex && explicit_ps == output.pixel);
    require(material_motion_profile(row.vertex_fingerprint, row.pixel_fingerprint) == &row);
    require(material_motion_pair_reviewed(row.vertex_fingerprint, row.pixel_fingerprint));
    require(!material_motion_pair_reviewed(row.vertex_fingerprint, 0) && !material_motion_pair_reviewed(0, row.pixel_fingerprint));
    passed(index, "pair_and_per_stage_lookups_applied_identically");
    if (row.vertex_fingerprint == argon_vertex && row.pixel_fingerprint == argon_pixel) {
        require(output.vertex.size() == 545 && output.pixel.size() == 1392);
        require(material_motion_fingerprint(output.vertex.data(), output.vertex.size()) == argon_vertex_variant_fnv);
        require(material_motion_fingerprint(output.pixel.data(), output.pixel.size()) == argon_pixel_variant_fnv);
        passed(index, "argon_output_byte_identical_to_previous_transformer");
    }
    auto stripped_vs = output.vertex, stripped_ps = output.pixel;
    stripped_vs.erase(stripped_vs.begin() + A + 3, stripped_vs.begin() + A + 19);
    stripped_vs.erase(stripped_vs.begin() + D, stripped_vs.begin() + D + 3);
    stripped_ps.erase(stripped_ps.begin() + Pa + 21, stripped_ps.begin() + Pa + 21 + 111);
    stripped_ps.erase(stripped_ps.begin() + Pc + 18, stripped_ps.begin() + Pc + 21);
    stripped_ps.erase(stripped_ps.begin() + Pd, stripped_ps.begin() + Pd + 18);
    require(stripped_vs == vs && stripped_ps == ps);
    passed(index, "all_original_words_preserved");
    require(slice(output.vertex, D, D + 3) == Words({0x0200001fu, 0x80000005u | (std::uint32_t(row.texcoord_index) << 16),
                                                    0xe00f0000u | row.vertex_output_register}));
    for (unsigned lane = 0; lane != 4; ++lane) {
        const std::size_t at = A + 3 + 4 * lane, original = row.position_dp4_dwords[lane];
        require(vs[original] == 0x03000009u && vs[original + 1] == (0xe0000000u | (1u << (16 + lane))));
        require(vs[original + 2] == (0x80e40000u | row.position_temporary) && vs[original + 3] == (0xa0e40000u | (row.matrix_register + lane)));
        require(output.vertex[at] == vs[original]);
        require(output.vertex[at + 1] == (vs[original + 1] | row.vertex_output_register));
        require(output.vertex[at + 2] == vs[original + 2]);
        require(output.vertex[at + 3] == (0xa0e40000u | (row.vertex_constant_base + lane)));
    }
    require(row.vertex_constant_base == MaterialMotionAbi::previous_vertex_constant && row.pixel_constant_base == MaterialMotionAbi::pixel_coordinates_constant &&
        row.pixel_constant_base + 1 == MaterialMotionAbi::pixel_mode_constant && row.pixel_output_register == MaterialMotionAbi::motion_render_target);
    passed(index, "vertex_previous_clip_and_public_abi");
    authored(row, slice(output.pixel, Pd, Pd + 18), slice(output.pixel, Pc + 18, Pc + 21), slice(output.pixel, Pa + 21, Pa + 21 + 111));
    passed(index, "authored_fragment_register_bits_literals_and_opcodes");
    const MaterialMotionVariant sentinel{{0x12345678u, 0xdeadbeefu}, {0x87654321u}};
    auto refuse = [&](const std::uint32_t* v, std::size_t vn, const std::uint32_t* p, std::size_t pn, MaterialMotionResult expected) {
        auto target = sentinel;
        require(material_motion_variant(v, vn, p, pn, target) == expected && same(target, sentinel));
    };
    for (unsigned stage = 0; stage != 2; ++stage) {
        refuse(stage ? vs.data() : nullptr, vs.size(), stage ? nullptr : ps.data(), ps.size(), MaterialMotionResult::InvalidInput);
        for (std::size_t n : {0u, 1u}) refuse(vs.data(), stage ? vs.size() : n, ps.data(), stage ? n : ps.size(), MaterialMotionResult::InvalidInput);
        refuse(vs.data(), vs.size() - (stage == 0), ps.data(), ps.size() - (stage == 1), MaterialMotionResult::UnsupportedShader);
    }
    auto extra_vs = vs, extra_ps = ps; extra_vs.push_back(0xffffu); extra_ps.push_back(0xffffu);
    refuse(extra_vs.data(), extra_vs.size(), ps.data(), ps.size(), MaterialMotionResult::UnsupportedShader);
    refuse(vs.data(), vs.size(), extra_ps.data(), extra_ps.size(), MaterialMotionResult::UnsupportedShader);
    refuse(ps.data(), ps.size(), vs.data(), vs.size(), MaterialMotionResult::UnsupportedShader);
    passed(index, "invalid_wrong_pair_truncated_appended_atomic_refusal");
    // Row-explicit forms: the fingerprint gate makes the structural revalidation
    // unreachable through the lookups, so perturb the row instead. Every
    // perturbation must be refused without touching the output.
    unsigned perturbations = 0;
    auto refuse_row = [&](const MotionOutputProfile& changed, MaterialMotionResult expected_vs, MaterialMotionResult expected_ps) {
        // A stage the perturbation does not touch must still transform to the
        // reference output; the refused stage must leave the sentinel intact.
        Words target_vs = sentinel.vertex, target_ps = sentinel.pixel;
        const std::string label = "row perturbation " + std::to_string(perturbations);
        const auto vertex_result = material_motion_vertex_variant_for(changed, vs.data(), vs.size(), target_vs);
        require(vertex_result == expected_vs, (label + " vertex result").c_str());
        require(target_vs == (expected_vs == MaterialMotionResult::Applied ? output.vertex : sentinel.vertex), (label + " vertex output").c_str());
        const auto pixel_result = material_motion_pixel_variant_for(changed, ps.data(), ps.size(), target_ps);
        require(pixel_result == expected_ps, (label + " pixel result").c_str());
        require(target_ps == (expected_ps == MaterialMotionResult::Applied ? output.pixel : sentinel.pixel), (label + " pixel output").c_str());
        ++perturbations;
    };
    const auto mismatch = MaterialMotionResult::ProfileMismatch, applied = MaterialMotionResult::Applied;
    { auto r = row; r.transformation_class = MotionOutputClass::RelocatedRegistersWithBranches;
      refuse_row(r, MaterialMotionResult::UnsupportedShader, MaterialMotionResult::UnsupportedShader); }
    { auto r = row; r.vertex_output_register = 0; refuse_row(r, mismatch, applied); }        // o0 is declared and written.
    { auto r = row; r.texcoord_index = 0; refuse_row(r, mismatch, mismatch); }              // TEXCOORD0 declared in both stages.
    { auto r = row; r.position_temporary ^= 1; refuse_row(r, mismatch, applied); }
    { auto r = row; r.matrix_register += 1; refuse_row(r, mismatch, applied); }
    { auto r = row; for (auto& d : r.position_dp4_dwords) d += 1; r.vertex_arithmetic_insert_dword += 1; refuse_row(r, mismatch, applied); }
    // Perturbations that make the row itself malformed are refused by both stages.
    { auto r = row; r.position_lane_masks[1] = 1; refuse_row(r, mismatch, mismatch); }
    { auto r = row; r.vertex_declaration_insert_dword -= 1; refuse_row(r, mismatch, applied); }
    { auto r = row; r.vertex_declaration_insert_dword += 1; refuse_row(r, mismatch, applied); }
    { auto r = row; r.vertex_constant_base = row.matrix_register; refuse_row(r, mismatch, mismatch); } // Rows overlap.
    { auto r = row; r.vertex_constant_base = 40; refuse_row(r, mismatch, applied); }        // Material constants in use.
    { auto r = row; r.light_loop_bound_required = false; refuse_row(r, mismatch, applied); } // VS addresses relatively.
    { auto r = row; r.pixel_input_register = 0; refuse_row(r, applied, mismatch); }         // v0 declared.
    { auto r = row; r.pixel_temporary_base = 0; refuse_row(r, applied, mismatch); }         // r0 written.
    { auto r = row; r.pixel_constant_base = 0; refuse_row(r, applied, mismatch); }          // c0 read.
    { auto r = row; r.pixel_output_register = 0; refuse_row(r, mismatch, mismatch); }       // oC0 is the color target.
    { auto r = row; r.pixel_definition_insert_dword -= 1; refuse_row(r, applied, mismatch); }
    { auto r = row; r.pixel_declaration_insert_dword += 1; refuse_row(r, applied, mismatch); }
    { auto r = row; r.pixel_declaration_insert_dword -= 1; refuse_row(r, applied, mismatch); }
    { auto r = row; r.pixel_append_dword -= 1; r.pixel_dword_count -= 1;
      refuse_row(r, applied, MaterialMotionResult::UnsupportedShader); }                    // Length gate first.
    { auto r = row; r.vertex_version = 0xfffe0200u; r.pixel_version = 0xffff0200u; refuse_row(r, mismatch, mismatch); }
    require(perturbations == 21);
    passed(index, "twenty_one_row_perturbations_refused_atomically");
    // Program-side perturbations: a row copy carrying the perturbed program's
    // fingerprint satisfies the fingerprint gate, so these reach the structural
    // revalidation with real programs and prove each refusal from the words,
    // not from the row. The untouched stage still transforms to the reference
    // output, and through the table lookups the perturbed program is simply
    // not a reviewed original.
    unsigned program_perturbations = 0;
    auto refuse_program = [&](const Words& changed_vs, const Words& changed_ps, const char* what) {
        auto r = row;
        r.vertex_fingerprint = material_motion_fingerprint(changed_vs.data(), changed_vs.size());
        r.pixel_fingerprint = material_motion_fingerprint(changed_ps.data(), changed_ps.size());
        const bool vs_changed = changed_vs != vs, ps_changed = changed_ps != ps;
        require(vs_changed != ps_changed, what);
        Words target_vs = sentinel.vertex, target_ps = sentinel.pixel, lookup;
        const auto vertex_result = material_motion_vertex_variant_for(r, changed_vs.data(), changed_vs.size(), target_vs);
        const auto pixel_result = material_motion_pixel_variant_for(r, changed_ps.data(), changed_ps.size(), target_ps);
        require(vertex_result == (vs_changed ? mismatch : applied) && target_vs == (vs_changed ? sentinel.vertex : output.vertex), what);
        require(pixel_result == (ps_changed ? mismatch : applied) && target_ps == (ps_changed ? sentinel.pixel : output.pixel), what);
        require((vs_changed ? material_motion_vertex_variant(changed_vs.data(), changed_vs.size(), lookup)
                            : material_motion_pixel_variant(changed_ps.data(), changed_ps.size(), lookup)) == MaterialMotionResult::UnsupportedShader && lookup.empty(), what);
        ++program_perturbations;
    };
    const auto vs_at = boundaries(vs), ps_at = boundaries(ps);
    auto find = [&](const std::vector<std::size_t>& at, auto&& predicate) {
        for (auto i : at) if (predicate(i)) return i;
        throw std::runtime_error("program perturbation target not found");
    };
    // Operand k (1-based) of the executable instruction at i that satisfies the predicate, or 0.
    auto operand_of = [&](const Words& w, std::size_t i, auto&& predicate) -> std::size_t {
        if (!executable(w[i])) return 0;
        for (std::size_t k = 1; k <= length(w[i]); ++k) if (predicate(w[i + k])) return k;
        return 0;
    };
    auto is_dp4 = [&](std::size_t i) { return std::find(std::begin(row.position_dp4_dwords), std::end(row.position_dp4_dwords), i) != std::end(row.position_dp4_dwords); };
    auto with_index = [](std::uint32_t word, unsigned index) { return (word & ~0x7ffu) | index; };
    // Vertex side: header declarations, executable references, framing.
    {
        const auto position = find(vs_at, [&](std::size_t i) { return i < D && opcode(vs[i]) == 0x1f && direct(vs[i + 2], 6) && (vs[i + 2] & 0x7ffu) == 0 && (vs[i + 1] & 0x1fu) == 0; });
        auto w = vs; w[position + 1] = (w[position + 1] & ~0x1fu) | 3u; refuse_program(w, ps, "o0 not declared as POSITION");
        w = vs; w[position + 2] = with_index(w[position + 2], row.vertex_output_register); refuse_program(w, ps, "chosen output register declared");
        const auto texcoord = find(vs_at, [&](std::size_t i) { return i < D && opcode(vs[i]) == 0x1f && direct(vs[i + 2], 6) && (vs[i + 1] & 0x1fu) == 5 && ((vs[i + 1] >> 16) & 0xfu) != row.texcoord_index; });
        w = vs; w[texcoord + 1] = (w[texcoord + 1] & ~0xf0000u) | (std::uint32_t(row.texcoord_index) << 16); refuse_program(w, ps, "chosen TEXCOORD index declared");
        const auto writes_output = find(vs_at, [&](std::size_t i) { return i >= D && !is_dp4(i) && executable(vs[i]) && length(vs[i]) >= 1 && direct(vs[i + 1], 6); });
        w = vs; w[writes_output + 1] = with_index(w[writes_output + 1], row.vertex_output_register); refuse_program(w, ps, "chosen output register written");
        std::size_t constant_operand = 0;
        const auto reads_constant = find(vs_at, [&](std::size_t i) { return i >= D && !is_dp4(i) && (constant_operand = operand_of(vs, i, [](std::uint32_t t) { return direct(t, 2); })) != 0; });
        w = vs; w[reads_constant + constant_operand] = with_index(w[reads_constant + constant_operand], row.vertex_constant_base); refuse_program(w, ps, "previous-row constant read");
        const auto first = find(vs_at, [&](std::size_t i) { return i >= D && executable(vs[i]); });
        w = vs; w[first] = (w[first] & ~0xffffu) | 0x51u; refuse_program(w, ps, "definition after the vertex header");
        w = vs; w[row.position_dp4_dwords[0]] |= 0x10000000u; refuse_program(w, ps, "predicated position dot");
        w = vs; w.back() = 0x0001ffffu; refuse_program(w, ps, "vertex END token malformed");
        w = vs; w[vs_at.back()] |= 0x0f000000u; refuse_program(w, ps, "vertex instruction length overruns END");
    }
    // Pixel side: definitions, declarations, executable references, refused opcodes, framing.
    {
        const auto definition = find(ps_at, [&](std::size_t i) { return i < Pd && opcode(ps[i]) == 0x51; });
        auto w = ps; w[definition + 1] = with_index(w[definition + 1], row.pixel_constant_base); refuse_program(vs, w, "ABI constant defined");
        const auto input = find(ps_at, [&](std::size_t i) { return i >= Pd && i < Pc && opcode(ps[i]) == 0x1f && direct(ps[i + 2], 1); });
        w = ps; w[input + 2] = with_index(w[input + 2], row.pixel_input_register); refuse_program(vs, w, "chosen input register declared");
        w = ps; w[input + 1] = (w[input + 1] & ~0xf001fu) | 5u | (std::uint32_t(row.texcoord_index) << 16); refuse_program(vs, w, "chosen TEXCOORD index declared as input");
        w = ps; w[input] = (w[input] & ~0xffffu) | 0x51u; refuse_program(vs, w, "definition between the pixel inserts");
        const auto writes_temporary = find(ps_at, [&](std::size_t i) { return i >= Pc && executable(ps[i]) && length(ps[i]) >= 1 && direct(ps[i + 1], 0); });
        w = ps; w[writes_temporary + 1] = with_index(w[writes_temporary + 1], row.pixel_temporary_base); refuse_program(vs, w, "chosen temporary written");
        std::size_t constant_operand = 0;
        const auto reads_constant = find(ps_at, [&](std::size_t i) { return i >= Pc && (constant_operand = operand_of(ps, i, [](std::uint32_t t) { return direct(t, 2); })) != 0; });
        w = ps; w[reads_constant + constant_operand] = with_index(w[reads_constant + constant_operand], row.pixel_constant_base); refuse_program(vs, w, "ABI constant read");
        w = ps; w[reads_constant + constant_operand] |= 0x2000u; refuse_program(vs, w, "relative addressing in the pixel program");
        const auto writes_color = find(ps_at, [&](std::size_t i) { return i >= Pc && executable(ps[i]) && length(ps[i]) >= 1 && direct(ps[i + 1], 8); });
        w = ps; w[writes_color + 1] = with_index(w[writes_color + 1], row.pixel_output_register); refuse_program(vs, w, "motion color output written");
        w = ps; w[writes_color + 1] = (w[writes_color + 1] & ~0x70001fffu) | (1u << 28) | 0x800u; refuse_program(vs, w, "oDepth written");
        const auto first = find(ps_at, [&](std::size_t i) { return i >= Pc && executable(ps[i]); });
        for (auto op : {0x41u, 0x28u, 0x60u, 0x51u}) { // texkill, if, breakp, def
            w = ps; w[first] = (w[first] & ~0xffffu) | op; refuse_program(vs, w, "refused pixel opcode");
        }
        w = ps; w[first] |= 0x10000000u; refuse_program(vs, w, "predicated pixel instruction");
        w = ps; w.back() = 0x0001ffffu; refuse_program(vs, w, "pixel END token malformed");
    }
    require(program_perturbations == 24);
    passed(index, "twenty_four_program_perturbations_refused_by_revalidation");
    std::size_t mutations = 0;
    for (unsigned stage = 0; stage != 2; ++stage) {
        Words changed = stage ? ps : vs;
        for (std::size_t i = 0; i < changed.size(); ++i) for (unsigned bit = 0; bit != 32; ++bit) {
            changed[i] ^= std::uint32_t(1) << bit;
            refuse(stage ? vs.data() : changed.data(), vs.size(), stage ? changed.data() : ps.data(), ps.size(), MaterialMotionResult::UnsupportedShader);
            changed[i] ^= std::uint32_t(1) << bit; ++mutations;
        }
    }
    require(mutations == (vs.size() + ps.size()) * 32);
    mutations_total += mutations;
    passed(index, "every_input_dword_every_bit_atomic_refusals");
    // All single-vector directions plus simultaneous ordinary/cross aliases.
    for (unsigned mode = 0; mode != 6; ++mode) {
        auto target = sentinel;
        const std::uint32_t *v = vs.data(), *p = ps.data();
        if (mode == 0 || mode == 4) { target.vertex = vs; v = target.vertex.data(); }
        if (mode == 1 || mode == 5) { target.pixel = vs; v = target.pixel.data(); }
        if (mode == 2 || mode == 5) { target.vertex = ps; p = target.vertex.data(); }
        if (mode == 3 || mode == 4) { target.pixel = ps; p = target.pixel.data(); }
        require(material_motion_variant(v, vs.size(), p, ps.size(), target) == MaterialMotionResult::Applied && same(target, output));
    }
    aliases_total += 6;
    passed(index, "six_input_output_alias_layouts_applied");
    auto bad_alias = MaterialMotionVariant{ps, vs}; const auto saved = bad_alias;
    require(material_motion_variant(bad_alias.pixel.data(), vs.size() - 1, bad_alias.vertex.data(), ps.size(), bad_alias) == MaterialMotionResult::UnsupportedShader && same(bad_alias, saved));
    passed(index, "cross_alias_refusal_atomic");
    std::printf("ROW index=%u vs=%016llx ps=%016llx class=%c status=PASS vertex_words=%zu pixel_words=%zu mutations=%zu program_perturbations=%u\n",
                index, static_cast<unsigned long long>(row.vertex_fingerprint), static_cast<unsigned long long>(row.pixel_fingerprint),
                row.transformation_class == MotionOutputClass::ReferenceRegisters ? 'A' : 'B', output.vertex.size(), output.pixel.size(), mutations, program_perturbations);
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: material_motion_structure <programs directory>");
        const std::string directory = argv[1];
        std::printf("TABLE rows=%zu\n", motion_output_profile_count);
        unsigned transformed = 0, skipped = 0, index = 0;
        for (const auto& row : motion_output_profiles) {
            Words vs, ps;
            const bool have_vs = read(program_path(directory, "vs", row.vertex_fingerprint), vs);
            const bool have_ps = read(program_path(directory, "ps", row.pixel_fingerprint), ps);
            if (!have_vs || !have_ps) {
                std::printf("ROW index=%u vs=%016llx ps=%016llx status=SKIP reason=missing_local_program%s%s\n", index,
                            static_cast<unsigned long long>(row.vertex_fingerprint), static_cast<unsigned long long>(row.pixel_fingerprint),
                            have_vs ? "" : " vs", have_ps ? "" : " ps");
                ++skipped; ++index;
                continue;
            }
            test_row(index, row, vs, ps);
            ++transformed; ++index;
        }
        require(transformed + skipped == motion_output_profile_count);
        std::printf("RESULT PASS rows=%zu transformed=%u skipped=%u checks=%u mutations=%zu aliases=%zu\n",
                    motion_output_profile_count, transformed, skipped, checks, mutations_total, aliases_total);
        return 0;
    } catch (const std::exception& e) { std::cerr << "RESULT FAIL " << e.what() << '\n'; return 1; }
}
