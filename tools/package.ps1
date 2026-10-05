param(
    [ValidateSet('Release', 'Nightly')][string]$Channel = 'Release',
    [string]$BuildDate = ''
)
. (Join-Path $PSScriptRoot 'build-common.ps1')
$info = Get-ReleaseInfo $Channel $BuildDate
$build = Assert-ValidatedBuild $info
$dll = Join-Path $buildRoot 'eu4_menu_patch.dll'
$resource = [Diagnostics.FileVersionInfo]::GetVersionInfo($dll)
if ($resource.FileVersion -ne "$($info.Version).0" -or $resource.ProductVersion -ne $info.PatchVersion) {
    throw 'DLL resource version does not match VERSION.'
}
$stage = Join-Path $buildRoot ('package-' + [guid]::NewGuid().ToString('N'))
$plugins = Join-Path $stage 'plugins'
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $plugins 'eu4_menu_patch.dll')
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $plugins 'eu4_menu_patch.LICENSE.txt')
[IO.File]::WriteAllText((Join-Path $plugins 'eu4_menu_patch.README.txt'), (Get-PlayerReadme $info), [Text.UTF8Encoding]::new($false))
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$zip = Join-Path $distRoot $info.PlayerPackage
Compress-Archive -Path $plugins -DestinationPath $zip -Force
$zipHash = Get-Sha256 $zip
[IO.File]::WriteAllText((Join-Path $distRoot 'SHA256SUMS.txt'), "$zipHash  $($info.PlayerPackage)" + [char]10, [Text.Encoding]::ASCII)
Write-Json (Join-Path $buildRoot 'package-info.json') ([ordered]@{
    author = 'VulonLok'; version = $info.Version; tag = $info.Tag
    channel = $info.Channel; build_date = $info.BuildDate; source_commit = $info.SourceCommit
    player_package = $info.PlayerPackage; package_sha256 = $zipHash
    patch_dll_sha256 = $build.patch_dll_sha256
    automated_checks_passed = $true; game_runtime_verified = $false
})
& (Join-Path $PSScriptRoot 'test-package.ps1') -Channel $Channel -BuildDate $info.BuildDate
Write-Output $zip
