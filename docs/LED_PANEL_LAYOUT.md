# LED panel layout (5×5 WS2812B)

How the firmware maps "what should light up" onto a physical LED panel, and how
to configure a new panel. Code: `src/WS2812BLedMatrix.{h,cpp}`.

## The two coordinate systems

**User view.** The drawing code only works in this system. It's the 5×5 grid
exactly as the fencer sees it in normal use, addressed row-major with
`u = row*5 + col` and `0` = top-left:

```
 0  1  2  3  4
 5  6  7  8  9
10 11 12 13 14
15 16 17 18 19
20 21 22 23 24
```

- Glyphs (R, E, F, P, C, GND, diamond) are 5-row bitmaps, top row first, with
  bit 4 = leftmost column. The `#####` comment next to each row is literally
  what you see.
- Animations are lists of user-view indices, in drawing order.
- The three wires are drawn as **columns**. The tables are written with wire 0
  on the right (wire *k* = column `4 - 2k`).

## Wire side: a second, independent per-board property

Which column shows wire 0 has to match where the banana jacks physically sit
next to the display. That's a property of the board, not of the LED wiring, so
it's a separate compile-time constant, `LED_WIRE0_ON_LEFT`, set per
`HARDWARE_REV` in `src/WS2812BLedMatrix.h`:

| Build env | `LED_WIRE0_ON_LEFT` | Wire *k* drawn in column |
|---|---|---|
| `hw_rev1`, `hw_rev2` | `false` | `4 - 2k` (wire 0 right) |
| `hw_rev3` | `true` | `2k` (wire 0 left). Verified on the bench, 2026-09-29 |

Wire content (`SetLine`, the good/broken/short/swap/wrong-connection
animations, ArBr, BrCr) is drawn through `setWirePixel()`, which mirrors
left↔right when the flag is set. Glyphs, the welcome sequence, the inner-9 and
blink pixels go through `setUserPixel()` and are never mirrored.

**Don't "fix" a wire-side problem with `LedLayout`.** On rev3, setting `BL-V`
makes the wires look right but mirrors every glyph. That's exactly how this
second property was discovered. The rule:
- If the glyphs are wrong, the layout is wrong.
- If only the wire order is wrong, `LED_WIRE0_ON_LEFT` is wrong.

**Chain order.** This is the order of the LEDs on the data line: LED #0 is the
first LED after the data input. It depends only on the panel's wiring and on
how the panel is mounted. The drawing code never sees it.

`setLayout()` builds a 25-entry table `m_map[u] = chain index`, and every pixel
write goes through it.

## Specifying a layout: `<corner>-<direction>[-P]`

All the panels we've seen so far are **serpentine** (snake) wired. For a square
panel, the complete wiring as seen by the user is fixed by two things:

| Part | Values | Meaning |
|---|---|---|
| corner | `BL` `BR` `TL` `TR` | where LED #0 sits, **as seen by the user** |
| direction | `V` `H` | the first run from that corner: `V` = along a column (up or down), `H` = along a row |
| `-P` (optional) | | progressive wiring: every run goes the same direction, instead of snaking back |

That gives 4 × 2 = **8 snake layouts**. Examples:

```
BL-V (reference layout)        BR-V (hw_rev3 board)
 4  5 14 15 24                 24 15 14  5  4
 3  6 13 16 23                 23 16 13  6  3
 2  7 12 17 22                 22 17 12  7  2
 1  8 11 18 21                 21 18 11  8  1
 0  9 10 19 20                 20 19 10  9  0
```

### Why rotation and mirroring need no extra setting

Rotating a panel, or seeing it from the other side (mirrored), turns one of
the 8 layouts into another. Those 8 are exactly the 4 rotations × 2 mirror
images of a square, so the corner and direction as seen by the user already
include the mounting. Starting from `BL-V`:

