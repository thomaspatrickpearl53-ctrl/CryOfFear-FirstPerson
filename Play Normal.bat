@echo off
rem Switches Cry of Fear back to the original client and starts it through Steam.
tasklist | findstr /i /b "cof.exe CoFLaunchApp.exe" >nul && (
  echo Cry of Fear is running. Close it first, then run this again.
  pause
  exit /b 1
)
set FP_NOPAUSE=1
call "%~dp0uninstall.bat" || (pause & exit /b 1)
start "" steam://rungameid/223710
