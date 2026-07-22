@echo off
setlocal
"%~dp0SurrealEngine.exe"
if errorlevel 1 (
    echo.
    echo Surreal Engine did not start successfully. See the message above.
    pause
)
endlocal
