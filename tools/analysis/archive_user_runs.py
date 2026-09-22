#!/usr/bin/env python3
"""Move completed user-run material out of docs/verification/user-runs.md.

`docs/verification/user-runs.md` is meant to hold only the open runs and the
completed-run table (AGENTS.md, Documentation). Three things grow without
bound and are moved here into `docs/archive/user-runs-completed.md`:

1. table rows older than the `--keep` highest run numbers, appended unchanged
   to `## Completed-run table (archived rows)`;
2. `**Run N (queued ...)**` instruction blocks for runs that are no longer
   named as queued in the trailing paragraph, appended under
   `## Run N (completed <date>: ...)`;
3. the tail of the trailing paragraph beyond the queued sentence and the
   sentences about the immediately previous run, appended under
   `## Run history paragraphs`.

Moved text is unchanged except for relative Markdown link targets, which are
rewritten to resolve from docs/archive/ (`rewrite_links`). The block of the
open (queued) run and its commands are never touched. Running the tool twice
changes nothing.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import re
import sys
from pathlib import Path

TABLE_HEADER_RE = re.compile(r"^\|\s*Run\s*\|")
TABLE_SEP_RE = re.compile(r"^\|\s*-+\s*\|")
ROW_RE = re.compile(r"^\|\s*(\d+)")
BLOCK_START_RE = re.compile(r"^\*\*Run\s+(\d+)\b")
QUEUED_PARAGRAPH_RE = re.compile(
    r"^Runs?\s+\d[^.]*\bqueued\s+runs?\b", re.IGNORECASE
)
SENTENCE_RUN_RE = re.compile(r"(?:\A|(?<=\.\s))Run\s+(\d+)\b")
RUN_TOKEN_RE = re.compile(r"\brun\d+\b")

ARCHIVED_TABLE_HEADING = "## Completed-run table (archived rows)"
PARAGRAPH_HEADING = "## Run history paragraphs"
TABLE_COLUMNS = ("| Run | Purpose | Sessions | Status |", "| --- | --- | ---: | --- |")

INVOCATION_SENTENCE = "Archive with `python3 tools/analysis/archive_user_runs.py`"

# Inline Markdown link or image target: `](target)` or `](target "title")`.
LINK_TARGET_RE = re.compile(r"(\]\()([^)\s]+)((?:\s+\"[^\"]*\")?\))")
SCHEME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
# Inline code span: a backtick run closed by a run of the same length.
CODE_SPAN_RE = re.compile(r"(?<!`)(`+)(?!`).*?(?<!`)\1(?!`)")
# user-runs.md lives in docs/verification/, the archive in docs/archive/.
SOURCE_DIR_FROM_ARCHIVE = "../verification/"


class DocError(RuntimeError):
    pass


# --- parsing -----------------------------------------------------------------


def _fence_map(lines):
    """Return a list of booleans: True when the line sits inside a code fence."""
    inside = False
    out = []
    for line in lines:
        if line.startswith("```"):
            out.append(True)
            inside = not inside
            continue
        out.append(inside)
    return out


def find_table(lines):
    """Return (header_index, separator_index, end_index) of the completed table.

    `end_index` is exclusive and points just past the last table line.
    """
    for i, line in enumerate(lines):
        if TABLE_HEADER_RE.match(line):
            if i + 1 >= len(lines) or not TABLE_SEP_RE.match(lines[i + 1]):
                raise DocError("completed-run table has no separator row")
            end = i + 2
            j = end
            while j < len(lines):
                if lines[j].startswith("|"):
                    end = j + 1
                elif lines[j].strip() == "":
                    pass  # stray blank line inside the table
                else:
                    break
                j += 1
            return i, i + 1, end
    raise DocError("no completed-run table found")


def row_run_number(row):
    match = ROW_RE.match(row)
    if not match:
        raise DocError("table row without a run number: %r" % row[:60])
    return int(match.group(1))


def row_key(row):
    """Identity of a table row: its first cell ('54 A', '57 A/B/C', '12')."""
    return row.split("|")[1].strip()


def find_trailing_paragraph(lines):
    """Index of the trailing 'Run N is the only queued run' paragraph, or None."""
    fences = _fence_map(lines)
    for i in range(len(lines) - 1, -1, -1):
        if fences[i] or lines[i].startswith("```"):
            continue
        if QUEUED_PARAGRAPH_RE.match(lines[i]):
            return i
    return None


def split_paragraph(paragraph):
    """Split the trailing paragraph into run-labelled segments.

    Returns a list of (run_number, text) in document order; the first segment
    is the queued sentence.
    """
    starts = [(m.start(), int(m.group(1))) for m in SENTENCE_RUN_RE.finditer(paragraph)]
    if not starts or starts[0][0] != 0:
        return [(None, paragraph)]
    segments = []
    for idx, (pos, run) in enumerate(starts):
        end = starts[idx + 1][0] if idx + 1 < len(starts) else len(paragraph)
        segments.append((run, paragraph[pos:end].rstrip()))
    return segments


def queued_runs(paragraph):
    first = paragraph.split(". ")[0]
    return sorted(int(n) for n in re.findall(r"\b(\d+)\b", first))


def find_blocks(lines, table_end, trailing_index):
    """Return [(run, start, end)] for each `**Run N ...**` instruction block."""
    limit = trailing_index if trailing_index is not None else len(lines)
    fences = _fence_map(lines)
    starts = []
    for i in range(table_end, limit):
        if fences[i]:
            continue
        match = BLOCK_START_RE.match(lines[i])
        if match:
            starts.append((i, int(match.group(1))))
    blocks = []
    for idx, (start, run) in enumerate(starts):
        end = starts[idx + 1][0] if idx + 1 < len(starts) else limit
        while end > start and lines[end - 1].strip() == "":
            end -= 1
        text = "\n".join(lines[start:end])
        if "(queued" not in text:
            continue
        blocks.append((run, start, end))
    return blocks


# --- link rewriting ----------------------------------------------------------


def rewrite_target(target):
    """Rewrite one link target written for docs/verification/ to docs/archive/.

    `foo.md#a` and `./foo.md` become `../verification/foo.md#a`; targets that
    start with `../` already resolve the same from both sibling directories
    and stay; bare `#anchor`, absolute paths and URLs (any scheme) stay.
    """
    if not target or target.startswith(("#", "/", "../")) or SCHEME_RE.match(target):
        return target
    while target.startswith("./"):
        target = target[2:]
    return SOURCE_DIR_FROM_ARCHIVE + target


def rewrite_links(line):
    """Rewrite inline link targets in `line` outside inline code spans.

    Fenced code is the caller's job.
    """
    out = []
    pos = 0
    for span in CODE_SPAN_RE.finditer(line):
        out.append(_rewrite_plain(line[pos:span.start()]))
        out.append(span.group(0))
        pos = span.end()
    out.append(_rewrite_plain(line[pos:]))
    return "".join(out)


def _rewrite_plain(text):
    return LINK_TARGET_RE.sub(
        lambda m: m.group(1) + rewrite_target(m.group(2)) + m.group(3), text
    )


def rewrite_moved_lines(lines):
    """`rewrite_links` over moved lines, skipping lines inside code fences."""
    fences = _fence_map(lines)
    return [line if fences[i] else rewrite_links(line) for i, line in enumerate(lines)]


# --- archive writing ---------------------------------------------------------


def archive_headings(archive_lines):
    return {line for line in archive_lines if line.startswith("## ")}


def archived_row_keys(archive_lines):
    """First-cell keys already present in the archived-row table."""
    keys = set()
    inside = False
    for line in archive_lines:
        if line.startswith("## "):
            inside = line.strip() == ARCHIVED_TABLE_HEADING
            continue
        if inside and line.startswith("|") and ROW_RE.match(line):
            keys.add(row_key(line))
    return keys


def section_end(archive_lines, heading):
    """Index just past the last non-blank line of `heading`'s section."""
    try:
        start = archive_lines.index(heading)
    except ValueError:
        return None
    end = len(archive_lines)
    for i in range(start + 1, len(archive_lines)):
        if archive_lines[i].startswith("## "):
            end = i
            break
    while end > start + 1 and archive_lines[end - 1].strip() == "":
        end -= 1
    return end


