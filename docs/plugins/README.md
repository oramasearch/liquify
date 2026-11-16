# Native plugins

**What.** liquify hosts native plugins through `liquify_plugin_v1`, the descriptor defined beside
liquid's language module contract in `liquid/frontend_abi.h`. The descriptor is C11 and uses the
existing frontend boundary's fixed-width values, byte spans, owned result buffers, error values,
progress callback, cancellation callback, and destroy callback. No C++ standard-library, liquid
C++, SDL, or ImGui type crosses it.

## Registrations

One plugin descriptor names the plugin and contains any combination of three registration arrays:

| Kind | Registration | Invocation |
|---|---|---|
| Query | globally unique query name | canonical-CBOR arguments and result; installed in `InteractiveAnalysis` as a liquid graph query |
| Command | globally unique command name, default binding, canonical-CBOR argument schema | canonical-CBOR arguments and result; exposed to the command surface |
| Panel | globally unique panel name | canonical-CBOR context to canonical-CBOR view model |

The initial view-model schema is a text panel:

```text
{
  "kind": "text",
  "text": text,
  "title": text
}
```

Map keys use canonical CBOR order. liquify validates canonical encoding, exact ownership,
UTF-8 text, and the view-model schema before rendering. Plugins provide data; they never render or
receive an ImGui context.

## Loading and failures

