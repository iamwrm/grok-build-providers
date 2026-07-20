# i0005: Last-prompt stats at turn end (cache read/write, TPS, cost)

**Status:** implemented in consolidated patches `0011–0013`; raw-wire grounding and release-profile verification complete; user-guide patch deferred
**Upstreams:** `checkouts/pi` + `../piagent-config/packages/ren-public-package/0012-last-turn.ts` (reference), `checkouts/grok-build` (patch target)
**Deliverable:** patches `0011–0013`, continuing the i0001–i0004 stack
**Implementation branch:** clean-room series based on `ba76b0a`; full tip `d8c0564`, tree `0a219b73e452c3ce19ea64cd7679dde3bf1e7a89`

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

### Patch 0011 — dynamically controllable raw sampling logs

grok-build already had a hidden raw-wire recorder: `--log-sampling` /
`GROK_LOG_SAMPLING=1` routes `target: "sampling_log"` tracing events to
`~/.grok/logs/sampling.jsonl` (layer in
`xai-grok-telemetry/src/sampling_log.rs`), and every raw SSE chunk was
already logged verbatim (`event = "sse_chunk"`) for all three backends —
so raw **responses** (including untyped usage objects) were already
captured. Raw **requests** were not.

- `xai-grok-sampler/src/sampling_log.rs`: one process-wide `AtomicBool`,
  initialized from `GROK_LOG_SAMPLING`, gates both request serialization and
  telemetry. `log_request_body(backend, endpoint, body)` emits
  `event = "request_body"` only when enabled; serialization errors are logged,
  never propagated.
- `xai-grok-sampler/src/client.rs`: call it at all six send sites
  (chat completions, Responses, Messages × streaming and non-streaming),
  **after** all defaulting/shape functions (`apply_message_defaults` →
  `apply_anthropic_oauth_request_shape`, `apply_chatgpt_codex_request_shape`,
  post-serialize JSON patches), so the log shows the true wire body —
  including the Claude Code identity block and `cache_control` markers.
- Runtime controls are `/debug sampling on|off|status`; startup controls remain
  `GROK_LOG_SAMPLING=true` and hidden `--log-sampling`. The telemetry layer
  keeps dynamic callsite interest so startup-off can become runtime-on.
- Privacy note: when enabled, `sampling.jsonl` contains full prompts, tool
  calls/results, and responses. It is off by default and enabling prints an
  explicit warning; existing size trimming applies.

### Grounding run (2026-07-20, live)

Headless two-call cycle (`claude-sonnet-4-6:low`, one terminal tool call)
with `GROK_LOG_SAMPLING=true`; raw payloads from `sampling.jsonl`:

- **Request shape:** exactly one `cache_control: {type: "ephemeral"}`
  breakpoint exists, on the trailing (real) system block; the identity
  block, tools, and all conversation messages carry none. (Note
  `GROK_LOG_SAMPLING=1` is rejected by clap's bool parser on the hidden
  `--log-sampling` flag — use `true`.)
- **Response usage (verbatim):**

  ```json
  message_start:  {"input_tokens":2747,"cache_creation_input_tokens":10605,
                   "cache_read_input_tokens":0,
                   "cache_creation":{"ephemeral_5m_input_tokens":10605,"ephemeral_1h_input_tokens":0},
                   "output_tokens":5,"service_tier":"standard",...}
  message_delta:  {"input_tokens":2747,"cache_creation_input_tokens":10605,
                   "cache_read_input_tokens":0,"output_tokens":91,
                   "output_tokens_details":{"thinking_tokens":0}}
  call 2 start:   {"input_tokens":2859,"cache_creation_input_tokens":0,
                   "cache_read_input_tokens":10605,...}
  ```

