#pragma once
// The D3DX texture row's observer (docs/architecture/overlay-material-strength.md 4 and "Farnham's Legacy"): the
// created texture with the source bytes the engine passed, for the baker's X3ML atlas marks (lod_texture_mark.h).
//
// observe_created is the step both editions run after the original D3DXCreateTextureFromFileInMemoryEx returned:
// AP inside loading_trace's texture row (loading_trace.cpp, with its telemetry span), FL inside the import-slot stub
// below. install() is the FL path (d3dx_texture_hook.cpp, linked by the fl target only): it resolves the main
// module's D3DXCreateTextureFromFileInMemoryEx import by name from whichever d3dx9_NN.dll descriptor names it (no
// version is assumed; X3FL.exe names d3dx9_43.dll, verification/results/fl-material-strength/import_census_out.txt)
// and exchanges that one aligned IAT dword for a forwarding stub. Documented Win32 PE/loader contracts only (import
// descriptors, GetModuleHandle/GetProcAddress, VirtualProtect, an interlocked pointer exchange): portable to native
// Windows, no Wine-private prerequisite.
#include <windows.h>
#include <d3d9.h>

namespace x3m::d3dx_texture_hook {
using TextureObserver = void (*)(IDirect3DDevice9* device, const void* data, UINT size, IDirect3DTexture9* texture);

// After the original returned `result` with the thread's LastError `error`: the observer sees a created texture only
// (success, out and *out set); its own API calls cannot leak a LastError to the caller. Nothing else is touched.
inline void observe_created(TextureObserver observer, IDirect3DDevice9* device, const void* data, UINT size,
                            HRESULT result, IDirect3DTexture9** out, DWORD error) {
    if (observer && SUCCEEDED(result) && out && *out) {
        observer(device, data, size, *out);
        SetLastError(error);
    }
}

// FL: patches the import slot of `module` (the game's main module) once, inside the engine-patch install window.
// Returns "ok" when the slot holds the stub, else the refusal reason (nothing written, or the write rolled back):
// already_installed, no_observer, late_claim, bad_image, import_absent, ambiguous_import, slot_misaligned,
// dll_not_loaded, foreign_redirect, pin_failed, slot_not_writable, protect_restore_failed, foreign_chained (another
// hook wrote over the stub before a failed protection restore could undo it: the stub stays in that chain, forwarding
// only, observer detached). The caller logs one row.
// Preserves the thread's LastError. The DLL is pinned before the slot goes live, so the stub stays until process exit.
const char* install(HMODULE module, TextureObserver observer);
bool installed();       // the slot holds the stub (install returned ok and restore() has not run)
const char* dll_name(); // the resolved d3dx9_NN.dll descriptor name ("-" before resolution)
unsigned slot_rva();    // the slot's RVA in `module` (0 before resolution)
// Puts the original pointer back, only over our stub (a later redirect on top is left alone). The stub stays callable
// and keeps forwarding to the original (a thread may already be inside it); the observer is detached first.
// Production does not call it (the DLL is pinned); the host fixture proves the rollback with it.
bool restore();
#ifdef X3M_D3DX_TEXTURE_HOOK_FIXTURE
void fixture_fail_protection_restores(unsigned calls); // the next `calls` protection restores fail
void fixture_protection_restore_probe(void (*probe)(PVOID* slot)); // called inside each failing restore
void fixture_set_original(void* original);              // forward the stub to `original` (CPU-state probe)
void* fixture_original();
void fixture_reset(); // back to the uninstalled state (after restore(); the window state is the caller's)
#endif
} // namespace x3m::d3dx_texture_hook
