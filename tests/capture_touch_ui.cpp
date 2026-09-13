// Writes the controls, drawn over a stand-in game frame, to a PNG. The port
// cannot be looked at on every host, so this puts the real renderer's output in
// front of whoever is judging sizes and reachability.
#include "touch_ui/touch_ui.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

namespace {

touch_ui::Config capture_config() {
  touch_ui::Config config;
  config.controls = {
      {1, touch_ui::Placement::dpad_up, "direction_up", false, 1U << 0},
      {2, touch_ui::Placement::dpad_down, "direction_down", false, 1U << 1},
      {3, touch_ui::Placement::dpad_left, "direction_left", false, 1U << 2},
      {4, touch_ui::Placement::dpad_right, "direction_right", false, 1U << 3},
      {5, touch_ui::Placement::action_primary, "attack", true, 1U << 4},
      {6, touch_ui::Placement::action_secondary, "use", true, 1U << 5},
      {7, touch_ui::Placement::top_right, "pause", true, 1U << 6},
  };
  return config;
}

void paint_backdrop(SDL_Renderer *renderer, int width, int height) {
  // A stand-in for a game frame: a sky-ish gradient with a ground band, so the
  // controls are judged over something other than flat black.
  for (int y = 0; y < height; ++y) {
    const float t = static_cast<float>(y) / static_cast<float>(height);
    SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(30 + 40 * t),
                           static_cast<Uint8>(60 + 70 * t), static_cast<Uint8>(120 + 60 * t), 255);
    SDL_RenderLine(renderer, 0.0F, static_cast<float>(y), static_cast<float>(width),
                   static_cast<float>(y));
  }
  SDL_SetRenderDrawColor(renderer, 40, 90, 50, 255);
  SDL_FRect ground{0.0F, static_cast<float>(height) * 0.72F, static_cast<float>(width),
                   static_cast<float>(height) * 0.28F};
  SDL_RenderFillRect(renderer, &ground);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 5) {
    std::fprintf(stderr, "usage: capture_touch_ui <png> <width> <height> <scale>\n");
    return 2;
  }
  const int width = std::atoi(argv[2]);
  const int height = std::atoi(argv[3]);
  const float scale = static_cast<float>(std::atof(argv[4]));
  if (width <= 0 || height <= 0) {
    std::fprintf(stderr, "capture: the surface must have a positive size\n");
    return 2;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "capture: SDL could not start: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
  SDL_Renderer *renderer =
      surface != nullptr ? SDL_CreateSoftwareRenderer(surface) : nullptr;
  if (renderer == nullptr) {
    std::fprintf(stderr, "capture: no renderer: %s\n", SDL_GetError());
    return 1;
  }
  touch_ui::Controls controls(capture_config(), [](std::uint32_t, bool) {});
  touch_ui::Geometry geometry;
  geometry.output_width = width;
  geometry.output_height = height;
  geometry.display_scale = scale;
  geometry.safe = touch_ui::Rect{0.0F, 0.0F, static_cast<float>(width),
                                 static_cast<float>(height)};
  controls.set_geometry(geometry);

  paint_backdrop(renderer, width, height);
  controls.present(renderer);

  SDL_Surface *captured = SDL_RenderReadPixels(renderer, nullptr);
  const bool saved = captured != nullptr && SDL_SaveBMP(captured, argv[1]);
  if (!saved) {
    std::fprintf(stderr, "capture: could not write %s: %s\n", argv[1], SDL_GetError());
  }
  if (captured != nullptr) {
    SDL_DestroySurface(captured);
  }
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(surface);
  SDL_Quit();
  return saved ? 0 : 1;
}
