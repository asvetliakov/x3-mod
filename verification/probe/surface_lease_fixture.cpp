// Actual canonical wrapper code, native SYSTEMMEM surface, per-instance COM
// spies. Reset backend is simulated; this is not native GPU Reset qualification.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace x3m::ownership;
namespace {
unsigned checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks; if (!value) ++failures;
    std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL");
}
void require(bool value, const char* label) {
    check(value, label);
    if (!value) ExitProcess(2);
}
struct Patch {
    void* object;
    void** saved;
    std::array<void*,119> table{};
    Patch(void* p, unsigned slots) : object(p), saved(*reinterpret_cast<void***>(p)) {
        std::memcpy(table.data(), saved, slots * sizeof(void*));
        *reinterpret_cast<void***>(p) = table.data();
    }
    void restore() {
        if (object) { *reinterpret_cast<void***>(object) = saved; object = nullptr; }
    }
    ~Patch() { restore(); }
};
struct Setup {
    HMODULE module = nullptr;
    HWND window = nullptr;
    IDirect3D9* factory = nullptr;
    IDirect3DDevice9* app = nullptr;
    IDirect3DDevice9* native = nullptr;
    D3DPRESENT_PARAMETERS pp{};
    Setup() {
        module = LoadLibraryA("d3d9.dll");
        require(module != nullptr, "load native d3d9");
        using Create = IDirect3D9*(WINAPI*)(UINT);
        Create create = nullptr;
        auto symbol = GetProcAddress(module, "Direct3DCreate9");
        std::memcpy(&create, &symbol, sizeof(create));
        require(create != nullptr, "Direct3DCreate9 export");
        auto* raw = create(D3D_SDK_VERSION);
        require(raw != nullptr, "native factory");
        require(wrap_factory(raw, &factory) == S_OK, "canonical factory");
        window = CreateWindowExA(0, "STATIC", "Surface lease fixture", WS_POPUP,
            0, 0, 32, 32, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
        require(window != nullptr, "fixture window");
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window; pp.BackBufferWidth = pp.BackBufferHeight = 32;
        require(factory->CreateDevice(0, D3DDEVTYPE_HAL, window,
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp, &app) == S_OK,
            "canonical device");
        native = borrowed_native_device(app);
        require(native != nullptr, "native device endpoint");
    }
    ~Setup() {
        if (app) app->Release();
        if (factory) factory->Release();
        if (window) DestroyWindow(window);
        if (module) FreeLibrary(module);
    }
};
using Ref = ULONG(WINAPI*)(IUnknown*);
struct SurfaceSpy {
    static inline SurfaceSpy* current = nullptr;
    Patch patch;
    Ref original_add, original_release;
    unsigned adds = 0, releases = 0;
    SurfaceLease* cleanup_lease = nullptr;
    IDirect3DDevice9* app_device = nullptr;
    IDirect3DSurface9* app_surface = nullptr;
    HANDLE entered = nullptr, finished = nullptr;
    bool cleanup_empty = false, reentry_refused = false, worker_completed = false;
    static ULONG WINAPI add(IUnknown* object) {
        auto& s = *current; ++s.adds; return s.original_add(object);
    }
    static ULONG WINAPI release(IUnknown* object) {
        auto& s = *current; ++s.releases;
        if (s.cleanup_lease) {
            s.cleanup_empty = s.cleanup_lease->get() == nullptr;
            s.cleanup_lease->reset(); // reentrant repeat must own no reference
            SurfaceLeaseIdentity identity;
            s.reentry_refused = snapshot_surface_identity(s.app_device, s.app_surface, &identity) == E_INVALIDARG;
            SetEvent(s.entered);
            s.worker_completed = WaitForSingleObject(s.finished, 3000) == WAIT_OBJECT_0;
        }
        // Backend may destroy itself: never restore through the dead object.
        s.patch.restore();
        return s.original_release(object);
    }
    explicit SurfaceSpy(IDirect3DSurface9* native) : patch(native, 17),
        original_add(reinterpret_cast<Ref>(patch.saved[1])),
        original_release(reinterpret_cast<Ref>(patch.saved[2])) {
        require(current == nullptr, "one native surface spy");
        current = this;
        patch.table[1] = reinterpret_cast<void*>(&add);
        patch.table[2] = reinterpret_cast<void*>(&release);
    }
    ~SurfaceSpy() { current = nullptr; }
};
using CreateSurface = HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,UINT,D3DFORMAT,D3DPOOL,IDirect3DSurface9**,HANDLE*);
CreateSurface create_surface_original = nullptr;
IDirect3DSurface9* created_native_surface = nullptr;
HRESULT WINAPI create_surface_spy(IDirect3DDevice9* device, UINT w, UINT h, D3DFORMAT format,
        D3DPOOL pool, IDirect3DSurface9** out, HANDLE* shared) {
    const auto hr = create_surface_original(device, w, h, format, pool, out, shared);
    if (SUCCEEDED(hr)) created_native_surface = *out; // borrowed while canonical wrapper lives
    return hr;
}
struct Surface {
    IDirect3DSurface9* app = nullptr;
    IDirect3DSurface9* native = nullptr;
    explicit Surface(Setup& s) {
        Patch patch(s.native, 119);
        create_surface_original = reinterpret_cast<CreateSurface>(patch.saved[36]);
        patch.table[36] = reinterpret_cast<void*>(&create_surface_spy);
        created_native_surface = nullptr;
        require(s.app->CreateOffscreenPlainSurface(8, 8, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
            &app, nullptr) == S_OK, "canonical SYSTEMMEM surface");
        native = created_native_surface;
        require(native && native != app, "native surface captured before adoption");
    }
    ~Surface() { if (app) app->Release(); }
};
void admission_and_lifetime() {
    Setup s, other;
    Surface surface(s);
    SurfaceSpy spy(surface.native);
    SurfaceLeaseIdentity identity, out;
    SurfaceLease lease, refused;
    require(snapshot_surface_identity(s.app, surface.app, &identity) == S_OK && identity.valid(), "live surface identity");
    check(acquire_surface_lease(s.app, surface.native, identity, refused) == E_INVALIDARG && !refused.get(), "raw native surface refused");
    check(acquire_surface_lease(s.native, surface.app, identity, refused) == E_INVALIDARG && !refused.get(), "raw native device refused");
    check(acquire_surface_lease(s.app, reinterpret_cast<IDirect3DSurface9*>(1), identity, refused) == E_INVALIDARG, "unreadable surface key refused");
    check(acquire_surface_lease(other.app, surface.app, identity, refused) == E_INVALIDARG, "wrong device refused");
    check(snapshot_surface_identity(s.app, reinterpret_cast<IDirect3DSurface9*>(s.app), &out) == E_INVALIDARG && !out.valid(), "wrong-kind surface key refused");
    check(acquire_surface_lease(s.app, surface.app, identity, lease) == S_OK && lease.get() == surface.app, "logical lease acquired");
    check(acquire_surface_lease(s.app, surface.app, identity, lease) == E_INVALIDARG && lease.get() == surface.app, "nonempty output refuses unchanged");
    check(spy.adds == 0 && spy.releases == 0, "acquire and refusals make no backend reference calls");
    check(surface.app->Release() == 1, "application release leaves sole lease reference");
    surface.app = nullptr;
    D3DSURFACE_DESC desc{};
    check(lease.get()->GetDesc(&desc) == S_OK && desc.Pool == D3DPOOL_SYSTEMMEM && desc.Width == 8, "borrowed leased surface remains callable");
    D3DLOCKED_RECT locked{};
    require(lease.get()->LockRect(&locked, nullptr, 0) == S_OK, "leased surface LockRect");
    *static_cast<DWORD*>(locked.pBits) = 0xff112233;
    check(lease.get()->UnlockRect() == S_OK, "leased surface UnlockRect");
    require(lease.get()->LockRect(&locked, nullptr, D3DLOCK_READONLY) == S_OK, "leased surface read LockRect");
    check(*static_cast<DWORD*>(locked.pBits) == 0xff112233, "leased actual SYSTEMMEM write persists");
    check(lease.get()->UnlockRect() == S_OK, "leased surface read UnlockRect");
    auto* app_device = s.app;
    auto* app_surface = lease.get();
    check(s.app->Release() == 1, "surface keeps logical parent device alive");
    s.app = nullptr;
    check(s.factory->Release() == 1, "device keeps logical factory alive");
    s.factory = nullptr;
    check(snapshot_surface_identity(app_device, app_surface, &out) == S_OK && detail::same_surface_identity(identity, out), "lease keeps identity owner storage alive");
    spy.cleanup_lease = &lease; spy.app_device = app_device; spy.app_surface = app_surface;
    spy.entered = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    spy.finished = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    require(spy.entered && spy.finished, "cleanup probe events");
    HRESULT worker_result = E_FAIL;
    std::thread worker([&] {
        if (WaitForSingleObject(spy.entered, 5000) == WAIT_OBJECT_0) {
            SurfaceLeaseIdentity ignored;
            worker_result = snapshot_surface_identity(app_device, app_surface, &ignored);
            SetEvent(spy.finished);
        }
    });
    lease.reset(); lease.reset(); worker.join();
    check(spy.releases == 1 && spy.adds == 0, "exactly one backend surface cleanup with no backend AddRef");
    check(spy.cleanup_empty && spy.reentry_refused, "reentrant cleanup sees empty lease and retired registry key");
    check(spy.worker_completed && worker_result == E_INVALIDARG, "backend cleanup runs outside registry mutex");
    check(snapshot_surface_identity(app_device, app_surface, &out) == E_INVALIDARG && !out.valid(), "final surface release retires device and surface keys");
    // Device-only lookup independently proves that the surface-owned logical
    // parent reference was released, instead of only the surface key disappearing.
    ExecutionView view;
    check(get_execution_view(app_device, &view) == E_INVALIDARG, "final cleanup balances logical device reference");
    CloseHandle(spy.entered); CloseHandle(spy.finished);
}

