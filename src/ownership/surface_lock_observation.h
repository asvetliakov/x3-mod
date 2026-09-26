#pragma once
#include <d3d9.h>

// Surface lock witness: one process-wide observer consulted by the generated
// Surface::LockRect / Surface::UnlockRect shells (the application-visible
// IDirect3DSurface9). The shell takes its caller's return address at entry
// (__builtin_return_address(0), before any helper frame), loads the observer
// once and, when unset (the default), pays exactly that one relaxed load and
// branch before the ordinary forward. When set, the observer runs on the
// calling thread twice per call: before the native call (`Result` is S_FALSE,
// the call may never return) and after it with the native HRESULT, inside the
// shell's own envelope: the game's incoming x87/MXCSR/LastError state is
// restored before the native call and the native outgoing state after the
// second observer call, so the observer need not preserve them. `native` is
// borrowed for the duration of the observer call, for documented read-only
// queries such as GetDesc; the observer must not lock, unlock or release it
// and must not allocate. Documented D3D9 only; no Wine-private prerequisite.
namespace x3m::ownership {
enum class SurfaceLockPhase : unsigned char { LockEnter, LockResult, UnlockEnter, UnlockResult };
struct SurfaceLockEvent {
    IDirect3DSurface9* surface; // the application-visible surface (identity the caller holds)
    IDirect3DSurface9* native;  // borrowed for this call only
    const void* return_address; // the shell's caller
    const RECT* rect;           // LockRect phases only
    DWORD flags;                // LockRect phases only
    HRESULT result;             // *Result phases; S_FALSE on *Enter
    SurfaceLockPhase phase;
};
using SurfaceLockObserver = void (*)(const SurfaceLockEvent&);
// Publishes (or clears, with nullptr) the observer. Relaxed atomics: the
// caller orders publication against the first observed call itself.
void set_surface_lock_observer(SurfaceLockObserver observer) noexcept;
}
