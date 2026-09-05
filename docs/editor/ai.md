# Assistant surface

**Decision.** AI is a first-class editor surface and is disabled by default. The repository
document key `config.liquify.ai.enabled` overrides the same key in the global configuration file.
The status-line `AI: off` / `AI: on` button and the Assistant: Toggle palette command write the
repository key. Global configuration uses `key=value` lines at the platform configuration path:

- macOS: `~/Library/Application Support/liquify/config`
- Windows: `%APPDATA%/liquify/config`
- Linux: `$XDG_CONFIG_HOME/liquify/config`, or `~/.config/liquify/config`

The supported keys are `config.liquify.ai.enabled`, `config.liquify.ai.context_bytes`,
`config.liquify.ai.model`, and `config.liquify.ai.max_tokens`. Defaults are off, 65,536 context
bytes, `claude-opus-5`, and 4,096 output tokens.

## Off means offline

When the effective switch is off, the application does not query the keychain and `Assistant`
does not invoke its provider factory. No provider object, resolver, TLS stream, socket, or request
exists. The panel remains visible and says that AI is off. `test_ai.cpp` gives the disabled
assistant a provider factory aimed at a connection-counting HTTPS double and proves both the
provider construction count and accepted-connection count remain zero. The macOS smoke job also
uses `lsof` throughout an AI-off run and fails if the process owns a TCP connection.

Turning AI off cancels an active request before destroying the provider. Turning it on reads the
key from the OS credential store or, if absent, `ANTHROPIC_API_KEY`. The UI reports only
`configured` or `not configured`. Secret bytes are not stored in repository metadata, the global
configuration, the assisted-edit log, status text, or request errors.

## Inspectable graph context

`EditorGraph::request_ai_context` assembles facts from the in-process `InteractiveAnalysis`,
compiler symbol records returned by `liquid::repository::symbols`, the build plan and latest build
result, and liquid's recorded build explanation. It does not search source text for related files.
For the cursor, or the selected range when one exists, `ContextBuilder` renders:

1. the selected source and enclosing definition;
2. the enclosing symbol record;
3. referenced definitions with signature-only stubs and locations;
4. diagnostics for the file and its translation unit;
5. the latest result for the file's build action, including cache status and `why`;
6. dependency edges one hop into and out of that action.

The block starts with `[liquify-context-v1]`. Its byte budget is applied in the order above. Every
excluded category and count is listed under `[omissions]`, and the final block never exceeds the
configured byte count. The **What will be sent** button, Primary+Shift+C, and the matching palette
command show the exact block without contacting a provider.

## Provider contract

`AiProvider` is plain C++: `complete(request, callbacks)` delivers text deltas and one terminal
result; `cancel()` stops the current operation. `AnthropicProvider` implements the Anthropic
Messages API with Corosio TLS streams and Boost.HTTP/1.1 on liquid's Capy runtime context. The UI
thread only starts or cancels an operation and receives state snapshots.

The provider sends `POST /v1/messages` with `x-api-key`, `anthropic-version: 2023-06-01`, and
`content-type: application/json`. The JSON contains `model`, `max_tokens`, `system`, `messages`,
and `stream: true`; it deliberately has no `thinking` member. Server-sent `content_block_delta`
events become text deltas. `message_delta.stop_reason` and `message_stop` produce the terminal
result. A `refusal` is a distinct successful stop reason shown as a refusal in the panel. An
in-process TLS double verifies the request and covers a normal answer, refusal, and disconnection
before `message_stop`; tests never contact a public endpoint.

## Commands and edits

The Assistant panel asks about the current selection or definition, explains a diagnostic, and
requests a replacement edit. All commands are listed by Primary+Shift+P:

| Command | Binding |
|---|---|
| Ask | Primary+Shift+A |
| Explain diagnostic | Primary+Shift+E |
| Propose edit | Primary+Shift+I |
| Accept edit | Primary+Enter |
| Reject edit | Primary+Backspace |
| Toggle | Primary+Shift+L |
| What will be sent | Primary+Shift+C |

A proposed replacement remains a `SourceEdit` preview until explicit acceptance. Rejection clears
it without changing the buffer. Acceptance validates its byte range, applies it to the current
buffer, and performs one normal `repository::write`, so authorship remains the accepting user and
the change is one drip. Because the current liquid record has no assisted flag, liquify appends a
local `.liquid/config.liquify.ai.log` JSON record with `assisted: true`, path, range, and timestamp.
It never records context, prompts, responses, source text, or credentials.

Agent sessions, autonomous promotion, model tool use, and a configurable local provider are
deferred. `AiProvider` is the boundary for the later local provider.
