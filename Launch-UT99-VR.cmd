@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-ut99-vr.ps1" %*
if errorlevel 1 (
    echo.
    echo UT99 VR did not start successfully. See the message above.
    pause
)
endlocal
