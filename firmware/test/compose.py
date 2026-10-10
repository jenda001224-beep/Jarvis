#!/usr/bin/env python3
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
FONTS = HERE.parent / "main" / "fonts"
TEXT = (245, 249, 255)
MUTED = (141, 162, 181)
DIM = (81, 96, 110)
ACCENT = (91, 215, 255)


def font(weight, size):
    return ImageFont.truetype(str(FONTS / f"inter_{weight}.ttf"), size)


def spaced(draw, centre, y, text, face, fill, spacing, anchor="middle"):
    widths = [draw.textlength(ch, font=face) for ch in text]
    total = sum(widths) + spacing * (len(text) - 1)
    x = centre - total / 2 if anchor == "middle" else centre if anchor == "left" else centre - total
    for ch, width in zip(text, widths):
        draw.text((x, y), ch, font=face, fill=fill)
        x += width + spacing


def idle(image):
    draw = ImageDraw.Draw(image)
    clock = ImageFont.truetype(str(FONTS / "inter_clock.ttf"), 92)
    draw.text((240, 206), "21:47", font=clock, fill=TEXT, anchor="mm")
    spaced(draw, 240, 266, "PÁTEK 9. ŘÍJNA", font(500, 13), MUTED, 3)
    spaced(draw, 24, 18, "J.A.R.V.I.S.", font(500, 12), DIM, 3, "left")
    spaced(draw, 240, 444, "ŘEKNI „JARVISI“", font(500, 12), DIM, 3)


def voice(image, caption, heard, spoken):
    draw = ImageDraw.Draw(image)
    spaced(draw, 240, 351, caption, font(500, 12), ACCENT, 4)
    draw.text((240, 380), heard, font=font(400, 17), fill=MUTED, anchor="mm")
    y = 407
    for line in spoken:
        draw.text((240, y), line, font=font(400, 20), fill=TEXT, anchor="mm")
        y += 25


def main():
    folder = Path(sys.argv[1])
    frames = sorted(folder.glob("*.ppm"))
    sheet = Image.new("RGB", (480 * 3 + 40, 480 * ((len(frames) + 2) // 3) + 20 * ((len(frames) + 2) // 3 + 1)), (24, 24, 28))
    for index, path in enumerate(frames):
        image = Image.open(path).convert("RGB")
        name = path.stem
        if "idle" in name:
            idle(image)
        elif "listening" in name or "wake" in name:
            voice(image, "POSLOUCHÁM", "zhasni světlo v obýváku", [])
        elif "thinking" in name:
            voice(image, "PŘEMÝŠLÍM", "zhasni světlo v obýváku", [])
        elif "speaking" in name:
            voice(image, "JARVIS", "zhasni světlo v obýváku", ["Zhasnuto. V obýváku je teď tma,", "jak sis přál."])
        elif "error" in name:
            voice(image, "NĚCO SE NEPOVEDLO", "", ["Gemini se neozval."])
        image.save(folder / f"{name}_ui.png")
        sheet.paste(image, (10 + (index % 3) * 490, 20 + (index // 3) * 500))
    sheet.save(folder / "sheet.png")


main()
