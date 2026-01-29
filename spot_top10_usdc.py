#!/usr/bin/env python3
import argparse
import json
import math
import ssl
import sys
import urllib.request

HL_INFO_URL = "https://api.hyperliquid.xyz/info"


# Official web UI (Spot -> USDC tab) ranking list as captured in the user's screenshots.
# This is a display-symbol order (BASE/USDC), deduplicated but preserving order.
WEB_USDC_RANKING = [
    "HYPE/USDC",
    "BTC/USDC",
    "USDH/USDC",
    "ETH/USDC",
    "SOL/USDC",
    "PUMP/USDC",
    "PURR/USDC",
    "USDT/USDC",
    "XAUT/USDC",
    "FARTCOIN/USDC",
    "USDE/USDC",
    "XPL/USDC",
    "FEUSD/USDC",
    "HFUN/USDC",
    "MON/USDC",
    "JEFF/USDC",
    "ENA/USDC",
    "SPX/USDC",
    "HAR/USDC",
    "PENGU/USDC",
    "2Z/USDC",
    "PIP/USDC",
    "USDHL/USDC",
    "ATEHUN/USDC",
    "CATBAL/USDC",
    "LIQD/USDC",
    "BONK/USDC",
    "SEDA/USDC",
    "BUDDY/USDC",
    "STABLE/USDC",
    "POINTS/USDC",
    "SCHIZO/USDC",
    "RUB/USDC",
    "MOVE/USDC",
    "OMNIX/USDC",
    "SOLV/USDC",
]


def build_ssl_context(*, insecure: bool) -> ssl.SSLContext:
    if insecure:
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        return ctx

    try:
        import certifi  # type: ignore

        return ssl.create_default_context(cafile=certifi.where())
    except Exception:
        # Fall back to system default CA bundle.
        return ssl.create_default_context()


def post_info(payload: dict, *, ssl_context: ssl.SSLContext):
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        HL_INFO_URL,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=20, context=ssl_context) as resp:
        return json.loads(resp.read().decode("utf-8"))


def fnum(x, digits=2):
    try:
        if x is None or (isinstance(x, float) and (math.isnan(x) or math.isinf(x))):
            return "-"
        return f"{x:,.{digits}f}"
    except Exception:
        return "-"


def infer_decimals_from_px_string(px: str) -> int:
    if not px or not isinstance(px, str):
        return 2
    if "e" in px or "E" in px:
        # Scientific notation is uncommon here; fall back.
        return 6
    if "." not in px:
        return 0
    return max(0, min(12, len(px.split(".", 1)[1])))


def fmt_signed(x: float, digits: int) -> str:
    if x is None:
        return "-"
    s = f"{abs(x):,.{digits}f}"
    return ("+" if x >= 0 else "-") + s


def pct(mid, prev):
    if prev is None or prev == 0 or mid is None:
        return None
    return (mid - prev) / prev * 100.0


def to_float(v):
    if v is None:
        return None
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        try:
            return float(v)
        except Exception:
            return None
    return None


