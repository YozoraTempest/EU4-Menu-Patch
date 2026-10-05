param(
    [ValidateSet('Release', 'Nightly')][string]$Channel = 'Release',
    [string]$BuildDate = '',
    [switch]$WriteNotes
)
. (Join-Path $PSScriptRoot 'build-common.ps1')
$info = Get-ReleaseInfo $Channel $BuildDate
if (!$WriteNotes) {
    Write-Output (@($info.Version, $info.Channel, $info.Tag, $info.SourceCommit, $info.BuildDate, $info.PlayerPackage) -join '|')
    exit 0
}
$updates = ''
if ($Channel -eq 'Release') {
    $changelog = [IO.File]::ReadAllText((Join-Path $projectRoot 'CHANGELOG.md'))
    $section = [regex]::Match($changelog, '(?ms)^## ' + [regex]::Escape($info.Version) + '\s*\r?\n(.*?)(?=^## |\z)')
    if (!$section.Success -or !$section.Groups[1].Value.Trim()) { throw 'Add this version to CHANGELOG.md before releasing.' }
    $updates = $section.Groups[1].Value.Trim()
} else {
    $updates = "Nightly 测试版，构建日期：$($info.BuildDate)（北京时间）。"
}
$repo = if ($env:GITHUB_REPOSITORY) { $env:GITHUB_REPOSITORY } else { 'YozoraTempest/EU4-Menu-Patch' }
$notes = @"
$updates

适用：EU4 1.37.5.0 Inca / Windows x64。

安装：完全退出游戏，将成品 ZIP 中的 plugins 文件夹解压到 eu4.exe 所在目录并覆盖。需要已有 Matanki EU4dll 的 VERSION.dll 加载器，补丁包不附带加载器。

检查：自动构建通过加载保护和成品完整性检查。本构建没有新的游戏内验证记录；联机战役同步、界面清理及模组兼容性不能由 CI 确认。

源码提交：[$($info.SourceCommit.Substring(0,7))](https://github.com/$repo/commit/$($info.SourceCommit))

作者：VulonLok · [QQ 交流群](https://qm.qq.com/q/Csnqqd8rUO)
"@
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $buildRoot 'release-notes.md'), $notes + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
