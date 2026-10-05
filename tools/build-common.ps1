$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$projectRoot = Split-Path $PSScriptRoot -Parent
$buildRoot = Join-Path $projectRoot 'build'
$distRoot = Join-Path $projectRoot 'dist'

function Get-SourceCommit {
    $commit = & git -C $projectRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') { throw 'Cannot read the source commit.' }
    return $commit
}

function Test-SourceTreeDirty {
    $changes = & git -C $projectRoot status --porcelain
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read checkout status.' }
    return [bool]$changes
}

function Assert-CleanCheckout {
    if (Test-SourceTreeDirty) { throw 'Commit source changes before packaging or publishing.' }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-Json([string]$Path, $Value) {
    $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding utf8NoBOM
}

function Get-ReleaseInfo([string]$Channel = 'Release', [string]$BuildDate = '') {
    $version = [IO.File]::ReadAllText((Join-Path $projectRoot 'VERSION')).Trim()
    if ($version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'VERSION must contain a numeric major.minor.patch version.'
    }
    foreach ($part in $version.Split('.')) {
        if ([long]$part -gt 65535) { throw 'VERSION exceeds the Windows resource version range.' }
    }
    $commit = Get-SourceCommit
    if ($Channel -eq 'Nightly') {
        if (!$BuildDate) {
            $BuildDate = [TimeZoneInfo]::ConvertTimeBySystemTimeZoneId(
                [DateTimeOffset]::UtcNow, 'China Standard Time').ToString('yyyyMMdd')
        }
        [void][DateTime]::ParseExact($BuildDate, 'yyyyMMdd', [Globalization.CultureInfo]::InvariantCulture)
        $patchVersion = "$version-nightly-$BuildDate-$($commit.Substring(0,7))"
    } elseif ($Channel -eq 'Release') {
        if ($BuildDate) { throw 'BuildDate is only used for nightly builds.' }
        $patchVersion = "$version-experimental"
    } else { throw 'Unknown release channel.' }
    return [pscustomobject]@{
        Version = $version; PatchVersion = $patchVersion; Channel = $Channel
        Tag = "v$patchVersion"; SourceCommit = $commit; BuildDate = $BuildDate
        PlayerPackage = "EU4MenuPatch-1.37.5-v$patchVersion.zip"
    }
}

function Initialize-Msvc {
    $vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere was not found.' }
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$vsRoot) { throw 'Install MSVC x64 build tools and a Windows SDK.' }
    $vcvarsCommand = '"{0}\VC\Auxiliary\Build\vcvars64.bat" >nul && set' -f $vsRoot
    $envLines = & $env:ComSpec /d /s /c $vcvarsCommand
    if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed.' }
    foreach ($line in $envLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
    $env:VSLANG = '1033'
}

function Assert-BuildMatches($Info) {
    $record = Get-Content -LiteralPath (Join-Path $buildRoot 'build-info.json') -Raw | ConvertFrom-Json
    if ($record.source_commit -ne $Info.SourceCommit -or $record.version -ne $Info.Version -or
        $record.tag -ne $Info.Tag -or $record.channel -ne $Info.Channel -or
        $record.build_date -ne $Info.BuildDate) { throw 'Build record does not match this source or release.' }
    if ($record.patch_dll_sha256 -ne (Get-Sha256 (Join-Path $buildRoot 'eu4_menu_patch.dll')) -or
        $record.probe_dll_sha256 -ne (Get-Sha256 (Join-Path $buildRoot 'menu_patch_probe.dll'))) {
        throw 'Build DLL hash mismatch.'
    }
    foreach ($inputFile in @('src/eu4_menu_patch.cpp', 'src/eu4_menu_patch.rc', 'VERSION')) {
        if ($record.source_hashes.$inputFile -ne (Get-Sha256 (Join-Path $projectRoot $inputFile))) {
            throw "Build input changed: $inputFile"
        }
    }
    return $record
}

function Assert-ValidatedBuild($Info) {
    Assert-CleanCheckout
    $build = Assert-BuildMatches $Info
    $validation = Get-Content -LiteralPath (Join-Path $buildRoot 'automated-validation.json') -Raw | ConvertFrom-Json
    $expectedTests = @('release_wrong_process', 'research_isolation', 'release_wrong_executable_hash')
    if ($build.source_tree_dirty -ne $false -or $validation.source_tree_dirty -ne $false -or
        $validation.passed -ne $true -or $validation.source_commit -ne $Info.SourceCommit -or
        $validation.tag -ne $Info.Tag -or $validation.patch_dll_sha256 -ne $build.patch_dll_sha256 -or
        $validation.probe_dll_sha256 -ne $build.probe_dll_sha256 -or
        (@($validation.tests | Sort-Object) -join ',') -cne (($expectedTests | Sort-Object) -join ',')) {
        throw 'Validation record does not match the tested build.'
    }
    return $build
}

function Get-PlayerReadme($Info) {
    $template = [IO.File]::ReadAllText((Join-Path $projectRoot 'docs/direct-install.txt'))
    if (!$template.Contains('@PATCH_VERSION@')) { throw 'Player README is missing its version placeholder.' }
    $text = $template.Replace('@PATCH_VERSION@', $Info.PatchVersion)
    if ($text -match '@[A-Z_]+@') { throw 'Player README has an unresolved placeholder.' }
    return $text
}
