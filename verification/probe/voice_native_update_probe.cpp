// Standalone diagnostic for the documented COM calls used by X3 004cf460.
// Authored code only. No game hooks, registry changes, renderer or audible output.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <dshow.h>
#include <amstream.h>
#include <austream.h>
#include <dmodshow.h>
#include <dmoreg.h>
#include <wmcodecdsp.h>
#include <dsound.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <vector>

static_assert(sizeof(void*) == 4, "probe must use the game's x86 COM ABI");
static_assert((DSBCAPS_LOCSOFTWARE | DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2) == 0x10088,
              "SDK DirectSound flags required");
static LARGE_INTEGER frequency;
static std::atomic<DWORD> active_tick{0};
static HANDLE watchdog_stop;
static DWORD owner_thread;
static bool report_refs = false;
static DWORD WINAPI watchdog(void*) {
    while (WaitForSingleObject(watchdog_stop, 100) == WAIT_TIMEOUT) {
        DWORD begin = active_tick.load();
        if (begin && DWORD(GetTickCount() - begin) > 20000) ExitProcess(124);
    }
    return 0;
}
static void pump() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}
static std::uint64_t cpu_time(bool& valid) {
    FILETIME a{}, b{}, c{}, d{};
    valid = GetThreadTimes(GetCurrentThread(), &a, &b, &c, &d) != FALSE;
    return ((std::uint64_t(c.dwHighDateTime) << 32) | c.dwLowDateTime) +
           ((std::uint64_t(d.dwHighDateTime) << 32) | d.dwLowDateTime);
}
struct Trace {
    int source{}, wrapper{}, route{}, repeat{-1};
    const char* phase = "startup";
    const char* fatal = "none";
    HRESULT fatal_hr = S_OK;
    int seq{};
    void prefix(const char* tag) const {
        std::printf("%s source=%d wrapper=%d route=%d repeat=%d", tag, source, wrapper, route, repeat);
    }
    template <class F> HRESULT call(const char* name, int attempt, bool required, F f) {
        if (GetCurrentThreadId() != owner_thread) ExitProcess(125);
        prefix("VOICE_BEGIN");
        std::printf(" seq=%d phase=%s name=%s attempt=%d required=%d\n", seq, phase, name, attempt, required);
        std::fflush(stdout);
        bool v0{}, v1{};
        const auto c0 = cpu_time(v0);
        LARGE_INTEGER a{}, b{};
        QueryPerformanceCounter(&a);
        active_tick.store(GetTickCount());
        HRESULT hr = f();
        QueryPerformanceCounter(&b);
        const auto c1 = cpu_time(v1);
        active_tick.store(0);
        prefix("VOICE_STAGE");
        std::printf(" seq=%d phase=%s name=%s attempt=%d required=%d hr=%08lx wall_ms=%.6f cpu_ms=%.6f cpu_valid=%d\n",
                    seq++, phase, name, attempt, required, static_cast<unsigned long>(hr),
                    1000. * (b.QuadPart - a.QuadPart) / frequency.QuadPart, (c1 - c0) / 10000., v0 && v1);
        std::fflush(stdout);
        return hr;
    }
    bool need(const char* name, HRESULT hr) {
        if (FAILED(hr)) {
            if (!std::strcmp(fatal, "none")) {
                fatal = name;
                fatal_hr = hr;
                prefix("VOICE_FAILURE");
                std::printf(" phase=%s name=%s hr=%08lx\n", phase, name, static_cast<unsigned long>(hr));
            }
            return false;
        }
        return true;
    }
    template <class F> HRESULT twice(const char* name, F f) {
        HRESULT hr = E_FAIL;
        for (int i = 1; i <= 2; ++i) {
            hr = call(name, i, true, f);
            if (SUCCEEDED(hr)) break;
        }
        // Native invokes its game memory recovery on E_OUTOFMEMORY. This probe
        // records the same attempts but cannot reproduce that private callback.
        return hr;
    }
    template <class T> void release(const char* name, T*& p) {
        if (p) {
            ULONG left = 0;
            call(name, 1, false, [&] {
                left = p->Release();
                return S_OK;
            });
            // Documented IUnknown::Release return value; only useful for the
            // last owned reference of a root object, reported for lifetime.
            if (report_refs) {
                prefix("VOICE_RELEASE");
                std::printf(" name=%s refs=%lu\n", name, static_cast<unsigned long>(left));
            }
            p = nullptr;
        }
    }
};
static void guid_text(REFGUID g, char* b) {
    std::snprintf(b, 40, "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", static_cast<unsigned long>(g.Data1),
                  g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
                  g.Data4[7]);
}
static void topology(Trace& t, IGraphBuilder* graph) {
    if (!graph) return;
    t.call("topology", 1, false, [&] {
        IEnumFilters* list{};
        HRESULT hr = graph->EnumFilters(&list);
        if (FAILED(hr)) return hr;
        IBaseFilter* filter{};
        int index = 0;
        while (index < 32 && list->Next(1, &filter, nullptr) == S_OK) {
            CLSID id{};
            HRESULT idhr = filter->GetClassID(&id);
            char cls[40];
            guid_text(id, cls);
            t.prefix("VOICE_FILTER");
            std::printf(" index=%d clsid=%s hr=%08lx\n", index, cls, static_cast<unsigned long>(idhr));
            IEnumPins* pins{};
            if (SUCCEEDED(filter->EnumPins(&pins))) {
                IPin* pin{};
                int pi = 0;
                while (pi < 32 && pins->Next(1, &pin, nullptr) == S_OK) {
                    PIN_DIRECTION direction{};
                    pin->QueryDirection(&direction);
                    AM_MEDIA_TYPE mt{};
                    HRESULT mhr = pin->ConnectionMediaType(&mt);
                    if (SUCCEEDED(mhr)) {
                        char major[40], sub[40], fmt[40];
                        guid_text(mt.majortype, major);
                        guid_text(mt.subtype, sub);
                        guid_text(mt.formattype, fmt);
                        unsigned tag = 0, rate = 0, channels = 0, bits = 0;
                        if (mt.formattype == FORMAT_WaveFormatEx && mt.cbFormat >= sizeof(WAVEFORMATEX) &&
                            mt.pbFormat) {
                            auto* w = reinterpret_cast<WAVEFORMATEX*>(mt.pbFormat);
                            tag = w->wFormatTag;
                            rate = w->nSamplesPerSec;
                            channels = w->nChannels;
                            bits = w->wBitsPerSample;
                        }
                        t.prefix("VOICE_PIN");
                        std::printf(
                            " filter=%d pin=%d direction=%d major=%s subtype=%s format=%s tag=%u rate=%u channels=%u bits=%u\n",
                            index, pi, int(direction), major, sub, fmt, tag, rate, channels, bits);
                        CoTaskMemFree(mt.pbFormat);
                        if (mt.pUnk) mt.pUnk->Release();
                    }
                    pin->Release();
                    ++pi;
                }
                pins->Release();
            }
            filter->Release();
            ++index;
        }
        list->Release();
        return S_OK;
    });
}
struct Graph {
    Trace& t;
    IAMMultiMediaStream* multi{};
    IMediaStream* media{};
    IAudioMediaStream* audio{};
    IGraphBuilder* graph{};
    IBaseFilter* wrapper{};
    IBaseFilter* splitter{};
    IBaseFilter* source{};
    IAudioData* data{};
    IAudioStreamSample* sample{};
    IDirectSoundBuffer* buffer{};
    IMediaPosition* position{};
    IMediaControl* control{};
    std::vector<BYTE> pcm;
    WAVEFORMATEX format{};
    double duration{};
    bool constructed{};
    bool audio_enabled{true};
    bool pending{};
    explicit Graph(Trace& x)
        : t(x) {}
    void cancel_pending() {
        if (!pending) return;
        pump();
        HRESULT hr = t.call("sample_cancel", 1, false, [&] {
            const DWORD begin = GetTickCount();
            do {
                pump();
                HRESULT done = sample->CompletionStatus(0, 0);
                if (done != MS_S_PENDING) return done;
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            } while (DWORD(GetTickCount() - begin) < 1000);
            return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        });
        if (hr != S_OK && hr != E_ABORT && hr != MS_S_NOUPDATE && hr != MS_S_ENDOFSTREAM) {
            // The sample may still own/write PCM. Do not release
            // it without confirmed completion; terminate this diagnostic
            // process with the entire ownership graph intact instead.
            std::printf("VOICE_ABORT stage=sample_cancel_unresolved hr=%08lx\n", static_cast<unsigned long>(hr));
            std::fflush(stdout);
            ExitProcess(126);
        }
        pending = false;
    }
    void cleanup() {
        t.phase = "cleanup";
        cancel_pending();
        if (buffer) t.call("buffer_stop", 1, false, [&] { return buffer->Stop(); });
        if (control) t.call("control_stop", 1, false, [&] { return control->Stop(); });
        if (multi) t.call("stream_stop", 1, false, [&] { return multi->SetState(STREAMSTATE_STOP); });
        // Native 4d1d40/4d1a40 stop before releasing control/sample/audio/filter
        // references. Diagnostic holds no extra graph references after this.
        t.release("release_position", position);
        t.release("release_control", control);
        t.release("release_buffer", buffer);
        t.release("release_sample", sample);
        t.release("release_data", data);
        t.release("release_audio", audio);
        t.release("release_media", media);
        if (graph) {
            if (wrapper) t.call("remove_wrapper", 1, false, [&] { return graph->RemoveFilter(wrapper); });
            if (splitter) t.call("remove_splitter", 1, false, [&] { return graph->RemoveFilter(splitter); });
            if (source) t.call("remove_source", 1, false, [&] { return graph->RemoveFilter(source); });
        }
        t.release("release_wrapper", wrapper);
        t.release("release_splitter", splitter);
        t.release("release_source", source);
        t.release("release_graph", graph);
        t.release("release_multimedia", multi);
        pcm.clear();
        pump();
    }
    HRESULT manual_sink_only() {
        // Render may select a default renderer on an unusual registration set.
        // Before RUN, require every connected terminal sink to be the owned
        // manual stream filter (or none after native no-audio downgrade).
        // Canonical IUnknown equality is documented COM
        // identity, not a backend-private filter name or layout assumption.
        IMediaStreamFilter* owned{};
        IUnknown* identity{};
        IEnumFilters* list{};
        HRESULT hr = multi->GetFilter(&owned);
        if (SUCCEEDED(hr)) hr = owned->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity));
        if (SUCCEEDED(hr)) hr = graph->EnumFilters(&list);
        unsigned terminals = 0, filters = 0;
        if (SUCCEEDED(hr)) {
            IBaseFilter* filter{};
            HRESULT next = S_OK;
            while ((next = list->Next(1, &filter, nullptr)) == S_OK) {
                if (++filters > 32) {
                    filter->Release();
                    hr = E_UNEXPECTED;
                    break;
                }
                IEnumPins* pins{};
                HRESULT pinhr = filter->EnumPins(&pins);
                unsigned inputs = 0, outputs = 0, pc = 0;
                if (SUCCEEDED(pinhr)) {
                    IPin* pin{};
                    HRESULT pn = S_OK;
                    while ((pn = pins->Next(1, &pin, nullptr)) == S_OK) {
                        if (++pc > 32) {
                            pin->Release();
                            pinhr = E_UNEXPECTED;
                            break;
                        }
                        IPin* peer{};
                        HRESULT connected = pin->ConnectedTo(&peer);
                        if (SUCCEEDED(connected)) {
                            PIN_DIRECTION direction{};
                            HRESULT dir = pin->QueryDirection(&direction);
                            if (FAILED(dir))
                                pinhr = dir;
                            else if (direction == PINDIR_INPUT)
                                ++inputs;
                            else
                                ++outputs;
                        } else if (connected != VFW_E_NOT_CONNECTED)
                            pinhr = connected;
                        if (peer) peer->Release();
                        pin->Release();
                        if (FAILED(pinhr)) break;
                    }
                    if (FAILED(pn)) pinhr = pn;
                    pins->Release();
                }
                if (FAILED(pinhr)) hr = pinhr;
                if (inputs && !outputs) {
                    ++terminals;
                    IUnknown* candidate{};
                    HRESULT same = filter->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&candidate));
                    if (FAILED(same) || candidate != identity) hr = E_ACCESSDENIED;
                    if (candidate) candidate->Release();
                }
                filter->Release();
                if (FAILED(hr)) break;
            }
            if (FAILED(next)) hr = next;
        }
        if (SUCCEEDED(hr) && (terminals > 1 || (audio_enabled && terminals != 1))) hr = E_ACCESSDENIED;
        if (list) list->Release();
        if (identity) identity->Release();
        if (owned) owned->Release();
        t.prefix("VOICE_SINK");
        std::printf(" terminals=%u filters=%u manual_only=%d hr=%08lx\n", terminals, filters, SUCCEEDED(hr),
                    static_cast<unsigned long>(hr));
        return hr;
    }
    bool create(const wchar_t* path, IDirectSound8* sound) {
        t.phase = "create";
#define NEED(name, expr)                                                                                               \
    do {                                                                                                               \
        if (!t.need(name, t.call(name, 1, true, [&] { return (expr); }))) return false;                                \
    } while (0)
        NEED("activate_stream", CoCreateInstance(CLSID_AMMultiMediaStream, nullptr, CLSCTX_INPROC_SERVER,
                                                 IID_IAMMultiMediaStream, reinterpret_cast<void**>(&multi)));
        NEED("initialize", multi->Initialize(STREAMTYPE_READ, AMMSF_NOGRAPHTHREAD, nullptr));
        HRESULT added = E_FAIL;
        for (int i = 1; i <= 2; ++i) {
            added = t.call("add_audio", i, false,
                           [&] { return multi->AddMediaStream(nullptr, &MSPID_PrimaryAudio, 0, &media); });
            if (SUCCEEDED(added)) break;
        }
        audio_enabled = SUCCEEDED(added);
        if (audio_enabled) {
            NEED("audio_qi_pre", media->QueryInterface(IID_IAudioMediaStream, reinterpret_cast<void**>(&audio)));
            WAVEFORMATEX wanted{WAVE_FORMAT_PCM, 1, 44100, 88200, 2, 16, 0};
            NEED("set_pcm", audio->SetFormat(&wanted));
        } else {
            t.prefix("VOICE_DOWNGRADE");
            std::printf(" flags=344 no_audio=1 hr=%08lx\n", static_cast<unsigned long>(added));
        }
        NEED("get_graph", multi->GetFilterGraph(&graph));
        if (t.wrapper && audio_enabled) {
            HRESULT hr = S_OK;
            for (int i = 1; i <= 2; ++i) {
                hr = t.call("activate_wrapper", i, false, [&] {
                    return CoCreateInstance(CLSID_DMOWrapperFilter, nullptr,
                                            CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER, IID_IBaseFilter,
                                            reinterpret_cast<void**>(&wrapper));
                });
                if (SUCCEEDED(hr)) break;
            }
            if (wrapper) {
                IDMOWrapperFilter* dmo{};
                hr = t.call("wrapper_qi", 1, false, [&] {
                    return wrapper->QueryInterface(IID_IDMOWrapperFilter, reinterpret_cast<void**>(&dmo));
                });
                if (SUCCEEDED(hr)) {
                    for (int i = 1; i <= 2; ++i) {
                        hr = t.call("wrapper_init", i, false,
                                    [&] { return dmo->Init(CLSID_CWMSPDecMediaObject, DMOCATEGORY_AUDIO_DECODER); });
                        if (SUCCEEDED(hr)) break;
                    }
                }
                t.release("release_dmo_interface", dmo);
                t.call("add_wrapper", 1, false, [&] { return graph->AddFilter(wrapper, nullptr); });
            }
        }
        if (t.route) {
            for (int i = 1; i <= 2; ++i) {
                HRESULT hr = t.call("activate_splitter", i, false, [&] {
                    return CoCreateInstance(CLSID_MPEG1Splitter, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER,
                                            IID_IBaseFilter, reinterpret_cast<void**>(&splitter));
                });
                if (SUCCEEDED(hr)) break;
            }
            if (splitter) t.call("add_splitter", 1, false, [&] { return graph->AddFilter(splitter, nullptr); });
        }
        HRESULT opened = E_FAIL;
        if (t.route == 2 && splitter) {
            // Mirrors the native alternate source/render route and same-graph
            // fallback, including retained partial filters on failed rendering.
            opened = t.call("add_source", 1, false,
                            [&] { return graph->AddSourceFilter(path, L"X File Source", &source); });
            if (SUCCEEDED(opened)) {
                IPin* pin{};
                opened = t.call("find_output", 1, false, [&] { return source->FindPin(L"Output", &pin); });
                if (SUCCEEDED(opened)) {
                    for (int i = 1; i <= 2; ++i) {
                        opened = t.call("render", i, false, [&] { return graph->Render(pin); });
                        if (SUCCEEDED(opened)) break;
                    }
                }
                t.release("release_output_pin", pin);
            }
        }
        if (FAILED(opened)) {
            if (t.route == 2) t.release("release_failed_source", source);
            opened = t.twice("open_file", [&] { return multi->OpenFile(path, 0); });
        }
        if (!t.need("open_file_or_render", opened)) return false;
        if (audio_enabled) {
            // Preserve independently owned pre-open QI references while replacing
            // them; the original overwrites its fields here. Do not copy a leak.
            t.release("release_audio_pre", audio);
            t.release("release_media_pre", media);
            if (!sound) return t.need("native_directsound_missing", E_POINTER);
            NEED("get_audio", multi->GetMediaStream(MSPID_PrimaryAudio, &media));
            NEED("audio_qi_post", media->QueryInterface(IID_IAudioMediaStream, reinterpret_cast<void**>(&audio)));
            NEED("get_format", audio->GetFormat(&format));
            t.prefix("VOICE_FORMAT");
            std::printf(" tag=%u rate=%lu channels=%u bits=%u align=%u avg=%lu extra=%u\n", format.wFormatTag,
                        static_cast<unsigned long>(format.nSamplesPerSec), format.nChannels, format.wBitsPerSample,
                        format.nBlockAlign, static_cast<unsigned long>(format.nAvgBytesPerSec), format.cbSize);
            // Native sizes: 2-second application sample, 5-second DS ring; cap only
            // absurd diagnostic allocations, report it rather than modifying format.
            if (format.wFormatTag != WAVE_FORMAT_PCM || format.wBitsPerSample != 16 || !format.nChannels ||
                format.nBlockAlign != 2 * format.nChannels || !format.nSamplesPerSec || !format.nAvgBytesPerSec ||
                format.nAvgBytesPerSec > 1000000 ||
                std::uint64_t(format.nSamplesPerSec) * format.nBlockAlign != format.nAvgBytesPerSec)
                return t.need("pcm_domain", E_INVALIDARG);
            pcm.resize(format.nAvgBytesPerSec * 2);
            NEED("activate_audio_data", CoCreateInstance(CLSID_AMAudioData, nullptr, CLSCTX_INPROC_SERVER,
                                                         IID_IAudioData, reinterpret_cast<void**>(&data)));
            NEED("set_buffer_native", data->SetBuffer(static_cast<DWORD>(pcm.size()), pcm.data(), 0));
            NEED("data_format", data->SetFormat(&format));
            NEED("create_sample", audio->CreateSample(data, 0, &sample));
            DSBUFFERDESC desc{};
            desc.dwSize = sizeof(desc);
            desc.dwFlags = 0x10088;
            desc.dwBufferBytes = format.nAvgBytesPerSec * 5;
            desc.lpwfxFormat = &format;
            NEED("create_dsound_buffer", sound->CreateSoundBuffer(&desc, &buffer, nullptr));
        }
        NEED("position_qi", graph->QueryInterface(IID_IMediaPosition, reinterpret_cast<void**>(&position)));
        NEED("control_qi", graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&control)));
        t.call("get_duration", 1, false, [&] { return position->get_Duration(&duration); }); // native ignores HRESULT
        NEED("probe_manual_sink_guard", manual_sink_only());
        NEED("stream_run", multi->SetState(STREAMSTATE_RUN));
        NEED("control_pause", control->Pause());
        constructed = true;
        return true;