| Panel is… | Layout to set |
|---|---|
| as-is | `BL-V` |
| rotated 90° clockwise | `TL-H` |
| rotated 180° | `TR-V` |
| rotated 270° clockwise | `BR-H` |
| mirrored left↔right (LEDs on the other face) | `BR-V` |
| mirrored + rotated 90° / 180° / 270° | `BL-H` / `TL-V` / `TR-H` |

The hw_rev3 board mounts D5–D29 on the PCB bottom, which is why its layout
(`BR-V`) is the mirror image of the reference `BL-V`.

## Per-board defaults and the runtime setting

The default comes from `LED_LAYOUT_DEFAULT` in `src/WS2812BLedMatrix.h`, one
per `HARDWARE_REV`:

| Build env | Default | Source |
|---|---|---|
| `hw_rev1` | `TL-V` | **inferred, not verified on hardware**: the legacy E/F tables with `MirrorMode=true` (the old firmware default) light the same LEDs as the verified hw_rev2 ones. The hw_rev2 commit message says the rev1 panel was "wired transposed", though, so run `ledtest` on a rev1 unit before trusting this |
| `hw_rev2` | `TL-V` | derived from the hw_rev2 R/E/F glyph maps that were checked on the real panel |
| `hw_rev3` | `BR-V` | native matrix on the board bottom. Verified: F/E/R upright with `BR-V`, mirrored with `BL-V` |

You can override it at runtime through the `LedLayout` setting (serial or web
terminal, and the web settings page), without rebuilding:

```
ledtest            # show the test pattern with the current layout
ledtest TR-H       # preview another layout for 5 s (not saved)
set LedLayout TR-H # save it
list               # shows the active LedLayout
```

An invalid stored value falls back to the board default at boot, with a
message on the serial console.

## Configuring an unknown panel

1. Run `ledtest`. It lights:
   - a green **F**, drawn upright in user view. F looks different in all 8
     layouts, so exactly one layout draws it correctly.
   - chain LED **#0 in red** and **#1 in orange**, written directly to the chain
     and bypassing the map. Together they show the start corner and the first
     direction.
2. Read the layout straight off the red/orange pair. For example, red at the
   bottom-left with orange directly above it is `BL-V`.
3. Confirm with `ledtest <layout>`: the F must be upright and readable, not
   mirrored.
4. Save it with `set LedLayout <layout>`. If it's a new board revision, also
   change `LED_LAYOUT_DEFAULT`.

## Adding or changing content

Draw in user view and don't think about the wiring:

- A new glyph is a `static const uint8_t X[LED_GRID]` bitmap plus
  `drawBitmap(X, color)`.
- A new animation is a list of user-view indices plus `setUserPixel(u, color)`.
- Never call `m_pixels->setPixelColor()` directly, except for deliberately
  chain-order output such as the LED #0/#1 markers in `LayoutTest()`.

## History (what this replaced, 2026-09-29)

- **`MirrorMode` (bool setting)** chose between identity and a top↔bottom row
  flip, which covers only 2 of the 8 layouts. The old NVS key is simply
  ignored now.
- **`CONFIG_15_20`** was a compile-time set of alternative tables for a
  left↔right mirrored panel.
- **`glyphIndex()` + `#if HARDWARE_REV == 2` R/E/F tables** were hand-mapped
  chain indices for one specific panel, generated with
  `WS2812c Codegen.xlsx`. That spreadsheet is no longer needed.
- All legacy tables were converted mechanically. The legacy content frame was
  the user view rotated 90° counter-clockwise. The conversion was verified: on
  hw_rev2 (`TL-V`), every pixel of every table lights exactly the same LED as
  the old code with `MirrorMode=true`.
- **Deliberate change:** the default-build `Symbol_GND` table was a copy of the
  F table, so `Draw_GND()` drew an F. It now draws the ground symbol that the
  `CONFIG_15_20` branch intended.
- **Units that ran with `MirrorMode=false`** showed everything except R/E/F
  flipped left↔right compared to the `true` default. They now use the board
  default. Run `ledtest` if in doubt.
