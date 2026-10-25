$ErrorActionPreference = 'Stop'
$revision = '9981c6350dc58c0ce21f024fd65e31e851c31689'
$destination = Join-Path $PSScriptRoot '../deps/ngx/include'
New-Item -ItemType Directory -Force $destination | Out-Null
# SDK license: https://github.com/NVIDIA/DLSS/tree/9981c6350dc58c0ce21f024fd65e31e851c31689
$headers = @{
    'nvsdk_ngx.h' = 'DC38E7467CF415379C9D12AE1B6E4A494C453ED92720FB53E92AECB523E7B848'
    'nvsdk_ngx_defs.h' = 'EA23F33497CD274860D1C25A97644FCE807DCB0037C594547203343103FAD03E'
    'nvsdk_ngx_params.h' = '943BC8CC5CDAE03B6303016FBAD3183636F2335AE27A2D18776798C3B4EFABBC'
}
foreach ($name in $headers.Keys) {
    $path = Join-Path $destination $name
    Invoke-WebRequest "https://raw.githubusercontent.com/NVIDIA/DLSS/$revision/include/$name" -OutFile $path
    if ((Get-FileHash $path -Algorithm SHA256).Hash -ne $headers[$name]) {
        Remove-Item $path
        throw "Checksum mismatch: $name"
    }
}
$libraryDir = Join-Path $PSScriptRoot '../deps/ngx/lib'
New-Item -ItemType Directory -Force $libraryDir | Out-Null
$library = Join-Path $libraryDir 'nvsdk_ngx_s.lib'
Invoke-WebRequest "https://raw.githubusercontent.com/NVIDIA/DLSS/$revision/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib" -OutFile $library
if ((Get-FileHash $library -Algorithm SHA256).Hash -ne '7B9A5239C1145AF5F645AB472AA066F9EA79DB36AC819562312AAE69D65F3765') {
    Remove-Item $library
    throw 'NGX library checksum mismatch'
}
Write-Host 'NGX SDK files ready.'
