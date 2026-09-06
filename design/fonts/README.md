# IBM Plex native fonts

liquify embeds IBM Plex Sans Regular and IBM Plex Mono Regular in its one binary. The files are
from `IBM/plex` commit `bf260093582f04622aacc1e9f9ca604d7ccd0c42`; `UPSTREAM.json` pins their
SHA-256 digests and `OFL.txt` is the upstream OFL-1.1 licence.

`design/build-tokens.mjs` emits both assets as byte arrays beside the native token constants.
Dear ImGui builds Sans faces at every design-system type-scale size and Mono faces for editor
text. The application chooses physical pixel sizes from SDL's per-window display scale.
