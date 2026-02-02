# Hyperliquid：推荐返佣 / Builder 分成 / Fee Share 机制与 API（整理）

> 本文基于 Hyperliquid GitBook 文档整理：
> - https://hyperliquid.gitbook.io/hyperliquid-docs
> - 重点覆盖：Referrals、Builder codes、Staking referral program proposal、Info endpoint、Exchange endpoint、HIP-3。
>
> 目标：回答“是否有代理/分销/分成说明，以及对应 API 接口？”并给出可直接用于实现的请求体/端点清单。

---

## 1. 术语对照（用来理解“代理/分销/分成”在 HL 里的对应物）

- **Referral code（推荐码/分销码）**
  - 用户通过 referral link 加入后绑定。
  - 绑定后对该用户后续交易产生折扣/返佣（直到达到一定交易量上限）。
  - 更像“分销/渠道返佣”。

- **Builder code（Builder 分成）**
  - Builder（做交易终端/机器人/界面/聚合器的人）可对“自己路由的订单”收一笔额外费用 **builder fee**。
  - builder fee **100% 归 builder**。
  - 每一笔订单可以独立指定 builder 参数；用户需先授权该 builder 的最大可收费用（max builder fee）。
  - 更像“代理商/渠道商对自己带来的交易收取服务费”。

- **HIP-3 Deployer Fee Share（HIP-3 市场部署者分成）**
  - HIP-3 允许 permissionless 的 perp DEX/market deployer。
  - 文档明确写了 deployer fee share 固定 50%（在特定 fee scale 下的表现也会在 fees/公式里反映）。
  - 更像“市场方/协议层分成”，不是传统分销。

---

## 2. Referrals（推荐返佣机制）

文档：
- https://hyperliquid.gitbook.io/hyperliquid-docs/referrals

### 2.1 规则摘要

- **创建推荐码门槛**：完成 $10,000 volume 后可在 App 创建 referral code。
- **推荐人返佣**：推荐人获得被推荐用户手续费的 **10%**，但会扣除被推荐用户享受的折扣部分。
- **返佣上限**：返佣仅对被推荐用户 **前 $1B cumulative volume** 生效。
- **被推荐人折扣**：被推荐用户可获得 **4% 手续费折扣**，仅对其 **前 $25M volume** 生效。
- **奖励结算**：
  - 返佣对所有 quote assets 累计。
  - 当累计奖励 > $1 时可 claim。
  - claim 后会体现在 spot balance。

### 2.2 API：如何查询 referral 状态

Referrals 页面本身更偏产品说明，但**referral 状态可通过 info endpoint 查询**（见 4.2）。

---

## 3. Builder Codes（Builder 分成机制）

文档：
- https://hyperliquid.gitbook.io/hyperliquid-docs/trading/builder-codes

### 3.1 规则摘要（分成/上限）

- Builder codes 的 builder 不是区块构建者，而是“DeFi builders（开发应用）”。
- 用户需要先通过 **ApproveBuilderFee** action 为特定 builder 授权最大可收 builder fee，可随时 revoke。
- Builder 必须满足最低条件：文档写明 builder 至少需要 **100 USDC perps account value**。
- 费用适用范围：
  - builder fee 只作用于以 quote/collateral 计费的部分。
  - 文档写：builder codes **不适用于 spot 买入侧**，但适用于 perp 双边。
- 费用上限：
  - **Perps：最多 0.1%**
  - **Spot：最多 1%**

### 3.2 订单里如何携带 builder 参数

授权完成后，builder 可在“代表用户发送的订单 action”里附带 builder 参数：

```json
{"b": "0x...builder", "f": 10}
```

- `b`：builder 地址
- `f`：builder fee，单位是 **0.1 bp（tenths of basis points）**
  - 例：`f = 10` 表示 1 bp = 0.01%

### 3.3 API：查询 builder fee 授权、builder 收入、fills 明细

- 查询某 user 对某 builder 的最大可收 fee（授权状态）：

