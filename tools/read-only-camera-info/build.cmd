@echo off
setlocal
set "T6_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%T6_VSWHERE%" (
  echo Visual Studio Build Tools with the C++ workload and Windows SDK are required.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%T6_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "T6_VSINSTALL=%%i"
if not defined T6_VSINSTALL exit /b 1
call "%T6_VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MD /DUNICODE /D_UNICODE /Fe:build\t6-camera-info.exe /Fo:build\camera_info.obj camera_info.cpp /link ole32.lib oleaut32.lib portabledeviceguids.lib
exit /b %errorlevel%
