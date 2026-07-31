# IV-0004: Public repository and cross-platform build/release CI

**Status:** implemented; current 17-patch base verified by three-OS build CI, prior base verified on all five release targets; first tag-only GitHub release publication remains pending
**Upstream:** `checkouts/grok-build` (patch target for Windows portability)
**Deliverable:** `.github/workflows/build.yml`, `.github/workflows/release.yml`, and consolidated patches `0009–0010`
**Upstream base:** `500129c714ad1b10e6095481f4a8387a2ec52649` (Grok `0.2.114`)
**Historical IV-0004 boundary:** commit `639cea1`, tree `2ebe20d81cfbeeadbae2e41ad8802bba66fd5886` (10 active patches at that checkpoint)
**Doctrine:** [DC-0001](DC-0001-agentic-workspace.md) — read before changing or retiring this initiative

## Lifecycle map

- **Why this exists:** continuously compile the complete local patch stack on
  each desktop OS and produce reproducible, downloadable release binaries.
- **Durable implementation:** `.github/workflows/build.yml`,
  `.github/workflows/release.yml`, and portability patches `0009–0010`.
- **Known consumers:** every initiative that contributes a Grok patch, branch
  pushes and pull requests, tag-driven GitHub releases, and maintainers
  rebasing the pinned upstream base.
- **Key assumption:** the selected GitHub runner labels, protoc/NASM setup, and
  target toolchains remain available; newly-created-tag publication is
  configured but still unexercised.
