# Command reference

Generated from the built-in command registry by `liquify docs commands`. Do not edit the table by hand.

| Group | Command | Name | macOS | Windows | Linux | Description |
|---|---|---|---|---|---|---|
| File | Close File | `file.close` | `Cmd+W` | `Ctrl+W` | `Ctrl+W` | Close the active buffer, confirming any unsaved changes. |
| File | Save | `file.save` | `Cmd+S` | `Ctrl+S` | `Ctrl+S` | Save the active editor buffer through libliquid. |
| Edit | Command Palette | `edit.command-palette` | `Cmd+Shift+P` | `Ctrl+Shift+P` | `Ctrl+Shift+P` | Find and run any registered command. |
| Edit | Select All | `edit.select-all` | `Cmd+A` | `Ctrl+A` | `Ctrl+A` | Select the active editor buffer. |
| Edit | Toggle Input Mode | `edit.toggle-input-mode` | `Cmd+M` | `Ctrl+M` | `Ctrl+M` | Switch between standard and modal input. |
| Selection | Expand Syntax Selection | `selection.expand` | `Cmd+Shift+Right` | `Ctrl+Shift+Right` | `Ctrl+Shift+Right` | Expand selection to the next syntax ancestor. |
| Selection | Shrink Syntax Selection | `selection.shrink` | `Cmd+Shift+Left` | `Ctrl+Shift+Left` | `Ctrl+Shift+Left` | Return to the previous syntax selection. |
| Selection | Select Enclosing Definition | `selection.enclosing-definition` | `Cmd+Shift+D` | `Ctrl+Shift+D` | `Ctrl+Shift+D` | Select the enclosing definition from the analysis graph. |
| Selection | Select Symbol References | `selection.symbol-references` | `Cmd+Option+F` | `Ctrl+Alt+F` | `Ctrl+Alt+F` | Find references to the selected symbol. |
| View | Toggle Sidebar | `view.toggle-sidebar` | `Cmd+B` | `Ctrl+B` | `Ctrl+B` | Show or hide the contextual sidebar. |
| View | Toggle Bottom Panel | `view.toggle-bottom-panel` | `Cmd+J` | `Ctrl+J` | `Ctrl+J` | Show or hide the bottom panel. |
| View | Toggle Editor Split | `view.toggle-editor-split` | `Cmd+\` | `Ctrl+\` | `Ctrl+\` | Show or hide the secondary editor overview. |
| View | Cycle Color Theme | `view.cycle-theme` | `Cmd+Shift+T` | `Ctrl+Shift+T` | `Ctrl+Shift+T` | Cycle system, light, and dark theme preferences. |
| View | Show Repository | `view.repository` | `—` | `—` | `—` | Open the Repository sidebar. |
| View | Show Search | `view.search` | `—` | `—` | `—` | Open the Search sidebar. |
| View | Show Review | `view.review` | `—` | `—` | `—` | Open the Review sidebar. |
| View | Show Build | `view.build` | `—` | `—` | `—` | Open the Build sidebar. |
| View | Show Multiplayer | `view.multiplayer` | `—` | `—` | `—` | Open the Multiplayer sidebar. |
| View | Show Diagnostics Panel | `panel.diagnostics` | `—` | `—` | `—` | Open the Diagnostics panel. |
| View | Show Build Panel | `panel.build` | `—` | `—` | `—` | Open the build, test, and push panel. |
| View | Show Assistant Panel | `panel.assistant` | `—` | `—` | `—` | Open the Assistant panel. |
| View | Show Plugins Panel | `panel.plugins` | `—` | `—` | `—` | Open the Plugins panel. |
| Go | Go to Definition | `go.definition` | `F12` | `F12` | `F12` | Open the selected symbol definition. |
| Go | Show Hover | `go.hover` | `F1` | `F1` | `F1` | Show graph-backed hover information. |
| Go | Show Completions | `go.completion` | `Cmd+Space` | `Ctrl+Space` | `Ctrl+Space` | Request graph-backed completions. |
| Refactor | Rename Symbol | `refactor.rename` | `Cmd+Shift+R` | `Ctrl+Shift+R` | `Ctrl+Shift+R` | Preview a cross-file symbol rename. |
| Refactor | Extract Function | `refactor.extract-function` | `Cmd+Shift+X` | `Ctrl+Shift+X` | `Ctrl+Shift+X` | Preview a C or C++ function extraction. |
| Run | Build | `run.build` | `F7` | `F7` | `F7` | Plan and start the configured build. |
| Run | Run All Tests | `run.tests` | `F8` | `F8` | `F8` | List and run all configured test actions. |
| Run | Push | `run.push` | `F9` | `F9` | `F9` | Push changes to the lake this repository came from. |
| Run | Set Up Build | `run.set-up-build` | `—` | `—` | `—` | Record the command that builds this repository. |
| Run | Set Lake | `run.set-lake` | `—` | `—` | `—` | Choose the lake repository this one pushes to. |
| Change | Cut Change | `change.cut` | `Cmd+Shift+K` | `Ctrl+Shift+K` | `Ctrl+Shift+K` | Name the drips saved since the last change as one change. |
| Review | Next Hunk | `review.next-hunk` | `Option+Down` | `Alt+Down` | `Alt+Down` | Move to the next review hunk. |
| Review | Previous Hunk | `review.previous-hunk` | `Option+Up` | `Alt+Up` | `Alt+Up` | Move to the previous review hunk. |
| Review | Next Comment | `review.next-comment` | `Option+C` | `Alt+C` | `Alt+C` | Move to the next review comment. |
| Review | New Comment | `review.new-comment` | `Cmd+Option+C` | `Ctrl+Alt+C` | `Ctrl+Alt+C` | Create a cursor-anchored comment. |
| Review | Approve | `review.approve` | `Cmd+Option+A` | `Ctrl+Alt+A` | `Ctrl+Alt+A` | Approve the open review. |
| Review | Request Review | `review.request` | `—` | `—` | `—` | Ask people on lake to review the current change. |
| Assistant | Ask Assistant | `assistant.ask` | `Cmd+Shift+A` | `Ctrl+Shift+A` | `Ctrl+Shift+A` | Ask with inspectable graph context. |
| Assistant | Explain Diagnostic | `assistant.explain-diagnostic` | `Cmd+Shift+E` | `Ctrl+Shift+E` | `Ctrl+Shift+E` | Explain the active diagnostic. |
| Assistant | Propose Edit | `assistant.propose-edit` | `Cmd+Shift+I` | `Ctrl+Shift+I` | `Ctrl+Shift+I` | Request an edit preview without changing the repository. |
| Assistant | Accept Edit | `assistant.accept-edit` | `Cmd+Enter` | `Ctrl+Enter` | `Ctrl+Enter` | Accept the current assistant edit preview. |
| Assistant | Reject Edit | `assistant.reject-edit` | `Cmd+Backspace` | `Ctrl+Backspace` | `Ctrl+Backspace` | Reject the current assistant edit preview. |
| Assistant | Toggle Assistant | `assistant.toggle` | `Cmd+Shift+L` | `Ctrl+Shift+L` | `Ctrl+Shift+L` | Turn the assistant on or off. |
| Edit | Find | `edit.find` | `Cmd+F` | `Ctrl+F` | `Ctrl+F` | Find text in the active editor. |
| Edit | Focus Search | `edit.focus-search` | `Cmd+Shift+F` | `Ctrl+Shift+F` | `Ctrl+Shift+F` | Focus the repository search panel. |
| Edit | Undo | `edit.undo` | `Cmd+Z` | `Ctrl+Z` | `Ctrl+Z` | Undo the latest editor operation. |
| Edit | Redo | `edit.redo` | `Cmd+Shift+Z` | `Ctrl+Shift+Z` | `Ctrl+Shift+Z` | Redo the latest editor operation. |
| Selection | Select Line | `selection.line` | `Cmd+L` | `Ctrl+L` | `Ctrl+L` | Select the active editor line. |
| Selection | Add Cursor Above | `selection.add-cursor-up` | `Cmd+Option+Up` | `Ctrl+Alt+Up` | `Ctrl+Alt+Up` | Add a cursor above the active editor cursor. |
| Selection | Add Cursor Below | `selection.add-cursor-down` | `Cmd+Option+Down` | `Ctrl+Alt+Down` | `Ctrl+Alt+Down` | Add a cursor below the active editor cursor. |
| Review | Refresh Review | `review.refresh` | `—` | `—` | `—` | Refresh the open review. |
| View | Reset Panel Layout | `panel.reset-layout` | `—` | `—` | `—` | Restore the default panel layout. |
| Preferences | Open Settings | `settings.open` | `Cmd+Comma` | `Ctrl+Comma` | `Ctrl+Comma` | Open the live settings surface. |
| Build, test and push | Build | `build.start` | `—` | `—` | `—` | Start the configured build from the build panel. |
| Build, test and push | Run All Tests | `build.test` | `—` | `—` | `—` | Run all configured test actions from the build panel. |
| Build, test and push | Push | `build.push` | `—` | `—` | `—` | Push changes from the build panel. |
| Assistant | Explain Diagnostic | `assistant.explain` | `—` | `—` | `—` | Explain the active diagnostic from the assistant panel. |
| Assistant | Propose Edit | `assistant.propose` | `—` | `—` | `—` | Request an edit preview from the assistant panel. |
| Assistant | Accept Edit | `assistant.accept` | `—` | `—` | `—` | Accept the assistant edit preview from the assistant panel. |
| Assistant | Reject Edit | `assistant.reject` | `—` | `—` | `—` | Reject the assistant edit preview from the assistant panel. |
| Assistant | Preview Assistant Context | `assistant.context` | `—` | `—` | `—` | Show the assistant context from the assistant panel. |
| Structural editing | Expand Syntax Selection | `structural.expand` | `—` | `—` | `—` | Expand selection from the structural editing panel. |
| Structural editing | Shrink Syntax Selection | `structural.shrink` | `—` | `—` | `—` | Shrink selection from the structural editing panel. |
| Structural editing | Select Enclosing Definition | `structural.definition` | `—` | `—` | `—` | Select the enclosing definition from the structural editing panel. |
| Structural editing | Select Symbol References | `structural.references` | `—` | `—` | `—` | Find symbol references from the structural editing panel. |
| Structural editing | Rename Symbol | `structural.rename` | `—` | `—` | `—` | Preview a symbol rename from the structural editing panel. |
| Structural editing | Extract Function | `structural.extract` | `—` | `—` | `—` | Preview function extraction from the structural editing panel. |
| Assistant | Preview Assistant Context | `assistant.preview-context` | `Cmd+Shift+C` | `Ctrl+Shift+C` | `Ctrl+Shift+C` | Show exactly what will be sent before a request. |
| Help | Show Version | `help.version` | `—` | `—` | `—` | Print the liquify and libliquid versions. |
| Help | Command Reference | `help.command-reference` | `—` | `—` | `—` | Generate the command registry reference. |

Plugin commands are appended at runtime under the Plugins group. Their ABI-declared default binding is resolved through the same keymap and conflict checks.

## Palette and native menus

`Cmd+Shift+P` on macOS and `Ctrl+Shift+P` elsewhere opens the palette. Search uses case-insensitive fuzzy subsequence matching over title, stable name, and group. The palette shows the resolved platform binding and keeps the eight most recently invoked commands at the top while the query is empty.

On macOS the native menu bar is built from the same registry groups and active bindings. Plugin commands therefore enter the palette and the Plugins menu through one registration path.

## Typed keymap overrides

The global settings file is `~/Library/Application Support/liquify/settings.liquid` on macOS, `%APPDATA%\liquify\settings.liquid` on Windows, and `${XDG_CONFIG_HOME:-~/.config}/liquify/settings.liquid` on Linux. A repository may override it with `.liquid/settings.liquid`; repository records are applied last. The settings surface tracked by issue #37 reads these same files.

liquify parses the files with liquid's typed `.liquid` parser, then checks the keymap-owned `keymap` and `bind` records. Other top-level records remain available to settings groups. Errors retain file, line, and column. Each override replaces a command's complete key list; an empty list unbinds it.

```liquid
keymap {
    bind "edit.command-palette" {
        keys: ["Primary+Shift+P"]
    }
}
```

`Primary` resolves to Command on macOS and Control on Windows and Linux. Explicit `Shift`, `Alt`/`Option`, and `Ctrl` modifiers are accepted. Duplicate active chords are reported with every owning command and are disabled until the conflict is removed; named invocation remains available. The generated complete default file is [`default-keymap.liquid`](default-keymap.liquid).

## Headless commands and generation

`liquify --command <name>` resolves the same stable registry name without opening SDL. `help.version` and `help.command-reference` are built-in headless commands. Globally configured plugin commands receive an empty canonical-CBOR map and write their canonical-CBOR result to standard output. Commands that require live editor state fail with a clear message. Unknown names exit 2.

Regenerate and verify the committed artifacts with:

```sh
liquify docs commands --out docs/ui/commands.md
liquify docs keymap --out docs/ui/default-keymap.liquid
liquify docs commands --check docs/ui/commands.md
liquify docs keymap --check docs/ui/default-keymap.liquid
```
