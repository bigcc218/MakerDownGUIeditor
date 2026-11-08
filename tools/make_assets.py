"""Build MDGE Studio's geometric icon assets without external dependencies beyond Pillow."""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
RESOURCES = ROOT / "resources"
ASSETS = ROOT / "samples" / "assets"


def rounded_rect(draw: ImageDraw.ImageDraw, box, radius, fill):
    draw.rounded_rectangle(box, radius=radius, fill=fill)


def make_app_icon(size: int = 1024) -> Image.Image:
    scale = 4
    canvas = size * scale
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    pad = int(canvas * 0.045)
    radius = int(canvas * 0.22)
    rounded_rect(draw, (pad, pad, canvas - pad, canvas - pad), radius, "#17231D")

    # A soft emerald field gives the mark a crisp silhouette at small sizes.
    inset = int(canvas * 0.16)
    rounded_rect(draw, (inset, inset, canvas - inset, canvas - inset),
                 int(canvas * 0.135), "#1E7A61")

    # Cream writing sheet with a folded top-right corner.
    sheet = (int(canvas * 0.31), int(canvas * 0.235),
             int(canvas * 0.69), int(canvas * 0.745))
    fold = int(canvas * 0.09)
    page_poly = [
        (sheet[0], sheet[1] + int(canvas * 0.035)),
        (sheet[0] + int(canvas * 0.03), sheet[1]),
        (sheet[2] - fold, sheet[1]),
        (sheet[2], sheet[1] + fold),
        (sheet[2], sheet[3] - int(canvas * 0.03)),
        (sheet[2] - int(canvas * 0.03), sheet[3]),
        (sheet[0] + int(canvas * 0.03), sheet[3]),
    ]
    draw.polygon(page_poly, fill="#FCF8ED")
    draw.polygon(
        [
            (sheet[2] - fold, sheet[1]),
            (sheet[2] - fold, sheet[1] + fold),
            (sheet[2], sheet[1] + fold),
        ],
        fill="#E7DFC9",
    )

    line_color = "#1E7A61"
    line_left = sheet[0] + int(canvas * 0.075)
    line_right = sheet[2] - int(canvas * 0.075)
    for index, width in enumerate((0.72, 0.52, 0.63)):
        y = sheet[1] + int(canvas * (0.20 + index * 0.105))
        right = line_left + int((line_right - line_left) * width)
        draw.rounded_rectangle(
            (line_left, y, right, y + int(canvas * 0.018)),
            radius=int(canvas * 0.009),
            fill=line_color,
        )

    # Gold bookmark anchors the mark without adding text.
    bookmark_width = int(canvas * 0.105)
    bookmark_left = int(canvas * 0.57)
    bookmark_top = int(canvas * 0.17)
    draw.rounded_rectangle(
        (
            bookmark_left,
            bookmark_top,
            bookmark_left + bookmark_width,
            bookmark_top + int(canvas * 0.17),
        ),
        radius=int(canvas * 0.02),
        fill="#C59A52",
    )
    draw.polygon(
        [
            (bookmark_left, bookmark_top + int(canvas * 0.13)),
            (bookmark_left + bookmark_width // 2, bookmark_top + int(canvas * 0.19)),
            (bookmark_left + bookmark_width, bookmark_top + int(canvas * 0.13)),
        ],
        fill="#C59A52",
    )

    # A thin cream baseline grounds the visual.
    baseline_y = int(canvas * 0.80)
    draw.rounded_rectangle(
        (
            int(canvas * 0.37),
            baseline_y,
            int(canvas * 0.63),
            baseline_y + int(canvas * 0.015),
        ),
        radius=int(canvas * 0.0075),
        fill="#AFC6B8",
    )
    return image.resize((size, size), Image.Resampling.LANCZOS)


def make_sample_mark(size: int = 512) -> Image.Image:
    scale = 4
    canvas = size * scale
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    center = canvas // 2
    margin = int(canvas * 0.035)
    draw.ellipse((margin, margin, canvas - margin, canvas - margin),
                 fill="#1D2B24")
    inner_margin = int(canvas * 0.10)
    draw.ellipse(
        (inner_margin, inner_margin, canvas - inner_margin, canvas - inner_margin),
        fill="#1E7A61",
    )

    # Paper cut-out.
    page = (
        int(canvas * 0.30),
        int(canvas * 0.22),
        int(canvas * 0.70),
        int(canvas * 0.78),
    )
    rounded_rect(draw, page, int(canvas * 0.035), "#FCF8ED")
    draw.rounded_rectangle(
        (
            page[0] + int(canvas * 0.07),
            page[1] + int(canvas * 0.12),
            page[2] - int(canvas * 0.07),
            page[1] + int(canvas * 0.155),
        ),
        radius=int(canvas * 0.012),
        fill="#1E7A61",
    )
    for i in range(2):
        y = page[1] + int(canvas * (0.25 + i * 0.11))
        draw.rounded_rectangle(
            (
                page[0] + int(canvas * 0.07),
                y,
                page[2] - int(canvas * (0.19 if i == 0 else 0.12)),
                y + int(canvas * 0.018),
            ),
            radius=int(canvas * 0.009),
            fill="#9DB5A7",
        )
    draw.rounded_rectangle(
        (
            page[0] + int(canvas * 0.07),
            page[3] - int(canvas * 0.12),
            page[2] - int(canvas * 0.30),
            page[3] - int(canvas * 0.09),
        ),
        radius=int(canvas * 0.015),
        fill="#C59A52",
    )

    # Two small orbit dots.
    for angle, distance, radius in ((-0.5, 0.38, 0.022), (2.35, 0.405, 0.015)):
        cx = center + math.cos(angle) * canvas * distance
        cy = center + math.sin(angle) * canvas * distance
        r = canvas * radius
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill="#E9DFC9")
    return image.resize((size, size), Image.Resampling.LANCZOS)


def main() -> None:
    RESOURCES.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    app_icon = make_app_icon()
    app_icon.save(RESOURCES / "app.ico",
                  sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                         (128, 128), (256, 256)])
    app_icon.resize((256, 256), Image.Resampling.LANCZOS).save(
        RESOURCES / "app-preview.png"
    )
    sample = make_sample_mark()
    sample.save(ASSETS / "mdge-mark.png")
    print("assets written")


if __name__ == "__main__":
    main()
