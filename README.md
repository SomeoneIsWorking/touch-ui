# touch-ui

The on-screen controls a native game port shows on a phone, shared by every
port instead of redrawn per title.

It owns three things a port should not re-implement:

- **Placement.** Controls sit inside the window's safe area, sized from the
  display scale, so a 20:9 phone and a desktop window get the same physical
  button size. Direction controls tile a 3x3 grid, which is what makes a
  diagonal reachable without a second hit test.
- **Contact ownership.** A touch that begins inside a control is claimed by it.
  While the overlay is visible it consumes the touch, because a tap that also
  reaches the game is a click, and in a game that click fires the player's
  weapon. Actions are reference counted, so two fingers on one arrow keep it
  held until the last one lifts.
- **Crisp art.** Buttons are the shared `port-assets` touch-control SVG set,
  embedded at build time and rasterised at the size actually drawn. A title
  draws them after its frame at output resolution, so a 3x display gets 3x the
  pixels instead of an upscaled low-resolution frame.

## Consuming

CMake target `touch_ui::touch_ui` (C++20). A title supplies its own action bits
and receives transitions; the module never decides what an action means.

```cmake
add_subdirectory(<shared>/touch-ui ...)
target_link_libraries(your_game PRIVATE touch_ui::touch_ui)
```

Resolved like the other shared checkouts: `TOUCH_UI_LUCENT_DIR` (or an
already-added `lucent::lucent` target) and `PORT_ASSETS_DIR` (or a sibling
`../port-assets`).

```cpp
touch_ui::Config config;
config.controls = {
    {1, touch_ui::Placement::dpad_left, "direction_left", false, 1U << 2},
    {5, touch_ui::Placement::action_primary, "attack", true, 1U << 4},
};
touch_ui::Controls controls(config, [&](std::uint32_t action, bool down) {
  apply_my_action(action, down);
});

// Each frame, before presenting:
controls.set_geometry({window_pixels_w, window_pixels_h, display_scale, safe_area});
controls.present(renderer);          // over the game frame, at output resolution

// In the event pump:
if (controls.handle_event(&event)) continue;
```

A title that installs the overlay sets `SDL_HINT_TOUCH_MOUSE_EVENTS` to `0`: SDL
otherwise synthesises a mouse click from every touch, and that click reaches the
game no matter what the finger event did.

`controls.note_controller_input()` (a pad button or a stick push) steps the
overlay aside and `note_touch_input()` brings it back; a title calls the first
from its gamepad events and the second happens on the next touch.

## Verifier

`uv run --frozen python tools/verify.py` runs Python lint, the CMake build,
clang-tidy against the real compile database, and the CTest suite: placement
inside the safe area in both orientations, diagonal cells, reference-counted
actions across two fingers, touch claiming and controller-visibility policy, and
a rendered frame captured from the real SDL renderer so a change that stops
drawing fails.

`python3 tools/capture.py scratch/controls.png --width 2400 --height 1080`
writes the drawn result on a phone-shaped surface for eyeballing on a host that
cannot run the phone.
