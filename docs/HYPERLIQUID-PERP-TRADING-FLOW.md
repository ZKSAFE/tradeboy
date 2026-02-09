# Hyperliquid Perp Trading Flow (API)

> Sources (official docs):
> - API overview: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api
> - Exchange endpoint: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/exchange-endpoint
> - Info endpoint: https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint
> - Perpetuals (info): https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint/perpetuals
> - Fees: https://hyperliquid.gitbook.io/hyperliquid-docs/trading/fees
> - Margining: https://hyperliquid.gitbook.io/hyperliquid-docs/trading/margining
> - Margin tiers: https://hyperliquid.gitbook.io/hyperliquid-docs/trading/margin-tiers
> - Liquidations: https://hyperliquid.gitbook.io/hyperliquid-docs/trading/liquidations
> - Robust price indices: https://hyperliquid.gitbook.io/hyperliquid-docs/trading/robust-price-indices

---

## 1) Perp 交易流程（高层）

1. **拉取市场/账户数据**（info endpoint）
   - 拉取 perp 元数据（universe + margin tables）
   - 拉取 perp 资产上下文（mark price, funding, OI 等）
   - 拉取用户仓位/保证金状态（clearinghouseState）
2. **构造订单参数**（exchange endpoint）
   - 选择资产 `a`（perp universe 索引）
   - 选择方向 `b`、数量 `s`、价格 `p`、订单类型 `t`
   - 可选 reduceOnly / cloid / builder fee
3. **签名并下单**（exchange endpoint）
   - 带 `nonce` + `signature` 发起 `action: { type: "order" }`
4. **读回订单/成交/仓位**（info endpoint）
   - `openOrders` / `frontendOpenOrders`
   - `userFills`
   - `clearinghouseState`

---

## 2) 必要参数 & 来源

### 2.1 资产/市场参数
**来源：** `POST /info` with `{"type":"meta"}` 或 `{"type":"metaAndAssetCtxs"}`

- `universe`：perp 资产列表
- **perp 资产 ID（用于下单）**：`universe` 中的索引（Number）
- `margin tables`：用于维护保证金计算（margin tiers 文档）
- `assetCtxs`：包含 `mark price`、`funding`、`open interest` 等

### 2.2 下单参数（Exchange Endpoint）
**来源：** 客户端输入 + market meta + account 状态

`POST https://api.hyperliquid.xyz/exchange`

```json
{
  "action": {
    "type": "order",
    "orders": [{
      "a": 0,
      "b": true,
      "p": "1234.5",
      "s": "0.01",
      "r": false,
      "t": {
        "limit": { "tif": "Alo" | "Ioc" | "Gtc" }
      },
      "c": "0x..." 
    }],
    "grouping": "na" | "normalTpsl" | "positionTpsl",
    "builder": {"b": "0x...", "f": 10}
  },
  "nonce": 1700000000000,
  "signature": { /* see SDK */ },
  "vaultAddress": "0x...",
  "expiresAfter": 1700000000000
}
```

**字段含义（docs 原文 key）：**
- `a` = asset（perp 资产索引，来自 `meta` / `universe`）
- `b` = isBuy（true=多，false=空）
- `p` = price（字符串）
- `s` = size（字符串）
- `r` = reduceOnly
- `t` = type
  - `limit`：`tif` in `Alo | Ioc | Gtc`
  - `trigger`：`isMarket`, `triggerPx`, `tpsl` (`tp` | `sl`)
- `c` = cloid（可选 client order id）
- `builder`：可选 builder fee，`b` 为地址，`f` 为**0.1bp**单位（f=10 => 1bp）

**签名/nonce**：SDK 生成（docs 指向官方 SDK）。

### 2.3 账户 / 仓位
**来源：** `POST /info` with `{"type":"clearinghouseState","user":"0x..."}`

返回包含：
- 用户 perp 仓位列表
- 保证金/可用保证金/未实现盈亏等

---

## 3) 订单与成交回读

**Open orders**
```json
{"type":"openOrders","user":"0x..."}
```

**Frontend open orders**（含更多前端字段）
```json
{"type":"frontendOpenOrders","user":"0x..."}
```

**Fills**
```json
{"type":"userFills","user":"0x...","aggregateByTime":true}
```

---

## 4) 费用（Perp）

**来源：** https://hyperliquid.gitbook.io/hyperliquid-docs/trading/fees

- Perp 有分层费率（按 14d 加权成交量）
- Maker / Taker 不同
- Builder fee 可额外收取（exchange endpoint `builder` 参数）
- 文档说明：所有费用流向社区/援助基金/部署者

**注意：** 费率表很长，实际使用时需要取文档当前值。

---

## 5) 保证金与清算价格

### 5.1 Margining（docs）
- **开仓初始保证金**：
  ```
  position_size * mark_price / leverage
  ```
- **跨仓**：共享保证金；**逐仓**：独立保证金
- **转出保证金限制**：
  ```
  transfer_margin_required = max(initial_margin_required, 0.1 * total_position_value)
  ```
- **维护保证金**：目前等于“最大杠杆下初始保证金的一半”

### 5.2 Margin Tiers（docs）
- 维护保证金公式：
  ```
  maintenance_margin = notional_position_value * maintenance_margin_rate - maintenance_deduction
  ```
- `maintenance_margin_rate` / `maintenance_deduction` 由 margin tiers 决定
- tiers 在 `meta` 中返回（margin table IDs）

### 5.3 Liquidation Price（docs）
清算价公式：
```
liq_price = price - side * margin_available / position_size / (1 - l * side)
```
- `side = 1` for long, `-1` for short
- `l = 1 / MAINTENANCE_LEVERAGE`
- `margin_available (cross) = account_value - maintenance_margin_required`
- `margin_available (isolated) = isolated_margin - maintenance_margin_required`

注意：
- cross 仓位清算价格**与设置的 leverage 无关**（因为 leverage 只决定占用多少保证金）
- isolated 仓位清算价格与 leverage 相关（因为分配保证金取决于初始保证金）

---

## 6) Mark Price 与资金费

**Mark price (docs):**
- 由 **oracle price** + HL 自身 mid price + 多交易所价格综合构成
- 具体为三路价格的 median（含 EMA 修正）
- Mark price 用于：保证金、清算、触发 TP/SL、未实现盈亏

资金费（funding）相关数据可通过：
- `POST /info` `{ "type": "fundingHistory", "coin": "ETH", ... }`
- `POST /info` `{ "type": "userFunding", ... }`

---

## 7) 建议的实现步骤（TradeBoy）

1. `meta` / `metaAndAssetCtxs` 拉取资产列表与上下文
2. 显示用户仓位：`clearinghouseState`
3. 下单时：
   - asset index from `universe`
   - 用户输入方向/价格/数量
   - 调用 exchange endpoint `type=order`
4. 回读：`openOrders` + `userFills`

---

## Appendix: API Endpoints (Perp)

**Exchange**
- `POST /exchange` (action: order / cancel / modify / leverage / margin)

**Info**
- `POST /info` type: `meta`, `metaAndAssetCtxs`, `clearinghouseState`
- `POST /info` type: `openOrders`, `frontendOpenOrders`, `userFills`
