# i0001: Add OpenAI (ChatGPT plan) OAuth provider to grok-build

**Status:** proposed — awaiting approval
**Upstreams:** `checkouts/pi` (reference implementation), `checkouts/grok-build` (patch target)
**Deliverable:** patch series in `patches/grok-build/`

## Goal

Let grok-build use OpenAI Codex models (`gpt-5.x-codex` family) through a
ChatGPT Plus/Pro subscription, by reimplementing pi's `openai-codex` OAuth
flow and Codex Responses API handling in Rust inside grok-build.

## How pi does it (reference)

Three pieces in `checkouts/pi/packages/ai/src/`:

### 1. OAuth flow — `auth/oauth/openai-codex.ts`

- Public client id `app_EMoamEEZ73f0CkXaXp7hrann` against `https://auth.openai.com`
  (`/oauth/authorize`, `/oauth/token`), PKCE S256 + random `state`,
  scope `openid profile email offline_access`, extra query params
  `id_token_add_organizations=true`, `codex_cli_simplified_flow=true`, `originator`.
- **Browser flow:** local HTTP callback server on `http://localhost:1455/auth/callback`
  (port is fixed — it is registered with the client id), with a manual
  paste-the-redirect-URL fallback when the server can't bind.
- **Device-code flow (headless):** `POST /api/accounts/deviceauth/usercode` →
  show user code + `https://auth.openai.com/codex/device` → poll
  `POST /api/accounts/deviceauth/token` (pending on 403/404 or
  `deviceauth_authorization_pending`, backoff on `slow_down`) → returns an
  authorization code + code verifier, exchanged at `/oauth/token` with
  `redirect_uri = https://auth.openai.com/deviceauth/callback`.
- **Credential:** `{access, refresh, expires, accountId}` where `accountId` is the
  `chatgpt_account_id` field of the JWT claim `https://api.openai.com/auth`
  decoded from the access token. Refresh via `grant_type=refresh_token`.
- The access token is then used directly as the bearer ("apiKey") for requests.

### 2. Provider — `providers/openai-codex.ts`

`baseUrl = https://chatgpt.com/backend-api`, model catalog in
`providers/openai-codex.models.ts` (`gpt-5.3-codex-spark`, `gpt-5.4`,
`gpt-5.4-mini`, `gpt-5.5`, `gpt-5.6-luna/sol/terra`, …).

### 3. API shape — `api/openai-codex-responses.ts`

Endpoint `{base}/codex/responses`. Deviations from the plain OpenAI
Responses API that the ChatGPT backend **requires**:

- Headers: `Authorization: Bearer <access>`, `chatgpt-account-id: <accountId>`,
  `originator`, `OpenAI-Beta: responses=experimental`,
  `accept: text/event-stream`, plus `session-id` / `x-client-request-id`.
- Body: `store: false` (backend rejects `true`), `stream: true`,
  `instructions` (system prompt goes here, not as a system message),
  `include: ["reasoning.encrypted_content"]`, `prompt_cache_key`,
  `reasoning: {effort, summary}`, `text: {verbosity}`.
- Error mapping: 429 / `usage_limit_reached` / `insufficient_quota` → friendly
  "ChatGPT usage limit" message with reset time; those are terminal, not retried.
- Optional niceties pi has that we treat as **non-goals for v1**:
  zstd request compression, WebSocket transport with SSE fallback,
  per-session websocket continuation.

## grok-build integration points (found in exploration)

grok-build already has almost every seam we need:

- `crates/codegen/xai-grok-sampler/src/config.rs` — `SamplerConfig` with
  `api_backend: Responses`, `auth_scheme` (`Bearer`/`XApiKey`), `extra_headers`
  (verbatim), and crucially **`bearer_resolver: Option<SharedBearerResolver>`**
  (live per-request bearer lookup) — perfect hook for a refreshing OAuth token.
- `crates/codegen/xai-grok-sampler/src/client.rs` — builds `{base_url}/responses`;
  so `base_url = "https://chatgpt.com/backend-api/codex"` yields the right URL
  with no URL-code changes. Has a 401 retry/attribution path.
- `crates/codegen/xai-grok-sampler/src/stream/responses.rs` — Responses SSE
  decode; needs the codex request-shape deltas (`store:false`, `instructions`,
  `include`, error mapping).
- `crates/codegen/xai-grok-shell/src/agent/{config,models}.rs` +
  `crates/codegen/xai-chat-state/src/types.rs` — `[model.*]` config
  (`api_key`/`env_key`/`base_url`/`api_backend`/`extra_headers`) and its
  resolution into `SamplerConfig` (`resolve_model_to_sampling_config`).
