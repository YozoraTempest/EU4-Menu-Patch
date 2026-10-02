$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$packageName='EU4MenuPatch-1.37.5-experimental'
$stageRoot=Join-Path $projectRoot ('build\package-'+[guid]::NewGuid().ToString('N'))
$files=@(
    'README.md','LICENSE','docs\build.md','docs\direct-install.txt','src\eu4_menu_patch.cpp','src\eu4_menu_patch.rc',
    'build\eu4_menu_patch.dll','tools\build.ps1','tools\test-guards.ps1',
    'tools\install.ps1','tools\uninstall.ps1','tools\package.ps1','tests\guard_host.cpp'
)
foreach ($relative in $files) {
    $target=Join-Path $stageRoot $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot $relative) -Destination $target
}
$dllHash=(Get-FileHash -LiteralPath (Join-Path $stageRoot 'build\eu4_menu_patch.dll') -Algorithm SHA256).Hash
$manifest=[ordered]@{
    author='VulonLok'
    status='experimental'
    game_version='1.37.5.0 Inca Windows x64'
    game_exe_sha256='9AD3EFE1AF169F40EE577F9DAE5DEBBC87AF6FB8B5450FB345EBF110DC4D771A'
    patch_dll_sha256=$dllHash
    source_sha256=(Get-FileHash -LiteralPath (Join-Path $stageRoot 'src\eu4_menu_patch.cpp') -Algorithm SHA256).Hash
    resource_sha256=(Get-FileHash -LiteralPath (Join-Path $stageRoot 'src\eu4_menu_patch.rc') -Algorithm SHA256).Hash
    tested_configuration='Single-player, non-Ironman, MEIOU and four local submods; two campaigns advanced to the next month and returned to menu in one process'
    validation_details='README.md'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stageRoot 'manifest.json') -Encoding utf8
$distRoot=Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
$zipPath=Join-Path $distRoot ($packageName+'.zip')
Compress-Archive -Path (Join-Path $stageRoot '*') -DestinationPath $zipPath -Force
Write-Output $zipPath

$dropinRoot=Join-Path $projectRoot ('build\drop-in-'+[guid]::NewGuid().ToString('N'))
$dropinPlugins=Join-Path $dropinRoot 'plugins'
New-Item -ItemType Directory -Path $dropinPlugins -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'build\eu4_menu_patch.dll') -Destination (Join-Path $dropinPlugins 'eu4_menu_patch.dll')
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\direct-install.txt') -Destination (Join-Path $dropinPlugins 'eu4_menu_patch.README.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $dropinPlugins 'eu4_menu_patch.LICENSE.txt')
$dropinZip=Join-Path $distRoot ($packageName+'-drop-in.zip')
Compress-Archive -Path (Join-Path $dropinRoot '*') -DestinationPath $dropinZip -Force
Write-Output $dropinZip
