from PIL import Image, ImageDraw, ImageFont
import os, random

W, H = 720, 480


def textlength(draw, s, font):
    # Pillow compatibility across versions
    if hasattr(draw, "textlength"):
        return draw.textlength(s, font=font)
    w, _h = draw.textsize(s, font=font)
    return w


def get_font(ttc_path, size):
    try:
        return ImageFont.truetype(ttc_path, size=size)
    except Exception:
        return ImageFont.load_default()


def y_center_for_font(draw, cy, font):
    # Compute a y such that typical glyphs are vertically centered around cy.
    # Using a representative string avoids per-string vertical jitter and
    # fixes Truetype fonts appearing "lower" than the default bitmap font.
    sample = "Ag"
    b = draw.textbbox((0, 0), sample, font=font)
    h = b[3] - b[1]
    return int(cy - (h / 2.0) - b[1])


def y_center_for_text(draw, cy, s, font):
    b = draw.textbbox((0, 0), s, font=font)
    h = b[3] - b[1]
    return int(cy - (h / 2.0) - b[1])


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    out_dir = os.path.join(root, "RPD")
    os.makedirs(out_dir, exist_ok=True)

    # Transparent background
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Colors (alpha kept opaque for panels/widgets)
    bg = (38, 39, 35, 0)
    panel = (78, 77, 66, 255)
    panel2 = (92, 90, 78, 255)
    stroke = (125, 123, 106, 255)
    text = (245, 245, 240, 255)
    muted = (200, 200, 185, 255)
    green = (110, 215, 140, 255)
    red = (235, 110, 110, 255)
    grid = (60, 60, 54, 255)

    # Fill is transparent, kept for clarity
    d.rectangle((0, 0, W, H), fill=bg)

    ttc = os.path.join(root, "NotoSansCJK-Regular.ttc")
    font_header = get_font(ttc, 24)
    font_header_x = get_font(ttc, 16)
    font_cell = get_font(ttc, 18)
    font_pair = get_font(ttc, 20)
    font_small = get_font(ttc, 14)

    m = 18
    header_h = 56
    bottom_h = 66

    mid_top = m + header_h + 10
    mid_bottom = H - m - bottom_h - 10

    left_w = 260

    hdr = (m, m, W - m, m + header_h)
    d.rounded_rectangle(hdr, radius=14, fill=panel, outline=stroke, width=2)

    pad_l = 18
    pad_r = 18
    cy = (hdr[1] + hdr[3]) // 2

    pair = "BTC/USDC"
    price = "68420.5"
    chg_amt = "+1320.4"
    chg_pct = "(+3.21%)"
    chg_color = green if chg_amt.startswith("+") else red
    period = "24H"

    # Buy hover background color reused for X
    buy_bg = panel2

    # X button: slightly larger, background same as Buy
    x_r = 14
    x_cx = hdr[2] - pad_r - x_r
    x_cy = cy
    d.ellipse((x_cx - x_r, x_cy - x_r, x_cx + x_r, x_cy + x_r), fill=buy_bg)

    xw = textlength(d, "X", font_header_x)
    d.text((x_cx - xw / 2, x_cy - 8), "X", font=font_header_x, fill=text)

    # Right aligned period with symmetric padding
    period_w = textlength(d, period, font_header)
    period_x = x_cx - x_r - 10 - period_w

    header_y = y_center_for_font(d, cy, font_header)
    d.text((hdr[0] + pad_l, header_y), pair, font=font_header, fill=text)

    # Move price/change left
    price_x = hdr[0] + 180
    chg_x = hdr[0] + 310

    # Keep price and change on the same baseline
    d.text((price_x, header_y), price, font=font_header, fill=text)
    d.text((chg_x, header_y), f"{chg_amt} {chg_pct}", font=font_header, fill=chg_color)
    d.text((period_x, header_y), period, font=font_header, fill=text)

    # Left list panel
    lp = (m, mid_top, m + left_w, mid_bottom)
    d.rounded_rectangle(lp, radius=14, fill=panel, outline=stroke, width=2)

    # Right chart panel
    rp = (lp[2] + 12, mid_top, W - m, mid_bottom)
    d.rounded_rectangle(rp, radius=14, fill=panel, outline=stroke, width=2)

    # List items: keep both sides with equal blank gaps
    rows = [
        ("BTC", "68420.5", "+3.21%"),
        ("ETH", "3488.2", "-1.05%"),
        ("SOL", "124.15", "+0.87%"),
        ("BNB", "842.00", "-0.12%"),
        ("XRP", "1.860", "+2.34%"),
        ("TRX", "0.2843", "-0.44%"),
        ("DOGE", "0.1291", "+0.18%"),
    ]

    row_h = 42
    pad_lr = 22
    start_y = lp[1] + 16

    sym_x = lp[0] + pad_lr
    price_cell_r = lp[2] - pad_lr - 78
    chg_x2 = price_cell_r + 8

    for i, (sym, p, cp) in enumerate(rows):
        y = start_y + i * row_h
        if y + row_h > lp[3] - 10:
            break
        selected = i == 0
        if selected:
            d.rounded_rectangle((lp[0] + 10, y - 4, lp[2] - 10, y + row_h - 4), radius=10, fill=panel2)

        row_cy = y + (row_h // 2)
        sym_y = y_center_for_text(d, row_cy, sym, font_pair)
        d.text((sym_x, sym_y), sym, font=font_pair, fill=text)

        pw = textlength(d, p, font_cell)
        val_y = y_center_for_text(d, row_cy, p, font_cell)
        d.text((price_cell_r - pw, val_y), p, font=font_cell, fill=text)

        color = green if cp.startswith("+") else red
        pct_y = y_center_for_text(d, row_cy, cp, font_cell)
        d.text((chg_x2, pct_y), cp, font=font_cell, fill=color)

    # Chart: keep grid counts unchanged
    chart = (rp[0] + 16, rp[1] + 16, rp[2] - 56, rp[3] - 16)
    chart_w = chart[2] - chart[0]
    chart_h = chart[3] - chart[1]

    num_h = 6
    for j in range(num_h):
        y = chart[1] + int(chart_h * j / (num_h - 1))
        d.line((chart[0], y, chart[2], y), fill=grid, width=1)

    cell_h = chart_h / (num_h - 1)
    num_v = max(5, int(round(chart_w / cell_h)))
    num_v = min(num_v, 10)

    xs = [chart[0] + int(chart_w * j / (num_v - 1)) for j in range(num_v)]
    for x in xs:
        d.line((x, chart[1], x, chart[3]), fill=grid, width=1)

    # Candles: increase count while keeping grid unchanged
    random.seed(11)
    candle_count = max(16, (num_v - 2) * 3)

    base = 68000
    prices = []
    cur = base
    for _i in range(candle_count):
        o = cur
        hi = o + random.randint(60, 240)
        lo = o - random.randint(60, 240)
        c = lo + random.randint(0, max(1, hi - lo))
        cur = c
        prices.append((o, hi, lo, c))

    max_hi = max(p[1] for p in prices)
    min_lo = min(p[2] for p in prices)

    p_pad = (max_hi - min_lo) * 0.12 if max_hi > min_lo else 1
    pmax = max_hi + p_pad
    pmin = min_lo - p_pad

    def py(v):
        t = (v - pmin) / (pmax - pmin) if pmax != pmin else 0.5
        t = max(0.0, min(1.0, t))
        return int(chart[3] - t * chart_h)

    # Evenly space candles across chart, leaving borders empty
    left_pad = max(8, int((chart_w) * 0.04))
    right_pad = left_pad
    usable_w = max(1, chart_w - left_pad - right_pad)

    step = usable_w / (candle_count + 1)
    candle_w = max(6, int(step * 0.65))
    for idx, (o, hi, lo, c) in enumerate(prices, start=1):
        x = int(chart[0] + left_pad + step * idx)
        col = green if c >= o else red
        y_hi = max(chart[1] + 2, min(chart[3] - 2, py(hi)))
        y_lo = max(chart[1] + 2, min(chart[3] - 2, py(lo)))
        d.line((x, y_hi, x, y_lo), fill=col, width=2)
        y_o = py(o)
        y_c = py(c)
        top = max(chart[1] + 2, min(y_o, y_c))
        bot = min(chart[3] - 2, max(y_o, y_c))
        if bot - top < 4:
            bot = min(chart[3] - 2, top + 4)
        d.rectangle((x - candle_w // 2, top, x + candle_w // 2, bot), fill=col)

    # Right price labels on horizontal lines
    for j in range(num_h):
        y = chart[1] + int(chart_h * j / (num_h - 1))
        t = (chart[3] - y) / chart_h
        pv = pmin + t * (pmax - pmin)
        d.text((chart[2] + 8, y - 8), f"{pv:.0f}", font=font_small, fill=muted)

    # High/low annotations
    hi_idx = max(range(candle_count), key=lambda i: prices[i][1])
    lo_idx = min(range(candle_count), key=lambda i: prices[i][2])
    hi_val = prices[hi_idx][1]
    lo_val = prices[lo_idx][2]

    hi_x = int(chart[0] + left_pad + step * (hi_idx + 1))
    lo_x = int(chart[0] + left_pad + step * (lo_idx + 1))

    hi_y = max(chart[1] + 6, min(chart[3] - 24, py(hi_val)))
    lo_y = max(chart[1] + 10, min(chart[3] - 10, py(lo_val)))

    d.text((hi_x - 18, hi_y - 18), f"{hi_val:.0f}", font=font_small, fill=muted)
    d.text((lo_x - 18, lo_y + 6), f"{lo_val:.0f}", font=font_small, fill=muted)

    # Bottom panel
    bp = (m, H - m - bottom_h, W - m, H - m)
    d.rounded_rectangle(bp, radius=14, fill=panel, outline=stroke, width=2)

    hold = "0.0500 BTC"
    val = "$3421.55"
    pnl = "+$65.2"
    pnl_color = green

    bcy = (bp[1] + bp[3]) // 2
    bottom_y = y_center_for_font(d, bcy, font_header)

    d.text((bp[0] + 16, bottom_y), hold, font=font_header, fill=text)
    d.text((bp[0] + 210, bottom_y), val, font=font_header, fill=text)
    d.text((bp[0] + 360, bottom_y), pnl, font=font_header, fill=pnl_color)

    # Buttons: no gap between Buy and Sell
    btn_w, btn_h = 100, 44
    bx = bp[2] - 16 - btn_w * 2
    by = (bp[1] + bp[3]) // 2 - btn_h // 2

    d.rounded_rectangle((bx, by, bx + btn_w, by + btn_h), radius=12, fill=buy_bg)
    for dx, dy in [(0, 0), (1, 0)]:
        buy_y = y_center_for_text(d, by + (btn_h // 2), "Buy", font_header)
        d.text((bx + 26 + dx, buy_y + dy), "Buy", font=font_header, fill=text)

    sx = bx + btn_w
    sell_y = y_center_for_text(d, by + (btn_h // 2), "Sell", font_header)
    d.text((sx + 28, sell_y), "Sell", font=font_header, fill=text)

    out_path = os.path.join(out_dir, "spot_v7.png")
    img.save(out_path)
    print("Wrote", out_path)


if __name__ == "__main__":
    main()
