# Contributing to Cloud Stick Pro

Start with the [developer guide](docs/DEVELOPER_GUIDE.md) and
[project layout](docs/PROJECT_LAYOUT.md). The firmware, desktop application and
Android viewer have different entry points, versions and acceptance requirements.
Keep a change focused on its actual component.

## Before a change

- Check the branch, commit, working-tree state and current
  [progress/release index](docs/PROJECT_HISTORY.md). Preserve
  uncommitted work and historical test failures.
- Describe the concrete behavior to improve and the smallest affected boundary.
  For a large change, agree a scoped design before adding new transports,
  persistent formats, dependencies or background services.
- Read [credits and license evidence](docs/CREDITS.md). Upstream's missing license
  text is unresolved; do not invent a grant or publish a package as license-complete.
  Retain author headers, dependency notices and copied-source provenance.

## Implementation expectations

Preserve the host-only microSD writer, generation-bound firmware reads, verified
receipt semantics, exact device binding and acknowledged lifecycle cleanup.
Keep Security 2 and TLS checks intact. A timeout or failed cleanup must not trigger
an unsafe reset, free uncertain ownership, erase data or downgrade authentication.

Review and Save are separate actions. Preserve active settings until a new record
commits successfully, and report an unknown outcome honestly. UI navigation or
late asynchronous responses must not imply successful authentication or saving.

Keep streaming and fixed/bounded storage appropriate to the original board's
limited RAM. Measure before claiming an optimization. UI work should be readable
on the actual display and avoid adding hidden scans, network polling or expensive
assets. Comments should explain ordering, ownership, limits and failure behavior,
not narrate assignments or claim unperformed tests.

## Validation and review

Run the tests appropriate to the change using the
[test navigation](docs/DEVELOPER_GUIDE.md#test-the-affected-behavior). Add meaningful
regressions for the observed failure and its boundary cases. Preserve sanitizer
diagnostics and failed attempts; do not suppress them into a passing result.

For a review, provide:

1. The problem, concrete resulting behavior and affected component/profile.
2. Source commit and dependency/provenance changes, if any.
3. Relevant commands/results, including negative, cancellation and cleanup cases.
4. Measured artifact size/memory changes for embedded or packaged changes.
5. Physical tests actually run, their exact image identity and unresolved limits.

Native mocks, browser/Qt previews and real hardware results must stay distinct.
Use disposable synthetic files for acceptance fixtures. Keep a candidate separate
from historical release profiles and record its app/partition hashes before any
reviewed installation. Source approval, hardware operations and publication are
different actions; do not turn a build or test command into an upload.

## Bug reports and private data

Include component/version, board/OS model, a short reproduction and fixed error
codes. State whether the problem occurs during discovery, authentication, saving,
backup, verification or cleanup. A redacted synthetic example is more useful than
an unrestricted diagnostic dump.

Do not attach account credentials, private owner cards, raw flash/NVS backups,
clinical files, private file history, signing keys or tokens. Use the
[security reporting policy](SECURITY.md#reporting-a-security-problem) for a
vulnerability. Do not put exploit details or private material into a public issue.

Keep revision notes and evidence in order so a later contributor can see what
was attempted, fixed, tested and still pending without repeating unsafe work.
