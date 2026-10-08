#pragma once
// The ps_3_0 instruction-slot budget the shader transformers check programs against (linear_material.cpp structure,
// linear_emission.cpp hull_structure; AGENTS.md "Shader slot budget", docs/architecture/platform-portability.md):
// D3DCAPS9::MaxPixelShader30InstructionSlots as the device reports it at creation, except that the spec minimum 512
// (what wined3d reports; this runtime creates and executes programs far above it, measured 2026-09-24) and anything
// below it plan against the modern-driver cap 32768 (D3DMAX30SHADERINSTRUCTIONS, also the ceiling). A device that then
// refuses a program at CreatePixelShader keeps the caller's one-row refusal (the twin or the pass stays off). Set once
// per device attach (MotionOutput::attach, the last device wins); 32768 until then (host tests and fixtures). Kept free
// of d3d9.h so host tests compile it.
#include <atomic>
#include <cstdint>
namespace x3m::renderer {
constexpr std::uint32_t ps3_spec_minimum_slots = 512, ps3_planned_slots = 32768;
constexpr std::uint32_t ps3_slot_budget_for(std::uint32_t reported) noexcept {
    return reported <= ps3_spec_minimum_slots ? ps3_planned_slots
           : reported < ps3_planned_slots     ? reported
                                              : ps3_planned_slots;
}
inline std::atomic<std::uint32_t> ps3_slot_budget_value{ps3_planned_slots};
inline std::uint32_t ps3_slot_budget() noexcept {
    return ps3_slot_budget_value.load(std::memory_order_relaxed);
}
// Applies the rule to the device's reported cap; returns the budget now in force.
inline std::uint32_t set_ps3_slot_budget(std::uint32_t reported) noexcept {
    const std::uint32_t budget = ps3_slot_budget_for(reported);
    ps3_slot_budget_value.store(budget, std::memory_order_relaxed);
    return budget;
}
} // namespace x3m::renderer
