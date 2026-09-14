# Read-only dump + verification of a TLSR8258 (Tuya ZTU) over a CP2102 SWire link.
# Proven settings: 921600 baud, 64-byte chunks, no per-read sleep, keep partial reads.
# Usage: .\tools\run_dumps.ps1 -Port COMx -Name unit2 [-Wait 180] [-SkipVerify "0x8000-0x5C000"]
#   -Port        serial port of the USB-UART adapter
#   -Wait        seconds to wait for a manual RST->GND reset (0 if the chip is already halted)
#   -SkipVerify  range(s) proven by other means (e.g. the app image CRC) to skip in the re-read pass
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [string]$Name = "ztu_stock",
    [int]$Wait = 180,
    [string[]]$SkipVerify = @()
)
$root = Split-Path -Parent $PSScriptRoot
$tcsw = Join-Path $PSScriptRoot "tcsw"
$dumps = Join-Path $root "dumps"
$out = Join-Path $dumps "${Name}_dump.bin"
$verified = Join-Path $dumps "${Name}_verified.bin"
Set-Location $tcsw

"=== dump start $(Get-Date -Format T)"
$ok = $false
foreach ($try in 1..6) {
    $extra = @("--chunk", "0x40", "--no-sleep", "--partial")
    if ($try -eq 1 -and $Wait -gt 0) { $extra += @("--wait", "$Wait") }
    if ($try -gt 1) { $extra += "--resume" }
    python -u dump_flash.py $Port $out @extra 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -eq 0) { $ok = $true; break }
    "--- attempt $try failed, resuming"
    Start-Sleep -Seconds 2
}
if (-not $ok) { "=== DUMP FAILED"; exit 1 }
"=== dump done $(Get-Date -Format T)"

$skip = @()
foreach ($r in $SkipVerify) { $skip += @("--skip", $r) }
python -u verify_dump.py $Port $out --out $verified @skip 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0) { "=== VERIFY FAILED"; exit 1 }

"=== SHA256"
Get-FileHash $out, $verified -Algorithm SHA256 | ForEach-Object { "$($_.Hash)  $(Split-Path $_.Path -Leaf)" }
"=== ALL DONE $(Get-Date -Format T)"
