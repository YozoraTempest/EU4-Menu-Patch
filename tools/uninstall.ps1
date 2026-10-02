param(
    [string]$GameDirectory='D:\SteamLibrary\steamapps\common\Europa Universalis IV'
)

$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$gameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path
$dllTarget=Join-Path $gameRoot 'plugins\eu4_menu_patch.dll'
if (-not (Test-Path -LiteralPath $dllTarget)) {
    Write-Output 'The menu patch is not installed'
    return
}
$backupRoot=Join-Path $projectRoot 'private\patch-backups'
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
$targetHash=(Get-FileHash -LiteralPath $dllTarget -Algorithm SHA256).Hash
Move-Item -LiteralPath $dllTarget -Destination (Join-Path $backupRoot "eu4_menu_patch-uninstalled-$targetHash.dll") -Force
Write-Output "Menu patch removed: $dllTarget"
