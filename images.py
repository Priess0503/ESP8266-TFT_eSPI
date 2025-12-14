from PIL import Image
import os

# 图标配置：(图片路径, 符号名)
# 如果图片尺寸不同，脚本会自动检测
ICONS = [
    ("centos.png", "centosIcon"),
    ("debian.png", "debianIcon"),
    ("raspi.png", "raspiIcon"),
    ("ji.png", "jiIcon"),
    # 添加你的100x100图片，例如：
    # ("your_image.png", "yourIcon"),
]

# 是否交换高低字节：多数颜色失真用 True 即可修正
SWAP_ENDIAN = True

# 透明像素处理配置
# TRANSPARENT_MODE: "black" (透明像素设为黑色0x0000) 或 "background" (使用背景色混合)
TRANSPARENT_MODE = "black"
# 背景色 (RGB格式，仅在 TRANSPARENT_MODE="background" 时使用)
BACKGROUND_COLOR = (0, 0, 0)  # 黑色背景
# 透明度阈值：alpha值低于此值视为完全透明
ALPHA_THRESHOLD = 128

def rgb_to_565(r, g, b):
    """将RGB值转换为RGB565格式"""
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    if SWAP_ENDIAN:
        rgb565 = ((rgb565 & 0x00FF) << 8) | (rgb565 >> 8)
    return rgb565

def blend_with_background(r, g, b, a, bg_r, bg_g, bg_b):
    """将透明像素与背景色混合"""
    if a >= 255:
        return r, g, b
    alpha = a / 255.0
    inv_alpha = 1.0 - alpha
    return (
        int(r * alpha + bg_r * inv_alpha),
        int(g * alpha + bg_g * inv_alpha),
        int(b * alpha + bg_b * inv_alpha)
    )

def png_to_565_array(path, sym):
    """将PNG图片转换为RGB565数组，自动检测尺寸，支持透明通道"""
    if not os.path.exists(path):
        raise FileNotFoundError(f"文件不存在: {path}")
    
    img = Image.open(path)
    width, height = img.size
    
    # 转换为RGBA模式以支持透明通道
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    
    vals = []
    bg_r, bg_g, bg_b = BACKGROUND_COLOR
    
    for y in range(height):
        for x in range(width):
            pixel = img.getpixel((x, y))
            
            # 处理RGBA或RGB格式
            if len(pixel) == 4:
                r, g, b, a = pixel
            else:
                r, g, b = pixel
                a = 255  # 不透明
            
            # 处理透明像素
            if a < ALPHA_THRESHOLD:
                # 完全透明
                if TRANSPARENT_MODE == "black":
                    rgb565 = 0x0000  # 黑色表示透明
                else:  # background
                    r, g, b = blend_with_background(r, g, b, 0, bg_r, bg_g, bg_b)
                    rgb565 = rgb_to_565(r, g, b)
            else:
                # 半透明或完全不透明
                if TRANSPARENT_MODE == "background" and a < 255:
                    r, g, b = blend_with_background(r, g, b, a, bg_r, bg_g, bg_b)
                rgb565 = rgb_to_565(r, g, b)
            
            vals.append(f"0x{rgb565:04X}")
    
    flat = ", ".join(vals)
    return f"const uint16_t {sym}[{width*height}] PROGMEM = {{ {flat} }};\n", width, height

def main():
    with open("icons_out.h", "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <pgmspace.h>\n#include <stdint.h>\n\n")
        
        # 为每个图标生成数组
        for path, sym in ICONS:
            try:
                array_code, width, height = png_to_565_array(path, sym)
                f.write(array_code)
                f.write("\n")
                print(f"✓ 已处理: {path} ({width}x{height}) -> {sym}")
            except Exception as e:
                print(f"✗ 处理 {path} 时出错: {e}")
    
    print("\n生成完成：icons_out.h")

if __name__ == "__main__":
    main()

