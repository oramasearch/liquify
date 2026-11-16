# What liquify is

liquify is the IDE for liquid: a native, cross-platform editor, one binary, built on libliquid's
graph. liquid is the tool. lake is where it collects. liquify is where you work. It is open source
under the MIT licence, like liquid.

## Why a native editor

A native editor that shares memory with liquid's graph instead of serialising across a process
boundary. That is the whole point, and it is why it is not an extension for someone else's
editor. What it does that others cannot, once the store, graph and daemon exist:

| | |
|---|---|
| Instant open | any repository at any size, no indexing wait: the virtual filesystem and the warm graph |
| Diagnostics without a second parse | from the same graph the build uses |
| Drips | every save is a signed, attributed change; history is a query |
| Multiplayer | people and agents in one buffer, attributed via TAAP |

## What it is built on

It starts from the window and UI stack Zetta's headed mode already proves on macOS, Windows and
Linux: SDL3 and Dear ImGui with the SDL_Renderer backend, with the editor buffer as a retained GPU
text layer inside ImGui ([`../app-shell/README.md`](../app-shell/README.md),
[`../editor/README.md`](../editor/README.md)). No Zetta code is used, only its choices; liquify
never depends on Zetta or on `oramasearch/lake`. liquify is a client of libliquid: liquid is a
sibling module resolved by `LIQUIFY_LIQUID_SOURCE_DIR` (default `../liquid`), built library-only
and linked as `liquid::liquid`, and everything
liquify knows about repositories, drips, builds, analysis and search comes through that API.
liquify talks to liquid (the daemon), and through liquid to lake. The numbered decisions behind
these choices are in `CLAUDE.md`.

## Where this sits

liquify is part of the liquid project. The product specification is `oramasearch/liquid`
`docs/features/21-liquify-ide.md`. What the source tree implements today is
[`status.md`](status.md).
