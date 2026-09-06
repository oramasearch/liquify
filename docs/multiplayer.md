# Multiplayer editor surface

## What ships

liquify opens the multiplayer surface through liquid's public repository API. Each open editor
receives a fresh session identifier, reports its current cursor as editing presence, queries the
head's line attribution, and refreshes the visible presence and lease state once per second.
Presence remains transient in liquid; it is never written into a drip or used as attribution.

The editor shows other active sessions with their actor, activity, path, and session identifier.
Its attribution gutter lists the lines around the cursor with the actor, session, change, and agent
marker that liquid recorded. The UI deliberately displays liquid's values rather than inferring an
author from a process or commit message.

## Leases and writes

`Take lease` and `Release lease` acquire and release a five-minute file lease through
`repository::lease` and `repository::release` using the editor session. A lease held by another
session is shown with its holder and prevents a local buffer mutation at its covered line before
the buffer changes. Save reopens liquid with that same editor session and calls
`EditorBuffer::save(repository)`, so liquid performs its normal authorization and conflict checks
on the write.

Range leases also prevent edits inside their one-based inclusive line interval. The lease list
shows each holder, scope, path, and session. Liquid remains the authority for every lease and write
operation; the UI check is an early, visible refusal rather than a second authorization system.

## Plugins

`PluginHost` publishes liquid presence, lease, and attribution records to the optional ABI-v1
event callback table. Callback bytes are canonical CBOR and match liquid's frontend contract:
presence contains actor/session/path/range/activity/time, lease contains its claim and authority,
and attribution contains the requested path/change and line records. The host copies only callback
pointers covered by the advertised descriptor/table sizes. Older v1 descriptors load without an
event table, and an incompatible table disables only event delivery.

## Measurement and verification

The ordinary display smoke run retains the existing 50,000-line frame and typing-latency output.
Multiplayer refreshes are asynchronous, so snapshot queries do not run on the render path. The
plugin test covers all three event callbacks and verifies the retained callback bytes are canonical
CBOR. Native static and shared suites, `liquify version`, and `liquify --smoke 30` are the release
proof for this surface.

Linux proof is deferred (decision 33).