def append_section(archive_lines, heading, body):
    """Append `body` lines to `heading`, creating the section at the end once."""
    end = section_end(archive_lines, heading)
    if end is None:
        while archive_lines and archive_lines[-1].strip() == "":
            archive_lines.pop()
        archive_lines.extend(["", heading, ""] + list(body))
    else:
        archive_lines[end:end] = list(body)
    return archive_lines


def append_block(archive_lines, heading, block_lines):
    while archive_lines and archive_lines[-1].strip() == "":
        archive_lines.pop()
    archive_lines.extend(["", heading, ""] + list(block_lines))
    return archive_lines


# --- the plan ----------------------------------------------------------------


def _drop_lines(lines, ranges):
    """Remove (start, end, join_blank) ranges from `lines`.

    `join_blank` removes the blank lines that followed the range and leaves
    exactly one blank separator (paragraph blocks); without it the range is
    deleted outright (table rows). Lines outside the ranges are kept byte for
    byte.
    """
    if not ranges:
        return list(lines)
    drop = set()
    joins = set()
    for start, end, join_blank in ranges:
        drop.update(range(start, end))
        if not join_blank:
            continue
        j = end
        while j < len(lines) and lines[j].strip() == "":
            drop.add(j)
            j += 1
        joins.add(j)
    result = []
    for i, line in enumerate(lines):
        if i in drop:
            continue
        if i in joins and line.strip() != "" and result and result[-1].strip() != "":
            result.append("")
        result.append(line)
    while result and result[-1].strip() == "":
        result.pop()
    return result


