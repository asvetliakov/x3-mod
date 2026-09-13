#include "material_motion.h"
#include "rigid_motion_pixel_program.h"
#include "current_depth_pixel_program.h"
#include "damage_motion_validation.h"
#include <iterator>

namespace x3m::renderer {
namespace {
using Words = std::vector<std::uint32_t>;

// Direct3D 9 shader token encoding (documented in the DirectX SDK "Shader
// Codes" reference). Instruction tokens carry the opcode in bits 0-15 and the
// length in bits 24-27 (comments: bits 16-30); parameter tokens have bit 31
// set, the register index in bits 0-10 and the register class split over bits
// 28-30 and 11-12.
constexpr std::uint32_t end_token = 0x0000ffffu;
constexpr unsigned op_dp4 = 0x09, op_dcl = 0x1f, op_def = 0x51, op_defb = 0x2f, op_defi = 0x30;
constexpr unsigned op_if = 0x28, op_else = 0x2a, op_endif = 0x2b;
constexpr unsigned op_call = 0x19, op_callnz = 0x1a, op_loop = 0x1b, op_ret = 0x1c, op_endloop = 0x1d;
constexpr unsigned op_label = 0x1e, op_rep = 0x26, op_endrep = 0x27, op_ifc = 0x29;
constexpr unsigned op_texkill = 0x41, op_comment = 0xfffe, op_end = 0xffff;
constexpr unsigned temporary_class = 0, input_class = 1, constant_class = 2;
constexpr unsigned output_class = 6, color_output_class = 8, depth_output_class = 9;
constexpr unsigned boolean_class = 14;
// Class C pixel programs hold `if b#`/`else`/`endif` blocks nested at most this
// deep; the four captured programs have two sequential blocks (depth 1).
constexpr unsigned max_branch_depth = 1;
constexpr std::uint32_t parameter_bit = 0x80000000u, relative_bit = 0x00002000u;
constexpr std::uint32_t predicated_bit = 0x10000000u;
constexpr unsigned position_usage = 0, texcoord_usage = 5;

unsigned register_type(std::uint32_t token) noexcept {
    return ((token >> 28) & 7) | ((token >> 8) & 0x18);
}
unsigned register_index(std::uint32_t token) noexcept { return token & 0x7ff; }
unsigned declaration_usage(std::uint32_t token) noexcept { return token & 0x1f; }
unsigned declaration_usage_index(std::uint32_t token) noexcept { return (token >> 16) & 0xf; }
std::size_t instruction_length(std::uint32_t token) noexcept {
    return (token & 0xffff) == op_comment ? ((token >> 16) & 0x7fff) : ((token >> 24) & 15);
}
bool definition_opcode(unsigned opcode) noexcept {
    return opcode == op_def || opcode == op_defi || opcode == op_defb;
}
// Control-flow and predication opcodes other than the static `if b#`/`else`/
// `endif` that class C admits. Classes A and B are straight-line pixel
// programs, so any control flow refuses them; class C refuses these.
bool refused_flow_opcode(unsigned opcode) noexcept {
    switch (opcode) {
    case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: // call, callnz, loop, ret, endloop, label
    case 0x26: case 0x27: case 0x29:                                  // rep, endrep, ifc (if_comp)
    case 0x2c: case 0x2d: case 0x5e: case 0x60:                       // break, breakc, setp, breakp
        return true;
    default:
        return false;
    }
}
bool static_branch_opcode(unsigned opcode) noexcept {
    return opcode == op_if || opcode == op_else || opcode == op_endif;
}
// Vertex-side block structure: every table VS holds a `rep` light loop and an
// `if b#` block, so the position dots and the arithmetic insert must be proven
// to sit outside every block (the new dots would otherwise run conditionally
// or per iteration). Subroutines make "depth 0" meaningless and refuse.
bool block_open_opcode(unsigned opcode) noexcept {
    return opcode == op_rep || opcode == op_loop || opcode == op_if || opcode == op_ifc;
}
bool block_close_opcode(unsigned opcode) noexcept {
    return opcode == op_endrep || opcode == op_endloop || opcode == op_endif;
}
bool subroutine_opcode(unsigned opcode) noexcept {
    return opcode == op_call || opcode == op_callnz || opcode == op_ret || opcode == op_label;
}
bool damage_class(const MotionOutputProfile& row) noexcept {
    return row.transformation_class == MotionOutputClass::BoundedDamageBranches;
}
bool branching_class(const MotionOutputProfile& row) noexcept {
    return row.transformation_class == MotionOutputClass::RelocatedRegistersWithBranches;
}

std::uint64_t fingerprint(const std::uint32_t* words, std::size_t count) noexcept {
    std::uint64_t value = 14695981039346656037ull;
    for (std::size_t i = 0; i < count; ++i)
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value ^= (words[i] >> shift) & 255;
            value *= 1099511628211ull;
        }
    return value;
}

// Visit every instruction boundary in order, comments and preshader metadata
// included as opaque instructions. fn(at, token, length) returns false to
// refuse. Succeeds only if every instruction fits and END is the last word.
template <class Fn>
bool walk(const std::uint32_t* words, std::size_t count, Fn&& fn) noexcept {
    for (std::size_t at = 1; at < count;) {
        const auto token = words[at];
        const auto opcode = token & 0xffff;
        if (opcode == op_end) return token == end_token && at == count - 1;
        const std::size_t length = instruction_length(token);
        if (length > count - at - 1) return false;
        if (!fn(at, token, length)) return false;
        at += length + 1;
    }
    return false;
}
// Visit every register parameter of an executable instruction (destination,
// predicate and sources alike), skipping the address token that follows a
// relatively addressed operand. Not for def/dcl/comment, whose trailing words
// are literals. fn(token) returns false to refuse.
template <class Fn>
bool for_each_parameter(const std::uint32_t* words, std::size_t at, std::size_t length,
                        bool& relative, Fn&& fn) noexcept {
    for (std::size_t i = 1; i <= length; ++i) {
        const auto token = words[at + i];
        if (!(token & parameter_bit) || !fn(token)) return false;
        if (token & relative_bit) {
            relative = true;
            if (++i > length || !(words[at + i] & parameter_bit)) return false;
        }
    }
    return true;
}

// Registers the row reserves in each stage. The depth registers are reserved
// whenever the row names them, whether or not a caller asks for the depth
// output: a row is a static fact about the program and must be right either way.
bool vertex_reserved(const MotionOutputProfile& row, std::uint32_t token) noexcept {
    const auto type = register_type(token), index = register_index(token);
    if (type == output_class)
        return index == row.vertex_output_register ||
            (motion_output_vertex_exports_depth(row) && index == row.vertex_depth_output_register);
    if (type == constant_class)
        return index >= row.vertex_constant_base && index < row.vertex_constant_base + 4u;
    return false;
}
bool pixel_reserved(const MotionOutputProfile& row, std::uint32_t token) noexcept {
    const auto type = register_type(token), index = register_index(token);
    switch (type) {
    case temporary_class:
        return index >= row.pixel_temporary_base && index < row.pixel_temporary_base + 3u;
    case input_class:
        return index == row.pixel_input_register || (row.depth_output && index == row.pixel_depth_input_register);
    case constant_class:
        return index >= row.pixel_constant_base && index < row.pixel_constant_base + 5u;
    case color_output_class:
        return index == row.pixel_output_register || (row.depth_output && index == MaterialMotionAbi::depth_render_target);
    default: return false;
    }
}
// TEXCOORD indices the row reserves: the motion interpolator and, when the
// stage takes part in the depth export, the depth interpolator.
bool vertex_texcoord_reserved(const MotionOutputProfile& row, unsigned index) noexcept {
    return index == row.texcoord_index || (motion_output_vertex_exports_depth(row) && index == row.depth_texcoord_index);
}
bool pixel_texcoord_reserved(const MotionOutputProfile& row, unsigned index) noexcept {
    return index == row.texcoord_index || (row.depth_output && index == row.depth_texcoord_index);
}

// Revalidate the row's vertex-side facts against the actual words: contiguous
// def/dcl header ending exactly at the declaration insert and declaring o0 as
// POSITION0 (so the dots below are the clip position), the four position dots
// at the recorded offsets with the recorded lane masks, temporary and matrix
// rows, the arithmetic insert one past the last dot, and no original
// declaration, write or read of the chosen output register, TEXCOORD index or
// previous-row constants. Relative addressing is permitted only when the row
// says the draw-time light-loop bound applies. Block depth (`rep`/`loop`/`if`/
// `ifc` against `endrep`/`endloop`/`endif`) must be zero at every position dot,
// at the arithmetic insert and at END; `call`/`callnz`/`ret`/`label` refuse.
bool vertex_structure(const MotionOutputProfile& row, const std::uint32_t* words,
                      std::size_t count) noexcept {
    if (words[0] != row.vertex_version) return false;
    const std::size_t header_end = row.vertex_declaration_insert_dword;
    bool header_boundary = false, arithmetic_boundary = false, relative = false;
    bool position_declared = false;
    unsigned dots = 0, depth = 0;
    const bool framed = walk(words, count, [&](std::size_t at, std::uint32_t token, std::size_t length) {
        if (at == header_end) header_boundary = true;
        if (at == std::size_t(row.vertex_arithmetic_insert_dword)) arithmetic_boundary = depth == 0;
        const auto opcode = token & 0xffff;
        if (opcode == op_comment) return true;
        const bool declaration = opcode == op_dcl, definition = definition_opcode(opcode);
        if (at < header_end) {
            if (declaration) {
                if (length != 2) return false;
                const auto usage = words[at + 1], target = words[at + 2];
                if (!(usage & parameter_bit) || !(target & parameter_bit) || vertex_reserved(row, target))
                    return false;
                if (register_type(target) != output_class) return true;
                if (register_index(target) == 0 && declaration_usage(usage) == position_usage &&
                    declaration_usage_index(usage) == 0)
                    position_declared = true;
                return declaration_usage(usage) != texcoord_usage ||
                    !vertex_texcoord_reserved(row, declaration_usage_index(usage));
            }
            if (definition)
                return length >= 1 && (words[at + 1] & parameter_bit) && !vertex_reserved(row, words[at + 1]);
            return false;
        }
        if (declaration || definition || subroutine_opcode(opcode)) return false;
        // The four dots need not be adjacent. Whatever sits between the first
        // dot and the arithmetic insert must neither write the position
        // temporary (any mask: the added dots read the same value the
        // original ones did) nor open, close or split a block.
        const bool in_span = at > std::size_t(row.position_dp4_dwords[0]) &&
            at < std::size_t(row.vertex_arithmetic_insert_dword);
        if (in_span) {
            if (block_open_opcode(opcode) || block_close_opcode(opcode) || opcode == op_else) return false;
            if (length >= 1 && (words[at + 1] & parameter_bit) && register_type(words[at + 1]) == temporary_class &&
                register_index(words[at + 1]) == row.position_temporary && opcode != op_texkill)
                return false;
        }
        if (block_open_opcode(opcode)) ++depth;
        else if (block_close_opcode(opcode) || opcode == op_else) {
            if (depth == 0) return false;
            if (opcode != op_else) --depth;
        }
        for (unsigned lane = 0; lane < 4; ++lane)
            if (at == std::size_t(row.position_dp4_dwords[lane])) {
                if (depth != 0 || token != ((3u << 24) | op_dp4) ||
                    words[at + 1] != (0xe0000000u | (std::uint32_t(row.position_lane_masks[lane]) << 16)) ||
                    words[at + 2] != (0x80e40000u | row.position_temporary) ||
                    words[at + 3] != (0xa0e40000u | (row.matrix_register + lane)))
                    return false;
                ++dots;
            }
        return for_each_parameter(words, at, length, relative,
                                  [&](std::uint32_t parameter) { return !vertex_reserved(row, parameter); });
    });
    return framed && header_boundary && position_declared && arithmetic_boundary && dots == 4 && depth == 0 &&
        (!relative || row.light_loop_bound_required);
}

// Revalidate the row's pixel-side facts: literal definitions up to the
// definition insert, declarations up to the declaration insert, executable
// code with no predication, texkill, oDepth write or relative addressing up to
// the END at the append point, and no original reference to the chosen input,
// TEXCOORD index, temporaries, constants or color output. Classes A and B are
// straight-line programs; class C additionally admits `if b#`/`else`/`endif`
// on a boolean constant register, balanced, nested at most max_branch_depth,
// with one `else` per block and depth 0 at the append point (so the appended
// fragment is unconditional), and requires at least one such block. Every
// other control-flow opcode refuses. The walk is linear over all
// instructions, so the register checks cover the branch bodies too. Depth is
// the rasterized depth in every class, which the previous-depth output relies
// on.
bool pixel_structure(const MotionOutputProfile& row, const std::uint32_t* words,
                     std::size_t count) noexcept {
    if (words[0] != row.pixel_version || std::size_t(row.pixel_append_dword) != count - 1) return false;
    const std::size_t definition_end = row.pixel_definition_insert_dword;
    const std::size_t header_end = row.pixel_declaration_insert_dword;
    bool definition_boundary = false, header_boundary = false, relative = false;
    const bool damage = damage_class(row);
    if (damage && !detail::damage_pixel_structure(row, words, count)) return false;
    const bool branches_allowed = branching_class(row) || damage;
    unsigned depth = 0, blocks = 0;
    bool else_seen[max_branch_depth + 1] = {};
    const bool framed = walk(words, count, [&](std::size_t at, std::uint32_t token, std::size_t length) {
        if (at == definition_end) definition_boundary = true;
        if (at == header_end) header_boundary = true;
        const auto opcode = token & 0xffff;
        if (opcode == op_comment) return true;
        if (at < definition_end)
            return definition_opcode(opcode) && length >= 1 && (words[at + 1] & parameter_bit) &&
                !pixel_reserved(row, words[at + 1]);
        if (at < header_end) {
            if (opcode != op_dcl || length != 2) return false;
            const auto usage = words[at + 1], target = words[at + 2];
            if (!(usage & parameter_bit) || !(target & parameter_bit) || pixel_reserved(row, target))
                return false;
            return register_type(target) != input_class ||
                declaration_usage(usage) != texcoord_usage ||
                !pixel_texcoord_reserved(row, declaration_usage_index(usage));
        }
        if (opcode == op_dcl || definition_opcode(opcode) || (refused_flow_opcode(opcode) && !(damage && opcode == op_ifc)) ||
            opcode == op_texkill || (token & predicated_bit))
            return false;
        if (damage && opcode == op_ifc) {
            // The independent owned walk proved the operands and single MOV.
            if (depth) return false;
            else_seen[++depth] = false;
            return true;
        }
        if (static_branch_opcode(opcode)) {
            if (!branches_allowed || token != ((opcode == op_if ? 1u << 24 : 0u) | opcode)) return false;
            if (opcode == op_if) {
                // Exactly one direct boolean constant register as the condition.
                const auto condition = words[at + 1];
                if (depth == max_branch_depth || !(condition & parameter_bit) || (condition & relative_bit) ||
                    register_type(condition) != boolean_class)
                    return false;
                else_seen[++depth] = false;
                ++blocks;
                return true;
            }
            if (depth == 0) return false;
            if (opcode == op_else) {
                if (else_seen[depth]) return false;
                else_seen[depth] = true;
            } else {
                --depth;
            }
            return true;
        }
        return for_each_parameter(words, at, length, relative, [&](std::uint32_t parameter) {
            return !pixel_reserved(row, parameter) && register_type(parameter) != depth_output_class;
        });
    });
    return framed && definition_boundary && header_boundary && !relative && depth == 0 &&
        (blocks != 0) == branches_allowed;
}

// Move one operand of our authored motion program to the row's registers.
// Never reinterprets a relative operand or a literal as an ordinary register.
bool relocate_register(const MotionOutputProfile& row, std::uint32_t& token) noexcept {
    if (!(token & parameter_bit) || (token & relative_bit)) return false;
    const auto type = register_type(token), index = register_index(token);
    unsigned relocated;
    switch (type) {
    case temporary_class: if (index > 2) return false; relocated = row.pixel_temporary_base + index; break;
    case input_class: if (index != 0) return false; relocated = row.pixel_input_register; break;
    case constant_class: if (index > 4) return false; relocated = row.pixel_constant_base + index; break;
    case color_output_class: if (index != 0) return false; relocated = row.pixel_output_register; break;
    default: return false;
    }
    token = (token & ~std::uint32_t(0x7ff)) | relocated;
    return true;
}

// Relocate only our authored motion program, not arbitrary game instructions.
// Preserve its finite/W/depth checks, previous-jitter and invalid-history ABI.
bool motion_fragment(const MotionOutputProfile& row, Words& constants, Words& inputs, Words& body) {
    const auto& code = rigid_motion_pixel_program();
    if (code[0] != 0xffff0300u) return false;
    bool body_started = false;
    unsigned definitions = 0, declarations = 0, outputs = 0;
    for (std::size_t at = 1; at < std::size(code);) {
        const auto token = code[at], opcode = token & 0xffff;
        if (opcode == op_end)
            return token == end_token && at == std::size(code) - 1 &&
                definitions == 3 && declarations == 1 && outputs == 1;
        const std::size_t operands = instruction_length(token);
        if (operands > std::size(code) - at - 1) return false;
        if (opcode == op_comment) { at += operands + 1; continue; }
        if (opcode == op_def) {
            if (body_started || token != 0x05000051u || operands != 5 ||
                code[at + 1] != (0xa00f0002u + definitions)) return false;
            constants.push_back(token);
            auto destination = code[at + 1];
            if (!relocate_register(row, destination)) return false;
            constants.push_back(destination);
            constants.insert(constants.end(), code + at + 2, code + at + 6);
            ++definitions;
        } else if (opcode == op_dcl) {
            if (body_started || token != 0x0200001fu || operands != 2 ||
                code[at + 1] != 0x80000005u || code[at + 2] != 0x900f0000u)
                return false;
            inputs.insert(inputs.end(), {token, 0x80000005u | (std::uint32_t(row.texcoord_index) << 16),
                                         0x900f0000u | row.pixel_input_register});
            ++declarations;
        } else {
            body_started = true;
            // The fixed program consists only of these straight-line operations.
            unsigned expected;
            switch (opcode) {
            case 1: case 6: expected = 2; break; // MOV, RCP
            case 2: case 5: case 8: case 9: case 11: expected = 3; break;
            case 4: case 88: expected = 4; break; // MAD, CMP
            default: return false;
            }
            if (token != (expected << 24 | opcode) || operands != expected) return false;
            body.push_back(token);
            for (std::size_t i = 1; i <= operands; ++i) {
                auto operand = code[at + i];
                if (i == 1 && register_type(operand) == color_output_class) ++outputs;
                if (!relocate_register(row, operand)) return false;
                body.push_back(operand);
            }
        }
        at += operands + 1;
    }
    return false;
}

// Move one operand of the authored depth fragment: its single input v0 to the
// row's depth input, its temporary r0 to the first motion temporary (dead
// once the motion fragment has written oC1, which precedes this fragment),
// and oC0 to the depth target. No constants exist in that program.
bool relocate_depth_register(const MotionOutputProfile& row, std::uint32_t& token) noexcept {
    if (!(token & parameter_bit) || (token & relative_bit)) return false;
    const auto type = register_type(token), index = register_index(token);
    unsigned relocated;
    switch (type) {
    case temporary_class: if (index != 0) return false; relocated = row.pixel_temporary_base; break;
    case input_class: if (index != 0) return false; relocated = row.pixel_depth_input_register; break;
    case color_output_class: if (index != 0) return false; relocated = MaterialMotionAbi::depth_render_target; break;
    default: return false;
    }
    token = (token & ~std::uint32_t(0x7ff)) | relocated;
    return true;
}
// Relocate only our authored current-depth program: one TEXCOORD1 input
// declaration, no definitions, a straight-line body (rcp, mul) and one color
// output write. Its shape is fixed at compile time of the fragment.
bool depth_fragment(const MotionOutputProfile& row, Words& inputs, Words& body) {
    const auto& code = current_depth_pixel_program();
    if (code[0] != 0xffff0300u) return false;
    bool body_started = false;
    unsigned declarations = 0, outputs = 0;
    for (std::size_t at = 1; at < std::size(code);) {
        const auto token = code[at], opcode = token & 0xffff;
        if (opcode == op_end)
            return token == end_token && at == std::size(code) - 1 && declarations == 1 && outputs == 1;
        const std::size_t operands = instruction_length(token);
        if (operands > std::size(code) - at - 1) return false;
        if (opcode == op_comment) { at += operands + 1; continue; }
        if (opcode == op_dcl) {
            // dcl_texcoord1 v0.xy: the usage index and the register move to the row's choices.
            if (body_started || token != 0x0200001fu || operands != 2 ||
                code[at + 1] != 0x80010005u || code[at + 2] != 0x90030000u)
                return false;
            inputs.insert(inputs.end(), {token, 0x80000005u | (std::uint32_t(row.depth_texcoord_index) << 16),
                                         0x90030000u | row.pixel_depth_input_register});
            ++declarations;
        } else if (definition_opcode(opcode)) {
            return false;
        } else {
            body_started = true;
            unsigned expected;
            switch (opcode) {
            case 6: expected = 2; break;  // RCP
            case 5: expected = 3; break;  // MUL
            default: return false;
            }
            if (token != (expected << 24 | opcode) || operands != expected) return false;
            body.push_back(token);
            for (std::size_t i = 1; i <= operands; ++i) {
                auto operand = code[at + i];
                if (i == 1 && register_type(operand) == color_output_class) ++outputs;
                if (!relocate_depth_register(row, operand)) return false;
                body.push_back(operand);
            }
        }
        at += operands + 1;
    }
    return false;
}

bool supported_class(const MotionOutputProfile& row) noexcept {
    return row.transformation_class == MotionOutputClass::ReferenceRegisters ||
        row.transformation_class == MotionOutputClass::RelocatedRegisters ||
        branching_class(row) || damage_class(row);
}
// Compile-time sorted indices into the row table: by (vertex, pixel)
// fingerprint for the pair lookup and by each stage's fingerprint (stable, so
// equal fingerprints keep table order) for the per-program lookups. Every
// lookup is then a binary search over the row count with no allocation, which
// the live route's per-draw pair gate and its registration path rely on.
using RowIndex = std::uint16_t;
static_assert(motion_output_profile_count < 65535, "row index type");
struct RowOrder { RowIndex at[motion_output_profile_count]; };
template <class Key>
constexpr RowOrder sorted_rows(Key key) noexcept {
    RowOrder order{};
    for (std::size_t i = 0; i < motion_output_profile_count; ++i) order.at[i] = RowIndex(i);
    for (std::size_t i = 1; i < motion_output_profile_count; ++i) { // Insertion sort, stable.
        const RowIndex moving = order.at[i];
        std::size_t j = i;
        while (j > 0 && key(motion_output_profiles[moving]) < key(motion_output_profiles[order.at[j - 1]])) {
            order.at[j] = order.at[j - 1];
            --j;
        }
        order.at[j] = moving;
    }
    return order;
}
struct PairKey {
    std::uint64_t vertex, pixel;
    constexpr bool operator<(const PairKey& other) const noexcept {
        return vertex < other.vertex || (vertex == other.vertex && pixel < other.pixel);
    }
};
constexpr RowOrder rows_by_pair = sorted_rows([](const MotionOutputProfile& row) {
    return PairKey{row.vertex_fingerprint, row.pixel_fingerprint};
});
constexpr RowOrder rows_by_vertex = sorted_rows([](const MotionOutputProfile& row) { return row.vertex_fingerprint; });
constexpr RowOrder rows_by_pixel = sorted_rows([](const MotionOutputProfile& row) { return row.pixel_fingerprint; });

// First position in `order` whose key is not less than `wanted`.
template <class Key, class Value>
std::size_t lower_bound(const RowOrder& order, Key key, const Value& wanted) noexcept {
    std::size_t lo = 0, hi = motion_output_profile_count;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (key(motion_output_profiles[order.at[mid]]) < wanted) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
constexpr std::uint64_t vertex_key(const MotionOutputProfile& row) noexcept { return row.vertex_fingerprint; }
constexpr std::uint64_t pixel_key(const MotionOutputProfile& row) noexcept { return row.pixel_fingerprint; }
constexpr PairKey pair_key(const MotionOutputProfile& row) noexcept {
    return PairKey{row.vertex_fingerprint, row.pixel_fingerprint};
}

// First row of a supported class for this program, in table order among the
// rows sharing it. A program shared with a row of an unsupported class (none
// today) must still transform for its supported pairs.
const MotionOutputProfile* vertex_row(std::uint64_t hash, std::size_t count) noexcept {
    if (!hash) return nullptr;
    for (std::size_t i = lower_bound(rows_by_vertex, vertex_key, hash); i < motion_output_profile_count; ++i) {
        const auto& row = motion_output_profiles[rows_by_vertex.at[i]];
        if (row.vertex_fingerprint != hash) break;
        if (row.vertex_dword_count == count && supported_class(row)) return &row;
    }
    return nullptr;
}
const MotionOutputProfile* pixel_row(std::uint64_t hash, std::size_t count) noexcept {
    if (!hash) return nullptr;
    for (std::size_t i = lower_bound(rows_by_pixel, pixel_key, hash); i < motion_output_profile_count; ++i) {
        const auto& row = motion_output_profiles[rows_by_pixel.at[i]];
        if (row.pixel_fingerprint != hash) break;
        if (row.pixel_dword_count == count && supported_class(row)) return &row;
    }
    return nullptr;
}
const MotionOutputProfile* pair_row(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    if (!vertex || !pixel) return nullptr;
    const PairKey wanted{vertex, pixel};
    const std::size_t i = lower_bound(rows_by_pair, pair_key, wanted);
    if (i == motion_output_profile_count) return nullptr;
    const auto& row = motion_output_profiles[rows_by_pair.at[i]];
    return row.vertex_fingerprint == vertex && row.pixel_fingerprint == pixel ? &row : nullptr;
}
// Each sorted index must be a permutation of the rows in key order, and the
// per-stage orders stable (equal fingerprints keep table order, which is what
// "first row of a supported class" means); proven at compile time so a
// regenerated table cannot silently break the searches.
template <class Key>
constexpr bool row_order_valid(const RowOrder& order, Key key, bool strict) noexcept {
    bool seen[motion_output_profile_count] = {};
    for (std::size_t i = 0; i < motion_output_profile_count; ++i) {
        const auto at = order.at[i];
        if (at >= motion_output_profile_count || seen[at]) return false;
        seen[at] = true;
        if (i == 0) continue;
        const auto previous = order.at[i - 1];
        const auto& before = motion_output_profiles[previous];
        const auto& here = motion_output_profiles[at];
        if (key(here) < key(before)) return false;
        const bool equal = !(key(before) < key(here));
        if (equal && (strict || previous > at)) return false; // Strict: no duplicate pairs; stable otherwise.
    }
    return true;
}
constexpr bool row_orders_valid() noexcept {
    return row_order_valid(rows_by_pair, pair_key, true) &&
        row_order_valid(rows_by_vertex, vertex_key, false) &&
        row_order_valid(rows_by_pixel, pixel_key, false);
}
static_assert(row_orders_valid(), "sorted row indices must be permutations: strict pair order, stable per-stage order");

// The route uploads the public ABI ranges for every routed draw, so every row
// must reserve exactly those registers.
constexpr bool rows_use_public_abi() noexcept {
    for (const auto& row : motion_output_profiles)
        if (row.vertex_constant_base != MaterialMotionAbi::previous_vertex_constant ||
            row.pixel_constant_base != MaterialMotionAbi::pixel_coordinates_constant ||
            row.pixel_constant_base + 1 != MaterialMotionAbi::pixel_mode_constant ||
            row.pixel_output_register != MaterialMotionAbi::motion_render_target)
            return false;
    return true;
}
static_assert(rows_use_public_abi(), "every profile row must use the public MaterialMotionAbi registers");
} // namespace

std::uint64_t material_motion_fingerprint(const std::uint32_t* words, std::size_t count) noexcept {
    return words ? fingerprint(words, count) : 0;
}
const MotionOutputProfile* material_motion_profile(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    return pair_row(vertex, pixel);
}
bool material_motion_pair_reviewed(std::uint64_t vertex, std::uint64_t pixel) noexcept {
    const auto* row = pair_row(vertex, pixel);
    return row && supported_class(*row);
}
const MotionOutputProfile* material_motion_vertex_row(std::uint64_t vertex, std::size_t words) noexcept {
    return vertex_row(vertex, words);
}
const MotionOutputProfile* material_motion_pixel_row(std::uint64_t pixel, std::size_t words) noexcept {
    return pixel_row(pixel, words);
}

bool material_motion_vertex_exports_depth(const MotionOutputProfile& row, bool current_depth) noexcept {
    return current_depth && motion_output_vertex_exports_depth(row);
}
bool material_motion_pixel_writes_depth(const MotionOutputProfile& row, bool current_depth) noexcept {
    return current_depth && row.depth_output;
}

MaterialMotionResult material_motion_vertex_variant_for(const MotionOutputProfile& row,
    const std::uint32_t* vertex, std::size_t vertex_words, std::vector<std::uint32_t>& output,
    bool current_depth) noexcept {
    if (!vertex || vertex_words < 2) return MaterialMotionResult::InvalidInput;
    if (!supported_class(row) || vertex_words != row.vertex_dword_count ||
        fingerprint(vertex, vertex_words) != row.vertex_fingerprint)
        return MaterialMotionResult::UnsupportedShader;
    if (!motion_output_profile_valid(row) || !vertex_structure(row, vertex, vertex_words))
        return MaterialMotionResult::ProfileMismatch;
    const std::size_t declaration_at = row.vertex_declaration_insert_dword;
    const std::size_t arithmetic_at = row.vertex_arithmetic_insert_dword;
    const bool depth = material_motion_vertex_exports_depth(row, current_depth);
    try {
        Words variant;
        variant.reserve(vertex_words + 19 + (depth ? 11 : 0));
        variant.insert(variant.end(), vertex, vertex + declaration_at);
        // New output o<n> is TEXCOORD<i> without centroid or precision modifiers;
        // existing declarations and math are intact.
        variant.insert(variant.end(), {0x0200001fu,
            0x80000005u | (std::uint32_t(row.texcoord_index) << 16),
            0xe00f0000u | row.vertex_output_register});
        if (depth)
            variant.insert(variant.end(), {0x0200001fu,
                0x80000005u | (std::uint32_t(row.depth_texcoord_index) << 16),
                0xe00f0000u | row.vertex_depth_output_register});
        variant.insert(variant.end(), vertex + declaration_at, vertex + arithmetic_at);
        // Previous clip position: the same position temporary against the
        // previous rows, one lane per dot exactly like the original quad.
        for (unsigned lane = 0; lane < 4; ++lane)
            variant.insert(variant.end(), {(3u << 24) | op_dp4,
                0xe0000000u | (std::uint32_t(row.position_lane_masks[lane]) << 16) | row.vertex_output_register,
                0x80e40000u | row.position_temporary,
                0xa0e40000u | (row.vertex_constant_base + lane)});
        // Current clip z and w: the same dots the original issues for o0.z and
        // o0.w (matrix rows 2 and 3), written to .xz and .yw so every lane of
        // the interpolator is defined without a literal zero: (z, w, z, w).
        if (depth)
            for (unsigned lane = 0; lane < 2; ++lane)
                variant.insert(variant.end(), {(3u << 24) | op_dp4,
                    0xe0000000u | ((lane ? 0xau : 0x5u) << 16) | row.vertex_depth_output_register,
                    0x80e40000u | row.position_temporary,
                    0xa0e40000u | (row.matrix_register + 2 + lane)});
        variant.insert(variant.end(), vertex + arithmetic_at, vertex + vertex_words);
        output.swap(variant);
        return MaterialMotionResult::Applied;
    } catch (...) { return MaterialMotionResult::AllocationFailure; }
}

MaterialMotionResult material_motion_pixel_variant_for(const MotionOutputProfile& row,
    const std::uint32_t* pixel, std::size_t pixel_words, std::vector<std::uint32_t>& output,
    bool current_depth) noexcept {
    if (!pixel || pixel_words < 2) return MaterialMotionResult::InvalidInput;
    if (!supported_class(row) || pixel_words != row.pixel_dword_count ||
        fingerprint(pixel, pixel_words) != row.pixel_fingerprint)
        return MaterialMotionResult::UnsupportedShader;
    if (!motion_output_profile_valid(row) || !pixel_structure(row, pixel, pixel_words))
        return MaterialMotionResult::ProfileMismatch;
    const std::size_t definition_at = row.pixel_definition_insert_dword;
    const std::size_t declaration_at = row.pixel_declaration_insert_dword;
    const std::size_t append_at = row.pixel_append_dword;
    const bool depth = material_motion_pixel_writes_depth(row, current_depth);
    try {
        Words constants, inputs, body;
        if (!motion_fragment(row, constants, inputs, body)) return MaterialMotionResult::ProfileMismatch;
        // The depth fragment follows the motion fragment: its declaration after
        // the motion input, its two instructions after the motion body.
        if (depth && !depth_fragment(row, inputs, body)) return MaterialMotionResult::ProfileMismatch;
        Words variant;
        variant.reserve(pixel_words + constants.size() + inputs.size() + body.size());
        variant.insert(variant.end(), pixel, pixel + definition_at);
        variant.insert(variant.end(), constants.begin(), constants.end());
        variant.insert(variant.end(), pixel + definition_at, pixel + declaration_at);
        variant.insert(variant.end(), inputs.begin(), inputs.end());
        variant.insert(variant.end(), pixel + declaration_at, pixel + append_at);
        variant.insert(variant.end(), body.begin(), body.end());
        variant.push_back(end_token);
        output.swap(variant);
        return MaterialMotionResult::Applied;
    } catch (...) { return MaterialMotionResult::AllocationFailure; }
}

MaterialMotionResult material_motion_vertex_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, std::vector<std::uint32_t>& output, bool current_depth) noexcept {
    if (!vertex || vertex_words < 2) return MaterialMotionResult::InvalidInput;
    // Rows sharing a vertex program agree on its side of the splice
    // (static_assert in motion_output_profiles.h), so the first row serves.
    const auto* row = vertex_row(fingerprint(vertex, vertex_words), vertex_words);
    if (!row) return MaterialMotionResult::UnsupportedShader;
    return material_motion_vertex_variant_for(*row, vertex, vertex_words, output, current_depth);
}

