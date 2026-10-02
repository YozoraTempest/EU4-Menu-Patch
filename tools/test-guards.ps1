$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$vsRoot='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$envLines=& "$env:ComSpec" /d /s /c "`"$vsRoot\VC\Auxiliary\Build\vcvars64.bat`" >nul && set"
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
    }
}
$testDir=Join-Path $projectRoot 'build\guard-tests'
New-Item -ItemType Directory -Path $testDir -Force | Out-Null
Push-Location $testDir
try {
    & cl.exe /nologo /O2 /W4 /MT /EHsc (Join-Path $projectRoot 'tests\guard_host.cpp') /Fe:guard_host.exe
    if ($LASTEXITCODE -ne 0) { throw 'Guard host build failed' }
    $releaseDll=Join-Path $projectRoot 'build\eu4_menu_patch.dll'
    $researchDll=Join-Path $projectRoot 'build\menu_patch_probe.dll'
    & '.\guard_host.exe' $releaseDll -1
    if ($LASTEXITCODE -ne 0) { throw 'Release wrong-process guard failed' }
    & '.\guard_host.exe' $researchDll -1
    if ($LASTEXITCODE -ne 0) { throw 'Research isolation guard failed' }
    Copy-Item -LiteralPath '.\guard_host.exe' -Destination '.\eu4.exe'
    & '.\eu4.exe' $releaseDll -2
    if ($LASTEXITCODE -ne 0) { throw 'Release executable hash guard failed' }
} finally { Pop-Location }
