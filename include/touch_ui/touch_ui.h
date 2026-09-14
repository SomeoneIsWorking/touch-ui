// touch_ui.h — in-game touch controls for native game ports.
//
// A port that ships on a phone needs on-screen controls. This owns the parts
// every such port shares: where the controls sit (from the window's safe area
// and display scale), which contact belongs to which control, and how they are
// drawn crisply at the output resolution. It never decides what an action
// means: the title supplies a bit for each control and receives transitions.
//
//   touch_ui::Controls controls(config, [&](std::uint32_t action, bool down) {
//     game_apply_action(action, down);
//   });
//   controls.set_geometry({output_width, output_height, scale, safe_area});
//   ...
//   if (controls.handle_event(&event)) continue;   // a claimed touch is not a tap
//   ...
//   controls.present(renderer);                     // over the presented frame
//
// Art comes from the shared port-assets touch-controls set: the SVG bytes are
// embedded at build time and rasterised at the size actually drawn, so the
// buttons stay sharp on any display instead of being upscaled from the game's
// low-resolution frame buffer.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace touch_ui {

struct Rect {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;

  [[nodiscard]] float width() const {
    return right - left;
  }
  [[nodiscard]] float height() const {
    return bottom - top;
  }
};

struct Point {
  float x = 0.0F;
  float y = 0.0F;
};

// Where a control sits inside the safe area. Direction placements follow the
// convention every d-pad uses; the action placements are the three buttons a
// thumb rests on, and the two top corners are the reachable places for a
// control a thumb would otherwise cover (pause, and a view toggle).
enum class Placement : std::uint8_t {
  dpad_up,
  dpad_up_right,
  dpad_right,
  dpad_down_right,
  dpad_down,
  dpad_down_left,
  dpad_left,
  dpad_up_left,
  action_primary,
  action_secondary,
  action_tertiary,
  top_right,
  top_left,
};

// One control the title asks for.
//   - `id` is the title's own identifier (any value; only uniqueness matters).
//   - `icon` names a glyph in port-assets' touch-controls set.
//   - `disc` draws a generated circular backing behind the glyph. The direction
//     glyphs carry their own button shape; the action glyphs are silhouettes.
//   - `actions` are the title's action bits this control asserts while held.
struct Control {
  std::uint32_t id = 0;
  Placement placement = Placement::action_primary;
  std::string icon;
  bool disc = true;
  std::uint32_t actions = 0;
};

struct Config {
  std::vector<Control> controls;
  // Extra space between a control and the safe-area edge, as a fraction of the
  // control unit. Zero keeps the default thumb margin.
  float edge_fraction = 0.32F;
};

// The geometry of the surface the controls are drawn into, in output pixels.
// `safe` excludes display cutouts and system bars; pass the whole output when
// the platform has none.
struct Geometry {
  int output_width = 0;
  int output_height = 0;
  float display_scale = 1.0F;
  Rect safe{};
};

// A control's hit region and drawn rectangle for one geometry.
struct Zone {
  std::uint32_t id = 0;
  Rect bounds{};
  std::uint32_t actions = 0;
};

struct Visual {
  std::uint32_t id = 0;
  Rect bounds{};
  std::string icon;
  bool disc = true;
  std::uint32_t actions = 0;
};

struct Layout {
  std::vector<Zone> zones;
  std::vector<Visual> visuals;
  float unit = 0.0F;
};

// Pure geometry: the same inputs always give the same layout, which is what
// lets a test check that zones and visuals agree.
[[nodiscard]] Layout make_layout(const Config &config, const Geometry &geometry);

// Action transitions the title applies, and pointer positions it may use for
// drag gestures. `state` is 1 on press, 0 on release, -1 while moving.
using ActionSink = std::function<void(std::uint32_t action, bool down)>;
using PointerSink = std::function<void(Point position, int state)>;

class Controls {
public:
  Controls(Config config, ActionSink actions);
  ~Controls();
  Controls(const Controls &) = delete;
  Controls &operator=(const Controls &) = delete;

  void set_pointer_sink(PointerSink sink);
  void set_geometry(const Geometry &geometry);
  [[nodiscard]] const Layout &layout() const;

  // True when the event belonged to the controls. A touch that begins inside a
  // control is claimed by it: the same finger can never also reach the game as
  // a tap, and a drag that leaves the button keeps feeding that control.
  bool handle_event(const SDL_Event &event);

  // Actions the controls must not offer (a scheme that has no such input).
  void set_unavailable_actions(std::uint32_t actions);

  // Overlay visibility. Disabled controls neither draw nor consume input; the
  // title keeps the setting, the module only obeys it.
  void set_enabled(bool enabled);
  [[nodiscard]] bool enabled() const;

  // Presentation policy: the overlay steps aside while a physical controller is
  // being used and returns on the next touch.
  void note_controller_input();
  void note_touch_input();
  [[nodiscard]] bool visible() const;

  // Release every held action and drop any claimed pointer (focus loss, pause).
  void cancel();

  // Draw over the presented frame at the output resolution.
  void present(SDL_Renderer *renderer);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace touch_ui
