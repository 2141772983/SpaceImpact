@echo off
setlocal
echo ===================================================
echo   Nokia 3310 Space Impact - One-Click MSVC Build
echo ===================================================

:: Check for MSVC environment
where cl >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo Setting up Visual Studio 2022 Developer Environment...
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)

echo Compiling SpaceImpact.cpp...
cl /EHsc /O2 /utf-8 /W3 /std:c++17 /Fe:SpaceImpact.exe SpaceImpact.cpp user32.lib gdi32.lib winmm.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ===================================================
    echo BUILD SUCCESSFUL! Run SpaceImpact.exe to play!
    echo ===================================================
) else (
    echo.
    echo ===================================================
    echo BUILD FAILED! Please check error output above.
    echo ===================================================
)
pause
