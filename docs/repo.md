# Repo layout & workflow

This repository carries reviewable patch series on top of upstream projects
without forks or submodules.

## Upstreams

- https://github.com/xai-org/grok-build
- https://github.com/earendil-works/pi

## Layout

```text
checkouts/          # disposable local clones of upstreams (gitignored)
patches/            # durable, ordered patch series grouped by upstream
configs/            # advanced provider configuration examples
projects/           # standalone initiative-owned proofs of concept
.github/workflows/  # clean-room build and release automation
docs/               # lifecycle maps, decisions, and maintenance guidance
```

The durable source of truth is the patch files plus the pinned upstream commit.
For grok-build, the current base is
`bb7f39d5858cbf5e00de639367f59debbdcb0138` (Grok `1.0.13`).

## Setup

```bash
mkdir -p checkouts
git clone https://github.com/xai-org/grok-build checkouts/grok-build
git clone https://github.com/earendil-works/pi checkouts/pi
```

`checkouts/` is ignored and may be deleted or recreated at any time.

## Applying the grok-build series

```bash
cd checkouts/grok-build
git fetch origin
git switch --detach bb7f39d5858cbf5e00de639367f59debbdcb0138
git am ../../patches/grok-build/*.patch
```

Both build and release CI perform that same clean-room operation. Keep the
base SHA synchronized in this document, `README.md`, `build.yml`, and
`release.yml`.

## Working on a change

1. Make and test the change in `checkouts/<project>/`.
2. Commit it as a feature-scoped change on top of the complete current stack.
3. Fold follow-up fixes into their owning patch rather than appending repair
   patches.
4. Re-export the ordered series with `git format-patch` and clean-room apply it
   before replacing the durable files.

The current grok-build stack ends at patch `0009`; a genuinely independent new
change starts at `0010`:

```bash
cd checkouts/grok-build
git format-patch HEAD~1..HEAD --start-number 10 --numbered \
  -o ../../patches/grok-build/
```

## Rebasing onto newer upstream

Treat a base move as a reduction exercise, not a blind replay:

1. Compare each local behavior with current upstream and drop patches whose
   behavior is now native.
2. Rebuild the remaining changes as coherent feature commits; combine OAuth
   with its transport, shared catalogs with catalogs, and fixups with their
   owning feature.
3. Renumber and export the complete replacement series.
4. Clean-room `git am` it onto the new base, run focused tests, and compile the
   pager on Linux, macOS, and Windows.
5. Update the base SHA, initiative front matter, and CI pins together.

## Current grok-build patch stack

| Patch | Purpose |
|---:|---|
| `0001` | OpenAI ChatGPT OAuth, credential handling, CLI, and Codex Responses transport |
| `0002` | Anthropic OAuth, credential handling, CLI, and Claude Messages transport |
| `0003` | Credential-gated OpenAI and Anthropic model catalogs |
| `0004` | `provider/model:effort` parsing and selection |
| `0005` | Windows portability for protoc hints and MSVC release linking |
| `0006` | Dynamically controllable raw sampling diagnostics |
| `0007` | Completed-turn prompt-cycle usage display |
| `0008` | Atomic batches of exact file edits |
| `0009` | Codex parallel tool-call identity and deterministic result ordering |

The rebase from Grok `0.2.114` to `1.0.13` reduced the stack from 17 patches to
nine. Local patches for maximum reasoning effort, cache-write accounting,
native-reasoning replay gating, and Responses remote compaction were removed
because the current upstream implementation supersedes those overlays.

## Current initiative map

| Initiative | Current patches | Purpose |
|---|---:|---|
| [IV-0001](IV-0001-openai-oauth.md) | `0001`, `0003–0004` | OpenAI ChatGPT-plan OAuth and Codex transport |
| [IV-0002](IV-0002-max-thinking.md) | upstream | Distinct `max` reasoning effort |
| [IV-0003](IV-0003-anthropic-oauth.md) | `0002–0004` | Anthropic OAuth, Claude transport, catalog, and effort support |
| [IV-0004](IV-0004-release-ci.md) | `0005` + workflows | Cross-platform build/release CI and Windows portability |
| [IV-0005](IV-0005-last-turn-stats.md) | `0006–0007` | Raw sampling diagnostics and completed-turn metrics |
| [IV-0006](IV-0006-batch-file-edits.md) | `0008` | Atomic multi-edit search/replace for one file |
| [IV-0007](IV-0007-codex-parallel-tools.md) | `0009` | Codex parallel tool-call wire identity and result ordering |
| [IV-0008](IV-0008-mid-session-model-switch.md) | upstream | Native-reasoning replay safety across route changes |
| [IV-0009](IV-0009-raylib-paste-hover.md) | — | Standalone raylib paste-chip hover proof of concept |
| [IV-0010](IV-0010-openai-server-compaction.md) | upstream | Responses compaction and checkpoint replay behavior |

Detailed IV sections below their rebase notices preserve historical design and
validation checkpoints. Their current front matter and this map are the
operational ownership references.

## Document conventions

Read [DC-0001](DC-0001-agentic-workspace.md) before changing or retiring an
initiative. Root initiative documents use `docs/IV-NNNN-short-slug.md` and
record intent, assumptions, implementation ownership, consumers, evidence,
and retirement state. Horizontal guidance uses `docs/DC-NNNN-short-slug.md`.

When patches are combined or removed, update the owning IV front matter and
this map, preserve useful historical evidence as an explicitly labeled
checkpoint, and verify the full replacement series before deleting old files.
