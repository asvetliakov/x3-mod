// Actual public D3DXCloneMesh through real ownership. Not an EXE/game hook.
#define X3M_LATTICE_UPLOAD_READABLE_EMBEDDED
#include "lattice_upload_readable_fixture.cpp"
#undef X3M_LATTICE_UPLOAD_READABLE_EMBEDDED
#include "../../src/ownership/clone_upload_observer.h"
#include <d3dx9mesh.h>
#include <memory>

namespace {
namespace cu = x3m::ownership::clone_upload;
using MeshCreate = HRESULT(WINAPI*)(DWORD, DWORD, DWORD, const D3DVERTEXELEMENT9*, IDirect3DDevice9*, ID3DXMesh**);
const D3DVERTEXELEMENT9 target_decl[] = {{0, 0, D3DDECLTYPE_FLOAT16_4, 0, D3DDECLUSAGE_POSITION, 0},
                                         {0, 8, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0},
                                         {0, 20, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0},
                                         {0, 28, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 1},
                                         {0, 36, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0},
                                         D3DDECL_END()};
const D3DVERTEXELEMENT9 source_decl[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0},  {0, 16, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0},
    {0, 28, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0}, {0, 36, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 1},
    {0, 44, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0},  D3DDECL_END()};
struct MeshCase {
    Com<ID3DXMesh> source, clone;
    Com<IDirect3DVertexBuffer9> source_vb, final_vb;
    Com<IDirect3DIndexBuffer9> source_ib, final_ib;
    std::vector<unsigned char> vertices, indices;
    unsigned slot;
    MeshCase(MeshCreate create, ReadableSession& s, unsigned selected, bool convert = false)
        : slot(selected) {
        const auto shape = cu::shapes[slot];
        vertices.resize(shape.vertices * 40u);
        indices.resize(shape.faces * 6u);
        const std::uint16_t half[] = {0x0000, 0x3c00, 0xc000, 0x3800};
        const float whole[] = {0.f, 1.f, -2.f, .5f};
        std::vector<unsigned char> input(shape.vertices * (convert ? 48u : 40u));
        for (unsigned v = 0; v < shape.vertices; ++v) {
            auto* dst = vertices.data() + v * 40u;
            const std::uint16_t position[] = {half[v % 4], half[(v + 1) % 4], half[(v + 2) % 4], 0x3c00};
            std::memcpy(dst, position, sizeof position);
            const float attributes[] = {0, 1, 0, float(v % 4) * .25f, float((v + 1) % 4) * .25f, .5f, .75f};
            std::memcpy(dst + 8, attributes, sizeof attributes);
            const std::uint32_t color = 0xff000000u | ((v * 7919u) & 0xffffffu);
            std::memcpy(dst + 36, &color, sizeof color);
            if (convert) {
                const float p[] = {whole[v % 4], whole[(v + 1) % 4], whole[(v + 2) % 4], 1.f};
                std::memcpy(input.data() + v * 48u, p, sizeof p);
                std::memcpy(input.data() + v * 48u + 16, dst + 8, 32);
            } else
                std::memcpy(input.data() + v * 40u, dst, 40);
        }
        for (unsigned f = 0; f < shape.faces; ++f)
            for (unsigned c = 0; c < 3; ++c) {
                const auto index = static_cast<std::uint16_t>((f * 7u + c * 11u) % shape.vertices);
                std::memcpy(indices.data() + (f * 3u + c) * 2u, &index, 2);
            }
        ok(create(shape.faces, shape.vertices, D3DXMESH_SYSTEMMEM, convert ? source_decl : target_decl, s.device.p,
                  &source.p),
           "create actual authored source mesh");
        void* map = nullptr;
        ok(source->LockVertexBuffer(0, &map), "author source VB Lock");
        std::memcpy(map, input.data(), input.size());
        ok(source->UnlockVertexBuffer(), "author source VB Unlock");
        ok(source->LockIndexBuffer(0, &map), "author source IB Lock");
        std::memcpy(map, indices.data(), indices.size());
        ok(source->UnlockIndexBuffer(), "author source IB Unlock");
        DWORD* attributes = nullptr;
        ok(source->LockAttributeBuffer(0, &attributes), "author attributes Lock");
        for (unsigned f = 0; f < shape.faces; ++f) attributes[f] = f % 2;
        ok(source->UnlockAttributeBuffer(), "author attributes Unlock");
        ok(source->GetVertexBuffer(&source_vb.p), "source public VB");
        ok(source->GetIndexBuffer(&source_ib.p), "source public IB");
    }
    HRESULT run(ReadableSession& s) {
        return clone_mesh_upload(source.p, D3DXMESH_MANAGED | D3DXMESH_WRITEONLY, target_decl, s.device.p, &clone.p);
    }
    void final_refs() {
        ok(clone->GetVertexBuffer(&final_vb.p), "final public VB");
        ok(clone->GetIndexBuffer(&final_ib.p), "final public IB");
    }
    void close() {
        final_vb.reset();
        final_ib.reset();
        clone.reset();
        source_vb.reset();
        source_ib.reset();
        source.reset();
    }
};
BufferLockView locks(IDirect3DResource9* resource) {
    BufferLockView view{};
    ok(get_buffer_lock_view(resource, &view), "buffer lock counters");
    return view;
}
unsigned route_counts[9]{};
void route_trace(CloneUploadFixtureEvent event, IUnknown*) {
    ++route_counts[static_cast<unsigned>(event)];
}
void route_report(MeshCase& c, cu::Store& store) {
    const auto stats = store.statistics();
    std::printf("ROUTE slot=%u refusal=%u invocations=%llu staged=%llu published=%llu events=", c.slot,
                static_cast<unsigned>(store.refusal()), static_cast<unsigned long long>(stats.invocations),
                static_cast<unsigned long long>(stats.staged_bytes),
                static_cast<unsigned long long>(stats.publications));
    for (unsigned i = 0; i < 9; ++i) std::printf("%s%u", i ? "," : "", route_counts[i]);
    std::printf("\n");
    IDirect3DResource9* resources[] = {c.source_vb.p, c.source_ib.p, c.final_vb.p, c.final_ib.p};
    for (unsigned i = 0; i < 4; ++i) {
        BufferLockView v{};
        const HRESULT hr = get_buffer_lock_view(resources[i], &v);
        std::printf(
            "ROUTE_BUFFER kind=%u hr=%08lx allocation=%llu revision=%llu locks=%llu unlocks=%llu offset=%u size=%u flags=%x pending=%u inflight=%u,%u ambiguous=%u\n",
            i, static_cast<unsigned long>(hr), static_cast<unsigned long long>(v.allocation_id),
            static_cast<unsigned long long>(v.revision), static_cast<unsigned long long>(v.attempt_serial),
            static_cast<unsigned long long>(v.unlock_serial), v.last_offset, v.last_size, v.last_flags, v.pending_locks,
            v.in_flight_locks, v.in_flight_unlocks, unsigned(v.ambiguous || v.saturated));
    }
}
void compare_payload(MeshCase& c, cu::Store& store) {
    c.final_refs();
    route_report(c, store);
    const auto r = store.record(c.slot);
    check(r.producer_payload_valid, "actual Clone publishes qualified producer payload");
    const auto vb = locks(c.final_vb.p), ib = locks(c.final_ib.p);
    check(vb.attempt_serial == 1 && vb.unlock_serial == 1 && ib.attempt_serial == 1 && ib.unlock_serial == 1,
          "observer adds no destination Lock or Unlock");
    const auto sv = locks(c.source_vb.p), si = locks(c.source_ib.p);
    check(sv.attempt_serial == 2 && sv.unlock_serial == 2 && si.attempt_serial == 2 && si.unlock_serial == 2,
          "observer adds no source Lock or Unlock");
    for (unsigned k = 0; k < 2; ++k) {
        auto& expected = k ? c.indices : c.vertices;
        std::vector<unsigned char> copied(expected.size() + 2, 0xa5);
        State original;
        unsigned short cw = 0x077f;
        unsigned mx = 0x3fa0;
        asm volatile("fninit\n\tfld1\n\tfldcw %0\n\tldmxcsr %1" ::"m"(cw), "m"(mx) : "memory");
        SetLastError(0x22334455);
        State incoming;
        const bool copied_ok = copy_clone_upload(c.slot, static_cast<cu::Buffer>(k), c.final_vb.p, c.final_ib.p,
                                                 copied.data() + 1, expected.size());
        State after;
        incoming.restore();
        const bool refused = copy_clone_upload(c.slot, static_cast<cu::Buffer>(k), c.final_vb.p, c.final_ib.p,
                                               copied.data() + 1, expected.size() - 1);
        State refused_after;
        original.restore();
        check(copied_ok, "copy retained with actual final identities");
        check(same_state(incoming, after) && !refused && same_state(incoming, refused_after),
              "copy success and refusal preserve full CPU state and LastError");
        check(copied.front() == 0xa5 && copied.back() == 0xa5 &&
                  std::equal(expected.begin(), expected.end(), copied.begin() + 1),
              "exact retained bytes and canaries");
        void* map = nullptr;
        const HRESULT locked = k ? c.final_ib->Lock(0, 0, &map, D3DLOCK_READONLY)
                                 : c.final_vb->Lock(0, 0, &map, D3DLOCK_READONLY);
        ok(locked, "fixture-only final validation Lock");
        check(!std::memcmp(map, expected.data(), expected.size()), "exact actual clone bytes");
        ok(k ? c.final_ib->Unlock() : c.final_vb->Unlock(), "fixture-only final validation Unlock");
    }
}
void successes(MeshCreate create, Create d3d, HWND window) {
    for (bool convert : {false, true}) {
        ReadableSession s(d3d, window);
        auto store = std::make_unique<cu::Store>();
        check(arm_clone_upload(store.get(), s.device.p, target_decl) == S_OK, "arm manual upload");
        for (unsigned slot = 0; slot < 2; ++slot) {
            MeshCase c(create, s, slot, convert);
            std::fill(std::begin(route_counts), std::end(route_counts), 0);
            clone_upload_fixture_hook(route_trace);
            const HRESULT hr = c.run(s);
            clone_upload_fixture_hook(nullptr);
            ok(hr, "actual public CloneMesh success");
            compare_payload(c, *store);
            void* map = nullptr;
            ok(c.final_vb->Lock(0, 0, &map, 0x800), "later ordinary write Lock");
            check(!store->record(slot).producer_payload_valid,
                  "later write invalidates retained record before native mutation");
            std::memcpy(map, c.vertices.data(), c.vertices.size());
            ok(c.final_vb->Unlock(), "later ordinary write Unlock");
            c.close();
        }
        check(!clone_upload_fixture_scope_active(), "normal scope leaves no active context");
        check(disarm_clone_upload(store.get()) == S_OK, "disarm manual upload");
        s.close();
    }
}
void source_failures(MeshCreate create, Create d3d, HWND window) {
    for (bool vertex : {false, true}) {
        ReadableSession s(d3d, window);
        auto store = std::make_unique<cu::Store>();
        MeshCase c(create, s, 0);
        check(arm_clone_upload(store.get(), s.device.p, target_decl) == S_OK, "arm source failure");
        {
            IUnknown* native = vertex ? static_cast<IUnknown*>(borrowed_native_buffer_for_lock_contract(c.source_vb.p))
                                      : static_cast<IUnknown*>(borrowed_native_buffer_for_lock_contract(c.source_ib.p));
            NativeFault fault(native, false);
            NativeFault::clear = true;
            const auto hr = c.run(s);
            check(FAILED(hr), "actual Clone reports injected source Lock failure");
        }
        check(!store->record(0).producer_payload_valid && !clone_upload_fixture_scope_active(),
              "source failure never publishes or strands scope");
        check(store->statistics().staged_bytes == (vertex ? c.indices.size() : 0),
              "observed source failure blocks its destination staging");
        check(store->discarded_arena_is_zero(), "failed source construction wipes speculative arena");
        check(disarm_clone_upload(store.get()) == S_OK, "disarm failed source");
        c.close();
        s.close();
    }
}
struct UnlockFault {
    using Fn = HRESULT(WINAPI*)(IUnknown*);
    static inline Fn original = nullptr;
    static inline unsigned calls = 0;
    IUnknown* object = nullptr;
    void** previous = nullptr;
    void* table[14]{};
    static HRESULT WINAPI fail(IUnknown* object) {
        ++calls;
        const HRESULT hr = original(object);
        return SUCCEEDED(hr) ? E_FAIL : hr;
    }
    explicit UnlockFault(IUnknown* p)
        : object(p)
        , previous(*reinterpret_cast<void***>(p)) {
        object->AddRef();
        std::memcpy(table, previous, sizeof table);
        std::memcpy(&original, &table[12], sizeof original);
        auto fn = &fail;
        std::memcpy(&table[12], &fn, sizeof fn);
        *reinterpret_cast<void***>(object) = table;
        calls = 0;
    }
    ~UnlockFault() {
        *reinterpret_cast<void***>(object) = previous;
        object->Release();
    }
};
std::unique_ptr<UnlockFault> unlock_fault;
void inject_unlock(CloneUploadFixtureEvent event, IUnknown* resource) {
    if (event != CloneUploadFixtureEvent::Created || unlock_fault || !resource) return;
    auto* native = borrowed_native_buffer_for_lock_contract(static_cast<IDirect3DIndexBuffer9*>(resource));
    if (native) unlock_fault = std::make_unique<UnlockFault>(native);
}
void ignored_unlock(MeshCreate create, Create d3d, HWND window) {
    ReadableSession s(d3d, window);
    auto store = std::make_unique<cu::Store>();
    MeshCase c(create, s, 0);
    check(arm_clone_upload(store.get(), s.device.p, target_decl) == S_OK, "arm ignored Unlock failure");
    clone_upload_fixture_hook(inject_unlock);
    const HRESULT hr = c.run(s);
    clone_upload_fixture_hook(nullptr);
    check(hr == S_OK && unlock_fault && UnlockFault::calls == 1,
          "actual D3DX ignores injected original destination Unlock failure");
    check(!store->record(0).producer_payload_valid && store->statistics().staged_bytes == c.indices.size(),
          "independent original Unlock gate vetoes Clone success");
    check(store->discarded_arena_is_zero(), "failed original closure wipes staged bytes");
    unlock_fault.reset();
    check(disarm_clone_upload(store.get()) == S_OK, "disarm ignored Unlock");
    c.close();
    s.close();
}
ReadableSession* callback_session = nullptr;
MeshCase* callback_case = nullptr;
unsigned callback_mode = 0;
bool callback_once = false;
HRESULT callback_result = E_FAIL;
void qualify_callback(CloneUploadFixtureEvent event, IUnknown*) {
    if (event != CloneUploadFixtureEvent::AfterStageQualifiers || callback_once) return;
    callback_once = true;
    if (callback_mode == 0) {
        callback_result = callback_session->device->Reset(&callback_session->pp);
    } else if (callback_mode == 1) {
        void* p = nullptr;
        callback_result = callback_case->source_vb->Lock(0, 0, &p, D3DLOCK_READONLY | D3DLOCK_NOSYSLOCK);
        if (SUCCEEDED(callback_result)) callback_case->source_vb->Unlock();
    } else if (callback_mode == 2) {
        Com<ID3DXMesh> nested;
        callback_result = clone_mesh_upload(callback_case->source.p, D3DXMESH_MANAGED | D3DXMESH_WRITEONLY, target_decl,
                                            callback_session->device.p, &nested.p);
    } else
        throw std::runtime_error("observer fixture C++ refusal");
}
void callback_refusals(MeshCreate create, Create d3d, HWND window) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        ReadableSession s(d3d, window);
        auto store = std::make_unique<cu::Store>();
        MeshCase c(create, s, 0);
        check(arm_clone_upload(store.get(), s.device.p, target_decl) == S_OK, "arm qualifier callback refusal");
        callback_session = &s;
        callback_case = &c;
        callback_mode = mode;
        callback_once = false;
        callback_result = E_FAIL;
        clone_upload_fixture_hook(qualify_callback);
        const HRESULT hr = c.run(s);
        clone_upload_fixture_hook(nullptr);
        check(callback_once, "callback ran after public qualifiers and before final CPU guard");
        if (mode != 0) check(hr == S_OK, "observer refusal preserves successful original Clone");
        if (mode < 3) ok(callback_result, "injected reset/reentry/nesting native result");
        check(!store->record(0).producer_payload_valid && !store->statistics().staged_bytes,
              "late qualifier callback refuses before raw memcpy");
        check(!clone_upload_fixture_scope_active() && store->discarded_arena_is_zero(),
              "callback refusal cleans context and arena");
        check(disarm_clone_upload(store.get()) == S_OK, "disarm callback refusal");
        c.close();
        s.close();
    }
}
void readable_fallback(MeshCreate create, Create d3d, HWND window) {
    ReadableSession s(d3d, window);
    auto store = std::make_unique<cu::Store>();
    MeshCase c(create, s, 0);
    check(arm_clone_upload(store.get(), s.device.p, target_decl) == S_OK, "arm unreadable fallback");
    {
        CreationFault fault(borrowed_native_device(s.device.p), true, false);
        ok(c.run(s), "original Clone succeeds with native WRITEONLY fallback");
    }
    check(!store->record(0).producer_payload_valid && !store->statistics().staged_bytes,
          "actual unreadable fallback refuses without mapping read");
    c.final_refs();
    D3DVERTEXBUFFER_DESC desc{};
    ok(borrowed_native_buffer_for_lock_contract(c.final_vb.p)->GetDesc(&desc), "actual fallback VB descriptor");
    check((desc.Usage & D3DUSAGE_WRITEONLY) != 0, "actual final VB remains WRITEONLY");
    check(disarm_clone_upload(store.get()) == S_OK, "disarm unreadable fallback");
    c.close();
    s.close();
}
struct ReleaseMutation {
    using Fn = ULONG(WINAPI*)(IUnknown*);
    static inline Fn original = nullptr;
    static inline bool injected = false;
    IDirect3DVertexBuffer9* buffer = nullptr;
    void** previous = nullptr;
    void* table[14]{};
    static ULONG WINAPI release(IUnknown* object) {
        if (!injected) {
            injected = true;
            invalidate_native_buffer_evidence(object);
        }
        return original(object);
    }
    explicit ReleaseMutation(ID3DXMesh* mesh) {
        ok(mesh->GetVertexBuffer(&buffer), "retain final Release callback fixture buffer");
        previous = *reinterpret_cast<void***>(buffer);
        std::memcpy(table, previous, sizeof table);
        std::memcpy(&original, &table[2], sizeof original);
        auto fn = &release;
        std::memcpy(&table[2], &fn, sizeof fn);
        injected = false;
        *reinterpret_cast<void***>(buffer) = table;
    }
    ~ReleaseMutation() {
        *reinterpret_cast<void***>(buffer) = previous;
        buffer->Release();
    }
};
std::unique_ptr<ReleaseMutation> release_mutation;
void inject_final_release(CloneUploadFixtureEvent event, IUnknown* mesh) {
    if (event == CloneUploadFixtureEvent::BeforeFinal && !release_mutation)
        release_mutation = std::make_unique<ReleaseMutation>(static_cast<ID3DXMesh*>(mesh));
}
void final_release_refusal(MeshCreate create, Create d3d, HWND window) {
    ReadableSession s(d3d, window);
    auto store = std::make_unique<cu::Store>();
    MeshCase c(create, s, 0);
    check(arm_clone_upload(store.get(), s.device.p, target_decl) == S_OK, "arm final Release refusal");
    clone_upload_fixture_hook(inject_final_release);
    ok(c.run(s), "final Release refusal preserves Clone success");
    clone_upload_fixture_hook(nullptr);
    check(release_mutation && ReleaseMutation::injected,
          "actual temporary qualifier Release reentered mutation notification");
    check(store->statistics().staged_bytes == c.vertices.size() + c.indices.size() &&
              !store->record(0).producer_payload_valid,
          "last qualifier Release invalidates both staged parts before publication");
    check(store->discarded_arena_is_zero(), "final qualifier refusal wipes all private bytes");
    release_mutation.reset();
    check(disarm_clone_upload(store.get()) == S_OK, "disarm final Release refusal");
    c.close();
    s.close();
}
struct ResetBarrier {
    HANDLE copy_locked = CreateEventA(nullptr, TRUE, FALSE, nullptr),
           reset_entry = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> paused{false}, copied{false}, native_started{false}, native_after_copy{false};
    bool wait_ok = false, excluded = false;
    DWORD creation_thread = GetCurrentThreadId(), reset_thread = 0;
    ~ResetBarrier() {
        CloseHandle(copy_locked);
        CloseHandle(reset_entry);
    }
};
ResetBarrier* reset_barrier = nullptr;
void barrier_callback(CloneUploadFixtureEvent event, IUnknown*) {
    auto& b = *reset_barrier;
    if (event == CloneUploadFixtureEvent::ResetEntry) {
        b.reset_thread = GetCurrentThreadId();
        SetEvent(b.reset_entry);
    } else if (event == CloneUploadFixtureEvent::StageRegistryAcquired && !b.paused.exchange(true)) {
        SetEvent(b.copy_locked);
        b.wait_ok = WaitForSingleObject(b.reset_entry, 5000) == WAIT_OBJECT_0;
        b.excluded = !b.native_started.load();
    } else if (event == CloneUploadFixtureEvent::StageCopyCompleted)
        b.copied = true;
}
struct NativeResetSpy {
    using Fn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    static inline Fn original = nullptr;
    IDirect3DDevice9* object;
    void** previous;
    void* table[119]{};
    static HRESULT WINAPI reset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp) {
        reset_barrier->native_started = true;
        reset_barrier->native_after_copy = reset_barrier->copied.load();
        return original(device, pp);
    }
    explicit NativeResetSpy(IDirect3DDevice9* device)
        : object(device)
        , previous(*reinterpret_cast<void***>(device)) {
        std::memcpy(table, previous, sizeof table);
        std::memcpy(&original, &table[16], sizeof original);
        auto fn = &reset;
        std::memcpy(&table[16], &fn, sizeof fn);
        *reinterpret_cast<void***>(object) = table;
    }
    ~NativeResetSpy() { *reinterpret_cast<void***>(object) = previous; }
};
void concurrent_reset(MeshCreate create, Create d3d, HWND window) {
    ReadableSession s(d3d, window);
    s.device.reset();
    ok(s.factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                               &s.pp, &s.device.p),
       "multithreaded device created on Reset thread");
    auto store = std::make_unique<cu::Store>();
    MeshCase c(create, s, 0);
    ResetBarrier barrier;
    reset_barrier = &barrier;
    check(arm_clone_upload(store.get(), s.device.p, target_decl) == S_OK, "arm concurrent Reset");
    HRESULT clone_hr = E_FAIL, reset_hr = E_FAIL;
    {
        NativeResetSpy spy(borrowed_native_device(s.device.p));
        clone_upload_fixture_hook(barrier_callback);
        std::thread producer([&] { clone_hr = c.run(s); });
        const bool ready = WaitForSingleObject(barrier.copy_locked, 5000) == WAIT_OBJECT_0;
        if (ready)
            reset_hr = s.device->Reset(&s.pp);
        else
            SetEvent(barrier.reset_entry); // bounded cleanup if fixture setup fails
        producer.join();
        clone_upload_fixture_hook(nullptr);
        check(ready && barrier.wait_ok, "actual producer parked with registry while Reset entered");
    }
    std::printf("CONCURRENT_RESET reset=%08lx clone=%08lx\n", static_cast<unsigned long>(reset_hr),
                static_cast<unsigned long>(clone_hr));
    check(barrier.reset_thread == barrier.creation_thread, "native Reset requested on device creation thread");
    check(barrier.excluded && barrier.native_started && barrier.native_after_copy,
          "native Reset excluded until guarded raw copy completed");
    check(!store->record(0).producer_payload_valid && !clone_upload_fixture_scope_active(),
          "concurrent Reset invalidates old scope before publication");
    check(store->discarded_arena_is_zero(), "concurrent Reset leaves private arena wiped");
    check(disarm_clone_upload(store.get()) == S_OK, "disarm concurrent Reset");
    c.close();
    s.close();
    reset_barrier = nullptr;
}
void unsupported_format(MeshCreate create, Create d3d, HWND window) {
    ReadableSession s(d3d, window);
    auto store = std::make_unique<cu::Store>();
    MeshCase c(create, s, 0);
    check(arm_clone_upload(store.get(), s.device.p, target_decl) == S_OK, "arm INDEX32 refusal");
    ok(clone_mesh_upload(c.source.p, D3DXMESH_MANAGED | D3DXMESH_WRITEONLY | D3DXMESH_32BIT, target_decl, s.device.p,
                         &c.clone.p),
       "unsupported INDEX32 still forwards original public Clone");
    check(!store->statistics().staged_bytes && !store->record(0).producer_payload_valid,
          "INDEX32 refused without raw read");
    check(disarm_clone_upload(store.get()) == S_OK, "disarm INDEX32 refusal");
    c.close();
    s.close();
}
}
int main(int argc, char** argv) {
    try {
        setvbuf(stdout, nullptr, _IONBF, 0);
        check(argc == 2, "explicit fixture D3DX path supplied");
        HMODULE d3d = LoadLibraryA("C:\\windows\\system32\\d3d9.dll"), mesh = LoadLibraryA(argv[1]);
        check(d3d && mesh, "load public D3D9 and D3DX");
        char module_path[32768]{};
        const DWORD path_size = GetModuleFileNameA(mesh, module_path, sizeof module_path);
        check(path_size && path_size < sizeof module_path, "selected D3DX module path available");
        std::printf("D3DX_MODULE path=%s\n", module_path);
        Create create = nullptr;
        MeshCreate create_mesh = nullptr;
        auto a = GetProcAddress(d3d, "Direct3DCreate9"), b = GetProcAddress(mesh, "D3DXCreateMesh");
        std::memcpy(&create, &a, sizeof create);
        std::memcpy(&create_mesh, &b, sizeof create_mesh);
        check(create && create_mesh, "public fixture exports");
        // Fixture provenance only: Wine's PE DOS-stub marker is not a production
        // capability or hash prerequisite. Keep the explicit selected module too.
        const char marker[] = "Wine builtin DLL";
        const auto* header = reinterpret_cast<const unsigned char*>(mesh);
        bool builtin = false;
        for (unsigned i = 0; i + sizeof(marker) - 1 <= 128; ++i)
            builtin = builtin || !std::memcmp(header + i, marker, sizeof(marker) - 1);
        const auto export_rva = reinterpret_cast<std::uintptr_t>(b) - reinterpret_cast<std::uintptr_t>(mesh);
        std::printf("D3DX_IDENTITY wine_builtin_marker=%u create_mesh_rva=%08lx\n", unsigned(builtin),
                    static_cast<unsigned long>(export_rva));
        check(!builtin, "fixture selected native D3DX for qualified route");
        HWND window = CreateWindowExA(0, "STATIC", "lattice CloneMesh fixture", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                      nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
        check(window != nullptr, "fixture window");
        successes(create_mesh, create, window);
        source_failures(create_mesh, create, window);
        ignored_unlock(create_mesh, create, window);
        callback_refusals(create_mesh, create, window);
        readable_fallback(create_mesh, create, window);
        final_release_refusal(create_mesh, create, window);
        concurrent_reset(create_mesh, create, window);
        unsupported_format(create_mesh, create, window);
        DestroyWindow(window);
        FreeLibrary(mesh);
        FreeLibrary(d3d);
        std::printf("RESULT PASS phase=manual_clone checks=%u inherited_callback_seh_tested=0\n", checks);
        return 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL %s checks=%u\n", e.what(), checks);
        return 1;
    }
}
