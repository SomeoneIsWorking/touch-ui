// The shipping implementation, exercised where it is used: geometry that places
// the controls inside the safe area, contact routing that keeps a touch from
// becoming a tap, reference-counted actions, and a rendered frame captured from
// the real SDL renderer so a style change that stops drawing fails here.
#include "touch_ui/touch_ui.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace {

int g_failures = 0;

void check(bool condition, const char *what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL %s\n", what);
    ++g_failures;
  }
}

touch_ui::Config test_config() {
  touch_ui::Config config;
  const std::uint32_t up = 1U << 0;
  const std::uint32_t down = 1U << 1;
  const std::uint32_t left = 1U << 2;
  const std::uint32_t right = 1U << 3;
  const std::uint32_t fire = 1U << 4;
  const std::uint32_t interact = 1U << 5;
  config.controls = {
      {1, touch_ui::Placement::dpad_up, "direction_up", false, up},
      {2, touch_ui::Placement::dpad_down, "direction_down", false, down},
      {3, touch_ui::Placement::dpad_left, "direction_left", false, left},
      {4, touch_ui::Placement::dpad_right, "direction_right", false, right},
      {5, touch_ui::Placement::action_primary, "attack", true, fire},
      {6, touch_ui::Placement::action_secondary, "use", true, interact},
      {7, touch_ui::Placement::top_right, "pause", true, 1U << 6},
  };
  return config;
}

touch_ui::Geometry phone_landscape() {
  touch_ui::Geometry geometry;
  geometry.output_width = 2400;
  geometry.output_height = 1080;
  geometry.display_scale = 3.0F;
  geometry.safe = touch_ui::Rect{0.0F, 0.0F, 2400.0F, 1080.0F};
  return geometry;
}

void layout_stays_inside_the_safe_area_and_scales_with_the_surface() {
  touch_ui::Config config = test_config();
  const touch_ui::Layout phone = touch_ui::make_layout(config, phone_landscape());
  check(phone.unit >= 56.0F && phone.unit <= 160.0F, "unit is clamped to a usable size");
  check(phone.visuals.size() == 7, "every configured control draws once");
  for (const touch_ui::Zone &zone : phone.zones) {
    check(zone.bounds.left >= 0.0F && zone.bounds.top >= 0.0F, "zones start inside the surface");
    check(zone.bounds.right <= 2400.0F && zone.bounds.bottom <= 1080.0F,
          "zones end inside the surface");
  }
  // A phone held the other way round must not push the controls off screen.
  touch_ui::Geometry portrait = phone_landscape();
  portrait.output_width = 1080;
  portrait.output_height = 2400;
  portrait.safe = touch_ui::Rect{0.0F, 0.0F, 1080.0F, 2400.0F};
  const touch_ui::Layout tall = touch_ui::make_layout(config, portrait);
  for (const touch_ui::Zone &zone : tall.zones) {
    check(zone.bounds.right <= 1080.0F && zone.bounds.bottom <= 2400.0F,
          "portrait zones stay on screen");
  }
  // Insets move the controls away from the cutout rather than hiding them.
  touch_ui::Geometry inset = phone_landscape();
  inset.safe = touch_ui::Rect{90.0F, 40.0F, 2310.0F, 1040.0F};
  const touch_ui::Layout inset_layout = touch_ui::make_layout(config, inset);
  for (const touch_ui::Zone &zone : inset_layout.zones) {
    check(zone.bounds.left >= 90.0F && zone.bounds.top >= 40.0F,
          "an inset moves the controls inward");
    check(zone.bounds.right <= 2310.0F && zone.bounds.bottom <= 1040.0F,
          "an inset keeps the controls reachable");
  }
}

