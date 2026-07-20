# i0001: Add OpenAI (ChatGPT plan) OAuth provider to grok-build

**Status:** implemented and exported
**Upstreams:** `checkouts/pi` (reference implementation), `checkouts/grok-build` (patch target)
**Deliverable:** consolidated patches `0001–0004` in `patches/grok-build/`
**Implementation branch:** clean-room series based on `ba76b0a` (Grok `0.2.106`); i0001 boundary `72ac660`, tree `98833e84644b5f092a29f1f610124c3d9157ce91`

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
  (port is fixed — it is registered with the client id). While that listener
  is running, users whose browser is on another machine can manually paste the
  final redirect URL. If the listener itself cannot bind, use the device-code
  flow instead.
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
  decode; needs Codex request/stream compatibility deltas (`store:false`,
  `instructions`, `include`, terminal-output assembly). Friendly quota/reset
  error mapping remains deferred rather than part of patch `0002`.
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

Implemented as four cohesive patches:

1. OAuth flow, credential storage/refresh, and `grok openai login|logout|status`.
2. Complete Codex transport contract: `AuthScheme::ChatgptOauth`, live
   bearer/header wiring, request shaping, multi-turn history normalization,
   streamed text/reasoning/tool-call assembly, and deployed SSE compatibility.
3. Generic `/model provider/model:effort` and `-m provider/model:effort` parsing.
4. Credential-gated built-in `openai-codex/*` catalog and user documentation.

The series is organized by subsystem. All Codex response compatibility
behavior is introduced in patch `0002`, so every later patch can rely on a
complete transport rather than repairing it.

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
  `{access_token, refresh_token, expires_at, account_id}`. The credential is
  intentionally not shared with or imported from `~/.codex/auth.json`, avoiding
  refresh-token races (see Decisions below).
- Refresh: single-flight refresh when `expires_at - now < 300s`, persisted back.
- CLI: `grok openai login [--device-code]`, `grok openai logout`,
  `grok openai status` wired in `xai-grok-pager/src/app/cli.rs`.

### Patch 0002 — complete Codex Responses transport

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
- Normalize prior assistant history as completed Responses output messages with
  `output_text`, `status`, and stable synthetic ids, matching pi's wire shape.
- Do not replay display-only reasoning summaries that lack an authentic
  upstream reasoning id/signature.
- Assemble streamed output defensively because Codex can omit assembled items
  from the terminal response:
  - preserve `response.output_text.delta` and done-only visible text;
  - accumulate reasoning-summary deltas for display;
  - accept complete function calls from `response.output_item.done`;
  - synthesize streamed text and function calls into the final typed response
    so normal completion classification and multi-loop tool execution work.

### Patch 0003 — pi-style model references (`provider/model:level`)

Adopt pi's model-reference syntax so switching is a single in-app token:

```
/model openai-codex/gpt-5.5:xhigh
grok -m openai-codex/gpt-5.5:xhigh -p "hello"
```

- **Provider prefix:** provider-qualified keys select the credential source,
  request flavor, and base URL; unprefixed names retain existing unambiguous
  matching behavior.
- **`:level` suffix:** try the full model id first, then split on the last colon
  only when the suffix is a canonical effort token. Model ids containing
  colons and the existing two-argument `/model <name> <effort>` form remain
  supported.
- Implemented generically in the pager CLI and `/model`, so later providers and
  reasoning levels reuse the same syntax.

### Patch 0004 — built-in Codex catalog and user documentation

- Add pi's explicit Codex catalog (`gpt-5.3-codex-spark`, `gpt-5.4[-mini]`,
  `gpt-5.5`, `gpt-5.6-luna/sol/terra`) with matching context windows.
- Register keys as `openai-codex/<id>`, route to
  `https://chatgpt.com/backend-api/codex`, and expose low through xhigh effort.
- Only add the catalog when `~/.grok/openai_auth.json` exists; insert entries
  before user overrides so advanced `[model.*]` customization still works.
- Document login/status/logout, browser/device-code flows, credential
  isolation, zero-config catalog behavior, API-key Responses separation, and
  `provider/model:effort` selection.
