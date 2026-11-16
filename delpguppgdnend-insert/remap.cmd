@echo off
cd /d "%~dp0"
if not exist remap.exe (
    echo remap.exe not found - run build.cmd first
    pause
    exit /b 1
)

:menu
echo.
echo   Which keys should send INSERT?
echo.
echo     1 = Del        2 = PgUp        3 = PgDn
echo     4 = End        5 = all four
echo.
choice /c 12345 /n /m "  choice > "
if %errorlevel%==1 set "ARGS=del"
if %errorlevel%==2 set "ARGS=pgup"
if %errorlevel%==3 set "ARGS=pgdn"
if %errorlevel%==4 set "ARGS=end"
if %errorlevel%==5 set "ARGS=del pgup pgdn end"
if not defined ARGS goto menu

echo.
echo   remapping system-wide. Ctrl+C stops. If the target app runs as
echo   administrator, close this and run remap.cmd as administrator.
echo.
remap.exe %ARGS%
