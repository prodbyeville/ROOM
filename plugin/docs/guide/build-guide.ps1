# Prints the ROOM guide (plugin\docs\guide\guide.html) to plugin\dist\ROOM-Guide.pdf with Microsoft Edge.
#   powershell -ExecutionPolicy Bypass -File plugin\docs\guide\build-guide.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw "Microsoft Edge not found" }
$html = (Join-Path $PSScriptRoot "guide.html") -replace '\\', '/'
$pdf = Join-Path $root "dist\ROOM-Guide.pdf"
New-Item -ItemType Directory -Force (Split-Path $pdf) | Out-Null
if (Test-Path $pdf) { Remove-Item $pdf }
$profile = Join-Path $env:TEMP "room-guide-edge"
& $edge --headless=new --disable-gpu --no-pdf-header-footer --allow-file-access-from-files --virtual-time-budget=8000 `
        "--user-data-dir=$profile" "--print-to-pdf=$pdf" "file:///$html" | Out-Null
$deadline = (Get-Date).AddSeconds(60)
while (-not (Test-Path $pdf) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (-not (Test-Path $pdf)) { throw "Edge didn't write the PDF" }
Write-Output "Guide: $pdf ($([Math]::Round((Get-Item $pdf).Length / 1MB, 1)) MB)"
