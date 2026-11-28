# Tower Defense HUD Theme

`game/config/tower_defense/ui_theme.json` is the Tower Defense-scoped HUD text
theme. It is preloaded by `GameConfigCache` and sent with the `tdRound` kickoff
and resume payloads as `ui_theme`. A SIGHUP config reload affects the next
session; an open session keeps the snapshot it started with.

The theme covers **text ink** and the **cost badge** only. Button fills, font
sizes, and the DOM help overlay are deliberately outside this system.

## Why it is mini-game scoped

Themes may differ substantially between mini-games. There is no global theme
layer and no shared fallback palette that can bleed between games. The schema and
client resolver are reusable, but each mini-game owns its own `ui_theme.json`.
Tower Defense is the first consumer.

## Canvas units

Tower Defense renders to a fixed `1400×780` virtual canvas and scales the whole
canvas with CSS. Theme dimensions are therefore expressed in **canvas units**,
not CSS pixels. They scale with the rest of the HUD and do not participate in
`apply_canvas_size_layout()` or any browser-resize relayout. The rare canvas-size
pass continues to exist only for the top HUD's minimum physical font sizes.

## Schema

```json
{
  "ink": {
    "primary": {
      "color": "#F1E4CC",
      "outline": {
        "color": "#24140C",
        "width": 1.5,
        "opacity": 1,
        "line_join": "round"
      },
      "shadow": {
        "color": "#000000",
        "offset_x": 1,
        "offset_y": 1,
        "blur": 1,
        "opacity": 0.5
      }
    }
  },
  "surfaces": {
    "copper_readout": "primary"
  }
}
```

### Ink roles

- `color` — text fill (CSS colour).
- `outline` — required. `color`, positive `width`, optional `opacity` (0–1),
  and optional `line_join` (`round`, `bevel`, or `miter`).
- `shadow` — optional. `color`, `offset_x`, `offset_y`, non-negative `blur`, and
  optional `opacity` (0–1).

All dimensions are canvas units. The outline is the primary legibility layer; the
shadow is subordinate.

### Surfaces

The required surface map is:

| Surface | Used by |
|---|---|
| `copper_readout` | Top HUD copper amount |
| `cost_badge` | Top-right picker cost label (manual canvas draw) |
| `lives_readout` | Top HUD lives amount, when present |
| `picker_label` | Unit and tower picker button labels |
| `context_button` | Recall/targeting/flask/upgrade ring buttons |
| `wave_button` | Start/pause-wave button |
| `forfeit_button` | Forfeit button |
| `help_button` | Standalone help `?` button |
| `menu_label` | Menu fallback label when the icon is unavailable |

`copper_readout` and `cost_badge` must both resolve to the `money` role so the
readout and cost badge keep the same diagetic copper language. The linter
enforces that mapping.

Widgets never own hex values. They resolve `surface → ink role → ink style`.
Missing data falls back to `primary`, then to the client's built-in readable
fallback.

## Cost badge

The cost badge is not a `Text` or `Button`; it is manually drawn in `afterDraw`
so it can sit in the top-right corner of a picker button. It resolves the
`cost_badge` surface and draws with the same outline/shadow ordering as
SimpleGame: the outline casts the shadow, then the fill is drawn cleanly on top.

The player-facing currency is copper. `money` ink is copper, not gold; the cost
badge and copper readout share that role.

## Validation

`tools/check_configs.py` validates:

- required ink roles (`primary`, `money`, `danger`)
- all required surfaces and their ink-role references
- CSS colour strings
- positive outline widths, opacity ranges, and valid line joins
- numeric shadow offsets and non-negative blur
- unknown fields and surfaces

## Deliberate exclusions

- No alternate or user-selectable themes.
- No button fill colours.
- No font family/size/weight changes.
- No glow or text background effects in this pass.
- No resize-time theme reapplication.
