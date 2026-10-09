"""address_space row (src/proxy/address_space_core.h, address_space.cpp; docs/architecture/logging-tiers.md).

The accounting core compiled with the host compiler against synthetic VirtualQuery region lists: the partition
free_total + reserved + committed_* == span, the 16 MiB private chunk class and its 64 KiB slack, the >= 1 MiB
big-private class, the clip at 0xFFFF0000, and the stop on a gap or an empty region. A row formatted like the
production one is parsed back and its invariants checked. The real VirtualQuery walk, LastError and the walk
cost are the X3 CPU fixture's (run_scene_graph_census.py, ADDRESS SPACE ROW). No Wine, no game.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPAN = 0xFFFF0000
MIB = 1 << 20

HARNESS = r'''
#include "address_space_core.h"
#include <cstdio>
using namespace x3m::address_space::core;
struct R { std::uint64_t size; std::uint32_t state, type; };
Totals walk(const R* rs, unsigned n) {
    Totals t{};
    std::uint64_t base = 0;
    for (unsigned i = 0; i < n; ++i) {
        const std::uint64_t next = add(t, base, rs[i].size, rs[i].state, rs[i].type);
        base += rs[i].size;
        if (!next) break;
    }
    return t;
}
void row(const Totals& t) {
    std::printf("address_space device=0 frame=0 when=host total_virtual=4294836224 avail_virtual=0 span=%llu regions=%llu "
                "free_total=%llu free_largest=%llu reserved=%llu committed_private=%llu committed_mapped=%llu "
                "committed_image=%llu committed_other=%llu chunk16_count=%llu chunk16_bytes=%llu big_private_count=%llu "
                "big_private_bytes=%llu capped=0 ticks=1 us=0\n",
                (unsigned long long)t.span, (unsigned long long)t.regions, (unsigned long long)t.free_total,
                (unsigned long long)t.free_largest, (unsigned long long)t.reserved, (unsigned long long)t.committed_private,
                (unsigned long long)t.committed_mapped, (unsigned long long)t.committed_image,
                (unsigned long long)t.committed_other, (unsigned long long)t.chunk16_count,
                (unsigned long long)t.chunk16_bytes, (unsigned long long)t.big_private_count,
                (unsigned long long)t.big_private_bytes);
}
int main() {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    const std::uint64_t K = 1024, M = K * K;
    // 0x10000 free, an image, a heap with a reserved tail, four chunk-class regions (exact, +64 KiB, +64 KiB+4 KiB,
    // 16 MiB - 4 KiB), a 4 MiB private, a mapped view, an unknown committed type, free to the top (clipped).
    const R rs[] = {
        {64 * K, mem_free, 0}, {4 * M, mem_commit, mem_image}, {1 * M, mem_commit, mem_private}, {3 * M, mem_reserve, mem_private},
        {16 * M, mem_commit, mem_private}, {16 * M + 64 * K, mem_commit, mem_private}, {16 * M + 68 * K, mem_commit, mem_private},
        {16 * M - 4 * K, mem_commit, mem_private}, {4 * K, mem_free, 0}, {4 * M, mem_commit, mem_private},
        {2 * M, mem_commit, mem_mapped}, {8 * K, mem_commit, 0x80000}, {0, 0, 0}};
    Totals t{};
    std::uint64_t base = 0;
    for (const R& r : rs) {
        if (!r.size) break;
        check(add(t, base, r.size, r.state, r.type) == base + r.size, "next address after each region");
        base += r.size;
    }
    check(add(t, base, 0x200000000ull, mem_free, 0) == 0, "the last region ends the walk");
    check(t.span == walk_end, "span clipped to 0xFFFF0000");
    check(t.free_total + t.reserved + t.committed_private + t.committed_mapped + t.committed_image + t.committed_other == t.span,
          "partition");
    check(t.chunk16_count == 2 && t.chunk16_bytes == 32 * M + 64 * K, "chunk16: 16 MiB and 16 MiB + 64 KiB only");
    check(t.big_private_count == 4 && t.big_private_bytes == 1 * M + 16 * M + 68 * K + 16 * M - 4 * K + 4 * M,
          "big private: >= 1 MiB outside the chunk class");
    check(t.reserved == 3 * M && t.committed_image == 4 * M && t.committed_mapped == 2 * M && t.committed_other == 8 * K,
          "reserved, image, mapped, other");
    check(t.free_largest == walk_end - base, "free_largest is the clipped top free run");
    check(t.regions == 13, "region count");
    row(t);
    // A gap (a region not starting at the covered end) or an empty region stops the walk; the partition holds on span.
    Totals g{};
    check(add(g, 0, 64 * K, mem_free, 0) == 64 * K, "first region");
    check(add(g, 128 * K, 64 * K, mem_free, 0) == 0 && g.span == 64 * K && g.regions == 1, "gap stops the walk");
    check(add(g, 64 * K, 0, mem_free, 0) == 0 && g.span == 64 * K, "empty region stops the walk");
    std::printf("ADDRESS SPACE HOST checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
'''


def parse(line):
    head, _, rest = line.partition(' ')
    assert head == 'address_space', line
    return {k: v for k, v in (kv.split('=', 1) for kv in rest.split())}


def invariants(row):
    n = {k: int(v) for k, v in row.items() if re.fullmatch(r'\d+', v)}
    committed = sum(n[k] for k in ('committed_private', 'committed_mapped', 'committed_image', 'committed_other'))
    return n['free_total'] + n['reserved'] + committed == n['span'], n['chunk16_bytes'] <= n['committed_private']


class AddressSpaceCore(unittest.TestCase):
    def test_core_harness(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            self.skipTest('no host C++ compiler')
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'harness'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr[-3000:])
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout[-3000:])
            self.assertRegex(run.stdout, r'ADDRESS SPACE HOST checks=\d+ failures=0')
            row = parse(next(line for line in run.stdout.splitlines() if line.startswith('address_space ')))
            self.assertEqual(int(row['span']), SPAN)
            self.assertEqual(invariants(row), (True, True))

    def test_row_format_matches_the_header(self):
        source = (ROOT / 'src/proxy/address_space.cpp').read_text()
        fmt = ''.join(re.findall(r'"([^"]*)"', source[source.index('log("address_space '):source.index('device, frame, when,')]))
        keys = re.findall(r'(\w+)=%', fmt)
        self.assertEqual(keys, ['device', 'frame', 'when', 'total_virtual', 'avail_virtual', 'span', 'regions', 'free_total',
                                'free_largest', 'reserved', 'committed_private', 'committed_mapped', 'committed_image',
                                'committed_other', 'chunk16_count', 'chunk16_bytes', 'big_private_count',
                                'big_private_bytes', 'capped', 'ticks', 'us'])
        header = (ROOT / 'src/proxy/address_space.h').read_text()
        for key in keys:
            self.assertIn(key + '=', header)
        # Both entry points save LastError first and restore it last; the Present row is emitted only when the pass
        # stops for a reason other than the tick budget, and present rows pass capped=false.
        for entry in ('void report_create(', 'bool tick('):
            body = source[source.index(entry):]
            body = body[:body.index('\n}\n')]
            self.assertIn('const DWORD error = GetLastError();', body.split('\n')[1])
            self.assertEqual(body.count('SetLastError(error);'), 1)
            self.assertGreater(body.index('SetLastError(error);'), body.index('emit('))
        self.assertIn('const bool emitted = stop != Stop::budget;', source)
        self.assertIn('emit("present", device, frame, pass_, false, pass_ticks_,', source)
        # Without a QPC frequency the walk is bounded by a region count instead of running unbounded.
        self.assertIn('if (budget_ticks ? (walked & 63u) == 0 && now() - started > budget_ticks : walked >= region_limit)',
                      source)
        self.assertEqual(source.count('tick_region_limit);'), 1)
        self.assertEqual(source.count('create_region_limit);'), 1)
        header = (ROOT / 'src/proxy/address_space.h').read_text()
        self.assertIn('constexpr unsigned long tick_budget_us = 2000, create_budget_us = 200000;', header)
        self.assertIn('constexpr unsigned tick_region_limit = 4096, create_region_limit = 409600;', header)

    def test_row_parser_reads_a_multi_tick_row(self):
        row = parse('address_space device=1 frame=6900 when=present total_virtual=4294836224 avail_virtual=0 '
                    'span=4294901760 regions=3 free_total=4294705152 free_largest=4294705152 reserved=65536 '
                    'committed_private=65536 committed_mapped=0 committed_image=65536 committed_other=0 chunk16_count=0 '
                    'chunk16_bytes=0 big_private_count=0 big_private_bytes=0 capped=0 ticks=23 us=44510')
        self.assertEqual((row['when'], row['ticks'], row['capped'], int(row['span'])), ('present', '23', '0', SPAN))
        self.assertEqual(invariants(row), (True, True))

    def test_row_is_wired(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('if ((log_tier::cached_perf || log_tier::cached_debug) && ctx.frame % 30 == 0)\n'
                      '        address_space::tick(ctx.id, ctx.frame);', capture)
        self.assertIn('if (SUCCEEDED(hr) && (log_tier::cached_perf || log_tier::cached_debug)) {\n'
                      '        const auto created = out && *out ? devices.find(*out) : devices.end();\n'
                      '        address_space::report_create(created != devices.end() ? created->second->id : 0, 0);', capture)
        self.assertLess(capture.index('hook_device(*out, p && p->hDeviceWindow'), capture.index('address_space::report_create('))
        self.assertEqual(capture.count('address_space::tick('), 1)
        self.assertEqual(capture.count('address_space::report_create('), 1)
        self.assertLess(capture.index('log("create_device_result'), capture.index('address_space::report_create('))
        self.assertIn('src/proxy/address_space.cpp', (ROOT / 'CMakeLists.txt').read_text())
        doc = (ROOT / 'docs/architecture/logging-tiers.md').read_text()
        row = next(line for line in doc.splitlines() if '`address_space` every 30 frames' in line)
        self.assertEqual(row.split(' | ')[3], 'perf')


if __name__ == '__main__':
    unittest.main()
