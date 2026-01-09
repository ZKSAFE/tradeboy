from PIL import Image, ImageDraw, ImageFont
import os
import math


def text_w(draw, font, s):
    # Pillow compatibility: ImageFont.load_default() may not implement getbbox()
    w, _h = draw.textsize(s, font=font)
    return w


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    out_path = os.path.join(root, "RPD", "spot_v6.png")

    W, H = 720, 480

    bg = (50, 50, 47)
    panel = (79, 78, 69)
    stroke = (150, 148, 130)

    text_main = (235, 234, 220)
    text_dim = (200, 199, 186)

    green = (118, 214, 134)
    red = (230, 110, 100)

    btn_hover_bg = (104, 102, 92)
    btn_hover_text = (248, 247, 238)

    img = Image.new("RGB", (W, H), bg)
    d = ImageDraw.Draw(img)

    # Keep the same retro/pixel look as existing v3/v4 mocks (default bitmap font)
    f = ImageFont.load_default()

    pad = 14
    r = 16

    # Header
    header_h = 56
    header = (pad, pad, W - pad, pad + header_h)
    d.rounded_rectangle(header, radius=r, fill=panel, outline=stroke, width=2)

    x0, y0, x1, y1 = header
    cy = (y0 + y1) // 2

    # Text positions roughly match v4
    d.text((x0 + 18, cy - 8), "BTC/USDC", font=f, fill=text_main)
    d.text((x0 + 185, cy - 8), "68420.5", font=f, fill=text_main)
    d.text((x0 + 335, cy - 8), "+1320.4 (+3.21%)", font=f, fill=green)

    # Right side: 24H + circular X button
    d.text((x1 - 78, cy - 8), "24H", font=f, fill=text_main)

    x_btn_d = 18  # v4-like small circle
    x_btn_cx = x1 - 28
    x_btn_cy = cy
    d.ellipse(
        (x_btn_cx - x_btn_d // 2, x_btn_cy - x_btn_d // 2, x_btn_cx + x_btn_d // 2, x_btn_cy + x_btn_d // 2),
        fill=btn_hover_bg,
        outline=None,
    )
    d.text((x_btn_cx - 4, x_btn_cy - 8), "x", font=f, fill=btn_hover_text)

    # Middle panels
    body_top = header[3] + 12
    body_bottom = H - pad - 56

    left_w = 270
    left_panel = (pad, body_top, pad + left_w, body_bottom)
    d.rounded_rectangle(left_panel, radius=r, fill=panel, outline=stroke, width=2)

    chart_panel = (left_panel[2] + 12, body_top, W - pad, body_bottom)
    d.rounded_rectangle(chart_panel, radius=r, fill=panel, outline=stroke, width=2)

    # Left list (keep v4 style)
    rows = [
        ("BTC", "68420.5", "+3.21%", green),
        ("ETH", "3488.2", "-1.05%", red),
        ("SOL", "124.15", "+0.87%", green),
        ("BNB", "842.00", "-0.12%", red),
        ("XRP", "1.860", "+2.34%", green),
        ("TRX", "0.2843", "-0.44%", red),
    ]

    lp0, lp1, lp2, lp3 = left_panel
    y = lp1 + 18
    row_h = 44
    margin_lr = 18
    for sym, p, pct, col in rows:
        d.text((lp0 + margin_lr, y), sym, font=f, fill=text_main)

        # Right-aligned: price then pct
        right_x = lp2 - margin_lr
        pct_w = text_w(d, f, pct)
        p_w = text_w(d, f, p)
        gap = 10

        d.text((right_x - pct_w, y), pct, font=f, fill=col)
        d.text((right_x - pct_w - gap - p_w, y), p, font=f, fill=text_main)
        y += row_h

    # Chart area inner rect
    cp0, cp1, cp2, cp3 = chart_panel
    gx0, gy0 = cp0 + 18, cp1 + 16
    gx1, gy1 = cp2 - 18, cp3 - 18

    grid_col = (70, 70, 64)
    d.rectangle((gx0, gy0, gx1, gy1), outline=grid_col, width=1)

    # Horizontal grid lines (v4-like)
    horiz = 4
    for i in range(1, horiz):
        yy = gy0 + (gy1 - gy0) * i // horiz
        d.line((gx0, yy, gx1, yy), fill=grid_col, width=1)

    # Vertical grid lines: v4-like (few)
    vert = 4
    for i in range(1, vert):
        xx = gx0 + (gx1 - gx0) * i // vert
        d.line((xx, gy0, xx, gy1), fill=grid_col, width=1)

    # Right-side price labels (v4 numbers)
    labels = ["68510", "68352", "68194", "68037", "67879", "67721"]
    for i, s in enumerate(labels):
        yy = gy0 + (gy1 - gy0) * i // (len(labels) - 1)
        d.text((gx1 + 8, yy - 6), s, font=f, fill=text_dim)

    # Candles: match v3 density (more candles), but keep grid lines sparse (v4).
    candle_count = 22

    def y_from(v):
        return int(gy0 + (gy1 - gy0) * v)

    # Create a smooth upward-ish series reminiscent of v3.
    for i in range(candle_count):
        t = i / (candle_count - 1)
        cx = int(gx0 + (gx1 - gx0) * (0.05 + 0.90 * t))

        # Base curve and small oscillation
        base = 0.62 - 0.36 * t
        osc = 0.04 * math.sin(t * 6.0 * math.pi)
        open_t = base + osc + 0.03 * math.sin(t * 2.0 * math.pi)
        close_t = base + osc - 0.03 * math.sin(t * 2.0 * math.pi)

        high_t = min(open_t, close_t) - 0.10 - 0.03 * math.sin(t * 3.0 * math.pi)
        low_t = max(open_t, close_t) + 0.10 + 0.03 * math.sin(t * 3.0 * math.pi)

        # Clamp into chart inner area
        open_t = max(0.08, min(0.92, open_t))
        close_t = max(0.08, min(0.92, close_t))
        high_t = max(0.06, min(0.94, high_t))
        low_t = max(0.06, min(0.94, low_t))

        open_y = y_from(open_t)
        close_y = y_from(close_t)
        high_y = y_from(high_t)
        low_y = y_from(low_t)

        col = green if close_y < open_y else red

        d.line((cx, high_y, cx, low_y), fill=col, width=2)
        bw = 10
        x0b = cx - bw // 2
        x1b = cx + bw // 2
        y0b = min(open_y, close_y)
        y1b = max(open_y, close_y)
        d.rectangle((x0b, y0b, x1b, y1b), fill=col)

    # High/low annotations (keep v4 positions/text)
    d.text((gx1 - 40, gy0 + 10), "68434", font=f, fill=text_dim)
    d.text((gx0 + 105, gy1 - 10), "67797", font=f, fill=text_dim)

    # Bottom bar (keep v4 layout)
    bottom = (pad, H - pad - 56, W - pad, H - pad)
    d.rounded_rectangle(bottom, radius=r, fill=panel, outline=stroke, width=2)

    bx0, by0, bx1, by1 = bottom
    bcy = (by0 + by1) // 2

    d.text((bx0 + 18, bcy - 8), "0.0500 BTC", font=f, fill=text_main)
    d.text((bx0 + 205, bcy - 8), "$3421.55", font=f, fill=text_main)
    d.text((bx0 + 345, bcy - 8), "+$65.2", font=f, fill=green)

    # Buy hover pill + Sell text, as in v4
    buy_x0, buy_y0, buy_x1, buy_y1 = bx1 - 170, bcy - 14, bx1 - 90, bcy + 14
    d.rounded_rectangle((buy_x0, buy_y0, buy_x1, buy_y1), radius=12, fill=btn_hover_bg, outline=None)
    d.text((buy_x0 + 24, bcy - 8), "Buy", font=f, fill=btn_hover_text)
    d.text((bx1 - 60, bcy - 8), "Sell", font=f, fill=text_main)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    img.save(out_path)
    print(out_path)


if __name__ == "__main__":
    main()
