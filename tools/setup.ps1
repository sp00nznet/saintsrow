# Saints Row recomp: one-shot setup. Run through Setup.cmd, or by the
# recomp-launcher installer as: setup.ps1 -Yes -Game <disc image or folder>.
#
# Does exactly what the README's "Step by step" section does, in order, and
# skips any step whose output already exists, so a rerun after a failure
# resumes. Details go to setup.log; the console gets one "[n/6]" line per step
# (the installer's progress bar reads them). It never downloads game data: you
# point it at your own copy.

param(
    [switch]$Yes,          # install missing tools without asking
    [string]$Game = ''     # disc image (.iso), a folder with default.xex, or a folder holding the .iso
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Log = Join-Path $Root 'setup.log'
$Data = Join-Path $Root 'extracted'
$Llvm = 'C:\Program Files\LLVM\bin'

# Pinned sources. Bump these together with the CHANGELOG.
$Sdk = Join-Path $Root 'tools\rexglue-sdk'
$SdkRepo = 'https://github.com/sp00nznet/rexglue-sdk'
$SdkCommit = 'd50d599'   # sp00nznet/rexglue-sdk: upstream development@bd833a2 + our fixes
$SdkInstall = Join-Path $Sdk 'out\install\win-amd64'
$Tools360 = Join-Path $Root 'tools\360tools'
$Tools360Commit = '1b53928'

"=== setup $(Get-Date -Format s)" | Out-File $Log -Append -Encoding utf8

$script:n = 0
function Step($name) { $script:n++; Write-Host "[$script:n/6] $name" }
function Fail($msg) {
    Write-Host ''
    Write-Host "Setup stopped: $msg" -ForegroundColor Red
    Write-Host "Details are in $Log"
    exit 1
}
# Run a native command, logging its output; stop on a non-zero exit. Only the
# exit code counts: Windows PowerShell turns a native program's stderr (git's
# "Cloning into ...") into error records, which 'Stop' would treat as fatal.
function Run($what, [scriptblock]$cmd) {
    $ErrorActionPreference = 'Continue'
    & $cmd *>> $Log
    if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit $LASTEXITCODE)." }
}
function Ask($q) { $Yes -or ((Read-Host "$q [y/N]") -match '^[yY]') }
function Clone($url, $dir, $commit) {
    if (-not (Test-Path (Join-Path $dir '.git'))) {
        Run "Cloning $url" { git -c core.longpaths=true clone $url $dir }
    }
    Run "Checking out $commit" { git -C $dir fetch --quiet origin; git -C $dir checkout --quiet $commit }
}

# --- 1. Prerequisites ---------------------------------------------------------
Step 'Checking prerequisites'
if (Test-Path $Llvm) { $env:PATH = "$Llvm;$env:PATH" }
$need = @(
    @{ exe = 'git';    id = 'Git.Git';            why = 'fetches the ReXGlue SDK';   size = '~60 MB' },
    @{ exe = 'cmake';  id = 'Kitware.CMake';      why = 'configures the build';      size = '~35 MB' },
    @{ exe = 'ninja';  id = 'Ninja-build.Ninja';  why = 'runs the build';            size = '~1 MB' },
    @{ exe = 'clang';  id = 'LLVM.LLVM';          why = 'compiles everything';       size = '~400 MB' },
    @{ exe = 'py';     id = 'Python.Python.3.12'; why = 'extracts the disc image';   size = '~30 MB' }
)
$installed = $false
foreach ($t in $need) {
    if (Get-Command $t.exe -ErrorAction SilentlyContinue) { continue }
    Write-Host "   Missing: $($t.exe) -- $($t.why), $($t.size) via winget ($($t.id))."
    if (-not (Ask '   Install it now?')) { Fail "$($t.exe) is required. Install it, open a new window, and run Setup.cmd again." }
    Run "Installing $($t.exe)" { winget install --id $t.id -e --accept-package-agreements --accept-source-agreements }
    $installed = $true
}
if ($installed) {
    # Pick up the new tools without a new window.
    $env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH', 'User')
    if (Test-Path $Llvm) { $env:PATH = "$Llvm;$env:PATH" }
}
# clang needs the Windows SDK and MSVC libraries from Visual Studio or its Build Tools.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere) -or -not (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)) {
    Write-Host '   Missing: Visual Studio 2022 Build Tools with "Desktop development with C++" (~3 GB).'
    if (-not (Ask '   Install it now?')) { Fail 'The MSVC libraries and Windows SDK are required.' }
    Run 'Installing VS Build Tools' { winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-package-agreements --accept-source-agreements --override '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' }
}

