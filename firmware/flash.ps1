param([string]$Port = 'COM10')
$ErrorActionPreference='Stop'
$python = 'C:\Users\64264\.espressif\python_env\idf5.4_py3.13_env\Scripts\python.exe'
Push-Location $PSScriptRoot
try {
    & $python -m esptool --chip esp32p4 --port $Port -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x2000 bootloader.bin 0x8000 partition-table.bin 0x10000 graphical_board_minimal.bin
    if($LASTEXITCODE -ne 0){throw 'Flash failed'}
} finally { Pop-Location }
