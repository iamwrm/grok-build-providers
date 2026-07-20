# i0007: Improve Codex parallel tool-call handling

**Status:** implemented in patch `0015`; release-profile unit tests and clean-room apply verified
**Upstream:** `checkouts/grok-build`
**Deliverable:** `patches/grok-build/0015-Improve-Codex-parallel-tool-call-handling.patch`
**Implementation base:** `ba76b0a683fa52e4e60685017b85905451be17bc`

## Goal

Make multi-tool turns on the OpenAI Codex Responses path behave like pi’s
contract: allow parallel function calls on the wire, preserve Responses item
ids for history replay, and keep local tool-result history in assistant source
order even when tools execute concurrently.

## Patch 0015

### Request

- Responses `CreateResponse` sets `parallel_tool_calls: true`.
- Codex request shaping also forces `parallel_tool_calls: true` (with
  `store: false` and the rest of the ChatGPT Codex contract).

### History / wire IDs

Codex SSE items carry both:

- `call_id` (e.g. `call_…`) used to pair `function_call_output`
- Responses item `id` (e.g. `fc_…`) used for item identity / reasoning pairing

Grok Build now stores compound tool-call ids as `call_id|item_id` when the
item id is present (pi-style), and splits them on emit:

- `FunctionCall.call_id` / `FunctionCall.id` restored for Responses input
- `FunctionCallOutput.call_id` uses the call half only
- Chat Completions and Anthropic Messages use the call half only

### Tool loop

Approved tools still run concurrently (with same-file write locks). Final
conversation history commits tool results in **assistant source order**:
out-of-order completions are buffered until the next expected index is ready.

## Verification

- `xai-grok-sampling-types` release suite: 279/279
- Focused helpers/round-trip tests for compound tool-call ids
- Codex request-shape tests assert `parallel_tool_calls == true`
- Multi-function-call stream index test still passes
- Clean-room `git am` of the full patch series onto `ba76b0a`
- Release build of `xai-grok-pager-bin` succeeds

Live Codex dumps via `GROK_LOG_SAMPLING=true` / `--log-sampling` show raw
`function_call` SSE frames with both `call_id` and `fc_…` item ids under
`~/.grok/logs/sampling.jsonl`.

## Non-goals

- Same-file realpath mutation queue (still string-path locks)
- Changing multi-edit `search_replace` semantics beyond prior batch work
- Forcing sequential tool execution globally
