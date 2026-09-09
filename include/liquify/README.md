# Public application headers

`app.hpp` exposes the application lifecycle and reported Diagnostics viewport using plain C++
options and geometry. Rendering implementation and the native-window test driver remain in
`src/app.cpp`; a private friend lets that driver exercise real frames and read their framebuffer
before presentation. The screenshot test uses the reported viewport instead of fixed panel
coordinates. See the [coordinate contract](../../docs/ui/app-shell.md) and
[default-window hierarchy](../../docs/ui/what-the-window-shows.md).

# Public IDE interfaces

These headers expose plain C++ application and editor models. `version.hpp` exposes the numerical
release value, revision-bearing IDE version, pinned library revision, and complete build
description. See [store compatibility](../../docs/build/store-compatibility.md).

`ui_layout.hpp` defines `UiFrameLayout`, returned by `App::frame_layout()`: current logical
window, toolbar, editor and panel bounds, drawable scale, theme colors and submitted text ink.
See [layout properties](../../docs/ui/property-tests.md) for the reporting contract.

`UiFrameLayout::controls` reports named current repository and Quick Open control rectangles
for native pointer acceptance. Hidden controls disappear on the next frame. See
[native acceptance](../../docs/ui/native-acceptance.md).
