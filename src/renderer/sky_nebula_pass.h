#pragma once
// Sky nebula deband, Decision C1 (docs/architecture/sector-background-modern.md, "Decision C"): the GPU side of the
// nebula part draw swap. One class owns the vs_3_0 / ps_3_0 pair (src/effects/sky_nebula_{vs,ps}.hlsl) and brackets one
// application draw: begin() reads the bound programs, VS c255 and PS c200-c201 before the first write, sets its pair and
// those constants; the caller forwards the application's draw (its vertex input, textures, sampler and render states and
// the engine's VS c0-c5 / PS c0 untouched); end() puts the programs and constants back in reverse order. A failed
// write in begin() is undone at once (begin reports the operation and the restoration apart); a failed restoration
// leaves the device's state unknown and the caller quarantines. Shader objects survive Reset: nothing is released or
// recreated there. Documented D3D9 only (Create/Set/Get Vertex/PixelShader, Set/Get Vertex/PixelShaderConstantF);
// attach checks shader model 3 and refuses a device whose getters fail (a pure device).
#include <d3d9.h>
#include <cstdint>
namespace x3m::renderer {
struct SkyNebulaCaps {
    bool enabled = false;
    const char* reason = "not_attached";
    unsigned vs_slots = 0, ps_slots = 0;
};
// The application's bindings begin() replaced (references held until end()).
struct SkyNebulaSaved {
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    float vs_c255[4] = {};
    float ps_c200[8] = {};
    unsigned written = 0; // bit 0 VS constant, 1 PS constants, 2 VS, 3 PS
};
struct SkyNebulaReport {
    HRESULT operation = S_FALSE; // begin: S_OK swapped, a failure code otherwise
    HRESULT restore = S_OK;      // a failed restoration leaves the device's state unknown
    unsigned calls = 0;
};
class SkyNebulaPass {
public:
    SkyNebulaPass() = default;
    ~SkyNebulaPass();
    SkyNebulaPass(const SkyNebulaPass&) = delete;
    SkyNebulaPass& operator=(const SkyNebulaPass&) = delete;
    HRESULT attach(IDirect3DDevice9*, void* const* native, const D3DCAPS9&) noexcept;
    void detach() noexcept;
    const SkyNebulaCaps& caps() const noexcept { return caps_; }
    bool ready() const noexcept { return device_ && caps_.enabled && vs_ && ps_; }
    // Swaps the pair in with VS c255 = jitter[4] and PS c200-c201 = pixel[8] (sky_nebula::core). On a failure every
    // write already made is restored before returning (report->restore), and *saved holds nothing.
    HRESULT begin(const float jitter[4], const float pixel[8], SkyNebulaSaved* saved, SkyNebulaReport* report) noexcept;
    // Puts back what begin() replaced and releases the references; the first failed restoration is returned.
    HRESULT end(SkyNebulaSaved* saved, unsigned* calls = nullptr) noexcept;
    unsigned references() const noexcept { return (vs_ ? 1u : 0u) + (ps_ ? 1u : 0u); }
#if defined(X3M_SKY_NEBULA_FIXTURE)
    // Fixture faults: bit 0 fails begin()'s last write (the PS) once, bit 1 fails end()'s first restoration once.
    void set_faults(unsigned faults) noexcept { faults_ = faults; }
#endif
private:
    template <class Fn> Fn call(unsigned slot) const noexcept {
        ++calls_;
        return reinterpret_cast<Fn>(vtable_[slot]);
    }
    HRESULT restore(SkyNebulaSaved* saved) noexcept;
    void release_objects() noexcept;
    IDirect3DDevice9* device_ = nullptr;
    void* const* vtable_ = nullptr; // borrowed
    SkyNebulaCaps caps_{};
    mutable unsigned calls_ = 0;
    unsigned faults_ = 0;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
};
} // namespace x3m::renderer
