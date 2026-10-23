# `portrait_frames.json`

Decorative **portrait frame** geometry for the client's `RetinuePortrait`
component (and, later, PFP frames). A frame is an ordinary transparent PNG drawn
**on top of** the portrait; this config tells the client where inside that frame
the portrait image should be mapped.

- **File:** `game/config/portrait_frames.json`
- **Loaded by:** `GameConfigCache::getPortraitFrames()` (`server/GameConfigCache.cpp`)
- **Served by:** `/api/getPortraitFrames` (see `api/getPortraitFrames.md`)
- **Validated by:** `tools/check_configs.py` (`validate_portrait_frames`)

## Why it exists

The frame overlay lets the visible opening be any ornamental shape (not only a
square): the portrait is stretched into a rectangle, and the frame's opaque
pixels cover whatever spills past the opening. Because the opening may be
off-center, asymmetric, or under an overhanging ornament, the rectangle cannot be
assumed — it is authored per frame.

## Schema

```json
{
    "format_version": "1.0",
    "frames": {
        "recruit": {
            "image": "/images/retinue/frames/recruit.png",
            "image_width": 600,
            "image_height": 600,
            "portrait": { "x": 44, "y": 44, "width": 512, "height": 512 }
        },
        "management": {
            "image": "/images/retinue/frames/management.png",
            "image_width": 600,
            "image_height": 600,
            "portrait": { "x": 44, "y": 44, "width": 512, "height": 512 }
        }
    }
}
```

| Field | Type | Purpose |
|---|---|---|
| `format_version` | string | Config format version (`"1.0"`). |
| `frames` | object | Map of variant id → frame definition. Non-empty. |
| `frames.<variant>.image` | string | Served overlay URL (`/images/...`). |
| `frames.<variant>.image_width` | int | Frame art pixel width (`> 0`). |
| `frames.<variant>.image_height` | int | Frame art pixel height (`> 0`). |
| `frames.<variant>.portrait` | object | Destination rectangle for the portrait, in source-image pixels. |
| `portrait.x` / `portrait.y` | number | Top-left of the rectangle (`>= 0`). |
| `portrait.width` / `portrait.height` | number | Rectangle size (`> 0`). |

`x + width <= image_width` and `y + height <= image_height`.

## Rendering contract

1. The frame image is **contained** inside the component's `size × size` box,
   preserving its aspect ratio.
2. The portrait rectangle is mapped through the **same** scale and centering
   offset, so it lands in the frame's opening regardless of frame aspect.
3. The portrait is stretched (`object-fit: fill`) into that rectangle; the frame
   is overlaid on top with `pointer-events: none`.
4. If a variant is unknown, or the frame image fails to load, the portrait fills
   the whole box and no overlay is drawn.

## Authoring notes

- Pixel coordinates are used because they are the easiest thing to measure in an
  image editor. Consumers convert to fractions internally using the declared
  `image_width`/`image_height`.
- The rectangle may extend under the frame's opaque edges; that is expected and
  is how non-square interiors are supported.
- Art still owed: `images/retinue/frames/{recruit,management}.png`. Until then
  the linter emits a warning (not an error) for the missing files.