#undef NEED
    }
    bool decode() {
        t.phase = "decode";
        if (!audio_enabled) return t.need("native_no_audio", HRESULT_FROM_WIN32(ERROR_NO_DATA));
        // Native construction above is unchanged. Reduce only the diagnostic
        // read size to 100 ms, avoiding multi-second/whole-file decode work.
        DWORD bytes = (format.nAvgBytesPerSec / 10 / format.nBlockAlign) * format.nBlockAlign;
        if (!t.need("set_buffer_probe",
                    t.call("set_buffer_probe", 1, true, [&] { return data->SetBuffer(bytes, pcm.data(), 0); })))
            return false;
        bool ok = true;
        int total_nonzero = 0;
        for (int cue = 0; cue < 2 && ok; ++cue) {
            const double seconds = cue ? 60. : 10.; // native request is +500 ms; native seek subtracts it
            HRESULT hr = t.call("cue_pause", 1, true, [&] { return control->Pause(); });
            if (!t.need("cue_pause", hr)) {
                ok = false;
                break;
            }
            hr = t.twice("cue_seek", [&] { return position->put_CurrentPosition(seconds); });
            if (!t.need("cue_seek", hr)) {
                ok = false;
                break;
            }
            hr = t.twice("cue_run", [&] { return control->Run(); });
            if (!t.need("cue_run", hr)) {
                ok = false;
                break;
            }
            STREAM_TIME previous_end = -1;
            for (int batch = 0; batch < 2 && ok; ++batch) {
                std::fill(pcm.begin(), pcm.end(), 0);
                hr = t.call("sample_update", 1, true,
                            [&] { return sample->Update(SSUPDATE_ASYNC, nullptr, nullptr, 0); });
                if (!t.need("sample_update", hr)) {
                    ok = false;
                    break;
                }
                pending = hr == MS_S_PENDING;
                if (pending) {
                    hr = t.call("sample_complete", 1, true, [&] {
                        const DWORD begin = GetTickCount();
                        HRESULT done = MS_S_PENDING;
                        while (DWORD(GetTickCount() - begin) < 3000) {
                            pump();
                            done = sample->CompletionStatus(0, 0);
                            if (done != MS_S_PENDING) return done;
                            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
                        }
                        return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
                    });
                }
                if (hr == S_OK || hr == E_ABORT || hr == MS_S_NOUPDATE || hr == MS_S_ENDOFSTREAM) pending = false;
                if (hr != S_OK) {
                    if (SUCCEEDED(hr))
                        hr = HRESULT_FROM_WIN32(hr == MS_S_ENDOFSTREAM ? ERROR_HANDLE_EOF : ERROR_NO_DATA);
                    t.need("sample_complete", hr);
                    ok = false;
                    break;
                }
                DWORD size{}, actual{};
                BYTE* ptr{};
                STREAM_TIME start{}, end{}, now{};
                hr = t.call("sample_info", 1, true, [&] { return data->GetInfo(&size, &ptr, &actual); });
                if (!t.need("sample_info", hr)) {
                    ok = false;
                    break;
                }
                hr = t.call("sample_times", 1, true, [&] { return sample->GetSampleTimes(&start, &end, &now); });
                if (!t.need("sample_times", hr)) {
                    ok = false;
                    break;
                }
                if (!ptr || actual > bytes || actual > size || actual % format.nBlockAlign || actual == 0 ||
                    end <= start || (batch && end <= previous_end)) {
                    t.need("sample_bounds_advance", E_FAIL);
                    ok = false;
                    break;
                }
                std::uint64_t energy = 0;
                unsigned nonzero = 0, peak = 0;
                for (DWORD n = 0; n < actual; n += 2) {
                    std::int16_t value;
                    std::memcpy(&value, ptr + n, 2);
                    int v = value;
                    energy += std::uint64_t(std::int64_t(v) * v);
                    nonzero += v != 0;
                    peak = std::max(peak, unsigned(std::abs(v)));
                }
                total_nonzero += nonzero;
                previous_end = end;
                t.prefix("VOICE_PCM");
                std::printf(
                    " cue=%d batch=%d requested_ms=%d seek_ms=%d actual=%lu start=%lld end=%lld current=%lld nonzero=%u peak=%u energy=%llu\n",
                    cue, batch, (cue ? 60000 : 10000) + 500, cue ? 60000 : 10000, static_cast<unsigned long>(actual),
                    static_cast<long long>(start), static_cast<long long>(end), static_cast<long long>(now), nonzero,
                    peak, static_cast<unsigned long long>(energy));
            }
        }
        cancel_pending();
        if (ok && !total_nonzero) return t.need("silent_all_cues", E_FAIL);
        return ok;
    }
    // ---- actual-file mode ----------------------------------------------
    // Same native construction as above; only the read schedule differs.
    struct Totals {
        int segments{}, reads{}, nonzero_reads{};
        std::uint64_t bytes{}, nonzero{};
        bool eos{};
        bool monotonic{true};
    };
    HRESULT finish_update(HRESULT hr) {
        // Native 4d0767 polls CompletionStatus(0,0) without an external event.
        if (hr != MS_S_PENDING) {
            pending = false;
            return hr;
        }
        pending = true;
        HRESULT done = t.call("sample_complete", 1, true, [&] {
            const DWORD begin = GetTickCount();
            while (DWORD(GetTickCount() - begin) < 15000) {
                pump();
                HRESULT status = sample->CompletionStatus(0, 0);
                if (status != MS_S_PENDING) return status;
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
            }
            return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        });
        if (done != MS_S_PENDING) pending = false;
        return done;
    }
    bool segment(const char* name, double start_s, double window_s, bool to_eos, std::vector<BYTE>* capture,
                 Totals& tot) {
        t.phase = name;
        if (!audio_enabled) return t.need("native_no_audio", HRESULT_FROM_WIN32(ERROR_NO_DATA));
        const DWORD read_bytes = (format.nAvgBytesPerSec / format.nBlockAlign) * format.nBlockAlign; // one second;
                                                                                                     // native app
                                                                                                     // sample holds two
        if (!read_bytes || read_bytes > pcm.size()) return t.need("read_size", E_INVALIDARG);
        if (!t.need("set_buffer_read",
                    t.call("set_buffer_read", 1, true, [&] { return data->SetBuffer(read_bytes, pcm.data(), 0); })))
            return false;
        if (!t.need("cue_pause", t.call("cue_pause", 1, true, [&] { return control->Pause(); }))) return false;
        if (!t.need("cue_seek", t.twice("cue_seek", [&] { return position->put_CurrentPosition(start_s); })))
            return false;
        if (!t.need("cue_run", t.twice("cue_run", [&] { return control->Run(); }))) return false;
        const std::uint64_t window_bytes = std::uint64_t(window_s * format.nAvgBytesPerSec);
        const int cap = int(window_s) + 8;
        int reads = 0, nonzero_reads = 0;
        std::uint64_t bytes = 0, nonzero_total = 0;
        STREAM_TIME first_start = 0, last_end = 0;
        bool have = false, eos = false, monotonic = true, failed = false;
        const char* reason = "window";
        HRESULT post = E_PENDING;
        for (int index = 0; index < cap; ++index) {
            std::fill(pcm.begin(), pcm.begin() + read_bytes, BYTE(0));
            HRESULT hr = finish_update(
                t.call("sample_update", 1, true, [&] { return sample->Update(SSUPDATE_ASYNC, nullptr, nullptr, 0); }));
            if (hr != S_OK) {
                if (hr == MS_S_ENDOFSTREAM) {
                    eos = true;
                    reason = "endofstream";
                } else if (hr == MS_S_NOUPDATE)
                    reason = "noupdate";
                else if (hr == E_ABORT)
                    reason = "abort";
                else {
                    reason = "failed";
                    failed = true;
                    t.need("sample_update", hr);
                }
                t.prefix("VOICE_READ");
                std::printf(" segment=%s index=%d hr=%08lx actual=0 start=0 end=0 current=0 nonzero=0 peak=0\n", name,
                            index, static_cast<unsigned long>(hr));
                break;
            }
            DWORD size{}, actual{};
            BYTE* ptr{};
            STREAM_TIME start{}, end{}, now{};
            if (!t.need("sample_info",
                        t.call("sample_info", 1, true, [&] { return data->GetInfo(&size, &ptr, &actual); })))
                return false;
            if (!t.need("sample_times",
                        t.call("sample_times", 1, true, [&] { return sample->GetSampleTimes(&start, &end, &now); })))
                return false;
            if (!ptr || actual > read_bytes || actual > size || actual % format.nBlockAlign)
                return t.need("sample_bounds", E_FAIL);
            if (!actual) {
                reason = "empty";
                t.prefix("VOICE_READ");
                std::printf(
                    " segment=%s index=%d hr=%08lx actual=0 start=%lld end=%lld current=%lld nonzero=0 peak=0\n", name,
                    index, static_cast<unsigned long>(hr), static_cast<long long>(start), static_cast<long long>(end),
                    static_cast<long long>(now));
                break;
            }
            unsigned nonzero = 0, peak = 0;
            for (DWORD n = 0; n < actual; n += 2) {
                std::int16_t value;
                std::memcpy(&value, ptr + n, 2);
                int v = value;
                nonzero += v != 0;
                peak = std::max(peak, unsigned(std::abs(v)));
            }
            if (end <= start || (have && end <= last_end)) monotonic = false;
            if (!have) {
                first_start = start;
                have = true;
            }
            last_end = end;
            bytes += actual;
            nonzero_total += nonzero;
            nonzero_reads += nonzero != 0;
            ++reads;
            if (capture && capture->size() < 4u * 1024u * 1024u) capture->insert(capture->end(), ptr, ptr + actual);
            t.prefix("VOICE_READ");
            std::printf(
                " segment=%s index=%d hr=%08lx actual=%lu start=%lld end=%lld current=%lld nonzero=%u peak=%u\n", name,
                index, static_cast<unsigned long>(hr), static_cast<unsigned long>(actual),
                static_cast<long long>(start), static_cast<long long>(end), static_cast<long long>(now), nonzero, peak);
            if (!to_eos && bytes >= window_bytes) break;
        }
        if (eos) {
            // One documented update past end of stream; it must not decode data.
            post = finish_update(t.call("post_eos_update", 1, false,
                                        [&] { return sample->Update(SSUPDATE_ASYNC, nullptr, nullptr, 0); }));
        }
        // Native pauses the graph once a cue end is reached (004d0700 tail).
        if (!failed) t.call("cue_end_pause", 1, false, [&] { return control->Pause(); });
        cancel_pending();
        t.prefix("VOICE_SEGMENT");
        std::printf(
            " name=%s start_ms=%lld reads=%d bytes=%llu decoded_ms=%.3f span_ms=%.3f first_start=%lld last_end=%lld nonzero=%llu nonzero_reads=%d eos=%d monotonic=%d end_reason=%s post_eos_hr=%08lx\n",
            name, static_cast<long long>(start_s * 1000. + 0.5), reads, static_cast<unsigned long long>(bytes),
            format.nAvgBytesPerSec ? 1000. * double(bytes) / format.nAvgBytesPerSec : 0.,
            (last_end - first_start) / 10000., static_cast<long long>(first_start), static_cast<long long>(last_end),
            static_cast<unsigned long long>(nonzero_total), nonzero_reads, eos, monotonic, reason,
            static_cast<unsigned long>(post));
        ++tot.segments;
        tot.reads += reads;
        tot.nonzero_reads += nonzero_reads;
        tot.bytes += bytes;
        tot.nonzero += nonzero_total;
        tot.eos = tot.eos || eos;
        tot.monotonic = tot.monotonic && monotonic;
        return !failed && reads > 0;
    }
};
static void actual_case(Trace& t, IDirectSound8* sound, int id, const wchar_t* path, double window_s, double tail_s,
                        std::vector<BYTE>* capture) {
    pump();
    t.source = id;
    t.wrapper = 0;
    t.route = 0;
    t.repeat = 0;
    t.fatal = "none";
    t.fatal_hr = S_OK;
    Graph g(t);
    Graph::Totals tot{};
    const bool made = g.create(path, sound);
    bool ok = made, seeked = false;
    if (ok) {
        ok = g.segment("head", 0., window_s, false, capture, tot);
        // The retained-stream reuse path seeks (004d0430) before restarting.
        if (ok && g.duration > 4 * window_s) {
            seeked = true;
            ok = g.segment("seek", g.duration / 2., window_s, false, nullptr, tot);
        }
        if (ok) ok = g.segment("tail", g.duration > tail_s ? g.duration - tail_s : 0., tail_s + 4., true, nullptr, tot);
    }
    const double requested = made ? window_s * (seeked ? 2. : 1.) + tail_s : 0.;
    const double decoded = g.format.nAvgBytesPerSec ? double(tot.bytes) / g.format.nAvgBytesPerSec : 0.;
    t.prefix("VOICE_FILE");
    std::printf(
        " created=%d complete=%d seeked=%d duration_s=%.6f requested_s=%.3f decoded_s=%.6f segments=%d reads=%d bytes=%llu nonzero_reads=%d nonzero=%llu eos=%d monotonic=%d rate=%lu channels=%u bits=%u fatal=%s hr=%08lx\n",
        made, ok, seeked, g.duration, requested, decoded, tot.segments, tot.reads,
        static_cast<unsigned long long>(tot.bytes), tot.nonzero_reads, static_cast<unsigned long long>(tot.nonzero),
        tot.eos, tot.monotonic, static_cast<unsigned long>(g.format.nSamplesPerSec), g.format.nChannels,
        g.format.wBitsPerSample, t.fatal, static_cast<unsigned long>(t.fatal_hr));
    // Native teardown order (004d1d40/004d1a40); refcounts printed per release.
    g.cleanup();
}
static void actual_reopen(Trace& t, IDirectSound8* sound, int id, const wchar_t* path, int attempt) {
    pump();
    t.source = id;
    t.wrapper = 0;
    t.route = 0;
    t.repeat = attempt;
    t.fatal = "none";
    t.fatal_hr = S_OK;
    Graph g(t);
    Graph::Totals tot{};
    const bool made = g.create(path, sound);
    const bool read = made && g.segment("reopen", 0., 1., false, nullptr, tot);
    t.prefix("VOICE_REOPEN");
    std::printf(" attempt=%d created=%d read=%d reads=%d bytes=%llu nonzero_reads=%d fatal=%s hr=%08lx\n", attempt,
                made, read, tot.reads, static_cast<unsigned long long>(tot.bytes), tot.nonzero_reads, t.fatal,
                static_cast<unsigned long>(t.fatal_hr));
    g.cleanup();
}
static void run_actual(Trace& t, IDirectSound8* sound, wchar_t** argv, double window_s, double tail_s) {
    std::vector<BYTE> capture;
    for (int file = 0; file < 2; ++file) {
        const int id = _wtoi(argv[4 + file * 2]);
        const wchar_t* path = argv[5 + file * 2];
        actual_case(t, sound, id, path, window_s, tail_s, file ? nullptr : &capture);
        // Two further opens of the same file mimic the native stream cache reuse.
        for (int attempt = 1; attempt <= 2; ++attempt) actual_reopen(t, sound, id, path, attempt);
    }
    std::size_t written = 0;
    if (!capture.empty()) {
        FILE* f = std::fopen("head.pcm", "wb");
        if (f) {
            written = std::fwrite(capture.data(), 1, capture.size(), f);
            std::fclose(f);
        }
    }
    std::printf("VOICE_CAPTURE bytes=%llu written=%llu\n", static_cast<unsigned long long>(capture.size()),
                static_cast<unsigned long long>(written));
}
int wmain(int argc, wchar_t** argv) {
    const bool actual = argc == 8 && !std::wcscmp(argv[1], L"actual");
    Trace t;
    double window_s = 0, tail_s = 0;
    if (actual) {
        const int window_ms = _wtoi(argv[2]), tail_ms = _wtoi(argv[3]);
        if (window_ms < 1000 || window_ms > 120000 || tail_ms < 1000 || tail_ms > 120000) return 2;
        if (_wtoi(argv[4]) <= 0 || _wtoi(argv[6]) <= 0) return 2;
        window_s = window_ms / 1000.;
        tail_s = tail_ms / 1000.;
        report_refs = true;
    } else {
        if (argc != 5) return 2;
        t.source = _wtoi(argv[1]);
        t.wrapper = _wtoi(argv[2]);
        t.route = _wtoi(argv[3]);
        if ((t.source != 144 && t.source != 244) || (t.wrapper != 0 && t.wrapper != 1) || t.route < 0 || t.route > 2)
            return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    owner_thread = GetCurrentThreadId();
    QueryPerformanceFrequency(&frequency);
    watchdog_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE watcher = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
    if (!watchdog_stop || !watcher) return 3;
    if (actual)
        std::printf(
            "VOICE_ACTUAL_HEADER schema=1 files=2 reopens=2 window_ms=%d tail_ms=%d thread=%lu native_flags=336 audible=0 sample_event=0\n",
            int(window_s * 1000. + 0.5), int(tail_s * 1000. + 0.5), static_cast<unsigned long>(owner_thread));
    else
        std::printf(
            "VOICE_HEADER schema=1 source=%d wrapper=%d route=%d thread=%lu repeats=2 cues=2 batches=2 native_flags=336 audible=0 sample_event=0\n",
            t.source, t.wrapper, t.route, static_cast<unsigned long>(owner_thread));
    HRESULT init = t.call("co_initialize", 1, true, [] { return CoInitialize(nullptr); });
    if (FAILED(init)) {
        std::printf("VOICE_ABORT stage=co_initialize hr=%08lx\n", static_cast<unsigned long>(init));
        SetEvent(watchdog_stop);
        WaitForSingleObject(watcher, 1000);
        CloseHandle(watcher);
        CloseHandle(watchdog_stop);
        return 0;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"X3VoiceProbe";
    RegisterClassW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"X3 silent voice probe", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                                nullptr, wc.hInstance, nullptr);
    IDirectSound8* sound{};
    HRESULT ds = t.call("directsound_create", 1, false,
                        [&] { return DirectSoundCreate8(&DSDEVID_DefaultPlayback, &sound, nullptr); });
    HRESULT coop = E_POINTER;
    if (sound && window)
        coop = t.call("directsound_cooperative", 1, false,
                      [&] { return sound->SetCooperativeLevel(window, DSSCL_PRIORITY); });
    std::printf("VOICE_STARTUP ds_hr=%08lx coop_hr=%08lx ds_present=%d window=%d primary_play=0\n",
                static_cast<unsigned long>(ds), static_cast<unsigned long>(coop), sound != nullptr, window != nullptr);
    if (actual)
        run_actual(t, sound, argv, window_s, tail_s);
    else
        for (int repeat = 0; repeat < 2; ++repeat) {
            pump();
            t.repeat = repeat;
            t.fatal = "none";
            t.fatal_hr = S_OK;
            Graph g(t);
            bool made = g.create(argv[4], sound);
            topology(t, g.graph);
            bool decoded = made && g.decode();
            g.cleanup();
            t.prefix("VOICE_CASE");
            std::printf(" constructed=%d decoded=%d audio_enabled=%d fatal=%s hr=%08lx duration=%.6f cleanup=1\n", made,
                        decoded, g.audio_enabled, t.fatal, static_cast<unsigned long>(t.fatal_hr), g.duration);
        }
    t.source = actual ? 0 : t.source;
    t.repeat = -1;
    t.phase = "shutdown";
    report_refs = false;
    t.release("release_directsound", sound);
    if (window) DestroyWindow(window);
    t.call("co_uninitialize", 1, false, [] {
        CoUninitialize();
        return S_OK;
    });
    SetEvent(watchdog_stop);
    WaitForSingleObject(watcher, 1000);
    CloseHandle(watcher);
    CloseHandle(watchdog_stop);
    if (actual)
        std::printf("VOICE_ACTUAL_COMPLETE files=2 reopens=2 audible=0\n");
    else
        std::printf("VOICE_COMPLETE cases=2 owner_thread=1 audible=0\n");
    return 0;
}
