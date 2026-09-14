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

| Task | Agent | Model / effort |
| --- | --- | --- |
| Hooks, ABI, lifetime, GPU transactions, anything that will be installed | `implement-fable` | Fable, medium |
| Well-specified change with a fixture that proves it (defaults, tooling, launcher, docs, small patches) | `implement-opus` | Opus 5, medium |
| Independent review before a checkpoint commit | `review` | Opus 5, medium |
| Run fixtures, host tests, hash checks; compare readbacks | `verify` | Sonnet 5, low |
| Log and capture triage: narrow a symptom to evidence | `triage` | Sonnet 5, medium |
| Ghidra, bytecode, shader disassembly; RE documentation | `disassemble` | Opus 5, high |
| Design note for a hard decision | `design` | Fable, high |
| Locate code or facts across many files | built-in `Explore` | Sonnet |

Escalation: a `review` of hook or ABI code that finds nothing but the change is
consequential gets a second `review` on Fable (pass `model: "fable"`). A `triage`
that cannot explain the symptom hands its evidence to `implement-fable` for
diagnosis, never to another triage.

Fixture-verifiable retry policy: dispatch on `implement-opus`. If the fixture
fails, spawn a fresh `implement-opus` at high effort with the same brief plus the
failure output; do not continue the failed agent. A second failure means the task
was misclassified: send it to `implement-fable`.

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
- Do not add "verify with a subagent" or "double-check" instructions to Opus
  briefs; Opus 5 verifies on its own and such lines cause over-verification.

## Report contract

The agent's final message is the only thing that enters the main context.
Every agent definition enforces this shape; hold ad-hoc `Agent` calls to it too.

- At most 25 lines, in this order: **Outcome**, **Evidence** (command and the
  numbers it produced), **Files changed**, **Open issues**.
- Long findings go into the owning note under `docs/`; the report gives the
  path and the outcome line only.
- No pasted file contents, no restatement of the task, no rejected
  alternatives, no narrative of attempts unless it changes the next step.

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
