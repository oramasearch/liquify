<!--
DRAFT for https://github.com/oramasearch/liquid-bridge/issues/11. Not in force.

Before this policy takes effect, the owner:
  1. replaces every [[PLACEHOLDER: ...]] (the reporting address, the PGP key, and every response time);
  2. answers every "Question for the owner" block and deletes the block;
  3. turns on "Private vulnerability reporting" under Settings -> Code security;
  4. deletes this comment and the "Draft" notice below.
The pre-public checklist that tracks these steps is liquid-bridge docs/security/pre-public-checklist.md.
liquid carries the same policy in its own SECURITY.md; keep the two in step.
-->

# Security policy

> **Draft, not in force.** The reporting address, the response times and the safe-harbour wording
> below are waiting on the owner's approval. Until then, report through GitHub private vulnerability
> reporting only.

liquify is the native IDE for liquid. It opens repositories other people wrote, parses their source
with tree-sitter, runs liquid's build graph in-process, loads plugins, can send context to an AI model
provider when the user turns that on, and edits with other people in real time. We treat every report
about it seriously.

## Reporting a vulnerability

**Do not open a public issue, pull request or discussion for a security problem.**

Report it privately:

1. **GitHub private vulnerability reporting** (preferred): *Report a vulnerability* on this
   repository's *Security* tab. Only the maintainers can see the advisory it creates.
2. **Email:** [[PLACEHOLDER: REPORTING_ADDRESS]]. To encrypt, use the PGP key
   [[PLACEHOLDER: PGP_FINGERPRINT]], published at [[PLACEHOLDER: PGP_KEY_URL]].

Include what you can of:

- the version (`liquify --version`) or the commit;
- the platform (macOS, Linux or Windows) and how liquify was installed;
- what was involved: opening a repository, a plugin, the AI assistant, multiplayer, settings, or an
  installer;
- what the problem is and what an attacker gains;
- how to reproduce it: a repository or file that triggers it, and any crash output;
- whether it is already public, and whether an automated tool found it.

A person, not an autoresponder, acknowledges every report.

## What you can expect from us

| Step | Target |
|---|---|
| Acknowledge the report | [[PLACEHOLDER: ACK_TARGET]] |
| First assessment: confirmed or declined, with a severity | [[PLACEHOLDER: TRIAGE_TARGET]] |
| Status updates while the report is open | [[PLACEHOLDER: UPDATE_INTERVAL]] |
| Fix released, **critical** (code execution from an opened repository, plugin escape, key or credential disclosure) | [[PLACEHOLDER: CRITICAL_FIX_TARGET]] |
| Fix released, **high** | [[PLACEHOLDER: HIGH_FIX_TARGET]] |
| Fix released, **medium** | [[PLACEHOLDER: MEDIUM_FIX_TARGET]] |
| Fix released, **low** | [[PLACEHOLDER: LOW_FIX_TARGET]] |

> **Question for the owner (response times):** the September draft proposed 3 business days to
> acknowledge, 7 days to assess, updates every 14 days, and fixes in 14 / 30 / 90 days by severity,
> low in the next release. Keep those, change them, or publish the policy without numbers? Each
> number here is a public commitment, and liquid's policy should carry the same ones.

These are targets, not guarantees. If we are going to miss one, we tell you why and give a new date.
When a fix ships we publish a GitHub security advisory, request a CVE through GitHub, and credit you in
the advisory unless you prefer not to be named.

## Coordinated disclosure

We ask that you keep the details private until a fix is released or
[[PLACEHOLDER: DISCLOSURE_WINDOW, proposed 90 days]] have passed since your report, whichever comes
first. If a fix needs longer we will ask for an extension and say why. If we stop answering and do not
reply to a reminder, you may publish once that window has passed.

