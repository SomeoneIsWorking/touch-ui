# Project goals

## G001 — One on-screen control layer for every port

Give every port the same touch controls: correct under a finger, correct on
every display, and crisp enough to look like the rest of a modern UI.

Success conditions:

- A touch inside a control belongs to that control and never also reaches the
  game as a tap, click, or press.
- Two fingers on one control hold it until the last lifts; a drag out of a
  button keeps feeding the button it started on.
- Controls stay inside the safe area in either orientation and keep a physical
  size that suits a thumb on a phone and a window on a desktop.
- Art comes from the shared `port-assets` SVG set, drawn at the output
  resolution, with no per-title copy and no bundled bitmap.
- An action's meaning stays with the title; this module only routes action bits.

## Non-goals

- No gameplay meaning: the module never maps an action to a game behaviour.
- No settings persistence: the title owns whether controls are enabled.
- No gesture recognisers beyond control claiming and a pointer passthrough.
