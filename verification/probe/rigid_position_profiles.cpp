// Local shader bytes are inputs only; this fixture emits derived facts, no code.
#include "../../src/renderer/rigid_position.h"
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
using namespace x3m::renderer;
// Older four-field aggregate initializers must never silently attest new math.
constexpr RigidPositionProfile legacy_profile{1, 2, 3, false};
static_assert(legacy_profile.shader_version == 0);
static_assert(legacy_profile.position_write_order == PositionWriteOrder::Unknown);
static_assert(legacy_profile.homogeneous_constructor == HomogeneousConstructor::Unknown);
static bool rejects(const std::uint32_t* code, std::size_t size) {
    return !find_rigid_position(code, size) && !find_pixel_coverage(code, size) &&
           classify_vertex_position(code, size) == VertexPositionPath::Unknown;
}
static int verify(const std::string& line, unsigned& mutations) {
    std::string path, hash;
    unsigned count, category, named, coverage, version, order, constructor;
    int reg;
    std::istringstream fields(line);
    if (!(fields >> std::quoted(path) >> count >> category >> reg >> named >> coverage >> hash >> version >> order >>
          constructor))
        return 3;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != std::streamoff(count * 4) || count < 2 || count > 1883) return 4;
    std::vector<std::uint32_t> code(count);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), count * 4);
    if (!file) return 5;
    const auto* rigid = find_rigid_position(code.data(), code.size());
    const auto* pixel = find_pixel_coverage(code.data(), code.size());
    if (classify_vertex_position(code.data(), code.size()) != static_cast<VertexPositionPath>(category)) return 6;
    if ((reg >= 0) != (rigid != nullptr) || bool(coverage) != (pixel != nullptr)) return 7;
    const auto expected_hash = std::stoull(hash, nullptr, 16);
    if (rigid &&
        (rigid->hash != expected_hash || rigid->word_count != count || rigid->matrix_register != reg ||
         rigid->named_world_view_projection != bool(named) || rigid->shader_version != version ||
         rigid->shader_version != code[0] || rigid->position_write_order != static_cast<PositionWriteOrder>(order) ||
         rigid->homogeneous_constructor != static_cast<HomogeneousConstructor>(constructor)))
        return 8;
    if (pixel && (pixel->hash != expected_hash || pixel->word_count != count)) return 9;
    if (!rejects(nullptr, count) || !rejects(code.data(), 0) || !rejects(code.data(), static_cast<std::size_t>(-1)) ||
        !rejects(code.data(), kMaxReviewedPixelShaderWords + 1) || !rejects(code.data(), count - 1))
        return 10;
    code.push_back(0);
    if (!rejects(code.data(), code.size())) return 11;
    code.pop_back();
    for (std::size_t offset = 0; offset < code.size(); ++offset) {
        code[offset] ^= 1;
        // Stage-relevant production gates, for every DWORD including metadata.
        if (category) {
            if (find_rigid_position(code.data(), count) ||
                classify_vertex_position(code.data(), count) != VertexPositionPath::Unknown)
                return 12;
        } else if (find_pixel_coverage(code.data(), count))
            return 13;
        code[offset] ^= 1;
        ++mutations;
    }
    std::printf(
        "PASS hash=%s words=%u path=%u matrix=%d coverage=%u version=%08x order=%u constructor=%u mutations=%u\n",
        hash.c_str(), count, category, reg, coverage, version, order, constructor, count);
    return 0;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream manifest(argv[1]);
    if (!manifest) return 2;
    std::string line;
    unsigned programs = 0, mutations = 0;
    while (std::getline(manifest, line)) {
        if (const int result = verify(line, mutations)) {
            std::fprintf(stderr, "FAIL program=%u code=%d\n", programs, result);
            return result;
        }
        ++programs;
    }
    std::printf("TOTAL programs=%u mutations=%u\n", programs, mutations);
    return programs ? 0 : 14;
}