void a_diagonal_cell_asserts_both_directions() {
  touch_ui::Config config = test_config();
  const touch_ui::Layout layout = touch_ui::make_layout(config, phone_landscape());
  bool found_diagonal = false;
  for (const touch_ui::Zone &zone : layout.zones) {
    const std::uint32_t direction_bits = zone.actions & 0x0FU;
    if (direction_bits != 0 && (direction_bits & (direction_bits - 1U)) != 0) {
      found_diagonal = true;
    }
  }
  check(found_diagonal, "a diagonal cell exists and asserts two directions");
  constexpr std::uint32_t kDirectionBits = 0x0FU;
  std::size_t direction_zones = 0;
  for (const touch_ui::Zone &zone : layout.zones) {
    if ((zone.actions & kDirectionBits) != 0U) {
      ++direction_zones;
    }
  }
  check(direction_zones == 8, "the eight direction cells are all hit regions");
}

void actions_are_reference_counted_across_two_fingers() {
  std::vector<std::pair<std::uint32_t, bool>> transitions;
  touch_ui::Controls controls(test_config(), [&transitions](std::uint32_t action, bool down) {
    transitions.emplace_back(action, down);
  });
  controls.set_geometry(phone_landscape());

  const touch_ui::Layout &layout = controls.layout();
  const touch_ui::Zone *up = nullptr;
  for (const touch_ui::Zone &zone : layout.zones) {
    if (zone.actions == 1U) {
      up = &zone;
      break;
    }
  }
  check(up != nullptr, "the up zone exists");
  if (up == nullptr) {
    return;
  }
  const float x = (up->bounds.left + up->bounds.right) * 0.5F / 2400.0F;
  const float y = (up->bounds.top + up->bounds.bottom) * 0.5F / 1080.0F;

  SDL_Event first{};
  first.type = SDL_EVENT_FINGER_DOWN;
  first.tfinger.fingerID = 1;
  first.tfinger.x = x;
  first.tfinger.y = y;
  check(controls.handle_event(first), "a touch inside a control is claimed");

  SDL_Event second = first;
  second.tfinger.fingerID = 2;
  controls.handle_event(second);
  check(transitions.size() == 1 && transitions[0] == std::make_pair(1U, true),
        "a second finger does not re-press the same action");

  SDL_Event lift = second;
  lift.type = SDL_EVENT_FINGER_UP;
  controls.handle_event(lift);
  check(transitions.size() == 1, "the action stays held while one finger remains");

  lift.tfinger.fingerID = 1;
  controls.handle_event(lift);
  check(transitions.size() == 2 && transitions[1] == std::make_pair(1U, false),
        "the action releases when the last finger lifts");
}

void a_touch_outside_the_controls_is_not_a_game_click() {
  std::vector<std::pair<float, float>> pointers;
  touch_ui::Controls controls(test_config(), [](std::uint32_t, bool) {
  });
  controls.set_geometry(phone_landscape());
  controls.set_pointer_sink([&pointers](touch_ui::Point position, int state) {
    if (state == 1) {
      pointers.emplace_back(position.x, position.y);
    }
  });

  SDL_Event outside{};
  outside.type = SDL_EVENT_FINGER_DOWN;
  outside.tfinger.fingerID = 3;
  outside.tfinger.x = 0.5F;
  outside.tfinger.y = 0.5F;
  check(controls.handle_event(outside), "a touch outside the controls is still consumed");
  check(pointers.size() == 1, "the touch becomes a pointer gesture for the game");

  // A hidden overlay must not swallow the game's own input.
  controls.set_enabled(false);
  check(!controls.handle_event(outside), "a disabled overlay lets the touch through");
  controls.set_enabled(true);

  // Using a controller steps the overlay aside, and touching brings it back.
  controls.note_controller_input();
  check(!controls.visible(), "controller input hides the overlay");
  check(!controls.handle_event(outside), "a hidden overlay lets the touch through");
  controls.note_touch_input();
  check(controls.visible(), "touching restores the overlay");
}

