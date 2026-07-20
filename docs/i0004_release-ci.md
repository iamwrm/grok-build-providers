# i0004: Public repository and cross-platform release CI

**Status:** implemented; current base locally verified, prior base verified on all five CI targets; `v*` tag publication configured but not yet exercised
**Upstream:** `checkouts/grok-build` (patch target for Windows portability)
**Deliverable:** `.github/workflows/release.yml` plus `patches/grok-build/0013..0015`
**Upstream base:** `ba76b0a683fa52e4e60685017b85905451be17bc` (Grok `0.2.106`)
**Current combined tip:** commit `d76cf1c8315728a9304b414734691ca692b1cc8e`, tree `f622605759a368e772f2da9afa0287321d6c8592` (15 patches)

## Goal

Make `https://github.com/iamwrm/grok-build-providers` public and build the
fully patched `xai-grok-pager` release binary for macOS, Linux, and Windows.
Manual runs should retain downloadable Actions artifacts; `v*` tags should
publish a GitHub release with checksums.

## Approach

`.github/workflows/release.yml`:

1. Checks out this repository.
2. Checks out `xai-org/grok-build` at the pinned base commit above.
3. Applies every `patches/grok-build/*.patch` with `git am`.
4. Installs protoc 29.x on every runner and NASM on Windows.
5. Builds `xai-grok-pager-bin` with Cargo's release profile, locked
   dependencies, no incremental state, and no release debug info.
6. Packages the binary with upstream's Apache-2.0 `LICENSE`.
7. Uploads one artifact per target; on `v*` tags, downloads all artifacts,
   writes `SHA256SUMS`, and publishes a GitHub release.

Targets:

| Runner | Rust target |
|---|---|
| `macos-latest` | `aarch64-apple-darwin` |
| `macos-15-intel` | `x86_64-apple-darwin` |
| `ubuntu-latest` | `x86_64-unknown-linux-gnu` |
| `ubuntu-24.04-arm` | `aarch64-unknown-linux-gnu` |
| `windows-latest` | `x86_64-pc-windows-msvc` |

Manual dispatch supports `target=all` (default) or `target=windows` so native
MSVC fixes can be tested without rebuilding the four already-green targets.
Tag events always select all targets.

## Windows portability patches

Most of the Rust workspace compiled on Windows, but one Unix-only proto-build
assumption and two MSVC linker limits blocked the final binary.

### Patch 0013 — protoc dependency hints

`xai-proto-build::emit_rerun_if_changed` invoked protoc with
`--dependency_out=/dev/stdout` and `--descriptor_set_out=/dev/null`.
`protoc.exe` cannot open Unix device paths, so every proto-using crate failed.
The hints only optimize incremental local builds; patch 0013 skips this helper
on Windows while preserving normal proto generation.

### Patch 0014 — larger PDB pages

Rustc emits a minimal release PDB on MSVC even with profile debug information
disabled. The pager's public-symbol table exceeded link.exe's default 4 KiB
page capacity (`LNK4319`). Patch 0014 selects the supported 16 KiB maximum for
both MSVC target configurations.

### Patch 0015 — pathological symbol truncation

One generated/generic pager symbol still triggered `LNK4318` and exhausted the
PDB public-symbol table at 16 KiB pages. Patch 0015 enables link.exe's
recommended `/DEBUG:LongSymbolTruncate`, retaining useful symbols while
truncating pathological names. The Windows release then linked, packaged, and
uploaded successfully.

## Verification

Current `ba76b0a` base:

- All 15 re-exported patches apply cleanly in a detached clean-room worktree;
  the result has tree `f622605759a368e772f2da9afa0287321d6c8592`.
- `cargo check --workspace --locked` passes.
- Sampling-types, sampler, shell, and pager library suites pass: 277, 159,
  5739, and 7390 tests respectively (23 ignored across shell and pager).
- Native arm64 macOS release build passes with `CARGO_INCREMENTAL=0`,
  `CARGO_PROFILE_RELEASE_DEBUG=false`, and `--locked`; the resulting
  `target/release/xai-grok-pager` reports `grok 0.2.106 (d76cf1c) [stable]`.
- Cross-platform CI for the rebased stack has not yet been run.

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
- Manual-dispatch release job was skipped as designed; it runs only for
  `refs/tags/v*` and consumes the same build artifacts. Therefore archive
  production is verified, but creation of an actual tagged GitHub release and
  `SHA256SUMS` attachment remains untested until the first `v*` tag.
- Windows-only validation run after patches 0013–0015:
  https://github.com/iamwrm/grok-build-providers/actions/runs/29678121444

## Maintenance notes

- Keep `GROK_BUILD_BASE` synchronized with the documented/exported patch base.
- `git am` is an intentional CI drift guard: if upstream or patch ordering
  changes, fail before compiling rather than silently building another tree.
- Use the currently supported `macos-15-intel` label for x86_64. The legacy
  `macos-13` label remained queued indefinitely during initial validation.
- The release profile override disables debug info to reduce artifact size,
  but MSVC still emits a minimal PDB internally; do not remove patches 0014/0015
  unless the linker behavior or pager symbol volume changes.
- Upstream is Apache-2.0; each archive includes its `LICENSE`.