A problem that lies in liquid (the library liquify links) is handled under
[liquid's policy](https://github.com/oramasearch/liquid/blob/dev/SECURITY.md); report it to either
repository and we move it. A problem in another third-party component we coordinate with its
maintainers.

We do not run a bug bounty.

## Safe harbour

> **Question for the owner (legal wording, needs approval and possibly counsel):** the text below is
> the September draft's proposed safe harbour, the same as liquid's. It promises not to pursue legal
> action under anti-hacking and anti-circumvention laws or the licence, and to say publicly that
> compliant research was authorised. Approve it as written, replace it with a standard text (for
> example the disclose.io core terms), or narrow it? It names U.S. and U.K. statutes; the company's
> own jurisdiction may call for different ones.

*Proposed text, not in force:*

We consider security research done in good faith and in line with this policy to be authorised. For
such research we will not pursue or support legal action against you under anti-hacking laws (such
as the U.S. Computer Fraud and Abuse Act or the U.K. Computer Misuse Act), anti-circumvention laws
(such as DMCA §1201) or this repository's licence terms, and we will not report you to law
enforcement. If a third party takes legal action against you for research that followed this policy,
we will make it known that your research was authorised.

Good faith means that you:

- test only your own installations and repositories, or ones you are allowed to use, and never
  another person's data, keys, model-provider account or editing session without their consent;
- run no denial-of-service or volumetric tests against any system you do not own, including the
  hosted service Lake and any model provider;
- use no social engineering, phishing or physical attacks against maintainers, contributors or users;
- access, keep or change only the data needed to show the problem, stop once you have shown it, and
  delete what you kept when the report is closed;
- report promptly, exploit the problem no further than the demonstration, and keep it private until
  the agreed date;
- follow the law that applies to you. This policy cannot authorise you to break a law we are not party
  to, and it does not bind anyone else.

If you are unsure whether something is covered, ask first through either reporting channel.

## Scope

**In scope:** this repository, `oramasearch/liquify`, and the installers and packages built from it.

| Area | Where |
|---|---|
| Opening a repository: names, file contents, the persisted analysis state under `.liquid/`, the tree-sitter parsers, the stub trees and the graph queries | `src/app.cpp`, `src/editor_buffer.cpp`, `src/editor_graph.cpp`, `src/interactive_analysis.cpp`, `src/stub_tree.cpp`, `src/repository_view.cpp` |
| Plugins: where they are found and loaded, what they can reach | `src/plugin.cpp`, `plugins/`, [`docs/plugins/`](docs/plugins/README.md) |
| The AI assistant: what is sent to a provider, where the provider key is kept, how model-proposed edits are applied | `src/ai_provider.cpp`, `src/ai_config.cpp`, `src/assistant.cpp`, `src/context_builder.cpp`, `src/structural_edit.cpp`, [`docs/editor/ai.md`](docs/editor/ai.md) |
| Multiplayer: the connection to other editors and the data they send | `src/multiplayer.cpp`, [`docs/multiplayer.md`](docs/multiplayer.md) |
| Settings and first run: files written and what an opened repository can change | `src/settings.cpp`, `src/first_run.cpp`, [`docs/ui/settings.md`](docs/ui/settings.md) |
| Installers and packages | `packaging/`, [`docs/packaging/`](docs/packaging/README.md) |
| The build itself: a way to make the CMake configure fetch or run something other than the pinned dependencies | `CMakeLists.txt`, `.gitmodules` |

Opening an untrusted repository is the everyday use, not an edge case. Code running because a
repository was opened, a plugin loaded from a place the repository controls, repository text that
steers the AI assistant into an edit or a disclosure the user did not ask for, a provider key or other
credential written where others can read it, a peer in a shared session reading or writing outside
what was shared, or a crash or unbounded allocation on a malformed file are vulnerabilities to us.

**Out of scope**

- The hosted service **Lake** and its infrastructure. It has its own policy at
  [[PLACEHOLDER: LAKE_SECURITY_POLICY_URL]].
- The behaviour of a model provider itself, and what it does with data the user chose to send it.
- A problem only in a third-party dependency with no liquify-specific trigger (SDL3, Dear ImGui,
  tree-sitter and its grammars, OpenSSL). Report it upstream and tell us, so we can move to a fixed
  version.
- Problems that need the attacker to already have the user's operating-system account, root, or
  physical access.
- Missing hardening that is not itself exploitable. Suggestions are welcome as ordinary issues.
- Automated scanner output with no analysis of exploitability.

## Supported versions

> **Question for the owner (support window):** proposed: before 1.0 only the latest release is
> supported, and fixes ship in the next release cut from `dev`. Confirm or change.

| Version | Supported |
|---|---|
| Latest release | [[PLACEHOLDER: yes]] |
| Earlier releases | [[PLACEHOLDER: no]] |

Thank you for helping keep liquify safe for the people who trust it with their code.
