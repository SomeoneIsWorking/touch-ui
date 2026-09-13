// controls.cpp — contact routing, pointer ownership, and crisp presentation.
//
// Two behaviours matter more than the drawing:
//
//   1. A touch that begins inside a control belongs to that control. While the
//      overlay is visible it consumes the touch instead of letting it reach the
//      game, because a tap that is also a click fires the player's weapon.
//   2. Actions are reference counted. Two fingers on one arrow keep it held
//      until the last finger lifts, instead of releasing on the first lift.
//
// The art is the shared port-assets SVG set, embedded at build time and
// rasterised at the size actually drawn, so a phone at 3x gets 3x the pixels
// rather than an upscaled copy of the game's low-resolution frame.
#include "touch_ui/touch_ui.h"

#include "embedded_icons.h"

#include <lucent/touch.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3_image/SDL_image.h>

namespace touch_ui {
namespace {

constexpr float kIconFraction = 0.62F;
constexpr float kDiscRadiusFraction = 0.46F;
constexpr float kRingWidthFraction = 0.06F;
constexpr int kCircleSegments = 40;

std::uint8_t scale_alpha(float alpha) {
  return static_cast<std::uint8_t>(std::lround(std::clamp(alpha, 0.0F, 1.0F) * 255.0F));
}

// One rasterised glyph, with the renderer and size it was made for. SDL owns a
// texture until its renderer goes away, so a glyph made for another renderer is
// replaced rather than destroyed here.
struct Glyph {
  SDL_Texture *texture = nullptr;
  SDL_Renderer *renderer = nullptr;
  int pixels = 0;
};

struct Captured {
  std::uint32_t zone_id = 0;
  bool active = false;
};

struct PointerState {
  std::int64_t finger = -1;
  Point position{};
  bool active = false;
};

void fill_circle(SDL_Renderer *renderer, SDL_FPoint centre, float radius, SDL_Color color) {
  if (radius <= 0.0F) {
    return;
  }
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  std::array<SDL_Vertex, static_cast<std::size_t>(kCircleSegments) + 2U> vertices{};
  std::array<int, static_cast<std::size_t>(kCircleSegments) * 3U> indices{};
  const SDL_FColor vertex_color{
      static_cast<float>(color.r) / 255.0F, static_cast<float>(color.g) / 255.0F,
      static_cast<float>(color.b) / 255.0F, static_cast<float>(color.a) / 255.0F};
  vertices[0].position = centre;
  vertices[0].color = vertex_color;
  vertices[0].tex_coord = SDL_FPoint{0.0F, 0.0F};
  const float step = 2.0F * SDL_PI_F / static_cast<float>(kCircleSegments);
  for (int index = 0; index < kCircleSegments; ++index) {
    const float angle = static_cast<float>(index) * step;
    vertices[static_cast<std::size_t>(index) + 1].position =
        SDL_FPoint{centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius};
    vertices[static_cast<std::size_t>(index) + 1].color = vertex_color;
    vertices[static_cast<std::size_t>(index) + 1].tex_coord = SDL_FPoint{0.0F, 0.0F};
    const int next = (index + 1) % kCircleSegments;
    indices[static_cast<std::size_t>(index) * 3U + 0U] = 0;
    indices[static_cast<std::size_t>(index) * 3U + 1U] = index + 1;
    indices[static_cast<std::size_t>(index) * 3U + 2U] = next + 1;
  }
  vertices[kCircleSegments + 1] = vertices[1];
  SDL_RenderGeometry(renderer, nullptr, vertices.data(), kCircleSegments + 2, indices.data(),
                     kCircleSegments * 3);
}

void stroke_circle(SDL_Renderer *renderer, SDL_FPoint centre, float radius, float width,
                   SDL_Color color) {
  if (radius <= 0.0F || width <= 0.0F) {
    return;
  }
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  const float inner = std::max(0.5F, radius - width);
  std::array<SDL_Vertex, static_cast<std::size_t>(kCircleSegments) * 2U> vertices{};
  std::array<int, static_cast<std::size_t>(kCircleSegments) * 6U> indices{};
  const SDL_FColor vertex_color{
      static_cast<float>(color.r) / 255.0F, static_cast<float>(color.g) / 255.0F,
      static_cast<float>(color.b) / 255.0F, static_cast<float>(color.a) / 255.0F};
  const float step = 2.0F * SDL_PI_F / static_cast<float>(kCircleSegments);
  for (int index = 0; index < kCircleSegments; ++index) {
    const float angle = static_cast<float>(index) * step;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    vertices[static_cast<std::size_t>(index) * 2U + 0U].position =
        SDL_FPoint{centre.x + cosine * inner, centre.y + sine * inner};
    vertices[static_cast<std::size_t>(index) * 2U + 0U].color = vertex_color;
    vertices[static_cast<std::size_t>(index) * 2U + 1U].position =
        SDL_FPoint{centre.x + cosine * radius, centre.y + sine * radius};
    vertices[static_cast<std::size_t>(index) * 2U + 1U].color = vertex_color;
    const int next = (index + 1) % kCircleSegments;
    indices[static_cast<std::size_t>(index) * 6U + 0U] = index * 2 + 0;
    indices[static_cast<std::size_t>(index) * 6U + 1U] = index * 2 + 1;
    indices[static_cast<std::size_t>(index) * 6U + 2U] = next * 2 + 1;
    indices[static_cast<std::size_t>(index) * 6U + 3U] = index * 2 + 0;
    indices[static_cast<std::size_t>(index) * 6U + 4U] = next * 2 + 1;
    indices[static_cast<std::size_t>(index) * 6U + 5U] = next * 2 + 0;
  }
  SDL_RenderGeometry(renderer, nullptr, vertices.data(), kCircleSegments * 2, indices.data(),
                     kCircleSegments * 6);
}

} // namespace

struct Controls::Impl {
  Config config;
  ActionSink actions;
  PointerSink pointer_sink;
  lucent::touch::Router router;
  Layout layout;
  Geometry geometry;
  std::unordered_map<std::uint32_t, int> action_references;
  std::unordered_map<std::int64_t, Captured> captured;
  std::unordered_map<std::string, Glyph> glyphs;
  PointerState pointer_state;
  std::uint32_t unavailable = 0;
  bool enabled = true;
  bool controller_last = false;

