// Host driver for src/renderer/shader_population.{h,cpp}, built natively by
// verification/analysis/test_shader_population.py. No Wine, no D3D, no game
// bytes: it links the real production tables and answers line commands.
//
//   tables                          -> "table <name> <count>" per table, then "tables <n> entries <n>"
//   dump                            -> "hash <table index> <hex>" for every entry
//   known <hex>                     -> "known <0|1>"
//   observe <hex> <vs|ps> <version hex> <bytes>
//                                   -> "observe <recorded> <known> <unknown> <overflow>"
//   take                            -> "take 0" or "take 1 <hex> <version> <bytes> <vs|ps>"
//   changed                         -> "changed <0|1>"
//   fade <vs hex> <ps hex>          -> "fade <sampler mask>"
#include "shader_population.h"
#include "linear_distance_fade.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>

using namespace x3m::renderer;

int main() {
    ShaderPopulation population;
    char command[64];
    while (std::scanf("%63s", command) == 1) {
        if (!std::strcmp(command, "tables")) {
            const std::size_t count = shader_table_count();
            for (std::size_t t = 0; t < count; ++t) {
                const ShaderTable& table = shader_table(t);
                std::printf("table %s %zu\n", table.name, table.count);
            }
            std::printf("tables %zu entries %zu\n", count, shader_table_entry_count());
        } else if (!std::strcmp(command, "dump")) {
            const std::size_t count = shader_table_count();
            for (std::size_t t = 0; t < count; ++t) {
                const ShaderTable& table = shader_table(t);
                for (std::size_t i = 0; i < table.count; ++i)
                    std::printf("hash %zu %016" PRIx64 "\n", t, table.at(i));
            }
        } else if (!std::strcmp(command, "known")) {
            std::uint64_t hash = 0;
            if (std::scanf("%" SCNx64, &hash) != 1) return 1;
            std::printf("known %d\n", shader_hash_known(hash) ? 1 : 0);
        } else if (!std::strcmp(command, "observe")) {
            std::uint64_t hash = 0; char kind[8]; unsigned version = 0, bytes = 0;
            if (std::scanf("%" SCNx64 " %7s %x %u", &hash, kind, &version, &bytes) != 4) return 1;
            const bool recorded = population.observe(hash, kind[0] == 'v', version, bytes);
            std::printf("observe %d %u %u %u\n", recorded ? 1 : 0, population.known(),
                        population.unknown(), population.overflow());
        } else if (!std::strcmp(command, "take")) {
            ShaderPopulation::Entry entry{};
            if (population.take(entry))
                std::printf("take 1 %016" PRIx64 " %u %u %s\n", entry.hash, entry.version,
                            entry.bytes, entry.vertex ? "vs" : "ps");
            else std::printf("take 0\n");
        } else if (!std::strcmp(command, "changed")) {
            std::printf("changed %d\n", population.counts_changed() ? 1 : 0);
        } else if (!std::strcmp(command, "fade")) {
            std::uint64_t vs = 0, ps = 0;
            if (std::scanf("%" SCNx64 " %" SCNx64, &vs, &ps) != 2) return 1;
            std::printf("fade %u\n", linear_distance_fade_sampler_mask(vs, ps));
        } else return 2;
        std::fflush(stdout);
    }
    return 0;
}
