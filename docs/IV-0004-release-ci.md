# IV-0004: Public repository and cross-platform release CI

**Status:** implemented; current base locally verified, prior base verified on all five CI targets; `v*` tag publication configured but not yet exercised
**Upstream:** `checkouts/grok-build` (patch target for Windows portability)
**Deliverable:** `.github/workflows/release.yml` plus consolidated patches `0009–0010`
**Upstream base:** `47348d13ec4508dcfe440e34c6d511bb02998fb2` (Grok `0.2.112`)
**Current IV-0004 boundary:** commit `639cea1`, tree `2ebe20d81cfbeeadbae2e41ad8802bba66fd5886` (10 active patches)
**Doctrine:** [DC-0001](DC-0001-agentic-workspace.md) — read before changing or retiring this initiative

## Lifecycle map

- **Why this exists:** turn the complete local patch stack into reproducible,
  downloadable release binaries across the supported desktop targets.
- **Durable implementation:** `.github/workflows/release.yml` and portability
  patches `0009–0010`.
- **Known consumers:** every initiative that contributes a Grok patch, manual
  artifact users, tag-driven GitHub releases, and maintainers rebasing the
  pinned upstream base.
- **Key assumption:** the selected GitHub runner labels, protoc/NASM setup, and
  target toolchains remain available; tag publication is configured but still
  unexercised.
- **Evidence route:** clean-room `git am` is the drift guard. Rerun the workflow
  and preserve run links when current cross-platform truth matters; see
  [Evidence and reproduction](#evidence-and-reproduction).

## Intent and lifecycle justification

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
- Claiming tagged-release publication is verified before the first `v*` run.
- Hiding patch drift by applying with a non-failing or best-effort mechanism.

## Evidence and reproduction

Current `47348d1` base:

- All 16 patches apply cleanly in a detached clean-room worktree and reproduce
  tree `a9a11f502de730d7600bb58f42ceb8c5f77a2a32`, exactly matching the
  rebased development branch.
- `cargo check -p xai-grok-pager-bin --locked` passes in the clean-room tree.
- A native Windows release build passes with `CARGO_INCREMENTAL=0`,
  `CARGO_PROFILE_RELEASE_DEBUG=false`, and `--locked`; `/DEBUG:NONE` avoids the
  PDB linker limit and the resulting binary reports `grok 0.2.112 (d560c35)`.
- Sampling-types, sampler, chat-state, and telemetry library suites pass 285,
  69, 351, and 154 tests respectively. Focused pager, shell, tool, and
  parallel-dispatch coverage is recorded in IV-0002 and IV-0005–IV-0007.
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
- Windows-only validation run for the pre-consolidation equivalents of active
  patches `0009–0010`:
  https://github.com/iamwrm/grok-build-providers/actions/runs/29678121444

## Maintenance notes

- Keep `GROK_BUILD_BASE` synchronized with the documented/exported patch base.
- `git am` is an intentional CI drift guard: if upstream or patch ordering
  changes, fail before compiling rather than silently building another tree.
- Use the currently supported `macos-15-intel` label for x86_64. The legacy
  `macos-13` label remained queued indefinitely during initial validation.
- The release profile override disables Rust debug info and patch `0010`
  suppresses final-link PDB generation with `/DEBUG:NONE`; keep both unless
  linker behavior or pager symbol volume changes.
- Upstream is Apache-2.0; each archive includes its `LICENSE`.
