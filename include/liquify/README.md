# Public application headers

`app.hpp` exposes the application lifecycle using plain C++ options. Rendering implementation
and the native-window test driver remain in `src/app.cpp`; a private friend lets that driver
exercise real frames and read their framebuffer before presentation.
See the [coordinate contract](../../docs/ui/app-shell.md).

# Public IDE interfaces

These headers expose plain C++ application and editor models. `version.hpp` exposes the numerical
release value, revision-bearing IDE version, pinned library revision, and complete build
description. See [store compatibility](../../docs/build/store-compatibility.md).
