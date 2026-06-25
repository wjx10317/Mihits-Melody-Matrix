"""生成 Melody Matrix 游戏所需的 PNG 资源图片（带 alpha 通道）"""
from PIL import Image, ImageDraw, ImageFilter, ImageChops
import math
import os

RES_DIR = os.path.join(os.path.dirname(__file__), "res")
os.makedirs(RES_DIR, exist_ok=True)


# ============================================================
# 1. 背景图片：黑色深框 + 白色圆角填充，256x256
# ============================================================
def gen_background():
    size = 256
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.rectangle([0, 0, size - 1, size - 1], fill=(0, 0, 0, 255))
    border = 12
    radius = 20
    margin = border
    draw.rounded_rectangle(
        [margin, margin, size - 1 - margin, size - 1 - margin],
        radius=radius,
        fill=(255, 255, 255, 255),
    )
    path = os.path.join(RES_DIR, "background.png")
    img.save(path, "PNG")
    print(f"[OK] {path}  ({size}x{size})")


# ============================================================
# 2. Tap：灰度渐变圆角方块 + 轻微发光描边，边缘柔和过渡
# ============================================================
def gen_tap():
    size = 128
    margin = 8
    radius = 18
    x0, y0 = margin, margin
    x1, y1 = size - 1 - margin, size - 1 - margin
    cx, cy = size / 2, size / 2

    # 灰度径向渐变（中心亮 → 边缘暗）
    gradient = Image.new("L", (size, size), 0)
    max_dist = math.sqrt(cx * cx + cy * cy)
    for y in range(size):
        for x in range(size):
            d = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
            val = int(220 - 120 * (d / max_dist))
            val = max(100, min(220, val))
            gradient.putpixel((x, y), val)

    # 圆角 mask（带柔和边缘）
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([x0, y0, x1, y1], radius=radius, fill=255)
    mask_soft = mask.filter(ImageFilter.GaussianBlur(radius=2))

    # 主体
    body = Image.composite(gradient, Image.new("L", (size, size), 0), mask_soft)

    # 外发光描边：直接画 outline，然后模糊
    glow = Image.new("L", (size, size), 0)
    glow_draw = ImageDraw.Draw(glow)
    glow_draw.rounded_rectangle(
        [x0, y0, x1, y1], radius=radius, outline=200, width=3
    )
    glow = glow.filter(ImageFilter.GaussianBlur(radius=4))

    # 合成
    rgba = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    # 发光层（白色，alpha 来自 glow outline）
    glow_layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for y in range(size):
        for x in range(size):
            ea = glow.getpixel((x, y))
            if ea > 3:
                glow_layer.putpixel((x, y), (255, 255, 255, min(180, ea)))
    rgba = Image.alpha_composite(rgba, glow_layer)

    # 主体层
    body_rgba = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for y in range(size):
        for x in range(size):
            v = body.getpixel((x, y))
            a = mask_soft.getpixel((x, y))
            if a > 3:
                body_rgba.putpixel((x, y), (v, v, v, a))
    rgba = Image.alpha_composite(rgba, body_rgba)

    path = os.path.join(RES_DIR, "tap.png")
    rgba.save(path, "PNG")
    print(f"[OK] {path}  ({size}x{size})")


# ============================================================
# 3. Slider：与 tap 接近，中心有偏小的非透明白色圆
# ============================================================
def gen_slider():
    size = 128
    margin = 8
    radius = 18
    x0, y0 = margin, margin
    x1, y1 = size - 1 - margin, size - 1 - margin
    cx, cy = size / 2, size / 2

    gradient = Image.new("L", (size, size), 0)
    max_dist = math.sqrt(cx * cx + cy * cy)
    for y in range(size):
        for x in range(size):
            d = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
            val = int(220 - 120 * (d / max_dist))
            val = max(100, min(220, val))
            gradient.putpixel((x, y), val)

    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([x0, y0, x1, y1], radius=radius, fill=255)
    mask_soft = mask.filter(ImageFilter.GaussianBlur(radius=2))

    body = Image.composite(gradient, Image.new("L", (size, size), 0), mask_soft)

    # 外发光（直接画 outline 再模糊，与 tap 一致）
    glow = Image.new("L", (size, size), 0)
    glow_draw = ImageDraw.Draw(glow)
    glow_draw.rounded_rectangle(
        [x0, y0, x1, y1], radius=radius, outline=200, width=3
    )
    glow = glow.filter(ImageFilter.GaussianBlur(radius=4))
    glow_only = glow

    rgba = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    glow_layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for y in range(size):
        for x in range(size):
            ea = glow_only.getpixel((x, y))
            if ea > 3:
                glow_layer.putpixel((x, y), (255, 255, 255, min(200, ea)))
    rgba = Image.alpha_composite(rgba, glow_layer)

    body_rgba = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for y in range(size):
        for x in range(size):
            v = body.getpixel((x, y))
            a = mask_soft.getpixel((x, y))
            if a > 3:
                body_rgba.putpixel((x, y), (v, v, v, a))
    rgba = Image.alpha_composite(rgba, body_rgba)

    # 中心白色圆（非透明，区分 tap）
    circle_r = 14
    circle_img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(circle_img).ellipse(
        [cx - circle_r, cy - circle_r, cx + circle_r, cy + circle_r],
        fill=(255, 255, 255, 240),
    )
    # 柔和边缘
    circle_mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(circle_mask).ellipse(
        [cx - circle_r, cy - circle_r, cx + circle_r, cy + circle_r], fill=255
    )
    circle_mask = circle_mask.filter(ImageFilter.GaussianBlur(radius=1.5))
    circle_img.putalpha(circle_mask)

    rgba = Image.alpha_composite(rgba, circle_img)

    path = os.path.join(RES_DIR, "slider.png")
    rgba.save(path, "PNG")
    print(f"[OK] {path}  ({size}x{size})")


