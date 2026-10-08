[CmdletBinding()]
param(
    [ValidateSet('build', 'flash', 'monitor', 'menuconfig', 'size', 'reconfigure')]
    [string]$Action = 'build',
    [ValidatePattern('^[a-z][a-z0-9_]*$')]
    [string]$Board = 'esp32c6_rgb8',
    [string]$Port,
    [string]$IdfPath,
    [switch]$WifiAp,
    [ValidatePattern('^[D-Zd-z]$')]
    [string]$Drive = 'R'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ($Board -eq 'esp32c6_rgb8' -and -not $WifiAp) {
    & (Join-Path $PSScriptRoot 'matter.ps1') -Action $Action -Port $Port -IdfPath $IdfPath
    exit $LASTEXITCODE
}
$profilePath = Join-Path $projectRoot "boards\$Board\board.json"
if (-not (Test-Path -LiteralPath $profilePath)) {
    throw "Unknown board '$Board'. See boards/."
}
$profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
if (($Action -eq 'flash' -or $Action -eq 'monitor') -and -not $Port) {
    throw 'Specify -Port COMx for flash or monitor.'
}

if (-not $IdfPath) { $IdfPath = $env:IDF_PATH }
if (-not $IdfPath) {
    # Discover the Windows Espressif installer registration, without installing anything.
    $toolsRoots = @($env:IDF_TOOLS_PATH, (Join-Path $env:USERPROFILE '.espressif'), 'C:\Espressif')
    foreach ($toolsRoot in $toolsRoots) {
        if (-not $toolsRoot) { continue }
        $registration = Join-Path $toolsRoot 'esp_idf.json'
        if (-not (Test-Path -LiteralPath $registration)) { continue }
        $installed = Get-Content -LiteralPath $registration -Raw | ConvertFrom-Json
        $selected = $installed.idfInstalled.PSObject.Properties[$installed.idfSelectedId]
        if ($null -ne $selected) {
            $IdfPath = $selected.Value.path
            break
        }
    }
}
if (-not $IdfPath -or -not (Test-Path -LiteralPath (Join-Path $IdfPath 'export.ps1'))) {
    throw 'Activate ESP-IDF 5.4 first or pass -IdfPath <installation path>.'
}

# Keep the same letter between builds: CMake caches absolute paths.
# Never replace an existing disk/mapping, and release only the mapping we create.
$driveName = $Drive.ToUpperInvariant() + ':'
$mapped = $false
$pushed = $false
$previousTarget = $env:IDF_TARGET
$previousPort = $env:ESPPORT
$result = 1
try {
    . (Join-Path $IdfPath 'export.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF activation failed.' }
    $env:IDF_TARGET = $profile.target
    if ($Port) { $env:ESPPORT = $Port }
    $buildRoot = $projectRoot
    if ($projectRoot -match '\s') {
        if (Test-Path -LiteralPath ($driveName + '\')) {
            throw "Drive $driveName is already in use. Choose another letter with -Drive."
        }
        & subst.exe $driveName $projectRoot
        if ($LASTEXITCODE -ne 0) { throw "Cannot map $driveName to the project directory." }
        $mapped = $true
        $buildRoot = $driveName + '\'
    }
    Push-Location -LiteralPath $buildRoot
    $pushed = $true
    # idf.py resolves SUBST back to the original path. Invoke the supported CMake
    # frontend directly so compiler/linker paths retain the space-free alias.
    $buildDir = Join-Path $buildRoot "build\$Board"
    $pythonPath = (Get-Command python -ErrorAction Stop).Source
    & cmake -S $buildRoot -B $buildDir -G Ninja "-DBTH_BOARD=$Board" `
        "-DPYTHON=$pythonPath" '-DPYTHON_DEPS_CHECKED=1' '-DCCACHE_ENABLE=0' `
        '-DBTH_WINDOWS_MATTER_BRIDGE=OFF' '-DBTH_WITH_MATTER=OFF'
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    if ($Action -eq 'reconfigure') {
        $result = 0
    }
    else {
        $target = if ($Action -eq 'build') { 'all' } else { $Action }
        & cmake --build $buildDir --target $target
        $result = $LASTEXITCODE
    }
}
finally {
    if ($pushed) { Pop-Location }
    if ($mapped) {
        & subst.exe $driveName /D
        if ($LASTEXITCODE -ne 0) { Write-Warning "Could not release temporary mapping $driveName" }
    }
    $env:IDF_TARGET = $previousTarget
    $env:ESPPORT = $previousPort
}
exit $result