```json
{"type": "maxBuilderFee", "user": "0x...", "builder": "0x..."}
```

- builder 收到的总费用（builderRewards）包含在 referral state 中：

```json
{"type": "referral", "user": "0x..."}
```

- builder fills 明细下载（lz4 压缩 CSV）：

```
https://stats-data.hyperliquid.xyz/Mainnet/builder_fills/{builder_address}/{YYYYMMDD}.csv.lz4
```

注意：
- URL **区分大小写**
- `builder_address` 必须全小写

### 3.4 领取 builder fee

文档写：builder fees 通过“usual referral reward claim process”领取。
（注意：本次整理未在 GitBook 中进一步定位到 claim action 的具体 request body；建议后续从 Python SDK 示例或 Exchange endpoint 的 claim 类 action 章节进一步确认。）

---

## 4. For Developers：Info Endpoint（查询类 API）

文档：
- https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint

### 4.1 统一入口

- **POST** `https://api.hyperliquid.xyz/info`
- `Content-Type: application/json`
- 通过请求体中的 `type` 字段决定返回 schema。

### 4.2 与“返佣/分成”直接相关的 info types

- Referral 状态：

```json
{"type": "referral", "user": "0x..."}
```

文档备注：
- `rewardHistory` 是 legacy rewards。
- 已 claim 的 rewards 现在通过 `nonFundingLedgerUpdate` 返回。

- Builder 授权状态（max builder fee）：

```json
{"type": "maxBuilderFee", "user": "0x...", "builder": "0x..."}
```

- 用户手续费信息（会涉及 referral discount 等）：

```json
{"type": "userFees", "user": "0x..."}
```

### 4.3 其它常用（与实现交易终端时常常一起用到）

- 全市场 mid：

```json
{"type": "allMids"}
```

- openOrders / frontendOpenOrders / userFills / userFillsByTime 等（略）。

### 4.4 Spot / Perp coin 命名差异（对接时的坑点）

Info endpoint 文档提到：
- Perp 的 `coin` 用 meta 返回的 name。
- Spot 的 `coin`：
  - PURR 用 `PURR/USDC`
  - 其它多数 spot token 用 `@{index}`（index 来自 spotMeta.universe 中 spot pair 的 index）
- 并提示：某些资产在 UI 里有 remap（例：UI 的 BTC/USDC 在 mainnet HyperCore 对应 UBTC/USDC）

---

## 5. For Developers：Exchange Endpoint（交易/授权类 API）

文档：
- https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/exchange-endpoint

### 5.1 统一入口

- **POST** `https://api.hyperliquid.xyz/exchange`
- `Content-Type: application/json`
- 典型 request 结构：
  - `action`: object（不同 `action.type` 对应不同功能）
  - `nonce`: number（推荐 ms timestamp）
  - `signature`: object（签名）
  - 可选：`vaultAddress`、`expiresAfter` 等

### 5.2 与 builder 分成直接相关：Approve builder fee

```json
{
  "action": {
    "type": "approveBuilderFee",
    "hyperliquidChain": "Mainnet",
    "signatureChainId": "0xa4b1",
    "maxFeeRate": "0.001%",
    "builder": "0x...",
    "nonce": 1730000000000
  },
  "nonce": 1730000000000,
  "signature": { }
}
```

关键点：
- 文档强调：该 action **必须由用户主钱包签名**，不能用 agent/API wallet。

### 5.3 订单 action 内携带 builder 参数

Builder codes 文档说明：订单 action 可包含 builder 参数 `{"b": address, "f": number}`。

（具体“place order”的 action schema 在 exchange endpoint 的 place order 章节；本文聚焦分成相关点，不赘述下单字段。）

---

## 6. Staking Referral Program（提案：更高阶的返佣/分成）

文档：
- https://hyperliquid.gitbook.io/hyperliquid-docs/referrals/proposal-staking-referral-program

### 6.1 Builder code 与 Referral code 的差异（文档解释很清晰）

