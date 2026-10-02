param(
    [string]$GameDirectory='D:\SteamLibrary\steamapps\common\Europa Universalis IV'
)

$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$gameRoot=(Resolve-Path -LiteralPath $GameDirectory).Path
$exePath=Join-Path $gameRoot 'eu4.exe'
$loaderPath=Join-Path $gameRoot 'VERSION.dll'
$exeHash='9AD3EFE1AF169F40EE577F9DAE5DEBBC87AF6FB8B5450FB345EBF110DC4D771A'
$loaderHash='1E91BB82A8EF5CF86DD20C8DF45B75643B6DA021AFF8FABDBC78B0F4F5F9916A'
if ((Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash -ne $exeHash) {
    throw 'Unsupported eu4.exe: SHA-256 mismatch'
}
if ((Get-FileHash -LiteralPath $loaderPath -Algorithm SHA256).Hash -ne $loaderHash) {
    throw 'The verified local VERSION.dll plugin loader is required'
}
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
