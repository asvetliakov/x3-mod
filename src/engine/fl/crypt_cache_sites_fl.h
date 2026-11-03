#pragma once
#include <cstdint>
#include "loading_windows_fl.h"

// FL sites of the CryptoAPI context/key cache (the AP twin: loading_trace.cpp's crypt_sites and four crypt rows; the
// cache itself, src/proxy/crypt_cache.cpp, is shared). Verified facts: docs/reverse-engineering/fl/loading-hooks.md,
// "Port (2026-11-03)" (verifier verification/probe/verify_crypt_cache_sites_fl.py). Every value is an RVA added to
// x3m::fl::module_base().
//
// Signature checker 0x0f8b50 (ECX text, EDX length, five stack arguments, plain ret; window "checker", whole function):
// the six `call [IAT]` (six bytes, operand a HIGHLOW = base + slot) whose return addresses are the cache's only
// admitted callers (AP: 0x4cac3e.. at 0x400000). Sibling signer 0x0f7c90: `mov ebx,[base+0x150010]` at 0x0f7c9e, then
// `call ebx` (returns 0x0f7ccc delete, 0x0f7ce1 new keyset), a private-key import at 0x0f8a02 and destroy / release /
// final delete at 0x0f8b15 / 0x0f8b21 / 0x0f8b3a (return 0x0f8b40): never admitted; its acquire on the scratch
// container retires the cache first (crypt_cache::retire_foreign), so its native delete/create/delete sees the
// namespace native alone would have.
namespace x3m::fl::crypt_cache_fl {
namespace W = x3m::fl::loading_windows;
struct Import {
    const char* name;
    std::uint32_t slot_rva;
};
// Claim order: CryptAcquireContextA last (no cache request can start before every lifecycle route is ours).
constexpr unsigned import_count = 4;
constexpr Import imports[import_count] = {{"CryptDestroyKey", 0x150008},
                                          {"CryptReleaseContext", 0x150034},
                                          {"CryptImportKey", 0x150000},
                                          {"CryptAcquireContextA", 0x150010}};
enum Index : unsigned { destroy_index, release_index, import_index, acquire_index };
struct Gate {
    std::uint32_t return_rva; // the call's end: [esp] at the import's entry
    unsigned index;
};
constexpr unsigned gate_count = 6;
constexpr Gate gates[gate_count] = {{0x0f8bc2, acquire_index}, {0x0f8bdb, acquire_index}, {0x0f8c08, import_index},
                                    {0x0f8db1, destroy_index}, {0x0f8dbd, release_index}, {0x0f8dd6, acquire_index}};
constexpr std::uint32_t checker_rva = 0x0f8b50, signer_rva = 0x0f7c90;
constexpr std::uint32_t sibling_returns[3] = {0x0f7ccc, 0x0f7ce1, 0x0f8b40};
constexpr std::uint32_t provider_string_rva = 0x1908e4, container_string_rva = 0x190910;
constexpr char provider_string[] = "Microsoft Base Cryptographic Provider v1.0";
constexpr char container_string[] = "X2EgosoftCSPContainer";

// Each gate's call operand (window "checker", offset return - 4) is its import's slot; the checker pushes both strings.
constexpr bool gates_bound() {
    for (const Gate& g : gates)
        if (W::fixup_target(W::crypt_windows, W::crypt_fixups, "checker", g.return_rva - 4 - checker_rva) !=
            imports[g.index].slot_rva)
            return false;
    return true;
}
static_assert(gates_bound());
static_assert(W::fixup_target(W::crypt_windows, W::crypt_fixups, "signer_head", 0x0f7ca0 - signer_rva) ==
              imports[acquire_index].slot_rva);
static_assert(W::fixup_target(W::crypt_windows, W::crypt_fixups, "checker", 0x0f8bac - checker_rva) ==
              provider_string_rva);
static_assert(W::fixup_target(W::crypt_windows, W::crypt_fixups, "checker", 0x0f8bb7 - checker_rva) ==
              container_string_rva);
}
