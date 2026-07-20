# i0002: Add `max` thinking level (above `xhigh`) to grok-build

**Status:** implemented and exported
**Upstreams:** `checkouts/pi` (reference semantics), `checkouts/grok-build` (patch target)
**Deliverable:** consolidated patch `0005` in `patches/grok-build/`
**Implementation branch:** clean-room series based on `ba76b0a`; i0002 boundary `a7b5601`, tree `7c51dd6e240654ea1ab684ccf452a3e2536d0226`
**Depends on:** i0001 (OpenAI Codex OAuth provider, patches `0001–0004`)

## Patch ownership

- Patches `0001`–`0004` in `patches/grok-build/` are owned by **i0001**; this
  work stacks on top.
- Patch `0005` is owned by **i0002** and contains both code and user docs. If it
  stops applying cleanly, fix and re-export that patch as one coherent change.

## Anthropic interaction

Patch `0005` introduces the canonical `Max` level and OpenAI Responses wire
workaround. The final Anthropic-specific mapping lives in i0003 patch `0008`:
`Xhigh → "xhigh"` on native-supporting models and `Max → "max"`, with per-model
menu gating. The pre-consolidation series briefly collapsed both values to
`"max"`; that intermediate state is historical only and is documented in
[patch-history.md](patch-history.md).

## Goal

Make `max` a real reasoning-effort level **above** `xhigh`, sent verbatim on the
wire for models that support it (pi parity: the gpt-5.6 family), while keeping
today's behavior — where `max` is merely a UX alias of `xhigh` — for every
other model.

```
/model openai-codex/gpt-5.6-sol:max
grok -m openai-codex/gpt-5.6-sol:max -p "hello"
/effort max
```

## How pi does it (reference)

- `packages/ai/src/types.ts`: `ThinkingLevel = "minimal" | "low" | "medium" |
  "high" | "xhigh" | "max"` — `max` is a distinct level, ordered last.
- `packages/ai/src/models.ts`: `EXTENDED_THINKING_LEVELS` ends `…, "xhigh",
  "max"`. `getSupportedThinkingLevels` only offers `xhigh`/`max` when the
  model's `thinkingLevelMap` explicitly maps them; `clampThinkingLevel` clamps
  an unsupported request to the nearest supported level (upward first, then
  downward — so `max` on an xhigh-only model degrades to `xhigh`).
- `packages/ai/scripts/generate-models.ts`:
  - `supportsOpenAiXhigh(id)` — gpt-5.2 through gpt-5.6;
  - `supportsOpenAiMax(model)` — **gpt-5.6 only**, on the
    `openai-responses` / `azure-openai-responses` / `openai-codex-responses` /
    `openai-completions` APIs → merges `{ max: "max" }`;
  - Anthropic adaptive-thinking models also map `max`; active i0003 patch
    `0008` owns native per-model `xhigh` differentiation.
- `packages/ai/src/api/openai-codex-responses.ts` (and `openai-responses.ts`):
  the effort string is passed through `thinkingLevelMap` and lands on
  `reasoning.effort` verbatim — i.e. the wire value is literally `"max"`.

## grok-build integration points (found in exploration)

- `crates/codegen/xai-grok-sampling-types/src/types.rs` — canonical
  `ReasoningEffort` enum (`None…Xhigh`). Today `FromStr` maps
  `"xhigh" | "max" => Xhigh` ("max is a CLI/UX alias of xhigh") and the error
  message plus `parse_canonical_effort_token` document that alias. Also here:
  `to_responses_api` / `from_responses_api` (bridges to async-openai),
  `to_messages_api` (final Anthropic mapping is owned by i0003 patch `0008`),
  `ReasoningEffortOption { id, value, label, … }` (per-model
  effort menus where `id` is presentation/input and `value` is the wire value).
