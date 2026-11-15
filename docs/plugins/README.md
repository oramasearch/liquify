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

`PluginHost::add` is the static registration path. `PluginHost::load` opens a module by path and
requires its `liquify_plugin_get_v1` export. Both paths run the same descriptor checks: ABI version,
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
never loads native code on its own say-so** ([liquify#71](https://github.com/oramasearch/liquify/issues/71)).
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
does not prevent another panel from updating. Native process faults remain process faults; plugins
that require stronger isolation belong behind a separate process protocol rather than this ABI.

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
records nothing without confirmation.
