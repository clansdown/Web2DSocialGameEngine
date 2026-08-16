#!/usr/bin/env python3
"""
Road Tile Art Generator

Generates the four procedural placeholder road tiles used by the manor's
auto-tiling system. Tiles are drawn so their seams align: each tile is a
square of packed-dirt texture with a stone-gravel path running edge-to-edge
on the connected sides. The client rotates a single base image (via
SimpleGame's setOrientation) to reach each connectivity case, so only four
base images are needed:

  road_straight.png   - path connecting east <-> west
  road_corner.png     - path connecting north <-> east
  road_three_way.png  - T junction connecting north, east, west
  road_four_way.png   - path connecting all four sides

The canonical connection sets are declared in fiefdom_building_types.json
under `road_tiles_canonical` (used by the client's rotation logic).

Usage:
    python3 tools/generate_road_tiles.py [--out-dir DIR]

Output (default: game/images/manor/buildings/):
    road_straight.png, road_corner.png, road_three_way.png, road_four_way.png
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


def draw_road_tile(connections: set[str], size: int = 256) -> Image.Image:
    """Draw a single road tile with the given orthogonal connections.

    Connections are a subset of {"n", "e", "s", "w"} — the sides the road
    path exits the tile from. The path is centered in the tile and drawn
    with a stone-gravel fill over a packed-dirt background so neighboring
    tiles with matching connections blend into a continuous road.

    Args:
        connections: Set of side names the road connects to
        size: Output image width/height in pixels

    Returns:
        PIL Image of the road tile (RGBA)
    """
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    dirt = Image.new("RGB", (size, size), (96, 78, 60))
    dirt_px = dirt.load()

    # Speckled packed-dirt texture
    import random
    rng = random.Random(20260808)
    for y in range(size):
        for x in range(size):
            n = rng.randint(-8, 8)
            v = min(255, max(0, 96 + n))
            dirt_px[x, y] = (v, v - 8, v - 28)

    # Road band thickness (relative to tile size)
    band = int(size * 0.42)

    # Path cells: draw a filled polygon covering the band across connected sides
    path = Image.new("L", (size, size), 0)
    pd = ImageDraw.Draw(path)

    cx = size // 2
    cy = size // 2
    half = band // 2

    def add_h_segment(x0: int, x1: int) -> None:
        pd.rectangle([x0, cy - half, x1, cy + half], fill=255)

    def add_v_segment(y0: int, y1: int) -> None:
        pd.rectangle([cx - half, y0, cx + half, y1], fill=255)

    if "w" in connections:
        add_h_segment(0, cx + half)
    if "e" in connections:
        add_h_segment(cx - half, size)
    if "n" in connections:
        add_v_segment(0, cy + half)
    if "s" in connections:
        add_v_segment(cy - half, size)

    # Round the centre joint so connections blend (soften edges slightly)
    path = path.filter(ImageFilter.GaussianBlur(1.2))

    road_layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    road = ImageDraw.Draw(road_layer)

    # Gravel base for the path
    gravel_base = (118, 108, 96)
    for y in range(size):
        for x in range(size):
            if path.getpixel((x, y)) > 128:
                n = rng.randint(-10, 10)
                r = min(255, max(0, gravel_base[0] + n))
                g = min(255, max(0, gravel_base[1] + n))
                b = min(255, max(0, gravel_base[2] + n))
                road_layer.putpixel((x, y), (r, g, b, 255))

    # Sprinkle a few lighter stones along the path
    for _ in range(int(size * size / 900)):
        while True:
            sx = rng.randint(0, size - 1)
            sy = rng.randint(0, size - 1)
            if path.getpixel((sx, sy)) > 128:
                s = rng.randint(2, 5)
                road.ellipse([sx, sy, sx + s, sy + s], fill=(168, 156, 138, 255))
                break

    # Darker edge lines on the path sides for definition
    for y in range(size):
        for x in range(size):
            if path.getpixel((x, y)) > 128:
                px_l = path.getpixel((x - 1, y)) if x > 0 else 0
                px_r = path.getpixel((x + 1, y)) if x < size - 1 else 0
                px_u = path.getpixel((x, y - 1)) if y > 0 else 0
                px_d = path.getpixel((x, y + 1)) if y < size - 1 else 0
                if px_l <= 128 or px_r <= 128 or px_u <= 128 or px_d <= 128:
                    road_layer.putpixel((x, y), (74, 66, 56, 255))

    img.paste(road_layer, (0, 0), road_layer)
    img.paste(Image.new("RGBA", (size, size), (0, 0, 0, 0)), (0, 0))
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(img)
    out.paste(dirt, (0, 0))
    out.alpha_composite(road_layer)
    return out


def main() -> None:
    """Generate the four road tile images and write them to the output dir."""
    parser = argparse.ArgumentParser(description="Generate procedural road tile art")
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("game/images/manor/buildings"),
        help="Directory to write road tile PNGs (default: game/images/manor/buildings)",
    )
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)

    tiles: dict[str, set[str]] = {
        "straight": {"e", "w"},
        "corner": {"n", "e"},
        "three_way": {"n", "e", "w"},
        "four_way": {"n", "e", "s", "w"},
    }

    for name, connections in tiles.items():
        img = draw_road_tile(connections)
        out_path = args.out_dir / f"road_{name}.png"
        img.save(out_path, "PNG")
        print(f"Wrote {out_path} ({img.size[0]}x{img.size[1]})")


if __name__ == "__main__":
    main()
