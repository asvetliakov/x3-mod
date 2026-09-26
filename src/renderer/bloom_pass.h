#pragma once
// Native D3D9 executor for the original-then-RGB-replacement bloom boundary.
// This class does not discover/patch the boundary, run TAA, meter, compile
// shaders, open/end scenes, acquire locks, or decide whether a game invocation
// is eligible. See docs/architecture/bloom-pass-runtime.md for caller duties.
#include <d3d9.h>
#include <cstddef>
#include <cstdint>
#include "../temporal/bloom.h"
#include "../temporal/sharpen.h"

namespace x3m::renderer {
struct BloomBytecode {
    const DWORD* words = nullptr;
    std::size_t count = 0;
};
struct BloomPrograms {
    BloomBytecode vertex; // existing pass-through quad vs_3_0
    // gamma/sRGB/none generic, then gamma/sRGB/none even-size extraction.
    BloomBytecode extract[6];
    BloomBytecode down, up, candidate, sharpen, copy;
};
struct BloomCaps {
    bool enabled = false;
    const char* reason = "detached";
    HRESULT formats = S_FALSE, programs = S_FALSE;
    bool mixed_vertex_processing = false;
};
// Caller pins these native resources across the original invocation. Depth
// nullptr is a KNOWN null binding, never an unavailable query. The exact
// descriptors must be captured by the caller and remain current until return.
struct BloomBoundary {
    IDirect3DSurface9* main = nullptr;
    IDirect3DSurface9* depth = nullptr;
    D3DSURFACE_DESC main_desc{}, depth_desc{};
    std::uint64_t frame = 0, reset = 0;
    DWORD thread = 0;
    // True only after caller qualified exact owner/thread/invocation and no
    // active queries, state-block recording, Reset, reentrancy or lost device;
    // an application BeginScene is already open. Requalify after original.
    bool admitted = false;
};
struct BloomPrepare {
    IDirect3DTexture9* scene = nullptr; // borrowed pre-original resolved FP16
    BloomBoundary boundary{};
    x3::temporal::BloomParams filter{};
    // Exact block used by the ordinary writeback, including its latched
    // multiplier. No exposure adaptation/reconstruction occurs in this pass.
    x3::temporal::AgxConstants agx{};
    x3::temporal::AgxDecode decode = x3::temporal::AgxDecode::gamma22;
    float sharpen = 0.f;
    // Live handoff copies the actual writeback c23. Standalone callers may
    // leave this off to derive the existing parameters from strength/size.
    x3::temporal::SharpenConstants sharpen_constants{};
    bool exact_sharpen = false;
};
struct BloomCandidate {
    const void* owner = nullptr;
    std::uint64_t epoch = 0, serial = 0, frame = 0, reset = 0;
    DWORD thread = 0;
    IDirect3DSurface9* surface = nullptr; // borrowed; identity valid until revoke
    IDirect3DSurface9* main = nullptr;
    IDirect3DSurface9* depth = nullptr;
};
struct BloomPreparation {
    BloomCandidate candidate{};
    HRESULT allocation = S_FALSE, saved = S_FALSE, operation = S_FALSE, restore = S_FALSE;
    bool ready = false, state_preserved = true;
    const char* reason = "invalid";
};
struct BloomCommit {
    HRESULT saved = S_FALSE, backup = S_FALSE, operation = S_FALSE;
    HRESULT restore = S_FALSE, recovery = S_FALSE, recovery_restore = S_FALSE;
    bool committed = false;
    // A failed draw can have modified main. These separate actual rollback
    // success from state restoration; committed is never set after recovery.
    bool original_preserved = true, state_preserved = true;
    bool write_attempted = false, requalification_required = false;
    const char* reason = "invalid";
};
enum class BloomFault : unsigned {
    None,
    Allocate,
    Save,
    Setup,
    PrepareDraw,
    PrepareRestore,
    Backup,
    CommitDrawAfterWrite,
    CommitRestore,
    Recovery,
    RecoveryRestore
};

class BloomPass {
public:
    BloomPass() = default;
    ~BloomPass();
    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;
    // Borrowed device/native method table, valid until shutdown. Supplied
    // authored bytecode need only live through this call; created shaders are
    // owned. This queries public caps/formats and creates resources, no draws.
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&, const BloomPrograms&) noexcept;
    const BloomCaps& caps() const noexcept { return caps_; }
    bool enabled() const noexcept { return caps_.enabled && !disabled_; }
    BloomPreparation prepare(const BloomPrepare&) noexcept;
    // Consumes the pending serial even on rejection. Caller must pin candidate,
    // main and depth through this call and supply a fresh post-original admission.
    BloomCommit commit(const BloomCandidate&, const BloomBoundary&) noexcept;
    bool valid(const BloomCandidate&) const noexcept;
    // Revoke before releasing DEFAULT-pool objects. Caller must release its
    // separately pinned invocation references BEFORE entering native Reset.
    void before_reset() noexcept;
    void shutdown() noexcept;
    std::uint64_t resource_bytes() const noexcept;
    std::uint64_t peak_allocation_bytes() const noexcept { return peak_bytes_; }
    unsigned references() const noexcept; // persistent native device references
    unsigned transient_views() const noexcept { return transient_views_; }
    bool releasing() const noexcept { return releasing_; }
#ifdef X3M_BLOOM_PASS_FIXTURE
    void set_fault(BloomFault f, unsigned count = 1) noexcept {
        const unsigned i = static_cast<unsigned>(f);
        if (i < fault_count_) faults_[i] = count;
    }
#endif
private:
    struct SavedState;
    struct TextureViews;
    struct Image {
        IDirect3DSurface9* surface = nullptr;
        UINT width = 0, height = 0;
    };
    struct Resources {
        Image down[x3::temporal::kBloomMaxLevels]{}, up[x3::temporal::kBloomMaxLevels]{};
        Image stage{}, candidate{}, recovery{};
        x3::temporal::BloomLayout layout{};
        UINT width = 0, height = 0;
        bool sharpen = false;
    } resources_{};
    template <class Fn> Fn call(unsigned slot) const noexcept { return reinterpret_cast<Fn>(native_[slot]); }
    static void release(Resources&) noexcept;
    static std::uint64_t bytes(const Resources&) noexcept;
    HRESULT create(Image&, UINT, UINT, D3DFORMAT) noexcept;
    HRESULT ensure(UINT, UINT, unsigned levels, bool sharpen) noexcept;
    HRESULT save(SavedState&) noexcept;
    HRESULT restore(const SavedState&, bool inject_partial_failure = false) noexcept;
    HRESULT setup(const SavedState&, DWORD mask) noexcept;
    HRESULT draw(const Image&, IDirect3DPixelShader9*, IDirect3DTexture9*, IDirect3DTexture9*,
                 const x3::temporal::BloomConstants* = nullptr, bool* issued = nullptr) noexcept;
    HRESULT validate_boundary(const BloomBoundary&, const SavedState&, bool full_viewport) const noexcept;
    HRESULT validate_inputs(const BloomPrepare&) const noexcept;
    bool owned(IDirect3DSurface9*) const noexcept;
    void revoke() noexcept;
#ifdef X3M_BLOOM_PASS_FIXTURE
    static constexpr unsigned fault_count_ = static_cast<unsigned>(BloomFault::RecoveryRestore) + 1;
    unsigned faults_[fault_count_]{};
    bool fault(BloomFault f) noexcept {
        auto& n = faults_[static_cast<unsigned>(f)];
        if (!n) return false;
        --n;
        return true;
    }
#else
    static constexpr bool fault(BloomFault) noexcept { return false; }
#endif
    IDirect3DDevice9* device_ = nullptr;
    void* const* native_ = nullptr;
    D3DCAPS9 device_caps_{};
    BloomCaps caps_{};
    IDirect3DVertexShader9* vertex_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DPixelShader9* extract_[6]{};
    IDirect3DPixelShader9* down_ = nullptr;
    IDirect3DPixelShader9* up_ = nullptr;
    IDirect3DPixelShader9* candidate_ = nullptr;
    IDirect3DPixelShader9* sharpen_ = nullptr;
    IDirect3DPixelShader9* copy_ = nullptr;
    bool disabled_ = false;
    bool releasing_ = false;
    unsigned transient_views_ = 0;
    std::uint64_t epoch_ = 1, serial_ = 0, peak_bytes_ = 0;
    BloomCandidate pending_{};
};
} // namespace x3m::renderer
