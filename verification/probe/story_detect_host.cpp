// The run-time story detector (src/proxy/story_detect.h) over a KC VM image built from an object on disk by
// verification/probe/story_vm_image.py: prints one `seta ...` and one `queue ...` row of key=value fields. The reader
// serves a read only when it lies wholly inside one segment (a fault elsewhere, as the engine reader refuses), and
// counts the reads. An optional second argument replaces the detection's read budget (story_detect.h
// detection_read_budget). No Windows, no Wine.
#include "story_detect.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
struct Segment {
    std::uint32_t at, size;
    std::vector<unsigned char> bytes;
};
std::vector<Segment> segments;
unsigned long reads = 0;
struct Reader {
    bool operator()(std::uint32_t at, void* out, unsigned n) const {
        ++reads;
        for (const Segment& s : segments)
            if (at >= s.at && n <= s.size && at - s.at <= s.size - n) {
                std::memcpy(out, s.bytes.data() + (at - s.at), n);
                return true;
            }
        return false;
    }
};
std::uint32_t u32(const std::vector<unsigned char>& b, std::size_t at) {
    return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 | std::uint32_t(b[at + 3]) << 24;
}
void hex(const unsigned char* p, unsigned n, char* out) {
    for (unsigned i = 0; i < n; ++i) std::snprintf(out + 2 * i, 3, "%02x", p[i]);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "usage: story_detect_host IMAGE [READ_BUDGET]\n");
        return 2;
    }
    const unsigned budget = argc == 3 ? unsigned(std::strtoul(argv[2], nullptr, 0)) : x3m::story::detection_read_budget;
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 2;
    std::vector<unsigned char> file;
    unsigned char chunk[65536];
    for (std::size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) file.insert(file.end(), chunk, chunk + n);
    std::fclose(f);
    if (file.size() < 12 || std::memcmp(file.data(), "X3VM", 4) != 0) return 2;
    const std::uint32_t vm = u32(file, 4), count = u32(file, 8);
    std::size_t p = 12;
    for (std::uint32_t i = 0; i < count && p + 8 <= file.size(); ++i) {
        Segment s{u32(file, p), u32(file, p + 4), {}};
        p += 8;
        if (p + s.size > file.size()) return 2;
        s.bytes.assign(file.begin() + std::ptrdiff_t(p), file.begin() + std::ptrdiff_t(p + s.size));
        p += s.size;
        segments.push_back(std::move(s));
    }
    static unsigned char scratch[x3m::story::scratch_size];
    Reader rd;
    x3m::story::SetaSites seta;
    reads = 0;
    x3m::story::detect_seta(rd, vm, seta, scratch, budget);
    const unsigned long seta_reads = reads;
    std::printf("seta fail=%s where=%s stop_ip=0x%05x gate_return=0x%05x reads=%lu", x3m::story::fail_name(seta.outcome.fail),
                seta.outcome.where, unsigned(seta.stop_ip), unsigned(seta.gate_return), seta_reads);
    for (unsigned i = 0; i < x3m::story::SetaSites::named_count; ++i)
        std::printf(" %s=0x%05x-0x%05x", x3m::story::seta_named[i], unsigned(seta.named[i].entry), unsigned(seta.named[i].end));
    std::printf(" name_gate=%s name_bottom=%s\n", x3m::story::seta_return_name(&seta, seta.gate_return, false),
                x3m::story::seta_return_name(&seta, seta.gate_return, true));
    x3m::story::QueueSites q;
    reads = 0;
    x3m::story::detect_queue(rd, vm, q, scratch, budget);
    char window[13]{}, patch[13]{};
    hex(q.warp_window_memory, x3m::story::warp_window_length, window);
    hex(q.warp_patch_memory, x3m::story::warp_window_length, patch);
    std::printf("queue fail=%s where=%s build=%u warp_entry=0x%05x fork=0x%05x cont=0x%05x window=%s patch=%s play_stop=0x%05x "
                "fade_stop=0x%05x play=0x%05x loop_return=0x%05x change_return=0x%05x depth=%u reads=%lu\n",
                x3m::story::fail_name(q.outcome.fail), q.outcome.where, unsigned(q.key.build), unsigned(q.warp_method_entry),
                unsigned(q.warp_fork_offset), unsigned(q.warp_continue_offset), window, patch, unsigned(q.kc_play_stop_ip),
                unsigned(q.kc_fade_stop_ip), unsigned(q.kc_play_ip), unsigned(q.kc_loop_music_return),
                unsigned(q.kc_change_track_return), q.queue_stack_depth, reads);
    return 0;
}
