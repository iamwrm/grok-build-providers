# IV-0010: Prefer Responses server-side compaction for GPT models

**Status:** implemented in durable patch `0017`; live GPT compaction and checkpoint replay plus explicit UI outcome reporting verified, live failure fallback not intentionally induced  
**Upstream:** `checkouts/grok-build`  
**Reference:** `IgorWarzocha/howaboua-pi-stuff` remote compaction v2 implementation  
**Deliverable:** `patches/grok-build/0017-Prefer-Responses-remote-compaction-for-GPT-models.patch`  
**Implementation base:** `500129c714ad1b10e6095481f4a8387a2ec52649` (Grok `0.2.114`)  
**Patch-0017 checkpoint:** source commit `e555274`; clean-room 17-patch head `b4b1d76`; tree `7ffd123dca8e25be6461cda7328f2b546406bb98`; patch SHA-256 `85f0a346879fe26258c4e14cac00c84a5149ce04af25adf6df615aa551dd0ba9`  
**Doctrine:** [DC-0001](DC-0001-agentic-workspace.md) — read before changing or retiring this initiative

## Lifecycle map

- **Why this exists:** prefer the provider's opaque Responses compaction for
  GPT models while retaining Grok Build's portable text summarizer as a
  failure fallback.
- **Durable implementation:** patch `0017`; the disposable reference clones
  and implementation checkout are not lifecycle roots.
- **Known consumers:** generic Responses sampling, ChatGPT Codex from
  [IV-0001](IV-0001-openai-oauth.md), exact-origin native-state replay from
  [IV-0008](IV-0008-mid-session-model-switch.md), session persistence, manual
  or automatic shell compaction, and pager/headless completion reporting.
- **Key assumptions:** a case-insensitive `gpt` substring is the requested
  capability convention; compatible servers implement the v2 trigger/header
  contract; encrypted checkpoints are valid only on the exact route, API, and
  wire model that created them.