MaterialMotionResult material_motion_pixel_variant(const std::uint32_t* pixel,
    std::size_t pixel_words, std::vector<std::uint32_t>& output, bool current_depth) noexcept {
    if (!pixel || pixel_words < 2) return MaterialMotionResult::InvalidInput;
    const auto* row = pixel_row(fingerprint(pixel, pixel_words), pixel_words);
    if (!row) return MaterialMotionResult::UnsupportedShader;
    return material_motion_pixel_variant_for(*row, pixel, pixel_words, output, current_depth);
}

MaterialMotionResult material_motion_variant(const std::uint32_t* vertex,
    std::size_t vertex_words, const std::uint32_t* pixel, std::size_t pixel_words,
    MaterialMotionVariant& output, bool current_depth) noexcept {
    if (!vertex || !pixel || vertex_words < 2 || pixel_words < 2)
        return MaterialMotionResult::InvalidInput;
    // Qualify both fingerprints against one row before either stage transforms,
    // so a wrong pair reports UnsupportedShader whichever stage is wrong and
    // nothing is published.
    const auto* row = material_motion_profile(fingerprint(vertex, vertex_words),
                                              fingerprint(pixel, pixel_words));
    if (!row || vertex_words != row->vertex_dword_count || pixel_words != row->pixel_dword_count)
        return MaterialMotionResult::UnsupportedShader;
    MaterialMotionVariant variant;
    const auto vertex_result = material_motion_vertex_variant_for(*row, vertex, vertex_words, variant.vertex, current_depth);
    if (vertex_result != MaterialMotionResult::Applied) return vertex_result;
    const auto pixel_result = material_motion_pixel_variant_for(*row, pixel, pixel_words, variant.pixel, current_depth);
    if (pixel_result != MaterialMotionResult::Applied) return pixel_result;
    output.vertex.swap(variant.vertex);
    output.pixel.swap(variant.pixel);
    return MaterialMotionResult::Applied;
}
} // namespace x3m::renderer
