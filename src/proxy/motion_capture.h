#pragma once
#include "draw_input.h"
#include "object_lifetime.h"
#include "../ownership/execution_state.h"
#include "../renderer/scene_boundary.h"
#include <vector>

namespace x3m {
// Private rigid-motion diagnostic at the selected pre-Clear boundary. All calls
// are serialized with application draws/writes. No application COM references or
// GPU resources survive before_clear(); only bounded CPU matrix history remains.
// Storage correspondence is deliberately separate from camera-cut continuity and
// final-color coverage. This output MUST NOT be used for temporal accumulation.
struct MotionCaptureDiagnostics {
    HRESULT operation=S_FALSE, restoration=S_OK;
    ownership::ExecutionView execution{};
    std::uint64_t observations=0, eligible=0, matched=0, rejected=0, completed=0;
    bool attempted=false, produced=false, continuity_known=false, color_coverage_known=false;
};
class MotionCapture {
public:
    using DiagnosticConsumer=void (*)(void*,const renderer::RigidMotionOutput&) noexcept;
    MotionCapture() noexcept;
    ~MotionCapture();
    MotionCapture(const MotionCapture&)=delete;
    MotionCapture& operator=(const MotionCapture&)=delete;
    void begin_frame(IDirect3DDevice9* application,std::uint64_t frame,bool requested) noexcept;
    ownership::GeometryFrameHandle geometry_frame() const noexcept { return geometry_; }
    // Submit every completed draw in SceneCapture's main Scene phase, including
    // ineligible observations, so a late duplicate can invalidate earlier input.
    void observe(const DrawInput&,std::uintptr_t registry,const object_lifetime::Snapshot&) noexcept;
    // Consumer is synchronous and diagnostic only; borrowed output expires on
    // return. It permits numerical verification without retaining game resources.
    MotionCaptureDiagnostics before_clear(IDirect3DDevice9* application,
        const renderer::Selection&,DiagnosticConsumer consumer=nullptr,void* context=nullptr) noexcept;
    void after_clear(bool selected_clear_confirmed) noexcept;
    // Returns only whether CPU storage correspondence was committed. This
    // never certifies temporal-color history or camera continuity.
    bool end_frame(bool selected_frame_confirmed,HRESULT present_result) noexcept;
    void invalidate() noexcept;
private:
    struct Record { DrawInput input;std::uintptr_t registry;object_lifetime::Snapshot lifetime; };
    struct Domain {
        std::uint64_t device=0,generation=0,color=0,depth=0,finite=0;
        std::uint64_t observer=0,load=0,registry=0,camera=0;
        std::uint32_t width=0,height=0;
    };
    static bool same(const Domain&,const Domain&) noexcept;
    void release_geometry() noexcept;
    std::vector<Record> records_;
    renderer::MotionHistory history_;
    ownership::GeometryFrameHandle geometry_{};
    Domain domain_{};
    std::uint64_t frame_=0,epoch_=0;
    bool active_=false,sealed_=false,produced_=false,confirmed_=false;
};
} // namespace x3m
