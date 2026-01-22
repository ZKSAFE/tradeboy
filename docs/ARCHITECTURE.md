# TradeBoy Architecture Overview

This document describes the current TradeBoy data flow and module responsibilities after the data-layer refactor.

## Goals

- UI is **read-only**: it never performs IO, JSON parsing, or network calls.
- All IO/WS/RPC parsing happens in the **data layer** (data sources + services).
- UI only reads **TradeModel snapshots**.

## High-level Flow

```
[Network/API]
   |  (Hyperliquid WS/HTTP, Arbitrum RPC)
   v
[Data Sources]
   |  (raw JSON, cached)
   v
[Services]
   |  (polling/backoff + parsing)
   v
[TradeModel]
   |  (mutex-protected state)
   v
[Presenter]
   |  (formatting/derived UI values)
   v
[UI Screens]
```

## Modules and Responsibilities

### 1) TradeModel (State)
- **Files**: `src/model/TradeModel.h/.cpp`
- Owns all application state used by UI.
- Thread-safe with `pthread_mutex_t`.
- Exposes snapshot getters:
  - `snapshot()` for spot rows
  - `wallet_snapshot()` for wallet info
  - `account_snapshot()` for balances
- Exposes setters for data layer:
  - `set_spot_rows`, `set_spot_row_idx`
  - `set_hl_usdc` (Hyperliquid USDC spot balance)
  - `set_arb_wallet_data` (ETH/USDC/gas)

### 2) Data Sources (Raw API + Cache)
- **Interface**: `src/market/IMarketDataSource.h`
  - `fetch_all_mids_raw(out_json)`
  - `fetch_spot_clearinghouse_state_raw(out_json)`
  - `fetch_user_webdata_raw(out_json)`
  - `set_user_address(...)`

- **HyperliquidWgetDataSource** (`src/market/HyperliquidWgetDataSource.*`)
  - Uses HTTP `/info` via `wget` (fallback path).

- **HyperliquidWsDataSource** (`src/market/HyperliquidWsDataSource.*`)
  - Maintains a WS connection via `openssl s_client`.
  - Subscribes to `allMids` and caches the latest mids JSON.
  - Uses **WS post** to request `spotClearinghouseState` and caches the response.

### 3) Services (Polling + Parsing + Backoff)
- **MarketDataService** (`src/market/MarketDataService.*`)
  - Background thread.
  - Periodically fetches:
    - `allMids` from data source cache
    - `spotClearinghouseState` via WS post
  - Parses USDC spot balance and updates `TradeModel`.
  - Uses backoff when fetch/parsing fails.

- **ArbitrumRpcService** (`src/arb/ArbitrumRpcService.*`)
  - Background thread polling Arbitrum RPC.
  - Updates `TradeModel` with ETH/USDC/gas info.

### 4) Presenter (Formatting)
- **SpotPresenter** (`src/spot/SpotPresenter.*`)
  - Converts `TradeModelSnapshot` to `SpotViewModel` for UI.
  - Responsible for formatting numbers, colors, and labels.

### 5) UI Screens (Rendering Only)
- **SpotScreen** (`src/spot/SpotScreen.*`)
- **AccountScreen** (`src/account/AccountScreen.*`)
- **PerpScreen**, **SpotOrderScreen**

UI screens take **view models or snapshots** and render only.
No IO, no parsing, no locks.

## Data Flow: Hyperliquid USDC (SPOT)

1. `MarketDataService` calls `fetch_spot_clearinghouse_state_raw()`.
2. `HyperliquidWsDataSource` issues a WS `post` request:
   ```json
   {"method":"post","id":1,"request":{"type":"info","payload":{"type":"spotClearinghouseState","user":"0x..."}}}
   ```
3. WS response is cached by the data source.
4. `MarketDataService` parses USDC balance and calls `TradeModel::set_hl_usdc`.
5. Account UI reads `TradeModel::account_snapshot()` and renders `hl_usdc_str`.

## Data Flow: Hyperliquid allMids

1. WS subscription (`allMids`) caches `mids` JSON inside `HyperliquidWsDataSource`.
2. `MarketDataService` fetches cached JSON and calls `TradeModel::update_mid_prices_from_allmids_json`.
3. Spot UI reads `TradeModelSnapshot` and displays prices.

## Data Flow: Arbitrum Wallet

1. `ArbitrumRpcService` polls RPC endpoints.
2. Parses ETH/USDC balances and gas.
3. Updates `TradeModel::set_arb_wallet_data`.
4. Account UI renders from `account_snapshot()`.

## Threading / Concurrency

- **Services** run in background threads.
- `TradeModel` is the only shared state across threads.
- UI thread only reads snapshots; no mutex contention with render.

## Known Device Constraints (RG34XX)

- Avoid heavy use of `std::mutex` and `varargs` logging on hot paths.
- Prefer `pthread_mutex_t` and simple `log_str("...")` logs.
- SDL/GLES must use `SDL_WINDOW_FULLSCREEN_DESKTOP` and `#version 100`.
