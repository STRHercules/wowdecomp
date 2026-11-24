@echo off
rem PC cabinets: ddraw.dll + version.dll (32-bit, beside the game's exe) and starcraft_libretro.dll (64-bit core).
rem   build.bat <out dir>
setlocal
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%~dp0out"
if not exist "%OUT%" mkdir "%OUT%"
set "VS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build"
set "SRC=%~dp0"

call "%VS%\vcvars32.bat" >nul || exit /b 1
pushd "%OUT%"
cl /nologo /O2 /MT /EHsc /std:c++17 /LD "%SRC%ddraw_bridge.cpp" "%SRC%net_bridge.cpp" /Fe:ddraw.dll /link /DEF:"%SRC%ddraw.def" user32.lib dxguid.lib ws2_32.lib || exit /b 1
cl /nologo /O2 /MT /LD "%SRC%version_loader.cpp" /Fe:version.dll /link /DEF:"%SRC%version.def" || exit /b 1
popd
endlocal & set "OUT=%OUT%" & set "SRC=%SRC%" & set "VS=%VS%"

call "%VS%\vcvars64.bat" >nul || exit /b 1
pushd "%OUT%"
cl /nologo /O2 /MT /EHsc /std:c++17 /LD "%SRC%starcraft_core.cpp" /Fe:starcraft_libretro.dll /link user32.lib || exit /b 1
popd
echo built in %OUT%
