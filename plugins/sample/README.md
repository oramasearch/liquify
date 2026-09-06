# Sample plugin

The sample implements all three `liquify_plugin_v1` registration kinds. `sample.echo` is a graph
query that returns its canonical-CBOR arguments, `sample.reset` is a command with the default
`Primary+Shift+R` binding and an object argument schema, and `sample.status` produces a text panel
view model accepted by the checked host.

The same source builds as `liquify-sample-plugin`, a dynamically loaded module exporting only
`liquify_plugin_get_v1`, and as `liquify-sample-plugin-static` for linked registration tests. The
test suite invokes the query through `InteractiveAnalysis` and invokes the command and panel through
both loading paths.
