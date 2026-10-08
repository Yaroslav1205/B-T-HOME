[CmdletBinding()]
param(
    [ValidateSet('build', 'flash', 'monitor', 'menuconfig', 'size', 'reconfigure')]
    [string]$Action = 'build',
    [string]$Port,
    [string]$IdfPath,
    [string]$Distribution = 'BTHMatter'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $projectRoot 'build\matter-esp32c6_rgb8'
if ($Action -in @('build', 'menuconfig', 'size', 'reconfigure')) {
    $buildTimer = [System.Diagnostics.Stopwatch]::StartNew()
    Write-Host "Matter action '$Action' in WSL distribution '$Distribution'..."
    Write-Host "Windows output: $buildDir"
    Push-Location -LiteralPath $projectRoot
    try {
        & wsl.exe -d $Distribution -- bash scripts/matter-wsl.sh $Action
        if ($LASTEXITCODE -ne 0) { throw 'Matter build failed. No firmware was flashed.' }
    } finally { Pop-Location }
    if ($Action -eq 'build') {
        Write-Host 'Matter build complete (WSL exited successfully; Windows artifacts ready).'
    }
    Write-Host ('Elapsed: {0:N1} s. Matter {1} finished.' -f $buildTimer.Elapsed.TotalSeconds, $Action)
    exit 0
}
if (-not $Port) { throw 'Specify -Port COMx.' }
foreach ($file in @('sdkconfig', 'bt_home.bin', 'bt_home.elf', 'flash_args',
                    'bootloader\bootloader.bin', 'partition_table\partition-table.bin')) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildDir $file))) {
        throw 'Build Matter first: scripts/matter.ps1 -Action build'
    }
}
if (-not (Select-String -LiteralPath (Join-Path $buildDir 'sdkconfig') -Pattern '^CONFIG_BTH_MATTER_ENABLED=y$' -Quiet)) {
    throw 'Refusing to use firmware without Matter enabled.'
}
if (-not (Select-String -LiteralPath (Join-Path $buildDir 'sdkconfig') -Pattern '^CONFIG_SUPPORT_ELECTRICAL_POWER_MEASUREMENT_CLUSTER=y$' -Quiet) -or
    -not (Select-String -LiteralPath (Join-Path $buildDir 'sdkconfig') -Pattern '^CONFIG_SUPPORT_POWER_TOPOLOGY_CLUSTER=y$' -Quiet) -or
    -not (Select-String -LiteralPath (Join-Path $buildDir 'sdkconfig') -Pattern '^CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=([7-9]|[1-9][0-9]+)$' -Quiet)) {
    throw 'Matter sdkconfig is stale. Update the Linux build sdkconfig to match sdkconfig.matter.defaults (including at least 7 endpoints), then rebuild and export the firmware.'
}
if (-not $IdfPath) { $IdfPath = $env:IDF_PATH }
if (-not $IdfPath) {
    foreach ($toolsRoot in @($env:IDF_TOOLS_PATH, (Join-Path $env:USERPROFILE '.espressif'), 'C:\Espressif')) {
        if (-not $toolsRoot) { continue }
        $registration = Join-Path $toolsRoot 'esp_idf.json'
        if (-not (Test-Path -LiteralPath $registration)) { continue }
        $installed = Get-Content -LiteralPath $registration -Raw | ConvertFrom-Json
        $selected = $installed.idfInstalled.PSObject.Properties[$installed.idfSelectedId]
        if ($null -ne $selected) { $IdfPath = $selected.Value.path; break }
    }
}
if (-not $IdfPath -or -not (Test-Path -LiteralPath (Join-Path $IdfPath 'export.ps1'))) {
    throw 'Activate the Windows ESP-IDF environment or pass -IdfPath.'
}
. (Join-Path $IdfPath 'export.ps1')
if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF activation failed.' }
Push-Location -LiteralPath $buildDir
try {
    if ($Action -eq 'flash') {
        # Only named firmware regions are written. No erase_flash or NVS erase.
        & python -m esptool --chip esp32c6 --port $Port --baud 460800 write_flash '@flash_args'
    } else {
        & python (Join-Path $IdfPath 'tools\idf_monitor.py') --port $Port --target esp32c6 --toolchain-prefix riscv32-esp-elf- bt_home.elf
    }
    if ($LASTEXITCODE -ne 0) { throw "Matter $Action failed." }
} finally { Pop-Location }
