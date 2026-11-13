param([switch]$BuildOnly)

$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildScript = Join-Path $projectRoot 'build-board.ps1'

function Find-BoardPort {
    $boardPorts = @(Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match 'VID_303A&PID_1001' } |
        ForEach-Object {
            if ($_.FriendlyName -match '\((COM\d+)\)') {
                $Matches[1]
            }
        })

    if ($boardPorts.Count -eq 1) {
        return $boardPorts[0]
    }
    if ($boardPorts -contains 'COM10') {
        return 'COM10'
    }
    if ($boardPorts.Count -gt 1) {
        throw "Multiple Espressif boards detected: $($boardPorts -join ', '). Disconnect the extra boards and retry."
    }

    $available = @([System.IO.Ports.SerialPort]::GetPortNames())
    if ($available -contains 'COM10') {
        return 'COM10'
    }
    throw 'Board not found. Connect its USB cable, wait for the COM port, and retry.'
}

try {
    if ($BuildOnly) {
        Write-Host 'Checking the latest board source...' -ForegroundColor Cyan
        & $buildScript
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE."
        }
        Write-Host 'Build check passed. Flash was skipped.' -ForegroundColor Green
        exit 0
    }

    $port = Find-BoardPort
    Write-Host "Board detected on $port. Building and flashing the latest source..." -ForegroundColor Cyan
    & $buildScript -Flash -Port $port
    if ($LASTEXITCODE -ne 0) {
        throw "Flash failed with exit code $LASTEXITCODE."
    }

    Write-Host 'Flash completed. The board has restarted.' -ForegroundColor Green
    exit 0
} catch {
    Write-Host ''
    Write-Host "FAILED: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'If the COM port is busy, close the serial monitor and retry.' -ForegroundColor Yellow
    exit 1
}