  Impl() = default;
  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  ~Impl() = default;

  [[nodiscard]] bool shows_controls() const {
    return enabled && !controller_last;
  }

  [[nodiscard]] const Zone *zone(std::uint32_t id) const {
    for (const Zone &candidate : layout.zones) {
      if (candidate.id == id) {
        return &candidate;
      }
    }
    return nullptr;
  }

  [[nodiscard]] bool point_in_control(float x, float y) const {
    for (const Zone &candidate : layout.zones) {
      if (x >= candidate.bounds.left && x <= candidate.bounds.right && y >= candidate.bounds.top &&
          y <= candidate.bounds.bottom) {
        return true;
      }
    }
    return false;
  }

  void set_action(std::uint32_t action, bool down) {
    int &references = action_references[action];
    const bool was_down = references > 0;
    references = down ? references + 1 : std::max(0, references - 1);
    const bool is_down = references > 0;
    if (was_down != is_down && actions) {
      actions(action, is_down);
    }
  }

  void release_all() {
    const std::vector<std::uint32_t> held = [this] {
      std::vector<std::uint32_t> bits;
      for (const auto &entry : action_references) {
        if (entry.second > 0) {
          bits.push_back(entry.first);
        }
      }
      return bits;
    }();
    action_references.clear();
    for (const std::uint32_t bit : held) {
      if (actions) {
        actions(bit, false);
      }
    }
    for (auto &entry : captured) {
      entry.second.active = false;
    }
    captured.clear();
    router.cancel();
    if (pointer_state.active) {
      pointer_state.active = false;
      pointer_sink_emit(pointer_state.position, 0);
    }
  }

  void pointer_sink_emit(Point position, int state) {
    if (pointer_sink_ready()) {
      pointer_sink(position, state);
    }
  }

  [[nodiscard]] bool pointer_sink_ready() const {
    return static_cast<bool>(pointer_sink);
  }

