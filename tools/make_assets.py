"""Build MDGE Studio's geometric icon assets without external dependencies beyond Pillow."""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
RESOURCES = ROOT / "resources"
ASSETS = ROOT / "samples" / "assets"


def rounded_rect(draw: ImageDraw.ImageDraw, box, radius, fill, outline=None,
                 width=0):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline,
                           width=width)


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


def make_md_file_icon(size: int = 1024) -> Image.Image:
    """A single Markdown page, distinct from the application icon."""
    canvas = size
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    left, top = int(canvas * 0.12), int(canvas * 0.055)
    right, bottom = int(canvas * 0.88), int(canvas * 0.945)
    rounded_rect(draw, (left, top, right, bottom), int(canvas * 0.055),
                 "#FCF8ED", "#183428", int(canvas * 0.018))
    fold = int(canvas * 0.19)
    draw.polygon(
        [(right - fold, top), (right, top + fold), (right - fold, top + fold)],
        fill="#C59A52",
    )
    draw.line(
        [(right - fold, top), (right - fold, top + fold), (right, top + fold)],
        fill="#183428", width=int(canvas * 0.014), joint="curve",
    )
    rounded_rect(draw, (int(canvas * 0.23), int(canvas * 0.19),
                        int(canvas * 0.73), int(canvas * 0.30)),
                 int(canvas * 0.025), "#1E7A61")
    for y, width in ((0.40, 0.52), (0.51, 0.40), (0.62, 0.47)):
        x0 = int(canvas * 0.23)
        x1 = x0 + int(canvas * width)
        y0 = int(canvas * y)
        rounded_rect(draw, (x0, y0, x1, y0 + int(canvas * 0.026)),
                     int(canvas * 0.013), "#7E9B8E")
    rounded_rect(draw, (int(canvas * 0.23), int(canvas * 0.78),
                        int(canvas * 0.55), int(canvas * 0.815)),
                 int(canvas * 0.017), "#C59A52")
    return image


def make_mdge_file_icon(size: int = 1024) -> Image.Image:
    """A layered gold project folder, distinct from both the app and .md icons."""
    canvas = size
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    rounded_rect(draw, (int(canvas * 0.10), int(canvas * 0.20),
                        int(canvas * 0.46), int(canvas * 0.34)),
                 int(canvas * 0.045), "#173D31")
    rounded_rect(draw, (int(canvas * 0.22), int(canvas * 0.12),
                        int(canvas * 0.58), int(canvas * 0.55)),
                 int(canvas * 0.035), "#F2EAD8", "#183428",
                 int(canvas * 0.012))
    rounded_rect(draw, (int(canvas * 0.32), int(canvas * 0.075),
                        int(canvas * 0.70), int(canvas * 0.50)),
                 int(canvas * 0.035), "#FCF8ED", "#183428",
                 int(canvas * 0.012))
    for y, width in ((0.19, 0.26), (0.29, 0.21), (0.39, 0.24)):
        x0 = int(canvas * 0.39)
        x1 = x0 + int(canvas * width)
        y0 = int(canvas * y)
        rounded_rect(draw, (x0, y0, x1, y0 + int(canvas * 0.024)),
                     int(canvas * 0.012), "#1E7A61")
    rounded_rect(draw, (int(canvas * 0.075), int(canvas * 0.31),
                        int(canvas * 0.925), int(canvas * 0.90)),
                 int(canvas * 0.075), "#C59A52", "#183428",
                 int(canvas * 0.018))
    rounded_rect(draw, (int(canvas * 0.075), int(canvas * 0.31),
                        int(canvas * 0.925), int(canvas * 0.44)),
                 int(canvas * 0.055), "#A97E3C")
    rounded_rect(draw, (int(canvas * 0.12), int(canvas * 0.43),
                        int(canvas * 0.88), int(canvas * 0.83)),
                 int(canvas * 0.045), "#1E7A61")
    for offset, fill in ((0.0, "#FCF8ED"), (0.10, "#E8DDC4"),
                         (0.20, "#D6C9AC")):
        rounded_rect(
            draw,
            (int(canvas * (0.23 + offset * 0.18)),
             int(canvas * (0.53 + offset)),
             int(canvas * (0.68 + offset * 0.18)),
             int(canvas * (0.66 + offset))),
            int(canvas * 0.025), fill, "#183428", int(canvas * 0.010),
        )
    return image


def save_icon(image: Image.Image, path: Path) -> None:
    image.save(path, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                            (128, 128), (256, 256)])


def main() -> None:
    RESOURCES.mkdir(parents=True, exist_ok=True)
    ASSETS.mkdir(parents=True, exist_ok=True)
    app_icon = make_app_icon()
    save_icon(app_icon, RESOURCES / "app.ico")
    app_icon.resize((256, 256), Image.Resampling.LANCZOS).save(
        RESOURCES / "app-preview.png"
    )
    mdge_file_icon = make_mdge_file_icon()
    save_icon(mdge_file_icon, RESOURCES / "mdge-file.ico")
    mdge_file_icon.resize((256, 256), Image.Resampling.LANCZOS).save(
        RESOURCES / "mdge-file-preview.png"
    )
    md_file_icon = make_md_file_icon()
    save_icon(md_file_icon, RESOURCES / "md-file.ico")
    md_file_icon.resize((256, 256), Image.Resampling.LANCZOS).save(
        RESOURCES / "md-file-preview.png"
    )
    sample = make_sample_mark()
    sample.save(ASSETS / "mdge-mark.png")
    print("assets written")


if __name__ == "__main__":
    main()