- Builder code：按订单收取 builder fee，**100% 给 builder**。
- Referral code：用户 join 时绑定，之后持续生效（直到用户累计交易量达到限制）。
- 文档写：**builder codes 会 override referral codes（对该笔订单）**。
- 文档写：referral codes 在用户累计交易量达到 **$1B** 后会 disable。

### 6.2 分成比例（与 staking tier / VIP tier 交互）

核心规则：
- 若 builder/referrer 的 staking tier 高于被推荐用户，在该笔交易中：
  - builder/referrer 最多可“保留”两者 staking discount 差额的 **一部分**
  - 这个“保留比例”随被推荐用户的 VIP tier 增长而下降：
    - VIP0：100%
    - VIP1：90%（volume > 5M）
    - VIP2：80%（>25M）
    - VIP3：70%（>100M）
    - VIP4：60%（>500M）
    - VIP5：50%（>2B）
    - VIP6：40%（>7B）

文档示例（原文）：
- Alice staking discount 30%
- Bob staking discount 10%，VIP1
- Alice keep (30%-10%) * 90% = 18% of the fees Bob pays
- Alice 可以分享给 Bob 最高 9%（等价于 Bob 最多拿到 9% fee discount）

---

## 7. HIP-3：Builder-deployed perpetuals（deployer fee share）

文档：
- https://hyperliquid.gitbook.io/hyperliquid-docs/hyperliquid-improvement-proposals-hips/hip-3-builder-deployed-perpetuals

### 7.1 与“分成”直接相关的结论

文档明确写：
- HIP-3 markets 会纳入常见 fee discount 来源（staking discount、referral rewards、aligned collateral discount）。
- **从 deployer 视角：fee share 固定 50%**。
- **从 user 视角：手续费是 validator-operated perp market 的 2x**。
- 净效应：协议收取的费用总体与在 validator-operated perp 上一致。
- 用户 rebate 不受影响，且不与 deployer 交互。

---

## 8. 签名与实现建议（重要注意事项）

文档：
- https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/signing

关键点：
- 推荐用 SDK，不建议手搓签名。
- 有两套签名方案：
  - `sign_l1_action`
  - `sign_user_signed_action`
- msgpack 字段顺序、数值 trailing zeros、地址大小写（建议全部 lowercase）都可能导致签名错误。

---

## 9. 最小实现清单（如果你要做“代理/分销/分成”功能）

### 9.1 “分销/推荐”能力
- App 侧：生成/绑定 referral code（更多在 UI/后台，不一定有公开 API 文档）
- 程序侧：
  - `POST /info` type=`referral` 查询返佣/绑定关系/累计量
  - `POST /info` type=`userFees` 查询 activeReferralDiscount 等

### 9.2 “Builder 分成”能力
- 让用户在 UI 里授权 builder：
  - `POST /exchange` action=`approveBuilderFee`
- 下单时携带 builder 参数（builder code）：
  - order action 增加 `{"b": builder, "f": fee}`
- 对账/统计：
  - `POST /info` type=`maxBuilderFee`
  - `POST /info` type=`referral`（读取 builderRewards 等）
  - 下载 `builder_fills/*.csv.lz4` 进行明细核对

---

## 10. 参考链接（原始文档）

- Referrals
  - https://hyperliquid.gitbook.io/hyperliquid-docs/referrals
- Proposal: Staking referral program
  - https://hyperliquid.gitbook.io/hyperliquid-docs/referrals/proposal-staking-referral-program
- Builder codes
  - https://hyperliquid.gitbook.io/hyperliquid-docs/trading/builder-codes
- Info endpoint
  - https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint
- Exchange endpoint
  - https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/exchange-endpoint
- Signing
  - https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/signing
- HIP-3: Builder-deployed perpetuals
  - https://hyperliquid.gitbook.io/hyperliquid-docs/hyperliquid-improvement-proposals-hips/hip-3-builder-deployed-perpetuals
