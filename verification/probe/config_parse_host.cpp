// Host harness of the x3m.ini parser (src/config/config_parse.h, docs/architecture/config-file.md): parses the file
// named on the command line with the DLL's own code and prints what the resolver would hold, one fact per line, for
// verification/analysis/test_config_schema.py. Built with the host compiler; no Windows header.
#include "../../src/config/config_parse.h"
#include <cstdio>
#include <cstring>

static char text[x3m::config::parse::file_limit + 1];

// resolve FILE|- PROFILE [NAME=VALUE ...] -- NAME ...: config::get's layers for a fake environment (the NAME=VALUE
// arguments) with the DLL's own below_environment: one "NAME=value@env|file|default|bare" or "NAME unset" line per
// query.
static int resolve(int argc, char** argv) {
    namespace parse = x3m::config::parse;
    const char* file = argv[0];
    const bool bare = !std::strcmp(argv[1], "bare"), none = !std::strcmp(argv[1], "none");
    static parse::Result result;
    if (!bare && !none && std::strcmp(file, "-")) {
        std::FILE* f = std::fopen(file, "rb");
        if (!f) {
            std::fprintf(stderr, "cannot open %s\n", file);
            return 2;
        }
        const std::size_t size = std::fread(text, 1, parse::file_limit, f);
        std::fclose(f);
        parse::parse(text, unsigned(size), result);
    }
    int k = 2;
    const char* env_name[64];
    const char* env_value[64];
    int env_count = 0;
    for (; k < argc && std::strcmp(argv[k], "--"); ++k) {
        char* eq = std::strchr(argv[k], '=');
        if (!eq || env_count == 64) return 2;
        *eq = 0;
        env_name[env_count] = argv[k];
        env_value[env_count++] = eq + 1;
    }
    const auto env_find = [&](const char* name) {
        for (int e = 0; e < env_count; ++e)
            if (!std::strcmp(env_name[e], name)) return e;
        return -1;
    };
    for (++k; k < argc; ++k) {
        const char* name = argv[k];
        const int e = env_find(name);
        if (e >= 0) {
            std::printf("%s=%s@env\n", name, env_value[e]);
            continue;
        }
        const int i = parse::find(name);
        if (i < 0) {
            std::printf("%s unset\n", name);
            continue;
        }
        const parse::Layer layer = parse::below_environment(
            i, bare, [&](int j) { return result.value[j] != nullptr; },
            [&](int j) { return env_find(x3m::config::schema::entries[j].env) >= 0; });
        if (layer == parse::Layer::file)
            std::printf("%s=%s@file\n", name, result.value[i]);
        else if (layer == parse::Layer::base)
            std::printf("%s=%s@%s\n", name, parse::base_value(i, bare), bare ? "bare" : "default");
        else
            std::printf("%s unset\n", name);
    }
    return 0;
}

int main(int argc, char** argv) {
    namespace parse = x3m::config::parse;
    namespace schema = x3m::config::schema;
    if (argc == 3 && !std::strcmp(argv[1], "find")) { // find X3M_NAME: the table index or -1
        std::printf("find %s %d\n", argv[2], parse::find(argv[2]));
        return 0;
    }
    if (argc >= 4 && !std::strcmp(argv[1], "resolve")) return resolve(argc - 2, argv + 2);
    if (argc != 2) {
        std::fprintf(
            stderr,
            "usage: config_parse_host FILE | find NAME | resolve FILE|- file|none|bare [NAME=VALUE...] -- NAME...\n");
        return 2;
    }
    std::FILE* file = std::fopen(argv[1], "rb");
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const std::size_t size = std::fread(text, 1, parse::file_limit, file);
    const bool larger = std::fgetc(file) != EOF;
    std::fclose(file);
    if (larger) {
        std::printf("refused size\n");
        return 0;
    }
    static parse::Result result;
    parse::parse(text, unsigned(size), result);
    std::printf("counts keys=%u unknown=%u invalid=%u duplicate=%u env_only=%u renamed=%u lines=%u more=%u\n",
                result.keys, result.unknown, result.invalid, result.duplicate, result.env_only, result.renamed,
                result.lines, result.more);
    for (unsigned i = 0; i < schema::entry_count; ++i)
        if (result.value[i])
            std::printf("value %s line=%u [%s]\n", schema::entries[i].key, result.line[i], result.value[i]);
    for (unsigned k = 0; k < result.issue_count; ++k) {
        const parse::Issue& issue = result.issues[k];
        std::printf("issue line=%u problem=%s key=[%s] value=[%s]\n", issue.line, parse::problem_name(issue.problem),
                    issue.key, issue.value);
    }
    // The table is sorted and every name is found at its own index (the resolver's binary search).
    for (unsigned i = 0; i < schema::entry_count; ++i)
        if (parse::find(schema::entries[i].env) != int(i) ||
            (i && parse::compare(schema::entries[i - 1].env, schema::entries[i].env) >= 0)) {
            std::printf("table_error %u\n", i);
            return 1;
        }
    return 0;
}
