# grok-build patch consolidation history

The active grok-build series was consolidated on 2026-07-20 from 20 patches
into 13 maintainable patches. This was a history-only rewrite: the final Git
tree stayed exactly the same.

- Upstream base: `ba76b0a683fa52e4e60685017b85905451be17bc`
- Pre-consolidation tip: `65ef5405879be39c05870142c9de5c63ce0d108e`
- Pre-consolidation final tree: `0a219b73e452c3ce19ea64cd7679dde3bf1e7a89`
- Consolidated authoring tip: `5eedd46` (`patch-consolidation`)
- Consolidated clean-room `git am` tip: `d8c0564`
- Consolidated final tree: `0a219b73e452c3ce19ea64cd7679dde3bf1e7a89`

The identical final tree proves that consolidation changed patch ownership and
history only, not product source or behavior. The clean-room 13-patch result
also passed:

```bash
CARGO_INCREMENTAL=0 cargo build --release --locked -p xai-grok-pager-bin
```

## Old-to-new patch map

| Active patch | Pre-consolidation patches | Rationale |
|---|---|---|
| `0001` OpenAI OAuth flow/storage/CLI | `0001` | Stable subsystem boundary retained. |
| `0002` Codex Responses transport | `0002` + `0018` | The deployed `response.metadata` compatibility fix belongs to the transport that introduced Responses decoding. |
| `0003` provider/model:effort references | `0004` | Generic cross-provider UX retained independently. |
| `0004` OpenAI Codex catalog and docs | `0003` + `0005` | Documentation now ships with the user-visible catalog. |
| `0005` `max` reasoning effort and docs | `0006` + `0007` | Documentation now ships with the semantic change. |
| `0006` Anthropic OAuth flow/storage/CLI | `0008` | Stable subsystem boundary retained. |
| `0007` Anthropic OAuth Messages transport | `0009` | Stable wire-protocol boundary retained. |
| `0008` Anthropic catalog/native `xhigh`/docs | `0010` + `0011` + `0012` | The final catalog introduces correct native-`xhigh` behavior and docs directly, without an intermediate inaccurate catalog. |
| `0009` portable protoc dependency hints | `0013` | Independent Windows portability fix retained. |
| `0010` successful MSVC pager link | `0014` + `0015` | The larger PDB pages and long-symbol truncation are both required; the first change alone did not link. |
| `0011` dynamically controllable raw sampling logs | `0016` + `0020` | Raw request recording now introduces its final shared runtime gate and command UX directly. |
| `0012` prompt-cache write accounting | `0017` | Independent normalized usage boundary retained. |
| `0013` turn-end prompt-cycle metrics | `0019` | Substantial user-facing pager/ACP feature retained independently. |

## Maintenance policy

- Active numbering follows the 13-patch series; the next new grok-build patch
  is `0014`.
- Old patch numbers in historical commits, issue discussions, or validation
  logs should be resolved through the table above.
- Do not reintroduce a later fix as a separate patch when it solely corrects a
  behavior owned by an existing active patch. Re-export the owning patch and
  retain the rationale in its initiative document.
- Keep meaningful subsystem boundaries: credential handling, provider
  transport, reusable model semantics, normalized usage, and user-facing
  rendering should not be collapsed merely to minimize patch count.
