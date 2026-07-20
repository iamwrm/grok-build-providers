# i0007: Improve Responses tool-call identity and concurrent result ordering

**Status:** implemented in durable patch `0015`; historical and dated integration verification recorded below
**Upstream:** `checkouts/grok-build`
**Deliverable:** `patches/grok-build/0015-Improve-Codex-parallel-tool-call-handling.patch`
**Implementation base:** `ba76b0a683fa52e4e60685017b85905451be17bc`
**Patch-0015 checkpoint:** commit `93328fe`; 15-patch tree `0829a441098580595575d3a1ee64195165b6a771`

## Goal

Improve behavior first exposed by OpenAI Codex Responses turns:

- explicitly permit a backend to emit multiple function calls in one response;
- preserve the optional native Responses function-call item ID for history
  replay;
- continue executing approved local tools concurrently while publishing their
  post-flight results in a deterministic order.

This is pi-compatible in intent. The concrete pi references are
`packages/ai/src/api/openai-codex-responses.ts`, which sets
`parallel_tool_calls: true`, and
`packages/ai/src/api/openai-responses-shared.ts`, which stores Responses tool
calls as `call_id|item_id` and projects each component back to its native wire
field.

## Scope

Patch `0015` was motivated by Codex, but its changes have different scopes:

- The ChatGPT Codex OAuth request shaper explicitly overwrites
  `parallel_tool_calls` with `true` alongside `store: false` and the remaining
  Codex-specific request rules.
- Generic Responses request construction also sets
  `parallel_tool_calls: true`, affecting all callers of that conversion.
- Optional function-call item-ID preservation applies to generic Responses
  history.
- Chat Completions and Anthropic Messages discard the Responses-only item-ID
  suffix when projecting that history to their wire formats.
- Approved-call post-flight ordering lives in the shared shell tool loop and
  therefore affects local tool execution across providers, not only Codex.
  Concurrent dispatch and literal-path locking already existed there; patch
  `0015` changes how dispatched results are published.

## Patch 0015

### Request permission for multiple function calls

Generic Responses `CreateResponse` construction sets
`parallel_tool_calls: true`. The ChatGPT Codex request shaper also forces the
serialized field to `true` so an earlier or omitted value cannot disable it on
that route.

This field grants permission; it does not require the backend or model to emit
multiple calls. Actual call multiplicity remains a backend/model decision.

### Responses history and wire IDs

A Responses function-call item carries a pairing `call_id` and may also carry a
native item `id`, commonly `fc_…`. When the item ID is present and non-empty,
Grok Build stores the internal tool-call ID as `call_id|item_id`. Plain IDs
remain valid for legacy sessions and calls whose response omitted the item ID.

During Responses replay:

- `FunctionCall.call_id` receives the first component.
- `FunctionCall.id` receives the optional second component.
- `FunctionCallOutput.call_id` receives only the first component.
- `FunctionCallOutput.id` remains unset.

Chat Completions and Anthropic Messages likewise receive only the first
component because they do not use the Responses item ID. The internal format
uses the first literal `|` as its delimiter and does not escape it.

Preserving `FunctionCall.id` reproduces the native Responses item identity on
history replay. The patch does not independently prove a direct
reasoning-to-function-call association through that ID, so this document does
not claim one.

### Existing concurrent dispatch and path locking

Patch `0015` retains the shell's existing concurrent-dispatch and locking
behavior; it does not introduce the lock mechanism. Approved tools are
dispatched concurrently. Within one execution batch, calls sharing the same
extracted path string are serialized whenever that path is targeted by at
least one non-read-only call. A read sharing that key with a write is therefore
also serialized.

The existing lock key is the literal extracted `file_path`, `path`, or
`target_file` string. It is not canonicalized or resolved through `realpath`,
so aliases such as relative versus absolute paths, `a/../b`, or symlink paths
can bypass the same-file lock.

### Approved-call result ordering

Execution futures can finish out of order. Patch `0015` buffers their results
and drains the **approved-call subset** in approved source order. Successful
results and errors returned after dispatch therefore enter post-flight
processing in a deterministic approved-call order.

The reorder buffer wraps more than conversation-history insertion. A fast later
call can finish execution while its ACP completion update, hooks, follow-ups,
telemetry, completion events, and tool-result insertion remain delayed behind
an earlier approved call. Execution remains concurrent; post-flight
publication is source-ordered and can experience head-of-line blocking.

