#Requires -Version 5.1
<#
.SYNOPSIS
    One-command local build for ICS_Generate (CRT-free Win32 iCalendar editor).

.DESCRIPTION
    Detects a usable CMake generator (Visual Studio, MinGW Makefiles, Ninja or
    NMake), configures the project, builds it, runs the unit tests, then prints
    the executable size and verifies that the image does not import the C
    runtime (msvcrt / ucrtbase / api-ms-win-crt-*).

.PARAMETER BuildDir
    CMake binary directory (git-ignored). Default: build-local.

.PARAMETER Configuration
    Build configuration, used by multi-config generators. Default: Release.

.PARAMETER Generator
    Force a CMake generator, for example "MinGW Makefiles".

.PARAMETER Architecture
    Force the target architecture for Visual Studio generators: x64 or Win32.

.PARAMETER SizeGateKb
    Hard size limit for the local check. Default: 96.

.PARAMETER SkipTests
    Do not run ics_core_tests.exe.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts/build.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -BuildDir build-mingw -Generator "MinGW Makefiles"
#>
[CmdletBinding()]
param(
    [string] $BuildDir      = 'build-local',
    [ValidateSet('Release', 'MinSizeRel', 'RelWithDebInfo', 'Debug')]
    [string] $Configuration = 'Release',
    [string] $Generator,
    [string] $Architecture,
    [int]    $SizeGateKb    = 96,
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'

$repoRoot  = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDir
$binDir    = Join-Path $buildPath 'bin'
$appExe    = Join-Path $binDir 'ICS_Generate.exe'
$testExe   = Join-Path $binDir 'ics_core_tests.exe'

$crtPattern = 'msvcrt|ucrtbase|ucrtbased|api-ms-win-crt|libgcc|libstdc\+\+|libwinpthread'
$whitelist  = @('KERNEL32.DLL', 'USER32.DLL', 'GDI32.DLL', 'COMCTL32.DLL', 'SHELL32.DLL',
                'SHLWAPI.DLL', 'OLE32.DLL', 'COMDLG32.DLL', 'ADVAPI32.DLL', 'DWMAPI.DLL',
                'UXTHEME.DLL')

function Write-Head([string] $Text) { Write-Host ''; Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Detail([string] $Text) { Write-Host "    $Text" }
function Write-Ok([string] $Text) { Write-Host "    $Text" -ForegroundColor Green }
function Write-WarnLine([string] $Text) { Write-Host "    $Text" -ForegroundColor Yellow }
function Fail([string] $Text) { Write-Host ''; Write-Host "ERROR: $Text" -ForegroundColor Red; exit 1 }

function Get-ToolPath([string] $Name) {
    $cmd = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue |
           Select-Object -First 1
    if ($cmd) { return $cmd.Source }
    return $null
}

function Get-VsWhere {
    $pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (-not $pf86) { $pf86 = $env:ProgramFiles }
    if (-not $pf86) { return $null }
    $candidate = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $candidate) { return $candidate }
    return $null
}

function Get-CmakeGenerators {
    $list = @()
    foreach ($line in (& cmake --help)) {
        if ($line -match '^\s*\*?\s*(.+?)\s+=\s+Generates') { $list += $Matches[1].Trim() }
    }
    return $list
}

function Resolve-CmakeGenerator {
    if (-not (Get-ToolPath 'cmake')) {
        Fail 'cmake was not found on PATH. Install CMake 3.20 or newer (https://cmake.org/download/).'
    }
    $available = Get-CmakeGenerators
    Write-Detail ("generators known to cmake: " + ($available -join '; '))

    if ($Generator) {
        if ($available -notcontains $Generator) {
            Fail ("unknown CMake generator '" + $Generator + "'. Known: " + ($available -join '; '))
        }
        return $Generator
    }

    # 1) Visual Studio (exactly what the CI matrix uses) when a C++ toolset exists.
    $vsWhere = Get-VsWhere
    if ($vsWhere) {
        $version = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
        if ($version) {
            $major = [int]($version.Trim().Split('.')[0])
            $map = @{ 18 = 'Visual Studio 18 2026'; 17 = 'Visual Studio 17 2022';
                      16 = 'Visual Studio 16 2019'; 15 = 'Visual Studio 15 2017' }
            if ($map.ContainsKey($major) -and ($available -contains $map[$major])) {
                return $map[$major]
            }
        }
    }

    # 2) MinGW-w64 - the toolchain used for the local size measurements.
    if (($available -contains 'MinGW Makefiles') -and (Get-ToolPath 'mingw32-make') -and
        ((Get-ToolPath 'g++') -or (Get-ToolPath 'gcc'))) {
        return 'MinGW Makefiles'
    }

    # 3) Ninja with whatever the default compiler is.
    if (($available -contains 'Ninja') -and (Get-ToolPath 'ninja')) { return 'Ninja' }

    # 4) NMake from a Visual Studio developer prompt.
    if (($available -contains 'NMake Makefiles') -and (Get-ToolPath 'nmake')) { return 'NMake Makefiles' }

    Fail 'no usable CMake generator found. Install Visual Studio with the C++ workload, or MinGW-w64 with mingw32-make, or Ninja.'
    return $null
}

function Resolve-Dumpbin {
    $direct = Get-ToolPath 'dumpbin'
    if ($direct) { return $direct }
    $vsWhere = Get-VsWhere
    if (-not $vsWhere) { return $null }
    $vsRoot = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsRoot) { return $null }
    $toolsRoot = Join-Path $vsRoot.Trim() 'VC\Tools\MSVC'
    if (-not (Test-Path $toolsRoot)) { return $null }
    $candidates = Get-ChildItem -Path $toolsRoot -Directory |
                  Sort-Object Name -Descending |
                  ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } |
                  Where-Object { Test-Path $_ }
    if ($candidates) { return ($candidates | Select-Object -First 1) }
    return $null
}