def build_plan(runs_text, archive_text, keep, today):
    """Return (new_runs_text, new_archive_text, plan) without writing anything."""
    runs_lines = runs_text.split("\n")
    archive_lines = archive_text.split("\n")
    plan = []

    header, sep, table_end = find_table(runs_lines)
    trailing_index = find_trailing_paragraph(runs_lines)
    paragraph = runs_lines[trailing_index] if trailing_index is not None else ""
    segments = split_paragraph(paragraph) if paragraph else []
    open_runs = set(queued_runs(paragraph)) if paragraph else set()

    # (1) table rows
    rows = [line for line in runs_lines[sep + 1 : table_end] if line.startswith("|")]
    numbers = sorted({row_run_number(r) for r in rows}, reverse=True)
    keep_numbers = set(numbers[:keep])
    kept_rows, old_rows = [], []
    for row in rows:
        (kept_rows if row_run_number(row) in keep_numbers else old_rows).append(row)

    existing_keys = archived_row_keys(archive_lines)
    new_archive_rows = [r for r in old_rows if row_key(r) not in existing_keys]
    duplicate_rows = len(old_rows) - len(new_archive_rows)
    if old_rows:
        plan.append(
            "table: move %d row(s) (runs %s), keep %d; %d already archived"
            % (
                len(old_rows),
                ", ".join(str(row_run_number(r)) for r in old_rows),
                len(kept_rows),
                duplicate_rows,
            )
        )

    # (2) instruction blocks
    blocks = find_blocks(runs_lines, table_end, trailing_index)
    headings = archive_headings(archive_lines)
    move_ranges = []
    block_appends = []
    for run, start, end in blocks:
        if run in open_runs:
            continue
        heading_prefix = "## Run %d (" % run
        if any(h.startswith(heading_prefix) for h in headings):
            plan.append("block Run %d: removed, already in the archive" % run)
            move_ranges.append((start, end, True))
            continue
        detail = _block_detail(run, segments, runs_lines[start])
        heading = "## Run %d (completed %s: %s)" % (run, today, detail)
        block_appends.append((heading, rewrite_moved_lines(runs_lines[start:end])))
        move_ranges.append((start, end, True))
        plan.append("block Run %d -> %s" % (run, heading))

    # (3) trailing paragraph
    paragraph_tail = []
    new_paragraph = paragraph
    if len(segments) > 1:
        tail_runs = [run for run, _ in segments[1:] if run is not None]
        if tail_runs:
            previous = max(tail_runs)
            keep_segments = [segments[0]] + [
                s for s in segments[1:] if s[0] == previous
            ]
            drop_segments = [s for s in segments[1:] if s[0] != previous]
            if drop_segments:
                new_paragraph = " ".join(text for _, text in keep_segments)
                paragraph_tail = [rewrite_links(text) for _, text in drop_segments]
                plan.append(
                    "paragraph: keep queued sentence + Run %d, move %d sentence(s) "
                    "about runs %s"
                    % (
                        previous,
                        len(drop_segments),
                        ", ".join(str(s[0]) for s in drop_segments),
                    )
                )

    # rebuild user-runs.md
    new_runs_lines = list(runs_lines)
    if trailing_index is not None and new_paragraph != paragraph:
        new_runs_lines[trailing_index] = new_paragraph
    table_ranges = []
    if old_rows:
        keep_set = set(keep_numbers)
        table_ranges = [
            (i, i + 1, False)
            for i in range(sep + 1, table_end)
            if (new_runs_lines[i].startswith("|") and row_run_number(new_runs_lines[i]) not in keep_set)
            or new_runs_lines[i].strip() == ""
        ]
    new_runs_lines = _drop_lines(new_runs_lines, table_ranges + move_ranges)
    new_runs_lines = _ensure_invocation_sentence(new_runs_lines, plan)
    new_runs_text = "\n".join(new_runs_lines)

    # rebuild the archive
    new_archive_lines = list(archive_lines)
    for heading, body in block_appends:
        append_block(new_archive_lines, heading, body)
    if new_archive_rows:
        body = [rewrite_links(r) for r in new_archive_rows]
        if section_end(new_archive_lines, ARCHIVED_TABLE_HEADING) is None:
            body = list(TABLE_COLUMNS) + body
        append_section(new_archive_lines, ARCHIVED_TABLE_HEADING, body)
    if paragraph_tail:
        append_section(
            new_archive_lines,
            PARAGRAPH_HEADING,
            ["Moved from `user-runs.md` on %s:" % today, ""]
            + [line for text in paragraph_tail for line in (text, "")][:-1],
        )
    new_archive_text = "\n".join(new_archive_lines)
    if not new_archive_text.endswith("\n"):
        new_archive_text += "\n"
    if not new_runs_text.endswith("\n"):
        new_runs_text += "\n"

    return new_runs_text, new_archive_text, plan