- **Evidence route:** clean-room apply the complete patch stack, run the Rust
  suites and pager check below, then repeat the live success/replay and mock
  fallback checks under [Reproduction procedure](#reproduction-procedure).

## Decision: a patch is required

The pinned base and fetched upstream `dd04f397` were searched before adding
this initiative. Neither sends a Responses input item shaped as
`{"type":"compaction_trigger"}`, recognizes `compaction` output items, or
persists and replays an encrypted v2 checkpoint.

Grok Build does already have a distinct xAI compaction mechanism. Its built-in
Grok 4.5 model enables `compaction_at_tokens` and `compactions_remaining`,
which become the request headers `x-compaction-at` and
`x-compactions-remaining`. That is existing official-route behavior, not the
OpenAI/Codex remote compaction v2 wire contract. Patch `0017` leaves it
untouched: `grok-4.5` does not match the GPT eligibility gate.

The result is intentionally two lanes:

| Lane | Behavior |
|---|---|
| Responses backend and wire model contains `gpt` (case-insensitive) | Try remote compaction v2 once, then use portable compaction on any returned failure |
| Any other model/API, including the existing Grok 4.5 route | Preserve existing compaction behavior |

## Reference investigation

The implementation was derived from
`IgorWarzocha/howaboua-pi-stuff` at `a1af4af`, especially:

- `a5a98cf` — “Align Codex compaction and Code Mode continuation behavior”;
- `088be70` — native compaction cache lanes and checkpoint reuse;
- `e9f30ea`, `744b0d5`, `67f6fdf`, and `a7f4e55` — later cache-continuity and
  compaction fixes;
- `packages/pi-codex-conversion/src/adapter/compaction/remote-v2-client.ts`;
- `remote-v2-history.ts` and `compaction.ts`;
- `providers/openai-responses/compaction-v2-feature.ts`.

Current OpenAI Codex source at `4642370` independently corroborated the
protocol in `codex-rs/core/src/compact_remote_v2*.rs` and the protocol
`ResponseItem::{Compaction, CompactionTrigger}` variants.

The relevant v2 contract is:

1. Use the ordinary streamed `/responses` operation.
2. Merge `remote_compaction_v2` into `x-codex-beta-features`.
3. Append a final input item `{"type":"compaction_trigger"}`.
4. Require normal stream completion and exactly one valid `compaction` output
   (`compaction_summary` is accepted as a deployed alias).
5. Persist the encrypted output as an opaque checkpoint and replay it as a raw
   Responses input item on later requests.
6. On failure, return control to the portable client-side summarizer.

Custom `/compact` guidance cannot parameterize the v2 trigger. Grok Build logs
that it is ignored if the remote attempt succeeds; if remote compaction fails,
the portable fallback still receives the guidance.

## Patch 0017

### Eligibility and fallback

`should_try_remote_compaction_v2` requires both:

```text
api_backend == Responses
lowercase(wire_model).contains("gpt")
```

An eligible compaction sends the full active Responses transcript and active
tool definitions through the current sampling client. A setup, HTTP, SSE,
timeout, incomplete/failed response, malformed checkpoint, or count mismatch
returns an error to the shell. The shell logs the
failure and continues through the pre-existing two-pass/full-replacement
portable compaction path.

There is no negative capability cache. Each later compaction on an eligible
model may try v2 again before falling back.

### Completion outcome in the UI

The shell reports the implementation that actually completed rather than
asking the pager to infer it from model names, logs, or summary shape:

| `AutoCompactCompleted.strategy` | Meaning | TUI completion text |
|---|---|---|
| `server_side` | A valid provider checkpoint completed and was installed | `Server-side compaction succeeded: …` |
| `portable_fallback` | The remote attempt returned an error, then the portable summarizer completed | `Portable fallback succeeded: …` |
| `portable` | No remote v2 attempt was eligible; normal portable compaction completed | Existing `Context compacted: …` text |
| absent/unknown | Older or newer incompatible shell payload | Existing legacy text |

The field is optional on the wire for replay/backward compatibility, and
unknown future values deserialize safely. The strategy is fixed at the
remote-attempt branch and returned only after the entire compaction operation
succeeds, so a remote error followed by portable success cannot be mislabeled
as server-side success. Failed portable fallback emits no completion event.

Auto-compaction keeps the outcome with its deferred completion marker while it
waits for the next provider-confirmed token count. Manual `/compact` flushes
the same structured marker when the command response arrives instead of
replacing it with the old generic command marker. Headless plain output uses
the same labels, and streaming JSON adds the same `strategy` value. The start
indicator stays generic because the final strategy is not known until the
remote attempt returns.

### Wire handling

The pinned `async-openai` version has no typed compaction input/output variant,
so patch `0017` adds narrowly scoped raw-JSON bridges:

- native checkpoints are paired with their final Responses input positions and
  injected after typed serialization;
- the trigger is appended only for the dedicated remote-compaction operation;
- an existing comma-separated `x-codex-beta-features` value is preserved and
  merged with `remote_compaction_v2`;
- compaction lifecycle events are collected and hidden from the older typed
  event parser;
- terminal response output is sanitized before typed deserialization and used
  as a checkpoint fallback when no completed lifecycle item was delivered;
- completed lifecycle items are authoritative because terminal output can omit
  them or serialize optional fields differently.

A valid output requires non-empty `encrypted_content`, an optional string
`id`, and optional object metadata whose supported `turn_id` is absent, null,
or a string. The opaque encrypted value is validated without trimming or other
mutation. Durable replay canonicalizes the item type to `compaction` and never
sends process-local provenance.

### Persistence and replay

`ResponsesCompactionItem` is a first-class `ConversationItem`. It survives
session JSONL persistence and carries the same non-secret trust-domain key
introduced by IV-0008:

```text
normalized route + API backend + wire model
```

The checkpoint is emitted only when all three fields exactly match the actual
request destination. Foreign or legacy checkpoints fail closed and are not
sent to Chat Completions, Anthropic Messages, or another Responses trust
domain. Stored history is not mutated, so switching back to the exact origin
can replay it again.

After successful remote compaction, the normal Grok scaffold retains the real
user query before the checkpoint and places newly rendered state reminders
after it. Assistant/tool tail content is not duplicated because it is already
represented by the opaque checkpoint. Opaque checkpoints are also omitted from
portable fork-background and transcript-segment rendering.

A later v2 compaction on the same origin receives the previous checkpoint plus
the live tail before the new trigger. A portable fallback on that same origin
can likewise include the prior checkpoint in its summarization request. If the
provider no longer accepts even replay of its old checkpoint, the portable
attempt can still fail; an encrypted checkpoint cannot be reconstructed or
made portable client-side.

## Files affected

| Area | Change |
|---|---|
| `xai-grok-sampling-types/src/conversation.rs` | Durable checkpoint type, canonical wire parsing/replay, exact-origin gate, raw input positioning |
| `xai-grok-sampling-types/src/types.rs` | Raw Responses input entries on the request wrapper |
| `xai-grok-sampler/src/client.rs` | Feature header, trigger injection, compaction event collection/validation, dedicated v2 operation |
| `xai-grok-shell/src/session/helpers/session_compact.rs` | GPT/Responses eligibility and remote request construction |
| `xai-grok-shell/src/session/compaction.rs` | Remote-first orchestration, portable fallback, and authoritative completion strategy |
| `xai-grok-shell/src/extensions/notification.rs` | Backward-compatible `server_side` / `portable` / `portable_fallback` completion field |
| `xai-grok-pager` | TUI and headless outcome labels; deferred auto/manual completion propagation |
| `xai-chat-state` | Checkpoint accounting, persistence support, and post-compaction scaffold |
| shell classifiers and subagent rendering | Treat the checkpoint as opaque, non-text state |

## Evidence and reproduction

### Automated evidence

On the patch source tree:

- `xai-grok-sampling-types --lib`: **301/301 passed**;
- `xai-grok-sampler --lib`: **175/175 passed**;
- `xai-chat-state --lib`: **352/352 passed**;
- `cargo check -p xai-grok-pager-bin --locked`: passed;
- `cargo test -p xai-grok-pager --lib compaction --locked -j 1`:
  **18/18 passed**, including strategy rendering, deferred propagation, manual
  flush, and notification-wire compatibility;
- `cargo fmt --all -- --check` and `git diff --check`: passed.

Focused protocol coverage verifies canonical alias handling, byte-preserving
encrypted content, durable serde, exact-origin replay, retained item position,
final trigger placement, previous-checkpoint reuse, beta-feature header
merging, completed-stream collection, and post-compaction checkpoint ordering.
The pager checkpoint additionally verifies exact server-side and portable
fallback labels, legacy rendering, strategy survival through deferred
auto-compaction, structured manual completion, and old payload deserialization.

The complete `0001–0017` series clean-room applies to `500129c`, producing
head `b4b1d766d0a356075d05c93eafea84aac7a8d603` and tree
`7ffd123dca8e25be6461cda7328f2b546406bb98`, identical to the patch source
tree. The clean-room pager-bin Cargo check also passes.

A shell test-profile build was attempted with one Cargo job but the host killed
`rustc` with SIGKILL while compiling the very large `xai-grok-shell` test
crate. This was a resource failure, not a Rust diagnostic; the production
pager graph and the three directly affected library suites pass.

### Live provider checkpoint — 2026-07-31

A temporary credential-gated integration harness exercised the patched sampler
against the ChatGPT Codex Responses route with wire model `gpt-5.5`. It read an
already-valid local OAuth access token at runtime, printed no credential or
checkpoint content, and was removed after the run so CI does not depend on a
personal account.

Observed results:

- the server completed the v2 request in **2,995 ms**;
- exactly one checkpoint was accepted, with an item ID and **1,208 encrypted
  bytes**;
- the immediate next request replayed that checkpoint to the same exact
  route/API/model origin;
- marker `IV0010_LIVE_MARKER_7319` appeared only in an assistant turn sent
  before compaction and nowhere in the replay request outside the encrypted
  checkpoint;
- the model recovered that exact marker from compacted context and returned it
  verbatim;
- the end-to-end live test passed in **5.65 s** with no retry or portable
  fallback.

This verifies that the live server accepted the request emitted by patch
`0017`, returned a usable encrypted checkpoint, and decrypted that checkpoint
on the next turn. Live failure fallback was not deliberately induced against
the working account; the remote-error-to-portable branch remains locally
verifiable with an unsupported mock endpoint.

### Reproduction procedure

To repeat the live success check:

1. Select a GPT model using the Responses backend (for example an
   `openai-codex/gpt-*` model).
2. Enable sensitive sampling diagnostics with `/debug sampling on`.
3. Build enough context to trigger compaction or invoke `/compact`.
4. Confirm the compaction request's final input item is
   `compaction_trigger`, the request carries the merged beta feature, and the
   persisted compacted history contains a `responses_compaction` checkpoint.
5. Confirm the completion line says `Server-side compaction succeeded`, then
   continue the session and confirm the raw `compaction` item is replayed
   before the live tail and the turn completes without portable summary text.

To verify fallback, point an eligible GPT/Responses configuration at a mock or
endpoint that rejects the feature/trigger, invoke compaction, and confirm the
warning is followed by the existing portable summary request, a usable text
summary, and a `Portable fallback succeeded` completion line.

Sampling logs and persisted checkpoints contain prompts, tool data, and opaque
provider state. Treat them as sensitive and sanitize any retained evidence.

## Non-goals and limitations

- Capability discovery or a per-route support cache; the requested model-name
  convention is used deliberately.
- Enabling this v2 protocol for non-GPT names.
- Replacing or modifying Grok 4.5's existing xAI compaction headers.
- Making encrypted checkpoints portable across routes, APIs, or models.
- Preserving the full pre-compaction context after switching away from the
  checkpoint's exact origin; only retained user/scaffold context remains
  portable.
- WebSocket-specific cached continuation, request shrinking, or transport
  retries from the reference implementation.
- Claiming a live unsupported-provider fallback checkpoint until that separate
  failure-path reproduction is run.
