param(
    [ValidateSet('Release', 'Nightly')][string]$Channel = 'Release',
    [string]$BuildDate = ''
)
. (Join-Path $PSScriptRoot 'build-common.ps1')
$info = Get-ReleaseInfo $Channel $BuildDate
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
foreach ($record in @('build-info.json', 'automated-validation.json', 'package-info.json')) {
    Remove-Item -LiteralPath (Join-Path $buildRoot $record) -Force -ErrorAction SilentlyContinue
}
Start-Transcript -Path (Join-Path $buildRoot 'build.log') -Force | Out-Null
try {
    Initialize-Msvc
    $header = @"
#pragma once
#define EU4_MENU_PATCH_VERSION "$($info.PatchVersion)"
#define EU4_MENU_PATCH_FILE_VERSION $($info.Version.Replace('.',',')),0
#define EU4_MENU_PATCH_FILE_VERSION_STRING "$($info.Version).0"
"@
    [IO.File]::WriteAllText((Join-Path $buildRoot 'patch-version.h'), $header + [Environment]::NewLine, [Text.Encoding]::ASCII)
    Push-Location $buildRoot
    try {
        & rc.exe /nologo "/I$buildRoot" /fo eu4_menu_patch.res (Join-Path $projectRoot 'src/eu4_menu_patch.rc')
        if ($LASTEXITCODE -ne 0) { throw "Resource build failed: $LASTEXITCODE" }
        $common = @('/nologo', '/LD', '/O2', '/Zi', '/W4', '/std:c++17', '/MT', '/EHsc', "/I$buildRoot")
        $source = Join-Path $projectRoot 'src/eu4_menu_patch.cpp'
        & cl.exe @common /Fd:eu4_menu_patch.compiler.pdb /Fo:eu4_menu_patch.obj $source eu4_menu_patch.res /link /DEBUG /INCREMENTAL:NO /OUT:eu4_menu_patch.dll /IMPLIB:eu4_menu_patch.lib /PDB:eu4_menu_patch.pdb /MAP:eu4_menu_patch.map
        if ($LASTEXITCODE -ne 0) { throw "C++ build failed: $LASTEXITCODE" }
        & cl.exe @common /DEU4_MENU_PATCH_RESEARCH /Fd:menu_patch_probe.compiler.pdb /Fo:menu_patch_probe.obj $source eu4_menu_patch.res /link /DEBUG /INCREMENTAL:NO /OUT:menu_patch_probe.dll /IMPLIB:menu_patch_probe.lib /PDB:menu_patch_probe.pdb /MAP:menu_patch_probe.map
        if ($LASTEXITCODE -ne 0) { throw "Research C++ build failed: $LASTEXITCODE" }
    } finally { Pop-Location }
    $sourceHashes = [ordered]@{}
    foreach ($inputFile in @('src/eu4_menu_patch.cpp', 'src/eu4_menu_patch.rc', 'VERSION')) {
        $sourceHashes[$inputFile] = Get-Sha256 (Join-Path $projectRoot $inputFile)
    }
    Write-Json (Join-Path $buildRoot 'build-info.json') ([ordered]@{
        source_commit = $info.SourceCommit; source_tree_dirty = (Test-SourceTreeDirty)
        version = $info.Version; tag = $info.Tag; channel = $info.Channel; build_date = $info.BuildDate
        configuration = 'MSVC x64 /O2 /MT with debug symbols'
        patch_dll_sha256 = (Get-Sha256 (Join-Path $buildRoot 'eu4_menu_patch.dll'))
        probe_dll_sha256 = (Get-Sha256 (Join-Path $buildRoot 'menu_patch_probe.dll'))
        source_hashes = $sourceHashes
    })
} finally { Stop-Transcript | Out-Null }