def map_token_display_sym(token_name: str, token_full_name: str) -> str:
    # Website display-name overrides (derived from app.hyperliquid.xyz bundle mapping).
    overrides = {
        "UBTC": "BTC",
        "UETH": "ETH",
        "USOL": "SOL",
        "UPUMP": "PUMP",
        "UBONK": "BONK",
        "UMON": "MON",
        "MON": "MONPRO",
        "UFART": "FARTCOIN",
        "UXPL": "XPL",
        "UENA": "ENA",
        "HPENGU": "PENGU",
        "UDZ": "2Z",
        "FXRP": "XRP",
        "USDE": "USDE",
        "FEUSD": "FEUSD",
        "USDHL": "USDHL",
        "MMOVE": "MOVE",
        "USDT0": "USDT",
        "XAUT0": "XAUT",
        "LINK0": "LINK",
        "TRX0": "TRX",
        "AAVE0": "AAVE",
        "AVAX0": "AVAX",
        "PEPE0": "PEPE",
        "BNB1": "BNB",
        "XMR1": "XMR",
    }
    if token_name in overrides:
        return overrides[token_name]

    # The web UI disambiguates multiple spot tokens that all have l1Name == "USD".
    # Use token_full_name to pick a stable display symbol.
    if token_name == "USD" and token_full_name:
        fn = token_full_name.upper()
        # First USD in the web UI list is USDE.
        if "USDE" in fn or "ETHENA" in fn:
            return "USDE"
        # Second USD in the web UI list is FEUSD.
        if "FEUSD" in fn:
            return "FEUSD"
        # Third USD in the web UI list is USDHL (USDH Llama).
        if "USDH" in fn:
            return "USDHL"

    if token_full_name:
        prefix = "Unit "
        if token_full_name.startswith(prefix) and len(token_full_name) > len(prefix):
            base = token_full_name[len(prefix) :]
            if base == "Bitcoin":
                return "BTC"
            if base == "Ethereum":
                return "ETH"
            if base == "Solana":
                return "SOL"
            if base == "Pump Fun":
                return "PUMP"
            if base == "Bonk":
                return "BONK"

        def is_upper_alnum(c: str) -> bool:
            return ("A" <= c <= "Z") or ("0" <= c <= "9")

        # Prefer extracting ticker from $TICKER patterns.
        for i, ch in enumerate(token_full_name):
            if ch == "$" and i + 1 < len(token_full_name):
                j = i + 1
                while j < len(token_full_name) and is_upper_alnum(token_full_name[j]):
                    j += 1
                if j > i + 1:
                    t = token_full_name[i + 1 : j]
                    while t and t[-1].isdigit():
                        t = t[:-1]
                    if t:
                        return t

        # Otherwise use the first all-caps token >=3 chars.
        cur = ""
        for ch in token_full_name + " ":
            if is_upper_alnum(ch):
                cur += ch
            else:
                if len(cur) >= 3:
                    while cur and cur[-1].isdigit():
                        cur = cur[:-1]
                    if len(cur) >= 3:
                        return cur
                cur = ""

    return token_name


