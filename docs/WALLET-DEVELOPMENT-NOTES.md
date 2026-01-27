# Wallet 开发笔记（TradeBoy / RG34XX）

## 背景

本笔记用于记录 TradeBoy 在 RG34XX 上进行钱包/链上交易相关开发过程中遇到的问题、原因分析与解决办法。

目标：

- 让程序在 RG34XX 上稳定启动与运行（避免 ABI/运行时崩溃）。
- 实现 Arbitrum One 上 USDC（6 decimals）转账测试：按键触发后广播交易，成功弹框显示 txhash，失败弹框显示原因，并把原始错误写入 `log.txt`。

---

## 问题 1：RG34XX 启动阶段随机闪退（SIGSEGV / SIGABRT）

### 现象

- 启动阶段看似崩在 `pthread_mutex_lock` / `TradeModel::set_spot_rows`。
- 也出现过“卡在 loading”之类表象。

### 根因

- RG34XX 的 armhf 用户态环境对 **C++ ABI / libstdc++ / varargs 调用约定**非常敏感。
- 典型触发点：
  - `std::mutex` / `std::lock_guard` 在该环境可能出现 ABI 不兼容或未定义行为。
  - 使用 `log_to_file(fmt, ...)` 这种 **可变参数 varargs** 的日志，在某些点会破坏栈，导致后续看似无关的位置崩溃（例如 mutex lock 直接 abort）。

### 解决办法

- 避免 `std::mutex`：改为 `pthread_mutex_t`，并确保初始化策略稳定（静态初始化或明确 init/destroy）。
- 启动关键路径禁止 varargs 日志：
  - 将启动阶段所有 `log_to_file("...%d...", ...)` 改为 `log_str("...")` 常量日志。
  - 将“首帧 tracing”一类的格式化日志也改为常量日志。

---

## 问题 2：交易/数据结构写入导致崩溃（std::string / vector 赋值）

### 现象

- 某些设备上在写入模型数据（例如 rows）时发生崩溃。

### 根因

- 在特定 armhf 环境下，频繁的 `std::string` 复制/赋值链条 + 多线程/ABI 组合可能触发未定义行为。

### 解决办法

- 在模型更新中优先使用 **swap/move** 替代深拷贝赋值。
- 减少跨线程共享中复杂对象的频繁复制。

---

## 问题 3：Arbitrum 交易广播返回 -32000（max fee per gas less than block base fee）

### 现象

- `eth_sendRawTransaction` 返回：
  - `code=-32000`
  - `message="max fee per gas less than block base fee ... maxFeePerGas: 0 baseFee: ..."`

### 根因

- Arbitrum 是 EIP-1559 链；节点会校验交易费用不能低于区块 baseFee。
- 本项目采用 legacy tx（`gasPrice`）路径时：
  - 若 `gasPrice` 太低，会被 baseFee 拒绝。
  - 另一个关键坑：若将十六进制 quantity 格式化成 **奇数位**（例如 `0x1abcd`），而 hex 解析器只接受偶数位，则会解析失败，最终在 RLP 中编码成空串，表现为 `gasPrice = 0`。

### 解决办法

- 读取 `eth_getBlockByNumber(latest)` 的 `baseFeePerGas`。
- 若 `gasPrice < baseFeePerGas`：将 `gasPrice` bump（例如 `1.5x baseFee + 1 wei`）。
- 修复 hex quantity 解析：
  - `hex_quantity_to_bytes_be` 支持奇数位（左侧补 0 nibble）。
  - bump 出来的 `gas_hex` 确保偶数位。

---

## 问题 4：Arbitrum 转 USDC 失败（insufficient funds for gas * price + value）

### 现象

- `eth_sendRawTransaction` 返回：
  - `insufficient funds for gas * price + value ... have 0 want ...`

### 根因

- 即使转的是 USDC（ERC20），也必须用 **Arbitrum ETH** 支付 gas。
- 钱包地址在 Arbitrum 上没有 ETH 时，必然失败。

### 解决办法

- 发送前做预检查：`eth_getBalance` 查询 ETH 余额。
- 计算 `want = gasPrice * gasLimit`，若 `have < want`：
  - 直接弹框提示 `insufficient_eth_for_gas have=... want=...`
  - 同时写入 `log.txt`。
- 使用者需要先给该地址转入少量 Arbitrum ETH（例如 0.0005~0.002 ETH）。

---

## 问题 5：第一次按充值失败，第二次成功（RPC no_response）

### 现象

- 第一次按下：`eth_getTransactionCount` / `eth_gasPrice` 失败，`no_response`（resp 为空）。
- 第二次按下：成功。

### 根因

- `wget` + 网络环境在设备上存在偶发抖动：DNS/TLS/短时超时。
- 这类失败属于“瞬时无输出”，不是 JSON-RPC 返回 error。

### 解决办法

- 对 `no_response` 增加轻量重试：
  - nonce / gasPrice 各重试 2~3 次（短 sleep 例如 250ms）。
- 每次 attempt 记录：attempt 编号、resp_len、resp_prefix。

---

## 问题 6：RPC 错误信息不够，难以定位

### 现象

- UI 只显示 `DEPOSIT_FAILED code=-32000`，不知道 message/data。

### 根因

- 解析只读取 `result`，忽略 JSON-RPC `error` 对象。

### 解决办法

- 增加 `error` 对象解析（轻量）：抽取 `code/message/data_prefix`。
- 将以下内容写入 `log.txt` 并尽可能展示在弹框：
  - `rpc_error code=... message="..." data_prefix=...`
  - `resp_prefix=<<<...>>>`（截断前 512 字符）

---

## 问题 7：Hyperliquid `/exchange` 的签名错误会导致“随机 user 地址 / 偶发成功失败”

### 现象

- 内部划转（`usdClassTransfer`）有时成功，有时失败。
- 失败时返回：
  - `Must deposit before performing actions. User: 0x...`
- `User: 0x...` 的地址和本地钱包地址不一致，并且会随着输入（甚至同样输入的不同尝试）变化。

### 根因

- Hyperliquid 的 user-signed action 使用 EIP-712 typed-data。
- 任何签名细节错误（domain/types/字段顺序/数值字符串格式/地址大小写等）都会导致服务端 recover 出不同的 signer。
- 即使本地用某种方式 recover 出“看起来正确”的地址，也不能证明服务端 recover 使用的 payload 与本地一致。
- 另外，ECDSA 的签名过程会导致 recovery id 的选择存在分支；如果 recovery 参数处理不一致（尤其是 low-s 规范下的 parity 翻转），会表现为“偶发”。

### 解决办法

- 严格对照官方文档与 Python SDK：
  - `sign_user_signed_action` / `encode_typed_data` 的 domain/types/message 必须一字不差。
  - 地址字段统一使用小写。
  - 数字字段作为 string 时避免多余尾零/科学计数法。
- 在 C++ 侧实现时，确保签名与 recover 的 recovery 参数一致（low-s parity 翻转要纳入 recid 选择）。
- 为了定位此类问题，可临时加入对照日志：digest、recover 出来的地址集合、最终选用的 v/r/s、resp_prefix；确认稳定后再移除。

## 部署/脚本相关

### upload.sh：避免自动 kill 进程

- 上传脚本中不强制 `killall -9 tradeboy-armhf`，避免干扰正在跑的进程状态。

---

## 备注

- USDC 精度为 6（micro = 1e-6）。
- 在 RG34XX 上，涉及日志/并发/格式化输出时优先保守实现，避免引入 ABI/varargs 风险。
