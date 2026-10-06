param(
    [string]$GameDirectory='D:\SteamLibrary\steamapps\common\Europa Universalis IV'
)

$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$gameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path
$exePath=Join-Path $gameRoot 'eu4.exe'
$loaderPath=Join-Path $gameRoot 'VERSION.dll'
if (!(Test-Path -LiteralPath $loaderPath -PathType Leaf)) {
    throw 'A VERSION.dll plugin loader is required'
}
$checker=Join-Path $projectRoot 'build/executable_check.exe'
& $checker $exePath
if ($LASTEXITCODE -ne 0) { throw 'Unsupported eu4.exe: executable compatibility check failed' }
$dllSource=Join-Path $projectRoot 'build\eu4_menu_patch.dll'
$dllTarget=Join-Path $gameRoot 'plugins\eu4_menu_patch.dll'
$sourceHash=(Get-FileHash -LiteralPath $dllSource -Algorithm SHA256).Hash
if (Test-Path -LiteralPath $dllTarget) {
    $targetHash=(Get-FileHash -LiteralPath $dllTarget -Algorithm SHA256).Hash
    if ($targetHash -eq $sourceHash) {
        Write-Output "Already installed: $dllTarget"
        return
    }
    $backupRoot=Join-Path $projectRoot 'private\patch-backups'
    New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
    Copy-Item -LiteralPath $dllTarget -Destination (Join-Path $backupRoot "eu4_menu_patch-$targetHash.dll")
}
Copy-Item -LiteralPath $dllSource -Destination $dllTarget
if ((Get-FileHash -LiteralPath $dllTarget -Algorithm SHA256).Hash -ne $sourceHash) {
    throw 'Installed DLL verification failed'
}
Write-Output "Installed for the next game launch: $dllTarget"
Write-Output "DLL SHA-256: $sourceHash"