- **Hard constraint:** `rs::ReasoningEffort` is
  `async_openai::types::responses` from crates.io (`async-openai 0.33`,
  re-exported in `sampling-types/src/lib.rs`). It has `None…Xhigh`, **no
  `Max`**, no `#[serde(other)]` leniency. We do not fork or patch the dep;
  instead we use grok-build's existing JSON seams:
  - requests are serialized to `serde_json::Value` and patched post-serialize
    (`patch_reasoning_text_types`, `apply_chatgpt_codex_request_shape` in
    `xai-grok-sampler/src/client.rs`);
  - `deserialize_response_event` (client.rs) already has a
    sanitize-JSON-and-retry fallback, and `apply_terminal_event_overrides`
    already peeks the raw JSON of terminal SSE events;
  - `CreateResponseWrapper` already carries process-local fields the sampler
    strips before sending (`trace`) — precedent for a side-channel field.
- `crates/codegen/xai-grok-shell/src/agent/config.rs` —
  `openai_codex_model_entries()` (patch `0004`): per-model
  `reasoning_efforts` menus, currently `low|medium|high|xhigh` for all seven
  Codex models.
- `crates/codegen/xai-grok-shell/src/agent/models.rs` —
  `model_offers_reasoning_effort`: server menu when present, else legacy
  built-in `low..xhigh` set.
- `crates/codegen/xai-grok-pager/src/acp/model_state.rs` —
  `resolve_effort_token_for`: menu-id match first, then canonical-level match
  **only if the model's menu offers that value** (unsupported levels are
  rejected, not clamped).
- `crates/codegen/xai-grok-pager/src/app/cli.rs` +
  `src/slash/commands/model.rs` — pi-style `provider/model:effort` references
  (patch `0003`) resolve the suffix via `parse_canonical_effort_token` and
  `resolve_effort_for_model`; they pick up a new enum variant with no parser
  changes.
- `crates/codegen/xai-grok-agent/src/config.rs` — agent-definition `Effort`
  enum **already has a distinct `Max`** (`VALID_VALUES` includes `"max"`); it
  currently collapses to xhigh at the sampler boundary via the string parse.
- `crates/codegen/xai-grok-sampling-types/src/conversation.rs` —
  `response_to_conversation_items` stamps per-response effort provenance from
  the echoed `response.reasoning.effort` via `from_responses_api`.

## Implemented patch series

### Patch 0005 — `max` reasoning effort and user documentation

**A. Canonical enum** — `xai-grok-sampling-types/src/types.rs`

- Add `Max` variant ordered above `Xhigh`.
- `as_str() → "max"`; serde `lowercase` gives `"max"` for free.
- `FromStr`: `"max" → Max` (alias removed); update the error string and the
  `parse_canonical_effort_token` doc comment; rewrite the upstream
  `reasoning_effort_from_str_accepts_max_as_xhigh` test to the new semantics.
- `to_messages_api`: `Max → Some("max")`; active i0003 patch `0008` owns
  native `Xhigh → "xhigh"` and per-model gating.
- `to_responses_api`: `Max → rs::ReasoningEffort::Xhigh` as a **typed
  placeholder only** (async-openai cannot represent `Max`); the true wire
  value is restored by the post-serialize patch below. Document this loudly.
- Exhaustive matches mean the compiler flags every other site that must
  handle `Max`.

**B. Request encoding** — `xai-grok-sampler/src/client.rs` (+
`CreateResponseWrapper` in sampling-types)

- Carry the canonical `Option<ReasoningEffort>` on `CreateResponseWrapper` as
  a process-local field (like `trace`, never serialized).
- After `serde_json::to_value(&request.inner)`, when the canonical effort is
  `Max`, patch `body["reasoning"]["effort"] = "max"` — in **both** the
  non-streaming (`create_response`) and streaming request paths, before the
  Codex reshape (which preserves `reasoning.effort`).
- Chat Completions path: grok-build serializes its own enum there, so `"max"`
  flows natively — verify, no change expected.

**C. Response decoding** — `client.rs` + `conversation.rs`

- `deserialize_response_event` sanitize path and the non-streaming
  `from_slice::<rs::Response>` path: when `response.reasoning.effort` is a
  string the typed enum cannot parse (`"max"`), rewrite it to `"xhigh"` so
  the typed parse succeeds, and stash the raw string into
  `response.metadata["reasoning_effort_wire"]`
  (`apply_terminal_event_overrides` precedent).
- `response_to_conversation_items`: prefer
  `metadata["reasoning_effort_wire"]` (parsed with the canonical `FromStr`)
  over the typed echo when stamping effort provenance, so `Max` round-trips
  onto assistant items and session metadata.

