@echo off
setlocal

if "%~1"=="" (
    echo Usage: OpenFpgaStream.bat ^<fpga-ip^> [player]
    echo Example: OpenFpgaStream.bat 192.168.1.50 edge
    exit /b 1
)

set FPGA_IP=%~1
set PLAYER=%~2

if "%PLAYER%"=="" set PLAYER=auto

powershell -ExecutionPolicy Bypass -File "%~dp0OpenFpgaStream.ps1" -FpgaIp "%FPGA_IP%" -Port 5969 -Player "%PLAYER%"
