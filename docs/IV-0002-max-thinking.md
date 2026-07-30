# IV-0002: Add `max` thinking level (above `xhigh`) to grok-build

**Status:** implemented and exported
**Upstreams:** `checkouts/pi` (reference semantics), `checkouts/grok-build` (patch target)
**Deliverable:** patch `0005` in `patches/grok-build/`
**Implementation base:** `500129c714ad1b10e6095481f4a8387a2ec52649` (Grok `0.2.114`)
**Depends on:** [IV-0001](IV-0001-openai-oauth.md) — OpenAI Codex OAuth provider, patches `0001–0004`
**Doctrine:** [DC-0001](DC-0001-agentic-workspace.md) — read before changing or retiring this initiative

## Lifecycle map

- **Why this exists:** preserve `max` as a real capability above `xhigh` where
  providers support it, without breaking the historical alias behavior on
  other models.
- **Durable implementation:** patch `0005`; upstream now owns the canonical
  `Max` enum and wire mapping, while the patch owns model gating, fallback
  behavior, subagent handling, and user docs.
- **Known consumers:** CLI and pager effort parsing, model menus, Responses and
  Messages conversion, subagent overrides, [IV-0001](IV-0001-openai-oauth.md),
  and [IV-0003](IV-0003-anthropic-oauth.md).
- **Key assumption:** upstream and its pinned async-openai revision continue to
  encode `Max` natively; provider catalogs still determine which models may
  offer it.
- **Evidence route:** rerun the focused Cargo suites and clean-room application
  under [Evidence and reproduction](#evidence-and-reproduction); the OpenAI
  live `:max` check remains explicitly outstanding.

## Patch ownership

- Patches `0001`–`0004` in `patches/grok-build/` are owned by **IV-0001**; this
  work stacks on top.
- Patch `0005` is owned by **IV-0002** and contains both code and user docs. If it
  stops applying cleanly, fix and re-export that patch as one coherent change.

## Anthropic interaction

Upstream Grok `0.2.114` already provides the canonical `Max` level and native
OpenAI Responses mapping. Patch `0005` now supplies the catalog and UX policy.
The Anthropic-specific per-model menu gating lives in IV-0003 patch `0008`.

## Intent and lifecycle justification

Make `max` a real reasoning-effort level **above** `xhigh`, sent verbatim on the
wire for models that support it (pi parity: the gpt-5.6 family), while keeping
the pre-initiative behavior — where `max` is merely a UX alias of `xhigh` — for every
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
  - Anthropic adaptive-thinking models also map `max`; active IV-0003 patch
    `0008` owns native per-model `xhigh` differentiation.
- `packages/ai/src/api/openai-codex-responses.ts` (and `openai-responses.ts`):
  the effort string is passed through `thinkingLevelMap` and lands on
  `reasoning.effort` verbatim — i.e. the wire value is literally `"max"`.

## grok-build integration points on `0.2.114`

- `xai-grok-sampling-types::ReasoningEffort` and the pinned async-openai
  Responses type both support `Max` natively. Requests and echoed responses no
  longer need a JSON rewrite or process-local effort side channel.
- `crates/codegen/xai-grok-shell/src/agent/config.rs` adds `max` only to the
  built-in Codex models whose catalog advertises it.
- `crates/codegen/xai-grok-pager/src/acp/model_state.rs` resolves menu IDs and
  preserves the compatibility downgrade from unsupported `max` to offered
  `xhigh`.
- `crates/codegen/xai-grok-shell/src/agent/subagent/handle_request.rs` applies
  the same downgrade to subagent effort overrides, which bypass pager-side
  resolution.
- Pager CLI/model parsing from patch `0003` consumes the upstream canonical
  enum directly.

## Implemented patch series

### Patch 0005 — `max` reasoning effort and user documentation

**A–C. Canonical and wire behavior — upstream-owned on `0.2.114`**

Upstream now carries `ReasoningEffort::Max` through parsing, async-openai,
request serialization, response decoding, and provenance. The rebased patch
therefore removes the former placeholder/rewrite workaround and makes no
sampling-types or sampler changes for `max`.

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
  downgrade). This preserves the pre-initiative `/effort max` alias UX on xAI models
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

