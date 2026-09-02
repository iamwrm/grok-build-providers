# grok-build-providers

[![build](https://github.com/iamwrm/grok-build-providers/actions/workflows/build.yml/badge.svg)](https://github.com/iamwrm/grok-build-providers/actions/workflows/build.yml)
[![release](https://github.com/iamwrm/grok-build-providers/actions/workflows/release.yml/badge.svg)](https://github.com/iamwrm/grok-build-providers/actions/workflows/release.yml)

Patches and tooling on top of [xai-org/grok-build](https://github.com/xai-org/grok-build)
and [earendil-works/pi](https://github.com/earendil-works/pi).

See [docs/repo.md](docs/repo.md) for the repo layout and workflow
(plain clones in `checkouts/`, durable patches in `patches/` — no forks, no submodules).
Repository work is organized by lifecycle initiatives (`docs/IV-*`) and
horizontal doctrine (`docs/DC-*`).

## Patch maintenance

The Grok `1.0.13` rebase reduces the grok-build stack from 17 patches to nine. Changes now provided by upstream were dropped, related provider and platform fixes were consolidated, and each remaining patch is feature-scoped.

## Continuous builds

`.github/workflows/build.yml` applies the complete patch series and compiles
the native pager binary on Linux, macOS, and Windows for branch pushes and
pull requests. It does not publish artifacts.

## Release binaries

`.github/workflows/release.yml` applies the complete patch series to pinned
upstream commit `bb7f39d5858cbf5e00de639367f59debbdcb0138`
(Grok `1.0.13`) and builds
`xai-grok-pager` for:

- macOS arm64 and x86_64
- Linux arm64 and x86_64
- Windows x86_64

Creating any repository tag triggers all five release builds. After every
build succeeds, the workflow creates the matching GitHub release and uploads
the per-target executable archives plus `SHA256SUMS`. Branch pushes, pull
requests, manual dispatches, tag deletions, and forced moves of existing tags
do not publish releases. Archives include the upstream Apache-2.0 `LICENSE`.
The first verified tag-driven publication is
[`v20260731`](https://github.com/iamwrm/grok-build-providers/releases/tag/v20260731).