def main():
    ap = argparse.ArgumentParser(description="Hyperliquid Spot USDC Top10 by 24h volume")
    ap.add_argument("--top", type=int, default=10)
    ap.add_argument(
        "--find",
        type=str,
        default="",
        help="Find a specific base display symbol within the ranked USDC list (e.g. PURR).",
    )
    ap.add_argument(
        "--compare-web",
        action="store_true",
        help="Compare API-derived USDC ranking against the embedded web UI ranking list.",
    )
    ap.add_argument(
        "--print-web",
        action="store_true",
        help="Print markets strictly in the embedded web UI order (for 1:1 screenshot verification).",
    )
    ap.add_argument(
        "--web-mode",
        action="store_true",
        help="Apply additional filtering heuristics to better match the web UI list (market cap / ctx availability).",
    )
    ap.add_argument(
        "--insecure",
        action="store_true",
        help="Disable TLS certificate verification (NOT recommended).",
    )
    ap.add_argument(
        "--no-default-excludes",
        action="store_true",
        help="Disable default l1 token excludes (use raw ranking).",
    )
    ap.add_argument(
        "--exclude-l1",
        action="append",
        default=[],
        help="Exclude a base token l1Name from ranking (can be repeated).",
    )
    ap.add_argument(
        "--debug",
        action="store_true",
        help="Print extra raw fields (l1Name/fullName/index/isCanonical/coinKey) for reconciliation.",
    )
    args = ap.parse_args()

    if args.compare_web:
        # Comparison is intended to match the web list, so enable web-mode heuristics by default.
        args.web_mode = True

    if args.print_web:
        # Printing in web order should not depend on filtering heuristics.
        args.web_mode = False
        args.no_default_excludes = True

    default_excludes = {
        # These appeared in our ranking but are not shown in the web UI list the user shared.
        "XMR1",
        "NOCEX",
        "FXRP",
        "BZEC",
        "PUP",
        "AAVE0",
        "LINK0",
    }
    excludes = set(args.exclude_l1 or [])
    if not args.no_default_excludes:
        excludes |= default_excludes

    ssl_context = build_ssl_context(insecure=args.insecure)

    # spotMetaAndAssetCtxs returns: [ {tokens, universe}, [ {coin, midPx, prevDayPx, dayNtlVlm, ...}, ... ] ]
    try:
        meta_and_ctxs = post_info({"type": "spotMetaAndAssetCtxs"}, ssl_context=ssl_context)
    except Exception as e:
        msg = str(e)
        if "CERTIFICATE_VERIFY_FAILED" in msg and not args.insecure:
            print("TLS certificate verification failed.", file=sys.stderr)
            print("Fix options:", file=sys.stderr)
            print("  1) Install certifi: pip3 install certifi", file=sys.stderr)
            print("  2) Or rerun with:   python3 spot_top10_usdc.py --insecure  (NOT recommended)", file=sys.stderr)
        raise

    spot_meta = meta_and_ctxs[0]
    ctxs = meta_and_ctxs[1]

    tokens = spot_meta["tokens"]
    universe = spot_meta["universe"]

    # token index -> (l1Name, fullName)
    token_by_index = {}
    for t in tokens:
        idx = t.get("index")
        nm = t.get("name") or ""
        fn = t.get("fullName") or ""
        if idx is not None:
            token_by_index[idx] = (nm, fn)

    usdc_index = None
    for idx, (nm, _fn) in token_by_index.items():
        if nm == "USDC":
            usdc_index = idx
            break
    if usdc_index is None:
        raise RuntimeError("USDC token not found in spotMeta tokens[]")

    # ctx map key is coin string:
    # - canonical pairs: coin == base symbol (e.g. "BTC")
    # - non-canonical: coin == "@<universe.index>" (e.g. "@243")
    ctx_by_coin = {c.get("coin"): c for c in ctxs if isinstance(c, dict) and c.get("coin")}

    rows = []
    for u in universe:
        tokens_pair = u.get("tokens")  # [baseTokenIndex, quoteTokenIndex]
        if not isinstance(tokens_pair, list) or len(tokens_pair) != 2:
            continue
        base_idx, quote_idx = tokens_pair
        if quote_idx != usdc_index:
            continue

        raw_pair_name = u.get("name")  # e.g. "BTC/USDC" or "@.../USDC" style
        is_canonical = bool(u.get("isCanonical"))
        u_index = u.get("index")

        # coin key used by ctxs
        if not raw_pair_name:
            continue
        primary_coin_key = raw_pair_name.split("/")[0] if is_canonical else f"@{u_index}"
        alt_coin_key = f"@{u_index}" if u_index is not None else ""
        pair_coin_key = raw_pair_name

        tried_coin_keys = [primary_coin_key]
        if alt_coin_key and alt_coin_key != primary_coin_key:
            tried_coin_keys.append(alt_coin_key)
        if pair_coin_key and pair_coin_key not in tried_coin_keys:
            tried_coin_keys.append(pair_coin_key)

        ctx = ctx_by_coin.get(primary_coin_key)
        used_coin_key = primary_coin_key
        ctx_found = ctx is not None and isinstance(ctx, dict)
        if (not ctx_found) and alt_coin_key:
            alt = ctx_by_coin.get(alt_coin_key)
            if alt is not None and isinstance(alt, dict):
                ctx = alt
                used_coin_key = alt_coin_key
                ctx_found = True
        if (not ctx_found) and pair_coin_key:
            alt = ctx_by_coin.get(pair_coin_key)
            if alt is not None and isinstance(alt, dict):
                ctx = alt
                used_coin_key = pair_coin_key
                ctx_found = True
        if not ctx_found:
            ctx = {}

        mid_s = ctx.get("midPx")
        prev_s = ctx.get("prevDayPx")
        circ_s = ctx.get("circulatingSupply")
        mid = to_float(mid_s)
        prev = to_float(prev_s)
        vol = to_float(ctx.get("dayNtlVlm"))
        circ = to_float(circ_s)

        mcap = None
        if mid is not None and circ is not None:
            mcap = mid * circ

        change_pct = pct(mid, prev)
        change_abs = (mid - prev) if (mid is not None and prev is not None) else None
        px_decimals = infer_decimals_from_px_string(mid_s) if isinstance(mid_s, str) else 6

        base_l1, base_full = token_by_index.get(base_idx, ("", ""))
        base_disp = map_token_display_sym(base_l1 or "", base_full or "")
        pair_disp = f"{base_disp}/USDC" if base_disp else raw_pair_name

        if base_l1 in excludes:
            continue

        if args.web_mode:
            # Web list seems to only include markets with usable ctx and (usually) a market cap.
            # Keep a small allow-list for stablecoins that show '--' market cap on the website.
            stable_allow = {"USDH/USDC", "USDT/USDC", "USDE/USDC", "FEUSD/USDC", "USDHL/USDC"}
            if not ctx_found:
                continue
            if mid is None:
                continue
            if pair_disp not in stable_allow:
                if circ is None or circ <= 0:
                    continue

        rows.append(
            {
                "pair": pair_disp,
                "price": mid,
                "priceStr": mid_s,
                "pxDecimals": px_decimals,
                "changeAbs": change_abs,
                "changePct": change_pct,
                "volume": vol,
                "coinKey": used_coin_key,
                "ctxFound": ctx_found,
                "ctxTried": tried_coin_keys,
                "circulatingSupply": circ,
                "marketCap": mcap,
                "baseL1": base_l1,
                "baseFull": base_full,
                "uIndex": u_index,
                "isCanonical": is_canonical,
            }
        )

    rows.sort(key=lambda r: (r["volume"] or 0.0), reverse=True)
    find_key = (args.find or "").strip().upper()

    if args.print_web:
        by_pair = {}
        for r in rows:
            p = r.get("pair")
            if isinstance(p, str) and p and p.endswith("/USDC"):
                # First occurrence is fine.
                if p not in by_pair:
                    by_pair[p] = r

        print(f"Web order list ({len(WEB_USDC_RANKING)} markets)\n")
        print(f"{'SYMBOL':<16} {'LAST PRICE':>14} {'24H CHANGE':>22} {'VOLUME':>14}")
        print("-" * 72)

        missing = []
        for p in WEB_USDC_RANKING:
            r = by_pair.get(p)
            if not r:
                missing.append(p)
                print(f"{p:<16} {'-':>14} {'-':>22} {'-':>14}")
                continue

            px_decimals = int(r.get("pxDecimals") or 6)
            price_str = fnum(r["price"], px_decimals) if r.get("price") is not None else "-"
            if r.get("changeAbs") is None or r.get("changePct") is None:
                change_str = "-"
            else:
                chg_abs = fmt_signed(r["changeAbs"], px_decimals)
                chg_pct = fmt_signed(r["changePct"], 2) + "%"
                change_str = f"{chg_abs} / {chg_pct}"
            vol_str = f"${fnum(r.get('volume'), 0)}" if r.get("volume") is not None else "-"
            print(f"{p:<16} {price_str:>14} {change_str:>22} {vol_str:>14}")

        if missing:
            print("\nMissing rows (not found in API-derived list by display symbol):")
            for p in missing:
                print("  ", p)
        return

    if args.compare_web:
        # Compare against embedded web ranking.
        expected = WEB_USDC_RANKING
        expected_set = set(expected)

        got = [r.get("pair") for r in rows if isinstance(r.get("pair"), str) and r.get("pair")]
        # Keep first occurrence (stable sort keeps order anyway) and ensure it ends with /USDC.
        seen = set()
        got_norm = []
        for p in got:
            if not p.endswith("/USDC"):
                continue
            if p in seen:
                continue
            seen.add(p)
            got_norm.append(p)

        got_set = set(got_norm)

        missing = [p for p in expected if p not in got_set]
        extra = [p for p in got_norm if p not in expected_set]

        print("Web ranking length:", len(expected))
        print("API ranking length:", len(got_norm))
        print()

        if missing:
            print("Missing from API-derived list:")
            for p in missing:
                print("  ", p)
            print()

        if extra:
            print("Extra (present in API list but not in web list):")
            for p in extra[:100]:
                print("  ", p)
            if len(extra) > 100:
                print("  ...", len(extra) - 100, "more")
            print()

        # Order mismatches: compare positions for common items.
        exp_pos = {p: i for i, p in enumerate(expected)}
        got_pos = {p: i for i, p in enumerate(got_norm)}
        mismatches = []
        for p in expected:
            if p in got_pos:
                if got_pos[p] != exp_pos[p]:
                    mismatches.append((p, exp_pos[p] + 1, got_pos[p] + 1))
        mismatches.sort(key=lambda x: abs(x[1] - x[2]), reverse=True)

        print("Order mismatches (expected_rank -> api_rank):")
        for p, epos, gpos in mismatches[:50]:
            print(f"  {p:<12} {epos:>3} -> {gpos:<3}")
        if len(mismatches) > 50:
            print("  ...", len(mismatches) - 50, "more")
        print()

        # Show the first N items side-by-side for visual diff.
        n = max(len(expected), 0)
        n = min(n, 60)
        print("Top rows (web vs api):")
        for i in range(n):
            w = expected[i] if i < len(expected) else ""
            a = got_norm[i] if i < len(got_norm) else ""
            mark = "==" if w == a and w else "!!"
            print(f"#{i+1:02d} {mark} web={w:<12}  api={a}")
        return

    if find_key:
        # Search the full ranked list for the first matching base display symbol.
        found_idx = -1
        found = None
        for i, r in enumerate(rows):
            base = (r.get("pair", "").split("/", 1)[0] or "").upper()
            if base == find_key:
                found_idx = i
                found = r
                break
        if found is None:
            print(f"Not found in USDC-quoted universe: {find_key}")
            return
        print(f"Found {find_key}/USDC at rank #{found_idx+1} by dayNtlVlm")
        print(
            f"pair={found.get('pair')} volume=${fnum(found.get('volume'),0)} price={found.get('priceStr')} "
            f"l1={found.get('baseL1')} full={found.get('baseFull')} uIndex={found.get('uIndex')} canon={found.get('isCanonical')} coinKey={found.get('coinKey')}"
        )
        print(
            f"ctxFound={found.get('ctxFound')} ctxTried={found.get('ctxTried')}"
        )

        # Probe ctx keys to see if there is any alternate coin naming.
        ctx_hits = []
        for k in ctx_by_coin.keys():
            if isinstance(k, str) and find_key in k.upper():
                ctx_hits.append(k)
        if ctx_hits:
            ctx_hits = sorted(ctx_hits)[:50]
            print(f"ctx coin keys containing '{find_key}': {', '.join(ctx_hits)}")
        else:
            print(f"No ctx coin keys contain '{find_key}'")
        # Also show a small window around it for context.
        lo = max(0, found_idx - 2)
        hi = min(len(rows), found_idx + 3)
        print("\nContext:")
        for j in range(lo, hi):
            r = rows[j]
            print(f"#{j+1:<4} {r.get('pair',''):<16} ${fnum(r.get('volume'),0):>12}")
        return

    top = rows[: max(1, int(args.top))]

    print(f"Top {len(top)} Spot (USDC) by 24h Volume (dayNtlVlm)\n")
    if args.debug:
        print(
            f"{'SYMBOL':<16} {'LAST PRICE':>14} {'24H CHANGE':>22} {'VOLUME':>14}  RAW"
        )
        print("-" * 120)
    else:
        print(f"{'SYMBOL':<16} {'LAST PRICE':>14} {'24H CHANGE':>22} {'VOLUME':>14}")
        print("-" * 72)
    for r in top:
        px_decimals = int(r.get("pxDecimals") or 6)
        price_str = fnum(r["price"], px_decimals) if r["price"] is not None else "-"
        if r.get("changeAbs") is None or r.get("changePct") is None:
            change_str = "-"
        else:
            chg_abs = fmt_signed(r["changeAbs"], px_decimals)
            chg_pct = fmt_signed(r["changePct"], 2) + "%"
            change_str = f"{chg_abs} / {chg_pct}"
        vol_str = f"${fnum(r['volume'], 0)}" if r.get("volume") is not None else "-"
        if args.debug:
            raw = (
                f"l1={r.get('baseL1','')}"
                f" full={r.get('baseFull','')}"
                f" uIndex={r.get('uIndex','')}"
                f" canon={r.get('isCanonical', False)}"
                f" coinKey={r.get('coinKey','')}"
            )
            print(f"{r['pair']:<16} {price_str:>14} {change_str:>22} {vol_str:>14}  {raw}")
        else:
            print(f"{r['pair']:<16} {price_str:>14} {change_str:>22} {vol_str:>14}")


if __name__ == "__main__":
    main()
