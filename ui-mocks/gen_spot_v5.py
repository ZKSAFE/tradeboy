from PIL import Image, ImageDraw, ImageFont
import os


def load_font(path, size):
    try:
        return ImageFont.truetype(path, size=size)
    except Exception:
        return ImageFont.load_default()


def text_w(draw, font, s):
    b = draw.textbbox((0, 0), s, font=font)
    return b[2] - b[0]


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    out_path = os.path.join(root, "RPD", "spot_v5.png")

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

    font_path = os.path.join(root, "NotoSansCJK-Regular.ttc")
    f_header = load_font(font_path, 22)
    f_header_b = load_font(font_path, 22)
    f_list_sym = load_font(font_path, 22)
    f_list_val = load_font(font_path, 18)
    f_small = load_font(font_path, 16)

    pad = 14
    r = 16

    header_h = 56
    header = (pad, pad, W - pad, pad + header_h)
    d.rounded_rectangle(header, radius=r, fill=panel, outline=stroke, width=2)

    x0, y0, x1, y1 = header
    cy = (y0 + y1) // 2

    pair = "BTC/USDC"
    price = "68420.5"
    chg = "+1320.4 (+3.21%)"
    tf = "24H"

    left_x = x0 + 18
    d.text((left_x, cy - 12), pair, font=f_header, fill=text_main)

    price_x = x0 + 185
    d.text((price_x, cy - 12), price, font=f_header, fill=text_main)

    chg_x = x0 + 335
    d.text((chg_x, cy - 12), chg, font=f_header, fill=green)

    tf_w = text_w(d, f_header, tf)

    x_btn = 26
    x_btn_w = 34
    x_btn_h = 28
    x_btn_x1 = x1 - 18
    x_btn_x0 = x_btn_x1 - x_btn_w
    x_btn_y0 = cy - x_btn_h // 2
    x_btn_y1 = x_btn_y0 + x_btn_h

    tf_x = x_btn_x0 - 10 - tf_w
    d.text((tf_x, cy - 12), tf, font=f_header, fill=text_main)

    d.rounded_rectangle((x_btn_x0, x_btn_y0, x_btn_x1, x_btn_y1), radius=10, fill=btn_hover_bg, outline=None)
    d.text((x_btn_x0 + 12, cy - 12), "x", font=f_header, fill=btn_hover_text)

    body_top = header[3] + 12
    body_bottom = H - pad - 56

    left_w = 270
    left_panel = (pad, body_top, pad + left_w, body_bottom)
    d.rounded_rectangle(left_panel, radius=r, fill=panel, outline=stroke, width=2)

    chart_panel = (left_panel[2] + 12, body_top, W - pad, body_bottom)
    d.rounded_rectangle(chart_panel, radius=r, fill=panel, outline=stroke, width=2)

    sym_margin_lr = 22
    row_h = 48
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
    for i, (sym, p, pct, col) in enumerate(rows):
        d.text((lp0 + sym_margin_lr, y), sym, font=f_list_sym, fill=text_main)

        right_x = lp2 - sym_margin_lr
        pct_w = text_w(d, f_list_val, pct)
        p_w = text_w(d, f_list_val, p)
        gap = 10

        d.text((right_x - pct_w, y + 6), pct, font=f_list_val, fill=col)
        d.text((right_x - pct_w - gap - p_w, y + 6), p, font=f_list_val, fill=text_main)

        y += row_h

    cp0, cp1, cp2, cp3 = chart_panel
    gx0, gy0 = cp0 + 18, cp1 + 16
    gx1, gy1 = cp2 - 18, cp3 - 18

    d.rectangle((gx0, gy0, gx1, gy1), outline=(70, 70, 64), width=1)

    horiz = 5
    for i in range(1, horiz):
        yy = gy0 + (gy1 - gy0) * i // horiz
        d.line((gx0, yy, gx1, yy), fill=(70, 70, 64), width=1)

    vert = 4
    for i in range(1, vert):
        xx = gx0 + (gx1 - gx0) * i // vert
        d.line((xx, gy0, xx, gy1), fill=(70, 70, 64), width=1)

    price_labels = ["68510", "68352", "68194", "68037", "67879", "67721"]
    for i, s in enumerate(price_labels):
        yy = gy0 + (gy1 - gy0) * i // (len(price_labels) - 1)
        d.text((gx1 + 8, yy - 8), s, font=f_small, fill=text_dim)

    candles = [
        (0.18, 0.52, 0.58, 0.46, 0.34),
        (0.36, 0.56, 0.62, 0.50, 0.36),
        (0.56, 0.44, 0.52, 0.40, 0.30),
        (0.74, 0.30, 0.40, 0.22, 0.38),
    ]

    def cy_from(t):
        return int(gy0 + (gy1 - gy0) * t)

    for idx, (cx_t, open_t, close_t, high_t, low_t) in enumerate(candles):
        cx = int(gx0 + (gx1 - gx0) * cx_t)
        open_y = cy_from(open_t)
        close_y = cy_from(close_t)
        high_y = cy_from(high_t)
        low_y = cy_from(low_t)

        col = green if close_y < open_y else red

        d.line((cx, high_y, cx, low_y), fill=col, width=2)
        bw = 38
        x0b = cx - bw // 2
        x1b = cx + bw // 2
        y0b = min(open_y, close_y)
        y1b = max(open_y, close_y)
        d.rectangle((x0b, y0b, x1b, y1b), fill=col)

    d.text((gx1 - 40, gy0 + 10), "68434", font=f_small, fill=text_dim)
    d.text((gx1 - 40, gy0 + 18), "|", font=f_small, fill=text_dim)

    d.text((gx0 + 105, gy1 - 6), "67797", font=f_small, fill=text_dim)

    bottom = (pad, H - pad - 56, W - pad, H - pad)
    d.rounded_rectangle(bottom, radius=r, fill=panel, outline=stroke, width=2)

    bx0, by0, bx1, by1 = bottom
    bcy = (by0 + by1) // 2

    d.text((bx0 + 18, bcy - 12), "0.0500 BTC", font=f_header, fill=text_main)
    d.text((bx0 + 205, bcy - 12), "$3421.55", font=f_header, fill=text_main)
    d.text((bx0 + 345, bcy - 12), "+$65.2", font=f_header, fill=green)

    btn_h = x_btn_h
    btn_w = 86
    btn_y0 = bcy - btn_h // 2
    btn_y1 = btn_y0 + btn_h

    sell_w = btn_w
    buy_w = btn_w
    right_margin = 18

    sell_x1 = bx1 - right_margin
    sell_x0 = sell_x1 - sell_w
    buy_x1 = sell_x0
    buy_x0 = buy_x1 - buy_w

    d.rounded_rectangle((buy_x0, btn_y0, buy_x1, btn_y1), radius=10, fill=btn_hover_bg, outline=None)
    d.text((buy_x0 + 24, bcy - 12), "Buy", font=f_header_b, fill=btn_hover_text)

    d.text((sell_x0 + 20, bcy - 12), "Sell", font=f_header, fill=text_main)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    img.save(out_path)
    print(out_path)


if __name__ == "__main__":
    main()