**D. Model gating (pi parity)** — `xai-grok-shell/src/agent/config.rs`

- In `openai_codex_model_entries()`, add `(Max, "max", "Max", false)` to the
  effort menus of the **gpt-5.6 family only** (`gpt-5.6-luna/sol/terra`),
  mirroring pi's `supportsOpenAiMax`. `gpt-5.3-codex-spark`, `gpt-5.4[-mini]`
  and `gpt-5.5` stay capped at `xhigh`.
- `models.rs` legacy fallback set (`low..xhigh`) is **unchanged** — models
  without an explicit menu never offer `max`.

**E. Back-compat downgrade** — `xai-grok-pager/src/acp/model_state.rs`

- `resolve_effort_token_for`: when the token parses to `Max` but the model's
  menu has no `Max` value, fall back to an offered `Xhigh` option (silent
  downgrade). This preserves today's `/effort max` alias UX on xAI models
  (which would otherwise start erroring) and matches pi's
  `clampThinkingLevel`. All other unsupported levels keep the existing
  strict-reject behavior.
- This single point covers `/effort`, `/model … max`, `:max` suffixes, the
  deferred CLI switch, and headless — they all funnel through
  `resolve_effort_for_model`.
- The shell-side subagent effort override (`effort: max` in an agent
  definition, parsed in `subagent/handle_request.rs`) bypasses the pager
  resolver, so it applies the same downgrade against the model's catalog
  menu before stamping the sampling config.

**F. Tests**

- enum: `FromStr`/`as_str`/serde round-trip; `max` distinct from `xhigh`.
- request: body patch emits `reasoning.effort: "max"` (plain Responses and
  ChatGPT-Codex shapes); `Xhigh` still emits `"xhigh"`.
- response: echoed `"max"` parses via sanitize path; provenance stamps `Max`.
- catalog: gpt-5.6 entries offer `max`; gpt-5.5 does not.
- resolution: `max` on an xhigh-only menu downgrades to `Xhigh`; `max` on a
  gpt-5.6 menu resolves to `Max`; `openai-codex/gpt-5.6-sol:max` reference
  parses end-to-end.

The same patch updates the pager user guide (`02-authentication`,
`04-slash-commands`, `11-custom-models`, `14-headless-mode`) with model support
and downgrade behavior.

## Files affected (summary)

| Repo | File | Change |
|------|------|--------|
| grok-build | `crates/codegen/xai-grok-sampling-types/src/types.rs` | `Max` variant, parse/format, Messages mapping, placeholder Responses mapping, wrapper field |
| grok-build | `crates/codegen/xai-grok-sampling-types/src/conversation.rs` | `patch_reasoning_effort_max`, `sanitize_unknown_reasoning_effort`, provenance from `reasoning_effort_wire` |
| grok-build | `crates/codegen/xai-grok-sampler/src/client.rs` | request-body `"max"` patch (both Responses paths); response sanitize on the SSE fallback and non-streaming parse |
| grok-build | `crates/codegen/xai-grok-shell/src/agent/config.rs` | `max` menu option on gpt-5.6 Codex entries + gating test |
| grok-build | `crates/codegen/xai-grok-shell/src/agent/session_config.rs` | `Max` label (compiler-driven exhaustive match) |
| grok-build | `crates/codegen/xai-grok-shell/src/agent/subagent/handle_request.rs` | subagent `effort: max` downgrade for non-max models |
| grok-build | `crates/codegen/xai-grok-pager/src/acp/model_state.rs` | `Max → Xhigh` downgrade in effort-token resolution + tests |
| grok-build | `crates/codegen/xai-grok-pager/src/slash/commands/effort_levels.rs` | `Max` description (compiler-driven; legacy fallback menu unchanged) |
| grok-build | `crates/codegen/xai-grok-pager/docs/user-guide/{02,04,11,14}-*.md` | docs in patch `0005` |
| this repo | `patches/grok-build/0005-*.patch` | durable patch |
| this repo | `docs/i0002_add_max_thinking.md` | this doc |

## Non-goals

