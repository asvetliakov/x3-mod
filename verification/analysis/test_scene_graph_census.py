"""Scene-graph census row (src/proxy/scene_graph_census_core.h; docs/reverse-engineering/object-lifetimes.md, "Run382").

The core compiled with the host compiler against a synthetic render manager (a fake reader over a sparse address map):
the four table counts and their refusal, the unattached-list walk newest first over prev links (length, sampled model-id histogram and top 8
with -1 as a key, cycle, unreadable link, foreign head, walk bound, time budget), the body names of the top ids, and the insert-caller
resolution (auto-id frame, ESP and EBP rules, unknown sites, refused reads) with its 32-slot table. The engine
sites the core relies on are pinned against the installed EXE when present. The production row sits under the
--perf/--debug 300-frame gate (test_logging_tiers.py). No Wine, no game.
"""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_chase_aim_sites as common  # noqa: E402
from verification.analysis.test_chase_aim_sites import synthetic_image  # noqa: E402

CORE = ROOT / 'src/proxy/scene_graph_census_core.h'

HARNESS = r'''
#include "scene_graph_census_core.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
using namespace x3m::scene_graph_census::core;
// Sparse 32-bit address space: 4 KB pages, a read fails unless every byte is on a present page.
struct Space {
    std::map<std::uint32_t, std::vector<unsigned char>> pages;
    unsigned reads = 0;
    unsigned char* at(std::uint32_t a) {
        auto& p = pages[a & ~0xfffu];
        if (p.empty()) p.assign(0x1000, 0);
        return &p[a & 0xfff];
    }
    void put32(std::uint32_t a, std::uint32_t v) { for (int i = 0; i < 4; ++i) *at(a + i) = (unsigned char)(v >> (8 * i)); }
    void put_text(std::uint32_t a, const char* s) { do { *at(a++) = (unsigned char)*s; } while (*s++); }
    void drop(std::uint32_t a) { pages.erase(a & ~0xfffu); }
    bool operator()(std::uintptr_t a, void* out, std::size_t n) {
        ++reads;
        for (std::size_t i = 0; i < n; ++i) {
            auto it = pages.find(std::uint32_t(a + i) & ~0xfffu);
            if (it == pages.end()) return false;
            static_cast<unsigned char*>(out)[i] = it->second[(a + i) & 0xfff];
        }
        return true;
    }
};
constexpr std::uint32_t G = 0x00608518, V = 0x006085e4, R = 0x10000000, VM = 0x11000000, NODES = 0x20000000;
std::uint32_t node_at(unsigned i) { return NODES + i * 0x270; }
// A manager with tables, a body table (ids 5, 7 named, 9 null name) and an unattached list of n nodes.
void build(Space& s, unsigned n, const int* ids, unsigned id_count) {
    s.put32(G, R); s.put32(V, VM);
    const std::uint32_t tables[] = {0x10100000, 0x10200000, 0x10300000, 0x11100000};
    s.put32(R + 0x0c, tables[0]); s.put32(R + 0x10, tables[1]); s.put32(R + 0x84, tables[2]); s.put32(VM + 0x00, tables[3]);
    const std::uint32_t counts[] = {123456, 17, 950, 4321};
    for (int t = 0; t < 4; ++t) { s.put32(tables[t] + 4, 4096); s.put32(tables[t] + 0xc, counts[t]); }
    s.put32(R + 0xb4, 11000); s.put32(R + 0xb8, 12); s.put32(R + 0xbc, 0x12000000);
    s.put32(0x12000000 + 5 * 0x1c + 0x0c, 0x12100000); s.put_text(0x12100000, "objects\\ships\\argon_m5");
    s.put32(0x12000000 + 7 * 0x1c + 0x0c, 0x12100100); s.put_text(0x12100100, "objects\\effects\\bolt one");
    s.put32(0x12000000 + 9 * 0x1c + 0x0c, 0);
    std::uint32_t prev = R + 0x28;
    s.put32(R + 0x28, n ? node_at(0) : R + 0x2c); s.put32(R + 0x2c, 0); s.put32(R + 0x30, n ? node_at(n - 1) : R + 0x28);
    for (unsigned i = 0; i < n; ++i) {
        s.put32(node_at(i), i + 1 < n ? node_at(i + 1) : R + 0x2c);
        s.put32(node_at(i) + 4, prev);
        s.put32(node_at(i) + 0x140, std::uint32_t(ids[i % id_count]));
        prev = node_at(i);
    }
}
int main() {
    unsigned failures = 0, checks = 0;
    auto check = [&](bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    auto never = [] { return false; };
    static HistogramSlot table[histogram_slots];
    const int mix[] = {5, 5, 5, 7, 7, 9, 5, 7, 5, -1};   // 5: 5x, 7: 3x, 9: 1x, -1 (no model): 1x per 10
    {
        Space s; build(s, 1000, mix, 10);
        const Counts c = read_counts(s, G, V);
        check(c.manager && c.manager_address == R && c.nodes.valid && c.nodes.value == 123456 && c.scenes.value == 17 &&
              c.cuts.value == 950 && c.cut_buckets.valid && c.cut_buckets.value == 4096 && c.tasks.valid && c.tasks.value == 4321,
              "counts: the four +0xc entry counts and the cut bucket count");
        const Walk w = walk_unattached(s, R, 1, table, never);
        check(w.length == 1000 && w.sampled == 1000 && !w.truncated && !w.cycle && !w.bounded && !w.capped && w.distinct == 4 && w.other == 0,
              "walk: 1000 nodes, every model read, clean end on the tail slot");
        TopEntry top[top_count]{};
        const unsigned n = histogram_top(table, top);
        check(n == 4 && top[0].id == 5 && top[0].count == 500 && top[1].id == 7 && top[1].count == 300 && top[2].id == -1 &&
              top[2].count == 100 && top[3].id == 9 && top[3].count == 100, "histogram: ids by count, ties by id, -1 a key");
        char name[name_size];
        body_name(s, R, 5, name); check(!std::strcmp(name, "objects\\ships\\argon_m5"), "body: named slot");
        body_name(s, R, 7, name); check(!std::strcmp(name, "objects\\effects\\bolt?one"), "body: space shown as ?");
        body_name(s, R, -1, name); check(!std::strcmp(name, "model-1"), "body: -1 (no model) -> model-1");
        body_name(s, R, 9, name); check(!std::strcmp(name, "v\\00009"), "body: null name pointer -> the engine default");
        body_name(s, R, 11, name); check(!std::strcmp(name, "v\\00011"), "body: zero slot -> the engine default");
        body_name(s, R, 20011, name); check(!std::strcmp(name, "model20011"), "body: unreadable slot -> model<id>");
        body_name(s, R, 20012, name); check(!std::strcmp(name, "model20012"), "body: slot >= fixed + dynamic -> model<id>");
        body_name(s, R, -3, name); check(!std::strcmp(name, "model-3"), "body: negative id -> model<id>");
        const unsigned before = s.reads;
        const Walk sampled = walk_unattached(s, R, 64, table, never);
        check(sampled.length == 1000 && sampled.sampled == 16 && s.reads - before == 1 + 1001 + 16,
              "sampling: every 64th model id (16 of 1000); one read per link, the header and the tail");
        const Walk bound = walk_unattached(s, R, 1, table, never, 400);
        check(bound.length == 400 && bound.bounded && !bound.truncated, "bound: stops at the bound with bounded=1");
    }
    {
        Space s; build(s, 1000, mix, 10);
        s.put32(node_at(300) + 4, node_at(999));   // walking newest first, node 300 links back to the tail
        const Walk w = walk_unattached(s, R, 1, table, never);
        check(w.cycle && !w.truncated && !w.bounded && w.length >= 700 && w.length <= 4 * 1000, "cycle: Brent detects a loop of 700 within 4n");
        s.put32(node_at(999) + 4, node_at(999));
        const Walk self = walk_unattached(s, R, 1, table, never);
        check(self.cycle && self.length == 1, "cycle: a self-link");
    }
    {
        Space s; build(s, 1000, mix, 10);
        s.put32(node_at(500) + 4, 0x30000000); // nodes 999..500 readable, then a link to an unmapped page
        const Walk w = walk_unattached(s, R, 1, table, never);
        check(w.truncated && w.length == 500 && !w.cycle, "unreadable link: truncated=1 after the 500 readable nodes");
        s.put32(0x30000000 + 4, 0);             // a zero prev that is not the list's own head slot
        const Walk foreign = walk_unattached(s, R, 1, table, never);
        check(foreign.truncated && foreign.length == 500, "foreign head: a zero prev outside R+0x28 is truncated");
        s.put32(node_at(500) + 4, 0);           // a null link on a node
        const Walk nulled = walk_unattached(s, R, 1, table, never);
        check(nulled.length == 499 && nulled.truncated, "null prev on a node: the end, not the head slot -> truncated");
    }
    {
        Space s; build(s, 1000, mix, 10);
        s.drop(node_at(700) + 0x140);           // the page holding a model id (and a few links)
        const Walk w = walk_unattached(s, R, 1, table, never);
        check(w.truncated && w.length < 300 && w.length > 280, "unreadable page ends the walk with truncated=1");
    }
    {
        Space s; build(s, 20000, mix, 10);
        unsigned asked = 0;
        const Walk w = walk_unattached(s, R, 64, table, [&] { return ++asked == 2; });
        check(w.capped && w.length == 2 * budget_check_interval && asked == 2, "budget: asked every 1024 nodes, capped=1 when expired");
        const Walk full = walk_unattached(s, R, 64, table, never);
        check(full.length == 20000 && !full.capped && full.sampled == (20000 + 63) / 64, "20000 nodes, stride 64");
    }
    {
        Space s; build(s, 0, mix, 10);
        const Walk w = walk_unattached(s, R, 1, table, never);
        check(w.length == 0 && !w.truncated, "empty list: head is the tail slot");
        s.put32(0x10300000 + 0xc, 20000000); s.put32(R + 0x10, 0);
        const Counts c = read_counts(s, G, V);
        check(!c.cuts.valid && !c.scenes.valid && c.nodes.valid, "refused counts: above 10^7 and a null table");
        s.put32(G, 0);
        const Counts none = read_counts(s, G, V);
        check(!none.manager && !none.nodes.valid && none.tasks.valid, "no manager: no manager counts, tasks still read");
        const Walk nomgr = walk_unattached(s, 0, 1, table, never);
        check(nomgr.truncated && nomgr.length == 0, "no manager: walk refused");
    }
    {
        // Histogram overflow: more distinct ids than the table holds go to other=.
        Space s;
        std::vector<int> ids(3000);
        for (unsigned i = 0; i < ids.size(); ++i) ids[i] = int(20000 + i);
        build(s, 3000, ids.data(), 3000);
        const Walk w = walk_unattached(s, R, 1, table, never);
        check(w.sampled == 3000 && w.distinct <= histogram_slots && w.distinct + w.other == 3000 && w.other >= 3000 - histogram_slots,
              "histogram overflow: counted in other, no slot over-filled");
    }
    {
        // Insert callers. esp0 = the stack slot of the hooked call's return address.
        Space s;
        const std::uint32_t esp0 = 0x0012f000;
        s.put32(esp0, autoid_insert_return); s.put32(esp0 + 24, 0x00486d7d); s.put32(esp0 + 24 + 28, 0x00401234);
        std::uint32_t site = 0, caller = 0;
        resolve_insert_caller(s, esp0, autoid_insert_return, 0, caller_rules, caller_rule_count, &site, &caller);
        check(site == 0x00486d7d && caller == 0x00401234, "auto-id path: allocator site and its return at frame+28 (0x00486d10)");
        s.put32(esp0 + 24, 0x0048860a); s.put32(esp0 + 24 + 20, 0x00405678);
        resolve_insert_caller(s, esp0, autoid_insert_return, 0, caller_rules, caller_rule_count, &site, &caller);
        check(site == 0x0048860a && caller == 0x00405678, "auto-id path: 0x004885a0 frame+20");
        s.put32(esp0 + 24, 0x00488cdb);
        resolve_insert_caller(s, esp0, autoid_insert_return, 0, caller_rules, caller_rule_count, &site, &caller);
        check(site == 0x00488cdb && caller == 0x00405678, "auto-id path: 0x00488c70 frame+20");
        s.put32(0x0012f800 + 4, 0x00409abc);
        resolve_insert_caller(s, esp0, 0x0047a6b2, 0x0012f800, caller_rules, caller_rule_count, &site, &caller);
        check(site == 0x0047a6b2 && caller == 0x00409abc, "direct path: 0x00479d10 EBP frame [ebp+4]");
        resolve_insert_caller(s, esp0, 0x00412345, 0x0012f800, caller_rules, caller_rule_count, &site, &caller);
        check(site == 0x00412345 && caller == 0, "unknown site: caller 0");
        resolve_insert_caller(s, 0x7ff00000, autoid_insert_return, 0, caller_rules, caller_rule_count, &site, &caller);
        check(site == autoid_insert_return && caller == 0, "unreadable auto-id frame: the site is the auto-id return, caller 0");
        resolve_insert_caller(s, esp0, 0x0047a6b2, 0x7ff00000, caller_rules, caller_rule_count, &site, &caller);
        check(site == 0x0047a6b2 && caller == 0, "unreadable EBP frame: caller 0");
        const unsigned before = s.reads;
        resolve_insert_caller(s, esp0, 0x00412345, 0, caller_rules, caller_rule_count, &site, &caller);
        const unsigned direct = s.reads - before;
        resolve_insert_caller(s, esp0, autoid_insert_return, 0, caller_rules, caller_rule_count, &site, &caller);
        check(direct == 0 && s.reads - before == 2, "reads per insert: 0 on an unknown direct site, 2 on the auto-id path");
        static CallerTable t{};
        for (unsigned i = 0; i < 40; ++i) caller_add(t, 0x1000 + i, 7);
        for (unsigned i = 0; i < 5; ++i) caller_add(t, 0x1003, 7);
        for (unsigned i = 0; i < 3; ++i) caller_add(t, 0x1003, 8);
        caller_sort(t);
        check(t.total == 48 && t.used == caller_slots && t.dropped == 8 + 3, "table: 32 pairs, the rest dropped");
        check(t.slots[0].site == 0x1003 && t.slots[0].caller == 7 && t.slots[0].count == 6, "table: sorted by count");
    }
    std::printf("SCENE GRAPH CENSUS HOST checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
'''