# ============================================================
# 4. SliderPush：矩形光效边框，尺寸略大于 slider
#    slider 是 128x128，sliderpush 用 144x144（每边多 8px）
#    光效从最左侧中心开始顺时针依次亮一圈
# ============================================================
def gen_sliderpush():
    size = 144  # 比 slider(128) 大 16px
    margin = 4  # 边框距离边缘
    border_w = 6  # 边框宽度
    x0, y0 = margin, margin
    x1, y1 = size - 1 - margin, size - 1 - margin

    # 底图：暗色半透明矩形边框
    ring = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(ring)
    # 外矩形
    draw.rectangle([x0, y0, x1, y1], fill=(80, 80, 80, 100))
    # 内矩形挖空
    inner_margin = margin + border_w
    draw.rectangle([inner_margin, inner_margin,
                    size - 1 - inner_margin, size - 1 - inner_margin],
                   fill=(0, 0, 0, 0))
    ring = ring.filter(ImageFilter.GaussianBlur(radius=1))

    path_ring = os.path.join(RES_DIR, "sliderpush_ring.png")
    ring.save(path_ring, "PNG")
    print(f"[OK] {path_ring}  ({size}x{size})")

    # 不同进度预览图
    # 从最左侧中心开始顺时针：左→上→右→下
    # 周长 = 2*(w+h)，w=h=size-1-2*margin
    w = x1 - x0
    h = y1 - y0
    perimeter = 2 * (w + h)

    for pct in [0, 25, 50, 75, 100]:
        if pct == 100:
            # 100% 时用亮色边框替换底图
            preview = Image.new("RGBA", (size, size), (0, 0, 0, 0))
            draw2 = ImageDraw.Draw(preview)
            draw2.rectangle([x0, y0, x1, y1], fill=(255, 255, 255, 220))
            draw2.rectangle([inner_margin, inner_margin,
                             size - 1 - inner_margin, size - 1 - inner_margin],
                            fill=(0, 0, 0, 0))
            preview = preview.filter(ImageFilter.GaussianBlur(radius=1.5))
        else:
            preview = ring.copy()
            if pct > 0:
                # 计算当前进度对应的像素长度
                length = int(perimeter * pct / 100)
                # 从最左侧中心 (x0, cy) 开始顺时针绘制
                cy = size // 2
                # 分段绘制：左→上→右→下
                draw2 = ImageDraw.Draw(preview)
                remaining = length
                # 段 1：左侧，从 cy 向上到 y0
                seg1 = cy - y0
                if remaining > 0:
                    draw_len = min(remaining, seg1)
                    if draw_len > 0:
                        draw2.line([(x0, cy), (x0, cy - draw_len)],
                                   fill=(255, 255, 255, 230), width=border_w)
                    remaining -= draw_len
                # 段 2：上侧，从 x0 向右到 x1
                seg2 = x1 - x0
                if remaining > 0:
                    draw_len = min(remaining, seg2)
                    if draw_len > 0:
                        draw2.line([(x0, y0), (x0 + draw_len, y0)],
                                   fill=(255, 255, 255, 230), width=border_w)
                    remaining -= draw_len
                # 段 3：右侧，从 y0 向下到 y1
                seg3 = y1 - y0
                if remaining > 0:
                    draw_len = min(remaining, seg3)
                    if draw_len > 0:
                        draw2.line([(x1, y0), (x1, y0 + draw_len)],
                                   fill=(255, 255, 255, 230), width=border_w)
                    remaining -= draw_len
                # 段 4：下侧，从 x1 向左到 x0
                seg4 = x1 - x0
                if remaining > 0:
                    draw_len = min(remaining, seg4)
                    if draw_len > 0:
                        draw2.line([(x1, y1), (x1 - draw_len, y1)],
                                   fill=(255, 255, 255, 230), width=border_w)
                    remaining -= draw_len
                preview = preview.filter(ImageFilter.GaussianBlur(radius=1.5))
        path = os.path.join(RES_DIR, f"sliderpush_{pct}.png")
        preview.save(path, "PNG")
        print(f"[OK] {path}  ({pct}%)")


# ============================================================
# 5. Overlay 缩圈图片：矩形框，中间全透明，外侧可绘制底色
# ============================================================
def gen_overlay():
    size = 256
    margin = 8  # 边框距离边缘
    border_w = 16  # 边框宽度

    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # 外矩形（半透明白色）
    draw.rectangle([margin, margin, size - 1 - margin, size - 1 - margin],
                   fill=(255, 255, 255, 200))
    # 内矩形挖空（中间全透明，不遮住 note）
    inner_margin = margin + border_w
    draw.rectangle([inner_margin, inner_margin,
                    size - 1 - inner_margin, size - 1 - inner_margin],
                   fill=(0, 0, 0, 0))

    # 柔和边缘
    img = img.filter(ImageFilter.GaussianBlur(radius=2))

    path = os.path.join(RES_DIR, "overlay.png")
    img.save(path, "PNG")
    print(f"[OK] {path}  ({size}x{size})")


# ============================================================
if __name__ == "__main__":
    print("=== 生成 Melody Matrix 游戏资源 ===\n")
    gen_background()
    gen_tap()
    gen_slider()
    gen_sliderpush()
    gen_overlay()
    print("\n=== 全部生成完毕 ===")
