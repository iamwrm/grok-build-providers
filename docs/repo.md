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
.github/workflows/  # clean-room build/release automation
docs/               # initiative narratives and maintenance guidance
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
   `ba76b0a683fa52e4e60685017b85905451be17bc`, not whatever `origin/main`
   happens to contain:

   ```bash
   cd checkouts/grok-build
   # Example: export one newly committed patch after current patch 0014.
   git format-patch HEAD~1..HEAD --start-number 15 \
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

git switch --detach ba76b0a683fa52e4e60685017b85905451be17bc
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

## Initiative docs

Every initiative gets its own doc in `docs/`, following the pattern of
[`docs/i0001_add_openai-oauth.md`](i0001_add_openai-oauth.md):

- **Naming:** `docs/iNNNN_short-slug.md` — a sequential initiative number
  (`i0001`, `i0002`, …) plus a short descriptive slug.
- **Header:** title `# iNNNN: <goal in one line>`, then status, the upstream
  checkouts involved, the deliverable (usually a patch series under
  `patches/<project>/`), and the implementation branch/base commit.
- **Body:** goal, reference material (how an upstream already solves it),
  integration points found during exploration, the implemented patch series
  (one section per numbered patch), a files-affected summary table,
  explicit non-goals, verification performed, handoff notes
  (requirements to preserve, gotchas, maintenance state), and
  decisions/deferred work.

The initiative doc is the durable narrative that ties the numbered patches
together — keep it updated as the patch series evolves. When patches are
consolidated, preserve the old-to-new mapping in
[`docs/patch-history.md`](patch-history.md), prove the final source tree is
unchanged, and clean-room apply the replacement series before deleting the old
files.

## Current patch ownership

| Initiative | Patches | Purpose |
|---|---:|---|
| [i0001](i0001_add_openai-oauth.md) | `0001–0004` | OpenAI ChatGPT-plan OAuth and Codex transport |
| [i0002](i0002_add_max_thinking.md) | `0005` | Distinct `max` reasoning level |
| [i0003](i0003_add_anthropic-oauth.md) | `0006–0008` | Anthropic OAuth, Claude catalog, native `xhigh` |
| [i0004](i0004_release-ci.md) | `0009–0010` | Cross-platform release CI and Windows portability |
| [i0005](i0005_last-turn-stats.md) | `0011–0013` | Raw sampling diagnostics and turn-end metrics |
| [i0006](i0006_batch-file-edits.md) | `0014` | Atomic multi-edit search/replace for one file |

## Conventions

- One directory per upstream under `patches/` (`patches/grok-build/`, `patches/pi/`).
- One doc per initiative under `docs/` (`iNNNN_slug.md`), as described above.
- Number patches (`0001-...`, `0002-...`) so apply order is explicit.
- If a patch stops applying cleanly, fix it and commit the updated patch —
  the patch files are the source of truth, not the checkouts.
