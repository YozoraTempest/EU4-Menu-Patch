$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$envLines = & "$env:ComSpec" /d /s /c "`"$vsRoot\VC\Auxiliary\Build\vcvars64.bat`" >nul && set"
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
    }
}
$buildDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
Push-Location $buildDir
try {
    & rc.exe /nologo /fo eu4_menu_patch.res (Join-Path $projectRoot 'src\eu4_menu_patch.rc')
    if ($LASTEXITCODE -ne 0) { throw "Resource build failed: $LASTEXITCODE" }
    & cl.exe /nologo /LD /O2 /W4 /std:c++17 /MT /EHsc /Fo:eu4_menu_patch.obj (Join-Path $projectRoot 'src\eu4_menu_patch.cpp') eu4_menu_patch.res /link /OUT:eu4_menu_patch.dll /IMPLIB:eu4_menu_patch.lib
    if ($LASTEXITCODE -ne 0) { throw "C++ build failed: $LASTEXITCODE" }
    & cl.exe /nologo /LD /O2 /W4 /std:c++17 /MT /EHsc /DEU4_MENU_PATCH_RESEARCH /Fo:menu_patch_probe.obj (Join-Path $projectRoot 'src\eu4_menu_patch.cpp') eu4_menu_patch.res /link /OUT:menu_patch_probe.dll /IMPLIB:menu_patch_probe.lib
    if ($LASTEXITCODE -ne 0) { throw "Research C++ build failed: $LASTEXITCODE" }
} finally { Pop-Location }
