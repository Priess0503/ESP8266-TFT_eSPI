from PIL import Image

ICON_W, ICON_H = 20, 20
ICONS = [
    ("centos.png", "centosIcon"),
    ("debian.png", "debianIcon"),
    ("raspi.png",  "raspiIcon"),
]

# 是否交换高低字节：多数颜色失真用 True 即可修正
SWAP_ENDIAN = True

def png_to_565_array(path, sym):
    img = Image.open(path).convert("RGB")
    if img.size != (ICON_W, ICON_H):
        raise ValueError(f"{path} 尺寸不是 {ICON_W}x{ICON_H}")
    vals = []
    for y in range(ICON_H):
        for x in range(ICON_W):
            r, g, b = img.getpixel((x, y))
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            if SWAP_ENDIAN:
                rgb565 = ((rgb565 & 0x00FF) << 8) | (rgb565 >> 8)
            vals.append(f"0x{rgb565:04X}")
    flat = ", ".join(vals)
    return f"const uint16_t {sym}[{ICON_W*ICON_H}] PROGMEM = {{ {flat} }};\n"

with open("icons_out.h", "w", encoding="utf-8") as f:
    f.write("#pragma once\n#include <pgmspace.h>\n#include <stdint.h>\n")
    f.write(f"static const uint16_t ICON_W = {ICON_W};\n")
    f.write(f"static const uint16_t ICON_H = {ICON_H};\n\n")
    for path, sym in ICONS:
        f.write(png_to_565_array(path, sym))
        f.write("\n")

print("生成完成：icons_out.h")