struct ResetSpy {
    static inline ResetSpy* current = nullptr;
    Patch patch;
    IDirect3DDevice9* device;
    IDirect3DSurface9* surface;
    SurfaceLeaseIdentity expected;
    HRESULT result = S_OK;
    unsigned calls = 0;
    bool refused = true;
    static HRESULT WINAPI reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) {
        auto& s = *current; ++s.calls;
        SurfaceLeaseIdentity observed;
        SurfaceLease lease;
        s.refused = s.refused && snapshot_surface_identity(s.device, s.surface, &observed) == S_FALSE && !observed.valid();
        s.refused = s.refused && acquire_surface_lease(s.device, s.surface, s.expected, lease) == S_FALSE && !lease.get();
        return s.result;
    }
    ResetSpy(Setup& s, Surface& target, SurfaceLeaseIdentity id)
        : patch(s.native,119), device(s.app), surface(target.app), expected(id) {
        current = this; patch.table[16] = reinterpret_cast<void*>(&reset);
    }
    ~ResetSpy() { current = nullptr; }
};
void reset_generation() {
    Setup s;
    Surface surface(s);
    SurfaceLeaseIdentity before, after, recovered;
    require(snapshot_surface_identity(s.app, surface.app, &before) == S_OK, "initial Reset-test identity");
    ResetSpy spy(s, surface, before);
    SurfaceLease lease;
    check(s.app->Reset(&s.pp) == S_OK, "actual Reset wrapper preserves simulated success");
    check(spy.calls == 1 && spy.refused, "lookup and retain refuse inside native Reset callback");
    check(snapshot_surface_identity(s.app, surface.app, &after) == S_OK && after.device_generation == before.device_generation + 1 &&
        after.device_serial == before.device_serial && after.surface_serial == before.surface_serial, "Reset success advances epoch without changing SYSTEMMEM identity");
    check(acquire_surface_lease(s.app, surface.app, before, lease) == E_INVALIDARG && !lease.get(), "pre-Reset identity cannot reacquire");
    check(acquire_surface_lease(s.app, surface.app, after, lease) == S_OK, "new epoch admits surface");
    lease.reset(); // no lease spans Reset; fixture never claims exclusion
    spy.expected = after; spy.result = D3DERR_INVALIDCALL;
    check(s.app->Reset(&s.pp) == D3DERR_INVALIDCALL, "actual Reset wrapper preserves simulated failure");
    check(snapshot_surface_identity(s.app, surface.app, &recovered) == S_FALSE && !recovered.valid(), "failed Reset leaves observation unavailable");
    check(acquire_surface_lease(s.app, surface.app, after, lease) == S_FALSE && !lease.get(), "failed Reset blocks acquisition");
    spy.result = S_OK;
    check(s.app->Reset(&s.pp) == S_OK, "simulated recovery passes actual Reset wrapper");
    check(spy.calls == 3 && spy.refused, "every Reset callback refuses new leases");
    check(snapshot_surface_identity(s.app, surface.app, &recovered) == S_OK && recovered.device_generation == after.device_generation + 2,
        "failure and recovery both advance epoch");
    check(acquire_surface_lease(s.app, surface.app, after, lease) == E_INVALIDARG && !lease.get(), "recovery never revives stale epoch");
    check(acquire_surface_lease(s.app, surface.app, recovered, lease) == S_OK, "recovered epoch admits lease");
    lease.reset();
}

