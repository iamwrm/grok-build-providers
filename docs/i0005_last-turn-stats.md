# i0005: Last-prompt stats at turn end (cache read/write, TPS, cost)

**Status:** in progress — patch 0016 implemented; grounding run pending; stats patches not started
**Upstreams:** `checkouts/pi` + `../piagent-config/packages/ren-public-package/0012-last-turn.ts` (reference), `checkouts/grok-build` (patch target)
**Deliverable:** patch series `patches/grok-build/0016..`, continuing the i0001–i0004 stack
**Implementation branch:** `checkouts/grok-build`, branch `openai-oauth`, base `ba76b0a` (stack tip after i0004: `d76cf1c`)

## Goal

Every time the agent finishes a prompt cycle and returns to waiting for user
input, show aggregate metrics for that cycle in the TUI — modeled on the
piagent-config `0012-last-turn.ts` extension footer line:

```
1m12s ↑14 ↓3.7k TPS=42.6 R154k W39k CH94.2% CTX33:44 $0.823
```

(↑ input, ↓ output, TPS provider throughput, R cache read, W cache write,
CH cache hit ratio, CTX context-window usage start:end, $ cost.)

## Prior state in grok-build

- The shell aggregates per-prompt usage in `UsageTotals`
  (`xai-chat-state/src/usage.rs`) and ships it at turn end as `PromptUsage`
  (`xai-grok-shell/src/extensions/notification.rs`) on both terminal rails
  (`PromptResponse._meta.usage` and the durable `TurnCompleted` update) —
  but the TUI discards it; only headless mode consumes it.
- Cache **read** tokens (`cached_prompt_tokens`) are plumbed end to end.
  Cache **write** tokens (`cache_creation_input_tokens` on the Anthropic
  Messages wire) are parsed in `xai-grok-sampler/src/stream/messages.rs` but
  folded into `prompt_tokens` and dropped as a separate stat.
- The only per-turn display is the "Worked for 12.3s" scrollback marker
  (`SessionEvent::TurnCompleted`).

## Implemented patch series

### Patch 0016 — record raw request payloads in the sampling log

grok-build already had a hidden raw-wire recorder: `--log-sampling` /
`GROK_LOG_SAMPLING=1` routes `target: "sampling_log"` tracing events to
`~/.grok/logs/sampling.jsonl` (layer in
`xai-grok-telemetry/src/sampling_log.rs`), and every raw SSE chunk was
already logged verbatim (`event = "sse_chunk"`) for all three backends —
so raw **responses** (including untyped usage objects) were already
captured. Raw **requests** were not.

- `xai-grok-sampler/src/sampling_log.rs`: new `enabled()` (cached read of
  `GROK_LOG_SAMPLING`, same truthy values as the telemetry layer) and
  `log_request_body(backend, endpoint, body)` — serializes and emits
  `event = "request_body"` only when the log is enabled; serialization
  errors are logged, never propagated.
- `xai-grok-sampler/src/client.rs`: call it at all six send sites
  (chat completions, Responses, Messages × streaming and non-streaming),
  **after** all defaulting/shape functions (`apply_message_defaults` →
  `apply_anthropic_oauth_request_shape`, `apply_chatgpt_codex_request_shape`,
  post-serialize JSON patches), so the log shows the true wire body —
  including the Claude Code identity block and `cache_control` markers.
- Privacy note: with the flag on, `sampling.jsonl` now contains full prompt
  text in addition to the full response text it already contained. Off by
  default, hidden flag, existing size-trimming applies.

### Planned next

1. **Grounding run:** live Claude turn with `GROK_LOG_SAMPLING=1`; inspect
   `sampling.jsonl` to confirm request `cache_control` placement and the
   exact response usage shape (flat `cache_creation_input_tokens` vs. the
   newer `cache_creation: {ephemeral_5m_input_tokens, ...}` object, values
   across a multi-tool loop) before committing to field names.
2. **Patch 0017 — cache-write plumbing:** `TokenUsage.cache_creation_prompt_tokens`
   → `UsageTotals.cached_write_tokens` → `PromptUsageModel.cachedWriteTokens`
   (+ headless `cache_creation_input_tokens` / `cacheCreationInputTokens`).
3. **Patch 0018 — TUI stats line:** stop dropping `usage` in the pager's
   turn-completed handlers; extend the "Worked for …" marker with the stats
   line (display-only render block; CTX from pager-side `context_state`).
4. **Patch 0019 — docs.**

## Non-goals

- Live ticking footer while the prompt runs (per-response usage is not
  streamed to the pager mid-run; would need new wire plumbing).
- FCH (first-turn cache hit) and per-turn cache-write sparkline from the pi
  extension (`PromptUsage` is aggregate; needs a per-call series).
- OTel/telemetry export of cache-write tokens.

## Verification

- Patch 0016: `cargo check -p xai-grok-sampler` clean; sampler lib test
  suite passes (see below); `parse_enabled` unit test added.