`PluginHost::add` is the static registration path. `PluginHost::load` starts a
`liquify-plugin-host` process that opens the module by path and requires its
`liquify_plugin_get_v1` export ([Process isolation](#process-isolation)). Both paths run the same
descriptor checks: ABI version,
descriptor size, plugin and registration names, operations, duplicate names within a registration
kind, command schemas, inputs, results, and owned-buffer release callbacks. A wrong version is
refused before any registration becomes visible.

The application loads dynamic modules from two places, and treats them differently.

**Global configuration (yours).** Repeated `config.liquify.plugins` entries in the per-user
liquify config load as configured. A value may list multiple paths separated by semicolons;
relative paths start at the config file's directory; duplicate normalized paths load once:

```text
config.liquify.plugins=plugins/format.so
config.liquify.plugins=/opt/liquify/navigation.so;/opt/liquify/build.so
```

**Repository metadata (not yours).** A repository may name plugins in its metadata value
`config.liquify.plugins`; relative paths start at the repository root. A repository can arrive
from anyone (an archive, a shared folder, another user's checkout), so **opening a repository
never loads native code on its own say-so**.
A repository-named plugin loads only when the SHA-256 digest of its exact bytes is already in the
user's **trust store**:

| | |
|---|---|
| Global plugins | `plugins` beside the user settings file, its own file (liquify#72): `config.liquify.plugins=PATH[,PATH...]` lines, `#` starts a comment. It is not the settings file, so saving settings never breaks plugin loading and a plugin line never breaks the settings parse. A `config.liquify.plugins` line left in the settings file by an earlier version is no longer read; move it here. |
| Trust store | `trusted-plugins` beside the user settings file (`~/Library/Application Support/liquify/` on macOS, `$XDG_CONFIG_HOME/liquify/` or `~/.config/liquify/` on Linux, `%APPDATA%\liquify\` on Windows). Never inside a repository; a `.liquid/trusted-plugins` or `.liquid/settings.liquid` in the repository is not read for plugins. |
| Format | one `sha256:<hex>` per line, optionally followed by a tab and the path it was trusted from; `#` starts a comment. Remove a line to revoke. A malformed line is reported and nothing from the repository loads. |
| Granting trust | `liquify plugin trust FILE` prints the plugin's file name, its origin directory and its digest, states that it is native code, and loads nothing. Trust is recorded only when the user types `yes` (or passes `--yes` on their own command line). The digest recorded is the one shown. |
| Inspecting | `liquify plugin list REPOSITORY` prints each configured plugin's source (`global` or `repository`), its digest and whether it is `trusted` or `not trusted`, without loading any. |
| Not trusted | the plugin is listed in the Plugins panel as *not trusted*, with its digest, the repository that named it and the command that would trust it. It is not loaded, and the panel's status counts it. |
| Trusted | the bytes that were hashed are written to a per-user, content-addressed cache (`plugin-cache/<hex><extension>` beside the trust store) and that copy is loaded. The repository's file is never passed to `dlopen`/`LoadLibraryW`, so it cannot be swapped between the digest check and the load. Changing even one byte of a trusted file makes it untrusted again. |

Digests, not signatures, are the trust unit: a digest pins the exact bytes the user approved and
needs no key infrastructure. Signed plugins (trusting a publisher rather than a file) would be a
separate, additional decision.

A path named by both the global configuration and a repository is the user's plugin and loads
through the global path. One failed path is reported in the plugin panel without suppressing
modules that loaded successfully.

Plugin commands enter the shared registry with their declared default binding. They appear in the
fuzzy palette and native macOS Plugins menu, participate in typed keymap overrides and conflict
reporting, and can be invoked headlessly by stable name when configured globally. Invocation uses
an empty canonical-CBOR map; the headed path keeps failures in editor status and the headless path
writes canonical-CBOR output. Plugin text panels refresh independently and render their decoded
view model below the analysis results; the plugin never receives UI objects.

Each panel operation is invoked independently. A returned error, exception, malformed owned value,
non-canonical CBOR value, or unsupported view model becomes that panel's error snapshot and
does not prevent another panel from updating. A crash or hang in a dynamic plugin is the same kind
of error: it happens in the plugin's own process (below), never in the IDE.

## Process isolation

**What.** Every plugin loaded from a file (global or trusted repository plugin) runs in its own
`liquify-plugin-host` process ([liquify#82](https://github.com/oramasearch/liquify/issues/82)).
`PluginHost::load` starts the host; the host maps the library, and **the IDE never maps a plugin
library into its own address space**. Plugins are unchanged: the same ABI v1 descriptor and entry
point, and only the canonical-CBOR values above cross between the processes. `PluginHost::add`
(a plugin compiled into liquify, such as the static sample in the tests) still runs in process.

**Why.** A trusted plugin used to run with the IDE's files, network, environment (API keys) and
signing identity, and its crash was the IDE's crash
([Y-01](https://github.com/oramasearch/liquid-bridge/blob/main/docs/security/review-2026-10.md#y-01)).

**The protocol.** `src/plugin_protocol.hpp`, our own, no dependency. Each frame is a 4-byte
big-endian length (at most 64 MiB) followed by one CBOR array:

| Message | From | Shape |
|---|---|---|
| Manifest | host, once, after mapping the plugin | `[1, name, [query], [[command, binding, schema]], [panel], [event]]`, or `[0, error]` |
| Request | IDE | `[kind, name, input]`; kind 0 query, 1 command, 2 panel, 3 event |
| Response | host | `[status, result, message]`; status is a `LIQUID_FRONTEND_STATUS_*` value |

The IDE repeats every descriptor check on the manifest (names, duplicates, canonical schemas,
event names) and validates every result as canonical CBOR, so the host is not trusted either.
Graph query kinds are still registered by name in the in-process graph; their `compute` is a
request to the host. Commands, panels and the presence, lease and attribution events are requests
the same way. The channel is a socketpair on file descriptor 3 (POSIX) or two inherited pipe
handles (Windows); one call at a time per plugin.

**Lifecycle.** The host starts with standard input and output on the null device, only the channel
inherited, and a scrubbed environment (`PATH`, `HOME`, `TZ`, the locale variables and
`LD_LIBRARY_PATH` only; no API keys or tokens). Queries and commands have 10 seconds, panels and
events 2 seconds. A call that misses its deadline, a host that exits, and a malformed frame end
the host and become that call's error: a panel's error snapshot, the editor status for a command, a
failed graph query. The next call starts the host again and must see the same manifest. After
three restarts the plugin stays disabled with its last error, until the repository is reopened.
The host exits when its channel closes and watches its parent (and on Linux gets `SIGKILL` when
the parent dies, and on Windows is killed with its job), so it never outlives the IDE.

**The sandbox**, applied by the host before it maps the plugin, so the plugin's initialisers are
confined too:

| Platform | Mechanism | Denied |
|---|---|---|
| macOS | `sandbox_init` profile | IP networking (connect, accept, bind), every file write except `/dev/null`, `exec`, `fork`, the keychain services |
| Linux | `no_new_privs`, Landlock, seccomp | Landlock: every filesystem write and `exec` right; TCP bind and connect on Landlock ABI 4+; abstract Unix sockets and signals outside the process on ABI 6+. seccomp (x86_64, aarch64): every socket family but `AF_UNIX`, `execve`, `execveat`, `ptrace`, `process_vm_*`, `io_uring` |
| Windows | low-integrity token, job object | Writes to the user's files and registry (no write up), child processes, clipboard and desktop access; the job is killed when liquify closes it |

**Limits.** Reads are not denied: a plugin can read the user's files, but it has no channel to send
them anywhere except its results to the IDE. Windows does not deny network access; that needs an
AppContainer and is not built. A Linux kernel without Landlock confines the host with seccomp
alone. The progress and
cancellation callbacks stay empty, as before. A plugin needing more than data-only CBOR (files,
network) has no path; that would be a separate decision.

## Requests off the UI thread

**What.** The window never waits for a plugin
([liquify#149](https://github.com/oramasearch/liquify/issues/149)). `PluginDispatcher`
(`include/liquify/plugin.hpp`, `src/plugin_dispatcher.cpp`) sends every panel refresh, command and
presence, lease and attribution event from a worker thread. The UI thread only queues requests and
reads the answers recorded so far.

**How.** Each loaded plugin has one worker, which runs that plugin's requests one at a time in the
order they were sent. A plugin that hangs delays only its own requests; another plugin's panels keep
refreshing. The deadlines (10 seconds for commands, 2 seconds for panels and events) and the
restart budget are the host's, unchanged: the worker waits for them instead of the frame. A
plugin compiled into liquify (`PluginHost::add`) goes through the same workers, so its panel,
command and event operations run on its worker thread, never on the UI thread, one at a time.

| What the window shows | When |
|---|---|
| *loading...* | the panel has been asked and has not answered yet |
| its last answer (view model or error) | while the next refresh is being asked |
| `plugin command running: NAME` in the status bar | from the moment the command is sent |
| `plugin command completed: NAME`, or the command's error | when the command answers |

A panel refresh is not queued while the previous one is still waiting, so a hanging panel never
builds a backlog: the window asks again 500 ms after the answer. Each answer asks the window for a
frame. Destroying the dispatcher (reopening a repository, quitting) never waits either: a request
still running finishes on its worker, its answer is dropped, and the worker exits. Graph queries
are unchanged; they already run on the graph's workers. The headless `--command` path still waits
for its command, because it has no window.

**Verify.** `tests/test_plugin.cpp` (`PluginDispatcher.*`) holds a static plugin's panel and command
behind a gate and requires every call on the caller's thread to return in under 250 ms, the loading
state, the last answer kept during a refresh, the command outcome, and a destructor that does not
wait; with the fault plugin it requires the same of a hanging panel and command in their host, each
ending at its deadline. The native scenario `plugin-hang` loads the fault plugin into the real
window, runs `fault.sleep` from the palette, and requires the loading state, both deadline errors
on screen and no four-frame pulse slower than one second
([native acceptance](../ui/native-acceptance.md)).

## Versioning

`LIQUIFY_PLUGIN_ABI_VERSION` versions the plugin descriptor independently of
`LIQUID_FRONTEND_ABI_VERSION`. `struct_size` reserves compatible tail growth. ABI v1 never removes,
reorders, or changes an existing field. A breaking descriptor change gets a new version constant,
entry point, and checked host path. Operation values remain canonical CBOR so individual query,
command, and panel schemas can carry their own version fields.

## Sample and proof

[`plugins/sample/`](../../plugins/sample/) proves all registrations. `sample.echo` is demanded
through the in-process liquid graph, `sample.reset` supplies its binding and argument schema, and
`sample.status` returns a text view model. CMake builds the source as both a module and a static
library; `test_plugin.cpp` exercises query, command, and panel behavior through both loading paths,
loads global configuration, reports a bad path without losing a healthy module,
refuses an ABI mismatch and non-canonical CBOR, and proves one failed panel leaves a healthy panel
available. The `PluginTrust` tests prove that repository metadata naming a plugin loads nothing,
that a trust store or settings file inside the repository is ignored, that a trusted digest loads
from the per-user cache, that changed bytes are refused, that global plugins keep loading beside
an untrusted repository, and that `liquify plugin trust` names the plugin and its origin and
records nothing without confirmation. The `PluginProcess` tests use `tests/fault_plugin.cpp`, a
plugin that misbehaves on purpose: the IDE does not map a loaded plugin, a crashing command fails
that call and the next call restarts the host, a hanging panel becomes that panel's error while
another panel renders, a plugin is disabled after its restart budget, and the plugin cannot write
a file, open a network connection (not on Windows) or see the IDE's environment. `PluginProtocol`
round-trips every message and refuses truncated, trailing and unknown frames.
