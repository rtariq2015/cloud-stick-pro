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
| Saved Wi-Fi policy | Main push, pull request, manual and weekly | Compiles historical, disabled and fair-retry native policy checks with sanitizers; simulates connection timing without hardware |
| Upload folder setup | Main push, pull request, manual and weekly | Exercises actual DAV and receipt code with simulated HTTP/TLS/storage; checks reduced request counts and failure recovery, without hardware or a cloud account |
| Native desktop installers | Desktop changes on main, desktop pull request or manual run | Builds Linux and Windows GUI + independent installer, checks signed offline data, runs native software/fault tests and synthetic window smoke; never contacts or flashes hardware |
| Wi-Fi wake policy / private observer | Main, pull request and scheduled public checks | Runs native sanitizer policy/observer tests, actual Java-to-mbedTLS P-256 verification and bounded loopback TCP; NVS/radio/auth/RNG/heap are simulated; no device acceptance |
| NVS cache cleanup | Main push, pull request, manual and weekly | Tests cleanup and service-fixture sources with synthetic SDK/flash, plus actual feature-header admission; no hardware access |

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
explicit reviewed policy expansion. Firmware build jobs remain separate. The native desktop packaging workflow
records actual run results before a native-build claim is made. Signing keys, live flash/NVS, owner cards and user files
stay outside GitHub and Actions. No unattended firmware flashing, cloud-account
access, deployment, release signing or automatic code merge is configured.

## What needs a working session

### Progress and saving verified milestones

During an active agent session, give a brief update at meaningful milestones and
approximately once per minute during longer work. Explain what changed, what was
verified and what remains. Report failures and investigate them; an attempted
command, queued check or unverified upload is not a successful result.

For tasks where the owner has authorized repository changes and publication:

1. Recheck the branch, intended remote, upstream, staged files and working tree.
   Check for concurrent activity before editing and again before committing.
   Preserve unrelated work; use an isolated worktree when another session is
   editing unrelated files. Pause for overlapping edits or an unexpected branch
   change rather than overwriting another session's work.
2. Complete a useful milestone and run the checks appropriate to its changes.
   Review the staged diff and the explicit publication allowlist. Stage only the
   intended files. Keep incomplete or failing work, credentials, private device
   evidence and unrelated edits out of publication. Never use a blanket add or
   force-push to save time.
3. Make a clear commit and push the verified milestone to the intended branch in
   `rtariq2015/cloud-stick-pro`. Use a development branch and pull request for
   changes to `main`; confirm the relevant checks pass before merging. Recheck
   the remote head to avoid racing another publisher. Investigate failed checks
   instead of weakening them or treating a partial result as complete.
4. Report the branch, commit identifier, push result and GitHub commit or pull
   request link. Distinguish local commits, successful remote publication and
   pending or completed CI. If terminal networking/authentication is unavailable,
   state that limitation; an authorized GitHub connector may publish the exact
   reviewed change, but does not establish that terminal `git push` works.

The project VS Code settings save edited files locally after 1.5 seconds and
require deliberate Git staging. Auto Save does not create commits or push files.
Commits record verified milestones; successful pushes save them to GitHub. These
instructions apply while an agent session is running. They do not create a timer,
continuous agent, unattended commits or background repair service.

GitHub repository publication and website deployment are separate operations.
Leave GitHub Pages disabled until the owner explicitly chooses to publish a
website. No Pages deployment, automatic merge, firmware flashing or signing is
part of this workspace setup.

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
