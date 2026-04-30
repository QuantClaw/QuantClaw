---
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
