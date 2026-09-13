// Detached host exporter; local original bytes and emitted programs stay
// untracked.
#include "../../src/renderer/linear_distance_fade.h"
#include "linear_distance_fade_composite_inc.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace x3m::renderer;
int main(int argc, char **argv) {
  if (argc == 3 && std::strcmp(argv[1], "--composite") == 0) {
    const auto words = distance_fade_composite::program();
    std::ofstream output(argv[2], std::ios::binary);
    output.write(reinterpret_cast<const char *>(words.data()),
                 words.size() * 4);
    return output.good() ? 0 : 8;
  }
  if (argc != 6)
    return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
  if (bytes.empty() || bytes.size() % 4)
    return 3;
  std::vector<std::uint32_t> original(bytes.size() / 4), output{0xabcdef01};
  std::memcpy(original.data(), bytes.data(), bytes.size());
  const auto saved = original;
  const bool vertex = original[0] == 0xfffe0300u;
  const int mode = std::stoi(argv[3]);
  const float gain = std::stof(argv[4]);
  const bool depth = std::stoi(argv[5]) != 0;
  LinearMaterialConfig config{gain, gain, gain};
  LinearMaterialResult result;
  if (mode == 0) {
    result =
        vertex ? linear_material_vertex_variant(
                     original.data(), original.size(), config, output, depth)
               : linear_material_pixel_variant(original.data(), original.size(),
                                               config, output, depth);
  } else {
#ifdef X3M_FADE_BASELINE
    return 4;
#else
    const auto fn = vertex ? linear_distance_fade_vertex_variant
                           : linear_distance_fade_pixel_variant;
    if (mode == 2) {
      output = original;
      result = fn(output.data(), output.size(), config, output);
    } else
      result = fn(original.data(), original.size(), config, output);
#endif
  }
  if (original != saved)
    return 5;
  if (result != LinearMaterialResult::Applied && mode != 2 &&
      output != std::vector<std::uint32_t>{0xabcdef01})
    return 6;
  std::cout << static_cast<int>(result) << ' ' << output.size() << '\n';
  std::ofstream destination(argv[2], std::ios::binary);
  destination.write(reinterpret_cast<const char *>(output.data()),
                    output.size() * 4);
  return destination.good() ? 0 : 7;
}
