# Orama design system

liquify compiles its native product-interface constants from the same pinned
`oramasearch/orama-design` DTCG snapshot as lake. The source is deterministic and offline:
`design/vendor/orama-design/` carries only the eight token JSON files and `UPSTREAM.json` records
the upstream commit plus a SHA-256 digest for each file.

## Token pipeline

`design/design-tokens.map.json` is the reviewed boundary between liquify names and upstream DTCG
paths. `design/design-tokens.inventory.json` fixes the public native role set. The dependency-free
`design/build-tokens.mjs` resolves aliases and light/dark values, then writes the committed
`include/liquify/design_tokens.generated.hpp` header. It contains normalized RGBA palettes,
spacing, control and shell sizes, radii, line heights, the full type scale, and embedded font
bytes. Do not edit the generated header or vendored JSON by hand.

Refresh from the pinned design-system checkout and regenerate with:

```sh
node design/sync-design-system.mjs \
  --from /path/to/orama-design
node design/build-tokens.mjs
```

The first command deliberately refreshes tokens only. Font updates are reviewed separately with
their source commit, licence and hashes in `design/fonts/UPSTREAM.json`.

## Fonts

IBM Plex Sans Regular and IBM Plex Mono Regular come from `IBM/plex` commit
`bf260093582f04622aacc1e9f9ca604d7ccd0c42`. Their OFL-1.1 licence and stamped hashes live beside
the TrueType files under `design/fonts/`. The generated header embeds them so packaged builds stay
one binary and never depend on a system font or network request.

The shell loads Sans at the design-system type sizes and Mono for editor text. Physical atlas
sizes follow SDL's per-window display scale so the logical type scale remains constant on
high-pixel-density displays.

## Gate

The gate verifies every stamped token and font digest, compares the committed header with a fresh
compile, and proves that changed generated output and a changed vendored token are rejected:

```sh
cmake --build build --target liquify-token-gate
```

CTest runs the same gate as `liquify-token-gate`. Build and deployment do not generate tokens;
Node.js participates only in configure-time test availability and the explicit gate.
