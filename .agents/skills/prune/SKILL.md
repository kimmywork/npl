---
name: prune
description: Remove clearly obsolete, explicitly abandoned, or reproducible workspace artifacts, and trim unnecessary content from drafted plans or designs. Use before commits, branch sync, or when simplifying drafted work.
---

# Prune

Less is more. Remove what is not necessary, in a working copy or in drafted work, and keep the smallest set that is still necessary and sufficient. Historical context belongs in version history, not in stale files that agents may mistake for current guidance. Subtraction is the operation; the object may be files in a working copy or overgrowth inside a design or plan that is about to land.

## Trigger

Run pruning before every commit, as part of branch-sync preparation, when a task explicitly requests file or workspace cleanup, and when landing a drafted design or writing a plan that has grown beyond what the task requires. It may also be invoked when the user asks to simplify or trim drafted material.

## Common barriers

A candidate is removed only if it is not required and not protected. Keeping beats removing when in doubt. Preserve whatever the task needs, whatever the user has asked to keep, and whatever you are not sure about — present ambiguous candidates and ask. If no prior decision exists, make a provisional judgment from the available evidence, state it, ask the user to confirm before destructive removal, and persist the confirmed classification in the current-state or change-log location. Any claim that something is no longer needed is a judgment you can state and defend, not an assumption you act on silently.

## Preconditions

1. Read the active agent-facing instructions and, where the project maintains one, its current workstate or change-log convention.
2. Before removing context, record the latest completed work, decisions, unresolved risks, and intended next step in that current-state location; if no such convention exists, confirm them in the session record instead.
3. Inspect `git status`, recent commits, tracked files, ignored files, and references to every cleanup candidate. Check ignore rules both ways: a broad pattern (for example `*.ssa`) can hide a required hand-written input from clean checkouts, and un-ignoring a pattern can expose unrelated vendored files; verify from a clean copy of tracked plus non-ignored files.
4. For generated file content, identify its source and exact regeneration command. Run the smallest relevant reproducibility check before deletion.

## File and workspace pruning

Remove obsolete, abandoned, and reproducible clutter from a working copy.

Remove only candidates that are clearly classified as one of:

- logs, plans, designs, reports, or duplicated status snapshots whose obsolescence is proven by supersession or an explicit decision, never by age;
- abandoned task branches, failed routes, POC/MVP/prototype code, scratch fixtures, temporary files, or debugging artifacts explicitly superseded or discarded by a decision;
- tracked or accidentally exposable intermediates or caches whose deletion does not change the build contract or remove useful build acceleration.

In file pruning, keep active instructions, the current workstate or log, normative references, the current approved plan, source files, tests, and reproducibility inputs. Never remove reference or published content — post-mortems, lessons learned, retrospectives, research conclusions, release notes, and delivered or published artifacts — unless the user explicitly requests removal. Do not remove a file merely because its implementation is partial. Before deleting, confirm valuable history already exists in version history; discard an uncommitted experiment only when its abandonment is explicit and no source-of-truth fact is lost. Prefer `git rm` for tracked files and targeted removal for untracked files. Never use broad `git clean -fdx` or delete build caches blindly. Remove stale inbound and outbound references along with a removed file, or leave an explicit historical marker. Do not rewrite implementation, alter behavior, or create an archive as a substitute for pruning.

## Plan and design pruning

Trim a drafted design or plan back to what the task actually requires.

Remove parts that add no requirement value: duplicated sections, speculative branches or options that have no decision behind them, excess abstraction, steps, or detail beyond what the task needs, and wasted analysis that made it onto the page. Do not keep a rejected path merely to show it was considered; pruning is reduction, not an archive. Keep what confirms capability — acceptance criteria, constraints, and requirements stay — and never cut anything the user marked as required. Verify by comparing the trimmed version against the source task or requirements and confirming nothing necessary was dropped; the output is the trimmed plan or design, reduced to the necessary and sufficient set. If the scope to cut is unclear, present the suggested trimmings and ask before finalizing. This domain is length and clutter subtraction, not a request for verification machinery that file cleanup needs.

## Scope and boundaries

Pruning may classify a candidate when no prior decision exists, but that classification is provisional until the user confirms it. Persist the confirmation before destructive removal, then perform only the authorized subtraction. Pruning does not resolve conflicting requirements, silently declare an active artifact obsolete, or create an archive as a substitute for removal. For drafted work, it may propose cuts; the trimmed plan or design must be reviewed before finalization. If a draft is explicitly rejected or will not become current, removing it is the outcome.

## Verification and transaction

After pruning, verify the intended result holds. For file and workspace pruning, confirm the working tree contains the intended source of truth, no removed path is still referenced, retained generated outputs still rebuild (rerun this after deletion as well as before, scaled to the cleanup type — build or fixed-point checks for artifacts that affect the build or generation chain, and reference plus working-tree checks for pure documentation cleanup), and permanent bootstrap artifacts are unchanged unless explicitly allowed. For plan and design pruning, verify by comparing the trimmed material against the source task, requirements, and the user's explicit keep-items. Record the pruning decision and evidence in the project's current-state or change-log location, then include the cleanup in the commit or leave the branch ready for review; present the trimmed plan or design for review before finalizing it.
