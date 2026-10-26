#pragma once
#include <cstdint>
#include <cstring>

// Portable core of the text-density option (X3M_TEXT_DENSITY=auto|1|2|3; docs/architecture/text-density.md,
// docs/reverse-engineering/font-rendering.md section 3 strategy (a)): the verified byte windows of every engine site
// and helper the module touches, the option parser, the density mapping, the font-file rewrite, the Materials
// row-flag plan, the engine's destination clip (a twin of 0x0048c090's), the blit-argument layout and the texture
// object layout. No Windows dependency: the host tests and the site verifier compile it directly.
//
// Text under the mod's UI scale s is drawn into generated textures at density d = ceil(s) through the engine's own
// `-fontscale N` path: cfg+0x784 = d (the config object *0x00606f34, created by 0x004ec9e0 from 0x0040283a before
// Direct3DCreate9 at 0x00402edc, so the DLL writes the field itself); every font open through 0x0048cdc0 asks for the
// d x font (size, cell width and y offset x d) when the loose file f\<name><S*d>.abc/.tga exists; the Materials rows
// that are MPF_GENERATED|MPF_WRITEABLE (35 in the shipped table) get MPF_FONTSCALE (and lose MPF_NOFILTERING when
// s != d) right after 0x004f44a0 loaded the table (the call at 0x0048af71, from 0x00403497 in the init routine, after
// Direct3DCreate9 and before the main loop 0x00403840 that opens the first font at 0x00403a26), while the 8 file-backed
// MPF_FONTSCALE rows lose the flag so they keep loading their stock files; the two block blits 0x0048c090 /
// 0x0048c460 receive a d x nearest-neighbour shadow of a static (file-backed) source when the destination is a
// flagged row and the source is not; KC's B3D_TexBltImage block at 0x004960c0 (which would scale the source rectangle
// of its 1x image too and read past it) runs through the C side with the d x shadow of the image or an unscaled
// source rectangle, clamped to both surfaces; the style-offset factor imm32 at 0x004f813b becomes d when d = 3.
//
// Every claim is byte-verified and the claims are one transaction (a failure restores the earlier ones). The stubs are
// asm thunks in the DLL (pinned): pushfd/pushad, one cdecl C call, popad/popfd, then the tail or a return.
namespace x3m::text_density::sites {
// ---- the engine's globals and layouts ----
constexpr std::uintptr_t config_slot_va = 0x00606f34; // *slot = the config object; +0x784 = the font-scale integer N
constexpr unsigned config_density_offset = 0x784;
constexpr std::uintptr_t row_count_va = 0x00608dac;  // int16: Materials rows
constexpr std::uintptr_t rows_slot_va = 0x00608db0;  // *slot = rows, stride 0x3c, +0x10 = MPF_ flags
constexpr std::uintptr_t table_slot_va = 0x006069ac; // *slot = texture table, stride 0x10: +4 flags, +8 object
constexpr std::uintptr_t named_count_va = 0x006069b4; // int16: named textures after the rows (ids rows .. rows+named-1)
constexpr unsigned row_stride = 0x3c, row_flags_offset = 0x10, row_width_offset = 0x14, row_height_offset = 0x16,
                   entry_stride = 0x10, entry_flags_offset = 4, entry_object_offset = 8;
constexpr std::uint32_t mpf_nofiltering = 0x100, mpf_fontscale = 0x10000, mpf_writeable = 0x40000,
                        mpf_generated = 0x800000;
// The texture object 0x004f3950 builds (100 bytes): +0 pixels (locked bits), +4/+6 width/height (int16), +0xc bytes
// per pixel, +0x10 surface flags, +0x30 the D3D texture wrapper. A generated 32-bit row is created with surface flags
// 0x402c and 32 bits per pixel (0x004f4160: `test ebp,0x60100` -> 0x402c; bit 0x2000 = 32 bpp; bit 8 = D3D-backed,
// pool 1 (managed) unless surface flag 0x8000000/0x80000000).
constexpr unsigned object_width_offset = 4, object_height_offset = 6, object_flags_offset = 0x10;
constexpr std::uint32_t generated_surface_flags = 0x402c;
constexpr std::uint32_t generated_bits_per_pixel = 32;
// ---- the engine functions the module calls (prologues compared before anything is claimed) ----
constexpr std::uintptr_t lookup_va = 0x004f5110;      // ECX = id -> texture object (creates lazily), 0 when none
constexpr std::uintptr_t alloc_va = 0x004f3950;       // EAX = bpp; cdecl (w, h, surface flags) -> object
constexpr std::uintptr_t free_va = 0x004f38d0;        // cdecl (object**): release, delete, *p = 0
constexpr std::uintptr_t copy_va = 0x004dbbe0;        // cdecl (src, dst, sx, sy, dx, dy, w, h): plain copy leaf
constexpr std::uintptr_t colour_copy_va = 0x004ee990; // EAX = dst, EDI = src; cdecl (sx, sy, dx, dy, w, h, colour)
constexpr std::uintptr_t alpha_leaf_va = 0x004efa70;  // EAX = dst, EDI = src; cdecl (sx, sy, dx, dy, w, h, colour)
// ---- the sites ----
constexpr std::uintptr_t font_site_va = 0x0048cdc0, font_return_va = 0x0048cdc6;
constexpr unsigned font_site_length = 6, font_window_length = 20;
constexpr unsigned char expected_font_window[font_window_length] = {
    0x51, 0x53, 0x56, 0x57, 0x8b, 0xf9, // push ecx; push ebx; push esi; push edi; mov edi,ecx   <- the claim
    0x57, 0x50, 0x68, 0xc0, 0x11, 0x56, 0x00, 0x33, 0xd2, 0xe8, 0xec, 0x16, 0x06, 0x00}; // push edi; push eax;
                                                                                         // push "%s%d"; xor edx,edx;
                                                                                         // call sprintf
constexpr std::uintptr_t materials_wrapper_va = 0x0048af70, materials_site_va = 0x0048af71,
                         materials_target_va = 0x004f44a0, materials_return_va = 0x0048af76,
                         materials_caller_va = 0x00403497;
constexpr unsigned materials_window_length = 11, materials_callee_length = 8;
constexpr unsigned char expected_materials_window[materials_window_length] = {
    0x51, 0xe8, 0x2a, 0x95, 0x06, 0x00, 0xe8, 0xe5, 0xa4, 0x06, 0x00}; // push ecx; call 0x4f44a0; call 0x4f5460
constexpr unsigned char expected_materials_callee[materials_callee_length] = {0x6a, 0xff, 0x68, 0x70, 0x03, 0x53,
                                                                             0x00, 0x64}; // the SEH prologue
constexpr std::uintptr_t style_window_va = 0x004f812d, style_write_va = 0x004f813b;
constexpr unsigned style_window_length = 18, style_write_offset = 14, style_write_length = 4;
constexpr unsigned char expected_style_window[style_window_length] = {
    0xbf, 0x01, 0x00, 0x00, 0x00,             // mov edi,1
    0x39, 0xb8, 0x84, 0x07, 0x00, 0x00,       // cmp [eax+0x784],edi
    0x7e, 0x05,                               // jle +5
    0xbf, 0x02, 0x00, 0x00, 0x00};            // mov edi,2         <- the imm32 at +14
constexpr std::uintptr_t blt_block_va = 0x0048c090, blt_alpha_va = 0x0048c460;
constexpr unsigned blit_site_length = 7, blit_window_length = 19;
constexpr unsigned char expected_blit_window[blit_window_length] = {
    0x8b, 0x4c, 0x24, 0x08, 0x83, 0xec, 0x24, // mov ecx,[esp+8]; sub esp,0x24                    <- the claim
    0x85, 0xc9, 0x53, 0x55, 0x56, 0x57, 0x0f, 0x8d, 0x9a, 0x01, 0x00, 0x00}; // test ecx,ecx; push x4; jge +0x19a
// The reads the blit makes of the config field and the row flags (the same function, deeper in).
constexpr std::uintptr_t config_read_va = 0x0048c29a, row_flag_test_va = 0x0048c28b, row_count_read_va = 0x0048c265;
constexpr unsigned config_read_length = 12, row_flag_test_length = 15, row_count_read_length = 7;
constexpr unsigned char expected_config_read[config_read_length] = {0x8b, 0x15, 0x34, 0x6f, 0x60, 0x00,
                                                                    0x8b, 0xb2, 0x84, 0x07, 0x00, 0x00};
constexpr unsigned char expected_row_flag_test[row_flag_test_length] = {0xa1, 0xb0, 0x8d, 0x60, 0x00, 0xf7, 0x44, 0xb0,
                                                                        0x10, 0x00, 0x00, 0x01, 0x00, 0x74, 0x1e};
constexpr unsigned char expected_row_count_read[row_count_read_length] = {0x0f, 0xbf, 0x35, 0xac, 0x8d, 0x60, 0x00};
// A config-flag test in the frame function (`mov eax,[0x00606f34]; test [eax+4],0x100`): compared as one more anchor of
// the config slot; it is not a texture-entry read (the entry flags are read by the texture creator 0x004f4160).
constexpr std::uintptr_t config_flag_test_va = 0x00472315;
constexpr unsigned config_flag_test_length = 12;
constexpr unsigned char expected_config_flag_test[config_flag_test_length] = {0xa1, 0x34, 0x6f, 0x60, 0x00,               // mov eax,[0x606f34]
                                                                              0xf7, 0x40, 0x04, 0x00, 0x01, 0x00, 0x00}; // test [eax+4],0x100
// Helpers: the whole lookup function, the allocator's prologue and field stores, the free's prologue, the whole
// plain-copy wrapper, the colour and alpha leaves' prologues.
constexpr unsigned lookup_length = 104;
constexpr unsigned char expected_lookup[lookup_length] = {
    0x66, 0x85, 0xc9, 0x56, 0x7c, 0x5e, 0x66, 0x3b, 0x0d, 0xac, 0x8d, 0x60, 0x00, 0x7d, 0x17, 0x0f, 0xbf, 0xc1, 0x8b,
    0xd0, 0xc1, 0xe2, 0x04, 0x2b, 0xd0, 0xa1, 0xb0, 0x8d, 0x60, 0x00, 0x66, 0x83, 0x7c, 0x90, 0x0c, 0x00, 0x74, 0x3e,
    0x0f, 0xbf, 0x15, 0xb0, 0x69, 0x60, 0x00, 0x0f, 0xbf, 0xc1, 0x0f, 0xbf, 0x0d, 0xb4, 0x69, 0x60, 0x00, 0x03, 0xca,
    0x3b, 0xc1, 0x7d, 0x27, 0x8b, 0x0d, 0xac, 0x69, 0x60, 0x00, 0x8b, 0xf0, 0xc1, 0xe6, 0x04, 0x83, 0x7c, 0x0e, 0x08,
    0x00, 0x75, 0x09, 0xe8, 0xfc, 0xef, 0xff, 0xff, 0x85, 0xc0, 0x74, 0x0c, 0x8b, 0x15, 0xac, 0x69, 0x60, 0x00, 0x8b,
    0x44, 0x16, 0x08, 0x5e, 0xc3, 0x33, 0xc0, 0x5e, 0xc3};
constexpr unsigned alloc_length = 16, alloc_fields_length = 11, free_length = 16, copy_length = 49,
                   colour_copy_length = 14, alpha_leaf_length = 9;
constexpr std::uintptr_t alloc_fields_va = 0x004f3a11;
constexpr unsigned char expected_alloc[alloc_length] = {0x83, 0xec, 0x08, 0x53, 0x55, 0x8b, 0x6c, 0x24,
                                                        0x1c, 0x56, 0x57, 0xbb, 0x64, 0x00, 0x00, 0x00};
constexpr unsigned char expected_alloc_fields[alloc_fields_length] = {0x89, 0x6e, 0x10, 0x66, 0x89, 0x46,
                                                                      0x04, 0x66, 0x89, 0x4e, 0x06}; // +0x10, +4, +6
constexpr unsigned char expected_free[free_length] = {0x53, 0x55, 0x8b, 0x6c, 0x24, 0x0c, 0x8b, 0x45,
                                                      0x00, 0x85, 0xc0, 0x56, 0x57, 0x75, 0x08, 0x83};
constexpr unsigned char expected_copy[copy_length] = {
    0x8b, 0x44, 0x24, 0x20, 0x8b, 0x4c, 0x24, 0x1c, 0x8b, 0x54, 0x24, 0x18, 0x57, 0x8b, 0x7c, 0x24, 0x08,
    0x50, 0x8b, 0x44, 0x24, 0x1c, 0x51, 0x8b, 0x4c, 0x24, 0x1c, 0x52, 0x8b, 0x54, 0x24, 0x1c, 0x50, 0x8b,
    0x44, 0x24, 0x1c, 0x51, 0x52, 0xe8, 0x44, 0x2c, 0x01, 0x00, 0x83, 0xc4, 0x18, 0x5f, 0xc3};
constexpr unsigned char expected_colour_copy[colour_copy_length] = {0x53, 0x8b, 0x5c, 0x24, 0x20, 0x55, 0x8b,
                                                                    0x6c, 0x24, 0x20, 0x56, 0x8b, 0xf0, 0x57};
constexpr unsigned char expected_alpha_leaf[alpha_leaf_length] = {0x56, 0x8b, 0xf0, 0x57, 0xe8, 0xa7, 0xd3, 0xfe, 0xff};
// The diagnostic entries (--debug only): the text line 0x0048b2d0 (dst = arg0, font slot = arg3) and the rect fill
// 0x0048b0b0 (dst = arg0).
constexpr std::uintptr_t text_line_va = 0x0048b2d0, rect_fill_va = 0x0048b0b0;
constexpr unsigned text_line_site_length = 6, text_line_window_length = 21, rect_fill_site_length = 8,
                   rect_fill_window_length = 19;
constexpr unsigned char expected_text_line_window[text_line_window_length] = {
    0x83, 0xec, 0x14, 0x53, 0x55, 0x56,                                  // sub esp,0x14; push ebx; push ebp; push esi
    0x8b, 0x74, 0x24, 0x24, 0x33, 0xed, 0x3b, 0xf5, 0x57, 0x0f, 0x8d, 0xf7, 0x00, 0x00, 0x00};
constexpr unsigned char expected_rect_fill_window[rect_fill_window_length] = {
    0x83, 0xec, 0x1c, 0x53, 0x8b, 0x5c, 0x24, 0x24,                      // sub esp,0x1c; push ebx; mov ebx,[esp+0x24]
    0x85, 0xdb, 0x55, 0x56, 0x57, 0x0f, 0x8d, 0x5a, 0x01, 0x00, 0x00};
// The order proof (documentation and the verifier; the DLL compares the constructor's two stores): the config
// constructor call, Direct3DCreate9 and the Materials load in the init routine 0x00402780, the first font open in the
// main loop.
constexpr std::uintptr_t init_routine_va = 0x00402780, init_routine_end_va = 0x0040383d, config_ctor_call_va = 0x0040283a,
                         config_ctor_va = 0x004ec9e0, config_store_va = 0x00402844, d3d_create_call_va = 0x00402edc,
                         main_loop_call_va = 0x0040373a, main_loop_va = 0x00403840, cockpit_init_call_va = 0x00403a26,
                         cockpit_init_va = 0x0041c960, config_default_store_va = 0x004ecae3, config_atol_store_va = 0x004ecff8;
constexpr unsigned config_store_length = 6;
constexpr unsigned char expected_config_default_store[config_store_length] = {0x89, 0x88, 0x84, 0x07, 0x00, 0x00};
constexpr unsigned char expected_config_atol_store[config_store_length] = {0x89, 0x81, 0x84, 0x07, 0x00, 0x00};

// ---- the option ----
constexpr unsigned setting_capacity = 8, density_min = 1, density_max = 3;
enum class Parse : unsigned char { automatic = 0, value = 1, invalid = 2 };
// X3M_TEXT_DENSITY: unset or empty = auto (nothing changes without a UI scale); "auto"; "1", "2", "3"; else invalid.
template <class Char> inline Parse parse_setting(const Char* text, unsigned* value) {
    *value = 1;
    if (!text || !text[0]) return Parse::automatic;
    if (text[0] == Char('a') && text[1] == Char('u') && text[2] == Char('t') && text[3] == Char('o') && !text[4])
        return Parse::automatic;
    if (text[0] >= Char('1') && text[0] <= Char('3') && !text[1]) {
        *value = unsigned(text[0] - Char('0'));
        return Parse::value;
    }
    return Parse::invalid;
}
// auto: 1 when s <= 1 (no UI scale), else the smallest integer >= s, clamped to 3; a value stands.
inline unsigned density_for(Parse mode, unsigned value, double s) {
    if (mode == Parse::value) return value < density_min ? density_min : value > density_max ? density_max : value;
    if (!(s > 1.0)) return 1;
    unsigned d = unsigned(s);
    if (double(d) < s) ++d;
    return d > density_max ? density_max : d;
}
// The loose font pair for a request (name, size) at density d: `f\<name><size*d>.abc` and `.tga` under the game
// directory (the engine's "%s%d" name and its `F\%s` path; the resolver takes a loose file before any catalogue).
// False when the name is empty, over 32 characters, not printable ASCII, or the size is not in 1..255.
constexpr unsigned font_name_capacity = 32, font_path_capacity = 48;
inline bool font_file(const char* name, unsigned size, unsigned d, const char* extension, char out[font_path_capacity]) {
    if (!name || !name[0] || size < 1 || size > 255 || d < 1 || d > density_max) return false;
    unsigned n = 0;
    while (name[n]) {
        const unsigned char c = static_cast<unsigned char>(name[n]);
        if (c < 0x21 || c > 0x7e || c == '\\' || c == '/' || c == ':') return false;
        if (++n > font_name_capacity) return false;
    }
    char digits[8];
    unsigned v = size * d, k = 0;
    do {
        digits[k++] = char('0' + v % 10);
        v /= 10;
    } while (v);
    unsigned o = 0;
    out[o++] = 'f';
    out[o++] = '\\';
    for (unsigned i = 0; i < n; ++i) out[o++] = name[i];
    while (k) out[o++] = digits[--k];
    for (const char* e = extension; *e; ++e) out[o++] = *e;
    out[o] = 0;
    return o < font_path_capacity;
}
// The four font families the game opens (types/Fonts: HUD Tahoma 13 in every language, LARGE Zekton 26, ZektonES 26
// for -L034, Harrier 24 for -L007). The install ships all four pairs per density, and the density is applied only
// when every pair is present: a missing LARGE font would otherwise open at 1x into d x textures (1/d size).
struct FontFamily {
    const char* name;
    unsigned size;
};
constexpr FontFamily font_families[4] = {{"Tahoma", 13}, {"Zekton", 26}, {"ZektonES", 26}, {"Harrier", 24}};
constexpr unsigned missing_list_capacity = 64;
// The pairs missing at density d through `exists(relative path)`; the names (`Tahoma26,Harrier48`) go to `out`.
template <class Exists> inline unsigned missing_fonts(unsigned d, Exists exists, char out[missing_list_capacity]) {
    unsigned missing = 0, o = 0;
    out[0] = '-';
    out[1] = 0;
    for (const FontFamily& f : font_families) {
        char abc[font_path_capacity] = "", tga[font_path_capacity] = "";
        const bool present = font_file(f.name, f.size, d, ".abc", abc) && font_file(f.name, f.size, d, ".tga", tga) && exists(abc) && exists(tga);
        if (present) continue;
        ++missing;
        if (o) out[o++] = ',';
        for (const char* p = abc + 2; *p && *p != '.'; ++p) out[o++] = *p; // the name and size without `f\` and `.abc`
        out[o] = 0;
    }
    return missing;
}
// The largest density d' <= d at which the largest flagged row (largest_w x largest_h at 1x) still fits the device's
// D3DCAPS9 MaxTextureWidth/Height (0 = no limit known); 1 when even 2x does not fit.
inline unsigned caps_density(unsigned d, unsigned largest_w, unsigned largest_h, unsigned max_w, unsigned max_h) {
    for (unsigned k = d; k > 1; --k)
        if ((!max_w || largest_w * k <= max_w) && (!max_h || largest_h * k <= max_h)) return k;
    return 1;
}
// ---- the Materials row plan (item 4) ----
enum class RowEdit : unsigned char { none = 0, flag = 1, unflag = 2 };
struct RowPlan {
    RowEdit edit;
    std::uint32_t flags; // the row's new MPF_ flags (the table entry gets the same bits changed)
};
// Generated writeable rows (the text targets, 35 in the shipped table) gain MPF_FONTSCALE and, when the texture will
// be minified (s != d), lose MPF_NOFILTERING; file-backed MPF_FONTSCALE rows (8) lose the flag so 0x004f4160 loads
// their stock files instead of the missing `tex\true\1<id>` variants.
inline RowPlan plan_row(std::uint32_t flags, bool clear_nofiltering) {
    if ((flags & mpf_generated) && (flags & mpf_writeable)) {
        std::uint32_t f = flags | mpf_fontscale;
        if (clear_nofiltering) f &= ~mpf_nofiltering;
        return RowPlan{f == flags ? RowEdit::none : RowEdit::flag, f};
    }
    if ((flags & mpf_fontscale) && !(flags & mpf_generated)) return RowPlan{RowEdit::unflag, flags & ~mpf_fontscale};
    return RowPlan{RowEdit::none, flags};
}
// ---- the blits (item 5) ----
// Both entries are cdecl with ten dword arguments; the thunk hands the C side the pushad/pushfd frame: [0..7] EDI,
// ESI, EBP, ESP, EBX, EDX, ECX, EAX, [8] EFLAGS, [9] the return address, [10..19] the arguments.
constexpr unsigned frame_eax = 7, frame_ecx = 6, frame_edx = 5, frame_return = 9, frame_args = 10;
enum BlitArg : unsigned { arg_src = 0, arg_dst, arg_sx, arg_sy, arg_dx, arg_dy, arg_w, arg_h, arg_colour, arg_clip };
// The font open's frame: EAX = name, ECX = size, [args+0] cell width, [args+1] flags, [args+2] y offset.
constexpr unsigned font_arg_cell_width = 0, font_arg_flags = 1, font_arg_y_offset = 2;
// 0x0048c090's destination clip (0x0048c2c8..0x0048c32c), in layout units against the flagged destination's
// W/N x H/N: false when nothing remains (the engine then skips the leaf). Applied only when the clip argument is set.
// Reproduced as the engine does it, quirk included: a rectangle that starts left of (above) 0 is moved to 0 and
// shortened by the overhang but not clipped against the far edge.
inline bool clip_destination(int W, int H, int* sx, int* sy, int* dx, int* dy, int* w, int* h) {
    bool ok = true;
    if (*dx < W) {
        const int right = *dx + *w;
        if (right > 0) {
            if (*dx < 0) {
                *sx -= *dx;
                *w = right;
                *dx = 0;
            } else if (right > W) {
                *w = W - *dx;
            }
        } else {
            ok = false;
        }
    } else {
        ok = false;
    }
    if (*dy < H) {
        const int bottom = *dy + *h;
        if (bottom > 0) {
            if (*dy < 0) {
                *sy -= *dy;
                *h = bottom;
                *dy = 0;
            } else if (bottom > H) {
                *h = H - *dy;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }
    return ok;
}
// Shadow limits: one D3D-backed 32-bit surface of d*W x d*H per static source, at most `shadow_slots` sources and
// `shadow_budget_bytes` in total; a side above `shadow_max_side` texels is refused (the surface pitch is int16 and
// the D3D9 minimum guaranteed size is 2048; 4096 is what every device of the last 15 years supports).
constexpr unsigned shadow_slots = 16, shadow_max_side = 4096;
constexpr std::uint32_t shadow_budget_bytes = 96u << 20;
inline bool shadow_fits(unsigned w, unsigned h, unsigned d, std::uint32_t used, std::uint32_t* bytes) {
    if (!w || !h || d < 1 || d > density_max || w * d > shadow_max_side || h * d > shadow_max_side) return false;
    *bytes = std::uint32_t(w) * d * h * d * 4u;
    return used + *bytes <= shadow_budget_bytes;
}
// ---- the on-screen filter of the flagged text textures (run 392) ----
// The gui quads of flagged rows sample point-filtered on screen even after MPF_NOFILTERING left the rows
// (font-rendering.md section 5: the quad's filter comes from the material flags the body copied at its load, or the
// effect's own choice; not traced to one clean site). Under s != d the proxy forces LINEAR min/mag on every draw whose
// stage-0 texture is the D3D texture of a flagged row: the engine object keeps its IDirect3DTexture9 at +0x34 (the
// level-0 surface at +0x30; 0x004dcc59 / 0x004dd1c5). The set is a sorted array rebuilt once per Present.
constexpr unsigned object_surface_offset = 0x30, object_texture_offset = 0x34;
constexpr unsigned filter_set_capacity = 64; // 35 flagged rows in the shipped table
struct FilterSet {
    std::uintptr_t textures[filter_set_capacity];
    unsigned count;
};
inline bool filter_set_insert(FilterSet* s, std::uintptr_t p) {
    if (!p || s->count >= filter_set_capacity) return false;
    unsigned i = 0;
    while (i < s->count && s->textures[i] < p) ++i;
    if (i < s->count && s->textures[i] == p) return true;
    for (unsigned k = s->count; k > i; --k) s->textures[k] = s->textures[k - 1];
    s->textures[i] = p;
    ++s->count;
    return true;
}
inline bool filter_set_contains(const FilterSet& s, std::uintptr_t p) {
    unsigned lo = 0, hi = s.count;
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        if (s.textures[mid] == p) return true;
        if (s.textures[mid] < p) lo = mid + 1;
        else hi = mid;
    }
    return false;
}
constexpr std::uint32_t d3d_texf_none = 0, d3d_texf_point = 1, d3d_texf_linear = 2, d3d_texf_anisotropic = 3; // D3DTEXTUREFILTERTYPE
// Only a NONE or POINT filter is raised to LINEAR; LINEAR and ANISOTROPIC (run 393: MIN 3, MAG 2 on the text quads)
// are left as the engine set them, per axis.
inline bool filter_needs_raise(std::uint32_t value) {
    return value <= d3d_texf_point;
}
// ---- B3D_TexBltImage (item 4b): KC native 109 of the script dispatcher 0x00493b40 ----
// The case 0x00495fc1 resolves the image path at [esi+5], keeps one image object (*0x006085cc, loaded by 0x004f3510;
// its path at *0x006085c8, so a repeated blit of the same image reuses the object), looks the destination row up and,
// from 0x0049608b, sets cx = 1 or cfg+0x784 when the destination row has MPF_FONTSCALE. The block at 0x004960c0 then
// loads the six record words sx [esi+0xb], sy [esi+0x10], dx [esi+0x15], dy [esi+0x1a], w [esi+0x1f], h [esi+0x24],
// multiplies ALL of them by cx (16-bit imul: the source rectangle too, whatever the source is) and calls the plain copy
// 0x004dbbe0(src = ebx = the image object, dst = [esp+0x10], sx, sy, dx, dy, w, h). An unflagged 1x image is then read
// d x out of bounds (the New Game menu crash: read past the image buffer in the copy leaf's memcpy). The claim takes
// the two loads at 0x004960c0 (8 bytes, the target of both branches `jge`/`je 0x004960c0`): EAX and EDX are written by
// the displaced instructions, EFLAGS by the `imul` right after them, so the thunk returns the C result in EAX; handled
// means the C side ran the leaf itself (the d x shadow of the image with every coordinate x d, or the source rectangle
// unscaled, always clamped to both surfaces) and the thunk skips the engine's block (`sub esp,0x20` for the eight
// arguments the engine would have pushed, then 0x00496101 where `mov cx,[esi+1]; add esp,0x20; call lookup` follow:
// EAX and EFLAGS dead). Zero continues into the engine's own block.
constexpr std::uintptr_t image_case_va = 0x00495fc1, image_scale_va = 0x0049608b, blt_image_site_va = 0x004960c0,
                         image_return_va = 0x00496101, image_call_va = 0x004960fc, image_object_slot_va = 0x006085cc,
                         image_path_slot_va = 0x006085c8, image_jump_table_va = 0x0049679c, image_table_entry_va = 0x00496950;
constexpr unsigned image_opcode = 109, image_site_length = 8, image_pushed_bytes = 0x20;
constexpr unsigned image_scale_window_length = 53, image_window_length = 82;
constexpr unsigned char expected_image_scale_window[image_scale_window_length] = {
    0x0f, 0xbf, 0x05, 0xac, 0x8d, 0x60, 0x00,             // movsx eax,word [0x608dac]   rows
    0x8b, 0x56, 0x01,                                     // mov edx,[esi+1]             dst id
    0x3b, 0xd0,                                           // cmp edx,eax
    0xb9, 0x01, 0x00, 0x00, 0x00,                         // mov ecx,1
    0x7d, 0x22,                                           // jge 0x4960c0
    0x8b, 0xc2, 0xc1, 0xe0, 0x04, 0x2b, 0xc2,             // eax = id*15
    0x8b, 0x15, 0xb0, 0x8d, 0x60, 0x00,                   // mov edx,[0x608db0]
    0xf7, 0x44, 0x82, 0x10, 0x00, 0x00, 0x01, 0x00,       // test [edx+eax*4+0x10],MPF_FONTSCALE
    0x74, 0x0b,                                           // je 0x4960c0
    0xa1, 0x34, 0x6f, 0x60, 0x00,                         // mov eax,[0x606f34]
    0x8b, 0x88, 0x84, 0x07, 0x00, 0x00};                  // mov ecx,[eax+0x784]
constexpr unsigned char expected_image_window[image_window_length] = {
    0x0f, 0xb7, 0x56, 0x24, 0x0f, 0xb7, 0x46, 0x1f,                                     // movzx edx,[esi+0x24]; movzx eax,[esi+0x1f] <- the claim
    0x66, 0x0f, 0xaf, 0xd1, 0x66, 0x0f, 0xaf, 0xc1, 0x52,                               // imul dx,cx; imul ax,cx; push edx (h)
    0x0f, 0xb7, 0x56, 0x1a, 0x66, 0x0f, 0xaf, 0xd1, 0x50,                               // dy; push eax (w)
    0x0f, 0xb7, 0x46, 0x15, 0x66, 0x0f, 0xaf, 0xc1, 0x52,                               // dx; push edx (dy)
    0x0f, 0xb7, 0x56, 0x10, 0x66, 0x0f, 0xaf, 0xd1, 0x50,                               // sy; push eax (dx)
    0x0f, 0xb7, 0x46, 0x0b, 0x66, 0x0f, 0xaf, 0xc1,                                     // sx
    0x8b, 0x4c, 0x24, 0x20, 0x52, 0x50, 0x51, 0x53,                                     // mov ecx,[esp+0x20] (dst); push sy, sx, dst, src
    0xe8, 0xdf, 0x5a, 0x04, 0x00,                                                       // call 0x4dbbe0
    0x66, 0x8b, 0x4e, 0x01, 0x83, 0xc4, 0x20, 0xe8, 0x03, 0xf0, 0x05, 0x00,             // mov cx,[esi+1]; add esp,0x20; call 0x4f5110
    0xe9, 0x20, 0xdb, 0xff, 0xff};                                                      // jmp 0x493c32
// The operand record (esi): the destination id and the six 16-bit coordinates.
constexpr unsigned image_record_dst_offset = 1, image_record_coords_offset = 0xb, image_record_coord_stride = 5,
                   image_record_length = 0x26;
// The image site's frame: no return address ([9] is the engine's [esp] at entry), the destination object at the
// engine's [esp+0x10]; ECX (frame_ecx) = the engine's cx (1 or N), EBX (frame_ebx) = the image object, ESI the record.
constexpr unsigned frame_esi = 1, frame_ebx = 4, image_frame_dst = 13;
constexpr unsigned image_path_capacity = 128;
// Surface formats. The allocator 0x004f3950 derives the depth bit of +0x10 from the bits-per-pixel argument when the
// flags carry none (0x004f39b6..0x004f3a07: 16 -> 0x100, 24 -> 0x200, 32 -> 0x2000), so a shadow allocated with
// (32, 0x402c) holds 0x602c, like every generated 32-bit row, and KC's image loader gives the image the display depth
// (0x100 / 0x200 / 0x2000, case 0x00495fff..0x00496045). The plain copy leaf 0x004ee850 classifies the (source,
// destination) pair through 0x004ee6e0 into 25 classes and copies only the four same-depth pairs (class 0 none/none
// -> 0x004b1d80, 5 16/16 -> 0x004b1e90, 10 24/24 -> 0x004b1fb0, 24 32/32 -> 0x004b20d0); every other class, and
// any surface without bit 0x4 (pixels), falls through to 0x004ee916 and copies nothing. The image shadow is therefore
// built only from a 32-bit image with pixels (the shadow's own class, the class the engine's own image -> row copy
// uses on a 32-bit display); any other image takes the unscaled-source fallback, which hands the engine's leaf the
// same pair the engine would have (nothing is copied, as in the stock game).
constexpr std::uint32_t surface_has_pixels = 0x4, surface_depth_16 = 0x100, surface_depth_24 = 0x200, surface_depth_32 = 0x2000,
                        surface_depth_mask = surface_depth_16 | surface_depth_24 | surface_depth_32;
constexpr bool image_shadow_compatible(std::uint32_t flags) {
    return (flags & surface_has_pixels) != 0 && (flags & surface_depth_mask) == surface_depth_32;
}
constexpr std::uintptr_t alloc_depth_va = 0x004f39b6, copy_dispatch_va = 0x004ee866, copy_class_table_va = 0x004ee934,
                         copy_slot_table_va = 0x004ee91c, copy_nothing_va = 0x004ee916, copy_classifier_va = 0x004ee6e0;
constexpr unsigned alloc_depth_length = 81, copy_dispatch_length = 44, copy_classes = 25, copy_slots = 6;
constexpr unsigned char expected_alloc_depth[alloc_depth_length] = {
    0xf7, 0xc5, 0x00, 0x01, 0x00, 0x00, 0x74, 0x05, 0xbf, 0x10, 0x00, 0x00, 0x00, // test ebp,0x100; je; mov edi,16
    0xf7, 0xc5, 0x00, 0x02, 0x00, 0x00, 0x74, 0x05, 0xbf, 0x18, 0x00, 0x00, 0x00, // test ebp,0x200; je; mov edi,24
    0xf7, 0xc5, 0x00, 0x20, 0x00, 0x00, 0x74, 0x07, 0xbf, 0x20, 0x00, 0x00, 0x00, 0xeb, 0x22, // test ebp,0x2000; je; mov edi,32; jmp
    0x66, 0x83, 0xff, 0x10, 0x75, 0x08, 0x81, 0xcd, 0x00, 0x01, 0x00, 0x00, 0xeb, 0x1a, // cmp di,16; jne; or ebp,0x100; jmp
    0x66, 0x83, 0xff, 0x18, 0x75, 0x08, 0x81, 0xcd, 0x00, 0x02, 0x00, 0x00, 0xeb, 0x0c, // cmp di,24; jne; or ebp,0x200; jmp
    0x66, 0x83, 0xff, 0x20, 0x75, 0x06, 0x81, 0xcd, 0x00, 0x20, 0x00, 0x00};           // cmp di,32; jne; or ebp,0x2000
constexpr unsigned char expected_copy_dispatch[copy_dispatch_length] = {
    0x66, 0x85, 0xc0, 0x0f, 0x84, 0xa7, 0x00, 0x00, 0x00, // test ax,ax; je 0x4ee916 (the surface lock failed)
    0x8b, 0xce, 0x8b, 0xc7, 0xe8, 0x68, 0xfe, 0xff, 0xff, // mov ecx,esi (dst); mov eax,edi (src); call 0x4ee6e0
    0x0f, 0xbf, 0xc0, 0x83, 0xf8, 0x18, 0x0f, 0x87, 0x92, 0x00, 0x00, 0x00, // movsx eax,ax; cmp eax,0x18; ja 0x4ee916
    0x0f, 0xb6, 0x80, 0x34, 0xe9, 0x4e, 0x00,             // movzx eax,byte [eax+0x4ee934]
    0xff, 0x24, 0x85, 0x1c, 0xe9, 0x4e, 0x00};            // jmp [eax*4+0x4ee91c]
constexpr unsigned char expected_copy_class_table[copy_classes] = {0x00, 0x05, 0x05, 0x05, 0x05, 0x01, 0x05, 0x05, 0x05, 0x05, 0x02, 0x05, 0x05,
                                                                   0x05, 0x05, 0x03, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x04};
constexpr unsigned copy_class_none = 0, copy_class_16 = 5, copy_class_24 = 10, copy_class_32 = 24;
// The plan for one TexBltImage into a destination of DW x DH texels from a 1x image of SW x SH texels, given the
// engine's factor N (1, or cfg+0x784 = d for a flagged destination), the raw record words and whether the d x shadow
// of the image exists: with the shadow (N = d) every coordinate is x N against the shadow (what the engine does for a
// flagged source); otherwise the source rectangle stays unscaled and only the destination position is x N (the icon
// draws 1x, never out of bounds). The rectangle is then clamped to both surfaces; draw = false when nothing remains.
struct ImagePlan {
    bool draw, shadow;
    int sx, sy, dx, dy, w, h;
};
inline bool clamp_copy(int SW, int SH, int DW, int DH, int* sx, int* sy, int* dx, int* dy, int* w, int* h) {
    if (*sx < 0 || *sy < 0 || *dx < 0 || *dy < 0 || *w <= 0 || *h <= 0) return false;
    if (*sx >= SW || *sy >= SH || *dx >= DW || *dy >= DH) return false;
    if (*w > SW - *sx) *w = SW - *sx;
    if (*w > DW - *dx) *w = DW - *dx;
    if (*h > SH - *sy) *h = SH - *sy;
    if (*h > DH - *dy) *h = DH - *dy;
    return *w > 0 && *h > 0;
}
inline ImagePlan plan_image_blit(unsigned N, unsigned d, bool have_shadow, int SW, int SH, int DW, int DH, const int raw[6]) {
    ImagePlan p{};
    p.shadow = N > 1 && N == d && have_shadow;
    const int n = int(N);
    if (p.shadow) {
        p.sx = raw[0] * n;
        p.sy = raw[1] * n;
        p.w = raw[4] * n;
        p.h = raw[5] * n;
        SW *= n;
        SH *= n;
    } else {
        p.sx = raw[0];
        p.sy = raw[1];
        p.w = raw[4];
        p.h = raw[5];
    }
    p.dx = raw[2] * n;
    p.dy = raw[3] * n;
    p.draw = SW > 0 && SH > 0 && DW > 0 && DH > 0 && clamp_copy(SW, SH, DW, DH, &p.sx, &p.sy, &p.dx, &p.dy, &p.w, &p.h);
    return p;
}
// ---- the diagnostic set (item 7) ----
// The function ids the thunks push (the diagnostic rows name them in this order).
enum Kind : unsigned { kind_text_line = 0, kind_blt_block = 1, kind_blt_alpha = 2, kind_rect_fill = 3, kind_blt_image = 4 };
constexpr unsigned diagnostic_rows = 96;
struct DiagnosticKey {
    std::uint8_t function; // 0 text line, 1 blit block, 2 blit alpha, 3 rect fill
    std::int32_t src, dst;
};
// True when the key was not in the table yet (and fits): the caller prints the row once.
inline bool diagnostic_first(DiagnosticKey* table, unsigned* count, DiagnosticKey key) {
    for (unsigned i = 0; i < *count; ++i)
        if (table[i].function == key.function && table[i].src == key.src && table[i].dst == key.dst) return false;
    if (*count >= diagnostic_rows) return false;
    table[(*count)++] = key;
    return true;
}
static_assert(style_window_va + style_write_offset == style_write_va, "the imm32 sits at +14 of the style window");
static_assert(expected_style_window[style_write_offset - 1] == 0xbf && expected_style_window[style_write_offset] == 2,
              "mov edi,2");
static_assert((style_write_va & 7u) + style_write_length <= 8u, "the imm32 lies inside one aligned qword");
static_assert((font_site_va & 7u) + 5 <= 8u && (blt_block_va & 7u) + 5 <= 8u && (blt_alpha_va & 7u) + 5 <= 8u &&
                  (materials_site_va & 7u) + 5 <= 8u && (text_line_va & 7u) + 5 <= 8u && (rect_fill_va & 7u) + 5 <= 8u &&
                  (blt_image_site_va & 7u) + 5 <= 8u,
              "every five-byte patch lies inside one aligned qword (one lock cmpxchg8b)");
static_assert(font_site_va + font_site_length == font_return_va && materials_site_va + 5 == materials_return_va,
              "continuations");
static_assert(image_scale_va + image_scale_window_length == blt_image_site_va && blt_image_site_va + 0x41 == image_return_va &&
                  blt_image_site_va + 0x3c == image_call_va && image_site_length <= image_window_length &&
                  expected_image_window[0x3c] == 0xe8 && expected_image_window[0x41] == 0x66 && expected_image_window[0x45] == 0x83 &&
                  expected_image_window[0x46] == 0xc4 && expected_image_window[0x47] == image_pushed_bytes,
              "the image block: the call at +0x3c, `mov cx,[esi+1]; add esp,0x20` at the return address");
static_assert(image_jump_table_va + image_opcode * 4 == image_table_entry_va, "KC native 109");
static_assert(expected_copy_class_table[copy_class_none] == 0 && expected_copy_class_table[copy_class_16] == 1 &&
                  expected_copy_class_table[copy_class_24] == 2 && expected_copy_class_table[copy_class_32] == 4 &&
                  expected_copy_dispatch[23] == copy_classes - 1 && (copy_dispatch_va + 9 + 4 + 5 + 3 + 3 + 6) + 0x92 == copy_nothing_va,
              "the four same-depth classes own the four copy slots; 25 classes; the rest fall through");
static_assert(image_shadow_compatible(generated_surface_flags | surface_depth_32) && !image_shadow_compatible(generated_surface_flags) &&
                  !image_shadow_compatible(surface_has_pixels | surface_depth_16) && !image_shadow_compatible(surface_depth_32),
              "a 32-bit surface with pixels is the shadow's class");
static_assert(expected_image_window[3] == image_record_coords_offset + 5 * image_record_coord_stride &&
                  expected_image_window[7] == image_record_coords_offset + 4 * image_record_coord_stride &&
                  image_record_coords_offset + 5 * image_record_coord_stride + 2 == image_record_length,
              "the record words h and w are the displaced loads");
static_assert(expected_materials_window[1] == 0xe8 &&
                  materials_return_va + (std::uint32_t(expected_materials_window[2]) | std::uint32_t(expected_materials_window[3]) << 8 |
                                         std::uint32_t(expected_materials_window[4]) << 16 |
                                         std::uint32_t(expected_materials_window[5]) << 24) == materials_target_va,
              "CALL 0x004f44a0");
static_assert(expected_config_read[2] == (config_slot_va & 0xff) && expected_config_read[3] == ((config_slot_va >> 8) & 0xff) &&
                  expected_config_read[8] == (config_density_offset & 0xff) && expected_config_read[9] == (config_density_offset >> 8),
              "the blit reads [*0x00606f34+0x784]");
static_assert(expected_row_flag_test[1] == (rows_slot_va & 0xff) && expected_row_flag_test[8] == row_flags_offset &&
                  expected_row_flag_test[11] == (mpf_fontscale >> 16),
              "the blit tests row+0x10 & MPF_FONTSCALE");
}