# (va, bytes, instruction) in the installed EXE: the reads the census makes and the frames the caller rules assume.
PATTERNS = (
    # 0x0048f550 pass A: R+0x28 is an embedded list header; node +0 = next; the walk ends on a zero next.
    (0x48f550, 'a118856000', 'mov eax,[0x00608518]'),
    (0x48f557, '8b7028', 'mov esi,[eax+0x28]'),
    (0x48f55c, '391e', 'cmp [esi],ebx (ebx = 0)'),
    (0x48f56e, '8b36', 'mov esi,[esi]'),
    # the hash table count field: insert add [tbl+0xc],1 / remove add [tbl+0xc],-1
    (0x4efc9e, '83470c01', 'add dword [edi+0xc],1'),
    (0x4efd8f, '83470cff', 'add dword [edi+0xc],-1'),
    # auto-id register 0x004efcc0: push ebx/esi/edi, then two pushes and the insert call returning to 0x004efd0e
    (0x4efcc0, '535657', 'push ebx; push esi; push edi'),
    (0x4efd03, '8b4c241051 56e8e2feffff'.replace(' ', ''), 'mov ecx,[esp+0x10]; push ecx; push esi; call 0x004efbf0'),
    # 0x00486d10: push ecx/ebx/ebp/esi/edi; R+0xc; push ebx; call 0x004efcc0 (return 0x00486d7d)
    (0x486d10, '5153555657', 'push ecx; push ebx; push ebp; push esi; push edi'),
    (0x486d6f, 'a1188560008b400c53e8438f0600', 'mov eax,[R]; mov eax,[eax+0xc]; push ebx; call 0x004efcc0'),
    # 0x004885a0: push ecx/esi/edi; push edi; call 0x004efcc0 (return 0x0048860a)
    (0x4885a0, '515657', 'push ecx; push esi; push edi'),
    (0x4885fc, 'a1188560008b400c57e8b6760600', 'mov eax,[R]; mov eax,[eax+0xc]; push edi; call 0x004efcc0'),
    # 0x00488c70: push ebx/esi/edi; push edi; call 0x004efcc0 (return 0x00488cdb)
    (0x488c70, '535657', 'push ebx; push esi; push edi'),
    (0x488ccd, 'a1188560008b400c57e8e56f0600', 'mov eax,[R]; mov eax,[eax+0xc]; push edi; call 0x004efcc0'),
    # 0x00479d10: EBP frame; direct insert on R+0xc at 0x0047a6ad (return 0x0047a6b2)
    (0x479d10, '558bec83e4f0', 'push ebp; mov ebp,esp; and esp,-16'),
    (0x47a6a2, '8b1518856000 8b7a0c 53 51 e83e550700'.replace(' ', ''), 'mov edx,[R]; mov edi,[edx+0xc]; push ebx; push ecx; call 0x004efbf0'),
)