std::vector<std::uint32_t> read_back(SDL_Renderer *renderer, int width, int height) {
  SDL_Surface *captured = SDL_RenderReadPixels(renderer, nullptr);
  if (captured == nullptr) {
    check(false, "the frame can be read back");
    return {};
  }
  SDL_Surface *converted = captured->format == SDL_PIXELFORMAT_ARGB8888
                               ? captured
                               : SDL_ConvertSurface(captured, SDL_PIXELFORMAT_ARGB8888);
  std::vector<std::uint32_t> pixels;
  if (converted != nullptr) {
    pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row) {
      const auto *source =
          static_cast<const unsigned char *>(converted->pixels) +
          static_cast<std::size_t>(row) * static_cast<std::size_t>(converted->pitch);
      const auto *row_pixels = reinterpret_cast<const std::uint32_t *>(source);
      for (int column = 0; column < width; ++column) {
        pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(column)] = row_pixels[column];
      }
    }
  }
  if (converted != captured) {
    SDL_DestroySurface(converted);
  }
  SDL_DestroySurface(captured);
  return pixels;
}

std::vector<std::uint32_t> render_frame(touch_ui::Controls &controls, int width, int height) {
  SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
  check(surface != nullptr, "the capture surface exists");
  if (surface == nullptr) {
    return {};
  }
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
  check(renderer != nullptr, "the offscreen renderer exists");
  if (renderer == nullptr) {
    SDL_DestroySurface(surface);
    return {};
  }
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  controls.present(renderer);
  const std::vector<std::uint32_t> pixels = read_back(renderer, width, height);
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(surface);
  return pixels;
}

std::size_t drawn_pixels(const std::vector<std::uint32_t> &pixels) {
  std::size_t drawn = 0;
  for (const std::uint32_t pixel : pixels) {
    if ((pixel & 0x00FFFFFFU) != 0U) {
      ++drawn;
    }
  }
  return drawn;
}

void the_controls_are_drawn_into_the_frame() {
  touch_ui::Controls controls(test_config(), [](std::uint32_t, bool) {
  });
  touch_ui::Geometry geometry = phone_landscape();
  geometry.output_width = 960;
  geometry.output_height = 432;
  controls.set_geometry(geometry);

  const std::vector<std::uint32_t> before = render_frame(controls, 960, 432);
  const std::size_t controls_drawn = drawn_pixels(before);
  check(controls_drawn > 2000, "the controls cover a real part of the frame");

  // The same frame with the overlay hidden must be empty: the capture is
  // measuring the controls, not the renderer's clear colour.
  controls.set_enabled(false);
  const std::vector<std::uint32_t> hidden = render_frame(controls, 960, 432);
  check(drawn_pixels(hidden) == 0, "a hidden overlay draws nothing");
  controls.set_enabled(true);

  // Pressing a control changes what is drawn over that control.
  const touch_ui::Layout &layout = controls.layout();
  check(!layout.zones.empty(), "the layout has zones");
  if (!layout.zones.empty()) {
    const touch_ui::Zone &zone = layout.zones.front();
    SDL_Event press{};
    press.type = SDL_EVENT_FINGER_DOWN;
    press.tfinger.fingerID = 7;
    press.tfinger.x = (zone.bounds.left + zone.bounds.right) * 0.5F / 960.0F;
    press.tfinger.y = (zone.bounds.top + zone.bounds.bottom) * 0.5F / 432.0F;
    check(controls.handle_event(press), "the press is claimed");
    const std::vector<std::uint32_t> pressed = render_frame(controls, 960, 432);
    std::size_t changed = 0;
    for (std::size_t index = 0; index < before.size() && index < pressed.size(); ++index) {
      if (before[index] != pressed[index]) {
        ++changed;
      }
    }
    check(changed > 100, "pressing a control visibly changes it");
  }
}

} // namespace

int main() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL could not start: %s\n", SDL_GetError());
    return 1;
  }
  layout_stays_inside_the_safe_area_and_scales_with_the_surface();
  a_diagonal_cell_asserts_both_directions();
  actions_are_reference_counted_across_two_fingers();
  a_touch_outside_the_controls_is_not_a_game_click();
  the_controls_are_drawn_into_the_frame();
  SDL_Quit();

  if (g_failures == 0) {
    std::fputs("all touch-ui tests passed\n", stdout);
    return 0;
  }
  std::fprintf(stderr, "%d failure(s)\n", g_failures);
  return 1;
}
