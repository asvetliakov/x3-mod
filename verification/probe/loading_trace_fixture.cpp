// Tests main-module import patching and exact forwarded results, without X3.
#include "../../src/proxy/loading_trace.h"
#include "../../src/proxy/engine_patch.h"
#include <d3dx9.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "loading_admission_witness.h"
#define PTR(T, n) reinterpret_cast<T*>(static_cast<uintptr_t>(n))
extern "C" {
void* __cdecl gzopen(const char*, const char*);
int __cdecl gzread(void*, void*, unsigned);
LONG __cdecl gzseek(void*, LONG, int);
int __cdecl inflate(void*, int);
void* __cdecl xmlReadMemory(const char*, int, const char*, const char*, int);
}
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
}
}
using namespace x3m::loading_trace;
static unsigned checks = 0, failures = 0;
static void check(bool okay, const char* label) {
    ++checks;
    if (!okay) {
        ++failures;
        printf("FAIL %s last_error=%lu\n", label, GetLastError());
    }
}
static const Sample& sample(const Snapshot& s, Operation op) {
    return s[static_cast<unsigned>(op)];
}
static PVOID* import_slot(const char* wanted) {
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imports->Name; ++imports) {
        auto names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + imports->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            auto name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (!strcmp(reinterpret_cast<const char*>(name->Name), wanted))
                return reinterpret_cast<PVOID*>(&slots->u1.Function);
        }
    }
    return nullptr;
}
static decltype(&ReadFile) fixture_raw_read = nullptr;
static BOOL WINAPI other_interceptor(HANDLE f, void* b, DWORD n, DWORD* r, OVERLAPPED* o) {
    return fixture_raw_read(f, b, n, r, o);
}
__attribute__((noinline)) static BOOL read_fresh_import(HANDLE file, void* data, DWORD* read) {
    return ReadFile(file, data, 4, read, nullptr);
}
// Execute both edges of a displaced near Jcc. This is the chase camera's new
// engine_patch contract, exercised on original synthetic code, never X3 code.
// Both site addresses cross an aligned qword so claim takes its plain-copy
// install-window path. Negative displacement is tested separately.
static void relocation_cases() {
    using namespace x3m::engine_patch;
    const unsigned before_checks = checks, before_failures = failures;
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(page != nullptr, "rel32_storage");
    if (!page) return;
    for (unsigned backward = 0; backward < 2; ++backward) {
        DWORD previous = 0;
        check(VirtualProtect(page, 4096, PAGE_READWRITE, &previous) != FALSE, "rel32_write_code");
        std::memset(page, 0xcc, 64);
        const unsigned entry = backward ? 8 : 2, site_offset = entry + 4, target = backward ? 0 : 21;
        const unsigned char load_arg[] = {0x8b, 0x44, 0x24, 0x04};                 // mov eax,[esp+4]
        const unsigned char branch[] = {0x83, 0xf8, 0x00, 0x0f, 0x84, 0, 0, 0, 0}; // cmp eax,0; jz rel32
        const unsigned char no[] = {0xb8, 11, 0, 0, 0, 0xc3}, yes[] = {0xb8, 22, 0, 0, 0, 0xc3};
        std::memcpy(page + entry, load_arg, sizeof load_arg);
        std::memcpy(page + site_offset, branch, sizeof branch);
        const uint32_t displacement = uint32_t(target - (site_offset + sizeof branch));
        std::memcpy(page + site_offset + 5, &displacement, 4);
        std::memcpy(page + site_offset + sizeof branch, no, sizeof no);
        std::memcpy(page + target, yes, sizeof yes);
        check(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &previous) != FALSE, "rel32_executable");
        check(FlushInstructionCache(GetCurrentProcess(), page, 64) != FALSE, "rel32_flush");
        using Fn = int(__cdecl*)(int);
        const auto fn = reinterpret_cast<Fn>(page + entry);
        check(fn(0) == 22 && fn(1) == 11, "rel32_native_edges");
        SiteSpec spec{"synthetic_jcc", reinterpret_cast<uintptr_t>(page + site_offset), {}, 9, 0, 5};
        std::memcpy(spec.expected, page + site_offset, 9);
        Site invalid;
        auto bad = spec;
        bad.rel32_offset = ~0u;
        check(!claim(invalid, bad) && std::strcmp(invalid.status, "invalid_spec") == 0, "rel32_overflow_rejected");
        Site site;
        const bool claimed = claim(site, spec);
        check(claimed, "rel32_claim");
        if (claimed) {
            check(!site.atomic_write, "rel32_cross_qword_plain_copy");
            check(fn(0) == 22, "rel32_relocated_taken");
            check(fn(1) == 11, "rel32_relocated_fallthrough");
            check(restore(site), "rel32_restore");
            check(verify_bytes(spec.address, spec.expected, spec.length), "rel32_restored_bytes");
            check(fn(0) == 22 && fn(1) == 11, "rel32_restored_edges");
        }
    }
    close_install_window("fixture_complete");
    Site late;
    SiteSpec spec{"late_jcc", reinterpret_cast<uintptr_t>(page + 12), {}, 9, 0, 5};
    std::memcpy(spec.expected, page + 12, 9);
    check(!claim(late, spec) && std::strcmp(late.status, "late_claim") == 0, "rel32_late_claim_rejected");
    VirtualFree(page, 0, MEM_RELEASE);
    printf("engine_patch_rel32 checks=%u failures=%u directions=2 edges=4 late_claim=1\n", checks - before_checks,
           failures - before_failures);
}
int main() {
    HMODULE self = GetModuleHandleW(nullptr);
    SetEnvironmentVariableW(L"X3M_TELEMETRY", nullptr);
    check(!fixture_initialize(self) && !active(), "disabled");
    SetEnvironmentVariableW(L"X3M_TELEMETRY", L"1");
    check(!fixture_initialize(nullptr) && !active(), "null_image_rejected");
    auto invalid = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(invalid != nullptr, "malformed_image_storage");
    check(!fixture_initialize(reinterpret_cast<HMODULE>(invalid)) && !active(), "invalid_dos_rejected");
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(invalid);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 128;
    check(!fixture_initialize(reinterpret_cast<HMODULE>(invalid)) && !active(), "invalid_nt_rejected");
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(invalid + 128);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.SizeOfImage = 4096;
    nt->OptionalHeader.NumberOfRvaAndSizes = 16;
    check(!fixture_initialize(reinterpret_cast<HMODULE>(invalid)) && !active(), "non_x86_image_rejected");
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    directory.VirtualAddress = 4090;
    directory.Size = 40;
    check(!fixture_initialize(reinterpret_cast<HMODULE>(invalid)) && !active(), "escaping_import_directory_rejected");
    directory.VirtualAddress = 0x200;
    directory.Size = 40;
    auto descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(invalid + 0x200);
    descriptor->Name = 0x300;
    descriptor->FirstThunk = 0x500;
    std::strcpy(reinterpret_cast<char*>(invalid + 0x300), "kernel32.dll");
    auto iat = reinterpret_cast<DWORD*>(invalid + 0x500);
    *iat = reinterpret_cast<DWORD>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ReadFile"));
    check(!fixture_initialize(reinterpret_cast<HMODULE>(invalid)) && !active(),
          "missing_named_thunk_not_inferred_from_iat");
    descriptor->OriginalFirstThunk = 0x400;
    *reinterpret_cast<DWORD*>(invalid + 0x400) = IMAGE_ORDINAL_FLAG32 | 1;
    check(!fixture_initialize(reinterpret_cast<HMODULE>(invalid)) && !active(), "ordinal_import_not_inferred_from_iat");
    *reinterpret_cast<DWORD*>(invalid + 0x400) = 0x600;
    std::strcpy(reinterpret_cast<char*>(invalid + 0x602), "UnrelatedFunction");
    check(!fixture_initialize(reinterpret_cast<HMODULE>(invalid)) && !active(), "unknown_symbol_not_inferred_from_iat");
    check(VirtualFree(invalid, 0, MEM_RELEASE) != FALSE, "malformed_image_release");
    auto kernel = GetModuleHandleW(L"kernel32.dll");
    auto rawRead = reinterpret_cast<decltype(&ReadFile)>(GetProcAddress(kernel, "ReadFile"));
    fixture_raw_read = rawRead;
    auto rawSeek = reinterpret_cast<decltype(&SetFilePointer)>(GetProcAddress(kernel, "SetFilePointer"));
    auto rawOpen = reinterpret_cast<decltype(&CreateFileA)>(GetProcAddress(kernel, "CreateFileA"));
    auto rawFindFirst = reinterpret_cast<decltype(&FindFirstFileA)>(GetProcAddress(kernel, "FindFirstFileA"));
    auto rawFindNext = reinterpret_cast<decltype(&FindNextFileA)>(GetProcAddress(kernel, "FindNextFileA"));
    auto rawFindClose = reinterpret_cast<decltype(&FindClose)>(GetProcAddress(kernel, "FindClose"));
    auto user = GetModuleHandleW(L"user32.dll");
    auto rawCursor = reinterpret_cast<decltype(&SetCursor)>(GetProcAddress(user, "SetCursor"));
    const DWORD sentinel = 0x2468;
    char temp[MAX_PATH]{};
    GetTempPathA(MAX_PATH, temp);
    strcat(temp, "x3-loading-fixture.tmp");
    HANDLE file = rawOpen(temp, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    check(file != INVALID_HANDLE_VALUE, "open_fixture");
    DWORD written = 0;
    check(WriteFile(file, "test", 4, &written, nullptr) && written == 4, "write_fixture");
    rawSeek(file, 0, nullptr, FILE_BEGIN);
    char baseline[8]{};
    DWORD read = 0;
    SetLastError(sentinel);
    BOOL baseline_result = rawRead(file, baseline, 4, &read, nullptr);
    DWORD baseline_error = GetLastError();
    check(baseline_result && read == 4, "baseline_read");
    rawSeek(file, 0, nullptr, FILE_BEGIN);
    PVOID* read_slot = import_slot("ReadFile");
    MEMORY_BASIC_INFORMATION before{}, after{};
    check(read_slot && VirtualQuery(read_slot, &before, sizeof before), "iat_page_before");
    SetEnvironmentVariableW(L"X3M_LOADING_PROBES", L"1"); // probe batch 2 rows the fixture imports (CloseHandle,
                                                          // WriteFile) install as light rows; the engine trampolines
                                                          // stay off (no verified executable)
    check(fixture_initialize(self) && active(), "install_named_imports");
    check(VirtualQuery(read_slot, &after, sizeof after) && before.Protect == after.Protect,
          "iat_protection_restored_after_install");
    // Complete module-lifetime setup before the exact steady counter window;
    // there is no file fingerprint or implementation version gate.
    ID3DXMesh* setup_mesh = nullptr;
    SetLastError(0x1357);
    HRESULT setup_hr = D3DXCreateMesh(11, 13, 0x41, PTR(D3DVERTEXELEMENT9, 0x1000), PTR(IDirect3DDevice9, 0x2000),
                                      &setup_mesh);
    check(setup_hr == S_FALSE && GetLastError() == 0x4321, "mesh setup before steady counter interval");
    take_snapshot();
    char actual[8]{};
    read = 0;
    SetLastError(sentinel);
    BOOL result = ReadFile(file, actual, 4, &read, nullptr);
    DWORD error = GetLastError();
    check(result == baseline_result && error == baseline_error && read == 4 && !memcmp(actual, baseline, 4),
          "read_success_exact");
    SetLastError(sentinel);
    result = ReadFile(INVALID_HANDLE_VALUE, actual, 4, &read, nullptr);
    error = GetLastError();
    SetLastError(sentinel);
    BOOL expected = rawRead(INVALID_HANDLE_VALUE, baseline, 4, &read, nullptr);
    DWORD expected_error = GetLastError();
    check(result == expected && error == expected_error && !result, "read_failure_exact");
    SetLastError(sentinel);
    DWORD offset = SetFilePointer(file, 2, nullptr, FILE_BEGIN);
    error = GetLastError();
    SetLastError(sentinel);
    DWORD expected_offset = rawSeek(file, 2, nullptr, FILE_BEGIN);
    expected_error = GetLastError();
    check(offset == expected_offset && error == expected_error, "seek_success_exact");
    SetLastError(sentinel);
    offset = SetFilePointer(INVALID_HANDLE_VALUE, 0, nullptr, FILE_BEGIN);
    error = GetLastError();
    SetLastError(sentinel);
    expected_offset = rawSeek(INVALID_HANDLE_VALUE, 0, nullptr, FILE_BEGIN);
    expected_error = GetLastError();
    check(offset == expected_offset && error == expected_error, "seek_failure_exact");
    SetLastError(sentinel);
    HANDLE absent = CreateFileA("Z:\\definitely-nonexistent-x3-loading-fixture", GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    error = GetLastError();
    SetLastError(sentinel);
    HANDLE expected_absent = rawOpen("Z:\\definitely-nonexistent-x3-loading-fixture", GENERIC_READ, 0, nullptr,
                                     OPEN_EXISTING, 0, nullptr);
    expected_error = GetLastError();
    check(absent == expected_absent && absent == INVALID_HANDLE_VALUE && error == expected_error, "open_failure_exact");
    HANDLE duplicate = CreateFileA(temp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
    check(duplicate != INVALID_HANDLE_VALUE, "open_success");
    if (duplicate != INVALID_HANDLE_VALUE) CloseHandle(duplicate);
    // Directory enumeration: the open temp file matches the pattern; results,
    // find data and LastError must equal the raw import's, including the
    // ERROR_NO_MORE_FILES termination that the resolver loop depends on.
    char pattern[MAX_PATH]{};
    GetTempPathA(MAX_PATH, pattern);
    strcat(pattern, "x3-loading-fixture*");
    WIN32_FIND_DATAA find_data{}, expected_find_data{};
    SetLastError(sentinel);
    HANDLE find = FindFirstFileA(pattern, &find_data);
    error = GetLastError();
    SetLastError(sentinel);
    HANDLE expected_find = rawFindFirst(pattern, &expected_find_data);
    expected_error = GetLastError();
    check(find != INVALID_HANDLE_VALUE && expected_find != INVALID_HANDLE_VALUE && error == expected_error &&
              !strcmp(find_data.cFileName, expected_find_data.cFileName) &&
              find_data.nFileSizeLow == expected_find_data.nFileSizeLow,
          "find_first_success_exact");
    unsigned next_calls = 0, expected_next_calls = 0;
    BOOL more = TRUE, expected_more = TRUE;
    while (more && next_calls < 64) {
        SetLastError(sentinel);
        more = FindNextFileA(find, &find_data);
        error = GetLastError();
        ++next_calls;
    }
    while (expected_more && expected_next_calls < 64) {
        SetLastError(sentinel);
        expected_more = rawFindNext(expected_find, &expected_find_data);
        expected_error = GetLastError();
        ++expected_next_calls;
    }
    check(!more && !expected_more && next_calls == expected_next_calls && error == expected_error &&
              error == ERROR_NO_MORE_FILES,
          "find_next_exhausted_exact");
    SetLastError(sentinel);
    BOOL closed = FindClose(find);
    error = GetLastError();
    SetLastError(sentinel);
    BOOL expected_closed = rawFindClose(expected_find);
    expected_error = GetLastError();
    check(closed && expected_closed && error == expected_error, "find_close_exact");
    strcat(pattern, "-absent*");
    SetLastError(sentinel);
    HANDLE unmatched = FindFirstFileA(pattern, &find_data);
    error = GetLastError();
    SetLastError(sentinel);
    HANDLE expected_unmatched = rawFindFirst(pattern, &expected_find_data);
    expected_error = GetLastError();
    check(unmatched == INVALID_HANDLE_VALUE && expected_unmatched == INVALID_HANDLE_VALUE && error == expected_error &&
              error == ERROR_FILE_NOT_FOUND,
          "find_first_no_match_exact");
    SetLastError(sentinel);
    HANDLE missing = FindFirstFileA("Z:\\definitely-nonexistent-x3-loading-fixture\\*", &find_data);
    error = GetLastError();
    SetLastError(sentinel);
    HANDLE expected_missing = rawFindFirst("Z:\\definitely-nonexistent-x3-loading-fixture\\*", &expected_find_data);
    expected_error = GetLastError();
    check(missing == INVALID_HANDLE_VALUE && expected_missing == INVALID_HANDLE_VALUE && error == expected_error &&
              error == ERROR_PATH_NOT_FOUND,
          "find_first_failure_exact");
    SetLastError(sentinel);
    more = FindNextFileA(INVALID_HANDLE_VALUE, &find_data);
    error = GetLastError();
    SetLastError(sentinel);
    expected_more = rawFindNext(INVALID_HANDLE_VALUE, &expected_find_data);
    expected_error = GetLastError();
    check(!more && !expected_more && error == expected_error && error != ERROR_NO_MORE_FILES,
          "find_next_failure_exact");
    SetLastError(sentinel);
    closed = FindClose(INVALID_HANDLE_VALUE);
    error = GetLastError();
    SetLastError(sentinel);
    expected_closed = rawFindClose(INVALID_HANDLE_VALUE);
    expected_error = GetLastError();
    check(!closed && !expected_closed && error == expected_error, "find_close_failure_exact");
    SetLastError(sentinel);
    rawCursor(nullptr);
    SetLastError(sentinel);
    HCURSOR expected_cursor = rawCursor(nullptr);
    expected_error = GetLastError();
    SetLastError(sentinel);
    HCURSOR cursor = SetCursor(nullptr);
    error = GetLastError();
    check(cursor == expected_cursor && error == expected_error, "setcursor_exact");
    // Reference SetCursorPos so it has a named import, but do not move the user's cursor.
    if (GetEnvironmentVariableW(L"X3M_FIXTURE_MOVE_CURSOR", nullptr, 0)) SetCursorPos(0, 0);
    ID3DXEffect* fx = nullptr;
    ID3DXBuffer* errors = nullptr;
    HRESULT hr = D3DXCreateEffect(PTR(IDirect3DDevice9, 0x1000), PTR(void, 0x2000), 123, PTR(D3DXMACRO, 0x3000),
                                  PTR(ID3DXInclude, 0x4000), 456, PTR(ID3DXEffectPool, 0x5000), &fx, &errors);
    error = GetLastError();
    check(hr == S_FALSE && error == 0x4321 && fx == PTR(ID3DXEffect, 0x6000) && errors == PTR(ID3DXBuffer, 0x7000),
          "effect_all_args_success");
    hr = D3DXCreateEffect(PTR(IDirect3DDevice9, 0x1000), PTR(void, 0x2000), 123, PTR(D3DXMACRO, 0x3000),
                          PTR(ID3DXInclude, 0x4000), 0, PTR(ID3DXEffectPool, 0x5000), &fx, &errors);
    error = GetLastError();
    check(hr == E_INVALIDARG && error == 0x8765, "effect_failure_exact");
    IDirect3DTexture9* texture = nullptr;
    hr = D3DXCreateTextureFromFileInMemoryEx(PTR(IDirect3DDevice9, 0x1000), PTR(void, 0x2000), 321, 12, 13, 14, 15,
                                             D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 16, 17, 18, PTR(D3DXIMAGE_INFO, 0x3000),
                                             PTR(PALETTEENTRY, 0x4000), &texture);
    error = GetLastError();
    check(hr == S_FALSE && error == 0x4321 && texture == PTR(IDirect3DTexture9, 0x5000), "texture_all_args");
    IDirect3DCubeTexture9* cube = nullptr;
    hr = D3DXCreateCubeTextureFromFileInMemoryEx(PTR(IDirect3DDevice9, 0x1000), PTR(void, 0x2000), 654, 12, 14, 15,
                                                 D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 16, 17, 18,
                                                 PTR(D3DXIMAGE_INFO, 0x3000), PTR(PALETTEENTRY, 0x4000), &cube);
    error = GetLastError();
    check(hr == S_FALSE && error == 0x4321 && cube == PTR(IDirect3DCubeTexture9, 0x5000), "cube_all_args");
    hr = D3DXLoadSurfaceFromFileInMemory(PTR(IDirect3DSurface9, 0x1000), PTR(PALETTEENTRY, 0x2000), PTR(RECT, 0x3000),
                                         PTR(void, 0x4000), 987, PTR(RECT, 0x5000), 16, 18,
                                         PTR(D3DXIMAGE_INFO, 0x6000));
    error = GetLastError();
    check(hr == S_FALSE && error == 0x4321, "surface_all_args");
    void* gz = gzopen(PTR(char, 0x1000), PTR(char, 0x2000));
    error = GetLastError();
    check(gz == PTR(void, 0x3000) && error == 0x4321, "gzopen_args");
    void* badgz = gzopen(nullptr, PTR(char, 0x2000));
    error = GetLastError();
    check(!badgz && error == 0x8765, "gzopen_failure");
    char codec[8]{};
    int n = gzread(gz, codec, 7);
    error = GetLastError();
    check(n == 4 && error == 0x4321 && !memcmp(codec, "gzip", 4), "gzread_args");
    n = gzread(nullptr, codec, 7);
    error = GetLastError();
    check(n == -1 && error == 0x8765, "gzread_failure");
    LONG pos = gzseek(gz, -33, 2);
    error = GetLastError();
    check(pos == 0x76543210 && error == 0x4321, "gzseek_signed32_args");
    pos = gzseek(gz, -33, 1);
    error = GetLastError();
    check(pos == -1 && error == 0x8765, "gzseek_failure");
    n = inflate(PTR(void, 0x1000), 4);
    error = GetLastError();
    check(n == 1 && error == 0x4321, "inflate_args");
    n = inflate(PTR(void, 0x1000), 0);
    error = GetLastError();
    check(n == -5 && error == 0x8765, "inflate_nonfatal");
    n = inflate(nullptr, 4);
    error = GetLastError();
    check(n == -3 && error == 0x8765, "inflate_failure");
    void* xml = xmlReadMemory(PTR(char, 0x1000), 123, PTR(char, 0x2000), PTR(char, 0x3000), 0x180);
    error = GetLastError();
    check(xml == PTR(void, 0x4000) && error == 0x4321, "xmlread_args");
    xml = xmlReadMemory(PTR(char, 0x1000), 123, PTR(char, 0x2000), PTR(char, 0x3000), 0);
    error = GetLastError();
    check(!xml && error == 0x8765, "xmlread_failure");
    ID3DXMesh* mesh = nullptr;
    SetLastError(0x1357);
    hr = D3DXCreateMesh(11, 13, 0x41, PTR(D3DVERTEXELEMENT9, 0x1000), PTR(IDirect3DDevice9, 0x2000), &mesh);
    error = GetLastError();
    check(hr == S_FALSE && error == 0x4321 && mesh == PTR(ID3DXMesh, 0x3000),
          "mesh_create_exact_args_output_and_incoming_error");
    SetLastError(0x1357);
    hr = D3DXCreateMesh(11, 13, 0x42, PTR(D3DVERTEXELEMENT9, 0x1000), PTR(IDirect3DDevice9, 0x2000), nullptr);
    error = GetLastError();
    check(hr == E_INVALIDARG && error == 0x8765, "mesh_create_failed_null_output_exact");
    DWORD meshAdjacency = 0;
    errors = nullptr;
    SetLastError(0x1357);
    hr = D3DXCleanMesh(D3DXCLEANTYPE(3), PTR(ID3DXMesh, 0x1000), PTR(DWORD, 0x2000), &mesh, &meshAdjacency, &errors);
    error = GetLastError();
    check(hr == S_FALSE && error == 0x4321 && mesh == PTR(ID3DXMesh, 0x3000) && meshAdjacency == 0xabcdef01 &&
              errors == PTR(ID3DXBuffer, 0x4000),
          "mesh_clean_exact_args_outputs_and_incoming_error");
    SetLastError(0x1357);
    hr = D3DXCleanMesh(D3DXCLEANTYPE(0), PTR(ID3DXMesh, 0x1000), nullptr, nullptr, nullptr, nullptr);
    error = GetLastError();
    check(hr == E_INVALIDARG && error == 0x8765, "mesh_clean_failed_null_outputs_exact");
    const auto data = take_snapshot();
    check(sample(data, Operation::FileRead).count == 2 && sample(data, Operation::FileRead).failures == 1 &&
              sample(data, Operation::FileRead).bytes == 4,
          "read_counters");
    check(sample(data, Operation::FileSeek).count == 2 && sample(data, Operation::FileSeek).ambiguous == 1,
          "seek_counters");
    check(sample(data, Operation::FileOpen).count == 2 && sample(data, Operation::FileOpen).failures == 1,
          "open_counters");
    check(sample(data, Operation::FindFirst).count == 3 && sample(data, Operation::FindFirst).failures == 1 &&
              sample(data, Operation::FindFirst).ambiguous == 1 && sample(data, Operation::FindFirst).bytes == 0,
          "find_first_counters");
    check(sample(data, Operation::FindNext).count == next_calls + 1 &&
              sample(data, Operation::FindNext).failures == 1 && sample(data, Operation::FindNext).ambiguous == 1,
          "find_next_counters");
    check(sample(data, Operation::FindClose).count == 2 && sample(data, Operation::FindClose).failures == 1 &&
              sample(data, Operation::FindClose).ambiguous == 0,
          "find_close_counters");
    check(sample(data, Operation::Effect).count == 2 && sample(data, Operation::Effect).failures == 1 &&
              sample(data, Operation::Effect).bytes == 246,
          "effect_counters");
    check(sample(data, Operation::Texture).bytes == 321 && sample(data, Operation::CubeTexture).bytes == 654 &&
              sample(data, Operation::Surface).bytes == 987,
          "texture_byte_counters");
    check(sample(data, Operation::MeshCreate).count == 2 && sample(data, Operation::MeshCreate).failures == 1,
          "mesh_create_counters");
    check(sample(data, Operation::MeshClean).count == 2 && sample(data, Operation::MeshClean).failures == 1,
          "mesh_clean_counters");
    check(sample(data, Operation::MeshAdjacency).count == 0 && sample(data, Operation::MeshOptimize).count == 0,
          "unverified_stub_dll_has_no_method_hooks");
    check(sample(data, Operation::CursorSet).count == 1, "cursor_count");
    check(sample(data, Operation::GzOpen).count == 2 && sample(data, Operation::GzOpen).failures == 1,
          "gzopen_counters");
    check(sample(data, Operation::GzRead).count == 2 && sample(data, Operation::GzRead).failures == 1 &&
              sample(data, Operation::GzRead).bytes == 4,
          "gzread_counters");
    check(sample(data, Operation::GzSeek).count == 2 && sample(data, Operation::GzSeek).failures == 1,
          "gzseek_counters");
    check(sample(data, Operation::Inflate).count == 3 && sample(data, Operation::Inflate).failures == 1 &&
              sample(data, Operation::Inflate).ambiguous == 1,
          "inflate_counters");
    check(sample(data, Operation::XmlRead).count == 2 && sample(data, Operation::XmlRead).failures == 1 &&
              sample(data, Operation::XmlRead).bytes == 246,
          "xml_counters");
    check(sample(data, Operation::HandleClose).count >= 1 && sample(data, Operation::HandleClose).failures == 0,
          "probe_row_close_handle_counted");
    check(sample(take_snapshot(), Operation::Effect).count == 0, "snapshot_exchange");
    // Same-process synthetic overhead check; never a claim about game loading.
    auto rawInflate = reinterpret_cast<int(__cdecl*)(void*, int)>(
        GetProcAddress(GetModuleHandleW(L"zlib1.dll"), "inflate"));
    LARGE_INTEGER hz{}, begin{}, middle{}, end{};
    QueryPerformanceFrequency(&hz);
    constexpr unsigned iterations = 20000;
    volatile int sink = 0;
    QueryPerformanceCounter(&begin);
    for (unsigned i = 0; i < iterations; ++i) sink = rawInflate(PTR(void, 0x1000), 4);
    QueryPerformanceCounter(&middle);
    for (unsigned i = 0; i < iterations; ++i) sink = inflate(PTR(void, 0x1000), 4);
    QueryPerformanceCounter(&end);
    printf("loading_benchmark synthetic=1 calls=%u raw_us_per_call=%.6f hooked_us_per_call=%.6f\n", iterations,
           double(middle.QuadPart - begin.QuadPart) * 1e6 / hz.QuadPart / iterations,
           double(end.QuadPart - middle.QuadPart) * 1e6 / hz.QuadPart / iterations);
    check(sink == 1 && sample(take_snapshot(), Operation::Inflate).count == iterations, "benchmark_count_and_result");
    char pipe_name[128];
    snprintf(pipe_name, sizeof pipe_name, "\\\\.\\pipe\\x3_loading_%lu", GetCurrentProcessId());
    HANDLE server = CreateNamedPipeA(pipe_name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_WAIT,
                                     1, 64, 64, 0, nullptr);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    check(server != INVALID_HANDLE_VALUE && ov.hEvent, "pending_pipe_setup");
    if (server != INVALID_HANDLE_VALUE && ov.hEvent) {
        BOOL connected = ConnectNamedPipe(server, &ov);
        DWORD connection_error = GetLastError();
        check(connected || connection_error == ERROR_IO_PENDING, "pending_pipe_connect");
        HANDLE client = rawOpen(pipe_name, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        check(client != INVALID_HANDLE_VALUE, "pending_pipe_client");
        DWORD transferred = 0;
        GetOverlappedResult(server, &ov, &transferred, TRUE);
        ResetEvent(ov.hEvent);
        BOOL pending_result = ReadFile(server, actual, 4, &transferred, &ov);
        DWORD pending_error = GetLastError();
        check(!pending_result && pending_error == ERROR_IO_PENDING, "read_pending_exact");
        CancelIoEx(server, &ov);
        GetOverlappedResult(server, &ov, &transferred, TRUE);
        if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
        const auto pending_data = take_snapshot();
        check(sample(pending_data, Operation::FileRead).count == 1 &&
                  sample(pending_data, Operation::FileRead).pending == 1 &&
                  sample(pending_data, Operation::FileRead).failures == 0,
              "pending_counter_not_failure");
    }
    if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
    if (ov.hEvent) CloseHandle(ov.hEvent);
    // Simulate another module replacing one of our hooks before teardown.
    DWORD old_protection = 0, discard = 0;
    check(VirtualProtect(read_slot, sizeof(PVOID), PAGE_READWRITE, &old_protection), "third_party_slot_writable");
    fixture_raw_read = reinterpret_cast<decltype(&ReadFile)>(*read_slot); // Foreign chain retains our dispatched thunk.
    InterlockedExchangePointer(read_slot, reinterpret_cast<PVOID>(other_interceptor));
    check(VirtualProtect(read_slot, sizeof(PVOID), old_protection, &discard), "third_party_slot_protected");
    SetLastError(sentinel);
    shutdown();
    check(!active() && GetLastError() == sentinel, "restore_and_error");
    check(*read_slot == reinterpret_cast<PVOID>(other_interceptor), "shutdown_preserves_other_hook");
    check(VirtualQuery(read_slot, &after, sizeof after) && before.Protect == after.Protect,
          "iat_protection_restored_after_shutdown");
    check(!fixture_initialize(self), "reinitialization_refused_preserves_original_chain");
    rawSeek(file, 0, nullptr, FILE_BEGIN);
    read = 0;
    check(read_fresh_import(file, actual, &read) && read == 4, "foreign_chain_to_saved_thunk_remains_callable");
    check(sample(take_snapshot(), Operation::FileRead).count == 1, "foreign_chain_uses_immutable_original_once");
    VirtualProtect(read_slot, sizeof(PVOID), PAGE_READWRITE, &old_protection);
    InterlockedExchangePointer(read_slot, reinterpret_cast<PVOID>(rawRead));
    VirtualProtect(read_slot, sizeof(PVOID), old_protection, &discard);
    rawSeek(file, 0, nullptr, FILE_BEGIN);
    read_fresh_import(file, actual, &read);
    check(sample(take_snapshot(), Operation::FileRead).count == 0, "restored_import_not_intercepted");
    CloseHandle(file);
    if (!loading_admission_witness()) ++failures;
    relocation_cases();
    printf("loading_fixture checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