- `crates/codegen/xai-grok-shell/src/auth/` — existing auth machinery
  (PKCE OIDC flow, device_code.rs, storage.rs for `~/.grok/auth.json`) we can
  mirror for structure, but the OpenAI credential is stored **separately**
  (it is a per-model provider credential, not the xAI session).
- `crates/codegen/xai-grok-pager/src/app/cli.rs` — CLI subcommands (`login`, …).

## Proposed approach

New first-class credential source `openai-oauth` for custom models, plus a
"chatgpt-codex" flavor of the Responses backend. Phased patch series:

### Patch 0001 — OAuth flow + storage + CLI

- New module `xai-grok-shell/src/auth/openai_codex/` (`flow.rs`, `device.rs`,
  `storage.rs`, `jwt.rs`): Rust port of pi's `auth/oauth/openai-codex.ts`
  (constants identical: client id, URLs, scope, port 1455, simplified-flow
  params). Browser flow with local callback server + manual-paste fallback;
  device-code flow for headless.
- Storage: `~/.grok/openai_auth.json` (mode 0600) with
  `{access_token, refresh_token, expires_at, account_id}`.
  Optional (open question): read-compatibility with `~/.codex/auth.json`.
- Refresh: single-flight refresh when `expires_at - now < 300s`, persisted back.
- CLI: `grok openai login [--device-code]`, `grok openai logout`,
  `grok openai status` wired in `xai-grok-pager/src/app/cli.rs`.

### Patch 0002 — model-config credential source + wiring

- Add optional `auth = "openai-oauth"` field to `[model.*]`
  (`xai-chat-state/src/types.rs`, shell config parse in
  `agent/config_model_override_parse.rs` / `agent/models.rs`).
- When set, `resolve_model_to_sampling_config` installs:
  - a `BearerResolver` impl backed by the stored/refreshing OpenAI credential,
  - `extra_headers`: `chatgpt-account-id`, `originator`, `OpenAI-Beta:
    responses=experimental` (account id known after login),
  - 401 hook → force refresh once, retry (reuses existing sampler 401 path).
- Fail with a clear "run `grok openai login`" error when no credential exists.

### Patch 0003 — Codex Responses request shape

- New knob on `SamplerConfig` (e.g. `responses_flavor: Standard | ChatgptCodex`,
  derived from the model entry) in `xai-grok-sampler`:
  - force `store: false`, `stream: true`;
  - move system prompt to `instructions`;
  - add `include: ["reasoning.encrypted_content"]`, `prompt_cache_key`,
    `reasoning.summary`;
  - map `usage_limit_reached` / quota errors to a friendly terminal error
    (no retry storm), following pi's `parseErrorResponse`/`isTerminalRateLimitError`.

### Patch 0004 — pi-style model references (`provider/model:level`)

Adopt pi's model-reference syntax so switching is a single in-app token:

```
/model openai-codex/gpt-5.5:xhigh
grok -m openai-codex/gpt-5.5:xhigh -p "hello"
```

- **Provider prefix:** built-in Codex catalog entries are registered under
  `openai-codex/<id>` (e.g. `openai-codex/gpt-5.5`); the prefix selects the
  provider (credential source + request flavor + base_url). Unprefixed names
  still resolve via the existing case-insensitive id/display-name matching
  when unambiguous (`/model gpt-5.5` works too); xAI built-ins keep their
  bare names for backward compatibility.
- **`:level` suffix:** port pi's `parseModelPattern` algorithm
  (`checkouts/pi/packages/coding-agent/src/core/model-resolver.ts`): try the
  full pattern as a model first, else split on the **last** colon, validate
  the suffix as a thinking level, recurse on the prefix — this keeps model
  ids containing colons working and warns on invalid levels.
- **Level mapping:** accept pi's levels `none|minimal|low|medium|high|xhigh`
  and map them onto `ReasoningEffort` / the codex `reasoning.effort` field
  (Codex models pass the OpenAI effort string through verbatim; for xAI
  models the existing effort names keep working). The existing
  `/model <name> <effort>` two-argument form stays as an alias.
- Implemented in `xai-grok-pager` (slash-command handler, `app/cli.rs` for
  `-m`, `acp/model_state.rs` for effort state) + catalog registration in
  `xai-grok-shell/src/agent/models.rs`.

### Patch 0005 — models, docs, sample config

- Built-in model entries for the Codex family (ids from pi's
  `openai-codex.models.ts`), `base_url = https://chatgpt.com/backend-api/codex`,
  `api_backend = "responses"`, `auth = "openai-oauth"`, correct context windows.
