# Project state

## Comparison baseline

Each port drew its own on-screen controls: circles and arrows composited into
the game's low-resolution frame buffer, with hit regions written as fractions of
the whole screen and no ownership of the touches they consumed. The result was a
blocky overlay whose buttons overlapped the player's own taps, and a tap on a
direction also fired the player's weapon.

## Current focus

Benefactor consumes touch-ui for its Android product; LF2's equivalent controls
are the next consumer to migrate.

## Capability inventory

| ID | Capability or outcome | State | Evidence or exact gap | Goals |
| --- | --- | --- | --- | --- |
| S001 | Controls are placed inside the safe area at a size derived from the surface and display scale | verified | `layout_stays_inside_the_safe_area_and_scales_with_the_surface` (landscape, portrait, insets); drawn capture at 2400x1080 | G001 |
| S002 | A diagonal press asserts both directions from one hit region | verified | `a_diagonal_cell_asserts_both_directions`; eight direction cells | G001 |
| S003 | A touch inside a control is claimed and never also reaches the game | verified | `a_touch_outside_the_controls_is_not_a_game_click`, plus Benefactor's Android run with SDL touch-to-mouse synthesis disabled | G001 |
| S004 | Two fingers on one control hold it until the last lifts | verified | `actions_are_reference_counted_across_two_fingers` | G001 |
| S005 | The overlay steps aside while a controller is used and returns on the next touch | verified | `a_touch_outside_the_controls_is_not_a_game_click` (visibility policy) | G001 |
| S006 | Buttons use the shared SVG art, rasterised at the drawn size | verified | `the_controls_are_drawn_into_the_frame`: real SDL renderer, `IMG_LoadSizedSVG_IO`, drawn-pixel and press-feedback assertions | G001 |
| S007 | Art is embedded at build time from the shared checkout | verified | `tools/embed_svg.py` with `set.json` completeness check; no runtime asset path | G001 |
| S008 | LF2's existing touch controls are migrated onto this module | missing | LF2 still owns `runtime/input/touch_*.h` and `runtime/ui/touch_controls.c` | G001 |

## Not applicable here

- Action meaning, control-set choice, and settings persistence belong to each
  consuming title and are recorded in its own state inventory.