- `configs/openai-codex.toml` remains an advanced example; normal usage needs
  no config edit.

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
| this repo | `patches/grok-build/0001..0004-*.patch` | durable patch series |
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
- `git diff ba76b0a..HEAD --check` passes.
- All four active i0001 patches apply cleanly with `git am` to a fresh detached
  worktree at `ba76b0a`.
- Live release-binary inference with `openai-codex/gpt-5.6-sol:high` returned
  one answer and exited successfully without retries.
- Live two-turn resume session `c8fb606f-6b4d-4d8e-8e9e-48080c2bd3d0`
  completed both turns, including three second-turn model loops with local tool
  calls; every loop completed on attempt 1 with zero inference retries.

Browser/device login itself was not repeated during the automated run; the
existing stored ChatGPT credential was used for live inference.

## Handoff: requirements, gotchas, and maintenance tips

### Requirements to preserve

- Keep upstream clones disposable and ignored under `checkouts/`; active
  patches `0001–0004` in `patches/grok-build/` are the durable source of truth.
  Do not add a fork or submodule.
- Normal use must remain zero-config: login once, then switch in-app with
  `/model openai-codex/gpt-5.5:xhigh` (or Ctrl+M). The TOML example is for
  advanced overrides only.
- Preserve `provider/model:effort` parsing by trying the full model id first and
  splitting only the last colon; real model ids may contain colons.
- Keep OpenAI credentials isolated from xAI and official Codex credentials.
- Disk space is constrained: when building the **binary**, build it in
  release mode only (`cargo build -p xai-grok-pager-bin --release`) — don't
  produce a debug binary on top. Building/running unit tests, `cargo check`,
  etc. is fine in either profile; the point is to avoid duplicating the big
  final artifacts across `target/debug` and `target/release`.

### Codex protocol gotchas

These are part of patch `0002`'s baseline contract, not optional fallbacks:

- Codex can stream visible text while leaving terminal `response.output` empty;
  streamed text must be synthesized into the typed final response or Grok will
  render an answer and then retry it as empty.
- Complete function calls can arrive only in `response.output_item.done`; emit
  live tool deltas **and** retain the completed call in the final response so
  local tool loops continue.
- Prior assistant text must be replayed as completed output messages with
  `output_text`, `status`, and a stable id—not as easy-input assistant text.
- Streamed reasoning summaries are display-only unless they carry an authentic
  upstream id/signature. Never replay a synthesized empty reasoning id; Codex
  rejects it with HTTP 400.
- Gate all of the above on `AuthScheme::ChatgptOauth` so ordinary Responses
  providers retain existing behavior.

A one-turn greeting does not exercise these paths. Regression checks must use a
fresh first turn followed by a resumed prompt that requires local tools, then
confirm every loop completed on attempt 1 and no `inference_retry` appears in
`~/.grok/logs/unified.jsonl`.

### Maintenance state and historical snapshot

- **Active i0001 slice:** patches `0001–0004` end at clean-room commit
  `72ac660`, tree `98833e84644b5f092a29f1f610124c3d9157ce91`, based on
  `ba76b0a683fa52e4e60685017b85905451be17bc`.
- **Pre-consolidation historical slice:** old patches `0001–0005` ended at
  `a17584579eee3824ee5a4b2135a3d8d62128ee3f`, tree
  `5ca9de32f88ec8e5055a2cb92be7f4aad9997df7`. See
  [patch-history.md](patch-history.md) for the old-to-new map.
- Patch `0002` owns the complete Codex transport, including exact
  `response.metadata` compatibility. Patch `0004` owns the catalog and docs.
- Existing release binary:
  `checkouts/grok-build/target/release/xai-grok-pager`. For a literal local
  rebuild, run `cargo build -p xai-grok-pager-bin --release` (release mode so
  the debug tree isn't duplicated for the bin; test/check builds in any
  profile are fine). `cargo fmt --all` may touch unrelated pager files, so
  inspect/revert unrelated formatting before export. Remove
  `target/release/incremental` afterward while preserving the binary.
- To reproduce only i0001, clean-room `git am` patches `0001–0004` onto the
  pinned base and compare with the active slice tree above. To reproduce
  the current product, apply the complete series; CI does this automatically.

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
