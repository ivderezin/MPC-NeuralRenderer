$ErrorActionPreference = 'Stop'
$revision = '9981c6350dc58c0ce21f024fd65e31e851c31689'
$destination = Join-Path $PSScriptRoot '../deps/ngx/include'
New-Item -ItemType Directory -Force $destination | Out-Null
# SDK license: https://github.com/NVIDIA/DLSS/tree/9981c6350dc58c0ce21f024fd65e31e851c31689
$headers = @{
    'nvsdk_ngx_helpers_dlssg_d3d.h' = '6335EBCB95DE39A361E87FE0257D6CE0723E5D8DEAA15DB3B35AFACF2B3F4220'
    'nvsdk_ngx_params_dlssg.h' = 'C24FA5AA68FFA2808C828B75CFA32843F1606F80A145F84055AC450D7E6C74AD'
    'nvsdk_ngx_defs_dlssg.h' = 'C29765AB9E0125F9D724AE9811CC2ABE03738D17486EC8EEE80D78A1504549E1'
    'nvsdk_ngx_helpers_d3d.h' = 'EC75224F36ED6580BAAF250FA83620405AD81A1D98C2C436654A0C9CF6A6B8BA'
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
