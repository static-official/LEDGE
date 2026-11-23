#!/usr/bin/env python3
"""Generates the LEDGE VR launcher icon (pastel toon, ledge motif)."""
from PIL import Image, ImageDraw
import os

S = 512
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# Rounded pastel sky background
d.rounded_rectangle([16, 16, S-16, S-16], radius=96, fill=(183, 228, 237, 255))

# Sun
d.ellipse([360, 70, 460, 170], fill=(255, 214, 130, 255))

# Clouds
for cx, cy, cw in [(90, 110, 70), (170, 90, 55)]:
    d.ellipse([cx, cy, cx+cw, cy+cw*0.62], fill=(255, 255, 255, 235))
d.ellipse([150, 100, 240, 160], fill=(255, 255, 255, 235))

# Ground
d.rectangle([16, 380, S-16, S-16], fill=(126, 200, 142, 255))
d.rectangle([16, 380, S-16, 396], fill=(105, 178, 120, 255))

# Ledge platform (the "ledge"!)
d.rounded_rectangle([120, 300, 392, 352], radius=14, fill=(240, 170, 128, 255))
d.rounded_rectangle([120, 340, 392, 372], radius=8, fill=(214, 142, 103, 255))

# Player blob standing on the ledge
d.ellipse([226, 196, 292, 262], fill=(255, 123, 140, 255))   # head/body
d.ellipse([240, 210, 256, 226], fill=(60, 50, 60, 255))      # eyes
d.ellipse([262, 210, 278, 226], fill=(60, 50, 60, 255))

# Title text
from PIL import ImageFont
font = None
for p in ["/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]:
    if os.path.exists(p):
        font = ImageFont.truetype(p, 72)
        break
if font is None:
    font = ImageFont.load_default()
d.text((S//2, 130), "LEDGE", font=font, fill=(44, 62, 80, 255), anchor="mm")
d.text((S//2, 196), "VR", font=ImageFont.truetype(p, 56) if os.path.exists(p) else font,
       fill=(44, 62, 80, 255), anchor="mm")

out_dir = "/home/z/my-project/ledgevr/app/res/mipmap"
os.makedirs(out_dir, exist_ok=True)
img.save(os.path.join(out_dir, "ic_launcher.png"))
# Also a small banner used as in-VR loading texture source is drawn procedurally in C++.
print("icon written", os.path.join(out_dir, "ic_launcher.png"))
