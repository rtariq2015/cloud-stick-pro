param(
    [string]$Python = "python",
    [string]$InnoSetupCompiler = ""
)
# Run in PowerShell on an actual Windows x64 host. No host configuration changes.
$ErrorActionPreference = "Stop"
if ($env:OS -ne "Windows_NT") { throw "Build Windows releases on Windows; PyInstaller is not a cross-compiler." }
$DesktopDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $env:SOURCE_DATE_EPOCH) { $env:SOURCE_DATE_EPOCH = "0" }
function Run-Python {
    & $Python @args
    if ($LASTEXITCODE -ne 0) { throw "Python command failed with exit code $LASTEXITCODE" }
}
Run-Python "$DesktopDir\packaging\check_build_environment.py" win32
Run-Python -m pip check
Run-Python "$DesktopDir\packaging\verify_firmware.py" "$DesktopDir\firmware"
Run-Python -m PyInstaller --clean --noconfirm --distpath "$DesktopDir\dist" --workpath "$DesktopDir\build\installer" "$DesktopDir\packaging\firmware-helper.spec"
Run-Python -m PyInstaller --clean --noconfirm --distpath "$DesktopDir\dist" --workpath "$DesktopDir\build\pyinstaller" "$DesktopDir\packaging\cloud-stick.spec"
Copy-Item -Recurse "$DesktopDir\dist\cloud-stick-installer" "$DesktopDir\dist\cloud-stick\firmware-helper"
Run-Python "$DesktopDir\packaging\package_artifacts.py"
if ($InnoSetupCompiler) {
    $AppVersion = & $Python "$DesktopDir\packaging\package_artifacts.py" --version
    if ($LASTEXITCODE -ne 0) { throw "Cannot read application version" }
    & $InnoSetupCompiler "/DAppVersion=$AppVersion" "/DBundleDir=$DesktopDir\dist\cloud-stick" "/DArtifactDir=$DesktopDir\dist\releases" "$DesktopDir\packaging\windows\cloud-stick.iss"
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE" }
    Run-Python "$DesktopDir\packaging\package_artifacts.py" --checksums-only
}

