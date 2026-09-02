# IV-0006: Apply multiple exact edits to one file as one validated batch

**Status:** implemented in durable patch `0008`
**Upstream:** `checkouts/grok-build`
**Deliverable:** `patches/grok-build/0008-Apply-exact-file-edits-atomically-in-batches.patch`
**Implementation base:** `bb7f39d5858cbf5e00de639367f59debbdcb0138` (Grok `1.0.13`)
**Patch-0014 checkpoint:** commit `89b85da`; 14-patch tree `90ac009b4f64b731558f6365e60824e2f0643091`
**Doctrine:** [DC-0001](DC-0001-agentic-workspace.md) — read before changing or retiring this initiative

> **Grok 1.0.13 rebase:** This feature moved from patch `0014` to `0008` when the rebased stack was renumbered. Historical checkpoints below intentionally retain their original identifiers.

## Lifecycle map

- **Why this exists:** reduce tool round trips while preserving exact-match
  validation and one-file write behavior for cognitively related edits.
- **Durable implementation:** patch `0008`; the schema and runtime validator jointly define single-edit versus batch mode.

- **Known consumers:** Grok's standard and concise `search_replace` tools,
  shell previews, workspace permissions, ACP diff rendering, Markdown-heavy
  agent sessions, and [IV-0007](IV-0007-codex-parallel-tools.md).
- **Key assumption:** validation atomicity is sufficient; this initiative does
  not promise filesystem transactions or concurrent-writer detection.
- **Evidence route:** rerun the focused `xai-grok-tools` release suite and
  clean-room patch application under
  [Evidence and reproduction](#evidence-and-reproduction).

## Intent and lifecycle justification

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

## Evidence and reproduction

### Historical patch-0014 rebase checkpoint

- Patches `0001–0014` clean-room apply to `47348d1` and produce tree
  `90ac009b4f64b731558f6365e60824e2f0643091`.
- At that checkpoint, focused `xai-grok-tools` `search_replace` coverage
  passed 112/112, including atomic success, dependent edits, rollback on late missing or
  ambiguous matches, mode/schema validation, and concise output.
- The previous-base full release suite remains a historical checkpoint: 2,672
  passed and 6 ignored. It is not asserted as the current upstream total.

### Integrated 17-patch rebase checkpoint

- Patch `0014` remains part of the durable `0001–0017` series.
- All 17 patches clean-room apply to base `500129c` and produce tree
  `7ffd123dca8e25be6461cda7328f2b546406bb98`.
- Current focused `xai-grok-tools` `search_replace` coverage passes 116/116;
  sampling-types, sampler, and chat-state library suites pass 301/301, 175/175,
  and 352/352; the clean-room pager-bin Cargo check passes.
- Shell parallel-dispatch tests (13/13) remain a previous-base checkpoint; the
  build workflow compiles but does not run test suites. Native Windows
  development compilation and all five release targets pass; release run:
  https://github.com/iamwrm/grok-build-providers/actions/runs/30613123813.

This integration checkpoint validates the current Linux build and clean-room
series but does not replace refreshed cross-platform CI or the historical
patch-0014 release-suite record.

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

- [IV-0007: Improve Responses tool-call identity and concurrent result ordering](IV-0007-codex-parallel-tools.md)
