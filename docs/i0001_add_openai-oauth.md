# i0001: Add OpenAI (ChatGPT plan) OAuth provider to grok-build

**Status:** implemented and exported
**Upstreams:** `checkouts/pi` (reference implementation), `checkouts/grok-build` (patch target)
**Deliverable:** six-patch series in `patches/grok-build/`
**Implementation branch:** `checkouts/grok-build`, branch `openai-oauth`, base `98c3b24`

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

## Implementation outcome

Implemented as six commits/patches:

1. OAuth flow, credential storage/refresh, and `grok openai login|logout|status`.
2. `AuthScheme::ChatgptOauth`, live bearer/header wiring, and Codex Responses
   request reshaping.
3. Credential-gated built-in `openai-codex/*` model catalog.
4. pi-style `/model provider/model:effort` and `-m provider/model:effort` parsing.
5. Upstream user-guide documentation.
6. Streamed-text terminal fallback: preserve Codex text deltas when the
   terminal response omits assembled output, preventing false
   `no_visible_content` retries.

Normal users make **no config changes**: run `grok openai login` once, then
switch in-app with `/model openai-codex/gpt-5.5:xhigh` or Ctrl+M.
`configs/openai-codex.toml` is only an advanced custom-entry example.

The implementation uses `auth_scheme = "chatgpt_oauth"` rather than adding a
separate `[model.*] auth` field or `responses_flavor` field. This keeps the
provider marker in the existing sampler auth seam. grok-build already forced
`store:false` and included encrypted reasoning content, so only the remaining
Codex body differences are applied in `client.rs`.

### Implemented patch series

### Patch 0001 — OAuth flow + storage + CLI

- New module `xai-grok-shell/src/auth/openai_codex.rs`: Rust port of pi's
  `auth/oauth/openai-codex.ts`
  (constants identical: client id, URLs, scope, port 1455, simplified-flow
  params). Browser flow with local callback server + manual-paste fallback;
  device-code flow for headless.
- Storage: `~/.grok/openai_auth.json` (mode 0600) with
  `{access_token, refresh_token, expires_at, account_id}`.
  Optional (open question): read-compatibility with `~/.codex/auth.json`.
- Refresh: single-flight refresh when `expires_at - now < 300s`, persisted back.
- CLI: `grok openai login [--device-code]`, `grok openai logout`,
  `grok openai status` wired in `xai-grok-pager/src/app/cli.rs`.

### Patch 0002 — credential wiring + Codex Responses request shape

- Add `AuthScheme::ChatgptOauth`, exposed to custom `[model.*]` entries as
  `auth_scheme = "chatgpt_oauth"`.
- The shell installs a `BearerResolver` backed by the stored/refreshing OpenAI
  credential and injects `chatgpt-account-id`, `originator`, and
  `OpenAI-Beta: responses=experimental`.
- ChatGPT OAuth models never fall back to the xAI session token or `XAI_API_KEY`;
  a missing credential logs a clear `grok openai login` warning.
- Gate Codex request reshaping directly on `AuthScheme::ChatgptOauth`:
  - keep grok-build's existing `store:false`, streaming, and encrypted-reasoning
    behavior;
  - move system input items to `instructions`;
  - set `reasoning.summary = "auto"`;
  - strip `max_output_tokens`, `temperature`, and `top_p`, which Codex rejects.

### Patch 0003 — built-in credential-gated model catalog

- Add pi's explicit Codex catalog (`gpt-5.3-codex-spark`, `gpt-5.4[-mini]`,
  `gpt-5.5`, `gpt-5.6-luna/sol/terra`) with matching context windows.
- Register keys as `openai-codex/<id>`, route to
  `https://chatgpt.com/backend-api/codex`, and expose low through xhigh effort.
- Only add the catalog when `~/.grok/openai_auth.json` exists; insert entries
  before user overrides so advanced `[model.*]` customization still works.

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
- Implemented in `xai-grok-pager` (`slash/commands/model.rs`, `app/cli.rs`
  for `-m`) plus catalog registration in `xai-grok-shell/src/agent/config.rs`.

### Patch 0005 — user documentation