# ------------------------------------------------------------------ configure
if (-not (Test-Path (Join-Path $repoRoot 'CMakeLists.txt'))) {
    Fail ("CMakeLists.txt was not found in " + $repoRoot)
}

$generator = Resolve-CmakeGenerator
$isMultiConfig = $generator -like 'Visual Studio*'

Write-Head 'Configuration'
Write-Detail ("repository : " + $repoRoot)
Write-Detail ("build dir  : " + $buildPath)
Write-Detail ("generator  : " + $generator)
Write-Detail ("config     : " + $Configuration)

$configureArgs = @('-S', $repoRoot, '-B', $buildPath, '-G', $generator,
                   ('-DCMAKE_BUILD_TYPE=' + $Configuration), '-DICSG_BUILD_TESTS=ON')
if ($Architecture) {
    if ($isMultiConfig) {
        $configureArgs += @('-A', $Architecture)
    } else {
        Write-WarnLine ("-Architecture " + $Architecture + " is ignored by the " + $generator + " generator")
    }
}

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { Fail ("cmake configure failed (exit " + $LASTEXITCODE + ")") }
Write-Ok 'configured'

# ---------------------------------------------------------------------- build
Write-Head ('Build (' + $Configuration + ')')
& cmake --build $buildPath --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { Fail ("cmake build failed (exit " + $LASTEXITCODE + ")") }
if (-not (Test-Path $appExe)) { Fail ("the build did not produce " + $appExe) }
Write-Ok 'built'

# ---------------------------------------------------------------------- tests
$testsOk = $false
if ($SkipTests) {
    Write-Head 'Unit tests (skipped)'
} elseif (-not (Test-Path $testExe)) {
    Write-WarnLine ("test runner not found: " + $testExe)
} else {
    Write-Head 'Unit tests'
    Push-Location $repoRoot
    try {
        & $testExe
        $testCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($testCode -ne 0) { Fail ("ics_core_tests.exe failed (exit " + $testCode + ")") }
    $testsOk = $true
    Write-Ok 'ics_core_tests.exe: PASS'
}

# ----------------------------------------------------------------- size gate
Write-Head 'Size report'
$item  = Get-Item $appExe
$bytes = $item.Length
$kb    = [math]::Round($bytes / 1KB, 2)
$gate  = $SizeGateKb * 1KB
Write-Detail ("file      : " + $item.FullName)
Write-Detail ("written   : " + $item.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
Write-Host ("    size      : {0} bytes ({1} KB)" -f $bytes, $kb) -ForegroundColor Green
Write-Detail ("product   : target <= 64 KB")
if ($bytes -gt $gate) {
    Fail ("size gate exceeded: " + $bytes + " bytes > " + $SizeGateKb + " KB (" + $gate + " bytes)")
}
Write-Ok ("size gate : PASS (limit " + $SizeGateKb + " KB)")

# ------------------------------------------------------- CRT-free import check
Write-Head 'CRT-free import check'
$dumpbin = Resolve-Dumpbin
$dlls = @()
if ($dumpbin) {
    Write-Detail ("tool      : " + $dumpbin)
    $text = (& $dumpbin /nologo /imports $appExe | Out-String)
    $dlls = [regex]::Matches($text, '(?m)^\s+(\S+\.dll)\s*$') |
            ForEach-Object { $_.Groups[1].Value }
} else {
    $objdump = Get-ToolPath 'objdump'
    if (-not $objdump) { Fail 'neither dumpbin.exe nor objdump was found - cannot verify the import table' }
    Write-Detail ("tool      : " + $objdump)
    $text = (& $objdump -p $appExe | Out-String)
    $dlls = [regex]::Matches($text, '(?m)^\s*DLL Name:\s*(\S+)') |
            ForEach-Object { $_.Groups[1].Value }
}
$dlls = @($dlls | Sort-Object -Unique)
if ($dlls.Count -eq 0) { Fail 'the import dump produced no DLL names - the check cannot be trusted' }

foreach ($dll in $dlls) { Write-Detail ("import    : " + $dll) }

$crtHits = @($dlls | Where-Object { $_ -match $crtPattern })
if ($crtHits.Count -gt 0) {
    Fail ("CRT import detected: " + ($crtHits -join ', ') + ' - the image must stay CRT-free')
}
$extra = @($dlls | Where-Object { $whitelist -notcontains $_.ToUpperInvariant() })
if ($extra.Count -gt 0) {
    Fail ("import outside the frozen whitelist: " + ($extra -join ', '))
}
Write-Ok 'CRT-free  : PASS (no msvcrt / ucrtbase / api-ms-win-crt import)'
Write-Ok 'whitelist : PASS'

# -------------------------------------------------------------------- summary
Write-Head 'Summary'
Write-Host ("    exe       : " + $appExe)
Write-Host ("    size      : {0} bytes ({1} KB), gate {2} KB" -f $bytes, $kb, $SizeGateKb)
Write-Host ("    imports   : " + ($dlls -join ', '))
Write-Host ("    CRT-free  : PASS")
if ($SkipTests) {
    Write-Host "    tests     : skipped"
} elseif ($testsOk) {
    Write-Host "    tests     : PASS"
} else {
    Write-Host "    tests     : NOT RUN (binary missing)" -ForegroundColor Yellow
}
Write-Host ''
exit 0
