"""Fixtures for tools/analysis/archive_user_runs.py on synthetic run docs."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import archive_user_runs as aur


OPEN_BLOCK = """**Run 20 (queued 2026-09-22): Run20 DLL `abc…`. Two sessions; both dry-runs verified.**

Session A, the open session.

```sh
env X3M_FIXTURE_BOTTLE=X3 /Users/x/x3run --direct --taa --open-run
```
"""

CLOSED_BLOCK = """**Run 19 completed as run207 (2026-09-21): crawl fixed.** Original instructions: **Run 19 (queued 2026-09-21): thin-region gate A/B.**

Session A (installed behaviour):

```sh
env X3M_FIXTURE_BOTTLE=X3 /Users/x/x3run --direct --taa --closed-run
```
"""

TRAILING = (
    "Run 20 is the only queued run. Run 19 returned **run207** (A: crawl fixed) and "
    "**run208** (B: gate on). Run 18 returned **run206** (lattice ground truth). "
    "Run 17 returned **run205** (flashes)."
)


def runs_doc(rows=12):
    table = ["| Run | Purpose | Sessions | Status |", "| --- | --- | ---: | --- |"]
    for n in range(1, rows + 1):
        table.append("| %d | Purpose %d | 0 | Completed as run%d |" % (n, n, 100 + n))
    table.append("| %d A | Split session | 3 | Completed |" % (rows + 1))
    return "\n".join(
        [
            "# Outstanding user gameplay runs",
            "",
            "Updated 2026-09-22. Only open runs keep their instructions here.",
            "",
        ]
        + table
        + [
            "",
            "Completed instructions are preserved in the archive.",
            "",
            CLOSED_BLOCK.strip(),
            "",
            OPEN_BLOCK.strip(),
            "",
            TRAILING,
            "",
        ]
    )


ARCHIVE_DOC = "\n".join(
    [
        "# Completed user runs: commands and instructions",
        "",
        "Provenance only.",
        "",
        "## Run 18 (completed 2026-09-21: run206)",
        "",
        "**Run 18 (queued 2026-09-21): moving-lattice ground truth.**",
        "",
    ]
)


class ArchiveUserRunsTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.runs = self.dir / 'user-runs.md'
        self.archive = self.dir / 'user-runs-completed.md'
        self.runs.write_text(runs_doc())
        self.archive.write_text(ARCHIVE_DOC)

    def run_tool(self, *extra):
        return aur.main([
            '--runs-file', str(self.runs),
            '--archive-file', str(self.archive),
            '--date', '2026-09-22',
            *extra,
        ])

    # --- table -------------------------------------------------------------
    def test_table_trim_keeps_n_highest_and_appends_rows_once(self):
        self.assertEqual(self.run_tool('--keep', '8'), 0)
        runs = self.runs.read_text()
        header, sep, end = aur.find_table(runs.split('\n'))
        rows = [r for r in runs.split('\n')[sep + 1:end] if r.startswith('|')]
        self.assertEqual(len(rows), 8)
        self.assertEqual(
            sorted(aur.row_run_number(r) for r in rows), [6, 7, 8, 9, 10, 11, 12, 13]
        )
        archive = self.archive.read_text()
        self.assertIn(aur.ARCHIVED_TABLE_HEADING, archive)
        self.assertIn('| 1 | Purpose 1 | 0 | Completed as run101 |', archive)
        # order preserved, no duplicates
        self.assertEqual(archive.count('| 5 | Purpose 5'), 1)
        moved = [
            aur.row_run_number(line)
            for line in archive.split('\n')
            if line.startswith('|') and aur.ROW_RE.match(line)
        ]
        self.assertEqual(moved, [1, 2, 3, 4, 5])

    def test_second_table_move_does_not_duplicate_existing_rows(self):
        self.run_tool('--keep', '8')
        # re-add an already archived row and archive again
        text = self.runs.read_text().replace(
            '| 6 | Purpose 6', '| 1 | Purpose 1 | 0 | Completed as run101 |\n| 6 | Purpose 6'
        )
        self.runs.write_text(text)
        self.run_tool('--keep', '8')
        archive = self.archive.read_text()
        self.assertEqual(archive.count('| 1 | Purpose 1'), 1)
        self.assertNotIn('| 1 | Purpose 1', self.runs.read_text())

    def test_moved_row_links_are_rewritten_to_resolve_from_the_archive(self):
        row = ('| 1 | See [cue](foo.md#run50-anchor), [arch](../architecture/x.md), '
               '[st](../status.md), [ext](https://e.org/a.md), [top](#local) | 0 | Done |')
        self.runs.write_text(runs_doc().replace('| 1 | Purpose 1 | 0 | Completed as run101 |', row))
        self.run_tool('--keep', '8')
        archive = self.archive.read_text()
        self.assertIn('[cue](../verification/foo.md#run50-anchor)', archive)
        self.assertIn('[arch](../architecture/x.md)', archive)
        self.assertIn('[st](../status.md)', archive)
        self.assertIn('[ext](https://e.org/a.md)', archive)
        self.assertIn('[top](#local)', archive)
        self.assertNotIn('](foo.md', archive)

    def test_links_inside_inline_code_spans_are_not_rewritten(self):
        row = ('| 1 | Quoted `[a](b.md)` and ``x `[c](d.md)` y``, real [r](e.md#h) | 0 | Done |')
        self.runs.write_text(runs_doc().replace('| 1 | Purpose 1 | 0 | Completed as run101 |', row))
        self.run_tool('--keep', '8')
        archive = self.archive.read_text()
        self.assertIn('Quoted `[a](b.md)` and ``x `[c](d.md)` y``, real [r](../verification/e.md#h)',
                      archive)

    def test_moved_block_and_paragraph_links_are_rewritten_outside_fences(self):
        block = CLOSED_BLOCK.strip().replace(
            'Session A (installed behaviour):',
            'Session A ([ledger](./motion-output.md#s)):',
        ).replace('--closed-run', '--closed-run # [x](keep.md)')
        text = runs_doc().replace(CLOSED_BLOCK.strip(), block).replace(
            '(flashes).', '(flashes, [note](media-cues.md#run17)).'
        )
        self.runs.write_text(text)
        self.run_tool()
        archive = self.archive.read_text()
        self.assertIn('[ledger](../verification/motion-output.md#s)', archive)
        self.assertIn('# [x](keep.md)', archive)
        self.assertIn('[note](../verification/media-cues.md#run17)', archive)

    # --- blocks ------------------------------------------------------------
    def test_closed_block_moves_and_open_block_is_untouched(self):
        before = self.runs.read_text()
        self.run_tool()
        runs = self.runs.read_text()
        self.assertNotIn('--closed-run', runs)
        self.assertIn(OPEN_BLOCK.strip(), runs)
        # the open block is byte for byte what it was, commands included
        start = before.index('**Run 20 (queued')
        block = before[start:before.index(TRAILING)].rstrip('\n')
        self.assertIn(block, runs)
        archive = self.archive.read_text()
        self.assertIn('## Run 19 (completed 2026-09-22: run207, run208)', archive)
        self.assertIn('--closed-run', archive)

    def test_block_with_existing_archive_heading_is_not_duplicated(self):
        text = self.runs.read_text().replace(
            CLOSED_BLOCK.strip(),
            '**Run 18 (queued 2026-09-21): moving-lattice ground truth.**\n\n'
            '```sh\nenv X3M_FIXTURE_BOTTLE=X3 /Users/x/x3run --old\n```',
        )
        self.runs.write_text(text)
        self.run_tool()
        self.assertNotIn('--old', self.runs.read_text())
        self.assertEqual(
            self.archive.read_text().count('## Run 18 (completed 2026-09-21: run206)'), 1
        )

    def test_block_detail_falls_back_to_the_block_own_run_numbers(self):
        self.runs.write_text(runs_doc().replace(TRAILING, 'Run 20 is the only queued run.'))
        self.run_tool()
        self.assertIn('## Run 19 (completed 2026-09-22: run207)', self.archive.read_text())

    def test_block_without_any_run_numbers_uses_see_table(self):
        text = runs_doc().replace(TRAILING, 'Run 20 is the only queued run.').replace(
            CLOSED_BLOCK.strip(), '**Run 19 (queued 2026-09-21): thin-region gate A/B.**'
        )
        self.runs.write_text(text)
        self.run_tool()
        self.assertIn('## Run 19 (completed 2026-09-22: see table)', self.archive.read_text())

    # --- trailing paragraph -------------------------------------------------
    def test_paragraph_keeps_queued_and_previous_run_only(self):
        self.run_tool()
        runs = self.runs.read_text()
        self.assertIn('Run 20 is the only queued run. Run 19 returned', runs)
        self.assertNotIn('Run 18 returned', runs)
        self.assertNotIn('Run 17 returned', runs)
        archive = self.archive.read_text()
        self.assertIn(aur.PARAGRAPH_HEADING, archive)
        self.assertIn('Run 18 returned **run206** (lattice ground truth).', archive)
        self.assertIn('Run 17 returned **run205** (flashes).', archive)

    def test_several_queued_runs_keep_their_blocks(self):
        text = runs_doc().replace(
            TRAILING, 'Runs 19 and 20 are the queued runs. Run 18 returned **run206**.'
        )
        self.runs.write_text(text)
        self.run_tool()
        runs = self.runs.read_text()
        self.assertIn('--closed-run', runs)
        self.assertIn('--open-run', runs)

    # --- invocation sentence, idempotence, exit codes -----------------------
    def test_invocation_sentence_added_once(self):
        self.run_tool()
        self.run_tool()
        runs = self.runs.read_text()
        self.assertEqual(runs.count(aur.INVOCATION_SENTENCE), 1)
        self.assertLess(runs.index(aur.INVOCATION_SENTENCE), runs.index('| Run |'))

    def test_idempotent(self):
        self.run_tool()
        runs_once, archive_once = self.runs.read_text(), self.archive.read_text()
        self.assertEqual(self.run_tool(), 0)
        self.assertEqual(self.runs.read_text(), runs_once)
        self.assertEqual(self.archive.read_text(), archive_once)

    def test_check_and_dry_run_exit_codes(self):
        self.assertEqual(self.run_tool('--check'), 2)
        self.assertEqual(self.runs.read_text(), runs_doc())
        self.assertEqual(self.run_tool('--dry-run'), 0)
        self.assertEqual(self.runs.read_text(), runs_doc())
        self.run_tool()
        self.assertEqual(self.run_tool('--check'), 0)

    def test_missing_table_reports_a_doc_error(self):
        self.runs.write_text('# Outstanding user gameplay runs\n\nNo table here.\n')
        self.assertEqual(self.run_tool(), 3)


if __name__ == '__main__':
    unittest.main()
