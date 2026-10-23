# `/api/getPortraitFrames`

Returns the decorative **portrait-frame** catalog
(`game/config/portrait_frames.json`): for each frame variant it gives the overlay
image, that image's pixel size, and the rectangle (in those same pixels) that the
portrait image is mapped into.

The frame is an ordinary transparent PNG the client draws **on top of** the
portrait (`pointer-events: none`). The rectangle is therefore a **draw box, not a
mask** — the frame's opaque pixels occlude whatever spills past the opening,
which lets the interior edge be non-square or ornamental. The portrait itself is
stretched (`object-fit: fill`) into the rectangle.

The client fetches this once and shares it across every `RetinuePortrait`
(`client/src/lib/portrait_frames.ts`).

**Requires authentication.**

## Request

```json
{
    "auth": {
        "username": "player",
        "token": "session_token_hex"
    }
}
```

No body fields beyond `auth` are required.

## Success Response

```json
{
    "status": "ok",
    "data": {
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
}
```

## Error Responses

```json
{ "needs_auth": true }
```

## Notes

- `image` is the served URL (`/images/retinue/frames/...`, served by the
  `/images/retinue/*` static route); the config stores it in that form, matching
  the building-image convention.
- `image_width` / `image_height` are the frame art's pixel dimensions. The client
  contains the frame inside the component's square box (preserving aspect) and
  maps `portrait` through the same scale/offset, so non-square frames align
  correctly.
- `portrait.{x,y}` are `>= 0` and `portrait.{width,height}` are `> 0`;
  `x + width <= image_width` and `y + height <= image_height` (validated by
  `tools/check_configs.py`).
- If a variant is absent (or its image 404s) the portrait simply fills the
  component box — no frame is drawn.
- Variants are config-driven; `recruit` and `management` are the current two.
  The catalog is generic so PFP frames can reuse it later.
- See `server/docs/portrait_frames.md` and `docs/retinue_design.md` §13.
