// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

export module quantclaw.builtin_skills;

import std;

export namespace quantclaw {

struct BuiltinSkill {
  const char* name;     // directory name (== skill name)
  const char* content;  // full SKILL.md content
};

// Returns the compile-time registry of built-in skills embedded from
// assets/skills/.  The vector is static and initialised once; callers receive
// a const reference that remains valid for the lifetime of the process.
//
// Each entry contains:
//   name    – the skill directory name (used as the skill identifier)
//   content – the full SKILL.md text written to ~/.quantclaw/skills/<name>/
//
// Onboarding copies these files to the user workspace, skipping any that
// already exist so user edits are preserved.
inline const std::vector<BuiltinSkill>& GetBuiltinSkills() {
  // Raw-string delimiter SKILL avoids conflicts with any character in the
  // markdown content (backticks, quotes, closing parens, arrows, etc.).
  static const std::vector<BuiltinSkill> kSkills = {
      {"search",
       R"SKILL(---
name: search
emoji: "🔍"
description: Web search with automatic provider fallback (Tavily → DuckDuckGo)
always: true
commands:
  - name: search
    description: Search the web for a query
    toolName: web_search
    argMode: freeform
---

Use the `web_search` tool to search the web. Results include titles, URLs, and snippets.

**Tool:** `web_search`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | ✓ | Search query |
| `count` | integer | — | Number of results (1–10, default 5) |
| `freshness` | string | — | Time filter: `day`, `week`, `month`, `year` |

**Provider cascade** (first available key wins):
1. **Tavily** (`TAVILY_API_KEY`) — recommended, high-quality results
2. **DuckDuckGo** — free, no API key required, always available as fallback

Set `TAVILY_API_KEY` in your environment or config for best results:
```json
{ "providers": { "tavily": { "apiKey": "tvly-..." } } }
```

**Examples:**
```
web_search({"query": "latest OpenAI news"})
web_search({"query": "Python asyncio tutorial", "count": 3})
web_search({"query": "market open price TSLA", "freshness": "day"})
```

**Slash command:** `/search <query>` triggers an immediate web search.
)SKILL"},
      {"weather",
       R"SKILL(---
name: weather
emoji: "🌦️"
description: Check current weather using wttr.in
always: true
---

You can check the weather for any location using the `system.run` tool.

**Usage:** Run `curl "wttr.in/{location}?format=3"` to get a compact weather summary.

For detailed forecast: `curl "wttr.in/{location}"`

Examples:
- `curl "wttr.in/Beijing?format=3"` → Beijing: ☀️ +25°C
- `curl "wttr.in/Tokyo?format=%C+%t+%w"` → Clear +22°C ↗10km/h
- `curl "wttr.in/London?lang=zh"` → Chinese output
)SKILL"},
      {"github",
       R"SKILL(---
name: github
emoji: "🐙"
description: Interact with GitHub via gh CLI
requires:
  bins:
    - gh
metadata:
  openclaw:
    install:
      apt: gh
---

You can interact with GitHub using the `gh` CLI tool via `system.run`.

**Common operations:**
- List repos: `gh repo list`
- View issues: `gh issue list -R owner/repo`
- Create issue: `gh issue create -R owner/repo --title "..." --body "..."`
- View PR: `gh pr view 123 -R owner/repo`
- Create PR: `gh pr create --title "..." --body "..."`
- Search code: `gh search code "query" --language python`
- View notifications: `gh api notifications`

**Authentication:** Ensure `gh auth login` has been run first.
)SKILL"},
      {"healthcheck",
       R"SKILL(---
name: healthcheck
emoji: "🏥"
description: System health audit and diagnostics
always: true
commands:
  - name: healthcheck
    description: Run system health check
    toolName: system.run
    argMode: none
---

You can perform system health checks and diagnostics using standard Linux tools.

**Checks to perform:**
1. **Disk usage:** `df -h` — check for filesystems over 90%
2. **Memory:** `free -h` — check available memory
3. **CPU load:** `uptime` — check load averages
4. **Network:** `ping -c 1 8.8.8.8` — check internet connectivity
5. **DNS:** `dig google.com +short` — check DNS resolution
6. **Services:** `systemctl --user list-units --state=running` — check running services
7. **Logs:** `journalctl --user -n 20 --no-pager` — recent log entries

**QuantClaw specific:**
- Gateway status: `quantclaw status`
- Config check: `quantclaw doctor`
- Health endpoint: `quantclaw health`
)SKILL"},
      {"skill-creator",
       R"SKILL(---
name: skill-creator
emoji: "🎨"
description: Guide for creating new QuantClaw skills
always: true
commands:
  - name: create-skill
    description: Create a new skill from template
    toolName: system.run
    argMode: freeform
---

You help create new skills for QuantClaw. A skill is a directory containing a `SKILL.md` file with YAML frontmatter and markdown instructions.

**Skill structure:**
```
~/.quantclaw/agents/main/workspace/skills/{skill-name}/
├── SKILL.md          # Required: frontmatter + instructions
├── scripts/          # Optional: helper scripts
├── references/       # Optional: reference documents
└── assets/           # Optional: images, templates, etc.
```

**SKILL.md frontmatter format:**
```yaml
---
name: my-skill
emoji: "🔧"
description: Short description of the skill
requires:
  bins:
    - required-binary
  env:
    - REQUIRED_ENV_VAR
  anyBins:
    - option-a
    - option-b
os:
  - linux
  - darwin
always: false
metadata:
  openclaw:
    install:
      apt: package-name
      node: npm-package
---
```

The markdown body after the frontmatter becomes the skill context injected into the LLM prompt.
)SKILL"},
      {"okx-trading",
       R"SKILL(---
name: okx-trading
emoji: "📈"
description: Trade on OKX via the Agent TradeKit MCP server (spot, swap, futures, options, grid/DCA bots)
requires:
  env:
    - OKX_ACCESS_TOKEN
metadata:
  mcp:
    server: okx-agent-trade-kit
    url: https://us.okx.com/api/v1/mcp/trading-oauth
    docs: https://app.okx.com/docs-v5/agent_en/
---

You can trade on OKX using the **OKX Agent TradeKit** MCP server. All tools are
exposed under the qualified prefix `mcp__okx-agent-trade-kit__<tool>`. Trading
permissions are enforced server-side from the user's API-key scopes — if a key
lacks `Trade`, the order tools will not be visible.

## Pre-trade safety checklist

Before placing any order, in this order:

1. **`market_get_ticker(instId)`** — confirm last/bid/ask are sane and the market is active.
2. **`account_get_balance(ccy)`** — verify funds in the relevant currency.
3. **`account_get_max_size(instId, tdMode, lever?)`** — get the max orderable size; never exceed it.
4. **`account_get_config()`** — confirm `posMode` (net vs long-short) before sending `posSide`.
5. For derivatives, **`market_get_mark_price`** + **`market_get_price_limit`** — your `px` must lie inside the price band.

Confirm with the user before any **live** order. Use small `sz` for first orders against a new market.

## Tool namespaces

| Namespace | Use when |
|-----------|----------|
| `market_*` | Public data (ticker, orderbook, candles, funding, OI, mark price). No auth needed. |
| `spot_*` | Spot orders. `sz` is in **base** currency for limit; for `market` buy, set `tgtCcy=quote_ccy` to size in quote. |
| `swap_*` | Perpetual contracts. Requires `posSide` (long/short) **only if** account is in long-short mode; `tdMode` ∈ {cross, isolated}. |
| `futures_*` | Dated delivery contracts. Same shape as swap, no funding. |
| `option_*` | Options. Use `option_get_instruments(uly)` first to discover contract `instId`s. |
| `account_*` | Balances, positions, bills, leverage, fees, audit log. |
| `bot.grid_*` | Grid bots — set `maxPx`/`minPx`/`gridNum`/`quoteSz`. |
| `bot.dca_*` | Recurring buys/sells with `interval` + bounds. |

## Order types (`ordType`)

- `market` — execute immediately at best available price. **No** `px`.
- `limit` — rest at `px`; partial fills allowed.
- `post-only` — limit that cancels if it would cross (taker). Use to guarantee maker fees.
- `fok` — fill-or-kill: fully filled or canceled.
- `ioc` — immediate-or-cancel: fill what's available, cancel the rest.

## Common workflows

**Place a limit spot order, then poll until filled or 60s elapsed:**
```
spot_place_order({instId:"BTC-USDT", side:"buy", ordType:"limit", sz:"0.001", px:"60000"})
→ {ordId:"..."}
spot_get_order({instId:"BTC-USDT", ordId:"..."})  # poll state ∈ {live, partially_filled, filled, canceled}
spot_cancel_order({instId:"BTC-USDT", ordId:"..."})  # if not filled in time
```

**Open a 5x long perpetual (cross margin, net mode):**
```
account_set_leverage({instId:"BTC-USDT-SWAP", lever:"5", mgnMode:"cross"})
swap_place_order({instId:"BTC-USDT-SWAP", side:"buy", ordType:"market", sz:"1", tdMode:"cross"})
swap_get_positions({instId:"BTC-USDT-SWAP"})
```

**Close a swap position fully:**
```
swap_close_position({instId:"BTC-USDT-SWAP"})
```

## Errors to handle

- **51008** insufficient balance → re-check `account_get_balance` and reduce `sz`.
- **51000** parameter error → almost always wrong `tdMode`/`posSide`/`px` precision; re-read instrument config.
- **51020** order size too small → below `minSz`; bump up.
- **50011** rate limit hit → back off; OKX limits trading to ≤60 req/2s per instrument family.
- **50102** timestamp drift → host clock is off, fix NTP.

## Sizing rules — short version

Always round **`px` to `tickSz`** and **`sz` to `lotSz`** for the instrument. See the `okx-instruments` skill for full precision and `ctVal`/`ctMult` rules. Failing to round causes `51000`.

## Read-only mode

If only `market_*` and `account_get_*` tools are visible, the connected key has Read permission only. Don't attempt order tools — they will be missing, and asking the user to "approve" won't unlock them.

## Audit log

`account_get_audit_log` returns every tool call this server has serviced for the connected key. Use it after any unexpected fill to reconcile.
)SKILL"},
      {"okx-instruments",
       R"SKILL(---
name: okx-instruments
emoji: "📐"
description: OKX instrument config — sizing precision, position modes, REST signing fallback
metadata:
  docs:
    - https://app.okx.com/docs-v5/en/#public-data-rest-api-get-instruments
    - https://app.okx.com/docs-v5/trick_en/
---

This skill encodes the precision and quoting rules every OKX order must obey.
Wrong rounding is the #1 cause of `51000 parameter error`. Use this skill
together with `okx-trading`.

## Instrument types

| `instType` | Examples | Settlement |
|------------|----------|------------|
| `SPOT`     | `BTC-USDT` | Same-day, base/quote currencies |
| `MARGIN`   | `BTC-USDT` (with `tdMode=cross\|isolated`) | Borrowed margin against spot |
| `SWAP`     | `BTC-USDT-SWAP` | Perpetual; funding paid every 8h |
| `FUTURES`  | `BTC-USDT-250627` | Dated delivery (quarterly/biweekly) |
| `OPTION`   | `BTC-USD-250627-70000-C` | European-style, USD-margined |

Discover live instruments with: `GET /api/v5/public/instruments?instType=SPOT`
or via the TradeKit `option_get_instruments` (options only).

## Critical fields from `/api/v5/public/instruments`

| Field | Meaning | Used for |
|-------|---------|----------|
| `instId` | Symbol | Every API call |
| `tickSz` | Min price increment (e.g. `0.1`) | `px` must be a multiple |
| `lotSz` | Min size increment (e.g. `0.0001`) | `sz` must be a multiple |
| `minSz` | Smallest legal `sz` | Floor on order size |
| `maxLmtSz` / `maxMktSz` | Per-order ceiling for limit / market orders | Cap on `sz` |
| `ctVal` | Contract value in `ctValCcy` (derivatives) | 1 SWAP/FUT contract = `ctVal` units |
| `ctMult` | Contract multiplier | Notional = `sz × ctVal × ctMult × px` |
| `ctValCcy` | Currency `ctVal` is denominated in | Notional unit |
| `lever` | Max allowed leverage | `account_set_leverage` ≤ this |
| `state` | `live` / `suspend` / `expired` | Skip if not `live` |
| `listTime` / `expTime` | ms epoch timestamps | Avoid trading pre-list / post-expiry |

## Sizing semantics by `instType`

- **SPOT limit**: `sz` is in **base** currency (e.g. BTC for BTC-USDT).
- **SPOT market buy**: `sz` defaults to base; pass `tgtCcy=quote_ccy` to size in quote (USDT).
- **SPOT market sell**: `sz` is always in base.
- **SWAP / FUTURES**: `sz` is **number of contracts**. Real notional = `sz × ctVal`.
  - Example: `BTC-USDT-SWAP` has `ctVal=0.01 BTC`, so `sz=10` ≈ 0.1 BTC.
- **OPTION**: `sz` is number of contracts; `ctVal` is typically `1` underlying.

## Precision rounding helper

```python
from decimal import Decimal, ROUND_DOWN

def round_to(value, step, mode=ROUND_DOWN):
    v = Decimal(str(value)); s = Decimal(str(step))
    return float((v / s).quantize(Decimal('1'), rounding=mode) * s)

px  = round_to(60123.456, "0.1")     # → 60123.4
sz  = round_to(0.001234, "0.0001")   # → 0.0012
```

Round `px` toward the side that's worse for you (buy → ROUND_DOWN, sell → ROUND_UP) so post-only orders don't accidentally cross.

## Position & trade modes

- **Account `posMode`** (`account_get_config.posMode`):
  - `net_mode` — one position per instrument; do **not** pass `posSide` (or use `net`).
  - `long_short_mode` — separate long and short books; **must** pass `posSide ∈ {long, short}`.
- **`tdMode`** per order:
  - `cash` — SPOT and unmargined OPTION.
  - `cross` — shared margin pool; liquidates worst position last.
  - `isolated` — margin walled per instrument; safer blast radius.

## Rate limits to respect

- **REST trading**: 60 req / 2s per instrument family per User ID (Options group by `uly`).
- **REST public**: 20 req / 2s per IP for most market endpoints.
- **WebSocket subscriptions**: 480 / hour per connection.
- **Bills/order history**: 5 req / 2s — cache aggressively.

Hitting `50011` means you're over the limit; back off ≥ 200 ms before retry.

## REST signing fallback (when MCP is unavailable)

Headers required for private endpoints:

```
OK-ACCESS-KEY:        <api key>
OK-ACCESS-SIGN:       base64(HMAC-SHA256(timestamp + method + path + body, secret))
OK-ACCESS-TIMESTAMP:  2026-04-30T12:34:56.789Z   # ISO 8601, ms precision
OK-ACCESS-PASSPHRASE: <passphrase set when API key was created>
Content-Type:         application/json
```

Demo trading: same hosts, add `x-simulated-trading: 1`.

Example signature payload for `POST /api/v5/trade/order` with body `{...}`:
```
"2026-04-30T12:34:56.789Z" + "POST" + "/api/v5/trade/order" + "{...}"
```

If server clock differs by >30s you get `50102 timestamp expired`.

## Currency conventions

- Quote currencies: `USDT`, `USDC`, `USD`, `BTC`, `ETH`.
- Always use the **canonical** instrument id (`BTC-USDT`, never `BTCUSDT`).
- Stablecoin pairs are not 1:1 — `USDT-USDC` has its own orderbook.
)SKILL"},
  };
  return kSkills;
}

}  // namespace quantclaw