- Forking, vendoring, or `[patch.crates-io]`-overriding async-openai.
- Adding `max` to xAI built-in models or the legacy fallback effort set —
  the server-driven `reasoning_efforts` menu can introduce it later without
  code changes (a server menu entry `{"value": "max"}` will parse into the
  new variant automatically).
- Anthropic-side `xhigh`-vs-`max` differentiation is owned by i0003 patch
  `0008`, not this cross-provider canonical-level patch.
- Changing pi.

## Verification completed after the `ba76b0a` rebase

The original pre-consolidation stack was tested with an isolated `GROK_HOME`.
The active clean-room i0002 boundary is commit `a7b5601`, tree
`7c51dd6e240654ea1ab684ccf452a3e2536d0226`. Historical validation counts below
remain applicable because the consolidated final source tree is identical.

- `cargo check --workspace --locked`: passes.
- `cargo test -p xai-grok-sampling-types --lib --locked`: 277 passed (includes
  patch/sanitize/provenance and enum round-trip tests).
- `cargo test -p xai-grok-sampler --lib --locked`: 159 passed.
- `cargo test -p xai-grok-pager --lib --locked`: 7390 passed, 10 ignored
  (includes `resolve_effort_token_max_downgrades_to_xhigh_unless_offered`).
- `cargo test -p xai-grok-shell --lib --locked`: 5739 passed, 13 ignored.
- `git diff --check` clean.
- Active clean-room: patches `0001–0005` apply to `ba76b0a`; patch `0005`
  ends at `a7b5601`, tree `7c51dd6e240654ea1ab684ccf452a3e2536d0226`.
- Pre-consolidation seven-patch boundary and hashes are recorded in repository
  history; see [patch-history.md](patch-history.md) for number mapping.

### Remaining OpenAI `max` live check

This was not run while implementing i0002 and is not superseded by the later
Anthropic work:

- Live check (requires stored ChatGPT credential): fresh turn + resumed
  tool-using turn with `openai-codex/gpt-5.6-sol:max`; confirm the request
  body carries `reasoning.effort: "max"`, every loop completes on attempt 1,
  and no `inference_retry` appears in `~/.grok/logs/unified.jsonl`
  (per the i0001 regression protocol — a one-turn greeting is insufficient).
- Release rebuild (`cargo build -p xai-grok-pager-bin --release`) if a new
  binary is needed; the i0001 disk-space cautions apply.

## Decisions and deferred work

1. **Downgrade vs. warn:** resolved to a silent `max → xhigh` downgrade on
   models that don't offer it (exactly the old alias behavior, and pi's
   clamp semantics). Implemented at both funnels: pager
   `resolve_effort_token_for` (covers `/effort`, `/model …:max`, `-m`,
   headless) and the shell subagent effort-override parse in
   `handle_request.rs`. A TUI notice remains a possible follow-up.
2. **async-openai:** stayed on pinned 0.33; the typed `Max` placeholder plus
   `patch_reasoning_effort_max` / `sanitize_unknown_reasoning_effort` can be
   deleted wholesale if upstream ever grows a `Max` variant.
3. **Wire-side notes for maintainers:**
   - the request-side literal `"max"` is restored **after**
     `patch_reasoning_text_types` and **before**
     `apply_chatgpt_codex_request_shape` in both `create_response` and
     `create_response_stream`; the canonical effort travels on
     `CreateResponseWrapper.reasoning_effort` (process-local, like `trace`);
   - response provenance for `max` flows through
     `metadata["reasoning_effort_wire"]`, so effort stamping is correct even
     though the typed `rs::Response` says xhigh;
   - config-level gates already behave: `default_reasoning_effort = "max"`
     and CLI overrides pass through `model_offers_reasoning_effort`, which
     only admits `Max` for models whose menu lists it.
4. **Codex backend acceptance:** pi's generated catalog says gpt-5.6 accepts
   `"max"`; confirm against the live backend during the deferred live check
   (a 400 on `reasoning.effort` would show up immediately on the first
   `:max` turn).
5. **TOML behavior change (intentional):** `reasoning_effort = "max"` in
   `[model.*]` previously failed serde deserialization (the alias lived only
   in `FromStr`); it now parses to the real `Max` level and is subject to
   the same per-model gating.
