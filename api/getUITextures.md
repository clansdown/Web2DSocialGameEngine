# POST /api/getUITextures

Return the UI background textures and component-specific padding/button settings
for a textured text component. Public endpoint — no authentication required.

## Request

```json
{
  "component_id": "story_text"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| component_id | string | Yes | Key in `game/config/ui_textures.json` (for example `story_text` or `game_text`) |

## Response

### Success (200 OK)

```json
{
  "status": "ok",
  "data": {
    "textures": [
      {
        "url": "/images/ui/text_silk_413.png",
        "width": 697,
        "height": 413,
        "slice_vertical_px": 80,
        "slice_horizontal_px": 80
      }
    ],
    "padding_vertical_px": 140,
    "padding_horizontal_px": 120,
    "button": {
      "image": "/images/ui/button_silk.png",
      "image_slice_px": 24,
      "text_color": "#f5e6c8",
      "min_width_px": 180,
      "min_height_px": 56,
      "padding": "0.5rem 2rem"
    }
  }
}
```

| Field | Type | Description |
|---|---|---|
| textures | array | Texture files matching the component's `texture_prefix`, sorted by height |
| textures[].url | string | Served texture path (rebased to the asset origin by the client) |
| textures[].width | integer | Native texture width in pixels |
| textures[].height | integer | Native texture height in pixels |
| textures[].slice_vertical_px | integer | Optional CSS `border-image-slice` top/bottom inset in source pixels |
| textures[].slice_horizontal_px | integer | Optional CSS `border-image-slice` left/right inset in source pixels |
| padding_vertical_px | integer | Safe-area vertical padding at native texture scale |
| padding_horizontal_px | integer | Safe-area horizontal padding at native texture scale |
| button | object | Optional dismiss-button styling; omitted when not configured |
| button.image | string | Button plaque image (bare filename resolved under `images/ui`, or a `/images/...` path) |
| button.image_slice_px | integer | When > 0, use CSS `border-image` 9-slice instead of stretching |
| button.text_color | string | CSS text color for the button label |
| button.min_width_px | integer | Optional minimum button width |
| button.min_height_px | integer | Optional minimum button height |
| button.padding | string | Optional CSS padding for the button label |

### Errors

```json
{
  "status": "ok",
  "data": {},
  "error": "component_id required"
}
```

## Implementation Notes

- Reads `game/config/ui_textures.json` by `component_id`.
- `texture_slice_px` is optional. It is an object keyed by exact image filename
  (including extension), with `vertical` and `horizontal` positive integer
  values. The server copies them onto each matching texture as
  `slice_vertical_px` / `slice_horizontal_px`; the client uses them for CSS
  `border-image` 9-slicing. Textures without an entry fall back to the
  component's `padding_*_px` in the client.
- A component's `padding_*_px` remains the text safe area. The client treats the
  larger of the configured padding and the texture slice as the total inset, so
  a thick frame never overlaps text.
- The `button` block is optional. When absent, `TextOverlayButton` falls back to
  the built-in brown/gold button style.
- A bare `button.image` filename is resolved to `/images/ui/<filename>`; a path
  beginning with `/` is used unchanged.
- The client rebases `/images/...` URLs to the configured public asset origin.
- `StoryText`/`GameText` use this endpoint for both inline and overlay modes.
  Overlay mode adds `TextOverlayFrame` and `TextOverlayButton`; the generic
  `DialogOverlay` is not used for story/game text.