  void rebuild_layout() {
    layout = make_layout(config, geometry);
    std::vector<lucent::touch::Zone> zones;
    zones.reserve(layout.zones.size());
    for (const Zone &zone_entry : layout.zones) {
      const bool offered = shows_controls() && (zone_entry.actions & unavailable) == 0U;
      /* A negative priority tells the router not to capture this zone at all,
       * which is how a hidden or unavailable control stops claiming touches. */
      zones.push_back(lucent::touch::Zone{zone_entry.id, zone_entry.bounds.left,
                                          zone_entry.bounds.top, zone_entry.bounds.right,
                                          zone_entry.bounds.bottom, offered ? 0 : -1});
    }
    router.set_zones(zones);
  }

  void route(const lucent::touch::Contact &contact) {
    const std::span<const lucent::touch::Contact> batch(&contact, 1);
    for (const lucent::touch::Event &event : router.route(batch)) {
      const Zone *zone_entry = zone(event.zone_id);
      if (zone_entry == nullptr || (zone_entry->actions & unavailable) != 0U) {
        continue;
      }
      if (event.phase == lucent::touch::Phase::began) {
        captured[event.contact_id] = Captured{event.zone_id, true};
        set_action(zone_entry->actions, true);
      } else if (event.phase == lucent::touch::Phase::ended ||
                 event.phase == lucent::touch::Phase::canceled) {
        const auto found = captured.find(event.contact_id);
        if (found != captured.end() && found->second.active) {
          found->second.active = false;
          captured.erase(found);
          set_action(zone_entry->actions, false);
        }
      }
    }
  }

  SDL_Texture *glyph_texture(SDL_Renderer *renderer, const std::string &name, int pixels) {
    if (name.empty() || pixels <= 0) {
      return nullptr;
    }
    Glyph &glyph = glyphs[name];
    if (glyph.texture != nullptr && glyph.pixels == pixels && glyph.renderer == renderer) {
      return glyph.texture;
    }
    if (glyph.texture != nullptr && glyph.renderer == renderer) {
      SDL_DestroyTexture(glyph.texture);
    }
    glyph.texture = nullptr;
    const embedded_icons::Icon *icon = embedded_icons::find(name);
    if (icon == nullptr) {
      return nullptr;
    }
    SDL_IOStream *stream = SDL_IOFromConstMem(icon->bytes, icon->size);
    if (stream == nullptr) {
      return nullptr;
    }
    SDL_Surface *loaded = IMG_LoadSizedSVG_IO(stream, pixels, pixels);
    SDL_CloseIO(stream);
    if (loaded == nullptr) {
      return nullptr;
    }
    SDL_Surface *converted = loaded->format == SDL_PIXELFORMAT_RGBA32
                                 ? loaded
                                 : SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    if (converted != loaded) {
      SDL_DestroySurface(loaded);
    }
    if (converted == nullptr) {
      return nullptr;
    }
    glyph.texture = SDL_CreateTextureFromSurface(renderer, converted);
    glyph.renderer = renderer;
    glyph.pixels = pixels;
    SDL_DestroySurface(converted);
    return glyph.texture;
  }

  [[nodiscard]] bool pressed(std::uint32_t id) const {
    for (const auto &entry : captured) {
      if (entry.second.active && entry.second.zone_id == id) {
        return true;
      }
    }
    return false;
  }
};

Controls::Controls(Config config, ActionSink actions) : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(config);
  impl_->actions = std::move(actions);
  impl_->rebuild_layout();
}

Controls::~Controls() = default;

void Controls::set_pointer_sink(PointerSink sink) {
  impl_->pointer_sink = std::move(sink);
}

void Controls::set_geometry(const Geometry &geometry) {
  impl_->geometry = geometry;
  impl_->rebuild_layout();
}

const Layout &Controls::layout() const {
  return impl_->layout;
}

