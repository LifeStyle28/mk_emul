# Download xPack arm-none-eabi-gcc into third_party/toolchain
param(
    [string]$Dest = (Join-Path $PSScriptRoot "..\third_party\toolchain")
)
$ErrorActionPreference = "Stop"
$ver = "13.3.1-1.1"
$url = "https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v$ver/xpack-arm-none-eabi-gcc-$ver-win32-x64.zip"
$zip = Join-Path $env:TEMP "arm-none-eabi-gcc.zip"
Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $zip
$extract = Join-Path $env:TEMP "arm-none-eabi-extract"
if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
Expand-Archive $zip -DestinationPath $extract
$inner = Get-ChildItem $extract -Directory | Select-Object -First 1
New-Item -ItemType Directory -Force -Path (Split-Path $Dest) | Out-Null
if (Test-Path $Dest) { Remove-Item $Dest -Recurse -Force }
Move-Item $inner.FullName $Dest
Write-Host "Installed toolchain to $Dest"
Write-Host "gcc: $(Join-Path $Dest 'bin\arm-none-eabi-gcc.exe')"