- **Evidence route:** clean-room `git am` is the drift guard. Rerun the workflow
  and preserve run links when current cross-platform truth matters; see
  [Evidence and reproduction](#evidence-and-reproduction).

## Intent and lifecycle justification

Make `https://github.com/iamwrm/grok-build-providers` public, compile every
ordinary change on macOS, Linux, and Windows, and build the fully patched
`xai-grok-pager` release binaries. Creating any repository tag should build all
five targets and publish a matching GitHub release with executable archives
and checksums. No branch, pull-request, manual, deleted-tag, or moved-tag event
should publish a release.

## Approach

`.github/workflows/build.yml` runs on every branch push, pull request, and
manual dispatch. Its Linux, macOS, and Windows jobs independently check out the
same pinned upstream base, apply the complete series with `git am`, install
protoc (plus NASM on Windows), and run
`cargo build -p xai-grok-pager-bin --locked`. It compiles native development
binaries with debug information disabled, but does not package, upload, or
publish them.

`.github/workflows/release.yml`:

1. Checks out this repository.
2. Checks out `xai-org/grok-build` at the pinned base commit above.
3. Applies every `patches/grok-build/*.patch` with `git am`.
4. Installs protoc 29.x on every runner and NASM on Windows.
5. Builds `xai-grok-pager-bin` with Cargo's release profile, locked
   dependencies, no incremental state, and no release debug info.
6. Packages the binary with upstream's Apache-2.0 `LICENSE`.
7. Transfers one archive per target between jobs, writes `SHA256SUMS`, and
   publishes the archives as assets on the GitHub release matching the new tag.

Release targets:

| Runner | Rust target |
|---|---|
| `macos-latest` | `aarch64-apple-darwin` |
| `macos-15-intel` | `x86_64-apple-darwin` |
| `ubuntu-latest` | `x86_64-unknown-linux-gnu` |
| `ubuntu-24.04-arm` | `aarch64-unknown-linux-gnu` |
| `windows-latest` | `x86_64-pc-windows-msvc` |

The release workflow has only an all-tag push trigger (`tags: ["**"]`). A
`github.event.created` guard rejects tag deletion and forced-move events, and
the static matrix always builds all five targets. Ordinary branch/PR validation
remains the responsibility of `build.yml`.

## Windows portability patches

Most of the Rust workspace compiled on Windows, but one Unix-only proto-build
assumption and two MSVC linker limits blocked the final binary.

### Patch 0009 — protoc dependency hints

`xai-proto-build::emit_rerun_if_changed` invoked protoc with
`--dependency_out=/dev/stdout` and `--descriptor_set_out=/dev/null`.
`protoc.exe` cannot open Unix device paths, so every proto-using crate failed.
The hints only optimize incremental local builds; patch `0009` skips this helper
on Windows while preserving normal proto generation.

### Patch 0010 — complete MSVC pager-link configuration

Rustc asks MSVC to emit debug information for the final link even when release
profile debug information is disabled. The pager's public-symbol table exceeds
link.exe's capacity (`LNK4319`), while the former PDB-page and
`/DEBUG:LongSymbolTruncate` flags are not portable across the current runner
linker. Release archives do not ship a PDB, so patch `0010` passes
`/DEBUG:NONE` for both MSVC targets. Forced unwind tables remain enabled for
panic backtraces.

## Non-goals

- Replacing the ordered patch stack with a fork, submodule, or mutable checkout.
- Publishing package-manager installers or auto-updaters.
- Claiming tagged-release publication is verified before the first new-tag run.
- Hiding patch drift by applying with a non-failing or best-effort mechanism.

## Evidence and reproduction

Current `500129c` base:

- All 17 patches apply cleanly in a detached clean-room worktree and reproduce
  tree `7ffd123dca8e25be6461cda7328f2b546406bb98`, exactly matching the
  rebased development branch.
- `cargo check -p xai-grok-pager-bin --locked` passes. The exact build-workflow
  command, `cargo build -p xai-grok-pager-bin --locked`, also passes locally on
  Linux with incremental and development debug info disabled.
  `git diff --check` and `cargo fmt --all -- --check` are clean.
- Sampling-types, sampler, and chat-state library suites pass 301/301, 175/175,
  and 352/352; focused `search_replace` coverage passes 116/116. Additional
  focused evidence is recorded in IV-0002 and IV-0005–IV-0008 plus IV-0010.
- The refreshed three-OS build workflow passed the complete 17-patch stack on
  Linux, macOS, and Windows:
  https://github.com/iamwrm/grok-build-providers/actions/runs/30610162380
- The five-target release workflow has not yet run on this base. Native Windows
  release success remains previous-base evidence.

Prior-base CI validation:

- Repository visibility changed from `PRIVATE` to `PUBLIC`; a credential-pattern
  scan over tracked patches/docs/configs found no access tokens.
- The all-target workflow run at the previous upstream base was:
  https://github.com/iamwrm/grok-build-providers/actions/runs/29679405277
- Every build completed successfully and uploaded an archive:
  - `xai-grok-pager-aarch64-apple-darwin`
  - `xai-grok-pager-x86_64-apple-darwin`
  - `xai-grok-pager-aarch64-unknown-linux-gnu`
  - `xai-grok-pager-x86_64-unknown-linux-gnu`
  - `xai-grok-pager-x86_64-pc-windows-msvc`
- At that historical checkpoint, the manual-dispatch publish job was skipped
  as designed and only Actions artifacts were produced. The current workflow
  no longer exposes manual dispatch: creation of any tag is the sole release
  trigger. Actual GitHub release creation and `SHA256SUMS` attachment remain
  untested until the first new-tag run.
- Windows-only validation run for the pre-consolidation equivalents of active
  patches `0009–0010`:
  https://github.com/iamwrm/grok-build-providers/actions/runs/29678121444

## Maintenance notes

- Keep `GROK_BUILD_BASE` in both workflows synchronized with the
  documented/exported patch base.
- `git am` is an intentional CI drift guard: if upstream or patch ordering
  changes, fail before compiling rather than silently building another tree.
- Use the currently supported `macos-15-intel` label for x86_64. The legacy
  `macos-13` label remained queued indefinitely during initial validation.
- The release profile override disables Rust debug info and patch `0010`
  suppresses final-link PDB generation with `/DEBUG:NONE`; keep both unless
  linker behavior or pager symbol volume changes.
- Upstream is Apache-2.0; each archive includes its `LICENSE`.
