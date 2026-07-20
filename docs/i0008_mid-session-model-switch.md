# i0008: Safe mid-session model and provider switching

**Status:** implemented in patch `0016` (numbered `0017` before the former sampling-panic fix was folded into patch `0011`); release-profile tests verified

**Upstream:** `checkouts/grok-build`  
**Deliverable:** `patches/grok-build/0016-Gate-native-reasoning-replay-by-route-API-and-model.patch`  
**Implementation base:** `ba76b0a683fa52e4e60685017b85905451be17bc`

## Problem

Provider-native reasoning is opaque state, not portable conversation text:

- Responses uses `reasoning` items with `rs_*` IDs and `encrypted_content`.
- Anthropic Messages uses signed `thinking` blocks.
- Some Chat Completions providers expose a provider-specific `reasoning_content` channel.

Replaying that state to another model or inference route can fail:

```text
400: encrypted content could not be verified
404: Item with id 'rs_…' not found; items are not persisted
```

A model-name-only check is insufficient. Two providers or custom proxies can expose the same wire model slug while using different signing/encryption domains.

## Pi reference

Pi stores `provider`, `api`, and `model` on each assistant turn. Its request-time `transformMessages` preserves native thinking only when all three match the destination:

```ts
assistantMsg.provider === model.provider &&
assistantMsg.api === model.api &&
assistantMsg.model === model.id
```

For a foreign destination, pi drops opaque/redacted thinking, removes thought signatures, and converts visible thinking to ordinary assistant text. Saved history remains unchanged, so switching back to the exact origin can replay its native state.

## Implementation

Grok Build applies the same rule using an explicit `ReasoningOrigin`:

```text
normalized inference route + API backend + wire model
```

The sampler, not the caller, stamps the destination at the common request boundary. It normalizes the configured base URL before persistence by removing user info, password, query, fragment, and trailing slashes. No credentials or authorization headers enter chat history.

Every successfully completed assistant response is stamped with the same origin before it is persisted.

### Wire-time transformation

The shared sanitizer associates each reasoning sibling with its following assistant and runs before all three wire conversions:

- Responses
- Anthropic Messages
- Chat Completions

| Source versus destination | Outgoing behavior |
|---|---|
| Exact route + API + model match | Preserve native IDs, signatures, and encrypted content |
| Any field differs | Never emit native reasoning metadata |
| Foreign reasoning has visible text | Fold it into ordinary assistant text |
| Foreign reasoning is opaque only | Drop it from the outgoing request |
| Legacy source lacks provenance | Fail closed when the real destination is known; retain visible text only |

In-memory and on-disk history are never mutated by this transformation.

## Switching behavior

For Claude A → GPT/Grok → Claude A:

1. Claude A's thinking signature is saved with its Messages route/API/model origin.
2. The GPT/Grok request receives any visible Claude reasoning as ordinary text; it receives no Claude signature.
3. GPT/Grok reasoning is saved with its own origin.
4. On return to the exact Claude A origin, Claude A's original signature is replayed.
5. The intervening GPT/Grok native reasoning remains foreign and is text-only or dropped for Claude.

Claude A → Claude B, or Claude through route A → the same model slug through route B, does not replay route A's signature.

## Compatibility

`AssistantItem.reasoning_origin` is optional in JSONL. Existing sessions deserialize normally. Legacy opaque reasoning is not replayed once a real request destination has been stamped; visible reasoning remains available as assistant text. New responses immediately carry complete provenance.

## Tests

Release tests cover:

- exact-origin Responses encrypted reasoning replay;
- model, route, and API mismatches independently;
- `rs_*` and encrypted-only foreign item removal;
- exact-origin Claude thinking/signature replay;
- Responses → Claude conversion without foreign thinking/signatures;
- same Claude model through a different route;
- foreign reasoning on Chat Completions;
- legacy unknown provenance;
- non-mutating transforms and switch-back restoration;
- provenance JSONL backward compatibility;
- route normalization without credential persistence.

## Manual verification

```bash
checkouts/grok-build/target/release/xai-grok-pager
# /debug sampling on
```

Exercise:

1. Claude → GPT/Grok
2. GPT/Grok → Claude
3. Claude A → GPT/Grok → Claude A
4. Claude A → Claude B

In `~/.grok/logs/sampling.jsonl`, foreign requests must contain no native `rs_*` IDs, encrypted blobs, or Claude thinking signatures. Returning to the exact original route/API/model should replay its saved native state and complete successfully.
