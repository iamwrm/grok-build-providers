# grok-build-providers

[![build](https://github.com/iamwrm/grok-build-providers/actions/workflows/build.yml/badge.svg)](https://github.com/iamwrm/grok-build-providers/actions/workflows/build.yml)
[![release](https://github.com/iamwrm/grok-build-providers/actions/workflows/release.yml/badge.svg)](https://github.com/iamwrm/grok-build-providers/actions/workflows/release.yml)

Patches and tooling on top of [xai-org/grok-build](https://github.com/xai-org/grok-build)
and [earendil-works/pi](https://github.com/earendil-works/pi).

See [docs/repo.md](docs/repo.md) for the repo layout and workflow
(plain clones in `checkouts/`, durable patches in `patches/` — no forks, no submodules).
Repository work is organized by lifecycle initiatives (`docs/IV-*`) and
horizontal doctrine (`docs/DC-*`).

## Continuous builds

`.github/workflows/build.yml` applies the complete patch series and compiles
the native pager binary on Linux, macOS, and Windows for branch pushes and
pull requests. It does not publish artifacts.

## Release binaries

`.github/workflows/release.yml` applies the complete patch series to pinned
upstream commit `500129c714ad1b10e6095481f4a8387a2ec52649`
(Grok `0.2.114`) and builds
`xai-grok-pager` for:

- macOS arm64 and x86_64
- Linux arm64 and x86_64
- Windows x86_64

Run the workflow manually to download Actions artifacts; those expire under
GitHub's artifact-retention policy. The workflow is configured so pushing a
`v*` tag publishes the five archives and `SHA256SUMS` as durable GitHub release
assets (the tag-publication path has not yet been exercised). Archives include
the upstream Apache-2.0 `LICENSE`.
