# i0006: Apply multiple exact edits to one file atomically

**Status:** implemented in patch `0014`; clean-room release verification complete
**Upstream:** `checkouts/grok-build`
**Deliverable:** `patches/grok-build/0014-Apply-exact-file-edits-atomically-in-batches.patch`
**Implementation branch:** 14-patch series based on `ba76b0a`; authoring tip `ec11661`, tree `25f2c549b4a3a357a521b60ec66d8b6d05f258d9`

## Goal

Reduce unnecessary model/tool round trips when editing Markdown or another file
in several independent places. The session that motivated this change made 64
Markdown `search_replace` calls: 63 succeeded and one used stale text. Exact
matching was reliable; the structural problem was that the tool accepted only
one `(old_string, new_string)` pair per invocation.

## Patch 0014 — atomic batch search/replace

`search_replace` now supports two backward-compatible modes:

- **Single edit:** existing top-level `old_string`, `new_string`, and
  `replace_all` fields.
- **Batch:** a non-empty `edits` array whose elements carry the same three
  fields. Dummy top-level strings are not required.

A batch reads the file once, applies edits in order to evolving in-memory
content, and writes once only after every edit validates. This supports
intentional dependent edits while ensuring that a later missing or ambiguous
match leaves the file untouched. Exact-match, per-edit `replace_all`, CRLF,
optional Unicode-confusable fallback, gitignore/path checks, and one
`FileWritten` notification are preserved.

The full and concise tool descriptions tell the model to prefer one batch for
multiple changes to the same file. Mixed, empty, incomplete, and file-creation
batch inputs fail clearly; error messages identify the 1-based failing edit.
Legacy single-edit file creation is unchanged.

The shell's pre-execution diff preview remains available for single edits.
Batch diffs are emitted from the successful structured tool result, where all
per-edit details are accurate, instead of showing one batch member as if it
represented the whole operation.

## Files affected

| Area | Change |
|---|---|
| `xai-grok-tools/.../grok_build/search_replace/mod.rs` | Batch schema, validation, atomic execution, output details, tests |
| `xai-grok-tools/.../grok_build_concise/search_replace.rs` | Batch guidance and concise-mode coverage |
| `xai-grok-shell/.../tool_calls.rs` | Optional single-edit fields and accurate batch preview behavior |
| `xai-grok-workspace/.../permission/types.rs` | Updated typed test fixture |

## Verification

- Release-profile `xai-grok-tools` suite: 2,672 passed, 6 ignored.
- Focused release-profile batch tests cover multi-section Markdown with CRLF,
  late miss rollback, ambiguity rollback, dependent edits, input validation,
  schema compatibility, concise output, and unchanged single-edit behavior.
- All 14 durable patches clean-room apply to `ba76b0a` and produce tree
  `25f2c549b4a3a357a521b60ec66d8b6d05f258d9`.
- Clean-room `CARGO_INCREMENTAL=0 cargo build --release --locked
  -p xai-grok-pager-bin` passes.

## Non-goals

- Fuzzy matching. Exact matching remains strict and predictable.
- Multi-file transactions. One batch targets one file.
- Batch file creation. Use the existing single-edit empty-`old_string` mode or
  the dedicated write tool.
