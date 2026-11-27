@echo off
title Build 天天打飞机 (C)
cd /d "%~dp0"

set "TCC=%~dp0_t\tools\tcc\tcc\tcc.exe"
if exist "%TCC%" (
    echo Building with TinyCC...
    "%TCC%" plane_shooter_gui.c -o "天天打飞机.exe" -luser32 -lgdi32 -lmsvcrt
) else (
    echo TinyCC not found, trying gcc...
    gcc plane_shooter_gui.c -o "天天打飞机.exe"
)

if errorlevel 1 (
    echo Build FAILED.
) else (
    echo Build OK: 天天打飞机.exe
)
pause
