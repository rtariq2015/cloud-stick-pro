# Native CI acceptance only: install into a fresh temporary directory, run both
# programs with synthetic data, then uninstall. No USB/Bluetooth access.
param([string]$ArtifactDir = "desktop/dist/releases")
$ErrorActionPreference = "Stop"
if ($env:OS -ne "Windows_NT") { throw "This test requires Windows" }
$prefix = Join-Path $env:RUNNER_TEMP "Cloud Stick Setup acceptance"
if (Test-Path $prefix) { throw "Synthetic installation target already exists" }
$installers = @(Get-ChildItem $ArtifactDir -Filter "cloud-stick-*-windows-x86_64-setup.exe")
if ($installers.Count -ne 1) { throw "Expected exactly one native installer" }
$report = @{ schema = "cloud-stick-windows-install-test/v1"; passed = $false; hardware_contact = $false; installed = $false; gui_smoke = $false; helper_self_test = $false; uninstalled = $false }
function Run-Checked([string]$File, [string[]]$Arguments) {
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Native program returned a failure" }
}
try {
    Run-Checked $installers[0].FullName @('/SP-', '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$prefix`"")
    $app = Join-Path $prefix "cloud-stick.exe"
    $helper = Join-Path $prefix "firmware-helper/cloud-stick-installer.exe"
    if (-not (Test-Path $app) -or -not (Test-Path $helper)) { throw "Installed programs are incomplete" }
    $report.installed = $true
    Run-Checked $helper @('--self-test', '--report', 'desktop/installed-helper-self-test.json')
    $result = Get-Content desktop/installed-helper-self-test.json -Raw | ConvertFrom-Json
    if ($result.passed -ne $true -or $result.hardware_contact -ne $false -or $result.windows_private_apis -ne $true) { throw "Installed helper proof failed" }
    $report.helper_self_test = $true
    Run-Checked $app @('--smoke-test', '--smoke-panel', 'firmware', '--report', 'desktop/installed-smoke.json', '--screenshot', 'desktop/installed-smoke.png')
    $result = Get-Content desktop/installed-smoke.json -Raw | ConvertFrom-Json
    if ($result.passed -ne $true -or $result.hardware_contact -ne $false) { throw "Installed GUI proof failed" }
    $report.gui_smoke = $true
    $uninstallers = @(Get-ChildItem $prefix -Filter "unins*.exe")
    if ($uninstallers.Count -ne 1) { throw "Expected one per-user uninstaller" }
    Run-Checked $uninstallers[0].FullName @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    if ((Test-Path $app) -or (Test-Path $helper)) { throw "Uninstall left a program behind" }
    $report.uninstalled = $true
    $report.passed = $true
} finally {
    $report | ConvertTo-Json | Set-Content desktop/windows-install-test.json
}
