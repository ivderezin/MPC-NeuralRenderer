$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$destination = Join-Path $root 'deps/vendors'
$manifest = Get-Content (Join-Path $PSScriptRoot 'vendor-sdks.json') -Raw | ConvertFrom-Json
foreach ($file in $manifest) {
    $path = Join-Path $destination $file.path
    if ((Test-Path $path) -and (Get-FileHash $path -Algorithm SHA256).Hash -eq $file.sha256) { continue }
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    $temporary = "$path.download"
    try {
        Invoke-WebRequest -Uri $file.url -OutFile $temporary
        if ((Get-FileHash $temporary -Algorithm SHA256).Hash -ne $file.sha256) {
            throw "SDK checksum mismatch: $($file.path)"
        }
        Move-Item -Force $temporary $path
    } finally {
        Remove-Item $temporary -Force -ErrorAction SilentlyContinue
    }
}
Write-Host "Pinned FSR and XeSS SDK files ready: $destination"
