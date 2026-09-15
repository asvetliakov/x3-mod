#pragma once
#include "loading_trace.h"
#include "loading_intervals_core.h"
#include <windows.h>
#include <wincrypt.h>
#include <cstdint>

// Light loading-trace rows and engine probe handlers (src/proxy/loading_trace_light.cpp).
//
// The translation unit is compiled with -mno-sse -mno-mmx -mfpmath=387 (CMake
// source property; the fixture build scripts compile it separately) and does
// no floating-point work, so nothing in it touches an XMM, MMX or x87
// register: the wrappers below run without CpuCallBoundary (whose two
// FNSAVE/FRSTOR pairs cost ~1.2 us per hooked call under FEX,
// docs/verification/gz-buffer.md) and preserve only the thread's last error,
// which is all a counting/timing forwarder can disturb. verification/probe/
// check_no_x87.py walks these entry points in the built DLL and the objdump
// proof in docs/verification/loading-probes.md covers the object file.
//
// Rules for code in that unit: no 64-bit std::atomic loads/stores (GCC emits
// x87 fild/fistp for them without SSE; the helpers below use lock cmpxchg8b),
// no logging (the printf formatter is x87 code), no double/float anywhere.
namespace x3m::loading_trace::light {
// ---- 64-bit counters without SSE/x87 (lock cmpxchg8b) -----------------------
inline uint64_t exchange64(uint64_t* p,uint64_t desired) noexcept {
    uint32_t lo=uint32_t(*p),hi=uint32_t(*p>>32);
    asm volatile("1:\n\tlock cmpxchg8b %0\n\tjnz 1b"
                 : "+m"(*p),"+a"(lo),"+d"(hi)
                 : "b"(uint32_t(desired)),"c"(uint32_t(desired>>32))
                 : "cc","memory");
    return uint64_t(hi)<<32|lo;
}
inline uint64_t load64(uint64_t* p) noexcept {
    uint32_t lo=0,hi=0; // compares 0 and exchanges 0: a read that writes nothing but zero over zero
    asm volatile("lock cmpxchg8b %0" : "+m"(*p),"+a"(lo),"+d"(hi) : "b"(0u),"c"(0u) : "cc","memory");
    return uint64_t(hi)<<32|lo;
}
inline void add64(uint64_t* p,uint64_t value) noexcept {
    const uint32_t v[2]={uint32_t(value),uint32_t(value>>32)}; // memory operands: eax/edx/ebx/ecx are all taken
    uint32_t lo=uint32_t(*p),hi=uint32_t(*p>>32);
    asm volatile("1:\n\t"
                 "mov %%eax,%%ebx\n\tmov %%edx,%%ecx\n\t"
                 "add %3,%%ebx\n\tadc %4,%%ecx\n\t"
                 "lock cmpxchg8b %0\n\tjnz 1b"
                 : "+m"(*p),"+a"(lo),"+d"(hi)
                 : "m"(v[0]),"m"(v[1])
                 : "ebx","ecx","cc","memory");
}
inline void max64(uint64_t* p,uint64_t value) noexcept {
    // Fields only grow between snapshots; a lost race here costs at most one maximum sample.
    if(*p<value)exchange64(p,value);
}
// Recorder setup/freeze/report accessors never free process-lifetime storage.
unsigned intervals_initialize(bool requested,uint64_t frequency) noexcept;
void intervals_freeze(uint64_t begin,uint64_t end,uint64_t device,uint64_t reset,uint64_t frame,DWORD tid) noexcept;
// 0=off/already exported, 1=recording, 2=outstanding, 3=claimed immutable snapshot.
unsigned intervals_snapshot(const intervals::Header*& header,const intervals::Ring*& rings,const intervals::Record*& records,LONG& outstanding) noexcept;
#ifdef X3M_LOADING_TRACE_FIXTURE
void fixture_interval_failures(unsigned mask) noexcept;
#endif
// ---- counter rows and the timing span -------------------------------------
struct Row { uint64_t calls,failures,pending,ambiguous,bytes,inclusive,exclusive,maximum,overhead; };
constexpr unsigned row_count=static_cast<unsigned>(Operation::Count);
// One-time setup: the TLS slot for span nesting and the clock. Safe to repeat.
bool initialize() noexcept;
void set_original(unsigned op,PVOID original) noexcept;
PVOID get_original(unsigned op) noexcept;
// Exchanges the row to zero (per-field; concurrent calls can straddle adjacent snapshots).
void take(unsigned op,Sample& out) noexcept;
uint64_t tick() noexcept;
// Nesting-aware timing of one forwarded call. begin() reads the caller's last
// error and the clock and pushes the span; before_call() restores the caller's
// last error (QueryPerformanceCounter and the TLS access may not change it, but
// nothing depends on that); finish() reads the callee's last error, accounts,
// pops the span and restores that error. Same accounting as the former
// loading_trace.cpp Span: inclusive, exclusive (minus nested hooked children),
// maximum, and the wrapper tail measured after the callee returned.
struct Span {
    intervals::Ring* interval_token=nullptr;
    uint64_t begin_ticks=0,children=0; Span* parent=nullptr; DWORD caller_error=0; unsigned op=0;
    void begin(unsigned operation) noexcept;
    void before_call() const noexcept;
    void finish(bool failed=false,uint64_t bytes=0,bool pending=false,bool ambiguous=false) noexcept;
};
bool nesting_available() noexcept; // TLS slot allocated (else exclusive == inclusive)

// ---- light import wrappers (installed in the loading_trace hook table) ----------
HANDLE WINAPI file_open(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
BOOL WINAPI file_read(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
DWORD WINAPI file_seek(HANDLE,LONG,PLONG,DWORD);
HCURSOR WINAPI cursor_set(HCURSOR);
BOOL WINAPI cursor_position(int,int);
HANDLE WINAPI find_first(LPCSTR,LPWIN32_FIND_DATAA);
BOOL WINAPI find_next(HANDLE,LPWIN32_FIND_DATAA);
BOOL WINAPI find_close(HANDLE);
void* __cdecl gz_open_traced(const char*,const char*);
int __cdecl gz_read_traced(void*,void*,unsigned);
LONG __cdecl gz_seek_traced(void*,LONG,int);
int __cdecl gz_getc_traced(void*);
LONG __cdecl gz_tell_traced(void*);
int __cdecl gz_close_traced(void*);
int __cdecl gz_write(void*,const void*,unsigned);
int __cdecl inflate_stream(void*,int);
void* __cdecl xml_read(const char*,int,const char*,const char*,int);
// Probe batch 2 rows (X3M_LOADING_PROBES=1): counted and timed the same way.
int __cdecl inflate_init2(void*,int,const char*,int);
int __cdecl inflate_end(void*);
BOOL WINAPI crypt_acquire_context(HCRYPTPROV*,LPCSTR,LPCSTR,DWORD,DWORD);
BOOL WINAPI crypt_release_context(HCRYPTPROV,DWORD);
BOOL WINAPI crypt_import_key(HCRYPTPROV,const BYTE*,DWORD,HCRYPTKEY,DWORD,HCRYPTKEY*);
BOOL WINAPI crypt_create_hash(HCRYPTPROV,ALG_ID,HCRYPTKEY,DWORD,HCRYPTHASH*);
BOOL WINAPI crypt_hash_data(HCRYPTHASH,const BYTE*,DWORD,DWORD);
BOOL WINAPI crypt_verify_signature(HCRYPTHASH,const BYTE*,DWORD,HCRYPTKEY,LPCSTR,DWORD);
BOOL WINAPI crypt_get_hash_param(HCRYPTHASH,DWORD,BYTE*,DWORD*,DWORD);
BOOL WINAPI crypt_destroy_hash(HCRYPTHASH);
BOOL WINAPI crypt_destroy_key(HCRYPTKEY);
BOOL WINAPI create_directory(LPCSTR,LPSECURITY_ATTRIBUTES);
BOOL WINAPI delete_file(LPCSTR);
BOOL WINAPI move_file(LPCSTR,LPCSTR);
BOOL WINAPI move_file_ex(LPCSTR,LPCSTR,DWORD);
BOOL WINAPI write_file(HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED);
DWORD WINAPI get_file_type(HANDLE);
BOOL WINAPI close_handle(HANDLE);
// The write-side rows keep the first paths they see (bounded; reported once by
// loading_trace::report as loading_probe_path lines) so the negative name-probe
// cache's invalidation set can be checked against a real load.
constexpr unsigned path_record_limit=16,path_record_length=200;
struct PathRecord { unsigned op; char path[path_record_length]; };
unsigned take_paths(PathRecord* out,unsigned capacity) noexcept; // drains the records

// ---- engine probes (docs/reverse-engineering/loading-probes.md) --------------
// Entry/exit counters of the engine functions patched by loading_probes.cpp.
// The generated entry stub calls probe_enter with the site index and a pointer
// to the pushfd/pushad frame (regs[0..7] = EDI ESI EBP ESP EBX EDX ECX EAX,
// regs[8] = EFLAGS, regs[9] = the return address slot, regs[10..] = stack
// arguments). For timed sites the handler replaces regs[9] with the shared
// exit stub and keeps the original on a per-thread shadow stack; probe_exit
// pops it (discarding entries below the current stack, i.e. frames that were
// unwound past without returning) and hands the address back to the stub.
constexpr unsigned probe_site_limit=16,probe_extra_count=4,probe_caller_limit=8,probe_shadow_depth=64;
struct ProbeRow {
    uint64_t calls,exits,inclusive,maximum,overflow,desync,bytes,extra[probe_extra_count];
    uint32_t caller_address[probe_caller_limit]; uint64_t caller_calls[probe_caller_limit];
};
enum class ProbeKind : unsigned { Plain, ResourceOpen, Resolve, ResourceRead, ReadDispatch, FindWrapper, CountOnly };
struct ProbeConfig { ProbeKind kind; unsigned ret_pop; };
void probe_configure(unsigned site,const ProbeConfig& config) noexcept;
void probe_set_exit_stub(const void* stub) noexcept;
void probe_set_size_global(const uint32_t* address) noexcept; // DAT_00596988 for the resource_read bytes
void probe_take(unsigned site,ProbeRow& out) noexcept;        // per-field exchange to zero (caller table kept)
// Shadow blocks allocated so far (one per thread that hit a timed probe) and
// their size: they are never freed while the process lives, see
// loading_probes::shutdown().
unsigned shadow_blocks() noexcept;
unsigned shadow_block_bytes() noexcept;
// Reads the dword at address when it lies in committed, readable, non-guard
// memory (VirtualQuery-backed, page cache; no SEH, no IsBadReadPtr). Used by
// the probe handlers before touching a file object a register may point at.
bool probe_read32(uint32_t address,uint32_t& out) noexcept;
extern "C" void __cdecl x3m_probe_enter(unsigned site,uint32_t* regs);
extern "C" uint32_t __cdecl x3m_probe_exit(uint32_t* regs);
}
