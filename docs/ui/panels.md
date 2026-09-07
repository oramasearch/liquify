# Panels as one system

Every liquify surface enters the shell through `PanelRegistry`. The registry is plain C++ in
`include/liquify/panel.hpp` and `src/panel.cpp`; it does not include SDL or Dear ImGui. `src/app.cpp`
is the renderer and command dispatcher for that model.

## Registry contract

Each descriptor has one stable identifier, title, one-character native icon, default placement,
command list, and plain-language empty state. Runtime state adds visibility, focus, placement,
order, the last docked placement, floating bounds, busy state, status text, and error text.

| Panel | Default placement | Registered commands |
|---|---|---|
| Repository | Sidebar | — |
| Search | Sidebar | — |
| Review | Sidebar | Refresh, hunk and comment navigation, new comment, approve |
| Build, test and deploy | Bottom | Build, run tests, deploy |
| Multiplayer | Sidebar | — |
| Diagnostics | Bottom | — |
| Assistant | Bottom | Ask, explain, propose, accept, reject, toggle, inspect context |
| Plugins | Bottom | Plugin commands remain supplied by each plugin |
| Structural editing | Editor tab | Selection expansion, definition and references, rename, extract |

The activity bar remains the quick route to Repository, Search, Review, Build, and Multiplayer.
It focuses the registered panel wherever that panel currently lives. The command palette is built
from the same descriptor commands and adds `Panels: Show …` entries plus `Panels: Reset layout`.

## Docking and repository-local state

The `…` menu in every panel moves it to the sidebar, bottom area, or editor tabs, or floats it in a
native ImGui window. A floating panel remembers its preceding dock placement so `Re-dock` returns
it there. Closing a floating window hides it; the command palette shows it again. Panel selection,
ordering, visibility, focus, last dock, and floating bounds are saved with the existing divider
and split values in `<repository>/.liquid/liquify-state.json`.

State version 2 stores a `panels` object. Loading version 1 maps its selected sidebar and bottom
panel into the registry, so an existing repository does not lose its selected surfaces. Version 3
adds the high-contrast preference. Unknown or missing panel entries receive current defaults.
`Panels: Reset layout` restores dimensions, visibility, and every panel placement while preserving
the theme and high-contrast preferences.

## Status, empty states, and errors

The registry is the single state and notification API for built-in panels. Every panel has an
empty sentence that says what is absent or what action enables it. Repository, review,
diagnostics, build, plugin, assistant, and structural failures are introduced with direct text such
as “Could not load reviews” or “Could not complete the operation”; controller error detail follows
that sentence.

One status-bar channel reports progress and completion for builds, saved-drip sync, repository
folder hydration, and assistant streaming. Selecting the message focuses its source panel.
Determinate work includes a progress bar. The channel offers Cancel only when the controller can
honour it: assistant streaming currently can; the pinned libliquid build handle, sync endpoint, and
repository directory future do not expose cancellation.

## Verification and changes

`PanelRegistry.*` tests cover descriptor metadata, focus, hiding, movement, floating bounds,
re-docking, default restoration, progress, and supported cancellation. `AppShellState.*` tests
cover version 3 round trips, version 1 migration, invalid state, and layout reset. The deterministic
`panels-light` and `panels-dark` states show an editor-tab Build panel, floating Assistant, docked
Diagnostics, and shared build progress.

After changing descriptors, persistence, or panel chrome, run:

```sh
cmake --build build -j 6
ctest --test-dir build --output-on-failure
LIQUIFY_BINARY=./build/liquify tools/ui/screenshot.sh
```

Review both themes, update this page and the screenshot registry together, and keep controller
logic outside `src/app.cpp` free of SDL and ImGui types.
