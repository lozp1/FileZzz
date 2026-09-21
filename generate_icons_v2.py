import os
from PIL import Image, ImageDraw, ImageFilter

out_dir = r"C:\Projects\c++\EzFiles\resources\romfs\img"
os.makedirs(out_dir, exist_ok=True)

def render_supersampled(draw_fn, final_size=64, scale=4):
    w = final_size * scale
    img = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    draw_fn(d, w)
    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

# 1. Explorer folder icon (Modern obsidian card folder with electric cyan glow)
def draw_folder(d, w):
    pad = int(w * 0.08)
    tab_w = int(w * 0.42)
    tab_h = int(w * 0.22)
    body_top = int(w * 0.24)
    r = int(w * 0.12)
    # Tab
    d.rounded_rectangle([pad, pad + int(w * 0.06), pad + tab_w, body_top + r], radius=r, fill=(56, 189, 248, 255))
    # Back body
    d.rounded_rectangle([pad, body_top, w - pad, w - pad], radius=r, fill=(14, 21, 38, 255), outline=(56, 189, 248, 255), width=int(w * 0.04))
    # Front flap
    d.rounded_rectangle([pad, int(w * 0.40), w - pad, w - pad], radius=r, fill=(20, 32, 58, 255), outline=(56, 189, 248, 220), width=int(w * 0.03))
    # Glowing highlight line
    d.rounded_rectangle([pad + int(w * 0.1), int(w * 0.50), w - pad - int(w * 0.1), int(w * 0.54)], radius=int(w * 0.02), fill=(56, 189, 248, 200))

# 2. USB-C icon (Emerald glow)
def draw_usb(d, w):
    cx = w // 2
    r = int(w * 0.10)
    # Plug
    d.rounded_rectangle([cx - int(w * 0.20), int(w * 0.14), cx + int(w * 0.20), int(w * 0.38)], radius=int(w * 0.06), fill=(16, 231, 97, 240))
    # Pins cutout
    d.rounded_rectangle([cx - int(w * 0.12), int(w * 0.22), cx - int(w * 0.04), int(w * 0.30)], radius=2, fill=(11, 17, 32, 255))
    d.rounded_rectangle([cx + int(w * 0.04), int(w * 0.22), cx + int(w * 0.12), int(w * 0.30)], radius=2, fill=(11, 17, 32, 255))
    # Body
    d.rounded_rectangle([cx - int(w * 0.28), int(w * 0.36), cx + int(w * 0.28), int(w * 0.78)], radius=r, fill=(14, 21, 38, 255), outline=(16, 231, 97, 255), width=int(w * 0.04))
    # Grip accents
    d.rounded_rectangle([cx - int(w * 0.16), int(w * 0.48), cx + int(w * 0.16), int(w * 0.52)], radius=2, fill=(16, 231, 97, 200))
    d.rounded_rectangle([cx - int(w * 0.16), int(w * 0.58), cx + int(w * 0.16), int(w * 0.62)], radius=2, fill=(16, 231, 97, 200))
    # Cord
    d.rounded_rectangle([cx - int(w * 0.10), int(w * 0.78), cx + int(w * 0.10), int(w * 0.90)], radius=int(w * 0.04), fill=(100, 116, 139, 255))

# 3. FTP / Network Waves icon (Violet glow)
def draw_ftp(d, w):
    cx = w // 2
    cy = int(w * 0.68)
    # Center dot
    d.ellipse([cx - int(w * 0.08), cy - int(w * 0.08), cx + int(w * 0.08), cy + int(w * 0.08)], fill=(168, 85, 247, 255))
    # Wave 1
    r1 = int(w * 0.22)
    d.arc([cx - r1, cy - r1, cx + r1, cy + r1], start=215, end=325, fill=(168, 85, 247, 255), width=int(w * 0.05))
    # Wave 2
    r2 = int(w * 0.36)
    d.arc([cx - r2, cy - r2, cx + r2, cy + r2], start=220, end=320, fill=(168, 85, 247, 220), width=int(w * 0.05))
    # Wave 3
    r3 = int(w * 0.48)
    d.arc([cx - r3, cy - r3, cx + r3, cy + r3], start=225, end=315, fill=(56, 189, 248, 190), width=int(w * 0.04))

