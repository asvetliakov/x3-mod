// Direct documented reader diagnostic: expose Open's HRESULT hidden by qasf.
// Reuse immutable R1 authored timing/owner-thread/watchdog helpers only.
#define wmain voice_r1_unused_entry
#include "voice_stream_probe.cpp"
#undef wmain
#include <wmsdk.h>

static bool metadata(Trace& t, IWMSyncReader* reader, DWORD& count) {
    HRESULT hr = t.call("output_count", 1, true, [&] { return reader->GetOutputCount(&count); });
    if (!t.need("output_count", hr)) return false;
    t.prefix("SYNC_OUTPUTS");
    std::printf(" count=%lu\n", static_cast<unsigned long>(count));
    if (count > 16) return t.need("output_bound", E_UNEXPECTED);
    for (DWORD index = 0; index < count; ++index) {
        IWMOutputMediaProps* props{};
        hr = t.call("output_props", index + 1, true, [&] { return reader->GetOutputProps(index, &props); });
        if (!t.need("output_props", hr)) return false;
        DWORD size{};
        hr = t.call("media_size", index + 1, true, [&] { return props->GetMediaType(nullptr, &size); });
        if (!t.need("media_size", hr)) {
            t.release("release_output_props", props);
            return false;
        }
        if (size < sizeof(WM_MEDIA_TYPE) || size > 65536) {
            t.release("release_output_props", props);
            return t.need("media_bound", E_UNEXPECTED);
        }
        std::vector<BYTE> buffer(size);
        const DWORD capacity = size;
        auto* mt = reinterpret_cast<WM_MEDIA_TYPE*>(buffer.data());
        hr = t.call("media_type", index + 1, true, [&] { return props->GetMediaType(mt, &size); });
        if (!t.need("media_type", hr)) {
            t.release("release_output_props", props);
            return false;
        }
        // GetMediaType writes a caller-owned contiguous buffer. Check returned
        // bounds before inspecting the public WAVEFORMATEX prefix; no payload.
        const auto begin = reinterpret_cast<std::uintptr_t>(buffer.data());
        const auto format = reinterpret_cast<std::uintptr_t>(mt->pbFormat);
        const bool bounds = size <= capacity && size >= sizeof(*mt) &&
                            (!mt->cbFormat ||
                             (format >= begin && format - begin <= size && mt->cbFormat <= size - (format - begin)));
        if (!bounds) {
            t.release("release_output_props", props);
            return t.need("media_payload_bound", E_UNEXPECTED);
        }
        char major[40], sub[40], fmt[40];
        guid_text(mt->majortype, major);
        guid_text(mt->subtype, sub);
        guid_text(mt->formattype, fmt);
        WAVEFORMATEX wave{};
        if (mt->formattype == FORMAT_WaveFormatEx && mt->cbFormat >= 16)
            std::memcpy(&wave, mt->pbFormat, std::min<std::size_t>(sizeof(wave), mt->cbFormat));
        t.prefix("SYNC_TYPE");
        std::printf(
            " index=%lu size=%lu major=%s subtype=%s format=%s format_bytes=%lu tag=%u channels=%u rate=%lu bits=%u align=%u avg=%lu extra=%u\n",
            static_cast<unsigned long>(index), static_cast<unsigned long>(size), major, sub, fmt,
            static_cast<unsigned long>(mt->cbFormat), wave.wFormatTag, wave.nChannels,
            static_cast<unsigned long>(wave.nSamplesPerSec), wave.wBitsPerSample, wave.nBlockAlign,
            static_cast<unsigned long>(wave.nAvgBytesPerSec), wave.cbSize);
        t.release("release_output_props", props);
    }
    return true;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    setvbuf(stdout, nullptr, _IONBF, 0);
    owner_thread = GetCurrentThreadId();
    QueryPerformanceFrequency(&frequency);
    watchdog_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE watcher = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
    if (!watchdog_stop || !watcher) return 3;
    std::printf("SYNC_HEADER schema=1 cases=2 processes=1 thread=%lu audible=0\n",
                static_cast<unsigned long>(owner_thread));
    Trace t;
    HRESULT init = t.call("co_initialize", 1, true, [] { return CoInitialize(nullptr); });
    if (FAILED(init)) {
        std::printf("SYNC_ABORT stage=co_initialize hr=%08lx\n", static_cast<unsigned long>(init));
        return 0;
    }
    HMODULE library{};
    HRESULT load = t.call("load_wmvcore", 1, true, [&] {
        library = LoadLibraryW(L"wmvcore.dll");
        return library ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    });
    const char* prerequisite = FAILED(load) ? "load_wmvcore" : "find_create";
    decltype(&WMCreateSyncReader) create{};
    HRESULT lookup = load;
    if (library)
        lookup = t.call("find_create", 1, true, [&] {
            const FARPROC address = GetProcAddress(library, "WMCreateSyncReader");
            static_assert(sizeof(address) == sizeof(create), "Windows function pointer ABI");
            std::memcpy(&create, &address, sizeof(create));
            return create ? S_OK : HRESULT_FROM_WIN32(GetLastError());
        });
    for (int index = 0; index < 2; ++index) {
        pump();
        t.source = index ? 244 : 144;
        t.repeat = 0;
        t.phase = "open";
        t.fatal = "none";
        t.fatal_hr = S_OK;
        IWMSyncReader* reader{};
        bool created = false, opened = false, inspected = false, closed = false;
        DWORD count = 0;
        HRESULT result = E_PENDING;
        if (FAILED(lookup))
            t.need(prerequisite, lookup);
        else {
            const HRESULT activation = t.call("create_sync_reader", 1, true,
                                              [&] { return create(nullptr, 0, &reader); });
            if (t.need("create_sync_reader", activation)) {
                created = true;
                result = t.call("sync_open", 1, true, [&] { return reader->Open(argv[1 + index]); });
                if (t.need("sync_open", result)) {
                    opened = true;
                    inspected = metadata(t, reader, count);
                }
            }
        }
        t.phase = "cleanup";
        if (opened) {
            HRESULT hr = t.call("sync_close", 1, true, [&] { return reader->Close(); });
            closed = t.need("sync_close", hr);
        }
        t.release("release_sync_reader", reader);
        pump();
        t.prefix("SYNC_CASE");
        std::printf(
            " created=%d opened=%d metadata=%d outputs=%lu closed=%d open_hr=%08lx fatal=%s hr=%08lx cleanup=1\n",
            created, opened, inspected, static_cast<unsigned long>(count), closed, static_cast<unsigned long>(result),
            t.fatal, static_cast<unsigned long>(t.fatal_hr));
    }
    t.source = 0;
    t.repeat = -1;
    t.phase = "shutdown";
    if (library)
        t.call("free_wmvcore", 1, false,
               [&] { return FreeLibrary(library) ? S_OK : HRESULT_FROM_WIN32(GetLastError()); });
    t.call("co_uninitialize", 1, false, [] {
        CoUninitialize();
        return S_OK;
    });
    SetEvent(watchdog_stop);
    WaitForSingleObject(watcher, 1000);
    CloseHandle(watcher);
    CloseHandle(watchdog_stop);
    std::printf("SYNC_COMPLETE cases=2 processes=1 audible=0\n");
    return 0;
}
