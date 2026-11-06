# How Cloud Stick Pro is maintained

The aim is a small, understandable project: focused fixes, relevant tests, clear
revision notes and reviewed releases. GitHub is the shared record; it does not
give an AI agent continuous access to the dongle or automatically fix bugs.

## Checks configured in this repository

| Check | When | What it does |
|---|---|---|
| Public repository checks | Push to `main`, pull request, manual run and Monday 13:23 UTC | Tests the publication checker, checks the approved source file set, curated Markdown/HTML links, PNG headers and basic private-key/token markers |
| Official upstream notice | Push to `main`, weekly or manual run | Reads the official Cloud Stick main commit and license endpoint; reports review required if the reviewed revision changes or a license file appears |
| Dependabot for GitHub Actions | Weekly | Proposes updates to the SHA-pinned workflow action; never merges or releases them automatically |
| Companion interface tests | Main push, pull request, manual and weekly | Runs desktop and Android JavaScript tests without a USB or phone |

The upstream monitor uses two fixed HTTPS GitHub API endpoints, finite timeouts,
bounded response sizes, no credentials and no redirects. Network/API failure is
reported as a check failure, not an unchanged or successful result. It does not
contact the original author, copy new code, change files or accept a license for us.

Check [Actions](https://github.com/rtariq2015/cloud-stick-pro/actions) for actual run
results. A configured workflow is not evidence that GitHub has run it; disabled
Actions, account policy, API availability or scheduling delays may prevent a run.
GitHub can disable scheduled workflows in inactive public repositories. Configure
GitHub notification preferences to receive failures; this project has not changed
your account's notification settings.

The checker is a publication guard, **not a comprehensive secret scanner**. Its
file policy admits the exact reviewed development source and maintenance files.
A maintainer must review policy and workflow changes; a malicious pull request
could change its own checks, so a green result cannot replace review. Link checks
cover the selected guides; the preserved upstream README has its documented
missing LICENSE link. The workflow also runs desktop and Android interface logic
tests. It runs no firmware build, device, Wi-Fi, real cloud or hardware tests, so
a green check is not physical acceptance or a complete product build.

## The process for a fix

1. Record the actual problem in an issue using sanitized, synthetic evidence.
2. Make the smallest useful change on a development branch and add a regression
   for the failure. Avoid unrelated redesigns or unnecessary new dependencies.
3. Run affected tests. Record desktop simulation separately from physical tests,
   including exact source/image identities and meaningful memory/size changes.
4. Review the pull request, notices, private-data exclusions and test result.
   A dependency update needs the same review; a green documentation check is not
   permission to merge a firmware change.
5. Bump the affected component version and write release notes. Preserve immutable
   previous artifacts and a machine-readable hash manifest. Publish a new tag and
   release, rather than silently replacing an old download.
6. For firmware, require a fresh private recovery checkpoint, board/layout
   compatibility, app-only installation and relevant hardware acceptance. Never
   alter USB storage ownership, settings, receipts, TLS checks or authentication
   just to get a test passing.

The upstream license-text gap remains documented alongside the published source;
no replacement grant or blanket license has been invented. New files need an
explicit reviewed policy expansion. Actual firmware build and native application
packaging jobs must be added before those checks can be claimed. Signing keys, live flash/NVS, owner cards and user files
stay outside GitHub and Actions. No unattended firmware flashing, cloud-account
access, deployment, release signing or automatic code merge is configured.

## What needs a working session

Reviewing reported bugs, diagnosing new device failures, implementing fixes and
validating hardware require an active maintainer or a separately configured coding
session. The scheduled checks above provide bounded monitoring; they are not an
always-running AI support service. There is no guaranteed response time or
production support window. The user does not need to learn coding to report a
problem or install a reviewed package using [the update guide](UPDATING.md).

## Workflow references

The workflow uses GitHub's documented
[event and permission syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax),
[Dependabot configuration](https://docs.github.com/en/code-security/reference/supply-chain-security/dependabot-options-reference)
and [SHA-pinned action guidance](https://docs.github.com/en/actions/security-for-github-actions/security-guides/security-hardening-for-github-actions).
The checkout pin was verified against the official
[`v7.0.1` tag](https://api.github.com/repos/actions/checkout/git/ref/tags/v7.0.1)
on 6 November 2026. Runtime access is read-only and checkout credentials are not
persisted. Untrusted pull-request code never runs through `pull_request_target`.
