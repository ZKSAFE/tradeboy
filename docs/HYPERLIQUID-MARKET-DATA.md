# Hyperliquid 市场数据（价格 / 24H / 元数据）

## 目标

本文件总结 TradeBoy 如何从 Hyperliquid 获取并组合：

- 实时价格（real-time）
- 24H 涨跌与成交量（server-aggregated）
- 市场元数据（交易对列表、精度/小数位、Spot token id 等）

并说明如何组合 **Info(HTTP)** 与 **WebSocket**，在支持大量交易对的同时保持开销可控。

## 官方网页（app.hyperliquid.xyz）的核心思路

### 1) 元数据（低频更新）

通过 `POST /info` 获取几乎不变的元数据：

- 永续：`{ "type": "allPerpMetas" }`
- 现货：`{ "type": "spotMeta" }` 或（推荐）`{ "type": "spotMetaAndAssetCtxs" }`

元数据中包含：

- `universe`：交易对列表
- `tokens`：token 列表（`name/fullName/szDecimals/weiDecimals/index/tokenId/...`）

### 2) 24H（由服务端聚合，客户端只做展示）

市场列表里的 24H 数据通常来自 `assetCtxs` 的聚合字段：

- `midPx`
- `prevDayPx`
- `dayNtlVlm`（24H notional volume）

客户端计算：

- `change = midPx - prevDayPx`
- `changePct = (midPx - prevDayPx) / prevDayPx`

重点：客户端一般不需要回放每笔成交来计算 24H。

### 3) 实时价格（单一 WS 订阅）

可扩展的做法：

- WebSocket 只订阅一次 `allMids`
- 从 `mids` map 中更新 UI 的 mid price

这样可以避免每个币单独订阅。

## TradeBoy 推荐接入方式

### 启动阶段（一次性）

- 拉取 perp 元数据：`allPerpMetas`
- 拉取 spot 元数据+ctx：`spotMetaAndAssetCtxs`
- 将 raw JSON 缓存进 model（方便后续解析与复用）

### 运行阶段（实时）

- 持续运行 WS `allMids`
- 用 `SpotRow.coin` 作为 key 从 `mids` 更新 `SpotRow.price`

### 24H 展示

- 优先使用 `spotMetaAndAssetCtxs` 的 `assetCtxs.prevDayPx` 来计算 24H
- 需要时再周期性刷新 `spotMetaAndAssetCtxs`（低频），更新 `prevDayPx/dayNtlVlm` 等

## Spot/USDC 排名对齐（本次 Python 验证结论）

在对齐网页的 Spot->USDC 列表时，需要同时处理两类“对齐问题”：

### 1) displayName（网页显示名）

网页会对部分 token 做显示名映射（例如 `USDT0 -> USDT`、`UMON -> MON`）。

结论：不要用简单字符串裁剪规则，优先使用显式的 `l1Name -> displayName` 映射表（来自网页 bundle）。

### 2) assetCtxs 的 coin key 不唯一（关键坑）

同一个 Spot/USDC 交易对，在 `assetCtxs[]` 中的 `coin` key 可能出现三种形式：

- `BASE`（例如 `BTC`）
- `@<universe.index>`（例如 `@243`）
- `BASE/USDC`（例如 `PURR/USDC`）

因此在把 `universe` 的交易对关联到 `assetCtxs` 时，不能只用单一 key。推荐策略：

- 先确定“用于 allMids 的 price key”
  - canonical：用 `BASE`
  - non-canonical：用 `@<index>`
- 关联 ctx 时按顺序尝试：
  - price key（`BASE` 或 `@<index>`）
  - `@<index>`
  - `BASE/USDC`

只有这样才能保证：

- `dayNtlVlm` 排名与网页一致
- `prevDayPx/midPx` 能正确用于 24H 展示
- WS `allMids` 能持续更新价格

