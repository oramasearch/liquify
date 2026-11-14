# Settings

The searchable Settings surface is the live editor for liquify preferences. Open it from
`Preferences: Open Settings` in the command palette or with `Cmd+,` on macOS (`Ctrl+,` is also
accepted). Each result remains in one of six groups: Appearance, Editor, Keymap, AI, Lake accounts,
and Telemetry. Reset restores only the open group's defaults.

## Files and precedence

Settings use liquid's `.liquid` configuration language and its public parser. The user file is:

- macOS: `~/Library/Application Support/liquify/settings.liquid`
- Linux: `$XDG_CONFIG_HOME/liquify/settings.liquid`, or `~/.config/liquify/settings.liquid`
- Windows: `%APPDATA%/liquify/settings.liquid`

An open repository may override it at `.liquid/settings.liquid`. Liquify starts from safe defaults,
applies the user file, then applies repository records in source order. A repository record may name
only the values it overrides. The scope control chooses which file receives changes made in the UI.

**What a repository's own file cannot do** (liquify#75, liquify#76). A repository can be someone
else's, so its `.liquid/settings.liquid` cannot add or change a Lake account, the AI provider or the
AI model; those records are checked and ignored. A save at repository scope writes only the groups
that differ from the user's own settings, and never a Lake account, the provider, the model or the AI
switch, so the user's account list never lands in a repository.

**No settings file turns AI on** (liquify#75). `ai { enabled: ... }` is accepted, so older files still
load, and ignored in both files. AI is turned on per user and per repository, by the settings
checkbox or the Assistant: Toggle command, and kept outside every repository in `ai-repositories`
beside the user's settings file, one canonical repository path per line. With no repository open the
checkbox is disabled.

```liquid
liquify settings {
  theme { mode: system }
  fonts {
    ui_family: "IBM Plex Sans"
    editor_family: "IBM Plex Mono"
    ui_size: 14
    editor_size: 14
  }
  editor {
    wrap: false
    whitespace: false
    format_on_save: false
  }
  keymap {
    bindings: {
      "settings.open": "Cmd+,",
      "run.build": "F7"
    }
  }
  ai {
    provider: anthropic
    model: "claude-opus-5"
    context_bytes: 65536
    max_tokens: 4096
  }
  lake {
    account "orama" { url: "https://api.oramasearch.com/liquid" }
  }
  telemetry { enabled: false }
}
```

Theme modes are `system`, `light`, and `dark`. The embedded font families are IBM Plex Sans and IBM
Plex Mono; the UI exposes their supported logical sizes. Keymap values are command-palette command
names mapped to readable chords. `Primary` means Cmd on macOS and Ctrl elsewhere; `Cmd` and `Ctrl`
are accepted as aliases.

## Live behavior and validation

Theme and font changes rebuild the current presentation, wrap changes the open buffer, and
whitespace markers appear or disappear without reopening the repository. Keymap overrides are read
by the same command dispatch used by the palette leaf commands. AI changes rebuild the optional
assistant boundary; when AI is off, no provider is constructed. Format on save applies liquid's
formatter to `.liquid` buffers and leaves a buffer unsaved with the parser message visible when it
is invalid.

Both files and every UI write go through `liquid::parse_configuration`. Lexer and parser failures
are shown inline verbatim, including file, line, column, expectation, found value, and hint.
Liquify's typed checks use the same location-rich diagnostic form for unsupported groups, fields,
enum values, types, and ranges. Invalid UI text does not replace the last valid file. A broken
repository override does not discard valid user settings.

## Credentials and telemetry

Settings files contain provider names, models, lake account names, and lake URLs, but never tokens.
The UI reports a credential as `******** (configured)` or `not configured`; password inputs write
the OS credential store on macOS and Windows, then clear their temporary buffers. It never renders
or logs the value. The `ANTHROPIC_API_KEY` and `LIQUID_TOKEN` environment variables remain fallback
inputs. Lake credentials use the account-specific `LAKE_TOKEN/<account>` credential name. Linux
reports that its native store is unavailable and retains the environment fallback. AI is off by
default. Telemetry is also off by default, and `telemetry.enabled` is its only switch.

## Screenshot and tests

The deterministic `settings-light` and `settings-dark` states show the grouped surface, masked
provider and lake credentials, and the explicit telemetry control:

| Theme | Evidence |
|---|---|
| Light | [settings-light.png](screenshots/settings-light.png) |
| Dark | [settings-dark.png](screenshots/settings-dark.png) |

`Settings` tests cover defaults, all typed groups, source-ordered user/repository overlays, exact
liquid parser messages, typed validation locations, credential-free serialization, group reset, and
search terms. `Screenshot.SettingsRenderGroupedControlsInBothThemes` renders both proof states.