- Document `grok openai login|status|logout`, browser/device-code flows,
  credential isolation, zero-config catalog behavior, and
  `/model openai-codex/gpt-5.5:xhigh`.
- Clarify API-key OpenAI Responses support versus ChatGPT OAuth.
- In this repo, add `configs/openai-codex.toml` as an advanced example only;
  normal usage requires no config edit.

### Patch 0006 — streamed terminal-output fallback

Session `019f75c4-c801-78c2-ae47-9aaf05ff5952` exposed a compatibility gap:
Codex streamed visible `response.output_text.delta` frames, but its terminal
response omitted the assembled `output` message. Grok rendered the answer,
then classified the final response as `no_visible_content` and retried it.

The Responses stream transformer now accumulates text deltas and synthesizes
(or fills) the final Assistant item only when typed terminal output has no
visible text. It also handles providers that send only
`response.output_text.done`, without duplicating normal delta streams.

## Files affected (summary)

| Repo | File | Change |
|------|------|--------|
| grok-build | `crates/codegen/xai-grok-shell/src/auth/openai_codex.rs` | **new** — OAuth flow, device code, storage, refresh |
| grok-build | `crates/codegen/xai-grok-shell/src/auth/mod.rs` | register module |
| grok-build | `crates/codegen/xai-grok-pager/src/app/cli.rs` + pager-bin `main.rs` | OpenAI CLI + `-m model:effort` parsing |
| grok-build | `crates/codegen/xai-grok-shell/src/agent/config.rs` | catalog, credential resolution, bearer/header wiring |
| grok-build | `crates/codegen/xai-grok-shell/src/agent/config_model_override_parse.rs` | `auth_scheme` override drift guard |
| grok-build | `crates/codegen/xai-grok-shell/src/session/acp_session_impl/sampler_turn.rs` | per-turn OpenAI bearer resolver |
| grok-build | `crates/codegen/xai-grok-sampler/src/config.rs` | `ChatgptOauth` scheme |
| grok-build | `crates/codegen/xai-grok-sampler/src/client.rs` | bearer handling + Codex request shape |
| grok-build | `crates/codegen/xai-grok-pager/src/slash/commands/model.rs` | `provider/model:level` parsing |
| grok-build | `crates/codegen/xai-grok-pager/docs/user-guide/{02,04,11}-*.md` | docs |
| this repo | `patches/grok-build/0001..0006-*.patch` | durable patch series |
| this repo | `configs/openai-codex.toml`, `docs/` | sample config, this doc |

## Non-goals (v1)

- WebSocket transport / continuation, zstd request compression (SSE only).
- Bundling OpenAI API-key auth (already works today via `env_key = "OPENAI_API_KEY"`
  + `api_backend = "responses"` against `api.openai.com`).
- Upstreaming to xai-org (patches stay local per `docs/repo.md`).

## Verification completed

- `cargo check --workspace` passes.
- OAuth/JWT/PKCE/expiry and catalog tests: 6 passed.
- ChatGPT Codex request-shape tests: 2 passed.
- pi-style model-reference tests: 3 passed.
- `cargo run -p xai-grok-pager-bin -- openai --help` exposes
  `login|logout|status` as expected.
- `git diff 98c3b24..HEAD --check` passes.
- All six exported patches apply cleanly with `git am` to a fresh detached
  worktree at `98c3b24`.
- Live release-binary inference with `openai-codex/gpt-5.6-sol:high` returned
  one answer and exited successfully without retries after patch 0006.

Browser/device login itself was not repeated during the automated run; the
existing stored ChatGPT credential was used for live inference.

## Decisions and deferred work

1. **`originator`:** resolved to `"codex_cli_rs"` for backend compatibility.
2. **Credential file:** resolved to private `~/.grok/openai_auth.json`; no
   `~/.codex/auth.json` sharing/import in v1 to avoid refresh-token races.
3. **Model list:** resolved to a credential-gated built-in catalog, so model
   switching is in-app only (`/model`, Ctrl+M) with zero config edits.
4. **Login scope:** browser and device-code flows both implemented.
5. **Deferred:** in-TUI `/openai-login`, WebSocket continuation, zstd request
   compression, friendly quota-reset mapping, and forced refresh/retry on an
   unexpected pre-expiry 401.