Calls rejected or resolved during pre-flight preparation do not enter the
approved list or this reorder buffer. Parse failures, unavailable tools,
permission or hook rejection, and calls skipped after an earlier cancellation
can be handled immediately. Patch `0015` therefore does **not** establish a
strict whole-turn ordering guarantee across a mixture of approved calls and
pre-flight failures.

## Files affected

| Area | Change |
|---|---|
| `xai-grok-sampler/src/client.rs` | Force `parallel_tool_calls: true` in ChatGPT Codex request shaping and test the serialized shape |
| `xai-grok-sampler/src/stream/responses.rs` | Preserve optional item IDs for streamed function-call fallback assembly |
| `xai-grok-sampling-types/src/conversation.rs` | Generic Responses permission, compound-ID helpers, capture/replay, and cross-backend projection tests |
| `xai-grok-shell/.../tool_calls.rs` | Add approved-call post-flight result buffering while retaining existing concurrent dispatch and literal-path locking |

## Verification record

### Patch-0015 publication checkpoint

- Patches `0001–0015` clean-room applied to the pinned base and produced tree
  `0829a441098580595575d3a1ee64195165b6a771`.
- Recorded `xai-grok-sampling-types` release result: 279 passed.
- Focused tests covered compound tool-call ID helpers and Responses replay.
- Codex request-shape tests asserted `parallel_tool_calls == true`.
- Existing stream-routing coverage confirmed that multiple function calls and
  their argument deltas receive distinct local tool indices.
- A release build of `xai-grok-pager-bin` succeeded.

Those counts describe the historical series ending at patch `0015`; they are
not current suite totals after later patches.

### Integrated 17-patch verification record (2026-07-20)

- Patch `0015` remains part of the `0001–0017` durable series.
- All 17 patches clean-room applied to the pinned base and produced tree
  `dd6c4ce1ead5b4a91aae81f5f0699d0fa65dee7c`.
- Recorded commands:
  `CARGO_INCREMENTAL=0 cargo test -p xai-grok-sampling-types --release --locked`,
  `CARGO_INCREMENTAL=0 cargo test -p xai-grok-sampler --release --locked`, and
  `CARGO_INCREMENTAL=0 cargo build -p xai-grok-pager-bin --release --locked`.
- Recorded results: 292 passing `xai-grok-sampling-types` tests, 188 passing
  `xai-grok-sampler` unit and integration tests, and a successful pager release
  build.

This is a dated integration record, not a promise that later revisions will
retain the same tree hash or counts. The recorded suite does not contain a
focused regression test that drives the patch-0015 approved-call reorder buffer
with completion order `[1, 0]`. The ordering contract above is grounded in the
implementation, but that missing direct test should be addressed.

## Live diagnosis

Enable raw sampling logs at runtime with:

```text
/debug sampling on
```

Startup alternatives are `--log-sampling` and
`GROK_LOG_SAMPLING=true`. Then issue an authenticated model turn that actually
requests multiple tools and inspect `~/.grok/logs/sampling.jsonl` for raw
function-call events. Check for `call_id` and, when supplied by the provider,
an item `id` such as `fc_…`.

Sampling logs contain raw prompts, tool arguments, tool results, and provider
responses. Treat the file as sensitive. A live observation cited as durable
verification should record its date, model, route, and sanitized evidence.

## Non-goals and limitations

- Requiring a backend to emit multiple calls merely because the request permits
  them.
- Strict original-turn ordering across approved calls and pre-flight failures.
- Immediate publication of a fast later approved call before an earlier slow
  call finishes.
- Canonical or realpath-based lock keys.
- Changing batch `search_replace` semantics from patch `0014`.
- Forcing sequential tool execution globally.

## Recommended follow-up coverage

Add focused tests where approved calls complete in `[1, 0]` and `[2, 0, 1]`
order, then assert ordered history and intentionally ordered post-flight
effects. Add a mixed approved/pre-flight-failure case to define whether strict
whole-turn ordering is required. If it is, allocate result slots from the
original assistant call list rather than only from the approved subset.

## Related

- [i0006: Apply multiple exact edits to one file as one validated batch](i0006_batch-file-edits.md)
