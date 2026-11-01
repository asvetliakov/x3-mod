#pragma once
// Admission of the game's main render target at the bloom boundary
// (BloomPass::validate_inputs / validate_boundary). No d3d9.h: the D3D9 values
// below are pinned against the real enums by static_asserts in bloom_pass.cpp,
// so the rule compiles on the host with any struct carrying the
// D3DSURFACE_DESC field names.
#include "../temporal/bloom.h"

namespace x3m::renderer {
constexpr unsigned long kBloomFormatA8R8G8B8 = 21; // D3DFMT_A8R8G8B8
constexpr unsigned long kBloomTypeSurface = 1;     // D3DRTYPE_SURFACE
constexpr unsigned long kBloomTypeTexture = 3;     // D3DRTYPE_TEXTURE
constexpr unsigned long kBloomUsageRenderTarget = 1; // D3DUSAGE_RENDERTARGET
constexpr unsigned long kBloomPoolDefault = 0;     // D3DPOOL_DEFAULT
constexpr unsigned long kBloomMultisampleNone = 0; // D3DMULTISAMPLE_NONE

template <class Desc> bool bloom_main_descriptor(const Desc& d) noexcept {
    const auto type = static_cast<unsigned long>(d.Type);
    // Native D3D9 reports the main target as a texture level (D3DRTYPE_TEXTURE);
    // Wine reports a standalone surface.
    return static_cast<unsigned long>(d.Format) == kBloomFormatA8R8G8B8 &&
           (type == kBloomTypeSurface || type == kBloomTypeTexture) &&
           (static_cast<unsigned long>(d.Usage) & kBloomUsageRenderTarget) &&
           static_cast<unsigned long>(d.Pool) == kBloomPoolDefault &&
           static_cast<unsigned long>(d.MultiSampleType) == kBloomMultisampleNone && !d.MultiSampleQuality &&
           x3::temporal::valid_bloom_size({d.Width, d.Height});
}
} // namespace x3m::renderer
