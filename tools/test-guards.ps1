param(
    [ValidateSet('Release', 'Nightly')][string]$Channel = 'Release',
    [string]$BuildDate = ''
)
. (Join-Path $PSScriptRoot 'build-common.ps1')
$info = Get-ReleaseInfo $Channel $BuildDate
$build = Assert-BuildMatches $info
Remove-Item -LiteralPath (Join-Path $buildRoot 'automated-validation.json') -Force -ErrorAction SilentlyContinue
$testDir = Join-Path $buildRoot 'guard-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
Start-Transcript -Path (Join-Path $buildRoot 'guard-tests.log') -Force | Out-Null
try {
    & (Join-Path $buildRoot 'executable_compatibility_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Executable compatibility tests failed.' }
    & (Join-Path $buildRoot 'menu_transition_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Menu transition lifecycle tests failed.' }
    Initialize-Msvc
    Push-Location $testDir
    try {
        & cl.exe /nologo /O2 /W4 /MT /EHsc (Join-Path $projectRoot 'tests/guard_host.cpp') /Fe:guard_host.exe
        if ($LASTEXITCODE -ne 0) { throw 'Guard host build failed.' }
        $releaseDll = Join-Path $buildRoot 'eu4_menu_patch.dll'
        $researchDll = Join-Path $buildRoot 'menu_patch_probe.dll'
        & './guard_host.exe' $releaseDll -1
        if ($LASTEXITCODE -ne 0) { throw 'Release wrong-process guard failed.' }
        & './guard_host.exe' $researchDll -1
        if ($LASTEXITCODE -ne 0) { throw 'Research isolation guard failed.' }
        Copy-Item -LiteralPath './guard_host.exe' -Destination './eu4.exe' -Force
        & './eu4.exe' $releaseDll -2
        if ($LASTEXITCODE -ne 0) { throw 'Release incompatible-executable guard failed.' }
    } finally { Pop-Location }
    $build = Assert-BuildMatches $info
    Write-Json (Join-Path $buildRoot 'automated-validation.json') ([ordered]@{
        source_commit = $info.SourceCommit; source_tree_dirty = (Test-SourceTreeDirty); tag = $info.Tag
        patch_dll_sha256 = $build.patch_dll_sha256; probe_dll_sha256 = $build.probe_dll_sha256
        passed = $true
        tests = @('executable_compatibility', 'menu_transition', 'release_wrong_process', 'research_isolation', 'release_incompatible_executable')
        game_runtime_verified = $false
    })
} finally { Stop-Transcript | Out-Null }
