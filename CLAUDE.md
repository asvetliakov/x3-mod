# Claude Code instructions

@AGENTS.md

`AGENTS.md` is the binding project contract and is shared with Codex. This file
adds only what is specific to Claude Code: model and effort routing, the agent
definitions under `.claude/agents/`, and the shape of what subagents report back.
Where `AGENTS.md` names Codex models (`gpt-6-astra`, `gpt-5.6-sol`), apply the
Claude mapping below instead.

## Main session

- Fable 5.1 at **medium** effort, held constant for the whole session. Do not
  toggle effort per phase: a top-level effort change restarts the prompt cache
  and the model stays consistent with replies written at the earlier level.
  Raise the session to high only if it is observed acting on stale context or
  skipping a verification step.
- The main session owns architecture, planning, the install candidate and the
  Wine queue. It does not read logs, captures, shader dumps or disassembly
  itself; it delegates the bulk reading and keeps the decision.
- A design question that needs deeper deliberation goes to the `design` agent
  with the exact question and files. The agent writes the design note; the main
  session ratifies or rejects it.

## Subagent routing

Opus 5.5 (released 2026-09-22) replaces Opus 5 everywhere below. The agent
definitions use the `opus` alias, which Claude Code resolves to the latest
Opus (5.5 today); record the resolved model in a checkpoint's evidence when a
routing comparison depends on it. Effort is set in each definition (Opus 5.5 defaults to medium, one level below
Opus 5, and thinks more per turn at a given level, so levels do not carry over
one to one). Prompting guide: platform.claude.com/docs/en/build-with-claude/
prompt-engineering/prompting-claude-opus-5-5.

| Task | Agent | Model / effort |
| --- | --- | --- |
| Hooks, ABI, lifetime, GPU transactions, install-bound change with established invariants | `implement-deep` | Opus 5.5, high |
| Same, but the change must establish or validate an uncertain invariant | `implement-deep` with `model: "fable"` | Fable, medium |
| Well-specified change with a fixture that proves it (defaults, tooling, launcher, docs, small patches) | `implement` | Opus 5.5, medium |
| Independent review before a checkpoint commit | `review` | Opus 5.5, medium |
| Run fixtures, host tests, hash checks; compare readbacks | `verify` | Sonnet 5, medium |
| Log and capture triage: narrow a symptom to evidence | `triage` | Opus 5.5, low (A/B 2026-09-22: matched Sonnet 5 medium on every number at ~0.4x the tokens; `docs/verification/model-routing.md`) |
| Triage that must explain, not only locate (a "why" question, or a plain triage that found the rows but no cause) | `triage-deep` | Opus 5.5, medium |
| Ghidra, bytecode, shader disassembly; RE documentation | `disassemble` | Opus 5.5, high |
| Design note for a hard decision | `design` | Fable, high |
| Locate code or facts across many files | built-in `Explore` | Inherits the session model capped at Opus, so Opus 5.5 at session effort; pass `model: "sonnet"` for a broad, cheap sweep |

Escalation: a `review` of hook or ABI code that finds nothing but the change is
consequential gets a second `review` on Fable (pass `model: "fable"`). A `triage`
that located the rows but cannot explain them goes once to `triage-deep`; if
that also cannot explain the symptom, its evidence goes to `implement-deep` on
Fable for diagnosis, never to a third triage.

Fixture-verifiable retry policy: dispatch on `implement` (Opus 5.5 medium). If
the fixture fails, the task was misclassified: spawn a fresh `implement-deep`
(Opus 5.5 high) with the same brief and the failure output; if that fails too,
spawn `implement-deep` with `model: "fable"`. Do not continue the failed agent.

Parallel design (opt-in, for the hardest decisions only): spawn `design` twice,
once on Fable high and once with `model: "opus"`, with the same brief
but distinct note paths (`<note>-fable.md`, `<note>-opus.md`). Two independent
designs surface different failure modes; the cost is two runs plus a merge.
The main session reads both Outcome lines, picks one, and asks the winning
agent (via `SendMessage`) to fold in what the other found; it does not merge
prose itself. Not for routine design questions.

## Briefing subagents

- State the goal, the acceptance check (the exact command and what must be
  true), the exact files, and the relevant constraint excerpt from `AGENTS.md`.
  Do not enumerate steps and do not ask the agent to read `docs/status.md` or a
  handoff wholesale.
- Every Wine command in the brief carries the `X3M_FIXTURE_BOTTLE=X3` prefix
  and the `wine_lock.py` wrapper.
- Run agents in the background and keep orchestrating. For a follow-up on the
  same work, continue the existing agent with `SendMessage` rather than
  spawning a fresh one; its context is cached.
- One agent per task. Agents do not spawn agents. Fan out only when the parts
  are genuinely independent.
- Use `isolation: "worktree"` only for agents that edit source. Agents that
  build or run fixtures work in the main checkout under the Wine queue owner;
  a worktree that runs fixtures accumulates gigabytes of untracked results.
- Do not add "verify with a subagent" or "double-check" instructions to Opus
  briefs; Opus verifies on its own and such lines cause over-verification. Do
  not add "think carefully" lines either; effort is the control for thinking.
- Every agent definition tells the agent that its first message without a tool
  call ends its turn and counts as the report. Opus 5.5 otherwise tends to stop
  after a milestone to report or offer a choice; if an agent still returns with
  open items and no blocker, continue it with `SendMessage` naming the items,
  at most twice, then treat it as stuck.
- When elapsed time matters (a user is waiting on a launch cycle), say so in
  the brief in one sentence; Opus 5.5 paces itself on time signals.

## Report contract

The agent's final message is the only thing that enters the main context.
Every agent definition enforces this shape; hold ad-hoc `Agent` calls to it too.

- In this order: **Outcome**, **Evidence** (command and the numbers it
  produced), **Files changed**, **Open issues**. About 25 lines of prose;
  tables do not count, pasted log rows and file contents are not allowed.
- The brief's numbered questions are answered in its order, each with its
  number, so reports can be scored and compared mechanically.
- Every figure is marked measured or inferred. Evidence that will enter a
  ledger keeps its producing script or one-liner under `verification/results/`,
  not only in the session scratchpad.
- Long findings go into the owning note under `docs/`; the report gives the
  path and the outcome line only.
- No restatement of the task, no narrative of attempts unless it changes the
  next step. Rejected alternatives stay out of every report except that the
  `design` note (not its report) records the options considered and why they lose.

## Documentation rules

- Append verification outcomes to the owning feature ledger under
  `docs/verification/` (one ledger per feature). Do not create new `review-NN`
  or `iteration-NN` files; the numbered series is closed.
- `docs/status.md` stays a short current handoff. One current handoff file
  `docs/handoff-<date>.md`; when a new one is written, move the previous
  handoff, pause snapshot or status archive into `docs/archive/`.
- `docs/verification/user-runs.md` holds only open runs and the completed-run
  table. Completed run commands and instructions move to
  `docs/archive/user-runs-completed.md`.
