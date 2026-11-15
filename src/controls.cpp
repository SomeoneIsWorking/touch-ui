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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3_image/SDL_image.h>

namespace touch_ui {
namespace {

constexpr float kIconFraction = 0.72F;
//: A held control grows a little and lights up, so the finger's own shadow does
//: not decide whether the press registered.
constexpr float kHeldIconFraction = 0.78F;
constexpr float kDiscRadiusFraction = 0.46F;
constexpr float kHeldDiscRadiusFraction = 0.49F;
constexpr float kRingWidthFraction = 0.06F;
constexpr int kCircleSegments = 40;
constexpr float kIdleIconAlpha = 235.0F / 255.0F;

// One rasterised glyph: the painter's image, the painter it belongs to and the
// size it was rasterised at.
struct Glyph {
  std::uintptr_t image = 0;
  Painter *painter = nullptr;
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

Vertex vertex_at(Point position, SDL_Color color) {
  return Vertex{position,
                static_cast<float>(color.r) / 255.0F,
                static_cast<float>(color.g) / 255.0F,
                static_cast<float>(color.b) / 255.0F,
                static_cast<float>(color.a) / 255.0F,
                Point{}};
}

void fill_circle(Painter &painter, Point centre, float radius, SDL_Color color) {
  if (radius <= 0.0F) {
    return;
  }
  std::array<Vertex, static_cast<std::size_t>(kCircleSegments) + 1U> vertices{};
  std::array<int, static_cast<std::size_t>(kCircleSegments) * 3U> indices{};
  vertices[0] = vertex_at(centre, color);
  const float step = 2.0F * SDL_PI_F / static_cast<float>(kCircleSegments);
  for (int index = 0; index < kCircleSegments; ++index) {
    const float angle = static_cast<float>(index) * step;
    vertices[static_cast<std::size_t>(index) + 1U] = vertex_at(
        Point{centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius}, color);
    const int next = (index + 1) % kCircleSegments;
    indices[static_cast<std::size_t>(index) * 3U + 0U] = 0;
    indices[static_cast<std::size_t>(index) * 3U + 1U] = index + 1;
    indices[static_cast<std::size_t>(index) * 3U + 2U] = next + 1;
  }
  painter.draw(0, vertices, indices);
}

void stroke_circle(Painter &painter, Point centre, float radius, float width, SDL_Color color) {
  if (radius <= 0.0F || width <= 0.0F) {
    return;
  }
  const float inner = std::max(0.5F, radius - width);
  std::array<Vertex, static_cast<std::size_t>(kCircleSegments) * 2U> vertices{};
  std::array<int, static_cast<std::size_t>(kCircleSegments) * 6U> indices{};
  const float step = 2.0F * SDL_PI_F / static_cast<float>(kCircleSegments);
  for (int index = 0; index < kCircleSegments; ++index) {
    const float angle = static_cast<float>(index) * step;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    vertices[static_cast<std::size_t>(index) * 2U + 0U] =
        vertex_at(Point{centre.x + cosine * inner, centre.y + sine * inner}, color);
    vertices[static_cast<std::size_t>(index) * 2U + 1U] =
        vertex_at(Point{centre.x + cosine * radius, centre.y + sine * radius}, color);
    const int next = (index + 1) % kCircleSegments;
    indices[static_cast<std::size_t>(index) * 6U + 0U] = index * 2 + 0;
    indices[static_cast<std::size_t>(index) * 6U + 1U] = index * 2 + 1;
    indices[static_cast<std::size_t>(index) * 6U + 2U] = next * 2 + 1;
    indices[static_cast<std::size_t>(index) * 6U + 3U] = index * 2 + 0;
    indices[static_cast<std::size_t>(index) * 6U + 4U] = next * 2 + 1;
    indices[static_cast<std::size_t>(index) * 6U + 5U] = next * 2 + 0;
  }
  painter.draw(0, vertices, indices);
}

void draw_image(Painter &painter, std::uintptr_t image, Rect bounds, float alpha) {
  const std::array<Vertex, 4> vertices{
      Vertex{Point{bounds.left, bounds.top}, 1.0F, 1.0F, 1.0F, alpha, Point{0.0F, 0.0F}},
      Vertex{Point{bounds.right, bounds.top}, 1.0F, 1.0F, 1.0F, alpha, Point{1.0F, 0.0F}},
      Vertex{Point{bounds.right, bounds.bottom}, 1.0F, 1.0F, 1.0F, alpha, Point{1.0F, 1.0F}},
      Vertex{Point{bounds.left, bounds.bottom}, 1.0F, 1.0F, 1.0F, alpha, Point{0.0F, 1.0F}}};
  const std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
  painter.draw(image, vertices, indices);
}

// An SDL_Renderer as a painter: images are textures of that renderer.
class RendererPainter final : public Painter {
public:
  explicit RendererPainter(SDL_Renderer *renderer) : renderer_(renderer) {
  }

  std::uintptr_t upload(std::span<const std::uint8_t> rgba, int side) override {
    SDL_Texture *texture =
        SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, side, side);
    if (texture == nullptr) {
      return 0;
    }
    SDL_UpdateTexture(texture, nullptr, rgba.data(), side * 4);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    textures_.push_back(texture);
    return textures_.size();
  }

  void release(std::uintptr_t image) override {
    SDL_DestroyTexture(textures_[image - 1U]);
    textures_[image - 1U] = nullptr;
  }

