#pragma once
// Authored, precompiled ps_3_0 bloom bundle: no runtime compiler or game bytes.
// Regenerate/check ONLY these nine new programs with
// python3 verification/probe/wine_lock.py python3 tools/shaders/generate_bloom_programs.py --check
// Existing quad, display RCAS and identity copy retain their own provenance.
#include "bloom_pass.h"
#include "quad_vertex_program.h"
#include "taa_sharpen_program.h"
#include "hdr_writeback_program.h"
#include <array>

namespace x3m::renderer {
namespace detail {
inline constexpr DWORD bloom_extract_gamma_words[] = {
#include "bloom_extract_gamma_program_inc.h"
};
inline constexpr DWORD bloom_extract_srgb_words[] = {
#include "bloom_extract_srgb_program_inc.h"
};
inline constexpr DWORD bloom_extract_none_words[] = {
#include "bloom_extract_none_program_inc.h"
};
inline constexpr DWORD bloom_extract_even_gamma_words[] = {
#include "bloom_extract_even_gamma_program_inc.h"
};
inline constexpr DWORD bloom_extract_even_srgb_words[] = {
#include "bloom_extract_even_srgb_program_inc.h"
};
inline constexpr DWORD bloom_extract_even_none_words[] = {
#include "bloom_extract_even_none_program_inc.h"
};
inline constexpr DWORD bloom_down_words[] = {
#include "bloom_down_program_inc.h"
};
inline constexpr DWORD bloom_up_words[] = {
#include "bloom_up_program_inc.h"
};
inline constexpr DWORD bloom_agx_words[] = {
#include "bloom_agx_program_inc.h"
};
// DWORD and uint32_t need not be the same C++ type under LLP64/MinGW.
// Convert the three existing arrays at compile time instead of type-punning
// their storage through a DWORD pointer. There is no runtime copy/allocation.
template<std::size_t N>
constexpr std::array<DWORD, N> bloom_native_words(const std::uint32_t (&source)[N]) noexcept {
    std::array<DWORD, N> result{};
    for (std::size_t i = 0; i < N; ++i) result[i] = source[i];
    return result;
}
inline constexpr auto bloom_quad_words = bloom_native_words(quad_vertex_program());
inline constexpr auto bloom_sharpen_words = bloom_native_words(taa_sharpen_program());
inline constexpr auto bloom_copy_words = bloom_native_words(hdr_writeback_program());
template<std::size_t N>
constexpr BloomBytecode bloom_code(const DWORD (&words)[N]) noexcept { return {words, N}; }
template<std::size_t N>
constexpr BloomBytecode bloom_code(const std::array<DWORD, N>& words) noexcept { return {words.data(), N}; }
} // namespace detail

inline constexpr BloomPrograms bloom_programs() noexcept {
    using namespace detail;
    return {bloom_code(bloom_quad_words),
        {bloom_code(bloom_extract_gamma_words), bloom_code(bloom_extract_srgb_words),
         bloom_code(bloom_extract_none_words), bloom_code(bloom_extract_even_gamma_words),
         bloom_code(bloom_extract_even_srgb_words), bloom_code(bloom_extract_even_none_words)},
        bloom_code(bloom_down_words), bloom_code(bloom_up_words), bloom_code(bloom_agx_words),
        bloom_code(bloom_sharpen_words), bloom_code(bloom_copy_words)};
}
} // namespace x3m::renderer
