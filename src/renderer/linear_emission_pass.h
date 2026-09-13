#pragma once
// Single indexed-draw executor; no classifier, shader transform/cache, geometry
// replay, scene lifetime, HdrPass ownership exchange or live hook integration.
#include <cstdint>
#include <d3d9.h>
namespace x3m::renderer {
struct LinearEmissionPassCaps {
  bool enabled = false;
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
  FrameClear
};
class LinearEmissionPass {
public:
  LinearEmissionPass() = default;
  ~LinearEmissionPass();
  LinearEmissionPass(const LinearEmissionPass &) = delete;
  LinearEmissionPass &operator=(const LinearEmissionPass &) = delete;
  // Device/table borrowed. Every injected device call uses native slots.
  HRESULT attach(IDirect3DDevice9 *, void *const *native, const D3DCAPS9 &,
                 D3DFORMAT adapter_format, D3DFORMAT depth_format) noexcept;
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
  void inject(LinearEmissionPassFault, unsigned count = 1) noexcept;
  LinearEmissionCompletion fixture_completion() const noexcept;
  IDirect3DSurface9 *fixture_native() const noexcept;
  IDirect3DSurface9 *fixture_energy() const noexcept;
#endif
private:
  struct Impl;
  Impl *impl_ = nullptr;
};
} // namespace x3m::renderer
