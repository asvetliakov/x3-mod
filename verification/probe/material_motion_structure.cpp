// Host-only structural oracle. Raw game programs are supplied locally, never embedded.
#include "../../src/renderer/material_motion.h"
#include "../../src/renderer/rigid_motion_pixel_program.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Words = std::vector<std::uint32_t>;
using namespace x3m::renderer;
static unsigned checks = 0;
static void require(bool value) { if (!value) throw std::runtime_error("structural mismatch"); }
static void passed(const char* name) { ++checks; std::cout << "CHECK " << name << '\n'; }
static Words read(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream));
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), {});
    require(bytes.size() % 4 == 0);
    Words result;
    for (std::size_t i = 0; i < bytes.size(); i += 4)
        result.push_back(std::uint32_t(bytes[i]) | (std::uint32_t(bytes[i+1]) << 8) |
            (std::uint32_t(bytes[i+2]) << 16) | (std::uint32_t(bytes[i+3]) << 24));
    return result;
}
static Words slice(const Words& w, std::size_t begin, std::size_t end) {
    require(begin <= end && end <= w.size());
    return Words(w.begin() + begin, w.begin() + end);
}
static unsigned kind(std::uint32_t word) { return ((word & 0x70000000u) >> 28) | ((word & 0x1800u) >> 8); }
static void operand(std::uint32_t original, std::uint32_t relocated) {
    require((original & 0x80002000u) == 0x80000000u);
    // No bit outside the register index may change: class, write mask, swizzle,
    // source modifier, destination modifier and shift are compared bitwise.
    require((original & ~0x7ffu) == (relocated & ~0x7ffu));
    unsigned expected = 0;
    switch (kind(original)) {
    case 0: require((original & 0x7ffu) <= 2); expected = (original & 0x7ffu) + 5; break;
    case 1: require((original & 0x7ffu) == 0); expected = 5; break;
    case 2: require((original & 0x7ffu) <= 4); expected = (original & 0x7ffu) + 216; break;
    case 8: require((original & 0x7ffu) == 0); expected = 1; break;
    default: require(false);
    }
    require((relocated & 0x7ffu) == expected);
}
static void authored(const Words& defs, const Words& declaration, const Words& body) {
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
            require(defs[d] == instruction); operand(source[i+1], defs[d+1]);
            for (unsigned literal = 2; literal != 6; ++literal) require(source[i+literal] == defs[d+literal]);
            d += 6; i += 6; continue;
        }
        if (op == 31) {
            require(i + 3 <= extent - 1 && instruction == 0x0200001fu);
            require(source[i+1] == 0x80000005u && source[i+2] == 0x900f0000u);
            require(declaration == Words({instruction, 0x80040005u, 0x900f0005u}));
            ++declarations; i += 3; continue;
        }
        // Decode known operation arity independently of the instruction length.
        const unsigned arity = op == 1 || op == 6 ? 2 :
            op == 2 || op == 5 || op == 8 || op == 9 || op == 11 ? 3 :
            op == 4 || op == 88 ? 4 : 0;
        require(arity != 0 && instruction == ((arity << 24) | op));
        require(i + arity + 1 <= extent - 1 && b + arity + 1 <= body.size());
        require(body[b] == instruction);
        for (unsigned k = 1; k <= arity; ++k) operand(source[i+k], body[b+k]);
        if (kind(source[i+1]) == 8) ++output_count;
        i += arity + 1; b += arity + 1;
    }
    require(d == 18 && d == defs.size() && b == 111 && b == body.size() && declarations == 1 && output_count == 1);
}
static bool same(const MaterialMotionVariant& a, const MaterialMotionVariant& b) {
    return a.vertex == b.vertex && a.pixel == b.pixel;
}
int main(int argc, char** argv) {
    try {
        require(argc == 3);
        const Words vs = read(argv[1]), ps = read(argv[2]);
        require(vs.size() == 526 && ps.size() == 1260);
        MaterialMotionVariant output;
        require(material_motion_variant(vs.data(), vs.size(), ps.data(), ps.size(), output) == MaterialMotionResult::Applied);
        require(output.vertex.size() == 545 && output.pixel.size() == 1392);
        passed("reviewed_pair_applied_545_1392");
        auto stripped_vs = output.vertex, stripped_ps = output.pixel;
        stripped_vs.erase(stripped_vs.begin()+469, stripped_vs.begin()+485);
        stripped_vs.erase(stripped_vs.begin()+335, stripped_vs.begin()+338);
        stripped_ps.erase(stripped_ps.begin()+1280, stripped_ps.begin()+1391);
        stripped_ps.erase(stripped_ps.begin()+1092, stripped_ps.begin()+1095);
        stripped_ps.erase(stripped_ps.begin()+1047, stripped_ps.begin()+1065);
        require(stripped_vs == vs && stripped_ps == ps);
        passed("all_original_words_preserved");
        require(slice(output.vertex,335,338) == Words({0x0200001fu,0x80040005u,0xe00f0006u}));
        for (unsigned lane = 0; lane != 4; ++lane) {
            const auto at = 469 + 4*lane;
            require(output.vertex[at] == vs[450+4*lane]);
            require(output.vertex[at+1] == (vs[451+4*lane] | 6u));
            require(output.vertex[at+2] == vs[452+4*lane]);
            require(output.vertex[at+3] == 0xa0e400fcu + lane);
        }
        require(MaterialMotionAbi::previous_vertex_constant == 252 && MaterialMotionAbi::pixel_coordinates_constant == 216 &&
            MaterialMotionAbi::pixel_mode_constant == 217 && MaterialMotionAbi::motion_render_target == 1);
        passed("vertex_previous_clip_and_public_abi");
        authored(slice(output.pixel,1047,1065), slice(output.pixel,1092,1095), slice(output.pixel,1280,1391));
        passed("authored_fragment_register_bits_literals_and_opcodes");
        const MaterialMotionVariant sentinel{{0x12345678u,0xdeadbeefu},{0x87654321u}};
        auto refuse = [&](const std::uint32_t* v, std::size_t vn, const std::uint32_t* p, std::size_t pn, MaterialMotionResult expected) {
            auto target = sentinel;
            require(material_motion_variant(v,vn,p,pn,target) == expected && same(target,sentinel));
        };
        for (unsigned stage = 0; stage != 2; ++stage) {
            refuse(stage ? vs.data() : nullptr, vs.size(), stage ? nullptr : ps.data(), ps.size(), MaterialMotionResult::InvalidInput);
            for (std::size_t n : {0u,1u}) refuse(vs.data(),stage ? vs.size() : n,ps.data(),stage ? n : ps.size(),MaterialMotionResult::InvalidInput);
            refuse(vs.data(),vs.size()-(stage == 0),ps.data(),ps.size()-(stage == 1),MaterialMotionResult::UnsupportedShader);
        }
        auto extra_vs = vs, extra_ps = ps; extra_vs.push_back(0xffffu); extra_ps.push_back(0xffffu);
        refuse(extra_vs.data(),extra_vs.size(),ps.data(),ps.size(),MaterialMotionResult::UnsupportedShader);
        refuse(vs.data(),vs.size(),extra_ps.data(),extra_ps.size(),MaterialMotionResult::UnsupportedShader);
        refuse(ps.data(),ps.size(),vs.data(),vs.size(),MaterialMotionResult::UnsupportedShader);
        passed("invalid_wrong_pair_truncated_appended_atomic_refusal");
        std::size_t mutations = 0;
        for (unsigned stage = 0; stage != 2; ++stage) {
            Words changed = stage ? ps : vs;
            for (std::size_t i = 0; i < changed.size(); ++i) for (unsigned bit = 0; bit != 32; ++bit) {
                changed[i] ^= std::uint32_t(1) << bit;
                refuse(stage ? vs.data() : changed.data(),vs.size(),stage ? changed.data() : ps.data(),ps.size(),MaterialMotionResult::UnsupportedShader);
                changed[i] ^= std::uint32_t(1) << bit; ++mutations;
            }
        }
        require(mutations == 57152); passed("every_input_dword_every_bit_57152_atomic_refusals");
        // All single-vector directions plus simultaneous ordinary/cross aliases.
        for (unsigned mode = 0; mode != 6; ++mode) {
            auto target = sentinel;
            const std::uint32_t *v = vs.data(), *p = ps.data();
            if (mode == 0 || mode == 4) { target.vertex=vs; v=target.vertex.data(); }
            if (mode == 1 || mode == 5) { target.pixel=vs; v=target.pixel.data(); }
            if (mode == 2 || mode == 5) { target.vertex=ps; p=target.vertex.data(); }
            if (mode == 3 || mode == 4) { target.pixel=ps; p=target.pixel.data(); }
            require(material_motion_variant(v,vs.size(),p,ps.size(),target) == MaterialMotionResult::Applied && same(target,output));
        }
        passed("six_input_output_alias_layouts_applied");
        auto bad_alias = MaterialMotionVariant{ps,vs}; const auto saved = bad_alias;
        require(material_motion_variant(bad_alias.pixel.data(),vs.size()-1,bad_alias.vertex.data(),ps.size(),bad_alias) == MaterialMotionResult::UnsupportedShader && same(bad_alias,saved));
        passed("cross_alias_refusal_atomic");
        std::cout << "RESULT PASS checks=" << checks << " mutations=" << mutations << " aliases=6 vertex_words=545 pixel_words=1392\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "RESULT FAIL " << e.what() << '\n'; return 1; }
}
