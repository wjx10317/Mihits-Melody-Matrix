"""生成 Melody Matrix 游戏所需的 PNG 资源图片（带 alpha 通道）"""
from PIL import Image, ImageDraw, ImageFilter, ImageChops
import math
import os

RES_DIR = os.path.join(os.path.dirname(__file__), "res")
os.makedirs(RES_DIR, exist_ok=True)


# ============================================================
# 1. 背景图片：白色圆角格面，外侧透明（无黑边；格缝由 Background Dim 填充）
# ============================================================
def gen_background():
    size = 256
    margin = 8
    radius = 36
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
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
# 4. HoldPush：圆角进度环（见 tools/generate_holdpush.py）
# ============================================================
def gen_holdpush():
    import subprocess
    import sys
    script = os.path.join(os.path.dirname(__file__), "tools", "generate_holdpush.py")
    subprocess.check_call([sys.executable, script])


# ============================================================
# 5. Overlay 缩圈：圆角环（见 tools/generate_overlay.py，与 tap/holdpush 同弧度）
# ============================================================
def gen_overlay():
    import subprocess
    import sys
    script = os.path.join(os.path.dirname(__file__), "tools", "generate_overlay.py")
    subprocess.check_call([sys.executable, script])


# ============================================================
if __name__ == "__main__":
    print("=== 生成 Melody Matrix 游戏资源 ===\n")
    gen_background()
    gen_tap()
    gen_slider()
    gen_holdpush()
    gen_overlay()
    print("\n=== 全部生成完毕 ===")