struct CpuState {
    unsigned char x87[108]; unsigned mxcsr; DWORD error;
    __attribute__((always_inline)) CpuState() noexcept {
        asm volatile("fnsave %0\n\tfrstor %0\n\tstmxcsr %1" : "=m"(x87), "=m"(mxcsr) :: "memory");
        error = GetLastError();
    }
    __attribute__((always_inline)) void restore() const noexcept {
        SetLastError(error);
        asm volatile("frstor %0\n\tldmxcsr %1" :: "m"(x87), "m"(mxcsr) : "memory");
    }
};
struct AbiResult { HRESULT result; unsigned ebx, esi, edi, entry_alignment; };
// Four arguments suit acquire; the cdecl snapshot ignores the unused fourth.
// At the actual API call, ESP mod 16 = 4, intentionally not the usual alignment.
extern "C" __attribute__((naked,noinline)) void call_surface_api_4byte(
        void*, void*, void*, const void*, void*, AbiResult*) {
    asm volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "andl $-16,%esp\n\tsubl $24,%esp\n\t"
        "movl 12(%ebp),%eax\n\tmovl %eax,0(%esp)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,4(%esp)\n\t"
        "movl 20(%ebp),%eax\n\tmovl %eax,8(%esp)\n\t"
        "movl 24(%ebp),%eax\n\tmovl %eax,12(%esp)\n\t"
        "leal -4(%esp),%eax\n\tandl $15,%eax\n\tmovl 28(%ebp),%edx\n\tmovl %eax,16(%edx)\n\t"
        "movl $0x11223344,%ebx\n\tmovl $0x55667788,%esi\n\tmovl $0x1234abcd,%edi\n\t"
        "call *8(%ebp)\n\tmovl 28(%ebp),%edx\n\tmovl %eax,0(%edx)\n\t"
        "movl %ebx,4(%edx)\n\tmovl %esi,8(%edx)\n\tmovl %edi,12(%edx)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
void cpu_call(void* api, IDirect3DDevice9* device, IDirect3DSurface9* surface,
        const void* third, void* fourth, HRESULT expected) {
    CpuState original;
    const unsigned short control = 0x077f;
    const unsigned mxcsr = 0x3fa1;
    asm volatile("fninit\n\tfldz\n\tfldz\n\tfdivp\n\tfld1\n\tfldpi\n\tfldcw %0\n\tldmxcsr %1"
        :: "m"(control), "m"(mxcsr) : "memory");
    SetLastError(0x91abcdef);
    CpuState before;
    AbiResult result{};
    call_surface_api_4byte(api, device, surface, third, fourth, &result);
    CpuState after;
    original.restore();
    check(result.result == expected, "four-byte-stack API preserves result");
    check(result.entry_alignment == 4 && result.ebx == 0x11223344 && result.esi == 0x55667788 && result.edi == 0x1234abcd,
        "actual API supports four-byte stack and callee-saved registers");
    check(before.mxcsr == after.mxcsr && before.error == after.error && !std::memcmp(before.x87, after.x87, 108),
        "actual API preserves complete x87 MXCSR and LastError");
}
void cpu_preservation() {
    Setup s;
    Surface surface(s);
    auto* snapshot = reinterpret_cast<void*>(&snapshot_surface_identity);
    auto* acquire = reinterpret_cast<void*>(&acquire_surface_lease);
    SurfaceLeaseIdentity identity;
    SurfaceLease lease;
    cpu_call(snapshot, s.app, surface.app, &identity, nullptr, S_OK);
    cpu_call(snapshot, s.app, surface.app, nullptr, nullptr, E_POINTER);
    cpu_call(snapshot, s.app, reinterpret_cast<IDirect3DSurface9*>(1), &identity, nullptr, E_INVALIDARG);
    require(snapshot_surface_identity(s.app, surface.app, &identity) == S_OK, "restore CPU-test identity");
    cpu_call(acquire, s.app, surface.app, &identity, &lease, S_OK);
    cpu_call(acquire, s.app, surface.app, &identity, &lease, E_INVALIDARG);
    lease.reset();
    auto stale = identity; ++stale.surface_serial;
    cpu_call(acquire, s.app, surface.app, &stale, &lease, E_INVALIDARG);
    ResetSpy spy(s, surface, identity);
    spy.result = D3DERR_INVALIDCALL;
    require(s.app->Reset(&s.pp) == D3DERR_INVALIDCALL, "CPU-test simulated loss");
    cpu_call(snapshot, s.app, surface.app, &identity, nullptr, S_FALSE);
    cpu_call(acquire, s.app, surface.app, &stale, &lease, S_FALSE);
}
}
int main() {
    admission_and_lifetime(); reset_generation(); cpu_preservation();
    std::printf("SURFACE LEASE RESULT checks=%u failures=%u simulated_reset=1\n", checks, failures);
    return failures ? 1 : 0;
}
