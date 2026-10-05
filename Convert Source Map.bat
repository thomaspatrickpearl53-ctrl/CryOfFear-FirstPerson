@echo off
rem Drag a Source engine map (.bsp) onto this file to convert it into a Cry of Fear map.
rem The .bsp must still be inside its game's maps folder, so its textures can be found.
rem The result is installed as s_<name> into Cry of Fear (and C:\cof if that copy exists).
setlocal
if "%~1"=="" (
  echo Drag a Source map ^(.bsp^) onto this file.
  echo Example: Steam\steamapps\common\Counter-Strike Source\cstrike\maps\cs_italy.bsp
  pause
  exit /b 1
)
set "COFS=--cof "C:\Program Files (x86)\Steam\steamapps\common\Cry of Fear\cryoffear""
if exist "C:\cof\cryoffear\maps" set "COFS=%COFS% --cof "C:\cof\cryoffear""
for %%F in (%*) do (
  echo.
  echo ===== %%~nxF
  py "%~dp0tools\source2cof.py" "%%~fF" %COFS%
)
echo.
pause
