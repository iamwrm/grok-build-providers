# Repo layout & workflow

This repo tracks work against upstream projects **without** forks or submodules.

## Upstreams

- https://github.com/xai-org/grok-build
- https://github.com/earendil-works/pi

## Layout

```
checkouts/          # plain git clones of upstreams (gitignored, disposable)
patches/            # durable patch files, grouped by upstream
configs/            # advanced provider-config examples
projects/            # standalone proofs of concept owned by initiatives
.github/workflows/  # clean-room build/release automation
docs/               # IV lifecycle maps, DC doctrine, and workflow guidance
```

## Why not forks or submodules?

- Forks add a remote-management burden and drift from upstream.
- Submodules pin SHAs, are easy to break, and are painful for everyone.
- Plain clones in `checkouts/` are disposable; the durable objects are the
  patch files in `patches/`, which are small, reviewable, and rebased easily.

## Setup

```bash
mkdir -p checkouts
git clone https://github.com/xai-org/grok-build checkouts/grok-build
git clone https://github.com/earendil-works/pi checkouts/pi
```

`checkouts/` is in `.gitignore` — delete and re-clone freely.

## Working on a change

1. Hack inside `checkouts/<project>/` on a branch or dirty tree.
2. Export the change as the next numbered patch in the existing stack. The
   grok-build series is based on pinned commit
   `47348d13ec4508dcfe440e34c6d511bb02998fb2`, not whatever `origin/main`
   happens to contain:

   ```bash
   cd checkouts/grok-build
   # Example: export one newly committed patch after current patch 0016.
   git format-patch HEAD~1..HEAD --start-number 17 \
     -o ../../patches/grok-build/
   ```

   For an uncommitted change, use `git diff` but still give the output the
   next stack number; do not create another `0001`.

3. Commit the patch in this repo.

## Applying patches to a fresh checkout

Use the same pinned base as CI, then apply the complete ordered series:

```bash
cd checkouts/grok-build
git fetch origin

git switch --detach 47348d13ec4508dcfe440e34c6d511bb02998fb2
git am ../../patches/grok-build/*.patch
```

`.github/workflows/release.yml` performs this exact clean-room operation before
every build. `GROK_BUILD_BASE` in that workflow is the operational source of
truth for the base SHA; keep this document and initiative headers synchronized
with it.

## Rebasing onto newer upstream

Moving the series to `origin/main` is an intentional rebase, not the normal
apply procedure. Use a temporary branch/worktree, apply or rebase each commit
in order, resolve and test conflicts, then re-export **all affected patches**
with their existing numbers. Finally, clean-room `git am` the complete series
onto the new base and update `GROK_BUILD_BASE`, initiative docs, and validation
hashes together. Do not reset a working checkout to `origin/main` and assume
the pinned series will still apply unchanged.

## Agentic workspace documents

Read [DC-0001](DC-0001-agentic-workspace.md) for the repository-wide IV/DC
workflow and interpretation doctrine.

Every initiative gets a root document in `docs/`, following the pattern of
[`docs/IV-0001-openai-oauth.md`](IV-0001-openai-oauth.md):

- **Naming:** `docs/IV-NNNN-short-slug.md` — a sequential initiative number
  (`IV-0001`, `IV-0002`, …) plus a short descriptive slug.
- **Lifecycle role:** explain why related artifacts exist and record relevant
  requirements, external knowledge, facts, assumptions, decisions, non-goals,
  implementation locations, known consumers, evidence, and reproduction.
- **Progressive disclosure:** split only when a semantically local part no
  longer fits one coherent working context. Keep an annotated child link in
  the root IV, and link every child back to its root.
- **Maintenance:** keep the IV, implementation, consumers, and reproduction
  path synchronized. Mark stale evidence as a checkpoint and rerun it when
  current truth matters.
- **Retirement:** begin at the root IV, follow linked and searched consumers,
  preserve behavior still justified elsewhere, verify, then remove or mark
  retired artifacts and links.

Horizontal guidance uses `docs/DC-NNNN-short-slug.md`. DCs inform judgment
across initiatives; they are not a deterministic policy or dependency engine.
Links in both dimensions are attention routes and lifecycle clues, so annotate
why a reader should follow them.

When patches are rewritten or combined, update the owning initiative docs to
the resulting layout, remove superseded descriptions, and clean-room apply the
complete replacement series before deleting old patch files.

## Current initiative map

| Initiative | Patches | Purpose |
|---|---:|---|
| [IV-0001](IV-0001-openai-oauth.md) | `0001–0004` | OpenAI ChatGPT-plan OAuth and Codex transport |
| [IV-0002](IV-0002-max-thinking.md) | `0005` | Distinct `max` reasoning level |
| [IV-0003](IV-0003-anthropic-oauth.md) | `0006–0008` | Anthropic OAuth, Claude catalog, native `xhigh` |
| [IV-0004](IV-0004-release-ci.md) | `0009–0010` | Cross-platform release CI and Windows portability |
| [IV-0005](IV-0005-last-turn-stats.md) | `0011–0013` | Raw sampling diagnostics (incl. sampling-layer panic fix), turn-end metrics |
| [IV-0006](IV-0006-batch-file-edits.md) | `0014` | Atomic multi-edit search/replace for one file |
| [IV-0007](IV-0007-codex-parallel-tools.md) | `0015` | Codex parallel tool-call wire + result ordering |
| [IV-0008](IV-0008-mid-session-model-switch.md) | `0016` | Safe native reasoning replay across model/provider switches |
| [IV-0009](IV-0009-raylib-paste-hover.md) | — | Standalone raylib paste-chip hover and scrolling-preview POC |

## Conventions

- One directory per upstream under `patches/` (`patches/grok-build/`, `patches/pi/`).
- One root doc per initiative under `docs/` (`IV-NNNN-slug.md`), as described above.
- Horizontal doctrine uses `docs/DC-NNNN-slug.md`; read applicable DCs before changes.
- Number patches (`0001-...`, `0002-...`) so apply order is explicit.
- If a patch stops applying cleanly, fix it and commit the updated patch —
  the patch files are the source of truth, not the checkouts.
- A bug fix for code **introduced by an existing patch** is folded into that
  patch, not appended as a new number: clean-room apply the series, reorder
  and `fixup` with `git rebase -i` (merging the commit messages), re-export
  the **full** series with `git format-patch`, and verify the resulting tree
  hash (`git rev-parse HEAD^{tree}`) is unchanged. New numbers are reserved
  for changes with independent scope or a new initiative.