# --- 2. Game data -------------------------------------------------------------
Step 'Game data'
if (-not (Test-Path (Join-Path $Data 'default.xex'))) {
    $src = $Game
    if (-not $src) {
        if ($env:RECOMP_LAUNCHER) { Fail 'No game given (-Game).' }
        $src = Read-Host '   Path to your Saints Row disc image (.iso), or a folder holding it or default.xex'
    }
    $src = $src.Trim('"')
    if (-not (Test-Path $src)) { Fail "'$src' doesn't exist." }
    $src = (Resolve-Path $src).Path
    if (Test-Path (Join-Path $src 'default.xex')) {
        New-Item -ItemType Directory -Force $Data | Out-Null
        Copy-Item -Recurse -Force "$src\*" $Data
    } else {
        $iso = if (Test-Path $src -PathType Leaf) { $src } else {
            Get-ChildItem $src -File -Recurse -Depth 3 -Filter *.iso | Select-Object -First 1 -ExpandProperty FullName
        }
        if (-not $iso) { Fail "No default.xex or .iso found in '$src'." }
        Write-Host "   Extracting $iso"
        Clone 'https://github.com/sp00nznet/360tools' $Tools360 $Tools360Commit
        Run 'Extracting the disc image' { py -I "$Tools360\tools\extract_iso.py" $iso $Data }
    }
    if (-not (Test-Path (Join-Path $Data 'default.xex'))) { Fail 'extracted\default.xex is missing. Is that the Saints Row disc?' }
}
Write-Host "   $Data"

# --- 3. ReXGlue SDK -----------------------------------------------------------
Step 'ReXGlue SDK (the first build takes 10-20 minutes)'
$sdkStamp = Join-Path $SdkInstall 'commit.txt'
if (-not ((Test-Path $sdkStamp) -and ((Get-Content $sdkStamp) -eq $SdkCommit))) {
    Clone $SdkRepo $Sdk $SdkCommit
    Run 'Fetching SDK submodules' { git -C $Sdk -c core.longpaths=true submodule update --init --recursive }
    Push-Location $Sdk
    try {
        # Forward slashes: CMake reads the backslashes in C:\Program Files\... as escapes.
        $rc = ($Llvm -replace '\\', '/') + '/llvm-rc.exe'
        Run 'Configuring the SDK' { cmake --preset win-amd64 "-DCMAKE_RC_COMPILER=$rc" }
        Run 'Building the SDK' { cmake --build out/build/win-amd64 --config Release }
        Run 'Installing the SDK' { cmake --install out/build/win-amd64 --config Release --prefix out/install/win-amd64 }
    } finally { Pop-Location }
    Set-Content $sdkStamp $SdkCommit
}

# --- 4. Codegen ---------------------------------------------------------------
Step 'Recompiling default.xex to C++ (codegen)'
Push-Location (Join-Path $Root 'project')
try {
    Run 'Codegen' { & "$SdkInstall\bin\rexglue.exe" codegen saintsrow_manifest.toml }

    # --- 5. Build -------------------------------------------------------------
    Step 'Building the game (10-20 minutes the first time)'
    if (-not (Test-Path 'build\build.ninja')) { Run 'Configuring the game' { cmake --preset win-amd64 } }
    Run 'Building the game' { cmake --build build }
} finally { Pop-Location }

# --- 6. Launcher --------------------------------------------------------------
Step 'Writing Play.cmd'
"@echo off`r`nstart `"`" `"%~dp0project\build\saintsrow.exe`" --game_data_root=`"%~dp0extracted`" %*`r`n" |
    Set-Content -Path (Join-Path $Root 'Play.cmd') -Encoding ascii -NoNewline
Write-Host ''
Write-Host 'Done. Double-click Play.cmd to start the game.' -ForegroundColor Green
