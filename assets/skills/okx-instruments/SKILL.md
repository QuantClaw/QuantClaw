---
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
