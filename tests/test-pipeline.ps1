param(
    [ValidateSet('Release', 'Nightly')][string]$Channel = 'Release',
    [string]$BuildDate = ''
)
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools/build-common.ps1')
$info = Get-ReleaseInfo $Channel $BuildDate
$validator = Join-Path $projectRoot 'tools/test-package.ps1'
function Assert-Rejected([string]$ExpectedError) {
    try { & $validator -Channel $Channel -BuildDate $info.BuildDate }
    catch {
        if ($_.Exception.Message -notlike "*$ExpectedError*") { throw }
        Write-Output "Rejected as expected: $ExpectedError"
        return
    }
    throw "Invalid package was accepted: $ExpectedError"
}
Start-Transcript -Path (Join-Path $buildRoot 'pipeline-tests.log') -Force | Out-Null
try {
    $dll = Join-Path $buildRoot 'eu4_menu_patch.dll'
    $bytes = [IO.File]::ReadAllBytes($dll)
    try {
        $changed = $bytes.Clone()
        $changed[0] = $changed[0] -bxor 1
        [IO.File]::WriteAllBytes($dll, $changed)
        Assert-Rejected 'Build DLL hash mismatch.'
    } finally { [IO.File]::WriteAllBytes($dll, $bytes) }

    $validationPath = Join-Path $buildRoot 'automated-validation.json'
    $validationBytes = [IO.File]::ReadAllBytes($validationPath)
    try {
        $validation = Get-Content -LiteralPath $validationPath -Raw | ConvertFrom-Json
        $validation.source_commit = '0000000000000000000000000000000000000000'
        Write-Json $validationPath $validation
        Assert-Rejected 'Validation record does not match'
    } finally { [IO.File]::WriteAllBytes($validationPath, $validationBytes) }

    $zipPath = Join-Path $distRoot $info.PlayerPackage
    $zipBytes = [IO.File]::ReadAllBytes($zipPath)
    try {
        $archive = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Update)
        try { [void]$archive.CreateEntry('eu4.exe') } finally { $archive.Dispose() }
        Assert-Rejected 'Unexpected ZIP file list.'
    } finally { [IO.File]::WriteAllBytes($zipPath, $zipBytes) }

    $sumPath = Join-Path $distRoot 'SHA256SUMS.txt'
    $sumBytes = [IO.File]::ReadAllBytes($sumPath)
    try {
        [IO.File]::WriteAllText($sumPath, 'invalid checksum')
        Assert-Rejected 'SHA256SUMS does not match'
    } finally { [IO.File]::WriteAllBytes($sumPath, $sumBytes) }
    & $validator -Channel $Channel -BuildDate $info.BuildDate
    Write-Output 'Pipeline integrity tests passed.'
} finally { Stop-Transcript | Out-Null }
