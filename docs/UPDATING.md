# Getting Cloud Stick Pro updates

You do not need to know how to code to follow the project. Use this repository's
[release notes](RELEASE_NOTES.md) to see what changed, and
[Releases](https://github.com/rtariq2015/cloud-stick-pro/releases) for downloadable
packages **when a reviewed release is published**. Choose **Watch → Custom →
Releases** on GitHub if you want release notifications.

## What is available now

The public repository currently contains the overview, synthetic screenshots,
release notes and maintenance tooling. **There is no public firmware or app
download yet.** Reviewed local candidates are firmware **0.4.3-ui-dev**, Linux
setup **0.5.3** and Android viewer **0.1.2**. Windows **0.5.3** is a shared-source
build recipe, not a tested Windows executable. These are separate version numbers.

The original [Cloud Stick by John / JMcrafter26](https://github.com/JMcrafter26/cloud-stick)
states MIT in its README, but its linked license grant file is missing from the
reviewed upstream tree. Derived source and installers remain local until that
publication issue is resolved. Attribution is retained; no substitute upstream
license has been invented. Do not treat GitHub's automatic source ZIP for this
documentation repository as the complete installable product.

## When downloadable releases are ready

1. Open the latest non-prerelease on **Releases** and read its compatibility and
   known-limit notes. A prerelease is a test candidate.
2. Pick the package for your component and operating system. The firmware target
   is the **original LILYGO T-Dongle S3, 16 MiB flash, no PSRAM**; a generic USB
   drive, C5 or S3 Plus is not interchangeable.
3. Compare the download's SHA-256 with the release manifest. Keep a copy of the
   previous installer and follow the release's recovery instructions.
4. Close the desktop app before replacing it. Android updates must retain the
   existing signing identity. Installing an app does **not** update the dongle.
5. Firmware requires its own reviewed, app-only procedure and current private
   recovery backup. The desktop's current firmware feature reviews a manifest;
   it does not provide a general update writer. Do not erase all flash, format
   microSD or replace settings/receipts as an ordinary update.

Nothing in this GitHub workflow remotely updates or restarts your device. There
is no automatic migration of your account, Wi-Fi or cloud settings.

## If something goes wrong

Open [Issues → New issue](https://github.com/rtariq2015/cloud-stick-pro/issues/new/choose),
choose **Report a problem**, and describe the few steps that caused it. Include
the component/version and error text. Remove passwords, owner cards, private
keys, backups, clinical files and private history before sharing any evidence.

A failed upload and a verified backup are different outcomes. Preserve your local
originals. Do not install an older firmware over a live device unless its exact
configuration/receipt compatibility and rollback procedure have been reviewed.

See [how maintenance works](MAINTENANCE.md) for automated checks and their limits.