- upstream compatibility: canonical enum and native request/response mapping
  remain covered by the sampling-types and sampler suites;
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
| grok-build | `crates/codegen/xai-grok-shell/src/agent/config.rs` | `max` menu option on gpt-5.6 Codex entries + gating test |
| grok-build | `crates/codegen/xai-grok-shell/src/agent/subagent/handle_request.rs` | subagent `effort: max` downgrade for non-max models |
| grok-build | `crates/codegen/xai-grok-pager/src/acp/model_state.rs` | `Max → Xhigh` downgrade in effort-token resolution + tests |
| grok-build | `crates/codegen/xai-grok-pager/docs/user-guide/{02,04,11,14}-*.md` | docs in patch `0005` |
| this repo | `patches/grok-build/0005-*.patch` | durable patch |
| this repo | `docs/IV-0002-max-thinking.md` | this doc |

## Non-goals

- Forking, vendoring, or `[patch.crates-io]`-overriding async-openai.
- Adding `max` to xAI built-in models or the legacy fallback effort set —
  the server-driven `reasoning_efforts` menu can introduce it later without
  code changes (a server menu entry `{"value": "max"}` will parse into the
  new variant automatically).
- Anthropic-side `xhigh`-vs-`max` differentiation is owned by IV-0003 patch
  `0008`, not this cross-provider canonical-level patch.
- Changing pi.

## Evidence and reproduction

Tests that read stored credentials use an isolated `GROK_HOME`.

- `cargo check -p xai-grok-pager-bin --locked`: passes.
- `cargo test -p xai-grok-sampling-types --lib --locked`: 299 passed.
- `cargo test -p xai-grok-sampler --lib --locked`: 174 passed.
- Previous-base focused pager reasoning-effort tests passed 9/9; focused OpenAI
  catalog/OAuth shell tests passed 6/6.
- `git diff --check` and `cargo fmt --all -- --check` are clean.
- Clean-room patches `0001–0005` apply to `500129c`.

### Remaining OpenAI `max` live check

This was not run while implementing IV-0002 and is not superseded by the later
Anthropic work:

- Live check (requires stored ChatGPT credential): fresh turn + resumed
  tool-using turn with `openai-codex/gpt-5.6-sol:max`; confirm the request
  body carries `reasoning.effort: "max"`, every loop completes on attempt 1,
  and no `inference_retry` appears in `~/.grok/logs/unified.jsonl`
  (per the IV-0001 regression protocol — a one-turn greeting is insufficient).
- Release rebuild (`cargo build -p xai-grok-pager-bin --release`) if a new
  binary is needed; the IV-0001 disk-space cautions apply.

## Decisions and deferred work

1. **Downgrade vs. warn:** resolved to a silent `max → xhigh` downgrade on
   models that don't offer it (exactly the old alias behavior, and pi's
   clamp semantics). Implemented at both funnels: pager
   `resolve_effort_token_for` (covers `/effort`, `/model …:max`, `-m`,
   headless) and the shell subagent effort-override parse in
   `handle_request.rs`. A TUI notice remains a possible follow-up.
2. **Canonical/wire ownership:** upstream `0.2.114` carries `Max`
   natively. Do not reintroduce the former typed-placeholder, JSON rewrite, or
   response-metadata side channel unless upstream regresses.
3. **Config-level gate:** `default_reasoning_effort = "max"` and CLI overrides
   pass through `model_offers_reasoning_effort`, which only admits `Max` for
   models whose menu lists it.
4. **Codex backend acceptance:** pi's generated catalog says gpt-5.6 accepts
   `"max"`; confirm against the live backend during the deferred live check
   (a 400 on `reasoning.effort` would show up immediately on the first
   `:max` turn).
5. **TOML behavior change (intentional):** `reasoning_effort = "max"` in
   `[model.*]` previously failed serde deserialization (the alias lived only
   in `FromStr`); it now parses to the real `Max` level and is subject to
   the same per-model gating.
