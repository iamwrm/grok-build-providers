# Repo layout & workflow

This repo tracks work against upstream projects **without** forks or submodules.

## Upstreams

- https://github.com/xai-org/grok-build
- https://github.com/earendil-works/pi

## Layout

```
checkouts/     # plain git clones of upstreams (gitignored, disposable)
patches/       # durable artifacts: patch files against upstreams (checked in)
docs/          # documentation
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
2. Export the change as a patch into `patches/<project>/`:

   ```bash
   cd checkouts/grok-build
   git diff > ../../patches/grok-build/0001-my-change.patch
   # or, for committed work:
   git format-patch origin/main -o ../../patches/grok-build/
   ```

3. Commit the patch in this repo.

## Applying patches to a fresh checkout

```bash
cd checkouts/grok-build
git apply ../../patches/grok-build/0001-my-change.patch
# or, for format-patch output (preserves commits):
git am ../../patches/grok-build/*.patch
```

## Refreshing against upstream

```bash
cd checkouts/grok-build
git fetch origin && git reset --hard origin/main
git apply ../../patches/grok-build/*.patch   # fix conflicts, re-export patches
```

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
together — keep it updated as the patch series evolves.

## Conventions

- One directory per upstream under `patches/` (`patches/grok-build/`, `patches/pi/`).
- One doc per initiative under `docs/` (`iNNNN_slug.md`), as described above.
- Number patches (`0001-...`, `0002-...`) so apply order is explicit.
- If a patch stops applying cleanly, fix it and commit the updated patch —
  the patch files are the source of truth, not the checkouts.
