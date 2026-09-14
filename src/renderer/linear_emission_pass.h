#pragma once
// Single indexed-draw executor; no classifier, shader transform/cache, geometry
// replay, scene lifetime, HdrPass ownership exchange or live hook integration.
#include <cstdint>
#include <d3d9.h>
namespace x3m::renderer {
// One pool/coverage owner serves every producer policy. Policy bits are stable
// configuration; each boundary selects only a capability-qualified program.
// DistanceFadeInPlace composes the fade rectangle back into the owning target
// itself (docs/architecture/linear-distance-fade-region.md, section 3): no
// owning candidate, no exchange, no acknowledgement. It shares the pool and
// the DistanceFade program and additionally needs D3DPRASTERCAPS_SCISSORTEST.
// PackedScreenInPlace runs the packed screen law L' = E + (1 - q) L inside
// the same in-place bracket (docs/architecture/screen-emission-region.md):
// backup B|R = A|R, plane init (P_c|R = (A_c, decode(A)_c, 0), M.alpha = A.alpha),
// the source into M (red|alpha) and three planes under ONE/INVSRCALPHA, then
// a scissored composite into A. Planes: E = P_r, C = P_g, one extra P_b; it
// needs four simultaneous targets, independent masks and INVSRCALPHA on FP16.
enum class LinearCompositionPolicy : unsigned { AdditiveEmission = 1, DistanceFade = 2, DistanceFadeInPlace = 4, PackedScreenInPlace = 8 };
constexpr unsigned composition_policy_bit(LinearCompositionPolicy p) noexcept { return unsigned(p); }
struct LinearEmissionPassCaps {
  bool enabled = false;
  unsigned supported_policies = 0, available_policies = 0;
  bool supports(LinearCompositionPolicy policy) const noexcept {
    return (available_policies & composition_policy_bit(policy)) != 0;
  }
  const char *reason = "detached";
  HRESULT formats = S_FALSE, programs = S_FALSE;
};
enum class LinearEmissionImage { None, Linear, Native, Incomplete };
struct LinearEmissionPreparation {
  bool ready = false, state_preserved = true;
  HRESULT saved = S_FALSE, operation = S_FALSE, restore = S_FALSE;
};
struct LinearEmissionCompletion {
  LinearEmissionImage image = LinearEmissionImage::None;
  HRESULT source = S_FALSE, composition = S_FALSE, restore = S_FALSE;
  bool candidate_bound = false; // NOT an ownership-publication acknowledgement
  // In-place policy only: after a failed source or composite the rectangle of
  // A is recovered from its pre-draw backup (exact copy); S_FALSE when unused.
  HRESULT recovery = S_FALSE;
  // In-place policy only: the rectangle actually backed up and composed
  // (the boundary region after the target/viewport/scissor intersection);
  // empty for the exchange policies. Diagnostic: per-frame region pixels.
  RECT region{};
};
struct LinearEmissionBoundary {
  // Exact borrowed HdrPass owning-slot value A. GetRT0 may be a canonical
  // interface alias, but acknowledgement returns this exact owner pointer.
  IDirect3DSurface9 *scene = nullptr;
  IDirect3DPixelShader9 *augmented =
      nullptr; // cached, qualified three-output PS
  std::uint64_t frame = 0;
  // Caller establishes single DIP, active scene/owner/thread, idle queries,
  // no recording/reentrancy/Reset/handoff pins, and flushed lazy MRT state.
  // Source shader has no oDepth; internal targets have no application aliases.
  bool admitted = false;
  // Saved original VS must be restored even after a partially mutating setter.
  IDirect3DVertexShader9 *augmented_vertex = nullptr;
  LinearCompositionPolicy policy = LinearCompositionPolicy::AdditiveEmission;
  // In-place policies (4, 8): conservative target-pixel rectangle the source
  // can touch (fade_region::derive). Unknown selects the whole owning target; the
  // pass further intersects with the target, the viewport and an enabled
  // application scissor, and any empty result again selects the whole target.
  RECT region{};
  bool region_known = false;
};
enum class LinearEmissionPassFault {
  None,
  Allocation,
  Save,
  Copy,
  EmissionClear,
  SourceBind,
  Composite,
  Restore,
  RecoveryRestore,
  FrameClear,
  RegionScissor,    // in-place: scissor set of the region backup
  CompositeScissor, // in-place: scissor set of the region composite
  RegionRecovery,   // in-place: exact rectangle recovery copy B|R -> A|R
  PlaneInit         // packed: the plane/M.alpha initialization draw
};
class LinearEmissionPass {
public:
  LinearEmissionPass() = default;
  ~LinearEmissionPass();
  LinearEmissionPass(const LinearEmissionPass &) = delete;
  LinearEmissionPass &operator=(const LinearEmissionPass &) = delete;
  // Device/table borrowed. Every injected device call uses native slots.
  HRESULT attach(IDirect3DDevice9 *, void *const *native, const D3DCAPS9 &,
                 D3DFORMAT adapter_format, D3DFORMAT depth_format,
                 unsigned requested_policies = composition_policy_bit(LinearCompositionPolicy::AdditiveEmission)) noexcept;
  const LinearEmissionPassCaps &caps() const noexcept;
  // Resource creation happens only here, outside any prepare/finish bracket.
  HRESULT ensure_targets(UINT width, UINT height) noexcept;
  // New frame, known/resynchronized caller state, no pending publication.
  // Clears M once and preserves application state. Same frame cannot re-clear.
  LinearEmissionPreparation begin_frame(std::uint64_t frame) noexcept;
  LinearEmissionPreparation prepare(const LinearEmissionBoundary &) noexcept;
  // Caller invokes the original native DIP ONCE between prepare and finish.
  // Failed source is always Incomplete, even if a best-effort B bind succeeds.
  LinearEmissionCompletion finish(HRESULT source) noexcept;
  // Only the selected, successfully bound candidate exposes its owning slot.
  // Caller may only pass *slot to HdrPass::exchange_target. No other mutation.
  // DistanceFadeInPlace never exposes a slot: finish() already left the
  // result in the owning target and returned the pass to idle.
  IDirect3DSurface9 **owning_candidate() noexcept;
  // Ownership acknowledgement only: even exchanged Incomplete B remains
  // incomplete/blocked with invalid coverage; this never authorizes history.
  // On success the slot must now contain old A. On failure it must still own
  // the candidate; the transaction remains pending for one recovery attempt.
  HRESULT acknowledge_exchange(bool exchanged) noexcept;
  LinearEmissionCompletion recover_native() noexcept;
  IDirect3DSurface9 *coverage_target() const noexcept; // borrowed until reset
  bool coverage_valid() const noexcept;
  // Persistent interface count only. Caller must defer device-reference
  // accounting during a bracket, when saved getter references are retained.
  bool reference_accounting_busy() const noexcept;
  unsigned references() const noexcept;
  unsigned allocations() const noexcept; // successful persistent target creates
  // Caller serializes Reset, releases external coverage/candidate views and
  // has not retained a handoff pin on a recycled surface. No refcount guessing.
  void before_reset() noexcept;
  void detach() noexcept;
#ifdef X3M_LINEAR_EMISSION_PASS_FIXTURE
  // Pre-attach fixture twin: retain checkpoint's separate copy/Clear sequence.
  void fixture_separate_copy(bool enabled) noexcept { if (!impl_) fixture_separate_copy_ = enabled; }
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
  // Borrowed bytes need only survive attach; CreatePixelShader owns the result.
  // A null program retains the unchanged additive component behavior.
  void fixture_source_over(const DWORD *words) noexcept { if (!impl_) fixture_source_over_ = words; }
#endif
  void inject(LinearEmissionPassFault, unsigned count = 1) noexcept;
  LinearEmissionCompletion fixture_completion() const noexcept;
  IDirect3DSurface9 *fixture_native() const noexcept;
  IDirect3DSurface9 *fixture_energy() const noexcept;
  IDirect3DSurface9 *fixture_plane_b() const noexcept; // policy 8 only, else null
#endif
private:
  struct Impl;
  Impl *impl_ = nullptr;
#ifdef X3M_LINEAR_EMISSION_PASS_FIXTURE
  bool fixture_separate_copy_ = false;
#ifdef X3M_LINEAR_DISTANCE_FADE_FIXTURE
  const DWORD *fixture_source_over_ = nullptr;
#endif
#endif
};
} // namespace x3m::renderer
