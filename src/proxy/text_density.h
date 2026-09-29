#pragma once
#include <cstdint>
struct IDirect3DDevice9;

// Text density (X3M_TEXT_DENSITY=auto|1|2|3, unset = auto; docs/architecture/text-density.md,
// docs/reverse-engineering/font-rendering.md section 3 strategy (a)). Under the UI scale s the in-game text is drawn
// into its textures at the integer density d = ceil(s) (auto) through the engine's own `-fontscale` supersampling,
// so the projection minifies d/s instead of magnifying a 1x bitmap: cfg+0x784 = d, every font open x d when the loose
// d x font pair exists under <game>\f, MPF_FONTSCALE on the generated writeable Materials rows (the text targets)
// right after the Materials load, MPF_FONTSCALE off the file-backed rows, a d x shadow for static blit sources into
// flagged targets, the style-offset factor d when d = 3. Six claims through engine_patch (four production, two
// diagnostic under X3M_DEBUG=1), all or none, at the backend-load path; the density itself is resolved at
// CreateDevice from ui_scale's scale (auto) and applied there (the config field, the style imm32, the rows when the
// Materials load already ran; otherwise the Materials thunk applies the rows). d = 1 (no UI scale) restores the
// claims and changes nothing.
namespace x3m::text_density {
bool initialize();                 // backend-load path: the setting, the executable, every window, the claims (inert)
// After ui_scale::device_created: resolves d and applies it when every d x font pair exists and the largest flagged row
// fits the device's texture limits (D3DCAPS9 through GetDeviceCaps; unknown = no limit when the device is null or the
// query fails); one text_density_install row.
bool device_created(double scale, IDirect3DDevice9* device);
void before_reset();               // frees the shadow surfaces (engine texture objects) before the device Reset
bool shutdown();                   // dynamic-unload detach only; true when nothing stays registered
const char* state();
unsigned density(); // the density in place (1 when off)
bool patched();     // any claim registered
}
extern "C" {
// The thunks (src/proxy/text_density.cpp) and their continuation words (the tails, or the original callee).
void x3m_text_density_font_thunk();
void x3m_text_density_materials_thunk();
void x3m_text_density_blt_block_thunk();
void x3m_text_density_blt_alpha_thunk();
void x3m_text_density_text_line_thunk();
void x3m_text_density_rect_fill_thunk();
extern std::uintptr_t x3m_text_density_font_continue, x3m_text_density_materials_continue,
    x3m_text_density_materials_return, x3m_text_density_blt_block_continue, x3m_text_density_blt_alpha_continue,
    x3m_text_density_text_line_continue, x3m_text_density_rect_fill_continue;
// The C sides, entered from the thunks with every register and EFLAGS saved (frame = the pushad/pushfd frame).
void __cdecl x3m_text_density_font_open(std::uint32_t* frame);
void __cdecl x3m_text_density_materials_loaded();
std::uint32_t __cdecl x3m_text_density_blit(std::uint32_t* frame, std::uint32_t kind);
void __cdecl x3m_text_density_diagnostic(std::uint32_t* frame, std::uint32_t kind);
}
