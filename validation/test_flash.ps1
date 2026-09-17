$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot '../scripts/flash_all.ps1'
& $script -DryRun
try {
    & $script -DryRun -Port19 COM21
    throw 'Duplicate ports were accepted.'
} catch {
    if ($_.Exception.Message -notmatch 'different COM port') { throw }
}
$temp = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
try {
    New-Item -ItemType Directory -Path $temp | Out-Null
    Copy-Item (Join-Path $PSScriptRoot '../firmware/v1.0.0/*') $temp
    $file = Join-Path $temp '2dk_range_node22_v1.bin'
    $bytes = [IO.File]::ReadAllBytes($file)
    $bytes[100] = $bytes[100] -bxor 1
    [IO.File]::WriteAllBytes($file, $bytes)
    try {
        & $script -DryRun -BinDir $temp
        throw 'Corrupt firmware was accepted.'
    } catch {
        if ($_.Exception.Message -notmatch 'SHA256 mismatch') { throw }
    }
} finally { Remove-Item $temp -Recurse -Force }
Write-Host 'PASS: complete preflight, duplicate port rejection, corrupt BIN rejection.'
