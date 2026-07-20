# i0006: Apply multiple exact edits to one file as one validated batch

**Status:** implemented in durable patch `0014`; historical and dated integration verification recorded below
**Upstream:** `checkouts/grok-build`
**Deliverable:** `patches/grok-build/0014-Apply-exact-file-edits-atomically-in-batches.patch`
**Implementation base:** `ba76b0a683fa52e4e60685017b85905451be17bc`
**Patch-0014 checkpoint:** commit `d8312ac`; 14-patch tree `25f2c549b4a3a357a521b60ec66d8b6d05f258d9`

## Goal

Reduce unnecessary model/tool round trips when editing Markdown or another file
in several places. The session that motivated this change made 64 Markdown
`search_replace` calls: 63 succeeded and one used stale text. Exact matching
was reliable; the structural problem was that the tool accepted only one
`(old_string, new_string)` pair per invocation.

## Patch 0014 — ordered, validation-atomic batch search/replace

`search_replace` accepts two wire shapes:

- **Single edit:** provide top-level `old_string`, `new_string`, and optional
  `replace_all`.
- **Batch:** provide a non-empty `edits` array. Every member requires
  `old_string` and `new_string` and has its own optional `replace_all`.

In batch mode, omit the top-level strings and omit or leave top-level
`replace_all` false. Existing complete single-edit payloads remain accepted.
The generated schema requires only `file_path` at the top level so it can
represent either shape; runtime validation requires exactly one valid mode.

```json
{
  "file_path": "README.md",
  "edits": [
    {
      "old_string": "Old heading",
      "new_string": "New heading"
    },
    {
      "old_string": "old term",
      "new_string": "new term",
      "replace_all": true
    }
  ]
}
```

### Atomicity boundary

A batch provides **validation atomicity within one tool call**. The tool reads
an existing file once, applies edits in array order to evolving in-memory
content, and begins its single filesystem write only after every member
validates. This permits intentional dependencies between edits. A missing or
ambiguous later match returns before the write, so that validation failure does
not leave a partially applied batch on disk.

This is not a filesystem transaction or compare-and-swap operation. The
implementation writes the final content directly; it does not use a temporary
file plus atomic rename, guarantee crash-safe rollback, or detect another
process changing the file between the initial read and final write.

Exact matching, per-member `replace_all`, CRLF preservation, gitignore/path
checks, and the existing optional Unicode-confusable fallback are shared with
the single-edit implementation. Exact matching is attempted first. The
optional normalization fallback is narrowly defined and is not general fuzzy
matching.

### Validation and file creation

Mixed modes, an empty `edits` array, incomplete legacy fields, top-level
`replace_all: true` in batch mode, identical old/new strings, and empty batch
`old_string` values are rejected. Match and validation failures attributable
to a member identify its 1-based edit index; file-level path, read, and write
errors do not.

Batch mode edits existing files only. An empty member `old_string` is rejected,
while a batch targeting a nonexistent file returns the ordinary file-not-found
result. Legacy single-edit creation with an empty top-level `old_string` is
unchanged; the dedicated write tool remains the clearer creation path.

### Result and diff behavior

A successful batch writes once and emits one `FileWritten` notification. Its
structured `edits.details` contains one record per actual replacement
occurrence in application order. A member using `replace_all` can therefore
contribute multiple records. The success message counts requested array
members, not total replacement occurrences.

Each detail describes the content state when that ordered edit was applied.
A later dependent edit can change an earlier record's final line position or
context, so the records are not an aggregate snapshot of the final file.

The shell retains its pre-execution substring preview for legacy single-edit
calls. Batch calls emit no pre-execution diff because presenting one member as
if it represented the batch would be misleading. On success, the Grok pager
uses structured `edits.details` from the tool result to render batch hunks. The
legacy aggregate `old_string` and `new_string` result fields are empty for a
batch, and `patch` is unset; generic ACP clients that ignore the structured
metadata may therefore not display a complete before/after batch diff.

## Files affected

| Area | Change |
|---|---|
| `xai-grok-tools/.../grok_build/search_replace/mod.rs` | Batch schema, runtime mode validation, ordered in-memory application, one-write success path, output details, and tests |
| `xai-grok-tools/.../grok_build_concise/search_replace.rs` | Batch guidance and concise-mode coverage |
| `xai-grok-shell/.../tool_calls.rs` | Preserve the single-edit start preview and suppress misleading batch pre-execution content |
| `xai-grok-workspace/.../permission/types.rs` | Adapt an existing typed permission fixture to optional legacy fields and `edits: None`; no new batch permission policy |

Existing ACP completion conversion is also relevant to consumers: it attaches
`edits.details` as diff metadata, while the batch aggregate old/new strings are
empty.

## Verification record

### Patch-0014 publication checkpoint

- Patches `0001–0014` clean-room applied to the pinned base and produced tree
  `25f2c549b4a3a357a521b60ec66d8b6d05f258d9`.
- Recorded command:
  `CARGO_INCREMENTAL=0 cargo test --release --locked -p xai-grok-tools`.
- Recorded result: 2,672 passed, 6 ignored.
- Focused coverage included CRLF-preserving multi-edit success and one
  notification, rollback on a late missing or ambiguous match, dependent
  edits, missing-file behavior, mixed/empty/incomplete mode rejection, schema
  compatibility, and concise output.
- Recorded clean-room pager build:
  `CARGO_INCREMENTAL=0 cargo build --release --locked -p xai-grok-pager-bin`.

Those counts describe the historical series ending at patch `0014`; they are
not asserted as current test totals after later patches.

### Integrated 17-patch verification record (2026-07-20)

- Patch `0014` remains part of the `0001–0017` durable series.
- All 17 patches clean-room applied to the pinned base and produced tree
  `dd6c4ce1ead5b4a91aae81f5f0699d0fa65dee7c`.
- Recorded commands:
  `CARGO_INCREMENTAL=0 cargo test -p xai-grok-sampling-types --release --locked`,
  `CARGO_INCREMENTAL=0 cargo test -p xai-grok-sampler --release --locked`, and
  `CARGO_INCREMENTAL=0 cargo build -p xai-grok-pager-bin --release --locked`.
- Recorded results: 292 passing `xai-grok-sampling-types` tests, 188 passing
  `xai-grok-sampler` unit and integration tests, and a successful pager release
  build.

This dated integration record validates the combined product but does not
replace the patch-0014-specific `xai-grok-tools` test record above. It is not a
promise that later revisions will retain the same tree hash or test counts.

## Non-goals and limitations

- General fuzzy or approximate matching.
- Multi-file transactions; one batch targets one file.
- Batch file creation.
- Filesystem-transaction or crash-safe rollback guarantees.
- Concurrent-writer detection or compare-and-swap semantics.
- A portable full-file before/after batch diff for ACP clients that ignore
  `edits.details`.

## Recommended follow-up coverage

Add focused tests for batch top-level `replace_all`, empty and identical member
strings, per-member `replace_all`, normalized fallback, write failures,
rollback notification absence, and ACP batch rendering. Consider carrying the
original and final full-file text or an aggregate patch when a portable batch
diff becomes a requirement.

## Related

- [i0007: Improve Responses tool-call identity and concurrent result ordering](i0007_codex-parallel-tools.md)
