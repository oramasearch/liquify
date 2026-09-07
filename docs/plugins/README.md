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

The application loads dynamic modules from repeated `config.liquify.plugins` entries in the
global liquify config and from the repository metadata value with the same name. A value may list
multiple paths separated by semicolons. Relative global paths start at the config file's directory;
relative repository paths start at the repository root. Global entries come first and duplicate
normalized paths load once:

```text
config.liquify.plugins=plugins/format.so
config.liquify.plugins=/opt/liquify/navigation.so;/opt/liquify/build.so
```

These are native libraries. Opening an untrusted repository whose metadata names a plugin can run
native code, so repositories and configured paths must be trusted. One failed path is reported in
the plugin panel without suppressing modules that loaded successfully.

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
loads global and repository configuration, reports a bad path without losing a healthy module,
refuses an ABI mismatch and non-canonical CBOR, and proves one failed panel leaves a healthy panel
available.
