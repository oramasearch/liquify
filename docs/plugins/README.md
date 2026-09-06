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
refuses an ABI mismatch and non-canonical CBOR, and proves one failed panel leaves a healthy
panel available.