def inspect(data):
    image = common.Image(data)
    return {va: image.read(va, len(bytes.fromhex(h))) == bytes.fromhex(h) for va, h, _ in PATTERNS}


def core_rules(text):
    """(return_site, via_ebp, offset) from caller_rules in the core header."""
    block = text[text.index('constexpr CallerRule caller_rules[] = {'):]
    block = block[:block.index('};')]
    rules = []
    for m in re.finditer(r'\{(0x[0-9a-fA-F]+), (true|false), ([0-9 +]+)\}', block):
        rules.append((int(m.group(1), 16), m.group(2) == 'true', eval(m.group(3))))  # noqa: S307 (digits and + only)
    return rules


class SceneGraphCensusCore(unittest.TestCase):
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
            self.assertRegex(run.stdout, r'SCENE GRAPH CENSUS HOST checks=\d+ failures=0')

    def test_rules_match_the_pinned_frames(self):
        rules = core_rules(CORE.read_text())
        self.assertEqual(rules, [(0x486d7d, False, 28), (0x48860a, False, 20), (0x488cdb, False, 20), (0x47a6b2, True, 4)])
        encoded = {va: bytes.fromhex(h) for va, h, _ in PATTERNS}
        # Each ESP rule's return site follows its pinned call of 0x004efcc0, and the offset is
        # 4 (the pushed node) + 4 per prologue push, over the called register's return slot.
        for site, prologue, call_block in ((0x486d7d, 0x486d10, 0x486d6f), (0x48860a, 0x4885a0, 0x4885fc), (0x488cdb, 0x488c70, 0x488ccd)):
            block = encoded[call_block]
            self.assertEqual(call_block + len(block), site)
            self.assertEqual(site + int.from_bytes(block[-4:], 'little', signed=True), 0x4efcc0)
            offset = dict((r[0], r[2]) for r in rules)[site]
            self.assertEqual(offset, 4 + 4 + 4 * len(encoded[prologue]))
        direct = encoded[0x47a6a2]
        self.assertEqual(0x47a6a2 + len(direct), 0x47a6b2)
        self.assertEqual(0x47a6b2 + int.from_bytes(direct[-4:], 'little', signed=True), 0x4efbf0)
        autoid = encoded[0x4efd03]
        self.assertEqual(0x4efd03 + len(autoid), int(re.search(r'autoid_insert_return = (0x[0-9a-f]+)', CORE.read_text()).group(1), 16))
        self.assertEqual(0x4efd0e + int.from_bytes(autoid[-4:], 'little', signed=True), 0x4efbf0)
        # return + two argument pushes + push ebx/esi/edi
        self.assertIn('constexpr unsigned autoid_return_offset = 24;', CORE.read_text())

    def test_changed_byte_is_caught(self):
        extra = [(va, bytes.fromhex(h)) for va, h, _ in PATTERNS]
        self.assertTrue(all(inspect(synthetic_image(extra=extra, text_size=0x100000)).values()))
        for va, h, _ in PATTERNS:
            raw = bytearray(bytes.fromhex(h))
            raw[-1] ^= 0x01
            changed = [(v, bytes(raw) if v == va else bytes.fromhex(x)) for v, x, _ in PATTERNS]
            result = inspect(synthetic_image(extra=changed, text_size=0x100000))
            self.assertFalse(result[va], hex(va))

    @unittest.skipUnless(common.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        import exe_identity
        data = common.image_bytes(common.DEFAULT_EXE)
        self.assertTrue(exe_identity.identity_ok(data))
        self.assertEqual([hex(va) for va, ok in inspect(data).items() if not ok], [])

    def test_row_is_wired_at_present(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('scene_graph_census::initialize(log_tier::cached_perf || log_tier::cached_debug);', capture)
        self.assertIn('scene_graph_census::report(ctx.id, ctx.frame);', capture)
        self.assertIn('src/proxy/scene_graph_census.cpp', (ROOT / 'CMakeLists.txt').read_text())


if __name__ == '__main__':
    unittest.main()
