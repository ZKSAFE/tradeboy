# Hyperliquid 行情接入说明（TradeBoy 当前实现）

本文档说明 TradeBoy 当前如何从 **Hyperliquid API** 获取行情、运行所需环境/依赖、踩坑点、以及返回数据如何解析。用于未来 UI 重构或把数据源替换成 WebSocket。

## 1. 总体架构（当前代码）

当前实现是 **可替换数据源** 的结构（便于将来把 REST/wget 改为 WS）：

- **Model（状态）**：`src/model/TradeModel.*`
  - 保存 `spot_rows`（symbol/price/prev_price/balance 等）、`tf_idx`、`spot_row_idx`、`kline_data`
  - 内部自带 `mutex`，提供 `snapshot()` + setters + 更新函数
  - 不做 IO、不启动线程、不依赖 UI/App
- **DataSource（数据源接口）**：`src/market/IMarketDataSource.h`
  - `fetch_all_mids_raw(out_json)`
  - `fetch_candle_snapshot_raw(req, out_json)`
- **DataSource 实现（当前）**：`src/market/HyperliquidWgetDataSource.*`
  - 通过 `src/market/Hyperliquid.*` 内基于 `wget` 的 HTTPS POST 拉取 JSON
- **Service（轮询线程/backoff）**：`src/market/MarketDataService.*`
  - 独立线程
  - 周期拉取 `allMids` 和 `candleSnapshot`，并写入 `TradeModel`
- **Presenter（派生 UI 数据）**：`src/spot/SpotPresenter.*`
  - 输入：`TradeModelSnapshot + SpotUiState`
  - 输出：`SpotViewModel`（字符串/颜色等）
- **View（渲染）**：`src/spot/SpotScreen.*`
  - 只渲染 `SpotViewModel`，不做 IO、不加锁、不解析 JSON

> 如果未来“行情图会砍掉”，可以只保留 `allMids` 链路，删除 candle/KLine 相关逻辑；解耦结构不需要改。

## 2. API 调用方式（当前使用 REST `/info`）

### 2.1 Base URL

- REST：`https://api.hyperliquid.xyz/info`

### 2.2 获取全部 mid 价格：`allMids`

- **HTTP**：`POST /info`
- **Body**：

```json
{"type":"allMids"}
```

- **返回**：JSON 对象，键是 symbol（例如 `BTC`/`ETH`），值是价格字符串：

```json
{"BTC":"90094.5","ETH":"3072.85", "SOL":"137.015", "...":"..."}
```

### 2.3 获取 K 线快照：`candleSnapshot`

- **HTTP**：`POST /info`
- **Body**：

```json
{
  "type":"candleSnapshot",
  "req":{
    "coin":"BTC",
    "interval":"1m",
    "startTime": 1767960000000,
    "endTime":   1767970000000
  }
}
```

- **返回**：JSON 数组，每个元素类似：

```json
{
  "t":1767970020000,
  "T":1767970079999,
  "s":"BTC",
  "i":"1m",
  "o":"90141.0",
  "c":"90175.0",
  "h":"90210.0",
  "l":"90090.0",
  "v":"22.47997",
  "n":425
}
```

TradeBoy 当前只用 `o/h/l/c` 这四个字段构建 `OHLC`。

## 3. 运行环境与依赖（为什么用 `wget`）

### 3.1 交叉编译环境（ARMHF builder）的限制

在 ARMHF Docker builder 内测试：

- 没有 `libcurl`
- 没有 OpenSSL（`-lssl -lcrypto`）

因此无法在 C++ 内直接实现 `https://...` TLS 请求（除非引入 mbedtls/openssl 源码或换 toolchain）。

### 3.2 设备端可用工具

设备上存在：

- `/usr/bin/wget`（GNU Wget 1.21.2）

所以当前实现采用：

- 程序通过 `popen()` 调用 `wget` 拉取 HTTPS JSON

优点：

- 不引入 TLS 依赖，跨编译稳定

缺点：

- 依赖设备系统命令
- 性能一般
- 请求频率需要控制（否则容易失败/限流）

## 4. 具体网络实现（代码落点）

