# Public application headers

`app.hpp` exposes the application lifecycle using plain C++ options. Rendering implementation
and the native-window test driver remain in `src/app.cpp`; a private friend lets that driver
exercise real frames and read their framebuffer before presentation.
See the [coordinate contract](../../docs/ui/app-shell.md).
