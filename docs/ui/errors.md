# Errors

A failure is shown as one error component: a sentence in the product's words, the step that fixes
it, and liquid's own message behind a **Details** button. It is never raw text alone, and never an
empty list (the rule lake's interface follows, lake `docs/ui/design-system.md`, *Components and
pages*).

## The model

`include/liquify/user_error.hpp` holds the plain C++ model; `src/app.cpp` draws it
(`App::draw_user_error`), because only that file may name ImGui.

| Field | Shown as |
|---|---|
| `summary` | One line in the danger role, naming what failed and where (the lake's host, the folder) |
| `next_step` | One line of body text: what the person does next |
| `details` | liquid's `context: message`, hidden until **Details** is pressed |
| `kind` | Not shown; it chooses the words and decides whether the failure is "offline" |

No field carries an absolute local path the person did not type. `without_absolute_paths` keeps a
path inside the repository relative to it, says "the repository" for the root, and reduces any
other absolute path (macOS `/private` aliases included) to its file name. URLs are left as they are.

## Lake failures

`classify_lake_failure` reads liquid's error code (`liquid::errc`) and gives one kind per class.
Each class has its own sentence and next step (`describe_clone_failure`):

| liquid's code | Kind | Sentence (clone) |
|---|---|---|
| `unavailable`, with a resolver or connect message (not known, refused, timed out, unreachable, no route) | offline | Could not reach *host*. The next step says working offline is normal. |
| `unavailable`, any other message (the TLS handshake), or `io` naming TLS or a certificate | secure connection | *host* answered, but a secure connection could not be made |
| `io`, otherwise | interrupted | The connection to *host* broke during the clone |
| `unauthorized` | unauthorized | *host* did not accept your token |
| `unauthorized`, with HTTP 403 or "forbidden" in the message | forbidden | Your token has no access to this repository on *host* |
| `not_found`, `not_remote` | not found | *host* has no repository at this address |
| `busy`, `locked`, `unavailable` with liquid's "returned HTTP 503" | lake unavailable | *host* cannot serve the repository right now |
| `unsupported`, `invalid_argument`, `corrupt`, `conflict` | unsupported lake | *host* answered in a way this liquify does not understand |
| a refused reference in the sync report | refused | *host* refused to send *reference* |
| a clone target that exists and is not empty | local | The folder *name* cannot hold the clone |

**"Offline is normal" appears only for the offline kind.** Only that kind puts the front door in
the `offline` lifecycle phase; every other kind is `failed`, because the lake answered.

liquid's HTTP client reports DNS, connect and TLS handshake failures with the same code,
`unavailable`, and its sync endpoint replaces the host context with `sync endpoint`. So, for that
one code, the kind is read from the operating system's message: the resolver and connect messages
listed above mean offline, and anything else is the handshake. The macOS TLS defect of liquid#368
arrives as `unspecified system_category error` and is classified as a secure-connection failure.
When liquid reports the stage itself ([liquid#530](https://github.com/oramasearch/liquid/issues/530)), this table reads it instead of the message.

## How to verify

- `ctest -R UserError` checks every class from liquid's codes, that only the offline class says
  offline is normal, that each class has its own sentence and step, and the path reduction.
- `liquify-native-acceptance-front-door-clone-failures` opens the real GPU window with no
  repository, presses **Clone from lake**, and clones from a loopback responder that answers 401,
  403, 404, 503 and 400, from the same responder over `https` (a real TLS handshake failure), and
  from a port nothing listens on. For each it requires the kind, the phase, the sentence and the
  Details control, opens Details, and saves the framebuffer in light and dark as
  `build/tests/native-acceptance/front-door-clone-failures/clone-<kind>-<theme>.png`.

## How to change

Add a kind to `FailureKind`, its row to `classify_lake_failure`, its words to the `describe_*`
function, a case to `tests/test_user_error.cpp`, and a case to the native scenario. Keep the
sentence in the product's words and the next step something the person can do.