bool Controls::handle_event(const SDL_Event &event) {
  if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
      event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED) {
    return false;
  }
  if (!impl_->shows_controls()) {
    /* Hidden controls must not swallow input: the game owns the touch. */
    return false;
  }
  const float x = event.tfinger.x * static_cast<float>(impl_->geometry.output_width);
  const float y = event.tfinger.y * static_cast<float>(impl_->geometry.output_height);
  const auto finger = static_cast<std::int64_t>(event.tfinger.fingerID);
  if (event.type == SDL_EVENT_FINGER_DOWN) {
    note_touch_input();
    if (impl_->point_in_control(x, y)) {
      impl_->route(lucent::touch::Contact{finger, {x, y}, lucent::touch::Phase::began});
      return true;
    }
    /* Outside the controls: become the game's pointer rather than a tap that
     * the game would read as a click on whatever it happens to hold. */
    if (!impl_->pointer_state.active) {
      impl_->pointer_state = PointerState{finger, Point{x, y}, true};
      impl_->pointer_sink_emit(Point{x, y}, 1);
    }
    return true;
  }
  if (impl_->captured.count(finger) != 0) {
    const auto phase = event.type == SDL_EVENT_FINGER_UP         ? lucent::touch::Phase::ended
                       : event.type == SDL_EVENT_FINGER_CANCELED ? lucent::touch::Phase::canceled
                                                                 : lucent::touch::Phase::moved;
    impl_->route(lucent::touch::Contact{finger, {x, y}, phase});
    return true;
  }
  if (impl_->pointer_state.active && impl_->pointer_state.finger == finger) {
    impl_->pointer_state.position = Point{x, y};
    if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED) {
      impl_->pointer_state.active = false;
      impl_->pointer_sink_emit(Point{x, y}, 0);
    } else {
      impl_->pointer_sink_emit(Point{x, y}, -1);
    }
  }
  return true;
}

void Controls::set_unavailable_actions(std::uint32_t actions) {
  if (impl_->unavailable == actions) {
    return;
  }
  impl_->unavailable = actions;
  impl_->release_all();
  impl_->rebuild_layout();
}

void Controls::set_enabled(bool enabled) {
  if (impl_->enabled == enabled) {
    return;
  }
  impl_->enabled = enabled;
  if (!enabled) {
    impl_->release_all();
  }
  impl_->rebuild_layout();
}

bool Controls::enabled() const {
  return impl_->enabled;
}

void Controls::note_controller_input() {
  if (impl_->controller_last) {
    return;
  }
  impl_->controller_last = true;
  impl_->release_all();
  impl_->rebuild_layout();
}

void Controls::note_touch_input() {
  if (!impl_->controller_last) {
    return;
  }
  impl_->controller_last = false;
  impl_->rebuild_layout();
}

bool Controls::visible() const {
  return impl_->shows_controls() && impl_->geometry.output_width > 0 &&
         impl_->geometry.output_height > 0;
}

void Controls::cancel() {
  impl_->release_all();
}

void Controls::present(SDL_Renderer *renderer) {
  if (renderer == nullptr || !visible()) {
    return;
  }
  SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
  SDL_GetRenderDrawBlendMode(renderer, &previous_blend);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (const Visual &visual : impl_->layout.visuals) {
    if ((visual.actions & impl_->unavailable) != 0U) {
      continue;
    }
    const bool held = impl_->pressed(visual.id);
    const SDL_FRect bounds{visual.bounds.left, visual.bounds.top, visual.bounds.width(),
                           visual.bounds.height()};
    const SDL_FPoint centre{bounds.x + bounds.w * 0.5F, bounds.y + bounds.h * 0.5F};
    if (visual.disc) {
      const float radius = std::min(bounds.w, bounds.h) * kDiscRadiusFraction;
      fill_circle(renderer, centre, radius,
                  SDL_Color{20, 27, 38, scale_alpha(held ? 0.80F : 0.60F)});
      stroke_circle(renderer, centre, radius, std::max(1.5F, radius * kRingWidthFraction),
                    SDL_Color{242, 247, 250, scale_alpha(held ? 1.0F : 0.70F)});
    }
    const int pixels = std::max(8, static_cast<int>(bounds.w * kIconFraction));
    SDL_Texture *texture = impl_->glyph_texture(renderer, visual.icon, pixels);
    if (texture == nullptr) {
      continue;
    }
    const float side = static_cast<float>(pixels);
    const SDL_FRect destination{bounds.x + (bounds.w - side) * 0.5F,
                                bounds.y + (bounds.h - side) * 0.5F, side, side};
    SDL_SetTextureAlphaMod(texture, held ? 255 : 230);
    SDL_RenderTexture(renderer, texture, nullptr, &destination);
  }
  SDL_SetRenderDrawBlendMode(renderer, previous_blend);
}

} // namespace touch_ui