- **Conclusions:** the flat `cache_creation_input_tokens` is present and
  authoritative on both `message_start` and `message_delta` — the typed
  `MessagesUsage`/`MessageDeltaUsage` fields grok-build already parses are
  correct, so patch `0012` can plumb them as planned. The newer
  `cache_creation` 5m/1h breakdown appears on `message_start` only
  (currently ignored by serde; not needed). Anthropic's `input_tokens` is
  uncached-only: total prompt = input + read + write (matches the
  sampler's summation).
- **Efficiency observation (out of scope here, candidate future patch):**
  because the only breakpoint is on the system block, the growing
  conversation history is never cached — call 2 re-billed 2,859 uncached
  input tokens and that number will grow every call. pi/Claude Code also
  place a `cache_control` marker on the trailing message. The planned
  W-per-cycle stat will make this cost visible.

### Patch 0012 — track prompt cache writes through usage accounting

- Added serde-compatible `TokenUsage.cache_creation_prompt_tokens` and
  populated it from Anthropic `cache_creation_input_tokens`.
- A live Codex check found that OpenAI Responses also emits
  `input_tokens_details.cache_write_tokens`, although the pinned
  `async-openai` type drops it. Patch `0012` preserves it from the raw terminal
  event through the existing process-local response-metadata side channel.
- Added `UsageTotals.cached_write_tokens`, folded per prompt and per model,
  then projected it as ACP `cachedWriteTokens`, last-call
  `_meta.cachedWriteTokens`, and headless `cache_creation_input_tokens` /
  `cacheCreationInputTokens`. All new serde fields default to zero.
- Kept the frozen headless token identity unchanged: headless `input_tokens`
  subtracts cache reads only, so cache writes are a reported subset rather
  than a second subtraction.

### OpenAI Codex cache grounding (2026-07-20, live)

Two identical `openai-codex/gpt-5.5:low` tool-using cycles showed automatic
prefix caching works without explicit request cache markers:

| Cycle/call | input | cached | cache write |
|---|---:|---:|---:|
| first / initial | 11,039 | 0 | 0 |
| first / tool follow-up | 11,108 | 0 | 0 |
| repeat / initial | 11,039 | 10,752 | 0 |
| repeat / tool follow-up | 11,108 | 10,752 | 0 |

The zero write count is a real wire value (automatic OpenAI cache population
is not separately billed), not an absent field. This is unlike Anthropic's
explicit, billed cache creation.

### Codex compatibility discovered during grounding

Live OpenAI Responses turns exposed the informational `response.metadata` SSE
event, which the pinned typed dependency does not model. Exact-event absorption
is now owned by i0001 patch `0002` (the Codex transport); all other unknown
events remain fail-closed.

### Patch 0013 — show prompt-cycle usage on completed-turn markers

- Carries aggregate `PromptUsage` on both terminal rails: durable
  `TurnCompleted` and legacy `x.ai/session/prompt_complete` (important because
  either can reach a viewer first).
- Driver `PromptResponse`, viewer finalization, and lost-RPC reconciliation all
  converge on the same optional `TurnUsageStats`; old shells and marker types
  keep the legacy text.
- Extends the display-only completed marker to:

  ```text
  Worked for 1m12s · ↑14 ↓3.7k TPS=42.6 R154k W39k CH79.8% CTX33:44 $0.823
  ```

  `↑` is the uncached remainder (`full − read − write`) to avoid double
  counting; TPS uses aggregate API duration (excludes tools); CH is
  `read/(uncached+read+write)`; costs appear only when complete/trusted.
- Captures context tokens at local and viewer turn boundaries; final context
  comes from the latest streamed context state. Unknown starts render `?`.
- Parked, bash, subagent, cancelled, failed, and replacement-UX markers remain
  unchanged.

### Deferred

- Upstream user-guide documentation for the marker and raw-wire privacy warning.
  The durable initiative doc covers the patch stack for now.

## Non-goals

- Live ticking footer while the prompt runs (per-response usage is not
  streamed to the pager mid-run; would need new wire plumbing).
- FCH (first-turn cache hit) and per-turn cache-write sparkline from the pi
  extension (`PromptUsage` is aggregate; needs a per-call series).
- OTel/telemetry export of cache-write tokens.

## Verification

- Patch `0011`: initial raw-request recorder verification passed sampler 160/160;
  final runtime gate verification passed sampler 2/2, shell ACP handler 2/2,
  pager `/debug` 8/8, focused pager dispatch/status 2/2, and dynamic callsite
  gating 1/1 in release profile.
- Live pre-consolidation grounding: headless
  `-m anthropic/claude-sonnet-4-6:low` tool-using turn completed on attempt
  1 with `GROK_LOG_SAMPLING=true`; `sampling.jsonl` contained 3
  `request_body` events (one per LLM call, plus a `responses`-backend
  auxiliary call) alongside the existing `sse_chunk` stream.
- Patch `0012`: sampler 161/161; chat-state 340/340; focused shell
  `PromptResponseMeta` and headless projection tests pass; final
  `cargo check -p xai-grok-shell --all-targets` clean.
- i0001 patch `0002`: sampler 162/162 including exact-name / JSON-type
  `response.metadata` absorption.
- Patch `0013`: pager session-event tests 41/41; turn-completion tests 19/19.
- Consolidation clean-room: all 13 patches apply to `ba76b0a`, final tree
  `0a219b73e452c3ce19ea64cd7679dde3bf1e7a89` exactly matches the old 20-patch
  tree, and `CARGO_INCREMENTAL=0 cargo build --release --locked
  -p xai-grok-pager-bin` passes.
