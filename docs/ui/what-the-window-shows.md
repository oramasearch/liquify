# What the window shows

The default window follows one rule: **content, control, or state worth acting on**.
Anything else is absent until someone deliberately opens its details.

## Three zones

- Navigation on the left uses recognizable icons with labels or tooltips. File names are the
  content of tree rows; type icons decorate them without bracketed language prefixes.
- Content in the centre contains the file and controls that act on it. The retained editor
  fills the available pane. Collaboration records and performance readouts consume no source rows.
- Context at the bottom contains diagnostics, build output, assistant and plugin surfaces.
  Each panel names itself once. `Diagnostics 0` is enough; another header and an empty count
  sentence add no information.

## Controls

| Group | Controls |
|---|---|
| File | Save |
| Build | Build (F7), Tests (F8), Push (F9) |
| View | Split, Hide panel |
| Mode | Standard, structural editing, optional assistant |

Groups remain distinguishable when the pane narrows. Definition (F12), Hover (F1) and completion
remain keyboard commands. Lease acquisition and release are deliberate menu actions.
Structural preview diagnostics are state, shown on Diagnostics or in the status bar.

## Information on demand

Presence appears in the default window only when another person is present. Attribution belongs
to its source line: hover or click its gutter marker to inspect the author, session and change.
Repository drip triggers, pending-drip and validation counters, repository-open timing and
diagnostic latency belong behind an explicit diagnostics toggle.

Identifiers preserve their scheme and show eight payload characters followed by an ellipsis:
`taap:1:ed25519:TY44R4SE…`, `blake3:e3d79408…`. The full value remains available on demand for
inspection or copying. The default reading and build/test path presents no cryptographic
identity, session identifier or millisecond timing.

## Acceptance

Judge the result in an ordinary running GPU window, with a real liquid repository containing a
README and source files. Headless fixture images and captures of the wrong desktop Space cannot
establish usability. Record before/after window captures and a person's walkthrough: open the
repository, find a file, read it, reach Build and Tests, and inspect the relocated details.

The native coordinate contract, popup drawing order and retained buffer collection are unchanged
by this design. Their registered regressions and the independent CLI compatibility check remain
required; see [app-shell.md](app-shell.md) and [buffers.md](buffers.md).
