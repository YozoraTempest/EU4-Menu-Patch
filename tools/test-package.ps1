param(
    [ValidateSet('Release', 'Nightly')][string]$Channel = 'Release',
    [string]$BuildDate = ''
)
. (Join-Path $PSScriptRoot 'build-common.ps1')
$info = Get-ReleaseInfo $Channel $BuildDate
$build = Assert-ValidatedBuild $info
$package = Get-Content -LiteralPath (Join-Path $buildRoot 'package-info.json') -Raw | ConvertFrom-Json
if ($package.source_commit -ne $info.SourceCommit -or $package.tag -ne $info.Tag -or
    $package.version -ne $info.Version -or $package.channel -ne $info.Channel -or
    $package.build_date -ne $info.BuildDate -or $package.player_package -cne $info.PlayerPackage -or
    $package.patch_dll_sha256 -ne $build.patch_dll_sha256 -or
    $package.automated_checks_passed -ne $true -or $package.game_runtime_verified -ne $false) {
    throw 'Package record does not match this release.'
}
$expected = @{
    'plugins/eu4_menu_patch.dll' = [IO.File]::ReadAllBytes((Join-Path $buildRoot 'eu4_menu_patch.dll'))
    'plugins/eu4_menu_patch.LICENSE.txt' = [IO.File]::ReadAllBytes((Join-Path $projectRoot 'LICENSE'))
    'plugins/eu4_menu_patch.README.txt' = [Text.UTF8Encoding]::new($false).GetBytes((Get-PlayerReadme $info))
}
$zipPath = Join-Path $distRoot $info.PlayerPackage
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    if (@($archive.Entries | Where-Object { $_.FullName.EndsWith('/') -and $_.FullName -cne 'plugins/' }).Count) {
        throw 'Unexpected ZIP directory.'
    }
    $entries = @($archive.Entries | Where-Object { !$_.FullName.EndsWith('/') })
    if (($entries.FullName | Sort-Object -CaseSensitive) -join ',' -cne
        (($expected.Keys | Sort-Object -CaseSensitive) -join ',')) { throw 'Unexpected ZIP file list.' }
    foreach ($entry in $entries) {
        $stream = $entry.Open()
        $memory = [IO.MemoryStream]::new()
        try {
            $stream.CopyTo($memory)
            $actualHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($memory.ToArray()))
            $expectedHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($expected[$entry.FullName]))
            if ($actualHash -cne $expectedHash) { throw "ZIP entry content mismatch: $($entry.FullName)" }
        } finally { $stream.Dispose(); $memory.Dispose() }
    }
} finally { $archive.Dispose() }
$hash = Get-Sha256 $zipPath
if ($hash -ne $package.package_sha256) { throw 'ZIP hash does not match package record.' }
$checksum = [IO.File]::ReadAllText((Join-Path $distRoot 'SHA256SUMS.txt')).TrimEnd()
if ($checksum -cne "$hash  $($info.PlayerPackage)") { throw 'SHA256SUMS does not match the package.' }
Write-Output "Package checks passed: $($info.PlayerPackage)"
