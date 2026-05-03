# Smoke Tests

## QA Channel

`qa-channel.js` is an OpenClaw-style synthetic transport harness. It does not
talk to a real IM platform. Instead, it injects inbound messages through
QuantClaw's channel HTTP API, reads session transcripts, and asserts the
transport contract:

- Same channel + different senders produce different session keys.
- Sender A history does not contain Sender B messages.
- Sender B history does not contain Sender A messages.
- Test sessions are deleted after the scenario.

Required environment:

```powershell
$cfg = Get-Content "$env:USERPROFILE\.quantclaw\quantclaw.json" -Raw | ConvertFrom-Json
$env:QUANTCLAW_AUTH_TOKEN = $cfg.gateway.auth.token
node scripts\smoke-tests\qa-channel.js
```

Optional:

```powershell
$env:QUANTCLAW_BASE_URL = "http://127.0.0.1:18801"
```

## Channel Session Isolation

`channel-session-isolation.js` verifies the QuantClaw HTTP channel route keeps
messages from different senders in the same channel in separate sessions.

Required environment:

```powershell
$env:QUANTCLAW_AUTH_TOKEN = "<gateway token>"
node scripts\smoke-tests\channel-session-isolation.js
```

Optional:

```powershell
$env:QUANTCLAW_BASE_URL = "http://127.0.0.1:18801"
```

## Feishu E2E

`feishu-e2e.js` drives real Feishu/Lark APIs. It is intentionally configured
only by environment variables so app secrets are not committed.

### Observe a Manual Feishu Group Probe

`feishu-live-verify.js` automates the manual validation flow after any real user
sends `@Quantbot /status <nonce>` in the qc group. It reads recent group
history, finds the user trigger and Quantbot reply, verifies the reply contains a
group-and-user scoped session key, then checks the local LLM history file did
not receive the slash command.

On the local development machine, it can read the existing Quantbot app
credentials from `~/.quantclaw/quantclaw.json`:

```powershell
$env:FEISHU_VERIFY_NONCE = "manual-1"
node scripts\smoke-tests\feishu-live-verify.js
```

Optional:

```powershell
$env:FEISHU_E2E_GROUP_CHAT_ID = "oc_xxx"
$env:FEISHU_VERIFY_TRIGGER_TEXT = "/status"
$env:FEISHU_VERIFY_LOOKBACK_SECONDS = "7200"
```

This mode is a real Feishu integration assertion, but it observes a message that
has already been sent by a human user. Fully automated sending requires a second
sender app/user token that is in the same Feishu group; Quantbot's own app
credential cannot be used to trigger itself because the adapter intentionally
ignores its own app messages.

Minimum group test:

```powershell
$env:FEISHU_E2E_APP_ID = "<sender app id>"
$env:FEISHU_E2E_APP_SECRET = "<sender app secret>"
$env:FEISHU_E2E_GROUP_CHAT_ID = "oc_xxx"
$env:FEISHU_E2E_BOT_NAME = "Quantbot"
node scripts\smoke-tests\feishu-e2e.js
```

The sender app must already be in `FEISHU_E2E_GROUP_CHAT_ID`; otherwise Feishu
returns `230002 Bot/User can NOT be out of the chat`.

If the target group requires an `@Quantbot` mention, also set the bot mention
ID:

```powershell
$env:FEISHU_E2E_BOT_MENTION_ID = "ou_or_app_user_id"
```

If the script can send and poll group history but times out waiting for a
reply, check these before debugging QuantClaw code:

- The Quantbot app is also in the same `FEISHU_E2E_GROUP_CHAT_ID`.
- The Quantbot app has message receive permissions enabled for that chat.
- The group requires an explicit at-mention and `FEISHU_E2E_BOT_MENTION_ID` was
  not set.
- The QuantClaw Feishu adapter process is running and connected to the gateway.

Strict anti-cross-talk test with two independent senders:

```powershell
$env:FEISHU_E2E_APP_ID = "<sender A app id>"
$env:FEISHU_E2E_APP_SECRET = "<sender A app secret>"
$env:FEISHU_E2E_APP_ID_B = "<sender B app id>"
$env:FEISHU_E2E_APP_SECRET_B = "<sender B app secret>"
$env:FEISHU_E2E_GROUP_CHAT_ID = "oc_xxx"
$env:FEISHU_E2E_BOT_NAME = "Quantbot"
$env:FEISHU_E2E_STRICT_ISOLATION = "1"
node scripts\smoke-tests\feishu-e2e.js
```

DM send test:

```powershell
$env:FEISHU_E2E_APP_ID = "<sender app id>"
$env:FEISHU_E2E_APP_SECRET = "<sender app secret>"
$env:FEISHU_E2E_DM_OPEN_IDS = "ou_xxx"
node scripts\smoke-tests\feishu-e2e.js
```

Feishu bot tokens can send p2p messages but cannot reliably read user-side p2p
history for a second app. Because of that, the script can automatically assert
group replies by polling group history, while DM mode is a real send/reachability
check that still needs manual reply verification unless a user-token receiver is
added.

Run offline tests for the script helpers:

```powershell
node scripts\smoke-tests\feishu-e2e.test.js
```
