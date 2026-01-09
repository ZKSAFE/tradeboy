from PIL import Image, ImageDraw, ImageFont
import os, random

W, H = 720, 480
out_dir = os.path.join('tradeboy', 'RPD')
os.makedirs(out_dir, exist_ok=True)

bg = (38, 39, 35)
panel = (78, 77, 66)
panel2 = (92, 90, 78)
stroke = (125, 123, 106)
text = (245, 245, 240)
muted = (200, 200, 185)
green = (110, 215, 140)
red = (235, 110, 110)
grid = (60, 60, 54)


def get_font(size):
    candidates = [
        '/System/Library/Fonts/Supplemental/Menlo.ttc',
        '/System/Library/Fonts/Supplemental/Courier New.ttf',
        '/System/Library/Fonts/Supplemental/Arial Unicode.ttf',
    ]
    for p in candidates:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size=size)
            except Exception:
                pass
    return ImageFont.load_default()

font_header = get_font(24)
font_header_x = get_font(16)
font_cell = get_font(18)
font_pair = get_font(20)
font_small = get_font(14)

img = Image.new('RGB', (W, H), bg)
d = ImageDraw.Draw(img)

m = 18
header_h = 56
bottom_h = 66

mid_top = m + header_h + 10
mid_bottom = H - m - bottom_h - 10

left_w = 260

hdr = (m, m, W-m, m+header_h)
d.rounded_rectangle(hdr, radius=14, fill=panel, outline=stroke, width=2)

pad_l = 18
pad_r = 18
cy = (hdr[1]+hdr[3])//2

pair = 'BTC/USDC'
price = '68420.5'
chg_amt = '+1320.4'
chg_pct = '(+3.21%)'
chg_color = green if chg_amt.startswith('+') else red
period = '24H'

# Smaller solid X, no border
x_r = 11
x_cx = hdr[2] - pad_r - x_r
x_cy = cy
x_fill = (70, 70, 62)
d.ellipse((x_cx-x_r, x_cy-x_r, x_cx+x_r, x_cy+x_r), fill=x_fill)
# X glyph smaller
xw = d.textlength('X', font=font_header_x)
d.text((x_cx - xw/2, x_cy-8), 'X', font=font_header_x, fill=text)

# Right aligned period with symmetric padding
period_w = d.textlength(period, font=font_header)
period_x = x_cx - x_r - 10 - period_w

d.text((hdr[0]+pad_l, cy-12), pair, font=font_header, fill=text)

# Move price/change left
price_x = hdr[0] + 180
chg_x = hdr[0] + 310

d.text((price_x, cy-12), price, font=font_header, fill=text)
d.text((chg_x, cy-14), f"{chg_amt} {chg_pct}", font=font_header, fill=chg_color)
d.text((period_x, cy-12), period, font=font_header, fill=text)

# Left list panel
lp = (m, mid_top, m+left_w, mid_bottom)
d.rounded_rectangle(lp, radius=14, fill=panel, outline=stroke, width=2)

# Right chart panel
rp = (lp[2]+12, mid_top, W-m, mid_bottom)
d.rounded_rectangle(rp, radius=14, fill=panel, outline=stroke, width=2)

# List items: bigger symbol, both sides more centered with padding
rows = [
    ('BTC', '68420.5', '+3.21%'),
    ('ETH', '3488.2', '-1.05%'),
    ('SOL', '124.15', '+0.87%'),
    ('BNB', '842.00', '-0.12%'),
    ('XRP', '1.860', '+2.34%'),
    ('TRX', '0.2843', '-0.44%'),
    ('DOGE','0.1291', '+0.18%'),
]

row_h = 42
pad_lr = 18
start_y = lp[1]+16

sym_x = lp[0] + pad_lr
price_cell_l = lp[0] + 90
price_cell_r = lp[2] - pad_lr - 78
chg_x = price_cell_r + 8

for i,(sym,p,cp) in enumerate(rows):
    y = start_y + i*row_h
    if y+row_h > lp[3]-10:
        break
    selected = (i==0)
    if selected:
        d.rounded_rectangle((lp[0]+10, y-4, lp[2]-10, y+row_h-4), radius=10, fill=panel2)

    d.text((sym_x, y+5), sym, font=font_pair, fill=text)

    pw = d.textlength(p, font=font_cell)
    d.text((price_cell_r - pw, y+7), p, font=font_cell, fill=text)

    color = green if cp.startswith('+') else red
    d.text((chg_x, y+7), cp, font=font_cell, fill=color)

# Chart: fewer vertical lines; make cells near square
chart = (rp[0]+16, rp[1]+16, rp[2]-56, rp[3]-16)
chart_w = chart[2]-chart[0]
chart_h = chart[3]-chart[1]

