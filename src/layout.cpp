// layout.cpp — pure placement of the controls inside the safe area.
//
// The unit comes from the surface rather than from a fraction of the whole
// screen, so a control keeps its physical size on a 20:9 phone and in a desktop
// window; it is then clamped so it is never a speck or a dinner plate. Direction
// controls tile a 3x3 grid: the four cardinal cells draw the arrow art and the
// corners are reachable, so a diagonal press walks and turns at once instead of
// needing a differently shaped hit test.
#include "touch_ui/touch_ui.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace touch_ui {
namespace {

constexpr float kUnitFraction = 0.17F;
constexpr float kMinUnit = 60.0F;
constexpr float kMaxUnit = 170.0F;
constexpr float kPauseScale = 0.9F;
//: Corner cells have no control of their own; their zone ids start here.
constexpr std::uint32_t kCornerIdBase = 0x1000U;

bool is_direction(Placement placement) {
  return placement <= Placement::dpad_up_left;
}

struct Cell {
  int column;
  int row;
  Placement placement;
};

// The eight direction cells. A corner press asserts both directions it sits
// between; a cardinal press asserts only that direction.
constexpr std::array<Cell, 8> kDirectionCells = {{
    {0, 0, Placement::dpad_up_left},
    {1, 0, Placement::dpad_up},
    {2, 0, Placement::dpad_up_right},
    {0, 1, Placement::dpad_left},
    {2, 1, Placement::dpad_right},
    {0, 2, Placement::dpad_down_left},
    {1, 2, Placement::dpad_down},
    {2, 2, Placement::dpad_down_right},
}};

Placement cardinal_for_column(int column) {
  return column == 0 ? Placement::dpad_left : Placement::dpad_right;
}

Placement cardinal_for_row(int row) {
  return row == 0 ? Placement::dpad_up : Placement::dpad_down;
}

bool is_corner(const Cell &cell) {
  return cell.column != 1 && cell.row != 1;
}

Rect rect_at(float left, float top, float size) {
  return {left, top, left + size, top + size};
}

struct ActionSlot {
  float column;
  float row;
};

// The three action buttons sit on a shallow diamond a thumb sweeps across:
// primary at the outer right, secondary below-left of it, tertiary above it.
ActionSlot action_slot(Placement placement) {
  switch (placement) {
  case Placement::action_primary:
    return {2.1F, 0.72F};
  case Placement::action_secondary:
    return {1.05F, 1.62F};
  default:
    return {0.0F, 0.72F};
  }
}

} // namespace

Layout make_layout(const Config &config, const Geometry &geometry) {
  Layout layout;
  const float surface_width = static_cast<float>(std::max(1, geometry.output_width));
  const float surface_height = static_cast<float>(std::max(1, geometry.output_height));
  /* A stale or unset safe area must never push a control off the surface: the
   * layout clamps it to what is actually drawn. */
  Rect safe = geometry.safe;
  if (safe.width() <= 0.0F || safe.height() <= 0.0F) {
    safe = Rect{0.0F, 0.0F, surface_width, surface_height};
  }
  safe.left = std::clamp(safe.left, 0.0F, surface_width);
  safe.top = std::clamp(safe.top, 0.0F, surface_height);
  safe.right = std::clamp(safe.right, safe.left, surface_width);
  safe.bottom = std::clamp(safe.bottom, safe.top, surface_height);
  const float safe_width = std::max(1.0F, safe.width());
  const float safe_height = std::max(1.0F, safe.height());
  layout.unit = std::clamp(std::min(safe_width, safe_height) * kUnitFraction, kMinUnit, kMaxUnit);
  const float unit = layout.unit;
  const float edge = std::max(16.0F, unit * std::max(0.0F, config.edge_fraction));

  const auto actions_for = [&config](Placement placement) {
    std::uint32_t actions = 0;
    for (const Control &control : config.controls) {
      if (control.placement == placement) {
        actions |= control.actions;
      }
    }
    return actions;
  };

  const float dpad_left = safe.left + edge;
  const float dpad_top = safe.bottom - edge - unit * 3.0F;
  for (const Cell &cell : kDirectionCells) {
    std::uint32_t actions = actions_for(cell.placement);
    if (is_corner(cell)) {
      actions |= actions_for(cardinal_for_column(cell.column));
      actions |= actions_for(cardinal_for_row(cell.row));
    }
    if (actions == 0) {
      continue;
    }
    /* A cardinal cell carries its control's own id, so pressed feedback in the
     * presentation matches the arrow drawn there. A corner cell is hit-only and
     * gets an id of its own. */
    const Control *owning = nullptr;
    for (const Control &control : config.controls) {
      if (control.placement == cell.placement) {
        owning = &control;
      }
    }
    const std::uint32_t id =
        owning != nullptr ? owning->id
                          : static_cast<std::uint32_t>(kCornerIdBase + cell.row * 3 + cell.column);
    const Rect bounds = rect_at(dpad_left + static_cast<float>(cell.column) * unit,
                                dpad_top + static_cast<float>(cell.row) * unit, unit);
    layout.zones.push_back(Zone{id, bounds, actions});
  }

  const float buttons_width = unit * 3.2F;
  const float buttons_left = safe.right - edge - buttons_width;
  const float buttons_top = safe.bottom - edge - unit * 2.65F;
  for (const Control &control : config.controls) {
    Rect bounds{};
    if (is_direction(control.placement)) {
      // The arrow art is centred in the cell that names this placement; the cell
      // itself is already a hit region from the pass above.
      const auto cell = std::find_if(kDirectionCells.begin(), kDirectionCells.end(),
                                     [&control](const Cell &candidate) {
                                       return candidate.placement == control.placement;
                                     });
      const int column = cell == kDirectionCells.end() ? 1 : cell->column;
      const int row = cell == kDirectionCells.end() ? 1 : cell->row;
      bounds = rect_at(dpad_left + static_cast<float>(column) * unit,
                       dpad_top + static_cast<float>(row) * unit, unit);
      layout.visuals.push_back(
          Visual{control.id, bounds, control.icon, control.disc, control.actions});
      continue;
    }
    if (control.placement == Placement::top_right || control.placement == Placement::top_left) {
      const float left = control.placement == Placement::top_right
                             ? safe.right - edge - unit * kPauseScale
                             : safe.left + edge;
      bounds = rect_at(left, safe.top + edge, unit * kPauseScale);
    } else {
      const ActionSlot slot = action_slot(control.placement);
      bounds = rect_at(buttons_left + slot.column * unit, buttons_top + slot.row * unit, unit);
    }
    layout.zones.push_back(Zone{control.id, bounds, control.actions});
    layout.visuals.push_back(
        Visual{control.id, bounds, control.icon, control.disc, control.actions});
  }

  return layout;
}

} // namespace touch_ui
