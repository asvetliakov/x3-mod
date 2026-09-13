#pragma once
// Authored detached ps_3_0 bytecode. No compiler/Wine is needed to freeze the
// exact composition input. Gamma fragment follows the qualified emission
// operand ordering; scalar q branches are new and independently GPU-qualified.
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>
namespace distance_fade_composite {
using Words = std::vector<std::uint32_t>;
inline std::uint32_t reg(unsigned t, unsigned n) {
  return 0x80000000u | ((t & 7) << 28) | ((t & 24) << 8) | n;
}
inline std::uint32_t dst(unsigned t, unsigned n, unsigned m = 15) {
  return reg(t, n) | (m << 16);
}
inline std::uint32_t src(unsigned t, unsigned n, unsigned s = 0xe4,
                         bool neg = false) {
  return reg(t, n) | (s << 16) | (neg ? 1u << 24 : 0);
}
inline void emit(Words &w, unsigned op,
                 std::initializer_list<std::uint32_t> args) {
  w.push_back(op | (unsigned(args.size()) << 24));
  w.insert(w.end(), args);
}
inline void def(Words &w, unsigned n, float x, float y, float z, float a) {
  const float f[] = {x, y, z, a};
  std::uint32_t b[4];
  std::memcpy(b, f, 16);
  emit(w, 81, {dst(2, n), b[0], b[1], b[2], b[3]});
}
inline void gamma(Words &w, bool encode) {
  emit(w, 11, {dst(0, 1, 7), src(0, 0), src(2, 20, 0)});
  emit(w, 10, {dst(0, 1, 7), src(0, 1), src(2, 20, 0x55)});
  emit(w, 11, {dst(0, 2, 7), src(0, 1), src(2, 20, encode ? 0xff : 0xaa)});
  for (unsigned k = 0; k < 3; ++k)
    emit(w, 32,
         {dst(0, 2, 1u << k), src(0, 2, k * 0x55),
          src(2, 21, encode ? 0x55 : 0)});
  emit(w, 88, {dst(0, 0, 7), src(0, 1, 0xe4, true), src(2, 20, 0), src(0, 2)});
  if (!encode) {
    emit(w, 11, {dst(0, 0, 7), src(0, 0), src(2, 20, 0)});
    emit(w, 10, {dst(0, 0, 7), src(0, 0), src(2, 20, 0x55)});
  }
}
inline Words program() {
  Words w{0xffff0300};
  emit(w, 31, {0x80000005, dst(1, 0, 3)});
  for (unsigned s = 0; s < 3; ++s)
    emit(w, 31, {0x90000000, dst(10, s)});
  def(w, 20, 0, 65504, 1e-10f, 1e-22f);
  def(w, 21, 2.2f, 1.f / 2.2f, 1, 0);
  emit(w, 66, {dst(0, 4), src(1, 0), src(10, 0)}); // raw A
  emit(w, 66, {dst(0, 5), src(1, 0), src(10, 1)}); // Q,q
  emit(w, 66, {dst(0, 6), src(1, 0), src(10, 2)}); // native B alpha
  emit(w, 11, {dst(0, 5, 7), src(0, 5), src(2, 20, 0)});
  emit(w, 10, {dst(0, 5, 7), src(0, 5), src(2, 20, 0x55)});
  emit(w, 11, {dst(0, 5, 8), src(0, 5, 0xff), src(2, 20, 0)});
  emit(w, 10, {dst(0, 5, 8), src(0, 5, 0xff), src(2, 21, 0xaa)});
  emit(w, 1, {dst(0, 0), src(0, 4)}); // exact q=0 raw path
  emit(w, 41 | (1u << 16), {src(0, 5, 0xff), src(2, 20, 0)});    // IF q>0
  emit(w, 41 | (4u << 16), {src(0, 5, 0xff), src(2, 21, 0xaa)}); // IF q<1
  gamma(w, false);
  emit(w, 2, {dst(0, 7, 1), src(2, 21, 0xaa), src(0, 5, 0xff, true)});
  emit(w, 4, {dst(0, 0, 7), src(0, 0), src(0, 7, 0), src(0, 5)});
  emit(w, 42, {}); // ELSE: no decode(A), no 0*bad background
  emit(w, 1, {dst(0, 0, 7), src(0, 5)});
  emit(w, 43, {});
  gamma(w, true);
  emit(w, 43, {});
  emit(w, 1, {dst(0, 0, 8), src(0, 6, 0xff)});
  emit(w, 1, {dst(8, 0), src(0, 0)});
  w.push_back(0xffff);
  return w;
}
} // namespace distance_fade_composite
