# Command reference

Generated from the built-in command registry by `liquify docs commands`. Do not edit the table by hand.

| Group | Command | Name | macOS | Windows | Linux | Keys live in | Description |
|---|---|---|---|---|---|---|---|
| File | Close File | `file.close` | `Cmd+W` | `Ctrl+W` | `Ctrl+W` | the window | Close the active buffer, confirming any unsaved changes. |
| File | Save | `file.save` | `Cmd+S` | `Ctrl+S` | `Ctrl+S` | the editor | Save the active editor buffer through libliquid. |
| Edit | Command Palette | `edit.command-palette` | `Cmd+Shift+P` | `Ctrl+Shift+P` | `Ctrl+Shift+P` | the window | Find and run any registered command. |
| Edit | Select All | `edit.select-all` | `Cmd+A` | `Ctrl+A` | `Ctrl+A` | the editor | Select the active editor buffer. |
| Edit | Toggle Input Mode | `edit.toggle-input-mode` | `Cmd+M` | `Ctrl+M` | `Ctrl+M` | the editor | Switch between standard and modal input. |
| Selection | Expand Syntax Selection | `selection.expand` | `Cmd+Shift+Right` | `Ctrl+Shift+Right` | `Ctrl+Shift+Right` | the editor | Expand selection to the next syntax ancestor. |
| Selection | Shrink Syntax Selection | `selection.shrink` | `Cmd+Shift+Left` | `Ctrl+Shift+Left` | `Ctrl+Shift+Left` | the editor | Return to the previous syntax selection. |
| Selection | Select Enclosing Definition | `selection.enclosing-definition` | `Cmd+Shift+D` | `Ctrl+Shift+D` | `Ctrl+Shift+D` | the editor | Select the enclosing definition from the analysis graph. |
| Selection | Select Symbol References | `selection.symbol-references` | `Cmd+Option+F` | `Ctrl+Alt+F` | `Ctrl+Alt+F` | the editor | Find references to the selected symbol. |
| View | Toggle Sidebar | `view.toggle-sidebar` | `Cmd+B` | `Ctrl+B` | `Ctrl+B` | the window | Show or hide the contextual sidebar. |
| View | Toggle Bottom Panel | `view.toggle-bottom-panel` | `Cmd+J` | `Ctrl+J` | `Ctrl+J` | the window | Show or hide the bottom panel. |
| View | Toggle Editor Split | `view.toggle-editor-split` | `Cmd+\` | `Ctrl+\` | `Ctrl+\` | the window | Show or hide the secondary editor overview. |
| View | Cycle Color Theme | `view.cycle-theme` | `Cmd+Shift+T` | `Ctrl+Shift+T` | `Ctrl+Shift+T` | the window | Cycle system, light, and dark theme preferences. |
| View | Show Repository | `view.repository` | `—` | `—` | `—` | the window | Open the Repository sidebar. |
| View | Show Review | `view.review` | `—` | `—` | `—` | the window | Open the Review sidebar. |
| View | Show Multiplayer | `view.multiplayer` | `—` | `—` | `—` | the window | Open the Multiplayer sidebar. |
| View | Show Diagnostics Panel | `panel.diagnostics` | `—` | `—` | `—` | the window | Open the Diagnostics panel. |
| View | Show Build Panel | `panel.build` | `—` | `—` | `—` | the window | Open the build, test, and push panel. |
| View | Show Assistant Panel | `panel.assistant` | `—` | `—` | `—` | the window | Open the Assistant panel. |
| View | Show Plugins Panel | `panel.plugins` | `—` | `—` | `—` | the window | Open the Plugins panel. |
| Go | Go to Definition | `go.definition` | `F12` | `F12` | `F12` | the editor | Open the selected symbol definition. |
| Go | Show Hover | `go.hover` | `F1` | `F1` | `F1` | the editor | Show graph-backed hover information. |
| Go | Show Completions | `go.completion` | `Cmd+Space` | `Ctrl+Space` | `Ctrl+Space` | the editor | Request graph-backed completions. |
| Refactor | Rename Symbol | `refactor.rename` | `Cmd+Shift+R` | `Ctrl+Shift+R` | `Ctrl+Shift+R` | the editor | Preview a cross-file symbol rename. |
| Refactor | Extract Function | `refactor.extract-function` | `Cmd+Shift+X` | `Ctrl+Shift+X` | `Ctrl+Shift+X` | the editor | Preview a C or C++ function extraction. |
| Run | Build | `run.build` | `F7` | `F7` | `F7` | the window | Plan and start the configured build. |
| Run | Run All Tests | `run.tests` | `F8` | `F8` | `F8` | the window | List and run all configured test actions. |
| Run | Push | `run.push` | `F9` | `F9` | `F9` | the window | Push changes to the lake this repository came from. |
| Run | Set Up Build | `run.set-up-build` | `—` | `—` | `—` | the window | Record the command that builds this repository. |
| Run | Set Lake | `run.set-lake` | `—` | `—` | `—` | the window | Choose the lake repository this one pushes to. |
| Change | Cut Change | `change.cut` | `Cmd+Shift+K` | `Ctrl+Shift+K` | `Ctrl+Shift+K` | the window | Name the drips saved since the last change as one change. |
| Review | Next Hunk | `review.next-hunk` | `Option+Down` | `Alt+Down` | `Alt+Down` | the editor | Move to the next review hunk. |
| Review | Previous Hunk | `review.previous-hunk` | `Option+Up` | `Alt+Up` | `Alt+Up` | the editor | Move to the previous review hunk. |
| Review | Next Comment | `review.next-comment` | `Option+C` | `Alt+C` | `Alt+C` | the editor | Move to the next review comment. |
| Review | New Comment | `review.new-comment` | `Cmd+Option+C` | `Ctrl+Alt+C` | `Ctrl+Alt+C` | the editor | Create a cursor-anchored comment. |
| Review | Approve | `review.approve` | `Cmd+Option+A` | `Ctrl+Alt+A` | `Ctrl+Alt+A` | the editor | Approve the open review. |
| Review | Request Review | `review.request` | `—` | `—` | `—` | the window | Ask people on lake to review the current change. |
| Assistant | Ask Assistant | `assistant.ask` | `Cmd+Shift+A` | `Ctrl+Shift+A` | `Ctrl+Shift+A` | the editor | Ask with inspectable graph context. |
| Assistant | Explain Diagnostic | `assistant.explain-diagnostic` | `Cmd+Shift+E` | `Ctrl+Shift+E` | `Ctrl+Shift+E` | the editor | Explain the active diagnostic. |
| Assistant | Propose Edit | `assistant.propose-edit` | `Cmd+Shift+I` | `Ctrl+Shift+I` | `Ctrl+Shift+I` | the editor | Request an edit preview without changing the repository. |
| Assistant | Accept Edit | `assistant.accept-edit` | `Cmd+Enter` | `Ctrl+Enter` | `Ctrl+Enter` | the editor | Accept the current assistant edit preview. |
| Assistant | Reject Edit | `assistant.reject-edit` | `Cmd+Backspace` | `Ctrl+Backspace` | `Ctrl+Backspace` | the editor | Reject the current assistant edit preview. |
| Assistant | Toggle Assistant | `assistant.toggle` | `Cmd+Shift+L` | `Ctrl+Shift+L` | `Ctrl+Shift+L` | the editor | Turn the assistant on or off. |
| Edit | Find | `edit.find` | `Cmd+F` | `Ctrl+F` | `Ctrl+F` | the editor | Find text in the active editor. |
| Edit | Focus Search | `edit.focus-search` | `Cmd+Shift+F` | `Ctrl+Shift+F` | `Ctrl+Shift+F` | the editor | Focus the repository search panel. |
| Edit | Undo | `edit.undo` | `Cmd+Z` | `Ctrl+Z` | `Ctrl+Z` | the editor | Undo the latest editor operation. |
| Edit | Redo | `edit.redo` | `Cmd+Shift+Z` | `Ctrl+Shift+Z` | `Ctrl+Shift+Z` | the editor | Redo the latest editor operation. |
| Selection | Select Line | `selection.line` | `Cmd+L` | `Ctrl+L` | `Ctrl+L` | the editor | Select the active editor line. |
| Selection | Add Cursor Above | `selection.add-cursor-up` | `Cmd+Option+Up` | `Ctrl+Alt+Up` | `Ctrl+Alt+Up` | the editor | Add a cursor above the active editor cursor. |
| Selection | Add Cursor Below | `selection.add-cursor-down` | `Cmd+Option+Down` | `Ctrl+Alt+Down` | `Ctrl+Alt+Down` | the editor | Add a cursor below the active editor cursor. |
| Review | Refresh Review | `review.refresh` | `—` | `—` | `—` | the window | Refresh the open review. |
| View | Reset Panel Layout | `panel.reset-layout` | `—` | `—` | `—` | the window | Restore the default panel layout. |
| Preferences | Open Settings | `settings.open` | `Cmd+Comma` | `Ctrl+Comma` | `Ctrl+Comma` | the window | Open the live settings surface. |
| Assistant | Preview Assistant Context | `assistant.preview-context` | `Cmd+Shift+C` | `Ctrl+Shift+C` | `Ctrl+Shift+C` | the editor | Show exactly what will be sent before a request. |
| Help | Show Version | `help.version` | `—` | `—` | `—` | the window | Print the liquify and libliquid versions. |
| Help | Command Reference | `help.command-reference` | `—` | `—` | `—` | the window | Generate the command registry reference. |
| Go | Quick Open | `go.quick-open` | `Cmd+P` | `Ctrl+P` | `Ctrl+P` | the window | Open a repository file by fuzzy path. |
| Go | Go to Symbol | `go.symbol` | `Cmd+T` | `Ctrl+T` | `Ctrl+T` | the window | Search the repository's symbols. |
| Go | Find References | `go.references` | `Shift+F12` | `Shift+F12` | `Shift+F12` | the window | List references to the symbol at the cursor. |
| Go | Call Hierarchy | `go.call-hierarchy` | `Option+F12` | `Alt+F12` | `Alt+F12` | the window | Show callers and callees of the symbol at the cursor. |
| Go | Focus Breadcrumbs | `go.breadcrumbs` | `Cmd+Shift+Period` | `Ctrl+Shift+Period` | `Ctrl+Shift+Period` | the window | Move keyboard focus to the editor breadcrumbs. |
| Go | Open Recent | `go.open-recent` | `Cmd+R` | `Ctrl+R` | `Ctrl+R` | the window | Reopen a recently visited location. |
| Go | Back | `go.back` | `Option+Left` | `Alt+Left` | `Alt+Left` | the window | Return to the previous location. |
| Go | Forward | `go.forward` | `Option+Right` | `Alt+Right` | `Alt+Right` | the window | Go forward to the next location. |
| Repository | Open Selected Entry | `repository.open` | `Enter` | `Enter` | `Enter` | the repository tree | Open the file selected in the repository tree. |
| Repository | Rename Entry | `repository.rename` | `F2` | `F2` | `F2` | the window | Rename the selected repository entry. |
| Repository | Move Entry | `repository.move` | `Cmd+Shift+M` | `Ctrl+Shift+M` | `Ctrl+Shift+M` | the window | Move the selected repository entry. |
| Repository | New File | `repository.new-file` | `Cmd+N` | `Ctrl+N` | `Ctrl+N` | the window | Create a file beside the selected repository entry. |
| Repository | Reveal in File Manager | `repository.reveal` | `Cmd+Option+R` | `Ctrl+Alt+R` | `Ctrl+Alt+R` | the window | Show the selected entry in the system file manager. |
| File | Quit liquify | `app.quit` | `Cmd+Q` | `Ctrl+Q` | `Ctrl+Q` | the window | Quit, confirming any unsaved changes. |
| Edit | Copy | `edit.copy` | `Cmd+C` | `Ctrl+C` | `Ctrl+C` | the editor | Copy the editor selection. |
| Edit | Cut | `edit.cut` | `Cmd+X` | `Ctrl+X` | `Ctrl+X` | the editor | Cut the editor selection. |
| Edit | Paste | `edit.paste` | `Cmd+V` | `Ctrl+V` | `Ctrl+V` | the editor | Paste the clipboard at the cursor. |
| View | Toggle High Contrast | `view.toggle-high-contrast` | `Cmd+Shift+H` | `Ctrl+Shift+H` | `Ctrl+Shift+H` | the window | Switch the high-contrast theme on or off. |

## Former names

Each row above is one action. A panel lists the same command the palette lists, not a copy of it. These former panel-scoped names resolve to that command, so keymaps, scripts and `liquify --command` that use them keep working; the palette does not list them.

| Former name | Resolves to |
|---|---|
| `build.start` | `run.build` |
| `build.test` | `run.tests` |
| `build.push` | `run.push` |
| `assistant.explain` | `assistant.explain-diagnostic` |
| `assistant.propose` | `assistant.propose-edit` |
| `assistant.accept` | `assistant.accept-edit` |
| `assistant.reject` | `assistant.reject-edit` |
| `assistant.context` | `assistant.preview-context` |
| `structural.expand` | `selection.expand` |
| `structural.shrink` | `selection.shrink` |
| `structural.definition` | `selection.enclosing-definition` |
| `structural.references` | `selection.symbol-references` |
| `structural.rename` | `refactor.rename` |
| `structural.extract` | `refactor.extract-function` |
| `view.build` | `panel.build` |
| `view.search` | `edit.focus-search` |
| `palette.open` | `edit.command-palette` |
| `build.run` | `run.build` |
| `tests.run` | `run.tests` |
| `push.run` | `run.push` |

Plugin commands are appended at runtime under the Plugins group. Their ABI-declared default binding is resolved through the same keymap and conflict checks.

## One dispatcher

Every way of running a command goes through the registry: a key pressed in the window, the palette, a native menu item, a panel button and `liquify --command`. A key press is turned into a chord and looked up in the registry's resolved bindings (the defaults above, then the keymap files, then the settings surface). The command runs when its keys are live where focus is (*Keys live in*); otherwise the key reaches the focused surface, so Enter types a newline in the editor and opens the selected entry in the repository tree. A chord that two commands claim runs neither and says so in the status bar and the palette until one is rebound. There are no shortcuts outside the registry.

A command that cannot act (no open file, no review, no repository) says why in the status bar; none succeeds as a no-op.

## Palette and native menus

`Cmd+Shift+P` on macOS and `Ctrl+Shift+P` elsewhere opens the palette. Search uses case-insensitive fuzzy subsequence matching over title, stable name, and group. The palette shows the resolved platform binding and keeps the eight most recently invoked commands at the top while the query is empty.

On macOS the native menu bar is built from the registry: one menu per group, one item per command, each showing its resolved key equivalent. Choosing an item runs the registry command. The menus do not take key presses themselves; keys go to the window and the one dispatcher above, so a menu shortcut never runs outside its scope or around a rebind. A rebind rebuilds the menus at once. Plugin commands enter the palette and the Plugins menu through the same registration path.

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

The settings surface's Keymap group edits the same bindings and takes effect as you type, in keys, the palette and the menus. It writes them inside `liquify settings` as `keymap { bindings: { "run.build": "F6" } }`; several chords are separated by `, `. Those are applied after the `keymap` records.

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
