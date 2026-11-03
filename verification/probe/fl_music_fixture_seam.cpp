// The fault seam of the FL music family's native fixtures (fl_music_shim.h): one-shot read-back flips (after
// fl_music_fault_read_skip matching reads) and push_front failures, otherwise the real engine_patch calls. Linked into each fixture executable.
#include <windows.h>
#include <cstdint>
#include "../../src/proxy/engine_patch.h"
std::uintptr_t fl_music_fault_read_at = 0, fl_music_fault_push_at = 0;
unsigned fl_music_fault_read_count = 0, fl_music_fault_read_skip = 0, fl_music_faults_taken = 0;
namespace x3m::engine_patch {
bool fixture_read_code(std::uintptr_t address, unsigned char* out, unsigned count) {
    const bool ok = read_code(address, out, count);
    if (ok && fl_music_fault_read_at && address == fl_music_fault_read_at && count == fl_music_fault_read_count) {
        if (fl_music_fault_read_skip) { // a matching read before the one to flip (a site's own expected-bytes read)
            --fl_music_fault_read_skip;
            return ok;
        }
        fl_music_fault_read_at = 0;
        ++fl_music_faults_taken;
        out[0] ^= 0xff;
    }
    return ok;
}
void* fixture_push_front(Site& site, void* stub) {
    if (fl_music_fault_push_at && site.spec.address == fl_music_fault_push_at) {
        fl_music_fault_push_at = 0;
        ++fl_music_faults_taken;
        return nullptr;
    }
    return push_front(site, stub);
}
}
