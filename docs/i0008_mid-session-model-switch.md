# i0008: Mid-session model switch (foreign encrypted reasoning)

**Status:** implemented in patch `0017`; release-profile unit tests verified  
**Upstream:** `checkouts/grok-build`  
**Deliverable:** `patches/grok-build/0017-Strip-foreign-encrypted-reasoning-on-model-switch.patch`  
**Implementation base:** `ba76b0a683fa52e4e60685017b85905451be17bc`

## Problem

Switching models mid-session (e.g. Grok → GPT-5.6 Sol / Codex Responses) failed with:

```text
invalid_request_error: The encrypted content for item rs_… could not be verified.
Reason: Encrypted content could not be decrypted or parsed.
```

Root cause: Responses history round-trips `reasoning` items with
`encrypted_content` (requested via `include: ["reasoning.encrypted_content"]`,
`store: false`). That blob is **model-bound**. Replaying Grok-produced
`rs_*` + `encrypted_content` to OpenAI Codex returns HTTP 400.

Previous behaviour detected the 400 and told the user to start a new session
(`encrypted_content_mismatch`). That is fail-closed but blocks mid-session
`/model` switches.

## How pi solves it

In `packages/ai/src/api/transform-messages.ts`, every assistant turn is
annotated with `provider` / `api` / `model`. At request build time:

```ts
const isSameModel =
  assistantMsg.provider === model.provider &&
  assistantMsg.api === model.api &&
  assistantMsg.model === model.id;
```

For thinking blocks:

- **Redacted** (opaque encrypted): keep only if `isSameModel`, else **drop**
- **Signed / non-empty thinking**: keep structured form only if same model;
  otherwise convert visible text to plain text or drop empties
- **Tool-call thought signatures**: stripped on cross-model

History on disk is unchanged; the transform is **wire-time only**, so
switching back to the original model can still replay its encrypted blobs.

## Fix (patch 0017)

In `xai-grok-sampling-types` `build_responses_input`:

1. Associate each `ConversationItem::Reasoning` with the following
   `AssistantItem.model_id` (skipping sibling reasoning / backend tool calls).
2. If `source_model_id == current_request.model` (or either side is unknown,
   for legacy sessions without a stamped model id): pass through unchanged
   (native `reasoning` item with `id` + `encrypted_content`).
3. If models **differ**:
   - Do **not** emit a typed `reasoning` item at all
     - foreign `encrypted_content` → HTTP 400 decrypt failure
     - foreign `rs_*` `id` with `store: false` → HTTP 404
       `"Item … not found. Items are not persisted"`
   - Fold visible summary/content into the following assistant text
     (pi converts foreign thinking blocks to plain `text`)
   - Drop encrypted-only foreign items

On-disk / in-memory chat history is not mutated.

The existing `encrypted_content_mismatch` terminal path remains as a
last-resort if a backend still rejects the scrubbed history.

## Tests

- `build_responses_input_strips_foreign_encrypted_reasoning_on_model_switch`
- `build_responses_input_drops_encrypted_only_foreign_reasoning`
- `build_responses_input_keeps_same_model_encrypted_reasoning`
- Existing multi-turn / cache-prefix reasoning tests still pass (no `model_id`
  on synthetic assistants → encrypted kept)

## Manual retest

```bash
checkouts/grok-build/target/release/xai-grok-pager
# start on a Grok model, complete a reasoning turn
# /model → GPT-5.6 Sol (or any Codex model)
# send a follow-up prompt
```

With sampling on, the Codex `request_body` must **not** contain
`encrypted_content` on reasoning items whose prior assistant `model_id` was
Grok. The turn should complete without the 400.
