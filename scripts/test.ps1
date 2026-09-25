[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$testBuild = Join-Path $projectRoot 'build\host'

# CMake's Visual Studio generator finds the compiler without changing the shell.
& cmake -S (Join-Path $projectRoot 'tests\host') -B $testBuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $testBuild --config Debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ctest --test-dir $testBuild -C Debug --output-on-failure
exit $LASTEXITCODE
