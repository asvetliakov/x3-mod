// Host-only probe of the shared fill-site analysis (linear_material_fill_sum).
// It builds small synthetic ps_3_0 word streams; no game bytes are involved.
// The transformer uses the same function, so a refusal here is the refusal the
// production path takes (docs/architecture/fill-light.md, "Implementation").
#include "../../src/renderer/linear_material.h"
#include <cstdint>
#include <iostream>
#include <vector>
using namespace x3m::renderer;
using Words = std::vector<std::uint32_t>;
namespace {
constexpr std::uint32_t pp = 0x200000u, identity = 0xe4u;
constexpr unsigned temp = 0, color_output = 8;
std::uint32_t reg(unsigned type, unsigned number) {
    return 0x80000000u | ((type & 7u) << 28) | ((type & 24u) << 8) | number;
}
std::uint32_t dst(unsigned type, unsigned number, unsigned lanes = 7) {
    return reg(type, number) | (lanes << 16);
}
std::uint32_t src(unsigned type, unsigned number) {
    return reg(type, number) | (identity << 16);
}
void emit(Words& out, unsigned opcode, std::initializer_list<std::uint32_t> operands) {
    out.push_back((static_cast<std::uint32_t>(operands.size()) << 24) | opcode);
    out.insert(out.end(), operands.begin(), operands.end());
}
// mul/mad forms of "lobe sum times albedo"; albedo is r3 throughout.
void albedo_multiply(Words& out, unsigned sum) {
    emit(out, 4, {dst(temp, 1) | pp, src(temp, sum), src(temp, 3), src(temp, 0)});
}
Words program(std::initializer_list<unsigned> sums, bool output_form = false) {
    Words words{0xffff0300u};
    emit(words, 2, {dst(temp, 2) | pp, src(temp, 4), src(temp, 5)});
    for (unsigned sum : sums) {
        if (output_form)
            emit(words, 5, {dst(color_output, 0) | pp, src(temp, sum), src(temp, 3)});
        else
            albedo_multiply(words, sum);
    }
    words.push_back(0xffffu);
    return words;
}
Words composite_program(unsigned composite_lanes = 7) {
    Words words{0xffff0300u};
    emit(words, 2, {dst(temp, 2) | pp, src(temp, 4), src(temp, 5)});
    emit(words, 1, {dst(temp, 6, composite_lanes) | pp, src(temp, 3)});
    albedo_multiply(words, 2);
    // Replace the direct albedo operand with the one-step composite r6.
    words[words.size() - 2] = src(temp, 6);
    words.push_back(0xffffu);
    return words;
}
unsigned failures = 0;
void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL " << what << '\n';
        ++failures;
    }
}
} // namespace
int main() {
    unsigned sum = 99, at = 99;
    const auto one = program({2});
    check(linear_material_fill_sum(one.data(), one.size(), 3, sum, at) && sum == 2 && at == 5, "one lobe sum resolves");
    // Two candidate destinations: ambiguous, so the fill is refused.
    unsigned ambiguous_sum = 99, ambiguous_at = 99;
    const auto two = program({2, 4});
    check(!linear_material_fill_sum(two.data(), two.size(), 3, ambiguous_sum, ambiguous_at),
          "ambiguous lobe sum refuses");
    // The same shape twice into oC0 is equally ambiguous.
    const auto two_outputs = program({2, 4}, true);
    check(!linear_material_fill_sum(two_outputs.data(), two_outputs.size(), 3, ambiguous_sum, ambiguous_at),
          "ambiguous output lobe sum refuses");
    const auto one_output = program({2}, true);
    check(linear_material_fill_sum(one_output.data(), one_output.size(), 3, sum, at) && sum == 2,
          "output form resolves");
    const auto composite = composite_program();
    check(linear_material_fill_sum(composite.data(), composite.size(), 3, sum, at) && sum == 2,
          "one-step albedo composite resolves");
    // No albedo multiply at all, and a program that is not a program.
    const auto none = program({});
    check(!linear_material_fill_sum(none.data(), none.size(), 3, sum, at), "absent lobe sum refuses");
    check(!linear_material_fill_sum(one.data(), 1, 3, sum, at), "truncated input refuses");
    check(!linear_material_fill_sum(nullptr, 8, 3, sum, at), "null input refuses");
    // A multiplier that is neither the albedo nor a one-step composite of it.
    const auto foreign = program({2});
    check(!linear_material_fill_sum(foreign.data(), foreign.size(), 6, sum, at), "foreign multiplier refuses");
    auto aliased = one;
    aliased[8] = aliased[7];
    check(!linear_material_fill_sum(aliased.data(), aliased.size(), 3, sum, at), "aliased sum and albedo refuse");
    auto swizzled = one;
    swizzled[7] &= ~(0xffu << 16);
    check(!linear_material_fill_sum(swizzled.data(), swizzled.size(), 3, sum, at), "swizzled sum read refuses");
    auto wrong_destination = one;
    wrong_destination[6] = dst(temp, 0) | pp;
    check(!linear_material_fill_sum(wrong_destination.data(), wrong_destination.size(), 3, sum, at),
          "unreviewed destination refuses");
    const auto partial_composite = composite_program(1);
    check(!linear_material_fill_sum(partial_composite.data(), partial_composite.size(), 3, sum, at),
          "partial albedo composite refuses");
    auto malformed = one;
    malformed[5] = (15u << 24) | 4u;
    check(!linear_material_fill_sum(malformed.data(), malformed.size(), 3, sum, at),
          "malformed instruction length refuses");
    std::cout << "{\"failures\":" << failures << "}\n";
    return failures ? 1 : 0;
}
