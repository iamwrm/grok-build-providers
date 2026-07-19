# i0003: Add Anthropic (Claude Pro/Max plan) OAuth provider to grok-build

**Status:** implemented, exported, and live-verified (2026-07-19)
**Upstreams:** `checkouts/pi` (reference implementation), `checkouts/grok-build` (patch target)
**Deliverable:** five-patch series `patches/grok-build/0008..0012`, continuing the i0001/i0002 stack
**Implementation branch:** `checkouts/grok-build`, branch `openai-oauth`, base `98c3b24` (stack tip after i0002: `cd19ebd`)

## Goal

Let grok-build use Anthropic Claude models (`claude-opus-4-x`, `claude-sonnet-*`,
`claude-fable-5`) through a Claude Pro/Max subscription, by reimplementing pi's
`anthropic` OAuth flow and consumer-OAuth Messages API handling in Rust inside
grok-build — including the system-prompt classifier workaround mechanism from
`../piagent-config/packages/ren-private-package/`.

## How pi does it (reference)

Three pieces in `checkouts/pi/packages/ai/src/`:

### 1. OAuth flow — `auth/oauth/anthropic.ts`

- Public client id `9d1c250a-e61b-44d9-88ed-5944d1962f5e` (base64-obfuscated in
  pi's source), authorize `https://claude.ai/oauth/authorize`, token
  `https://platform.claude.com/v1/oauth/token`, PKCE S256.
- **The PKCE verifier doubles as the OAuth `state`.** Authorize query includes
  `code=true` plus the standard PKCE params; scope
  `org:create_api_key user:profile user:inference user:sessions:claude_code
  user:mcp_servers user:file_upload`.
- **Browser flow only** (no device-code endpoint exists): local callback at
  `http://localhost:53692/callback` (fixed port), with a manual paste fallback.
  claude.ai shows the manual credential as a `code#state` pair.
- Token exchange is a **JSON** POST (not form-encoded like OpenAI) and
  additionally requires the `state` field. Refresh via
  `grant_type=refresh_token` (JSON as well).
- **Credential:** `{access, refresh, expires}`; the `sk-ant-oat...` access token
  is used directly as the bearer.

### 2. Provider — `providers/anthropic.ts` + `anthropic.models.ts`

`baseUrl = https://api.anthropic.com`, catalog includes the adaptive-thinking
generation (all 1M context): Opus 4.6/4.7/4.8 (128k output), Sonnet 4.6 (64k),
Sonnet 5 (128k), Fable 5 (128k). Opus 4.7/4.8 reject `temperature` outright
(`compat.supportsTemperature: false`).

### 3. API shape — `api/anthropic-messages.ts` (OAuth branch)

Detected by `sk-ant-oat` in the key. Deviations the consumer-OAuth backend
**requires**:

- Bearer auth (`Authorization`, not `x-api-key`).
- Headers: `anthropic-beta: claude-code-20250219,oauth-2025-04-20`,
  `user-agent: claude-cli/2.1.75`, `x-app: cli`.
- **System prompt:** first system block MUST be exactly
  `"You are Claude Code, Anthropic's official CLI for Claude."`; the real
  system prompt follows as a second block.
- Temperature omitted when extended thinking is enabled.
- pi additionally renames tools to Claude Code's canonical names
  (`toClaudeCodeName` stealth mode) — **non-goal for v1 here** (see below).

### 4. The classifier workaround — `../piagent-config/packages/ren-private-package/`

Anthropic runs a server-side classifier over consumer-OAuth requests that
rejects system prompts of known third-party coding agents with HTTP 400
("Third-party apps now draw from your extra usage, not your plan limits...").
It keys on specific text fragments, not length. piagent-config's extension
rewrites the minimal trigger fragments (found by binary-search isolation) only
for the anthropic provider. This initiative ports the **mechanism** — a
documented replacement table applied to system text on OAuth requests — with
grok-build-specific triggers to be discovered empirically (the table ships
empty; grok's prompt may not trigger at all).

## grok-build integration points

grok-build already had a complete Anthropic Messages transport:

- `ApiBackend::Messages` → `{base_url}/messages`, typed
  `MessagesRequest`/SSE decode (`xai-grok-sampling-types/src/messages.rs`,
  `xai-grok-sampler/src/stream/messages.rs`), full conversation conversion
  incl. thinking blocks and adaptive `output_config.effort`
  (`conversation.rs::build_messages_request`).
- `ReasoningEffort::to_messages_api` originally mapped `low|medium|high`
  verbatim and collapsed `xhigh|max → "max"`; patch 0012 changes it to the
  pi-parity native mapping (`xhigh → "xhigh"`, `max → "max"`) with per-model
  menu gating.
- The i0001 seams (`AuthScheme` provider marker, `BearerResolver`, header
  injection in `sampling_config_for_model` and `sampler_turn.rs`,
  credential-gated catalog insertion in `resolve_model_list`, generic
  `provider/model:effort` parsing) all extend directly.

## Implemented patch series

### Patch 0008 — OAuth flow + storage + CLI

- New module `xai-grok-shell/src/auth/anthropic.rs`: Rust port of pi's
  `auth/oauth/anthropic.ts` (constants identical: client id, URLs, scopes,
  port 53692, verifier-as-state). Browser flow with local callback server +
  stdin paste fallback (redirect URL, `code#state`, or bare code). No
  device-code flow — upstream doesn't offer one.
- Storage: `~/.grok/anthropic_auth.json` (mode 0600) with
  `{access_token, refresh_token, expires_at}`; single-flight refresh when
  `expires_at - now < 300s`, persisted back. No `~/.claude` sharing/import.
- CLI: `grok anthropic login|logout|status` wired in
  `xai-grok-pager/src/app/cli.rs` + pager-bin `main.rs`.

### Patch 0009 — Anthropic OAuth Messages transport

- `AuthScheme::AnthropicOauth` (config string `anthropic_oauth`); wire format
  identical to `Bearer` in all sampler auth arms.
- The shell installs `AnthropicBearerResolver` (live refresh) and injects the
  headers via `inject_anthropic_oauth_headers`: `anthropic-version:
  2023-06-01`, `anthropic-beta: claude-code-20250219,oauth-2025-04-20`,
  `user-agent: claude-cli/2.1.75`, `x-app: cli`. Explicit user headers win.
- The sampler skips its own User-Agent when `extra_headers` provides one
  (small general fix: explicit extra headers now win for UA).
- Anthropic-OAuth models never fall back to the xAI session token or
  `XAI_API_KEY`; a missing credential logs a `grok anthropic login` warning.
  `byok_from_lookup` treats both OAuth schemes as always-BYOK.
- `apply_anthropic_oauth_request_shape` (typed, in `client.rs`, gated on the
  scheme, applied in `apply_message_defaults` for both streaming and
  non-streaming paths):
  - prepend the Claude Code identity system block (idempotent);
  - apply `ANTHROPIC_OAUTH_SYSTEM_REPLACEMENTS` (classifier workaround table,
    **currently empty**) to system text;
  - preserve existing cache markers; cache the trailing block when none exist;
  - strip `temperature`/`top_p` when extended thinking is enabled.

### Patch 0010 — built-in credential-gated model catalog

- `anthropic_model_entries()`: pi's adaptive-thinking Claude generation only —
  `claude-opus-4-8/-4-7/-4-6`, `claude-sonnet-5`, `claude-sonnet-4-6`,
  `claude-fable-5` — keyed `anthropic/<id>`, 1M context,
  per-model `max_completion_tokens` (64k Sonnet 4.6, 128k others), routed to
  `https://api.anthropic.com/v1` with `ApiBackend::Messages`.
- Older budget-thinking models (Opus 4.5, Sonnet 4.5, Haiku 4.5, …) are
  excluded: grok-build's Messages conversion drives thinking exclusively
  through the adaptive `output_config.effort` field, which they reject.
- Efforts `low|medium|high (default)|max`, plus native `xhigh` where
  supported (see patch 0012).
- Catalog only appears when `~/.grok/anthropic_auth.json` exists; inserted
  before `[model.*]` overrides (same semantics as the Codex catalog).

### Patch 0011 — user documentation

- `02-authentication.md`: Anthropic Claude Pro/Max section (login flow, paste
  fallback, credential isolation, model switching).
- `11-custom-models.md`: zero-config `anthropic/*` built-ins,
  `anthropic_oauth` in the `auth_scheme` enum, API-key-vs-OAuth Messages
  clarification, advanced custom-entry override.
- `04-slash-commands.md`: `anthropic/...` provider-prefix example; `max` note.

### Patch 0012 — native `xhigh` on the Messages wire

Added after review: the initial catalog omitted `xhigh` (and the docs
wrongly called it an accepted alias — the effort resolver actually rejected
it, since it only accepts menu tokens plus the legacy `max→xhigh`
downgrade). Per pi's `thinkingLevelMap`, native `xhigh` is a distinct level
between `high` and `max` on Opus 4.7/4.8, Sonnet 5, and Fable 5.

- `ReasoningEffort::to_messages_api`: `Xhigh → "xhigh"`, `Max → "max"`
  (was `Xhigh|Max → "max"`; updates the upstream mapping + i0002-era tests).
- Catalog: `xhigh` offered on exactly the native-xhigh models; Opus 4.6 and
  Sonnet 4.6 keep `low|medium|high|max`.
- Resolver (`model_state.rs::resolve_effort_token_for`): symmetric fallback
  — requested `xhigh` on a menu without it resolves to an offered `max`
  (mirrors pi's Opus-4.6 map and the existing `max→xhigh` legacy branch).
- Docs corrected; custom Messages entries for older Claude models should
  use `max` (native `xhigh` now passes through verbatim and would 400).
- **Known behavior change:** custom `[model.*]` Messages entries that set
  `xhigh` against models without native xhigh now send `"xhigh"` instead of
  `"max"`. Built-ins are unaffected; noted in `11-custom-models.md`.

## Files affected (summary)

| Repo | File | Change |
|------|------|--------|
| grok-build | `crates/codegen/xai-grok-shell/src/auth/anthropic.rs` | **new** — OAuth flow, storage, refresh, bearer resolver |
| grok-build | `crates/codegen/xai-grok-shell/src/auth/mod.rs` | register module |
| grok-build | `crates/codegen/xai-grok-pager/src/app/cli.rs` + pager-bin `main.rs` | `grok anthropic` CLI |
| grok-build | `crates/codegen/xai-grok-sampler/src/config.rs` | `AnthropicOauth` scheme |
| grok-build | `crates/codegen/xai-grok-sampler/src/client.rs` | bearer arms, UA respect, OAuth request shape + replacement table |
| grok-build | `crates/codegen/xai-grok-shell/src/agent/config.rs` | credential resolution, header injection, catalog |
| grok-build | `crates/codegen/xai-grok-shell/src/session/acp_session_impl/sampler_turn.rs` | per-turn bearer resolver + headers |
| grok-build | `crates/codegen/xai-grok-pager/docs/user-guide/{02,04,11}-*.md` | docs |
| grok-build | `crates/codegen/xai-grok-sampling-types/src/{types,conversation}.rs` | native `xhigh` Messages wire mapping (0012) |
| grok-build | `crates/codegen/xai-grok-pager/src/acp/model_state.rs` | `xhigh→max` resolver fallback (0012) |
| this repo | `patches/grok-build/0008..0012-*.patch` | durable patch series |
| this repo | `configs/anthropic-oauth.toml`, this doc | sample config, narrative |

## Non-goals (v1)

- **Claude Code stealth tool naming** (pi's `toClaudeCodeName`): grok-build
  keeps its own tool names. Add only if live testing shows the classifier or
  backend keys on tool names.
- Budget-based (`thinking.budget_tokens`) models — catalog is adaptive-only.
- Interleaved-thinking / fine-grained-tool-streaming beta headers (adaptive
  models don't need them).
- Anthropic API-key auth (already works today via `api_backend = "messages"` +
  `extra_headers x-api-key`).
- `~/.claude/credentials` import (refresh-token race risk).

## Verification

- `cargo check --workspace` passes.
- Unit tests: 3 OAuth-flow tests (paste parsing, authorize URL/PKCE/state,
  expiry window), 2 sampler request-shape tests (identity prepend + cache +
  sampling-param strip; identity-only + idempotency), header-injection test,
  catalog test — all pass; full sampler lib suite 159/159.
- `agent::config` shell tests 310/310 — **run with an isolated
  `GROK_HOME=$(mktemp -d)`**: three prefetch-count tests
  (`e2e_enterprise_custom_endpoint_skips_xai_defaults`,
  `resolve_model_list_prefetch_visibility_matches_auth_and_server_list`,
  `resolve_model_list_empty_prefetch_yields_empty_base`) count catalog
  entries and fail pre-existing on any machine with a stored
  `~/.grok/openai_auth.json` (or now `anthropic_auth.json`), independent of
  this series.
- `git diff cd19ebd..HEAD --check` clean.
- Clean-room: all **twelve** patches `git am` onto a detached worktree at
  `98c3b24`; resulting tree hash `acd130e5f9027dccb0bfa9fe73e3507c39983798`
  matches the branch tip (`6ff0a31`) exactly.
- Patch 0012 additions: sampling-types suite 273/273 (native wire mapping),
  pager `model_state` 17/17 (incl. the symmetric `xhigh→max` fallback),
  catalog test asserts per-model native-xhigh gating.
- **Live testing (2026-07-19):** `grok anthropic login` browser flow
  completed and stored `~/.grok/anthropic_auth.json`; user-confirmed working
  live inference on the `anthropic/*` catalog (including native `xhigh`
  selection after patch 0012). No `inference_retry` entries in
  `~/.grok/logs/unified.jsonl` after the credential timestamp, and —
  notably — **no classifier rejection with the replacement table empty**:
  grok-build's system prompt + the Claude Code identity block currently
  pass the consumer-OAuth classifier as-is. Keep the recipe below for when
  Anthropic re-tightens the classifier.

### Live regression recipe

A one-turn greeting does not exercise the risky paths. After login:

1. Fresh first turn (`grok -m anthropic/claude-opus-4-8:high -p ...`).
2. Resumed second turn that requires local tools (multi-loop).
3. Confirm every loop completed on attempt 1 and no `inference_retry` in
   `~/.grok/logs/unified.jsonl`.
4. If requests fail with the "Third-party apps now draw from your extra
   usage..." 400: the classifier fired. Isolate the grok system-prompt
   trigger fragments by binary search (mirror
   `../piagent-config/packages/ren-private-package/repro-matrix.sh`), add
   minimal rewrites to `ANTHROPIC_OAUTH_SYSTEM_REPLACEMENTS` in
   `xai-grok-sampler/src/client.rs`, and document each trigger + date there.
   Transient `overloaded_error` responses are capacity issues — retry, don't
   touch the table.

## Handoff: requirements, gotchas, and maintenance tips

- The identity block must stay byte-identical to
  `"You are Claude Code, Anthropic's official CLI for Claude."` and remain
  the **first** system block; pi confirms the backend rejects OAuth requests
  without it.
- Keep the OAuth token out of `x-api-key`: the consumer backend requires
  Bearer. `AuthScheme::AnthropicOauth` shares every `Bearer` match arm.
- Anthropic rejects `temperature` alongside extended thinking (and Opus
  4.7/4.8 reject it always); the shape function strips it whenever thinking
  is on. Catalog entries default to effort `high`, so thinking is always on
  for built-ins.
- The `user-agent` must be the Claude Code one; the sampler only honors it
  because `SamplingClient::new` now skips its default UA when `extra_headers`
  supplies one — don't regress that when rebasing.
- Refresh tokens rotate on every refresh; `refresh_credential` persists
  before returning, and `current_bearer_blocking` single-flights refreshes.
- Disk space: build the binary in **release only**
  (`cargo build -p xai-grok-pager-bin --release`), prefer
  `CARGO_INCREMENTAL=0`, and delete `target/*/incremental` when space runs
  low (a full debug-incremental tree exceeded the disk during this work).

## Decisions and deferred work

1. **Provider key prefix:** `anthropic/` (pi parity).
2. **Catalog scope:** adaptive-thinking generation only (see patch 0010).
3. **Effort menu:** `low|medium|high|max`, plus native `xhigh` on Opus
   4.7/4.8, Sonnet 5, and Fable 5 (patch 0012; supersedes the v1 decision to
   collapse `xhigh` into `max`). Off-menu `xhigh` resolves to an offered
   `max` and vice versa.
4. **Classifier table ships empty** pending a live repro; the mechanism and
   maintenance procedure are in place.
5. **Deferred:** stealth tool naming, in-TUI `/anthropic-login`, budget
   thinking for older Claude models, `~/.claude` credential import, friendly
   quota-reset error mapping.
