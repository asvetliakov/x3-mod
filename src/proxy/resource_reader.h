#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>

// Fast resource reader for the engine's archive reader 0x004e8880
// (X3M_RESOURCE_READ=native|verify|fast, tools/manage.py --resource-read;
// docs/reverse-engineering/resource-reader.md for the decompiled contract,
// docs/verification/resource-reader.md for the fixture evidence).
//
// The game decodes every gzip resource (loose `.pck`, gzip records inside the
// catalogue `.dat` files) with a 3-byte magic probe, a trailer seek, a
// memset of the whole output, 10-40 locked fgetc calls over the gzip header
// and a loop of 1 KiB fread + byte XOR + inflate. The core below reads the
// whole extent with one fread, unscrambles it word-wide, walks the header in
// memory and runs one inflate into the exact-size output allocated with the
// game's own _malloc, then fills the four allocation counters and the two size
// globals exactly as the original does, and leaves the record cursor and the
// stream where the original's 1 KiB chunk loop leaves them (short of the
// record end when the trailer's tail falls into a chunk of its own). On any deviation from the cases it
// understands (not open, gz handle, progress callback, transparent file, bad
// header, inflate error, size mismatch, allocation failure) it restores the
// stream position and the record cursor and reports Fallback so the original
// body runs unchanged.
//
// resource_reader_core.cpp is compiled without SSE/MMX and does no
// floating-point work: it runs inside the game's loader thread with no CPU
// state boundary (only the last error is transported). Logging happens in
// resource_reader.cpp (SSE build) under PreserveCpuState through a function
// pointer, never on the fast path.
namespace x3m::resource_reader {
// The file object shared by 0x004e8780/0x004e8880/0x004e9210 (offsets verified in the decompile).
struct FileObject {
    void* file;        // +0x00 FILE* (modes 1/3) or gzFile (mode 5)
    uint32_t flags;    // +0x04 1 open, 2 catalogue-resident, 4 gz handle, 0x10 progress callback
    int32_t offset;    // +0x08 record offset inside the .dat (catalogue mode)
    int32_t length;    // +0x0c record length (catalogue mode)
    int32_t cursor;    // +0x10 bytes consumed inside the record (catalogue mode)
};
static_assert(sizeof(FileObject)==0x14);
// Functions of the game's static CRT and its zlib import, bound at initialization
// (the fixture binds its own CRT and the copied zlib1.dll).
struct Environment {
    size_t (__cdecl* fread)(void*,size_t,size_t,void*)=nullptr;   // 0x00512213
    int (__cdecl* fseek)(void*,long,int)=nullptr;                // 0x00510343
    long (__cdecl* ftell)(void*)=nullptr;                        // 0x005128e6
    void* (__cdecl* malloc)(size_t)=nullptr;                     // 0x005112c4
    void (__cdecl* free)(void*)=nullptr;                         // 0x0050e1b0
    int (__cdecl* inflateInit2_)(void*,int,const char*,int)=nullptr;
    int (__cdecl* inflate)(void*,int)=nullptr;
    int (__cdecl* inflateEnd)(void*)=nullptr;
    uint32_t* counters[4]{};      // 0x006085f4, 0x006085f8, 0x006089f8 (bytes), 0x006089fc (allocations)
    uint32_t* size_globals[2]{};  // 0x00596988, 0x0059698c (result size)
};
enum class Outcome : unsigned {
    Handled,          // the buffer is the game's, counters and globals filled
    Fallback,         // stream and cursor restored; run the original
    Count
};
enum class Reason : unsigned {
    None, Unopened, GzHandle, Progress, Tell, State, Length, Scratch, ShortRead, NotGzip,
    Method, Reserved, Header, Empty, Alloc, Init, Inflate, Size, Count
};
const char* reason_name(unsigned reason) noexcept;
struct Result {
    Outcome outcome=Outcome::Fallback; Reason reason=Reason::None;
    void* buffer=nullptr; uint32_t size=0;          // output (Handled)
    uint32_t extent=0;                              // compressed bytes read (Handled or later fallback)
    bool catalogue=false,scrambled=false;
    int32_t expected_cursor=0;                      // catalogue: the cursor the original's chunk loop leaves (see resource-reader.md "Record cursor")
    long expected_position=0;                       // the stream position the original leaves (fast mode seeks there; verify compares)
    bool cursor_short=false;                        // the original stops before the record end (trailer tail in its own chunk)
    uint64_t ticks=0;                               // QPC ticks inside decode
    uint64_t read_ticks=0,scan_ticks=0,alloc_ticks=0,inflate_ticks=0; // phases: fseek+fread, XOR+magic+header, malloc, inflateInit2_/inflate/inflateEnd
};
// Decodes one resource. game_buffer=true allocates with env.malloc and fills the
// counters and globals (fast mode); false decodes into a HeapAlloc scratch
// buffer, touches nothing of the game's and restores the stream (verify mode;
// free the buffer with HeapFree). Never logs.
Result decode(const Environment& env,FileObject* object,bool game_buffer) noexcept;
// Word-wide XOR helper (exposed for the fixture's reference checks).
void xor_bytes(void* data,size_t size,unsigned char key) noexcept;

// ---- hook (resource_reader.cpp) ----
enum class Mode : unsigned { Native, Verify, Fast };
bool initialize();              // reads X3M_RESOURCE_READ; needs object_trace::executable_verified()
Mode mode();
bool installed();
const char* status();
void report();                  // resource_reader_metric line (cumulative), called from loading_trace::report and shutdown
void shutdown();
struct Statistics {
    uint64_t calls=0,handled=0,fallbacks=0,bytes_in=0,bytes_out=0,ticks=0,max_ticks=0;
    uint64_t verify_files=0,verify_equal=0,verify_mismatched=0,verify_original_null=0,original_ticks=0;
    uint64_t catalogue=0,scrambled=0,cursor_short=0,reasons[static_cast<unsigned>(Reason::Count)]{};
    uint64_t read_ticks=0,scan_ticks=0,alloc_ticks=0,inflate_ticks=0; // phase decomposition of `ticks` over handled files
};
Statistics statistics();
// One verify-mode comparison (delivered to the hook's sink only when unequal).
struct VerifyEvent {
    uint32_t size=0,original_size=0,first_mismatch=0,mismatches=0;
    bool equal=false,original_null=false,globals_ok=false,counters_ok=false,cursor_ok=false,position_ok=false,catalogue=false,scrambled=false;
    int32_t cursor=0,expected_cursor=0; long position=0,expected_position=0; // after the original ran vs the chunk-loop prediction
    uint64_t our_ticks=0,original_ticks=0;
};
// Binds the core's environment, mode, the stub's continuation word (the original entry) and the sink.
void core_bind(const Environment& env,Mode mode,void** next_slot,void (*sink)(const VerifyEvent&)) noexcept;
// Entry handler called by the generated stub with the pushfd/pushad frame
// (regs[7] = EAX = the file object). Returns 1 when regs[7] now holds the
// result and the stub must return to the caller, 0 to continue to the original.
extern "C" uint32_t __cdecl x3m_resource_read_entry(uint32_t* regs);

// ---- catalogue .dat handle pool (X3M_DAT_HANDLES=1) ----
// The catalogue branch of 0x004e8780 does _fopen(<NN>.dat,"rb") per resource and
// 0x004e9360 _fclose()s it; both are plain call sites (0x004e87ff, 0x004e9392).
// The pool answers the open from a handle kept from an earlier close of the
// same path (the original then _fseek()s to the record as before) and keeps
// the handle at close instead of closing it. Never shares a handle between two
// open objects; bounded (docs/reverse-engineering/resource-reader.md).
struct PoolEnvironment {
    void* (__cdecl* fopen)(const char*,const char*)=nullptr;   // 0x00512a18
    int (__cdecl* fclose)(void*)=nullptr;                     // 0x0050f8d4
    unsigned flag_offset=0x0c,error_flag=0x20;                // FILE::_flag, _IOERR (VC8 CRT)
};
extern "C" void* __cdecl x3m_pool_fopen(const char* path,const char* mode);
extern "C" int __cdecl x3m_pool_fclose(void* file);
void pool_bind(const PoolEnvironment& env) noexcept;
struct PoolStatistics { uint64_t opens=0,reused=0,real_opens=0,closes=0,kept=0,real_closes=0,errors=0,full=0; unsigned held=0; };
PoolStatistics pool_statistics() noexcept;
void pool_drain() noexcept; // closes every kept handle (shutdown/fixture)
bool pool_requested();
bool pool_installed();

#ifdef X3M_RESOURCE_READER_FIXTURE
// Fixture seams (absent from production): bind an environment, set the mode,
// install the stub on a fixture site, and read the counters.
void fixture_bind(const Environment& env,Mode mode);          // after fixture_install: environment and mode
void fixture_pool_bind(const PoolEnvironment& env);
bool fixture_install(uintptr_t site_address,const unsigned char* expected,unsigned length);
void fixture_shutdown();
#endif
}