  void draw(std::uintptr_t image, std::span<const Vertex> vertices,
            std::span<const int> indices) override {
    converted_.resize(vertices.size());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
      const Vertex &from = vertices[index];
      converted_[index] = SDL_Vertex{SDL_FPoint{from.position.x, from.position.y},
                                     SDL_FColor{from.red, from.green, from.blue, from.alpha},
                                     SDL_FPoint{from.uv.x, from.uv.y}};
    }
    SDL_RenderGeometry(renderer_, image == 0 ? nullptr : textures_[image - 1U], converted_.data(),
                       static_cast<int>(converted_.size()), indices.data(),
                       static_cast<int>(indices.size()));
  }

private:
  SDL_Renderer *renderer_;
  // Handle n is textures_[n - 1].
  std::vector<SDL_Texture *> textures_;
  std::vector<SDL_Vertex> converted_;
};

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
  std::unordered_map<SDL_Renderer *, std::unique_ptr<RendererPainter>> renderer_painters;
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

  std::uintptr_t glyph_image(Painter &painter, const std::string &name, int pixels) {
    if (name.empty() || pixels <= 0) {
      return 0;
    }
    Glyph &glyph = glyphs[name];
    if (glyph.image != 0 && glyph.pixels == pixels && glyph.painter == &painter) {
      return glyph.image;
    }
    if (glyph.image != 0 && glyph.painter == &painter) {
      painter.release(glyph.image);
    }
    glyph.image = 0;
    const embedded_icons::Icon *icon = embedded_icons::find(name);
    if (icon == nullptr) {
      return 0;
    }
    SDL_IOStream *stream = SDL_IOFromConstMem(icon->bytes, icon->size);
    if (stream == nullptr) {
      return 0;
    }
    SDL_Surface *loaded = IMG_LoadSizedSVG_IO(stream, pixels, pixels);
    SDL_CloseIO(stream);
    if (loaded == nullptr) {
      return 0;
    }
    SDL_Surface *converted = loaded->format == SDL_PIXELFORMAT_RGBA32
                                 ? loaded
                                 : SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    if (converted != loaded) {
      SDL_DestroySurface(loaded);
    }
    if (converted == nullptr) {
      return 0;
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(pixels) *
                                   static_cast<std::size_t>(pixels) * 4U);
    for (int row = 0; row < pixels; ++row) {
      const auto *source = static_cast<const std::uint8_t *>(converted->pixels) +
                           static_cast<std::ptrdiff_t>(row) * converted->pitch;
      std::copy_n(source, static_cast<std::size_t>(pixels) * 4U,
                  rgba.begin() + static_cast<std::ptrdiff_t>(row) * pixels * 4);
    }
    SDL_DestroySurface(converted);
    glyph.image = painter.upload(rgba, pixels);
    glyph.painter = &painter;
    glyph.pixels = pixels;
    return glyph.image;
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
  if (renderer == nullptr) {
    return;
  }
  SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
  SDL_GetRenderDrawBlendMode(renderer, &previous_blend);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  // The glyphs belong to this renderer for as long as it lives; one painter per renderer keeps
  // them.
  auto &painter = impl_->renderer_painters[renderer];
  if (!painter) {
    painter = std::make_unique<RendererPainter>(renderer);
  }
  present(*painter);
  SDL_SetRenderDrawBlendMode(renderer, previous_blend);
}

void Controls::present(Painter &painter) {
  if (!visible()) {
    return;
  }
  for (const Visual &visual : impl_->layout.visuals) {
    if ((visual.actions & impl_->unavailable) != 0U) {
      continue;
    }
    const bool held = impl_->pressed(visual.id);
    const Rect &bounds = visual.bounds;
    const Point centre{bounds.left + bounds.width() * 0.5F, bounds.top + bounds.height() * 0.5F};
    const float radius = std::min(bounds.width(), bounds.height()) *
                         (held ? kHeldDiscRadiusFraction : kDiscRadiusFraction);
    if (visual.disc) {
      /* Held: the accent fill the rest of the port's UI uses, at ring brightness.
       * Idle: a translucent slate so the game stays readable through it. */
      fill_circle(painter, centre, radius,
                  held ? SDL_Color{53, 81, 106, 240} : SDL_Color{20, 27, 38, 153});
    }
    /* The ring is drawn for every control, including the ones whose art carries
     * its own button: a direction glyph's circle sits just inside it, so a held
     * direction lights a gold halo instead of only brightening its arrow. */
    stroke_circle(painter, centre, radius,
                  std::max(1.5F, radius * (held ? kRingWidthFraction * 1.5F : kRingWidthFraction)),
                  held ? SDL_Color{255, 214, 90, 255} : SDL_Color{242, 247, 250, 178});
    const int pixels =
        std::max(8, static_cast<int>(bounds.width() * (held ? kHeldIconFraction : kIconFraction)));
    const std::uintptr_t image = impl_->glyph_image(painter, visual.icon, pixels);
    if (image == 0) {
      continue;
    }
    const float side = static_cast<float>(pixels);
    const float left = bounds.left + (bounds.width() - side) * 0.5F;
    const float top = bounds.top + (bounds.height() - side) * 0.5F;
    draw_image(painter, image, Rect{left, top, left + side, top + side},
               held ? 1.0F : kIdleIconAlpha);
  }
}

} // namespace touch_ui
