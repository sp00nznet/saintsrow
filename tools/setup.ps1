# Saints Row recomp: one-shot setup. Run through Setup.cmd.
#
# Does exactly what the README's "Step by step" section does, in order, and
# skips any step whose output already exists, so a rerun after a failure
# resumes. Details go to setup.log; the console gets one line per step.
# It never downloads game data: you point it at your own disc image or folder.

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Log = Join-Path $Root 'setup.log'
$Sdk = Join-Path $Root 'tools\rexglue-sdk'
$SdkInstall = Join-Path $Sdk 'out\install\win-amd64'
# sp00nznet/rexglue-sdk, branch saintsrow: upstream development@bd833a2 + this project's fixes
$SdkCommit = 'abc7ced'
$Game = Join-Path $Root 'extracted'
$Llvm = 'C:\Program Files\LLVM\bin'

"=== setup $(Get-Date -Format s)" | Out-File $Log -Append -Encoding utf8

function Step($name) { Write-Host "-> $name" }
function Fail($msg) {
    Write-Host ""
    Write-Host "Setup stopped: $msg" -ForegroundColor Red
    Write-Host "Details are in $Log"
    exit 1
}
# Run a native command, logging its output; stop on a non-zero exit.
function Run($what, [scriptblock]$cmd) {
    & $cmd *>> $Log
    if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit $LASTEXITCODE)." }
}
function Ask($q) { (Read-Host "$q [y/N]") -match '^[yY]' }

# --- 1. Prerequisites ---------------------------------------------------------
Step 'Checking prerequisites'
if (Test-Path $Llvm) { $env:PATH = "$Llvm;$env:PATH" }
$need = @(
    @{ exe = 'git';    id = 'Git.Git';              why = 'fetches the ReXGlue SDK';   size = '~60 MB' },
    @{ exe = 'cmake';  id = 'Kitware.CMake';        why = 'configures the build';      size = '~35 MB' },
    @{ exe = 'ninja';  id = 'Ninja-build.Ninja';    why = 'runs the build';            size = '~1 MB' },
    @{ exe = 'clang';  id = 'LLVM.LLVM';            why = 'compiles everything';       size = '~400 MB' },
    @{ exe = 'python'; id = 'Python.Python.3.12';   why = 'extracts the disc image';   size = '~30 MB' }
)
foreach ($n in $need) {
    if (Get-Command $n.exe -ErrorAction SilentlyContinue) { continue }
    Write-Host "   Missing: $($n.exe) -- $($n.why), $($n.size) via winget ($($n.id))."
    if (-not (Ask "   Install it now?")) { Fail "$($n.exe) is required. Install it, open a new window, and run Setup.cmd again." }
    Run "Installing $($n.exe)" { winget install --id $n.id -e --accept-package-agreements --accept-source-agreements }
    Write-Host "   Installed. Close this window and run Setup.cmd again so the new PATH is picked up."
    exit 0
}
# clang needs the Windows SDK and MSVC libraries from Visual Studio or its Build Tools.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere) -or -not (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)) {
    Write-Host '   Missing: Visual Studio 2022 Build Tools with "Desktop development with C++" (~3 GB).'
    if (-not (Ask '   Install it now?')) { Fail 'The MSVC libraries and Windows SDK are required.' }
    Run 'Installing VS Build Tools' { winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' }
}

# --- 2. Game data -------------------------------------------------------------
Step 'Game data'
if (-not (Test-Path (Join-Path $Game 'default.xex'))) {
    $src = Read-Host '   Path to your Saints Row disc image (.iso) or an extracted folder containing default.xex'
    $src = $src.Trim('"')
    if (Test-Path (Join-Path $src 'default.xex')) {
        Copy-Item -Recurse -Force "$src\*" $Game
    } elseif ($src -match '\.iso$' -and (Test-Path $src)) {
        $tools360 = Join-Path $Root 'tools\360tools'
        if (-not (Test-Path $tools360)) {
            Run 'Cloning 360tools' { git clone --depth 1 https://github.com/sp00nznet/360tools $tools360 }
        }
        Run 'Extracting the disc image' { python -I "$tools360\tools\extract_iso.py" $src $Game }
    } else {
        Fail "'$src' is neither an .iso nor a folder with default.xex in it."
    }
    if (-not (Test-Path (Join-Path $Game 'default.xex'))) { Fail 'Extraction finished but extracted\default.xex is missing. Is this the Saints Row disc?' }
}
Write-Host "   $Game"

# --- 3. ReXGlue SDK -----------------------------------------------------------
Step 'ReXGlue SDK (first build takes 10-20 minutes)'
if (-not (Test-Path (Join-Path $SdkInstall 'bin\rexglue.exe'))) {
    if (-not (Test-Path $Sdk)) {
        Run 'Cloning the ReXGlue SDK fork' { git clone -b saintsrow https://github.com/sp00nznet/rexglue-sdk $Sdk }
        Run 'Checking out the pinned SDK' { git -C $Sdk checkout $SdkCommit }
    }
    Run 'Fetching SDK submodules' { git -C $Sdk submodule update --init --recursive }
    Push-Location $Sdk
    try {
        Run 'Configuring the SDK' { cmake --preset win-amd64 "-DCMAKE_RC_COMPILER=$Llvm\llvm-rc.exe" }
        Run 'Building the SDK' { cmake --build out/build/win-amd64 --config Release }
        Run 'Installing the SDK' { cmake --install out/build/win-amd64 --config Release --prefix out/install/win-amd64 }
    } finally { Pop-Location }
}

# --- 4. The game --------------------------------------------------------------
Step 'Saints Row (codegen + build, ~5 minutes the first time)'
Push-Location (Join-Path $Root 'project')
try {
    if (-not (Test-Path 'build\build.ninja')) {
        Run 'Configuring the game' { cmake --preset win-amd64 }
    }
    Run 'Building the game' { cmake --build build }
} finally { Pop-Location }

# --- 5. Launcher --------------------------------------------------------------
$play = Join-Path $Root 'Play.cmd'
"@echo off`r`n`"%~dp0project\build\saintsrow.exe`" --game_data_root=`"%~dp0extracted`" %*`r`n" |
    Set-Content -Path $play -Encoding ascii -NoNewline
Write-Host ''
Write-Host 'Done. Double-click Play.cmd to start the game.' -ForegroundColor Green
