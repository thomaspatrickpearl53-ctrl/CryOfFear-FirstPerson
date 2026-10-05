@echo off
rem Switches Cry of Fear to the modded client and starts it through Steam.
tasklist /fi "imagename eq cof.exe" | find /i "cof.exe" >nul && (
  echo Cry of Fear is running. Close it first, then run this again.
  pause
  exit /b 1
)
set FP_NOPAUSE=1
call "%~dp0install.bat" || (pause & exit /b 1)
start "" steam://rungameid/223710