文件：`src/market/Hyperliquid.cpp`

### 4.1 POST 方式（避免 shell 转义坑）

实现策略：

- 把 JSON body 写到 `/tmp/*.json`
- 用 `wget --post-file=/tmp/*.json` 发送
- 避免在 shell 命令行内拼 JSON（容易转义失败导致 400）

概念命令：

```sh
/usr/bin/wget -qO- \
  --header="Content-Type: application/json" \
  --post-file=/tmp/hl_allmids.json \
  https://api.hyperliquid.xyz/info
```

### 4.2 错误诊断（失败时回退到带 headers 的 wget）

当前实现会先尝试 `-qO-` 拉 body；失败后再尝试一次 `-S -O- 2>&1`，把 headers/错误输出带回来，便于写入 `log.txt` 定位。

## 5. JSON 解析方式（当前为轻量解析）

> 目前没有引入正式 JSON 库（是为了避免交叉编译依赖膨胀）。属于“最小可用”解析。

### 5.1 `allMids` 解析

文件：`src/market/Hyperliquid.cpp`

- 查找 `"<COIN>":"<price>"` 片段
- needle：`"\"" + coin + "\":"`
- 解析后面的 quoted string
- `strtod()` 转 double

接口：

- `parse_mid_price(const std::string& all_mids_json, const std::string& coin, double& out_price)`

### 5.2 `candleSnapshot` 解析

文件：`src/market/Hyperliquid.cpp`

- 在 JSON 数组中循环找 `{...}`
- 从对象片段内查 `"o"/"h"/"l"/"c"` 对应的字符串
- 转 float 写入 `OHLC`

接口：

- `parse_candle_snapshot(const std::string& candle_json) -> std::vector<OHLC>`

说明：这依赖返回格式稳定；未来如果要更健壮，建议改用正式 JSON 库（需要处理 toolchain/依赖）。

## 6. 轮询频率、限流与稳定性坑（踩过的坑）

### 6.1 价格“过一段时间不变”

现象：UI 价格停更。

根因：

- 高频请求或网络抖动导致 `wget` 失败
- 初期没有失败日志，表现为“停更”

处理：`MarketDataService` 内已经做了：

- `allMids` 默认约 **2.5 秒一次**
- 失败 backoff：5s -> 10s -> 20s -> 30s 上限
- 失败写 `log.txt`（例如 `[Market] allMids fetch failed ...`）

### 6.2 shell 引号/转义导致 400

在命令行直接拼 `--post-data='{...}'` 很容易被转义破坏。

最终解决：**写文件 + `--post-file`**。

### 6.3 TLS 依赖缺失

builder 里没 curl/openssl，所以才走 `wget`。

## 7. Timeframe 与 candle 请求映射（当前实现）

`MarketDataService` 根据 `tf_idx` 映射：

- `tf_idx==0`（24H）：`interval="1h"`，`start=now-24h`
- `tf_idx==1`（4H）：`interval="15m"`，`start=now-4h`
- `tf_idx==2`（1H）：`interval="5m"`，`start=now-1h`

## 8. 未来改 WebSocket（建议方向，不是当前实现）

未来切到 WS 推荐：

- 新增 `HyperliquidWsDataSource : IMarketDataSource`
- `MarketDataService` 继续只更新 `TradeModel`
- 上层 UI/Persenter 不动

WS 订阅文档参考：Hyperliquid Docs 的 `wss://api.hyperliquid.xyz/ws` 订阅 `allMids` / `candle`。

## 9. 相关文件索引

- `src/model/TradeModel.h/.cpp`：行情状态与线程安全快照
- `src/market/Hyperliquid.h/.cpp`：wget 调用 + 轻量解析
- `src/market/IMarketDataSource.h`：数据源接口
- `src/market/HyperliquidWgetDataSource.*`：当前数据源实现
- `src/market/MarketDataService.*`：轮询线程/backoff
- `src/spot/SpotPresenter.*`：model snapshot -> SpotViewModel
- `src/spot/SpotScreen.*`：纯渲染
- `install.sh`：部署脚本（已强制 password auth + retry）
