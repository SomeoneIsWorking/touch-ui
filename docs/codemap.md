# Codemap

touch-ui is one cohesive subsystem: the on-screen controls a port shows on a
touch device. Titles supply action bits and receive transitions; nothing
title-specific lives here.

| Responsibility | Where | Notes |
|---|---|---|
| Placement and hit-region geometry | `src/layout.cpp` | Safe-area clamping, unit sizing from the display scale, the 3x3 direction grid, action-button slots, pause corner. Pure: the same input always gives the same layout. |
| Contact routing, pointer ownership, presentation | `src/controls.cpp` | Wraps `lucent::touch::Router`, reference-counts actions, claims touches over the overlay, rasterises and draws the SVG art at output resolution. |
| Public API | `include/touch_ui/touch_ui.h` | `Config`, `Control`, `Geometry`, `Layout`, `Controls`. |
| Shared art embedding | `tools/embed_svg.py`, generated `embedded_icons.{h,cpp}` | The SVG bytes come from `shared/port-assets/sets/touch-controls` at build time; nothing is copied into a project. |
| Tests | `tests/test_touch_ui.cpp` | Layout, diagonals, reference counting, claiming policy, rendered-frame capture. |
| Capture for review | `tests/capture_touch_ui.cpp`, `tools/capture.py` | Draws over a stand-in frame at a chosen surface size. |
| Verifier | `tools/verify.py` | Ruff, CMake, clang-tidy, CTest. |
