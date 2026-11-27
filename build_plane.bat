@echo off
chcp 65001 >nul
title Build Plane Shooter (C)
cd /d "%~dp0"

set "TCC=%~dp0_t\tools\tcc\tcc\tcc.exe"
if exist "%TCC%" (
    echo Building with TinyCC...
    "%TCC%" plane_shooter.c -o plane_shooter.exe -luser32
) else (
    echo TinyCC not found, trying gcc...
    gcc plane_shooter.c -o plane_shooter.exe
)

if errorlevel 1 (
    echo Build FAILED.
) else (
    echo Build OK: plane_shooter.exe
)
pause