num_h = 6
for j in range(num_h):
    y = chart[1] + int(chart_h * j/(num_h-1))
    d.line((chart[0], y, chart[2], y), fill=grid, width=1)

# Choose vertical cells close to square (use horizontal cell height)
cell_h = chart_h/(num_h-1)
num_v = max(5, int(round(chart_w/cell_h)))
# ensure odd-ish and not too many
num_v = min(num_v, 10)

xs = [chart[0] + int(chart_w * j/(num_v-1)) for j in range(num_v)]
for x in xs:
    d.line((x, chart[1], x, chart[3]), fill=grid, width=1)

# Candles aligned to inner vertical lines, leave border columns empty
random.seed(11)
N = num_v - 2
base = 68000
prices = []
cur = base
for i in range(N):
    o = cur
    hi = o + random.randint(60, 240)
    lo = o - random.randint(60, 240)
    c = lo + random.randint(0, max(1, hi-lo))
    cur = c
    prices.append((o,hi,lo,c))

max_hi = max(p[1] for p in prices)
min_lo = min(p[2] for p in prices)

# Add padding so high/low not touching top/bottom
p_pad = (max_hi-min_lo)*0.12 if max_hi>min_lo else 1
pmax = max_hi + p_pad
pmin = min_lo - p_pad

def py(v):
    t = (v - pmin)/(pmax-pmin) if pmax!=pmin else 0.5
    t = max(0.0, min(1.0, t))
    return int(chart[3] - t*chart_h)

# candle width based on distance between xs
step = xs[1]-xs[0] if len(xs)>1 else 20
candle_w = max(8, int(step*0.55))
for idx,(o,hi,lo,c) in enumerate(prices, start=1):
    x = xs[idx]
    col = green if c>=o else red
    y_hi = max(chart[1]+2, min(chart[3]-2, py(hi)))
    y_lo = max(chart[1]+2, min(chart[3]-2, py(lo)))
    d.line((x, y_hi, x, y_lo), fill=col, width=2)
    y_o = py(o)
    y_c = py(c)
    top = max(chart[1]+2, min(y_o,y_c))
    bot = min(chart[3]-2, max(y_o,y_c))
    if bot-top < 4:
        bot = min(chart[3]-2, top+4)
    d.rectangle((x-candle_w//2, top, x+candle_w//2, bot), fill=col)

# Right price labels on horizontal lines
for j in range(num_h):
    y = chart[1] + int(chart_h * j/(num_h-1))
    t = (chart[3]-y)/chart_h
    pv = pmin + t*(pmax-pmin)
    d.text((chart[2]+8, y-8), f"{pv:.0f}", font=font_small, fill=muted)

# high/low annotations with margin
hi_idx = max(range(N), key=lambda i: prices[i][1])
lo_idx = min(range(N), key=lambda i: prices[i][2])
hi_val = prices[hi_idx][1]
lo_val = prices[lo_idx][2]
hi_x = xs[hi_idx+1]
lo_x = xs[lo_idx+1]
hi_y = max(chart[1]+6, min(chart[3]-24, py(hi_val)))
lo_y = max(chart[1]+10, min(chart[3]-10, py(lo_val)))
d.text((hi_x-18, hi_y-18), f"{hi_val:.0f}", font=font_small, fill=muted)
d.text((lo_x-18, lo_y+6), f"{lo_val:.0f}", font=font_small, fill=muted)

# Bottom panel
bp = (m, H-m-bottom_h, W-m, H-m)
d.rounded_rectangle(bp, radius=14, fill=panel, outline=stroke, width=2)

# Bottom text same size as header
hold = '0.0500 BTC'
val = '$3421.55'
pnl = '+$65.2'
pnl_color = green

y = (bp[1]+bp[3])//2 - 12

d.text((bp[0]+16, y), hold, font=font_header, fill=text)
d.text((bp[0]+210, y), val, font=font_header, fill=text)
d.text((bp[0]+360, y), pnl, font=font_header, fill=pnl_color)

# Buttons: default hover on Buy (background, bold-ish simulated by double draw)
btn_w, btn_h = 100, 44
bx = bp[2]-16 - btn_w*2 - 16
by = (bp[1]+bp[3])//2 - btn_h//2

# Buy hover: background, no border
buy_bg = panel2
d.rounded_rectangle((bx, by, bx+btn_w, by+btn_h), radius=12, fill=buy_bg)
# pseudo-bold
for dx,dy in [(0,0),(1,0)]:
    d.text((bx+26+dx, by+10+dy), 'Buy', font=font_header, fill=text)

# Sell normal: text only
sx = bx + btn_w + 16
d.text((sx+28, by+10), 'Sell', font=font_header, fill=text)

out_path = os.path.join(out_dir, 'spot_v4.png')
img.save(out_path)
print('Wrote', out_path)