- **Zero-config switching:** because the entries are built-in, switching is
  purely in-app (`/model gpt-5.5`, Ctrl+M picker, `grok -m gpt-5.5`) — no
  `config.toml` edits ever required. The only terminal step is the one-time
  `grok openai login`.
- **Picker gating:** Codex entries are listed in `/model` / Ctrl+M only when a
  credential exists in `~/.grok/openai_auth.json`; selecting one without a
  login yields "run `grok openai login`" instead of a request error.
- **In-app login (stretch):** `/openai-login` slash command running the
  device-code flow inside the TUI (grok-build already renders device-code
  prompts for xAI login), so login also needs no terminal round-trip.
- Update `xai-grok-pager/docs/user-guide/11-custom-models.md` and
  `02-authentication.md`.
- In this repo: `configs/openai-codex.toml` sample snippet + README note.

## Files affected (summary)

| Repo | File | Change |
|------|------|--------|
| grok-build | `crates/codegen/xai-grok-shell/src/auth/openai_codex/*` | **new** — OAuth flow, device code, storage, refresh |
| grok-build | `crates/codegen/xai-grok-shell/src/auth/mod.rs` | register module |
| grok-build | `crates/codegen/xai-grok-pager/src/app/cli.rs` | `grok openai login/logout/status` |
| grok-build | `crates/codegen/xai-chat-state/src/types.rs` | `[model.*] auth` field |
| grok-build | `crates/codegen/xai-grok-shell/src/agent/models.rs` | catalog entries + field plumbing |
| grok-build | `crates/codegen/xai-grok-shell/src/agent/config.rs` | bearer resolver + headers + 401 refresh wiring |
| grok-build | `crates/codegen/xai-grok-sampler/src/config.rs` | `responses_flavor` knob |
| grok-build | `crates/codegen/xai-grok-sampler/src/client.rs` | codex error mapping |
| grok-build | `crates/codegen/xai-grok-sampler/src/stream/responses.rs` | codex request-shape deltas |
| grok-build | `crates/codegen/xai-grok-pager/src/{app/cli.rs, acp/model_state.rs}` + slash-command handler | `provider/model:level` parsing (pi's `parseModelPattern` port) |
| grok-build | `crates/codegen/xai-grok-pager/docs/user-guide/{02,04,11}-*.md` | docs |
| this repo | `patches/grok-build/0001..0005-*.patch` | durable patch series |
| this repo | `configs/openai-codex.toml`, `docs/` | sample config, this doc |

## Non-goals (v1)

- WebSocket transport / continuation, zstd request compression (SSE only).
- Bundling OpenAI API-key auth (already works today via `env_key = "OPENAI_API_KEY"`
  + `api_backend = "responses"` against `api.openai.com`).
- Upstreaming to xai-org (patches stay local per `docs/repo.md`).

## Verification plan

- Unit tests ported from pi's `test/openai-codex-oauth.test.ts` (JWT account-id
  extraction, token exchange/refresh parsing, device-code polling states) using
  grok-build's `xai-grok-test-support` mock server.
- Unit tests for reference parsing ported from pi's model-resolver tests:
  `openai-codex/gpt-5.5:xhigh`, `gpt-5.5:xhigh`, bare `gpt-5.5`, invalid
  level warning, ids containing colons.
- Manual: `cargo build`, `grok openai login` (browser + device-code),
  `grok -m openai-codex/gpt-5.5:xhigh -p "hello"`, `/model` switching
  mid-session, expiry-refresh path (tamper `expires_at`), 401-refresh path,
  usage-limit error message.
- Export patches via `git format-patch` into `patches/grok-build/` and verify
  they apply cleanly on a fresh checkout (per `docs/repo.md`).

## Open questions

1. **`originator` value:** pi sends `"pi"`; official Codex CLI sends
   `"codex_cli_rs"`. Proposal: `"codex_cli_rs"` for maximum backend
   compatibility (grok-build is not an approved originator).
2. **Credential file:** own `~/.grok/openai_auth.json` (proposed) vs. sharing
   `~/.codex/auth.json` with the official Codex CLI (interop, but risk of
   fighting over refresh-token rotation). Proposal: own file, plus optional
   one-time import from `~/.codex/auth.json` if present.
3. **Model list:** ~~hardcode vs. sample config~~ — **resolved: hardcode the
   catalog** so model switching is in-app only (`/model`, Ctrl+M) with zero
   config edits; keep the sample config only as documentation.
4. **v1 login scope:** browser + device-code both (proposed), or browser only.