def _block_detail(run, segments, first_line):
    """'run235 (…)' style detail for the archive heading, or 'see table'."""
    for seg_run, text in segments:
        if seg_run == run:
            tokens = RUN_TOKEN_RE.findall(text)
            if tokens:
                return ", ".join(dict.fromkeys(tokens))
    if "completed as" in first_line:
        # only the run numbers in the "completed as ...:" prefix, not those
        # mentioned later in the instructions (positions, earlier runs)
        prefix = first_line.split("completed as", 1)[1].split(":", 1)[0]
        tokens = RUN_TOKEN_RE.findall(prefix)
        if tokens:
            return ", ".join(dict.fromkeys(tokens))
    return "see table"


def _ensure_invocation_sentence(lines, plan):
    if any(INVOCATION_SENTENCE in line for line in lines):
        return lines
    out = list(lines)
    insert_at = 1
    while insert_at < len(out) and out[insert_at].strip() == "":
        insert_at += 1
    out[insert_at:insert_at] = [INVOCATION_SENTENCE + ".", ""]
    plan.append("prologue: add the archiving invocation sentence")
    return out


# --- CLI ---------------------------------------------------------------------


def repo_root():
    return Path(__file__).resolve().parents[2]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--keep", type=int, default=8,
                        help="completed-run table rows to keep (default 8)")
    parser.add_argument("--runs-file", type=Path,
                        default=repo_root() / "docs/verification/user-runs.md")
    parser.add_argument("--archive-file", type=Path,
                        default=repo_root() / "docs/archive/user-runs-completed.md")
    parser.add_argument("--date", default=None,
                        help="date for new archive headings (default today)")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the plan, write nothing")
    parser.add_argument("--check", action="store_true",
                        help="exit 2 if either file would change")
    args = parser.parse_args(argv)

    if args.keep < 1:
        parser.error("--keep must be at least 1")
    today = args.date or _dt.date.today().isoformat()

    runs_text = args.runs_file.read_text()
    archive_text = args.archive_file.read_text()
    try:
        new_runs, new_archive, plan = build_plan(runs_text, archive_text, args.keep, today)
    except DocError as exc:
        print("archive_user_runs: %s" % exc, file=sys.stderr)
        return 3

    changed = new_runs != runs_text or new_archive != archive_text
    for line in plan:
        print(line)
    if not changed:
        print("nothing to archive")
    if args.check:
        return 2 if changed else 0
    if args.dry_run:
        print("dry run: no files written")
        return 0
    if changed:
        args.runs_file.write_text(new_runs)
        args.archive_file.write_text(new_archive)
        print("wrote %s and %s" % (args.runs_file, args.archive_file))
    return 0


if __name__ == "__main__":
    sys.exit(main())
