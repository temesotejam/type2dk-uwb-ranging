[CmdletBinding()]
param(
    [string]$Programmer = 'C:\NXP\DK6ProductionFlashProgrammer\DK6Programmer.exe',
    [string]$BinDir = '',
    [ValidatePattern('^COM[0-9]+$')][string]$Port19 = 'COM19',
    [ValidatePattern('^COM[0-9]+$')][string]$Port21 = 'COM21',
    [ValidatePattern('^COM[0-9]+$')][string]$Port22 = 'COM22',
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $BinDir) {
    if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'manifest.json')) {
        $BinDir = $PSScriptRoot
    } else {
        $BinDir = Join-Path $PSScriptRoot '../firmware/v1.0.0'
    }
}
$BinDir = (Resolve-Path -LiteralPath $BinDir).Path
$ports = @($Port19.ToUpperInvariant(), $Port21.ToUpperInvariant(), $Port22.ToUpperInvariant())
if (@($ports | Select-Object -Unique).Count -ne 3) { throw 'Each node requires a different COM port.' }
$nodes = @(19, 21, 22)
$manifest = Get-Content -LiteralPath (Join-Path $BinDir 'manifest.json') -Raw | ConvertFrom-Json
if (@($manifest.binaries).Count -ne 3) { throw 'Expected exactly three firmware entries.' }
$files = @()
# Validate every file before starting the first write.
foreach ($node in $nodes) {
    $entries = @($manifest.binaries | Where-Object { $_.node -eq $node })
    if ($entries.Count -ne 1) { throw "Expected exactly one entry for node $node." }
    $entry = $entries[0]
    if ($entry.file -notmatch "^2dk_range_node${node}(_v[0-9._-]+)?\.bin$") { throw "Wrong filename for node $node." }
    $file = Join-Path $BinDir $entry.file
    if ((Get-Item -LiteralPath $file).Length -ne $entry.bytes) { throw "Size mismatch: $file" }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "SHA256 mismatch: $file"
    }
    $files += $file
}
if (-not $DryRun -and -not (Test-Path -LiteralPath $Programmer -PathType Leaf)) {
    throw "DK6Programmer not found: $Programmer"
}
for ($i = 0; $i -lt 3; $i++) {
    Write-Host "Node $($nodes[$i]) -> $($ports[$i]): $($files[$i])"
}
if ($DryRun) { Write-Host 'Dry run passed. No boards were written.'; return }
for ($i = 0; $i -lt 3; $i++) {
    & $Programmer -V 0 -P 1000000 -s $ports[$i] -Y -p $files[$i]
    if ($LASTEXITCODE -ne 0) { throw "Programming failed on $($ports[$i]); exit $LASTEXITCODE. Stopped." }
}
Write-Host 'Programming completed. Power-cycle the boards. Read COM19 at 3000000 bps, 8N1, no flow control.'
