import os
from PIL import Image, ImageDraw

out_dir = r"C:\Projects\c++\EzFiles\resources\romfs\img"
os.makedirs(out_dir, exist_ok=True)

def create_folder_icon(path, size=64):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Modern sleek folder with glowing cyan
    # Tab
    d.rounded_rectangle([6, 12, 28, 26], radius=4, fill=(56, 189, 248, 255))
    # Back body
    d.rounded_rectangle([6, 18, size-6, size-10], radius=8, fill=(14, 21, 38, 255), outline=(56, 189, 248, 255), width=3)
    # Front flap with gradient look
    d.rounded_rectangle([6, 26, size-6, size-10], radius=7, fill=(20, 32, 58, 255), outline=(56, 189, 248, 220), width=2)
    # Accent line
    d.line([14, 34, size-14, 34], fill=(56, 189, 248, 180), width=2)
    img.save(path)
    print(f"Created {path}")

def create_usb_icon(path, size=64):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Modern USB-C connector with emerald glow
    cx = size // 2
    # Plug tip
    d.rounded_rectangle([cx-12, 10, cx+12, 26], radius=4, fill=(16, 231, 97, 240))
    # Inner pins
    d.rectangle([cx-7, 14, cx-3, 20], fill=(11, 17, 32, 255))
    d.rectangle([cx+3, 14, cx+7, 20], fill=(11, 17, 32, 255))
    # Body
    d.rounded_rectangle([cx-18, 24, cx+18, 50], radius=6, fill=(14, 21, 38, 255), outline=(16, 231, 97, 255), width=3)
    # Cable collar
    d.rounded_rectangle([cx-6, 50, cx+6, 58], radius=2, fill=(100, 116, 139, 255))
    img.save(path)
    print(f"Created {path}")

def create_ftp_icon(path, size=64):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Modern WiFi / Cloud Waves with violet glow
    cx, cy = size // 2, 42
    # Center dot
    d.ellipse([cx-4, cy-4, cx+4, cy+4], fill=(168, 85, 247, 255))
    # Wave 1
    d.arc([cx-14, cy-14, cx+14, cy+14], start=210, end=330, fill=(168, 85, 247, 255), width=3)
    # Wave 2
    d.arc([cx-24, cy-24, cx+24, cy+24], start=215, end=325, fill=(168, 85, 247, 200), width=3)
    # Wave 3
    d.arc([cx-32, cy-32, cx+32, cy+32], start=220, end=320, fill=(56, 189, 248, 180), width=2)
    img.save(path)
    print(f"Created {path}")

def create_lang_icon(path, size=64):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Modern Globe / i18n sphere with sky blue glow
    cx, cy = size // 2, size // 2
    r = 22
    d.ellipse([cx-r, cy-r, cx+r, cy+r], fill=(14, 21, 38, 255), outline=(56, 189, 248, 255), width=3)
    # Horizontal latitude
    d.line([cx-r+2, cy, cx+r-2, cy], fill=(56, 189, 248, 220), width=2)
    # Vertical longitude ellipse
    d.ellipse([cx-11, cy-r, cx+11, cy+r], outline=(56, 189, 248, 200), width=2)
    img.save(path)
    print(f"Created {path}")

def create_about_icon(path, size=64):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Modern Info badge with amber glow
    cx, cy = size // 2, size // 2
    r = 22
    d.ellipse([cx-r, cy-r, cx+r, cy+r], fill=(14, 21, 38, 255), outline=(245, 158, 11, 255), width=3)
    # Info "i" dot
    d.ellipse([cx-3, cy-14, cx+3, cy-8], fill=(245, 158, 11, 255))
    # Info "i" stem
    d.rounded_rectangle([cx-3, cy-4, cx+3, cy+14], radius=2, fill=(245, 158, 11, 255))
    img.save(path)
    print(f"Created {path}")

def create_file_icon(path, size=48, accent=(59, 130, 246, 255)):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Document sheet with folded corner
    d.rounded_rectangle([10, 6, size-10, size-6], radius=5, fill=(14, 21, 38, 255), outline=(148, 163, 184, 200), width=2)
    # Bottom accent stripe
    d.rounded_rectangle([10, size-14, size-10, size-6], radius=3, fill=accent)
    # Lines
    d.line([16, 16, size-16, 16], fill=(100, 116, 139, 220), width=2)
    d.line([16, 24, size-20, 24], fill=(100, 116, 139, 180), width=2)
    img.save(path)
    print(f"Created {path}")

create_folder_icon(os.path.join(out_dir, "icon_explorer.png"))
create_usb_icon(os.path.join(out_dir, "icon_usb.png"))
create_ftp_icon(os.path.join(out_dir, "icon_ftp.png"))
create_lang_icon(os.path.join(out_dir, "icon_lang.png"))
create_about_icon(os.path.join(out_dir, "icon_about.png"))

# Small file type icons for explorer
create_folder_icon(os.path.join(out_dir, "icon_folder_sm.png"), size=36)
create_file_icon(os.path.join(out_dir, "icon_file_sm.png"), size=36, accent=(59, 130, 246, 255))
create_file_icon(os.path.join(out_dir, "icon_nsp_sm.png"), size=36, accent=(168, 85, 247, 255))
create_file_icon(os.path.join(out_dir, "icon_nro_sm.png"), size=36, accent=(16, 231, 97, 255))
create_file_icon(os.path.join(out_dir, "icon_sav_sm.png"), size=36, accent=(245, 158, 11, 255))