# 4. Language Globe icon (Blue glow)
def draw_lang(d, w):
    cx = w // 2
    cy = w // 2
    r = int(w * 0.38)
    # Sphere
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(14, 21, 38, 255), outline=(59, 130, 246, 255), width=int(w * 0.05))
    # Equator
    d.line([cx - r, cy, cx + r, cy], fill=(59, 130, 246, 220), width=int(w * 0.035))
    # Latitude lines
    lat_r = int(r * 0.65)
    d.arc([cx - r, cy - lat_r, cx + r, cy + lat_r], start=0, end=180, fill=(59, 130, 246, 170), width=int(w * 0.025))
    d.arc([cx - r, cy - lat_r, cx + r, cy + lat_r], start=180, end=360, fill=(59, 130, 246, 170), width=int(w * 0.025))
    # Meridian
    d.ellipse([cx - int(r * 0.45), cy - r, cx + int(r * 0.45), cy + r], outline=(56, 189, 248, 220), width=int(w * 0.035))

# 5. About Info icon (Cyan/Emerald glow)
def draw_about(d, w):
    cx = w // 2
    cy = w // 2
    r = int(w * 0.38)
    # Badge circle
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(14, 21, 38, 255), outline=(56, 189, 248, 255), width=int(w * 0.05))
    # Dot
    dot_r = int(w * 0.065)
    d.ellipse([cx - dot_r, int(w * 0.28) - dot_r, cx + dot_r, int(w * 0.28) + dot_r], fill=(56, 189, 248, 255))
    # Stem
    stem_w = int(w * 0.065)
    d.rounded_rectangle([cx - stem_w, int(w * 0.42), cx + stem_w, int(w * 0.72)], radius=int(w * 0.03), fill=(56, 189, 248, 255))

# 6. Small explorer item icons
def draw_file_generic(accent_col):
    def fn(d, w):
        pad_x = int(w * 0.16)
        pad_y = int(w * 0.10)
        r = int(w * 0.10)
        # Sheet
        d.rounded_rectangle([pad_x, pad_y, w - pad_x, w - pad_y], radius=r, fill=(14, 21, 38, 255), outline=(148, 163, 184, 220), width=int(w * 0.04))
        # Bottom badge stripe
        d.rounded_rectangle([pad_x, int(w * 0.72), w - pad_x, w - pad_y], radius=r, fill=accent_col)
        # Content lines
        line_pad = int(w * 0.26)
        d.line([line_pad, int(w * 0.30), w - line_pad, int(w * 0.30)], fill=(148, 163, 184, 200), width=int(w * 0.04))
        d.line([line_pad, int(w * 0.44), w - line_pad - int(w * 0.12), int(w * 0.44)], fill=(148, 163, 184, 180), width=int(w * 0.04))
        d.line([line_pad, int(w * 0.58), w - line_pad, int(w * 0.58)], fill=(148, 163, 184, 160), width=int(w * 0.04))
    return fn

render_supersampled(draw_folder, 64).save(os.path.join(out_dir, "icon_explorer.png"))
render_supersampled(draw_usb, 64).save(os.path.join(out_dir, "icon_usb.png"))
render_supersampled(draw_ftp, 64).save(os.path.join(out_dir, "icon_ftp.png"))
render_supersampled(draw_lang, 64).save(os.path.join(out_dir, "icon_lang.png"))
render_supersampled(draw_about, 64).save(os.path.join(out_dir, "icon_about.png"))

render_supersampled(draw_folder, 36).save(os.path.join(out_dir, "icon_folder_sm.png"))
render_supersampled(draw_file_generic((59, 130, 246, 255)), 36).save(os.path.join(out_dir, "icon_file_sm.png"))
render_supersampled(draw_file_generic((168, 85, 247, 255)), 36).save(os.path.join(out_dir, "icon_nsp_sm.png"))
render_supersampled(draw_file_generic((16, 231, 97, 255)), 36).save(os.path.join(out_dir, "icon_nro_sm.png"))
render_supersampled(draw_file_generic((245, 158, 11, 255)), 36).save(os.path.join(out_dir, "icon_sav_sm.png"))
print("High-res supersampled icons generated successfully!")
