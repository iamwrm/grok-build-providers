# IV-0007: Improve Responses tool-call identity and concurrent result ordering

**Status:** implemented in durable patch `0015`; historical integration checkpoints recorded below
**Upstream:** `checkouts/grok-build`
**Deliverable:** `patches/grok-build/0015-Improve-Codex-parallel-tool-call-handling.patch`
**Implementation base:** `500129c714ad1b10e6095481f4a8387a2ec52649` (Grok `0.2.114`)
**Patch-0015 checkpoint:** commit `84c9b7f`; 15-patch tree `4fcba74364770269399a76cb310d3a3305b0cc94`
**Doctrine:** [DC-0001](DC-0001-agentic-workspace.md) — read before changing or retiring this initiative

## Lifecycle map

- **Why this exists:** permit multiple Responses tool calls, preserve native
  call identity across replay, and keep concurrent execution while making
  approved-call publication deterministic.
- **Durable implementation:** patch `0015` across sampler request shaping,
  Responses stream/history conversion, and the shared shell tool loop.
- **Known consumers:** ChatGPT Codex and generic Responses routes, cross-backend
  history projection, all providers using shared local-tool dispatch, and
  [IV-0006](IV-0006-batch-file-edits.md).
- **Key assumption:** approved-subset source ordering is sufficient; strict
  whole-turn ordering and canonicalized path locks remain outside this scope.
- **Evidence route:** rerun clean-room application, focused sampling-types and
  sampler tests, and the raw-sampling diagnosis under
  [Evidence and reproduction](#evidence-and-reproduction). Missing direct
  out-of-order completion tests remain documented follow-up work.

## Intent and lifecycle justification

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

## Evidence and reproduction

### Historical patch-0015 rebase checkpoint

- Patches `0001–0015` clean-room apply to `47348d1` and produce tree
  `4fcba74364770269399a76cb310d3a3305b0cc94`.
- At that checkpoint, sampling-types and sampler library suites passed 285/285
  and 69/69. Coverage included compound tool-call IDs, Responses replay,
  request shaping, and stream routing.
- Shell parallel-dispatch tests passed 13/13 while retaining concurrent
  execution and the ordered final-result publication contract.

### Integrated 16-patch rebase checkpoint

- Patch `0015` remains part of the durable `0001–0016` series.
- All 16 patches clean-room apply to base `500129c` and produce tree
  `f54122409a429e1071f6bb2a19bfcf984346adb6`; the clean-room pager-bin Cargo
  check passes.
- Current sampling-types and sampler library suites pass 299/299 and 174/174.
  Shell parallel-dispatch tests remain a previous-base checkpoint; the new
  build workflow compiles but does not run test suites. The native Windows
  release build is also previous-base evidence until the refreshed release
  workflow runs; native Windows development compilation passes.

This is an integration checkpoint, not a promise that later revisions will
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
verification should record its semantic checkpoint, model, route, and
sanitized evidence.

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

- [IV-0006: Apply multiple exact edits to one file as one validated batch](IV-0006-batch-file-edits.md)
