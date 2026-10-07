param(
    [string] $Destination = (Join-Path $PSScriptRoot '..\.deps\ctags'),
    [switch] $Force,
    [switch] $IncludeSource
)

$manifest = Get-Content (Join-Path $PSScriptRoot '..\third_party\universal-ctags-6.2.1.json') -Raw |
    ConvertFrom-Json
$archive = Join-Path $Destination (Split-Path $manifest.binary_url -Leaf)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
if ($Force -or -not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri $manifest.binary_url -OutFile $archive
}
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $manifest.sha256) {
    Remove-Item -LiteralPath $archive -Force
    throw "Universal Ctags archive hash mismatch: $actual"
}
Write-Output (Resolve-Path -LiteralPath $archive).Path
if ($IncludeSource) {
    $source = Join-Path $Destination 'ctags-v6.2.1-source.tar.gz'
    if ($Force -or -not (Test-Path -LiteralPath $source)) {
        Invoke-WebRequest -Uri $manifest.source_url -OutFile $source
    }
    Write-Output (Resolve-Path -LiteralPath $source).Path
}
