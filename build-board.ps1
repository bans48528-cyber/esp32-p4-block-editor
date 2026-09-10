param([switch]$Flash, [string]$Port = 'COM10')
$ErrorActionPreference = 'Stop'
$sdkRoot = 'C:\esp\esp-idf-v5.4.2'
$buildRoot = 'C:\esp\board-editor'
if (!(Test-Path "$sdkRoot\export.ps1")) { throw "ESP-IDF not installed at $sdkRoot" }
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
foreach ($entry in @('main','components','CMakeLists.txt','sdkconfig.defaults','partitions.csv')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $entry) -Destination $buildRoot -Recurse -Force
}
. "$sdkRoot\export.ps1"
Push-Location $buildRoot
try {
    python "$sdkRoot\tools\idf.py" build
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    if ($Flash) {
        python "$sdkRoot\tools\idf.py" -p $Port flash
        if ($LASTEXITCODE -ne 0) { throw 'Flash failed' }
    }
} finally { Pop-Location }
